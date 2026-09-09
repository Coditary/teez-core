#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "teez/core/compression.hpp"

TEST_CASE("zstd_compress and zstd_decompress roundtrip payload", "[compression]") {
    const std::string text = "teez coverage payload with repeated bytes: abcabcabcabc";
    const std::vector<std::uint8_t> input(text.begin(), text.end());

    const auto compressed = teez::core::zstd_compress(input);
    REQUIRE_FALSE(compressed.empty());

    const auto restored = teez::core::zstd_decompress(compressed);
    REQUIRE(restored == input);
}

TEST_CASE("zstd helpers accept empty input", "[compression]") {
    const std::vector<std::uint8_t> empty;
    REQUIRE(teez::core::zstd_compress(empty).empty());
    REQUIRE(teez::core::zstd_decompress(empty).empty());
}

TEST_CASE("zstd_decompress rejects invalid frame", "[compression]") {
    const std::vector<std::uint8_t> garbage = {0x00, 0x01, 0x02, 0x03};
    REQUIRE_THROWS_AS(teez::core::zstd_decompress(garbage), std::runtime_error);
}
