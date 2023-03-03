#include <catch2/catch.hpp>
#include <filesystem>
#include <fstream>
#include <ivio/fasta/reader.h>

TEST_CASE("reading fasta files", "[fasta][reader]") {
    auto tmp = std::filesystem::temp_directory_path() / "ivio_test";
    std::filesystem::create_directory(tmp);

    auto expected = std::vector<ivio::fasta::record> {
        ivio::fasta::record{.id = "I am genomic data",    .seq = "ACGTACGTACGTACGT"},
        ivio::fasta::record{.id = "Some other data",      .seq = "GNNNGNJKL"},
        ivio::fasta::record{.id = "Weird multiline data", .seq = "AGgagatAGagagAGAJKNNNN"},
    };

    auto test_data = std::string{
        ">I am genomic data\n"
        "ACGTACGTACGTACGT\n"
        ">Some other data\n"
        "GNNNGNJKL\n"
        ">Weird multiline data\n"
        "AGgagat\n"
        "AGagagAGA\n"
        "JKNNNN\n"
    };

    SECTION("Creating Test file") {
        auto ofs = std::ofstream{tmp / "file.fa", std::ios::binary};
        ofs << test_data;
    }

    SECTION("Read fasta file using std::filesystem::path as input") {
        auto reader = ivio::fasta::reader{{tmp / "file.fa"}};
        auto vec = std::vector(begin(reader), end(reader));
        static_assert(std::same_as<decltype(vec), decltype(expected)>, "vec and expected should have the exact same type");
        CHECK(expected == vec);
    }

    SECTION("Read fasta file using std::ifstream as input") {
        auto ifs = std::ifstream{tmp / "file.fa"};
        auto reader = ivio::fasta::reader{{ifs}};
        auto vec = std::vector(begin(reader), end(reader));
        static_assert(std::same_as<decltype(vec), decltype(expected)>, "vec and expected should have the exact same type");
        CHECK(expected == vec);
    }

    SECTION("Read fasta file using std::stringstream as input") {
        auto ss = std::stringstream{test_data};
        auto reader = ivio::fasta::reader{{ss}};
        auto vec = std::vector(begin(reader), end(reader));
        static_assert(std::same_as<decltype(vec), decltype(expected)>, "vec and expected should have the exact same type");
        CHECK(expected == vec);
    }

    SECTION("cleanup - deleting temp folder") {
        std::filesystem::remove_all(tmp);
    }
}
