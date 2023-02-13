#include "writer.h"

#include "../buffered_writer.h"
#include "../file_writer.h"
#include "../stream_writer.h"
#include "../zlib_file_writer.h"

#include <cassert>

namespace io3 {

template <>
struct writer_base<fasta::writer>::pimpl {
    using Writers = std::variant<file_writer,
                                 buffered_writer<zlib_file_writer>,
                                 stream_writer,
                                 buffered_writer<zlib_stream_writer>
                                 >;

    size_t contig_length;
    Writers writer;
    std::string buffer;
    pimpl(std::filesystem::path output, size_t contig_length, bool)
        : contig_length{contig_length}
        , writer {[&]() -> Writers {
            if (output.extension() == ".gz") {
                return buffered_writer{zlib_file_writer{file_writer{output}}};
            }
            return file_writer{output};
        }()}
    {}

    pimpl(std::ostream& output, size_t contig_length, bool compressed)
        : contig_length{contig_length}
        , writer {[&]() -> Writers {
            if (compressed) {
                return buffered_writer{zlib_stream_writer{stream_writer{output}}};
            }
            return stream_writer{output};
        }()}
    {}
};

}

namespace io3::fasta {

writer::writer(config config_)
    : writer_base{std::visit([&](auto& p) {
        return std::make_unique<pimpl>(p, config_.length, config_.compressed);
    }, config_.output)}
{
    assert(config_.length > 0);
}
writer::~writer() {
    if (pimpl_) {
        std::visit([&](auto& writer) {
            writer.write({}, true);
        }, pimpl_->writer);
    }
}

void writer::write(record_view record) {
    assert(pimpl_);
    std::visit([&](auto& writer) {
        auto const& contig_length = pimpl_->contig_length;
        auto& buffer = pimpl_->buffer;
        buffer.clear();
        buffer = '>';
        buffer += record.id;
        buffer += '\n';
        auto seq = record.seq;
        while (seq.size() > contig_length) {
            buffer += seq.substr(0, contig_length);
            buffer += '\n';
            seq = seq.substr(contig_length);
        }
        if (seq.size() > 0) {
            buffer += seq;
            buffer += '\n';
        }
        writer.write(buffer, false);
    }, pimpl_->writer);
}

}
