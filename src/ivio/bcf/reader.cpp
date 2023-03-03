#include "reader.h"

#include "../bgzf_reader.h"
#include "../bgzf_mt_reader.h"
#include "../buffered_reader.h"
#include "../file_reader.h"
#include "../mmap_reader.h"
#include "../stream_reader.h"
#include "../zlib_file_reader.h"
#include "../zlib_mmap2_reader.h"

#include <cassert>
#include <charconv>
#include <functional>
#include <optional>
#include <ranges>

namespace ivio {

namespace {
// helper type for the visitor #4
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };

struct bcf_buffer {
    char const* iter{};
    char const* end{};

    auto readDescriptor() -> std::tuple<uint8_t, uint8_t>  {
        auto v = ivio::bgzfUnpack<uint8_t>(iter);
        iter += 1;
        auto l = uint8_t(v >> 4);
        auto t = uint8_t(v & 0x0f);
        return std::make_tuple(t, l);
    }

    static auto bcf_type(int t) -> std::string {
        if (t == 0) return "MISSING";
        if (t == 1) return "Integer [8 bit]";
        if (t == 2) return "Integer [16 bit]";
        if (t == 3) return "Integer [32 bit]";
        if (t == 5) return "Float [32 bit]";
        if (t == 7) return "Character";
        throw std::runtime_error("unknown type: " + std::to_string(t));
    }

    template <typename T>
    auto readIntOfType() -> T {
        static_assert(std::same_as<int8_t, T> or std::same_as<int16_t, T> or std::same_as<int32_t, T>
                      or std::same_as<uint8_t, T> or std::same_as<uint16_t, T> or std::same_as<uint32_t, T>,
                      "Can only read (usigned) ints of 8, 16 and 32 bit length");
        auto value = ivio::bgzfUnpack<T>(iter);
        iter += sizeof(T);
        return value;
    }

    auto readIntOfType(int t) -> int32_t {
        if (t == 1) return readIntOfType<int8_t>();
        if (t == 2) return readIntOfType<int16_t>();
        if (t == 3) return readIntOfType<int32_t>();
        throw std::runtime_error{"BCF error, expected an int, but got " + bcf_type(t)};
    }
    auto readInt() -> int32_t {
        auto [t, l] = readDescriptor();
        if (l != 1) throw std::runtime_error{"BCF error, expected length 1"};
        return readIntOfType(t);
    }

    auto readFloat() -> std::optional<float> {
       auto q     = ivio::bgzfUnpack<float>(iter);
       iter += 4;
       if (q == 0b0111'1111'1000'0000'0000'0000'0001) return std::nullopt;
       return {q};
    }

    auto readString() -> std::string_view {
        auto [t, l] = readDescriptor();
        if (t != 7) {
            throw std::runtime_error("BCF error: unexpected type: " + bcf_type(t));
        }
        if (l == 15) {
           l = readInt();
        }
        auto value = std::string_view{iter, iter+l};
        iter += l;
        return value;
    }

    auto readVectorOfInt() -> std::vector<int32_t> {
        auto [t, l] = readDescriptor();
        if (t != 1 and t != 2 and t != 3) {
            throw std::runtime_error("BCF error: unexpected type: " + bcf_type(t));
        }
        if (l == 15) {
            l = readInt();
        }
        auto res = std::vector<int32_t>{};
        res.resize(l);
        for (auto& v : res) {
            v = readIntOfType(t);
        }
        return res;
    }

    auto readVectorOfFloat() -> std::vector<float> {
        auto [t, l] = readDescriptor();
        if (t != 5) {
            throw std::runtime_error("BCF error: unexpected type: " + bcf_type(t));
        }
        if (l == 15) {
            l = readInt();
        }
        auto res = std::vector<float>{};
        res.resize(l);
        for (auto& v : res) {
            v = readFloat().value();
        }
        return res;
    }

    auto readAny() -> std::variant<nullptr_t, int32_t, float, char, std::string_view, std::vector<int32_t>, std::vector<float>> {
        auto [t, l] = readDescriptor();
        if (t == 0) return nullptr;
        if (l == 1 && 0 < t and t < 4) return readIntOfType(t);
        if (l == 1 && t == 5) return readFloat().value();
        if (l == 1 && t == 7) return readIntOfType(1);
        if (l > 1 && 0 < t and t < 4) {
            iter -= 1;
            return readVectorOfInt();
        }
        if (l > 1 && t == 5) {
            iter -= 1;
            return readVectorOfFloat();
        }
        if (l > 1 && t == 7) {
            iter -= 1;
            return readString();
        }
        throw std::runtime_error{"BCF error: unknown type/length combination - " + bcf_type(t) + "/" + std::to_string(l)};
    }

    template <typename CB>
    auto capture(CB cb) -> std::span<uint8_t const> {
        auto begin = iter;
        cb();
        return {reinterpret_cast<uint8_t const*>(begin), reinterpret_cast<uint8_t const*>(iter)};
    }
};
}


template <>
struct reader_base<bcf::reader>::pimpl {
    VarBufferedReader ureader;
    size_t lastUsed{};

    std::vector<std::tuple<std::string, std::string>> header;
    std::vector<std::string> genotypes;

    pimpl(std::filesystem::path file, size_t threadNbr)
        : ureader {[&]() -> VarBufferedReader {
            if (threadNbr == 0) {
                return buffered_reader<1<<16>{bgzf_reader{mmap_reader{file}}};
            }
            return bgzf_mt_reader{mmap_reader{file}, threadNbr};
        }()}
    {}
    pimpl(std::istream& file, size_t threadNbr)
        : ureader {[&]() -> VarBufferedReader {
            if (threadNbr == 0) {
                return buffered_reader<1<<16>{bgzf_reader{stream_reader{file}}};
            }
            return bgzf_mt_reader{stream_reader{file}, threadNbr};
        }()}
    {}

