#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include <sol/sol.hpp>

#include "teez/core/lua_helpers.hpp"

namespace {

const std::filesystem::path kFixturesDir =
    std::filesystem::path(__FILE__).parent_path() / "fixtures" / "coverage";

const std::filesystem::path kTempDir =
    std::filesystem::temp_directory_path() / "teez_coverage_lua_extended_tests";

void reset_temp_dir() {
    std::filesystem::remove_all(kTempDir);
    std::filesystem::create_directories(kTempDir);
}

}  // namespace

TEST_CASE("coverage lua helpers expose rates diff exports and reporters", "[coverage][lua_helpers]") {
    reset_temp_dir();
    sol::state lua;
    teez::core::register_lua_helpers(lua);

    const auto lcov_path = (kFixturesDir / "sample.lcov").string();
    const auto export_lcov = (kTempDir / "exported.lcov").string();
    const auto export_xml = (kTempDir / "exported.xml").string();
    const auto export_json = (kTempDir / "exported.json").string();

    const auto result = lua.safe_script(R"(
        local base = coverage.load(")" + lcov_path + R"(")
        local duplicate = coverage.load(")" + lcov_path + R"(")
        coverage.merge(base, duplicate)
        coverage.register_alias(base, "alias.cpp", "/project/src/example.cpp")
        coverage.add_source_root(base, "/project")
        coverage.mark_excluded(base, "/project/src/example.cpp", 11)

        local before = coverage.load(")" + lcov_path + R"(")
        local diff = coverage.diff(before, base)
        local check = coverage.check_threshold(base, { min_line_rate = 0.1 })
        local reporters = coverage.reporters()

        coverage.export_lcov(base, ")" + export_lcov + R"(")
        coverage.export_cobertura(base, ")" + export_xml + R"(")
        coverage.export(base, "json", ")" + export_json + R"(")

        return {
            line_rate = coverage.line_rate(base),
            branch_rate = coverage.branch_rate(base),
            function_rate = coverage.function_rate(base),
            msgpack_size = #coverage.to_msgpack(base),
            files = coverage.files_matching(base, "src/*"),
            diff_changes = #diff.line_changes,
            check_passed = check.passed,
            reporter_count = #reporters,
        }
    )");

    REQUIRE(result.valid());
    const sol::table payload = result.get<sol::table>();
    REQUIRE(payload["line_rate"].get<double>() > 0.0);
    REQUIRE(payload["branch_rate"].get<double>() >= 0.0);
    REQUIRE(payload["function_rate"].get<double>() >= 0.0);
    REQUIRE(payload["msgpack_size"].get<int>() > 0);
    REQUIRE(payload["files"].get<sol::table>().size() >= 1);
    REQUIRE(payload["diff_changes"].get<int>() >= 0);
    REQUIRE(payload["check_passed"].get<bool>());
    REQUIRE(payload["reporter_count"].get<int>() == 5);
    REQUIRE(std::filesystem::exists(export_lcov));
    REQUIRE(std::filesystem::exists(export_xml));
    REQUIRE(std::filesystem::exists(export_json));
}

TEST_CASE("coverage lua check_threshold reports failures without throwing", "[coverage][lua_helpers]") {
    sol::state lua;
    teez::core::register_lua_helpers(lua);

    const auto lcov_path = (kFixturesDir / "sample.lcov").string();
    const auto result = lua.safe_script(R"(
        local table = coverage.load(")" + lcov_path + R"(")
        local check = coverage.check_threshold(table, { min_line_rate = 0.99 })
        return { passed = check.passed, failures = #check.failures }
    )");

    REQUIRE(result.valid());
    const sol::table payload = result.get<sol::table>();
    REQUIRE_FALSE(payload["passed"].get<bool>());
    REQUIRE(payload["failures"].get<int>() >= 1);
}

TEST_CASE("filter lua helpers roundtrip descriptors and filters", "[coverage][lua_helpers][filter]") {
    sol::state lua;
    teez::core::register_lua_helpers(lua);

    const auto result = lua.safe_script(R"(
        local descriptor = {
            file = "tests/unit/auth.teez.lua",
            type = "unit",
            suites = { "Auth" },
            name = "login works",
        }
        local filters = {
            types = { "unit" },
            name_glob = "login*",
        }
        return {
            glob = filter.glob_match("login works", "login*"),
            regex = filter.regex_match("login works", "^login"),
            id = filter.serialize_id(descriptor),
            template = filter.apply_template("${type}::${name}", descriptor),
            resolved = filter.resolve_type(descriptor.file, nil),
            matches = filter.matches(descriptor, filters),
        }
    )");

    REQUIRE(result.valid());
    const sol::table payload = result.get<sol::table>();
    REQUIRE(payload["glob"].get<bool>());
    REQUIRE(payload["regex"].get<bool>());
    REQUIRE(payload["id"].get<std::string>().find("login works") != std::string::npos);
    REQUIRE(payload["template"].get<std::string>() == "unit::login works");
    REQUIRE(payload["matches"].get<bool>());
}
