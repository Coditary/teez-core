#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sol/sol.hpp>

#include "teez/core/coverage.hpp"
#include "teez/core/lua_helpers.hpp"

namespace {

const std::filesystem::path kFixturesDir =
    std::filesystem::path(__FILE__).parent_path() / "fixtures" / "coverage";

const std::filesystem::path kTempDir =
    std::filesystem::temp_directory_path() / "teez_coverage_tests";

void reset_temp_dir() {
    std::filesystem::remove_all(kTempDir);
    std::filesystem::create_directories(kTempDir);
}

}  // namespace

TEST_CASE("normalize_coverage_path uses forward slashes", "[coverage]") {
    REQUIRE(teez::core::normalize_coverage_path("src/./example.cpp") == "src/example.cpp");
}

TEST_CASE("coverage_path_matches supports filename and path globs", "[coverage]") {
    REQUIRE(teez::core::coverage_path_matches("src/example.cpp", "*.cpp"));
    REQUIRE(teez::core::coverage_path_matches("src/nested/example.cpp", "src/**"));
    REQUIRE_FALSE(teez::core::coverage_path_matches("lib/example.cpp", "src/**"));
}

TEST_CASE("load_coverage_table_from_lcov parses lines functions branches and tests", "[coverage]") {
    const auto table = teez::core::load_coverage_table_from_lcov(kFixturesDir / "sample.lcov");

    REQUIRE(table.source_format == teez::core::CoverageSourceFormat::Lcov);
    REQUIRE(table.meta.source_report_path.has_value());
    REQUIRE(table.files.size() == 2);
    REQUIRE(table.by_test.size() == 2);

    const auto* example = table.find_file("/project/src/example.cpp");
    REQUIRE(example != nullptr);
    REQUIRE(example->lines_found() == 3);
    REQUIRE(example->lines_hit() == 2);
    REQUIRE(example->functions_found() == 1);
    REQUIRE(example->functions_hit() == 1);
    REQUIRE(example->branches_found() == 3);
    REQUIRE(example->branches_hit() == 1);

    const auto* fn = example->find_function("example_fn");
    REQUIRE(fn != nullptr);
    REQUIRE(fn->start_line == 8);
    REQUIRE(fn->end_line == 12);
    REQUIRE(fn->hit_count == 2);

    const auto* line_10 = example->find_line(10);
    REQUIRE(line_10 != nullptr);
    REQUIRE(line_10->hit_count == 3);
    REQUIRE(line_10->kind == teez::core::CoverageLineKind::Covered);
    REQUIRE(line_10->branches.size() == 2);

    const auto* line_11 = example->find_line(11);
    REQUIRE(line_11 != nullptr);
    REQUIRE(line_11->kind == teez::core::CoverageLineKind::Executable);
    REQUIRE(line_11->status == teez::core::CoverageLineStatus::Uncovered);
    REQUIRE(line_11->branches.size() == 1);
    REQUIRE(line_11->branches.front().taken == false);

    const auto* login_slice = table.by_test.at("test_login").find_file("/project/src/example.cpp");
    REQUIRE(login_slice != nullptr);
    REQUIRE(login_slice->find_line(10)->hit_count == 3);
    REQUIRE(table.by_test.at("test_login").meta.test_name == std::string("test_login"));
}

TEST_CASE("load_coverage_table_from_cobertura parses metadata hierarchy sources and conditions",
          "[coverage]") {
    const auto table =
        teez::core::load_coverage_table_from_cobertura(kFixturesDir / "sample.cobertura.xml");

    REQUIRE(table.source_format == teez::core::CoverageSourceFormat::Cobertura);
    REQUIRE(table.meta.version == "1.9");
    REQUIRE(table.meta.timestamp == 1700000000);
    REQUIRE(table.meta.line_rate_reported.has_value());
    REQUIRE(std::abs(*table.meta.line_rate_reported - 0.5) < 0.0001);
    REQUIRE(table.meta.branch_rate_reported.has_value());
    REQUIRE(table.source_roots.size() == 1);
    REQUIRE(table.source_roots.front() == "/project");

    const auto* example = table.find_file("src/example.cpp");
    REQUIRE(example != nullptr);
    REQUIRE(example->class_name == "Example");
    REQUIRE(example->package_name == "example");
    REQUIRE(example->lines_found() == 3);
    REQUIRE(example->lines_hit() == 2);

    const auto* fn = example->find_function("run");
    REQUIRE(fn != nullptr);
    REQUIRE(fn->start_line == 8);
    REQUIRE(fn->hit_count == 3);
    REQUIRE(fn->signature == "()");

    const auto* line_10 = example->find_line(10);
    REQUIRE(line_10 != nullptr);
    REQUIRE(line_10->branches.size() == 1);
    REQUIRE(line_10->branches.front().condition_coverage == "50% (1/2)");
    REQUIRE(line_10->branches.front().conditions.size() == 2);
    REQUIRE(line_10->branches.front().conditions.front().type == "jump");
}

