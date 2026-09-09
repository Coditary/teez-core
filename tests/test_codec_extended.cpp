#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "teez/core/test_codec.hpp"

namespace {

const std::filesystem::path kTempDir =
    std::filesystem::temp_directory_path() / "teez_test_codec_extended_tests";

void reset_temp_dir() {
    std::filesystem::remove_all(kTempDir);
    std::filesystem::create_directories(kTempDir);
}

teez::core::TestRunReport sample_report() {
    teez::core::TestRunReport report;
    report.suites.push_back({.id = "suite",
                             .name = "suite",
                             .cases = {{.id = "suite::case",
                                        .name = "case",
                                        .status = teez::core::TestStatus::Passed}}});
    return report;
}

}  // namespace

TEST_CASE("test_report_codec_by_name rejects unknown codecs", "[test_codec]") {
    REQUIRE_FALSE(teez::core::is_test_report_codec_name("unknown"));
    REQUIRE_THROWS_AS(teez::core::test_report_codec_by_name("unknown"), std::runtime_error);
    REQUIRE_THROWS_AS(teez::core::encode_test_report(sample_report(), "unknown"),
                      std::runtime_error);
    REQUIRE_THROWS_AS(teez::core::decode_test_report("{}", "unknown"), std::runtime_error);
}

TEST_CASE("read_test_report roundtrips written codec files", "[test_codec]") {
    reset_temp_dir();
    const auto original = sample_report();
    const auto json_path = kTempDir / "report.json";
    const auto msgpack_path = kTempDir / "report.msgpack";
    const auto zst_path = kTempDir / "report.msgpack.zst";

    teez::core::write_test_report(original, "json", json_path);
    teez::core::write_test_report(original, "msgpack", msgpack_path);
    teez::core::write_test_report(original, "zst", zst_path);

    REQUIRE(teez::core::read_test_report(json_path, "json").summary().passed == 1);
    REQUIRE(teez::core::read_test_report(msgpack_path, "msgpack").summary().passed == 1);
    REQUIRE(teez::core::read_test_report(zst_path, "zst").summary().passed == 1);
}

TEST_CASE("read_test_report throws for missing files", "[test_codec]") {
    const auto missing = kTempDir / "missing-report.json";
    REQUIRE_THROWS_AS(teez::core::read_test_report(missing, "json"), std::runtime_error);
}
