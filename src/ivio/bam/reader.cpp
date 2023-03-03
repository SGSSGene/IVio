#include "reader.h"

#include "../bgzf_reader.h"
#include "../bgzf_mt_reader.h"
#include "../buffered_reader.h"
#include "../file_reader.h"
#include "../mmap_reader.h"
#include "../stream_reader.h"
#include "../zlib_file_reader.h"
#include "../zlib_mmap2_reader.h"
#include "../zlib_ng_file_reader.h"

#include <cassert>
#include <charconv>
#include <functional>
#include <optional>
#include <ranges>

namespace ivio {

template <>
struct reader_base<bam::reader>::pimpl {
    VarBufferedReader ureader;
    size_t lastUsed{};

    bam::header header;

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

    void readHeader() {
        auto [ptr, size] = ureader.read(12);

        if (size < 12) throw std::runtime_error{"something went wrong reading bam file (1)"};

        size_t l_text = ivio::bgzfUnpack<uint32_t>(ptr + 4);
        std::tie(ptr, size) = ureader.read(12 + l_text + 8); // read complete header
        if (size < 12 + l_text + 8) throw std::runtime_error{"couldn't read header of bam file (1a)"};

        header.buffer.resize(l_text);
        memcpy(header.buffer.data(), ptr + 12, l_text);

        auto n_ref = ivio::bgzfUnpack<uint32_t>(ptr + 8 + l_text);
        ureader.dropUntil(12 + l_text);

        // read list of references
        for (size_t i{0}; i < n_ref; ++i) {
            std::tie(ptr, size) = ureader.read(9);
            if (size < 9) throw std::runtime_error{"something went wrong reading bam file (1b)"};
            auto l_name = ivio::bgzfUnpack<uint32_t>(ptr);
            std::tie(ptr, size) = ureader.read(8 + l_name);
            if (size < 8 + l_name) throw std::runtime_error{"error reading entry " + std::to_string(i)};
            /*auto l_ref  = */ivio::bgzfUnpack<uint32_t>(ptr+4+l_name);
            //!TODO read name and save it somewhere
            ureader.dropUntil(8+l_name);
        }
    }

    auto next() -> std::optional<bam::record_view> {
        ureader.dropUntil(lastUsed);
        auto [ptr, size] = ureader.read(40);
        if (size == 0) return std::nullopt;
        if (size < 40) throw std::runtime_error{"something went wrong reading bam file (2)"};

        auto block_size  = ivio::bgzfUnpack<uint32_t>(ptr+0);

        auto [ptr2, size2] = ureader.read(block_size+4); // read again, !TODO this could fail
        auto refID       = ivio::bgzfUnpack<int32_t>(ptr2+4);
        auto pos         = ivio::bgzfUnpack<int32_t>(ptr2+8);
        auto l_read_name = ivio::bgzfUnpack<uint8_t>(ptr2+12);
        auto mapq        = ivio::bgzfUnpack<uint8_t>(ptr2+13);
        auto bin         = ivio::bgzfUnpack<uint16_t>(ptr2+14);
        auto n_cigar_op  = ivio::bgzfUnpack<uint16_t>(ptr2+16);
        auto flag        = ivio::bgzfUnpack<uint16_t>(ptr2+18);
        auto l_seq       = ivio::bgzfUnpack<uint32_t>(ptr2+20);
        auto next_refID  = ivio::bgzfUnpack<int32_t>(ptr2+24);
        auto next_pos    = ivio::bgzfUnpack<int32_t>(ptr2+28);
        auto tlen        = ivio::bgzfUnpack<int32_t>(ptr2+32);
        auto read_name   = ureader.string_view(36, 36+l_read_name);
        auto start_cigar = l_read_name + 36;
        auto cigar       = ureader.string_view(start_cigar, start_cigar+n_cigar_op*4);
        auto start_seq   = start_cigar+n_cigar_op*4;
        auto seq         = ureader.string_view(start_seq, start_seq + (l_seq+1)/2);
        auto start_qual  = start_seq + (l_seq+1)/2;
        auto qual        = ureader.string_view(start_qual, start_qual + l_seq);
        //!TODO List of auxiliary data (tags) is missing

        lastUsed = block_size+4;

        return bam::record_view { .refID      = refID,
                                  .pos        = pos,
                                  .mapq       = mapq,
                                  .bin        = bin,
                                  .flag       = flag,
                                  .next_refID = next_refID,
                                  .next_pos   = next_pos,
                                  .tlen       = tlen,
                                  .read_name  = read_name,
                                  .cigar      = std::span{reinterpret_cast<uint8_t const*>(cigar.data()), cigar.size()},
                                  .seq        = {std::span{reinterpret_cast<uint8_t const*>(seq.data()), seq.size()}, l_seq},
                                  .qual       = std::span{reinterpret_cast<uint8_t const*>(qual.data()), qual.size()},
                                };
    }
};
}

namespace ivio::bam {

reader::reader(config const& config_)
    : reader_base{std::visit([&](auto& p) {
        return std::make_unique<pimpl>(p, config_.threadNbr);
    }, config_.input)}
{
    pimpl_->readHeader();
    header_ = std::move(pimpl_->header);
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
