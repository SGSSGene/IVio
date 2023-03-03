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
struct reader_base<sam::reader>::pimpl {
    VarBufferedReader ureader;
    size_t lastUsed{};

    std::vector<std::string> header;

    pimpl(std::filesystem::path file)
        : ureader {[&]() -> VarBufferedReader {
            return mmap_reader{file.c_str()};
        }()}
    {}
    pimpl(std::istream& file)
        : ureader {[&]() -> VarBufferedReader {
            return stream_reader{file};
        }()}
    {}

    bool readHeaderLine() {
        auto [buffer, size] = ureader.read(1);
        if (size >= 1 and buffer[0] == '@') {
            auto end = ureader.readUntil('\n', 0);
            if (ureader.eof(end)) return false;
            ureader.dropUntil(end+1);
            return true;
        }
        return false;
    }

    void readHeader() {
        while (readHeaderLine()) {} // read complete header
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

namespace ivio::sam {

reader::reader(config const& config_)
    : reader_base{std::visit([&](auto& p) {
        return std::make_unique<pimpl>(p);
    }, config_.input)}
{
    pimpl_->readHeader();
}


reader::~reader() = default;

auto reader::next() -> std::optional<record_view> {
    assert(pimpl_);

    auto& ureader  = pimpl_->ureader;
    auto& lastUsed = pimpl_->lastUsed;

    if (ureader.eof(lastUsed)) return std::nullopt;
    ureader.dropUntil(lastUsed);

    auto res = pimpl_->readLine<11, '\t'>();
    if (!res) return std::nullopt;

    auto [qname, flag, rname, pos, mapq, cigar, rnext, pnext, tlen, seq, qual] = *res;

    return record_view {.qname = qname,
                        .flag  = convertTo<int32_t>(flag),
                        .rname = rname,
                        .pos   = convertTo<int32_t>(pos),
                        .mapq  = convertTo<int32_t>(mapq),
                        .cigar = cigar,
                        .rnext = rnext,
                        .pnext = convertTo<int32_t>(pnext),
                        .tlen  = convertTo<int32_t>(tlen),
                        .seq   = seq,
                        .qual  = qual,
                      };
}

void reader::close() {
    pimpl_.reset();
}

static_assert(record_reader_c<reader>);

}
