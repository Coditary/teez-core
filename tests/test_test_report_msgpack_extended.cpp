#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "teez/core/test_report.hpp"
#include "teez/core/test_report_msgpack.hpp"

namespace {

teez::core::TestRunReport sample_report() {
    teez::core::TestRunReport report;
    report.tool = {.name = "teez", .version = "0.1.0"};
    report.suites.push_back({.id = "suite",
                             .name = "suite",
                             .cases = {{.id = "suite::case",
                                        .name = "case",
                                        .status = teez::core::TestStatus::Passed}}});
    return report;
}

}  // namespace

TEST_CASE("test report msgpack bytes helpers roundtrip", "[test_report_msgpack]") {
    const auto original = sample_report();

    const auto msgpack_bytes = teez::core::test_report_to_msgpack_bytes(original);
    const auto restored = teez::core::test_report_from_msgpack_bytes(msgpack_bytes);

    REQUIRE(restored.suites.size() == 1);
    REQUIRE(restored.suites.front().cases.front().name == "case");

    const auto zst_bytes = teez::core::test_report_to_zstd_msgpack_bytes(original);
    const auto from_zst = teez::core::test_report_from_zstd_msgpack_bytes(zst_bytes);
    REQUIRE(from_zst.summary().passed == 1);
}

TEST_CASE("test report msgpack file helpers roundtrip", "[test_report_msgpack]") {
    const auto temp_dir =
        std::filesystem::temp_directory_path() / "teez_test_report_msgpack_file_test";
    std::filesystem::remove_all(temp_dir);
    std::filesystem::create_directories(temp_dir);

    const auto original = sample_report();
    const auto msgpack_path = temp_dir / "report.msgpack";
    const auto zst_path = temp_dir / "report.zst";

    teez::core::write_test_report_msgpack(original, msgpack_path);
    teez::core::write_test_report_zstd_msgpack(original, zst_path);

    const auto from_msgpack = teez::core::load_test_report_msgpack(msgpack_path);
    const auto from_zst = teez::core::load_test_report_zstd_msgpack(zst_path);

    REQUIRE(from_msgpack.suites.front().cases.front().status == teez::core::TestStatus::Passed);
    REQUIRE(from_zst.tool.version == "0.1.0");
}

TEST_CASE("test report msgpack rejects invalid envelopes", "[test_report_msgpack]") {
    const std::vector<std::uint8_t> invalid = {0x00, 0x01, 0x02};
    REQUIRE_THROWS(teez::core::test_report_from_msgpack(invalid));

    const nlohmann::json wrong_format = {
        {"format", "not-a-test-report"}, {"version", 1}, {"report", nlohmann::json::object()}};
    REQUIRE_THROWS_AS(teez::core::test_report_from_msgpack(nlohmann::json::to_msgpack(wrong_format)),
                      std::runtime_error);

    const nlohmann::json wrong_version = {
        {"format", "teez.test-report"}, {"version", 99}, {"report", nlohmann::json::object()}};
    REQUIRE_THROWS_AS(teez::core::test_report_from_msgpack(nlohmann::json::to_msgpack(wrong_version)),
                      std::runtime_error);

    const auto missing_path =
        std::filesystem::temp_directory_path() / "teez_missing_msgpack_report.bin";
    REQUIRE_THROWS_AS(teez::core::load_test_report_msgpack(missing_path), std::runtime_error);
}