TEST_CASE("load_coverage_table auto-detects format from extension", "[coverage]") {
    const auto lcov = teez::core::load_coverage_table(kFixturesDir / "sample.lcov");
    const auto cobertura = teez::core::load_coverage_table(kFixturesDir / "sample.cobertura.xml");

    REQUIRE(lcov.source_format == teez::core::CoverageSourceFormat::Lcov);
    REQUIRE(cobertura.source_format == teez::core::CoverageSourceFormat::Cobertura);
}

TEST_CASE("CoverageTable merge sums line function and branch hits", "[coverage]") {
    teez::core::CoverageTable base = teez::core::load_coverage_table_from_lcov(kFixturesDir / "sample.lcov");
    const teez::core::CoverageTable duplicate =
        teez::core::load_coverage_table_from_lcov(kFixturesDir / "sample.lcov");

    base.merge(duplicate);

    const auto* example = base.find_file("/project/src/example.cpp");
    REQUIRE(example != nullptr);
    REQUIRE(example->find_line(10)->hit_count == 6);
    REQUIRE(example->find_function("example_fn")->hit_count == 4);
    REQUIRE(example->find_line(10)->branches.front().taken_count == 2);
}

TEST_CASE("CoverageTable merges lcov and cobertura via source root and basename", "[coverage]") {
    teez::core::CoverageTable lcov = teez::core::load_coverage_table_from_lcov(kFixturesDir / "sample.lcov");
    const teez::core::CoverageTable cobertura =
        teez::core::load_coverage_table_from_cobertura(kFixturesDir / "sample.cobertura.xml");

    lcov.add_source_root("/project");
    lcov.merge(cobertura);

    const auto* merged = lcov.find_file("src/example.cpp");
    REQUIRE(merged != nullptr);
    REQUIRE(merged->find_line(10)->hit_count >= 5);
    REQUIRE(merged->find_function("example_fn") != nullptr);
    REQUIRE(merged->find_function("run") != nullptr);
}

TEST_CASE("CoverageTable reports overall line branch and function rates", "[coverage]") {
    const auto table = teez::core::load_coverage_table_from_lcov(kFixturesDir / "sample.lcov");

    REQUIRE(table.total_lines_found() == 5);
    REQUIRE(table.total_lines_hit() == 3);
    REQUIRE(std::abs(table.line_rate() - 0.6) < 0.0001);
    REQUIRE(table.total_functions_found() == 2);
    REQUIRE(table.total_functions_hit() == 1);
    REQUIRE(table.total_branches_found() == 3);
    REQUIRE(table.total_branches_hit() == 1);
}

TEST_CASE("CoverageTable query helpers filter uncovered lines and functions", "[coverage]") {
    teez::core::CoverageTable table =
        teez::core::load_coverage_table_from_lcov(kFixturesDir / "sample.lcov");

    const auto example_matches = table.files_matching("*example.cpp");
    REQUIRE(example_matches.size() == 1);
    REQUIRE(example_matches.front().find("example.cpp") != std::string::npos);

    const auto src_matches = table.files_matching("src/**");
    REQUIRE(src_matches.size() == 1);
    REQUIRE(src_matches.front() == "src/other.cpp");

    const auto uncovered = table.uncovered_lines("/project/src/example.cpp");
    REQUIRE(std::find(uncovered.begin(), uncovered.end(), 11) != uncovered.end());

    const auto never_hit = table.never_hit_functions("src/other.cpp");
    REQUIRE(never_hit.size() == 1);
    REQUIRE(never_hit.front() == "other_fn");
}

TEST_CASE("CoverageTable excluded lines are omitted from hit counts", "[coverage]") {
    teez::core::CoverageTable table =
        teez::core::load_coverage_table_from_lcov(kFixturesDir / "sample.lcov");
    auto& file = table.upsert_file("/project/src/example.cpp");
    file.mark_line_excluded(11, true);

    REQUIRE(file.find_line(11)->kind == teez::core::CoverageLineKind::NonExecutable);
    REQUIRE(file.lines_hit() == 2);
    REQUIRE(table.uncovered_lines("/project/src/example.cpp").empty());
}

TEST_CASE("check_coverage_thresholds validates aggregate rates", "[coverage]") {
    const auto table = teez::core::load_coverage_table_from_lcov(kFixturesDir / "sample.lcov");

    teez::core::CoverageThresholds pass_thresholds;
    pass_thresholds.min_line_rate = 0.5;
    pass_thresholds.min_branch_rate = 0.1;
    pass_thresholds.min_function_rate = 0.4;
    REQUIRE(teez::core::check_coverage_thresholds(table, pass_thresholds).passed);

    teez::core::CoverageThresholds fail_thresholds;
    fail_thresholds.min_line_rate = 0.95;
    const auto result = teez::core::check_coverage_thresholds(table, fail_thresholds);
    REQUIRE_FALSE(result.passed);
    REQUIRE(result.failures.size() == 1);
}

