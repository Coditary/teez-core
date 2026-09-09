#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "teez/core/coverage_msgpack.hpp"

namespace {

const std::filesystem::path kFixturesDir =
    std::filesystem::path(__FILE__).parent_path() / "fixtures" / "coverage";

const std::filesystem::path kTempDir =
    std::filesystem::temp_directory_path() / "teez_coverage_msgpack_tests";

void reset_temp_dir() {
    std::filesystem::remove_all(kTempDir);
    std::filesystem::create_directories(kTempDir);
}

}  // namespace

TEST_CASE("coverage_table_to_msgpack roundtrips in-memory table", "[coverage_msgpack]") {
    const auto original = teez::core::load_coverage_table_from_lcov(kFixturesDir / "sample.lcov");
    const auto bytes = teez::core::coverage_table_to_msgpack(original);
    REQUIRE_FALSE(bytes.empty());

    const auto restored = teez::core::coverage_table_from_msgpack(bytes);
    REQUIRE(restored.source_format == original.source_format);
    REQUIRE(restored.files.size() == original.files.size());
    REQUIRE(restored.by_test.size() == original.by_test.size());
    REQUIRE(restored.meta.source_report_path == original.meta.source_report_path);
    REQUIRE(restored.find_file("/project/src/example.cpp")->find_line(10)->hit_count ==
            original.find_file("/project/src/example.cpp")->find_line(10)->hit_count);
}

TEST_CASE("write and load coverage msgpack file", "[coverage_msgpack]") {
    reset_temp_dir();
    const auto original = teez::core::load_coverage_table_from_lcov(kFixturesDir / "sample.lcov");
    const auto path = kTempDir / "table.msgpack";

    teez::core::write_coverage_table_to_msgpack(original, path);
    const auto restored = teez::core::load_coverage_table_from_msgpack(path);

    REQUIRE(restored.files.size() == original.files.size());
    REQUIRE(restored.find_file("/project/src/example.cpp") != nullptr);
    REQUIRE(restored.find_file("/project/src/example.cpp")->find_line(11)->hit_count == 0);
}

TEST_CASE("load_coverage_table auto-detects msgpack extension", "[coverage_msgpack]") {
    reset_temp_dir();
    const auto original = teez::core::load_coverage_table_from_lcov(kFixturesDir / "sample.lcov");
    const auto path = kTempDir / "auto.msgpack";
    teez::core::write_coverage_table_to_msgpack(original, path);

    const auto restored = teez::core::load_coverage_table(path);
    REQUIRE(restored.files.size() == original.files.size());
}