    bool readHeaderLine() {
        auto [buffer, size] = ureader.read(2);
        if (size >= 2 and buffer[0] == '#' and buffer[1] == '#') {
            auto start = 2;
            auto mid = ureader.readUntil('=', start);
            if (ureader.eof(mid)) return false;
            auto end = ureader.readUntil('\n', mid+1);
            header.emplace_back(ureader.string_view(start, mid), ureader.string_view(mid+1, end));
            if (ureader.eof(end)) return false;
            ureader.dropUntil(end+1);
            return true;
        }
        return false;
    }

    void readHeader() {
        auto [ptr, size] = ureader.read(9);

        if (size < 9) throw std::runtime_error{"something went wrong reading bcf file (1)"};

        size_t txt_len = ivio::bgzfUnpack<uint32_t>(ptr + 5);
        ureader.dropUntil(9);
        std::tie(ptr, size) = ureader.read(txt_len); // read complete header

        while (readHeaderLine()) {};

        size_t s{};
        for (auto [key, value] : header) {
            s += key.size() + value.size() + 4;
        }

        std::tie(ptr, size) = ureader.read(txt_len - s);
        if (size < 1 or ptr[0] != '#') {
            throw std::runtime_error("faulty bcf header");
        }

        auto tableHeader = std::string_view{ptr, txt_len-s};
        for (auto v : std::views::split(tableHeader, '\t')) {
            genotypes.emplace_back(v.begin(), v.end());
        }
        if (genotypes.size() < 9) {
            throw std::runtime_error("Header description line is invalid");
        }
        genotypes.erase(begin(genotypes), begin(genotypes)+9);
        ureader.dropUntil(txt_len-s);
    }


    auto next() -> std::optional<bcf::record_view> {
        ureader.dropUntil(lastUsed);
        auto [ptr, size] = ureader.read(8);
        if (size == 0) return std::nullopt;
        if (size < 8) throw std::runtime_error{"something went wrong reading bcf file (2)"};

        auto l_shared = ivio::bgzfUnpack<uint32_t>(ptr+0);
        auto l_indiv  = ivio::bgzfUnpack<uint32_t>(ptr+4);
        auto flen = l_shared + l_indiv + 8;
        std::tie(ptr, size) = ureader.read(flen);
        if (size < flen) throw std::runtime_error{"something went wrong reading bcf file (3)"};
        if (size < 32+3) throw std::runtime_error{"something went worng reading bcf file (4)"};

        auto buffer = bcf_buffer{ptr+8, ptr+flen};

        auto chromId = buffer.readIntOfType<int32_t>();
        if (chromId < 0) throw std::runtime_error{"chromId is invalid, negative values not allowed"};

        auto pos      = buffer.readIntOfType<int32_t>();
        auto rlen     = buffer.readIntOfType<int32_t>();
        auto qual     = buffer.readFloat();
        auto n_info   = buffer.readIntOfType<uint16_t>();
        auto n_allele = buffer.readIntOfType<uint16_t>();
        if (n_allele == 0) throw std::runtime_error{"n_allele is zero, it must be at least 1"};
        auto n_sample = ivio::bgzfUnpack<uint32_t>(buffer.iter) & 0x00ffffff;
        auto n_fmt    = ivio::bgzfUnpack<uint8_t>(buffer.iter+3);
        buffer.iter += 4;

        auto id       = buffer.readString();
        auto ref      = buffer.readString();

        auto alt = buffer.capture([&]() {
            for (size_t i{1}; i < size_t(n_allele); ++i) {
                buffer.readString();
            }
        });

        auto filter = buffer.capture([&]() {
            buffer.readVectorOfInt();
        });

        auto info = buffer.capture([&]() {
            for (size_t i{0}; i < size_t{n_info}; ++i) {
                /*auto id = */buffer.readInt();
                auto a = buffer.readAny();
                std::visit(overloaded{
                    [](nullptr_t) {},
                    [](int32_t v) {},
                    [](float v) {},
                    [](char v) {},
                    [](std::vector<int32_t> const& v) {},
                    [](std::vector<float> const& v) {},
                    [](std::string_view v) {}
                }, a);
            }
        });
        auto format = buffer.capture([&]() {
            for (size_t i{0}; i < size_t{n_fmt}; ++i) {
                /*auto id = */buffer.readInt();
                auto [t, l] = buffer.readDescriptor();
                buffer.iter += l*t*n_sample; // Jump over the data
            }
        });

        assert(buffer.iter == buffer.end);
        lastUsed = l_shared + l_indiv + 8;

        auto r = bcf::record_view {
            .chromId    = chromId,
            .pos        = pos,
            .rlen       = rlen,
            .qual       = qual,
            .n_info     = n_info,
            .n_allele   = n_allele,
            .n_sample   = n_sample,
            .n_fmt      = n_fmt,
            .id         = id,
            .ref        = ref,
            .alt        = alt,
            .filter     = filter,
            .info       = info,
            .format     = format,
        };
        return r;
    }
};
}

namespace ivio::bcf {

reader::reader(config const& config_)
    : reader_base{std::visit([&](auto& p) {
        return std::make_unique<pimpl>(p, config_.threadNbr);
    }, config_.input)}
{
    pimpl_->readHeader();
    header_.table     = std::move(pimpl_->header);
    header_.genotypes = std::move(pimpl_->genotypes);
}

reader::~reader() = default;

auto reader::next() -> std::optional<record_view> {
    assert(pimpl_);
    return pimpl_->next();
}

void reader::close() {
    pimpl_.reset();
}

static_assert(record_reader_c<reader>);

}