TEST_CASE("diff_coverage_tables reports changed line hits and rates", "[coverage]") {
    teez::core::CoverageTable before =
        teez::core::load_coverage_table_from_lcov(kFixturesDir / "sample.lcov");
    teez::core::CoverageTable after = before;
    after.upsert_file("/project/src/example.cpp").lines[10].hit_count = 99;

    const auto diff = teez::core::diff_coverage_tables(before, after);
    REQUIRE(diff.line_rate_before <= diff.line_rate_after);
    REQUIRE(diff.line_changes.size() >= 1);
    REQUIRE(diff.line_changes.front().after_hits == 99);
}

TEST_CASE("coverage_table_to_json_string exports structured json", "[coverage]") {
    const auto table = teez::core::load_coverage_table_from_lcov(kFixturesDir / "sample.lcov");
    const auto json = nlohmann::json::parse(teez::core::coverage_table_to_json_string(table));

    REQUIRE(json.at("source_format") == "lcov");
    REQUIRE(json.at("total_lines_found") == 5);
    REQUIRE(json.at("files").contains("/project/src/example.cpp"));
    REQUIRE(json.at("by_test").contains("test_login"));
}

TEST_CASE("coverage exporters roundtrip lcov and cobertura", "[coverage]") {
    reset_temp_dir();
    const auto table = teez::core::load_coverage_table_from_lcov(kFixturesDir / "sample.lcov");

    const auto lcov_path = kTempDir / "exported.lcov";
    const auto cobertura_path = kTempDir / "exported.xml";
    teez::core::export_coverage_table_to_lcov(table, lcov_path);
    teez::core::export_coverage_table_to_cobertura(table, cobertura_path);

    REQUIRE(std::filesystem::exists(lcov_path));
    REQUIRE(std::filesystem::exists(cobertura_path));

    const auto reloaded_lcov = teez::core::load_coverage_table_from_lcov(lcov_path);
    REQUIRE(reloaded_lcov.find_file("/project/src/example.cpp") != nullptr);
    REQUIRE(reloaded_lcov.find_file("/project/src/example.cpp")->lines_hit() == 2);

    const auto reloaded_cobertura = teez::core::load_coverage_table_from_cobertura(cobertura_path);
    REQUIRE(reloaded_cobertura.find_file("/project/src/example.cpp") != nullptr);
}

TEST_CASE("load_coverage_table rejects unsupported format", "[coverage]") {
    reset_temp_dir();
    const auto path = kTempDir / "unsupported.txt";
    std::ofstream(path) << "not coverage\n";
    REQUIRE_THROWS_AS(teez::core::load_coverage_table(path), std::runtime_error);
}

TEST_CASE("coverage lua helpers load merge query and export", "[coverage][lua_helpers]") {
    reset_temp_dir();
    sol::state lua;
    teez::core::register_lua_helpers(lua);

    const auto lcov_path = (kFixturesDir / "sample.lcov").string();
    const auto export_path = (kTempDir / "from_lua.lcov").string();

    const auto result = lua.safe_script(R"(
        local base = coverage.load(")" + lcov_path + R"(")
        local duplicate = coverage.load(")" + lcov_path + R"(")
        coverage.merge(base, duplicate)
        local uncovered = coverage.uncovered_lines(base, "/project/src/example.cpp")
        local never_hit = coverage.never_hit_functions(base, "src/other.cpp")
        local json = coverage.to_json(base)
        coverage.export_lcov(base, ")" + export_path + R"(")
        return {
            line_rate = coverage.line_rate(base),
            uncovered_count = #uncovered,
            never_hit = never_hit[1],
            json_has_files = json:find('"files"') ~= nil,
            export_ok = true,
        }
    )");

    REQUIRE(result.valid());
    const sol::table payload = result.get<sol::table>();
    REQUIRE(payload["line_rate"].get<double>() > 0.5);
    REQUIRE(payload["uncovered_count"].get<int>() >= 1);
    REQUIRE(payload["never_hit"].get<std::string>() == "other_fn");
    REQUIRE(payload["json_has_files"].get<bool>());
    REQUIRE(std::filesystem::exists(export_path));
}

TEST_CASE("coverage lua assert_threshold throws on failure", "[coverage][lua_helpers]") {
    sol::state lua;
    teez::core::register_lua_helpers(lua);

    const auto lcov_path = (kFixturesDir / "sample.lcov").string();
    const auto result = lua.safe_script(R"(
        local ok, err = pcall(function()
            local table = coverage.load(")" + lcov_path + R"(")
            coverage.assert_threshold(table, { min_line_rate = 0.99 })
        end)
        return { ok = ok, err = err }
    )");

    REQUIRE(result.valid());
    const sol::table payload = result.get<sol::table>();
    const bool ok = payload["ok"].get<bool>();
    REQUIRE_FALSE(ok);
}

TEST_CASE("coverage_table_to_event builds NDJSON payload", "[coverage]") {
    const auto table = teez::core::load_coverage_table_from_lcov(kFixturesDir / "sample.lcov");
    const auto event = teez::core::coverage_table_to_event(table);

    REQUIRE(event.at("event").get<std::string>() == "coverage");
    REQUIRE(event.at("line_rate").get<double>() > 0.0);
    REQUIRE(event.at("lines_found").get<int>() > 0);
}
