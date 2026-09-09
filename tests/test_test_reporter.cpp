#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "teez/core/test_reporter.hpp"

namespace {

const std::filesystem::path kTempDir =
    std::filesystem::temp_directory_path() / "teez_test_reporter_tests";

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

TEST_CASE("test_reporter_names lists supported formats", "[test_reporter]") {
    const auto names = teez::core::test_reporter_names();
    REQUIRE(names.size() == 4);
    REQUIRE(teez::core::is_test_reporter_name("json"));
    REQUIRE(teez::core::is_test_reporter_name("junit"));
    REQUIRE(teez::core::is_test_reporter_name("msgpack"));
    REQUIRE(teez::core::is_test_reporter_name("zst"));
}

TEST_CASE("junit test reporter writes testcase xml", "[test_reporter]") {
    reset_temp_dir();
    const auto path = kTempDir / "report.junit.xml";
    teez::core::write_test_report(sample_report(), "junit", path);

    std::ifstream in(path);
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    REQUIRE(content.find("<testsuites") != std::string::npos);
    REQUIRE(content.find("smoke.teez.lua::Smoke &gt; checks") != std::string::npos);
    REQUIRE(content.find("<failure message=\"assertion failed\"") != std::string::npos);
    REQUIRE(content.find("time=\"0.340000\"") != std::string::npos);
    REQUIRE(content.find("<skipped/>") != std::string::npos);
}

TEST_CASE("json test reporter writes canonical report", "[test_reporter]") {
    reset_temp_dir();
    const auto path = kTempDir / "report.json";
    teez::core::write_test_report(sample_report(), "json", path);

    const auto json = nlohmann::json::parse(std::ifstream(path));
    REQUIRE(json.at("schema_version").get<std::string>() == teez::core::kTestReportSchemaVersion);
    REQUIRE(json.at("summary").at("passed").get<int>() == 1);
    REQUIRE(json.at("summary").at("failed").get<int>() == 1);
    REQUIRE(json.at("suites").size() == 1);
    REQUIRE(json.at("suites").at(0).at("cases").size() == 3);
}

TEST_CASE("resolve_test_report_cli_overrides merges config and cli values", "[test_reporter]") {
    const nlohmann::json config = {
        {"test_report", {{"reporter", "junit"}, {"output", "reports/out.xml"}}},
    };

    teez::core::TestReportCliOverrides cli;
    cli.reporter = "json";
    const auto resolved = teez::core::resolve_test_report_cli_overrides(config, cli);
    REQUIRE(resolved.reporter == "json");
    REQUIRE(resolved.output == std::filesystem::path("reports/out.xml"));

    teez::core::TestReportCliOverrides empty_cli;
    const auto from_config = teez::core::resolve_test_report_cli_overrides(config, empty_cli);
    REQUIRE(from_config.reporter == "junit");
    REQUIRE(from_config.output == std::filesystem::path("reports/out.xml"));

    const nlohmann::json legacy = {
        {"report", {{"reporter", "msgpack"}, {"output", "legacy.msgpack"}}}};
    const auto legacy_resolved =
        teez::core::resolve_test_report_cli_overrides(legacy, empty_cli);
    REQUIRE(legacy_resolved.reporter == "msgpack");
    REQUIRE(legacy_resolved.output == std::filesystem::path("legacy.msgpack"));
}
