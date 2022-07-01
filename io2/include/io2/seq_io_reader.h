#pragma once

#include "alphabet_seqan223.h"
#include "iterator.h"

#include <filesystem>
#include <optional>
#include <seqan/seq_io.h>
#include <seqan3/alphabet/nucleotide/dna5.hpp>
#include <seqan3/alphabet/quality/phred42.hpp>
#include <string_view>


namespace io2 {

namespace seq_io {

/* A single record
 *
 * This record represents a single entry in the file.
 * It provides views into the file.
 */
template <typename AlphabetS3, typename QualitiesS3>
struct record {
    using sequence_view  = decltype(toSeqan3<AlphabetS3>({}));
    using qualities_view = decltype(toSeqan3<QualitiesS3>({}));

    std::string_view id;
    sequence_view    seq;
    qualities_view   qual;
};


/** A reader to read sequence files like fasta, fastq, genbank, embl
 *
 * Usage:
 *    auto reader = io2::seq_io::reader {
 *       .input = _file,                         // accepts string and streams
 *       .alphabet = sgg_io::type<seqan3::dna5>, // default dna5
 *   };
 */
template <typename AlphabetS3 = seqan3::dna5,
          typename QualitiesS3 = seqan3::phred42>
struct reader {
    /** Wrapper to allow path and stream inputs
     */
    struct Input {
        using File = seqan::SeqFileIn;
        using Stream = seqan::Iter<std::istream, seqan::StreamIterator<seqan::Input>>;

        std::variant<File, Stream> fileIn;

        // function that avoids std::variant lookup
        using F = std::function<void(seqan::CharString&, seqan::String<detail::AlphabetAdaptor<AlphabetS3>>&, seqan::String<detail::AlphabetAdaptor<QualitiesS3>>&)>;
        F readRecord;

        bool atEnd{false};


        Input(char const* _path)
            : fileIn{std::in_place_index<0>, _path}
        {
            readRecord = [&](auto&...args) {
                auto& file = std::get<0>(fileIn);
                seqan::readRecord(args..., file);
                atEnd = seqan::atEnd(file);
            };
        }
        Input(std::string const& _path)
            : Input(_path.c_str())
        {}
        Input(std::filesystem::path const& _path)
            : Input(_path.c_str())
        {}
        Input(std::istream& istr)
            : fileIn{std::in_place_index<1>, istr}
        {
            readRecord = [&](auto&...args) {
                auto& stream = std::get<1>(fileIn);
                seqan::readRecord(args..., stream, seqan::Fasta{});
                atEnd = seqan::atEnd(stream);
            };
        }
    };

    // configurable from the outside
    Input input;
    [[no_unique_address]] detail::empty_class<AlphabetS3>  alphabet{};
    [[no_unique_address]] detail::empty_class<QualitiesS3> qualities{};


    // internal variables
    // storage for one record
    struct {
        seqan::CharString id;
        seqan::String<detail::AlphabetAdaptor<AlphabetS3>> seq;
        seqan::String<detail::AlphabetAdaptor<QualitiesS3>> qual;

        record<AlphabetS3, QualitiesS3> return_record;
    } storage;

    auto next() -> record<AlphabetS3, QualitiesS3> const* {
        if (input.atEnd) return nullptr;
        input.readRecord(storage.id, storage.seq, storage.qual);

        storage.return_record = record<AlphabetS3, QualitiesS3> {
            .id  = to_view(storage.id),
            .seq = toSeqan3(storage.seq),
            .qual = toSeqan3(storage.qual),
        };
        return &storage.return_record;
    }

    friend auto begin(reader& _reader) {
        using iter = detail::iterator<reader, record<AlphabetS3, QualitiesS3>>;
        return iter{.reader = _reader};
    }
    friend auto end(reader const&) {
        return nullptr;
    }

};

}
}