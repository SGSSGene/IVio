#include "reader.h"

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

namespace {
template <typename T>
auto convertTo(std::string_view view) {
    T value{}; //!Should not be initialized with {}, but gcc warns...
    auto result = std::from_chars(begin(view), end(view), value);
    if (result.ec == std::errc::invalid_argument) {
        throw "can't convert to int";
    }
    return value;
}
}

namespace ivio {

template <>
struct reader_base<vcf::reader>::pimpl {
    VarBufferedReader ureader;
    size_t lastUsed{};

    std::vector<std::tuple<std::string, std::string>> header;
    std::vector<std::string> genotypes;

    pimpl(std::filesystem::path file, bool)
        : ureader {[&]() -> VarBufferedReader {
            if (file.extension() == ".vcf") {
                return mmap_reader{file.c_str()};
            } else if (file.extension() == ".gz") {
                return zlib_reader{mmap_reader{file.c_str()}};
            }
            throw std::runtime_error("unknown file extension");
        }()}
    {}
    pimpl(std::istream& file, bool compressed)
        : ureader {[&]() -> VarBufferedReader {
            if (!compressed) {
                return stream_reader{file};
            } else {
                return zlib_reader{stream_reader{file}};
            }
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
        while (readHeaderLine()) {}
        auto [buffer, size] = ureader.read(1);
        if (size >= 1 and buffer[0] == '#') {
            auto start = 1;
            auto end = ureader.readUntil('\n', start);
            auto tableHeader = ureader.string_view(start, end);
            for (auto v : std::views::split(tableHeader, '\t')) {
                genotypes.emplace_back(v.begin(), v.end());
            }
            if (genotypes.size() < 9) {
                throw std::runtime_error("Header description line is invalid");
            }
            genotypes.erase(begin(genotypes), begin(genotypes)+9);
            ureader.dropUntil(end);
            if (!ureader.eof(end)) ureader.dropUntil(1);
        }
    }

    template <size_t ct, char sep>
    auto readLine() -> std::optional<std::array<std::string_view, ct>> {
        auto res = std::array<std::string_view, ct>{};
        size_t start{};
        for (size_t i{}; i < ct-1; ++i) {
            auto end = ureader.readUntil(sep, start);
            if (ureader.eof(end)) return std::nullopt;
            res[i] = ureader.string_view(start, end);
            start = end+1;
        }
        auto end = ureader.readUntil('\n', start);
        if (ureader.eof(end)) return std::nullopt;
        res.back() = ureader.string_view(start, end);
        lastUsed = end;
        if (!ureader.eof(lastUsed)) lastUsed += 1;
        return res;
    }
};
}

namespace ivio::vcf {

reader::reader(config const& config_)
    : reader_base{std::visit([&](auto& p) {
        return std::make_unique<pimpl>(p, config_.compressed);
    }, config_.input)}
{
    pimpl_->readHeader();
    header_.table     = std::move(pimpl_->header);
    header_.genotypes = std::move(pimpl_->genotypes);
}


reader::~reader() = default;

auto reader::next() -> std::optional<record_view> {
    assert(pimpl_);

    auto& ureader  = pimpl_->ureader;
    auto& lastUsed = pimpl_->lastUsed;

    if (ureader.eof(lastUsed)) return std::nullopt;
    ureader.dropUntil(lastUsed);

    auto res = pimpl_->readLine<10, '\t'>();
    if (!res) return std::nullopt;

    auto [chrom, pos, id, ref, alts, qual, filters, infos, formats, samples] = *res;

    return record_view {
        .chrom   = chrom,
        .pos     = convertTo<int32_t>(pos),
        .id      = id,
        .ref     = ref,
        .alts    = alts,
        .qual    = convertTo<float>(qual),
        .filters = filters,
        .infos   = infos,
        .formats = formats,
        .samples = samples,
    };
}

void reader::close() {
    pimpl_.reset();
}

static_assert(record_reader_c<reader>);

}
