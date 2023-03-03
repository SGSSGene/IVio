#include "reader.h"

#include "../buffered_reader.h"
#include "../file_reader.h"
#include "../mmap_reader.h"
#include "../stream_reader.h"
#include "../zlib_file_reader.h"
#include "../zlib_mmap2_reader.h"
#include "../zlib_ng_file_reader.h"

static_assert(std::ranges::range<ivio::fasta::reader>, "reader must be a range (unittest)");
static_assert(ivio::record_reader_c<ivio::fasta::reader>, "must fulfill the record_reader concept (unittest)");

namespace ivio {

template <>
struct reader_base<fasta::reader>::pimpl {
    VarBufferedReader ureader;
    size_t lastUsed{};
    std::string s;

    pimpl(std::filesystem::path file, bool)
        : ureader {[&]() -> VarBufferedReader {
            if (file.extension() == ".fa") {
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
};
}

namespace ivio::fasta {

reader::reader(config const& config_)
    : reader_base{std::visit([&](auto& p) {
        return std::make_unique<pimpl>(p, config_.compressed);
    }, config_.input)}
{}

reader::~reader() = default;

auto reader::next() -> std::optional<record_view> {
    assert(pimpl_);

    auto& ureader  = pimpl_->ureader;
    auto& lastUsed = pimpl_->lastUsed;
    auto& s        = pimpl_->s;

    auto startId = ureader.readUntil('>', lastUsed);
    if (ureader.eof(startId)) return std::nullopt;
    ureader.dropUntil(startId+1);

    auto endId = ureader.readUntil('\n', 0);
    if (ureader.eof(endId)) return std::nullopt;

    auto startSeq = endId+1;

    // convert into dense string representation
    s.clear();
    {
        auto s2 = startSeq;
        do {
            auto s1 = s2;
            s2 = ureader.readUntil('\n', s1);
            s += ureader.string_view(s1, s2);
            s2 += 1;
        } while (!ureader.eof(s2) and ureader.string_view(s2, s2+1)[0] != '>');
        lastUsed = s2;
    }


    return record_view {
        .id  = ureader.string_view(0, endId),
        .seq = s,
    };
}

void reader::close() {
    pimpl_.reset();
}

}
