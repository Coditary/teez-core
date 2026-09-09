#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "teez/core/coverage_reporter.hpp"
#include "teez/core/coverage_msgpack.hpp"

namespace {

const std::filesystem::path kFixturesDir =
    std::filesystem::path(__FILE__).parent_path() / "fixtures" / "coverage";

const std::filesystem::path kTempDir =
    std::filesystem::temp_directory_path() / "teez_coverage_reporter_tests";

void reset_temp_dir() {
    std::filesystem::remove_all(kTempDir);
    std::filesystem::create_directories(kTempDir);
}

}  // namespace

TEST_CASE("coverage_reporter_names lists all supported formats", "[coverage_reporter]") {
    const auto names = teez::core::coverage_reporter_names();
    REQUIRE(names.size() == 5);
    REQUIRE(teez::core::is_coverage_reporter_name("json"));
    REQUIRE(teez::core::is_coverage_reporter_name("msgpack"));
    REQUIRE(teez::core::is_coverage_reporter_name("lcov"));
    REQUIRE(teez::core::is_coverage_reporter_name("cobertura"));
    REQUIRE(teez::core::is_coverage_reporter_name("junit"));
}

TEST_CASE("make_coverage_reporter rejects unknown format", "[coverage_reporter]") {
    REQUIRE_THROWS_AS(teez::core::make_coverage_reporter("html"), std::runtime_error);
}

TEST_CASE("coverage reporters export sample lcov to all formats", "[coverage_reporter]") {
    reset_temp_dir();
    const auto table = teez::core::load_coverage_table_from_lcov(kFixturesDir / "sample.lcov");

    const auto json_path = kTempDir / "report.json";
    const auto lcov_path = kTempDir / "report.lcov";
    const auto cobertura_path = kTempDir / "report.cobertura.xml";
    const auto junit_path = kTempDir / "report.junit.xml";
    const auto msgpack_path = kTempDir / "report.msgpack";

    teez::core::write_coverage_report(table, "json", json_path);
    teez::core::write_coverage_report(table, "lcov", lcov_path);
    teez::core::write_coverage_report(table, "cobertura", cobertura_path);
    teez::core::write_coverage_report(table, "junit", junit_path);
    teez::core::write_coverage_report(table, "msgpack", msgpack_path);

    REQUIRE(std::filesystem::exists(json_path));
    REQUIRE(std::filesystem::exists(lcov_path));
    REQUIRE(std::filesystem::exists(cobertura_path));
    REQUIRE(std::filesystem::exists(junit_path));
    REQUIRE(std::filesystem::exists(msgpack_path));

    const auto json = nlohmann::json::parse(std::ifstream(json_path));
    REQUIRE(json.at("source_format") == "lcov");

    std::ifstream junit_file(junit_path);
    std::string junit_content((std::istreambuf_iterator<char>(junit_file)),
                              std::istreambuf_iterator<char>());
    REQUIRE(junit_content.find("<testsuite name=\"coverage\"") != std::string::npos);
    REQUIRE(junit_content.find("coverage.file") != std::string::npos);

    const auto roundtrip = teez::core::load_coverage_table_from_msgpack(msgpack_path);
    REQUIRE(roundtrip.source_format == table.source_format);
    REQUIRE(roundtrip.files.size() == table.files.size());
    REQUIRE(roundtrip.by_test.size() == table.by_test.size());
    const auto* example = roundtrip.find_file("/project/src/example.cpp");
    REQUIRE(example != nullptr);
    REQUIRE(example->find_line(10)->hit_count == 3);
    REQUIRE(example->find_function("example_fn")->end_line == 12);
}

TEST_CASE("resolve_coverage_report_options merges config and cli overrides", "[coverage_reporter]") {
    const nlohmann::json config = {
        {"coverage",
         {{"reporter", "lcov"}, {"input", "in.lcov"}, {"output", "out.lcov"}, {"min_line_rate", 0.8}}},
    };

    teez::core::CoverageReportCliOverrides cli;
    cli.reporter = "json";
    cli.output = "custom.json";

    const auto resolved = teez::core::resolve_coverage_report_options(config, cli);
    REQUIRE(resolved.reporter == "json");
    REQUIRE(resolved.input == std::filesystem::path("in.lcov"));
    REQUIRE(resolved.output == std::filesystem::path("custom.json"));
    REQUIRE(resolved.min_line_rate.has_value());
    REQUIRE(*resolved.min_line_rate == 0.8);
}

TEST_CASE("export_coverage_report loads input and writes selected reporter", "[coverage_reporter]") {
    reset_temp_dir();
    teez::core::CoverageReportOptions options;
    options.reporter = "json";
    options.input = kFixturesDir / "sample.lcov";
    options.output = kTempDir / "exported.json";

    teez::core::export_coverage_report(options);
    REQUIRE(std::filesystem::exists(*options.output));
}
