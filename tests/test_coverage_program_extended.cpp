#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include <sol/sol.hpp>

#include "teez/core/coverage_program.hpp"
#include "teez/core/lua_helpers.hpp"
#include "teez/core/teez_config.hpp"

namespace {

const std::filesystem::path kFixturesDir =
    std::filesystem::path(__FILE__).parent_path() / "fixtures" / "coverage";

std::filesystem::path make_temp_dir(const char* suffix) {
    const auto dir =
        std::filesystem::temp_directory_path() / ("teez_coverage_program_ext_" + std::string(suffix));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

}  // namespace

TEST_CASE("run_coverage_program runs collect command and enforces thresholds", "[coverage_program]") {
    const auto temp_dir = make_temp_dir("collect");
    std::filesystem::copy_file(kFixturesDir / "sample.lcov", temp_dir / "coverage.lcov");

    teez::core::CoverageProgramSpec spec;
    spec.command.command = "true";
    spec.collect_command = teez::core::CommandSpec{.command = "true"};
    spec.working_directory = temp_dir;
    spec.report_path = "coverage.lcov";
    spec.min_line_rate = 0.1;
    spec.reporter = "json";
    spec.output_path = "coverage/report.json";

    const auto result = teez::core::run_coverage_program(spec);
    REQUIRE(result.execution.exit_code == 0);
    REQUIRE(result.threshold.passed);
    REQUIRE(result.exported_path.has_value());
    REQUIRE(std::filesystem::exists(*result.exported_path));
}

TEST_CASE("coverage_program_spec_from_config lists available profiles on error", "[coverage_program]") {
    const auto temp_dir = make_temp_dir("profiles");
    std::ofstream(temp_dir / "teez.config.lua") << R"(
return {
  coverage = {
    profiles = {
      demo = { command = "true", report = "coverage.lcov" },
    },
  },
}
)";
    teez::core::set_active_config(teez::core::TeezConfig::load(temp_dir));

    sol::state lua;
    REQUIRE_THROWS_AS(teez::core::coverage_program_spec_from_config("missing", std::nullopt, lua),
                      std::runtime_error);
}

TEST_CASE("coverage.run_program lua helper fails on threshold violation", "[coverage_program][lua]") {
    const auto temp_dir = make_temp_dir("threshold");
    std::filesystem::copy_file(kFixturesDir / "sample.lcov", temp_dir / "coverage.lcov");

    sol::state lua;
    teez::core::register_lua_helpers(lua);

    const auto cwd = temp_dir.string();
    const auto result = lua.safe_script(R"(
        local ok, err = pcall(function()
            coverage.run_program({
                command = "true",
                cwd = ")" + cwd + R"(",
                report = "coverage.lcov",
                min_line_rate = 0.99,
            })
        end)
        return { ok = ok, err = tostring(err) }
    )");

    REQUIRE(result.valid());
    const sol::table payload = result.get<sol::table>();
    REQUIRE_FALSE(payload["ok"].get<bool>());
    REQUIRE(payload["err"].get<std::string>().find("threshold") != std::string::npos);
}
