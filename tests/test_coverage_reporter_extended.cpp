#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "teez/core/coverage_reporter.hpp"

namespace {

const std::filesystem::path kFixturesDir =
    std::filesystem::path(__FILE__).parent_path() / "fixtures" / "coverage";

const std::filesystem::path kTempDir =
    std::filesystem::temp_directory_path() / "teez_coverage_reporter_extended_tests";

void reset_temp_dir() {
    std::filesystem::remove_all(kTempDir);
    std::filesystem::create_directories(kTempDir);
}

}  // namespace

TEST_CASE("coverage reporter default extensions are stable", "[coverage_reporter]") {
    REQUIRE(teez::core::make_coverage_reporter("json")->default_extension() == ".json");
    REQUIRE(teez::core::make_coverage_reporter("lcov")->default_extension() == ".lcov");
    REQUIRE(teez::core::make_coverage_reporter("cobertura")->default_extension() == ".xml");
    REQUIRE(teez::core::make_coverage_reporter("junit")->default_extension() == ".xml");
    REQUIRE(teez::core::make_coverage_reporter("msgpack")->default_extension() == ".msgpack");
}

TEST_CASE("resolve_coverage_report_options falls back to config defaults", "[coverage_reporter]") {
    const nlohmann::json config = {
        {"coverage",
         {{"reporter", "cobertura"},
          {"input", "coverage/input.lcov"},
          {"output", "coverage/out.xml"},
          {"min_line_rate", 0.75}}}};

    const auto resolved = teez::core::resolve_coverage_report_options(config, {});
    REQUIRE(resolved.reporter == "cobertura");
    REQUIRE(resolved.input == std::filesystem::path("coverage/input.lcov"));
    REQUIRE(resolved.output == std::filesystem::path("coverage/out.xml"));
    REQUIRE(*resolved.min_line_rate == 0.75);
}

TEST_CASE("export_coverage_report writes cobertura and junit outputs", "[coverage_reporter]") {
    reset_temp_dir();

    teez::core::CoverageReportOptions cobertura;
    cobertura.reporter = "cobertura";
    cobertura.input = kFixturesDir / "sample.lcov";
    cobertura.output = kTempDir / "report.cobertura.xml";
    teez::core::export_coverage_report(cobertura);
    REQUIRE(std::filesystem::exists(*cobertura.output));

    teez::core::CoverageReportOptions junit;
    junit.reporter = "junit";
    junit.input = kFixturesDir / "sample.lcov";
    junit.output = kTempDir / "report.junit.xml";
    teez::core::export_coverage_report(junit);
    REQUIRE(std::filesystem::exists(*junit.output));

    std::ifstream junit_file(*junit.output);
    const std::string junit_content((std::istreambuf_iterator<char>(junit_file)),
                                    std::istreambuf_iterator<char>());
    REQUIRE((junit_content.find("&lt;") != std::string::npos ||
             junit_content.find("<") != std::string::npos));
}
