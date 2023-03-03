#include "reader.h"

#include "../buffered_reader.h"
#include "../file_reader.h"
#include "../mmap_reader.h"
#include "../stream_reader.h"
#include "../zlib_file_reader.h"
#include "../zlib_mmap2_reader.h"

namespace ivio {

template <>
struct reader_base<fastq::reader>::pimpl {
    VarBufferedReader ureader;
    size_t lastUsed{};

    pimpl(std::filesystem::path file, bool)
        : ureader {[&]() -> VarBufferedReader {
            if (file.extension() == ".fq") {
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

namespace ivio::fastq {

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

    auto startId = ureader.readUntil('@', lastUsed);
    if (ureader.eof(startId)) return std::nullopt;
    ureader.dropUntil(startId+1);

    auto endId = ureader.readUntil('\n', 0);
    if (ureader.eof(endId)) return std::nullopt;

    auto startSeq = endId+1;
    if (ureader.eof(startSeq)) return std::nullopt;

    auto endSeq = ureader.readUntil('\n', startSeq);
    if (ureader.eof(endSeq)) return std::nullopt;
    if (ureader.eof(endSeq+1)) return std::nullopt;

    auto startId2 = endSeq+2;
    auto endId2 = ureader.readUntil('\n', startId2);
    if (ureader.eof(endId2)) return std::nullopt;

    auto startQual = endId2+1;
    auto endQual = ureader.readUntil('\n', startQual);
    if (ureader.eof(endQual)) return std::nullopt;

    lastUsed = endQual;

    return record_view {
        .id   = ureader.string_view(0, endId),
        .seq  = ureader.string_view(startSeq, endSeq),
        .id2  = ureader.string_view(startId2, endId2),
        .qual = ureader.string_view(startQual, endQual),
    };
}

void reader::close() {
    pimpl_.reset();
}

static_assert(record_reader_c<reader>);

}
