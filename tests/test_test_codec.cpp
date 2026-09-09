#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "teez/core/test_codec.hpp"

namespace {

const std::filesystem::path kTempDir =
    std::filesystem::temp_directory_path() / "teez_test_codec_tests";

void reset_temp_dir() {
    std::filesystem::remove_all(kTempDir);
    std::filesystem::create_directories(kTempDir);
}

teez::core::TestRunReport sample_report() {
    teez::core::TestRunReport report;
    report.suites.push_back({.id = "smoke",
                             .name = "smoke",
                             .cases = {
                                 {.id = "smoke.teez.lua::Smoke > checks::ok",
                                  .name = "ok",
                                  .status = teez::core::TestStatus::Passed,
                                  .duration_seconds = 0.12},
                                 {.id = "smoke.teez.lua::Smoke > checks::bad",
                                  .name = "bad",
                                  .status = teez::core::TestStatus::Failed,
                                  .duration_seconds = 0.34,
                                  .failure = teez::core::TestFailureInfo{
                                      .message = std::string("assertion failed")},
                                  .stderr_lines = {"expected 1, got 2"}},
                                 {.id = "smoke.teez.lua::Smoke > checks::later",
                                  .name = "later",
                                  .status = teez::core::TestStatus::Skipped},
                             }});
    return report;
}

}  // namespace

TEST_CASE("test_report_codec_names lists supported codecs", "[test_codec]") {
    const auto names = teez::core::test_report_codec_names();
    REQUIRE(names.size() == 4);
    REQUIRE(teez::core::is_test_report_codec_name("json"));
    REQUIRE(teez::core::is_test_report_codec_name("junit"));
    REQUIRE(teez::core::is_test_report_codec_name("msgpack"));
    REQUIRE(teez::core::is_test_report_codec_name("zst"));
    REQUIRE(teez::core::is_test_reporter_name("json"));
}

TEST_CASE("json codec roundtrips canonical report", "[test_codec]") {
    const auto original = sample_report();
    const auto& codec = teez::core::test_report_codec_by_name("json");

    const auto encoded = codec.encode(original);
    const auto restored = codec.decode(encoded);

    REQUIRE(restored.schema_version == teez::core::kTestReportSchemaVersion);
    REQUIRE(restored.suites.size() == 1);
    REQUIRE(restored.all_cases().size() == 3);
    REQUIRE(restored.summary().failed == 1);
}

TEST_CASE("junit codec roundtrips testcase data", "[test_codec]") {
    const auto original = sample_report();
    const auto& codec = teez::core::test_report_codec_by_name("junit");

    const auto encoded = codec.encode(original);
    const auto restored = codec.decode(encoded);

    REQUIRE(restored.suites.size() == 1);
    REQUIRE(restored.all_cases().size() == 3);
    REQUIRE(restored.summary().passed == 1);
    REQUIRE(restored.summary().failed == 1);
    REQUIRE(restored.summary().skipped == 1);

    const auto restored_cases = restored.all_cases();
    const auto failed_it = std::find_if(restored_cases.begin(), restored_cases.end(),
                                        [](const teez::core::TestCaseResult& test_case) {
                                            return test_case.name == "bad";
                                        });
    REQUIRE(failed_it != restored_cases.end());
    REQUIRE(failed_it->status == teez::core::TestStatus::Failed);
    REQUIRE(failed_it->failure.has_value());
    REQUIRE(failed_it->failure->message == "assertion failed");
    REQUIRE(failed_it->duration_seconds.has_value());
}

TEST_CASE("junit transformer imports external xml", "[test_codec]") {
    const std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<testsuites tests="2" failures="1" skipped="0" errors="0">
  <testsuite name="alpha" tests="2" failures="1" skipped="0" errors="0" time="1.5">
    <testcase classname="alpha.suite" name="passes" time="0.5"/>
    <testcase classname="alpha.suite" name="fails" time="1.0">
      <failure message="boom">stack</failure>
      <system-err>stderr text</system-err>
    </testcase>
  </testsuite>
</testsuites>
)";

    const auto report = teez::core::test_report_from_junit_xml(xml);
    REQUIRE(report.suites.size() == 1);
    REQUIRE(report.suites.front().name == "alpha");
    REQUIRE(report.suites.front().duration_seconds == 1.5);
    REQUIRE(report.all_cases().size() == 2);

    const auto& passed = report.suites.front().cases.front();
    REQUIRE(passed.status == teez::core::TestStatus::Passed);
    REQUIRE(passed.id == "alpha.suite::passes");

    const auto& failed = report.suites.front().cases.back();
    REQUIRE(failed.status == teez::core::TestStatus::Failed);
    REQUIRE(failed.failure->message == "boom");
    REQUIRE(failed.failure->stacktrace == "stack");
    REQUIRE(failed.stderr_lines.size() == 1);
}

TEST_CASE("codec read and write report files", "[test_codec]") {
    reset_temp_dir();
    const auto json_path = kTempDir / "report.json";
    const auto junit_path = kTempDir / "report.xml";
    const auto original = sample_report();

    teez::core::write_test_report(original, "json", json_path);
    teez::core::write_test_report(original, "junit", junit_path);

    const auto from_json = teez::core::read_test_report(json_path, "json");
    const auto from_junit = teez::core::read_test_report(junit_path, "junit");

    REQUIRE(from_json.all_cases().size() == 3);
    REQUIRE(from_junit.summary().failed == 1);
}

TEST_CASE("encode_test_report and decode_test_report helpers", "[test_codec]") {
    const auto original = sample_report();
    const auto encoded = teez::core::encode_test_report(original, "json");
    const auto restored = teez::core::decode_test_report(encoded, "json");
    REQUIRE(restored.summary().total() == original.summary().total());
}

TEST_CASE("msgpack codec roundtrips canonical report", "[test_codec]") {
    const auto original = sample_report();
    const auto& codec = teez::core::test_report_codec_by_name("msgpack");

    const auto encoded = codec.encode(original);
    const auto restored = codec.decode(encoded);

    REQUIRE(restored.schema_version == teez::core::kTestReportSchemaVersion);
    REQUIRE(restored.all_cases().size() == 3);
    REQUIRE(restored.summary().failed == 1);
}

TEST_CASE("zst codec roundtrips compressed msgpack report", "[test_codec]") {
    const auto original = sample_report();
    const auto& codec = teez::core::test_report_codec_by_name("zst");

    const auto encoded = codec.encode(original);
    REQUIRE(encoded.size() > 4);
    REQUIRE(static_cast<unsigned char>(encoded[0]) == 0x28);
    REQUIRE(static_cast<unsigned char>(encoded[1]) == 0xB5);

    const auto restored = codec.decode(encoded);
    REQUIRE(restored.all_cases().size() == 3);
    REQUIRE(restored.summary().skipped == 1);
}

TEST_CASE("msgpack and zst report files roundtrip", "[test_codec]") {
    reset_temp_dir();
    const auto msgpack_path = kTempDir / "report.msgpack";
    const auto zst_path = kTempDir / "report.msgpack.zst";
    const auto original = sample_report();

    teez::core::write_test_report(original, "msgpack", msgpack_path);
    teez::core::write_test_report(original, "zst", zst_path);

    const auto from_msgpack = teez::core::read_test_report(msgpack_path, "msgpack");
    const auto from_zst = teez::core::read_test_report(zst_path, "zst");

    REQUIRE(from_msgpack.summary().total() == 3);
    REQUIRE(from_zst.summary().failed == 1);
    REQUIRE(std::filesystem::file_size(zst_path) < std::filesystem::file_size(msgpack_path));
}
