#include "reader_mt.h"

#include "../buffered_reader.h"
#include "../file_reader.h"
#include "../mmap_reader.h"
#include "../stream_reader.h"
#include "../zlib_file_reader.h"
#include "../zlib_mmap2_reader.h"

#include <condition_variable>
#include <mutex>
#include <thread>

namespace ivio::fasta {

struct reader_mt_phase {
   std::atomic_int phase{0};

   void waitFor(int expected) {
        int _expected;
        do {
            _expected = expected;
        } while(!phase.compare_exchange_weak(
            _expected, -1,
            std::memory_order_release,
            std::memory_order_relaxed));
    }
    void trigger(int _phase) {
        phase.store(_phase, std::memory_order_release);
    }
};

struct reader_mt_pimpl {
    VarBufferedReader reader;
    std::jthread thread;

    std::optional<record_view> next;
    reader_mt_phase phase;

    reader_mt_pimpl(std::filesystem::path file, bool)
        : reader {[&]() -> VarBufferedReader {
            if (file.extension() == ".fa") {
                return mmap_reader{file.c_str()};
            } else if (file.extension() == ".gz") {
                return zlib_reader{mmap_reader{file.c_str()}};
            }
            throw std::runtime_error("unknown file extension");
        }()}
    {}
    reader_mt_pimpl(std::istream& file, bool compressed)
        : reader {[&]() -> VarBufferedReader {
            if (!compressed) {
                return stream_reader{file};
            } else {
                return zlib_reader{stream_reader{file}};
            }
        }()}
    {}

};

reader_mt::reader_mt(reader_mt_config config)
    : pimpl{std::visit([&](auto& p) {
        return std::make_unique<reader_mt_pimpl>(p, config.compressed);
    }, config.input)}
{
    pimpl->thread = std::jthread{[this](std::stop_token stoken) {
        auto& reader      = pimpl->reader;
        size_t lastUsed{};
        struct R {
            size_t lastUsed{};
            std::string id;
            std::string seq;
        };
        auto   s = std::array<R, 3>{};
        size_t currentS{};


        while(!stoken.stop_requested()) {
            auto& s0          = s[currentS];

            reader.dropUntil(s0.lastUsed);

            s[(currentS+1)%3].lastUsed -= s0.lastUsed;
            s[(currentS+2)%3].lastUsed -= s0.lastUsed;
            lastUsed -= s0.lastUsed;

            auto startId = reader.readUntil('>', lastUsed);
            if (reader.eof(startId)) {
                pimpl->phase.waitFor(0);
                pimpl->next.reset();
                pimpl->phase.trigger(1);
                return;
            }

            auto endId = reader.readUntil('\n', startId+1);
            if (reader.eof(endId)) {
                pimpl->phase.waitFor(0);
                pimpl->next.reset();
                pimpl->phase.trigger(1);
                return;
            }

            auto startSeq = endId+1;

            // convert into dense string representation
            s0.seq.clear();
            {
                auto s2 = startSeq;
                do {
                    auto s1 = s2;
                    s2 = reader.readUntil('\n', s1);
                    s0.seq += reader.string_view(s1, s2);
                    s2 += 1;
                } while (!reader.eof(s2) and reader.string_view(s2, s2+1)[0] != '>');
                lastUsed = s2;
            }
            s0.id = reader.string_view(startId+1, endId);

            pimpl->phase.waitFor(0);
            pimpl->next = record_view {
                .id  = s0.id,
                .seq = s0.seq,
            };
            pimpl->phase.trigger(1);
            currentS = (currentS + 1) % 3;
            s0.lastUsed = lastUsed;
        }
    }};
}
reader_mt::~reader_mt() = default;

auto reader_mt::next() -> std::optional<record_view> {
    pimpl->phase.waitFor(1);
    auto r = std::move(pimpl->next);
    pimpl->phase.trigger(0);
    return r;
}

auto begin(reader_mt& _reader) -> reader_mt::iter {
    return {_reader};
}

}
