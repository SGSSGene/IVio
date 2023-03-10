#pragma once

#include "../reader_base.h"
#include "record.h"

#include <filesystem>
#include <optional>
#include <tuple>
#include <variant>

namespace ivio::fastq {

struct reader : public reader_base<reader> {
    using record      = fastq::record;
    using record_view = fastq::record_view;

    struct config {
        // Source: file or stream
        std::variant<std::filesystem::path, std::reference_wrapper<std::istream>> input;

        // This is only relevant if a stream is being used
        bool compressed{};
    };

public:
    reader(config const& config_);
    ~reader();

    auto next() -> std::optional<record_view>;
    void close();
};

}
