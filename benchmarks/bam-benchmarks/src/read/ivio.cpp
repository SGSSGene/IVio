#include "../fasta-benchmarks/src/read/dna5_rank_view.h"
#include "Result.h"

#include <ivio/bam/reader.h>

auto ivio_bench(std::filesystem::path file, size_t threadNbr) -> Result {
    Result result;
    std::array<size_t, 16> ctChars{};
    for (auto record : ivio::bam::reader{{file, threadNbr}}) {
        for (auto c : record.seq) {
            ctChars[c] += 1;
        }
    }
    result.ctChars[0] = ctChars[ivio::bam::record_view::char_to_rank['A']];
    result.ctChars[1] = ctChars[ivio::bam::record_view::char_to_rank['C']];
    result.ctChars[2] = ctChars[ivio::bam::record_view::char_to_rank['G']];
    result.ctChars[3] = ctChars[ivio::bam::record_view::char_to_rank['T']];
    result.ctChars[4] = ctChars[ivio::bam::record_view::char_to_rank['N']];

    return result;
}
