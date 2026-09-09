#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include <sol/sol.hpp>

#include "teez/core/coverage_program.hpp"
#include "teez/core/lua_helpers.hpp"
#include "teez/core/teez_config.hpp"
#include "teez/core/plugin.hpp"

namespace {

const std::filesystem::path kFixturesDir =
    std::filesystem::path(__FILE__).parent_path() / "fixtures" / "coverage";

std::filesystem::path make_temp_dir(const char* suffix) {
    const auto dir = std::filesystem::temp_directory_path() / ("teez_coverage_program_" + std::string(suffix));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

}  // namespace

TEST_CASE("run_coverage_program loads report after command", "[coverage_program]") {
    const auto temp_dir = make_temp_dir("run");
    std::filesystem::copy_file(kFixturesDir / "sample.lcov", temp_dir / "coverage.lcov");

    teez::core::CoverageProgramSpec spec;
    spec.command.command = "true";
    spec.working_directory = temp_dir;
    spec.report_path = "coverage.lcov";
    spec.reporter = "msgpack";
    spec.output_path = "coverage.msgpack";

    const auto result = teez::core::run_coverage_program(spec);
    REQUIRE(result.execution.exit_code == 0);
    REQUIRE(result.table.find_file("/project/src/example.cpp") != nullptr);
    REQUIRE(result.exported_path.has_value());
    REQUIRE(std::filesystem::exists(*result.exported_path));
}

TEST_CASE("coverage.run_program lua helper returns rates", "[coverage_program][lua_helpers]") {
    const auto temp_dir = make_temp_dir("lua_run_program");
    std::filesystem::copy_file(kFixturesDir / "sample.lcov", temp_dir / "coverage.lcov");

    sol::state lua;
    teez::core::register_lua_helpers(lua);

    const auto cwd = temp_dir.string();
    const auto result = lua.safe_script(R"(
        return coverage.run_program({
            command = "true",
            cwd = ")" + cwd + R"(",
            report = "coverage.lcov",
            reporter = "msgpack",
            output = "out.msgpack",
        })
    )");

    REQUIRE(result.valid());
    const sol::table payload = result.get<sol::table>();
    REQUIRE(payload["exit_code"].get<int>() == 0);
    REQUIRE(payload["line_rate"].get<double>() > 0.0);
    REQUIRE(payload["exported_path"].get<std::string>().find("out.msgpack") != std::string::npos);
}

TEST_CASE("coverage.run_profile merges config defaults and profile", "[coverage_program][lua_helpers]") {
    const auto temp_dir = make_temp_dir("run_profile");
    {
        std::ofstream config_file(temp_dir / "teez.config.lua");
        config_file << "return {\n"
                       "  coverage = {\n"
                       "    reporter = \"msgpack\",\n"
                       "    min_line_rate = 0.5,\n"
                       "    profiles = {\n"
                       "      demo = {\n"
                       "        command = \"true\",\n"
                       "        cwd = \"" +
                           temp_dir.string() + "\",\n"
                                               "        report = \"coverage.lcov\",\n"
                                               "        output = \"profile.msgpack\",\n"
                                               "      },\n"
                                               "    },\n"
                                               "  },\n"
                                               "}\n";
    }
    std::filesystem::copy_file(kFixturesDir / "sample.lcov", temp_dir / "coverage.lcov");
    const auto loaded = teez::core::TeezConfig::load(temp_dir);
    REQUIRE_FALSE(loaded.empty());
    REQUIRE(loaded.data()["coverage"].contains("profiles"));
    teez::core::set_active_config(std::move(loaded));
    REQUIRE(teez::core::coverage_profile_names().size() == 1);

    sol::state lua;
    teez::core::register_lua_helpers(lua);

    const auto names = lua.safe_script("return coverage.profile_names()");
    REQUIRE(names.valid());
    const sol::table profile_names = names.get<sol::table>();
    REQUIRE(profile_names.size() == 1);
    REQUIRE(profile_names[1].get<std::string>() == "demo");

    const auto result = lua.safe_script("return coverage.run_profile(\"demo\")");
    REQUIRE(result.valid());
    const sol::table payload = result.get<sol::table>();
    REQUIRE(payload["exit_code"].get<int>() == 0);
    REQUIRE(payload["line_rate"].get<double>() > 0.0);
    REQUIRE(payload["threshold_passed"].get<bool>());
    REQUIRE(payload["exported_path"].get<std::string>().find("profile.msgpack") != std::string::npos);
}

TEST_CASE("Plugin build_coverage_run parses optional coverage spec", "[coverage_program][plugin]") {
    const auto temp_dir = make_temp_dir("plugin");
    const auto plugin_path = temp_dir / "coverage_plugin.lua";
    std::ofstream(plugin_path) << R"(
function build_coverage_run(context)
    return {
        command = "true",
        cwd = context.target_path,
        report = "coverage.lcov",
        reporter = "json",
    }
end
)";

    std::filesystem::copy_file(kFixturesDir / "sample.lcov", temp_dir / "coverage.lcov");

    teez::core::Plugin plugin(plugin_path);
    REQUIRE(plugin.supports_coverage_run());

    teez::core::RunContext context;
    context.command = "run";
    context.target_path = temp_dir;

    const auto result = plugin.run_with_coverage(context);
    REQUIRE(result.table.find_file("/project/src/example.cpp") != nullptr);
}
