#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "teez/core/coverage_program.hpp"
#include "teez/core/plugin.hpp"
#include "teez/core/runner_config.hpp"

#ifndef TEEZ_PLUGIN_DIR
#error "TEEZ_PLUGIN_DIR must be defined"
#endif

namespace {

const std::filesystem::path kCtestPlugin =
    std::filesystem::path(TEEZ_PLUGIN_DIR) / "teez-plugin-ctest.lua";

}  // namespace

TEST_CASE("ctest plugin parse_line maps Passed to pass event", "[ctest][plugin]") {
    const std::string line =
        " 1/30 Test  #1: load_manifests reads pytest and worker plugin manifests .........   "
        "Passed    0.00 sec";

    const auto json = teez::core::call_parse_line(kCtestPlugin, line);

    REQUIRE(json.find("\"event\":\"pass\"") != std::string::npos);
    REQUIRE(json.find("load_manifests reads pytest and worker plugin manifests") !=
            std::string::npos);
}

TEST_CASE("ctest plugin parse_line maps Failed to fail event", "[ctest][plugin]") {
    const std::string line =
        "25/30 Test #25: broken test ................................................***Failed    "
        "0.01 sec";

    const auto json = teez::core::call_parse_line(kCtestPlugin, line);

    REQUIRE(json.find("\"event\":\"fail\"") != std::string::npos);
    REQUIRE(json.find("broken test") != std::string::npos);
    REQUIRE(json.find("\"msg\":\"Failed\"") != std::string::npos);
}

TEST_CASE("ctest plugin parse_line maps Not Run to skip event", "[ctest][plugin]") {
    const std::string line =
        "4/4 Test #4: teez_core_tests_NOT_BUILT-b12d07c .....................***Not Run   0.00 sec";

    const auto json = teez::core::call_parse_line(kCtestPlugin, line);

    REQUIRE(json.find("\"event\":\"skip\"") != std::string::npos);
}

TEST_CASE("ctest plugin parse_line maps Start to start event", "[ctest][plugin]") {
    const auto json = teez::core::call_parse_line(kCtestPlugin, "      Start  1: some test");

    REQUIRE(json.find("\"event\":\"start\"") != std::string::npos);
    REQUIRE(json.find("some test") != std::string::npos);
}

TEST_CASE("ctest plugin parse_line includes duration_ms from ctest timing", "[ctest][plugin]") {
    const std::string line =
        " 1/30 Test  #1: load_manifests reads pytest and worker plugin manifests .........   "
        "Passed    0.42 sec";

    const auto json = teez::core::call_parse_line(kCtestPlugin, line);

    REQUIRE(json.find("\"duration_ms\":420") != std::string::npos);
}

TEST_CASE("ctest plugin build_command targets build directory", "[ctest][plugin]") {
    const auto build_dir = std::filesystem::temp_directory_path() / "teez_ctest_plugin_build";
    std::filesystem::create_directories(build_dir);
    std::ofstream(build_dir / "CTestTestfile.cmake") << "# test\n";

    teez::core::RunContext ctx{
        .command = "run",
        .target_path = build_dir,
    };

    const auto spec = teez::core::call_build_command(kCtestPlugin, ctx);

    REQUIRE(spec.command == "ctest");
    REQUIRE_FALSE(spec.args.empty());
    REQUIRE(spec.args[0] == "--test-dir");
    REQUIRE(spec.args[1] == build_dir.string());
    REQUIRE(std::find(spec.args.begin(), spec.args.end(), "--output-on-failure") != spec.args.end());
}

TEST_CASE("ctest plugin build_command reads runners.ctest options", "[ctest][plugin]") {
    const auto project_dir = std::filesystem::temp_directory_path() / "teez_ctest_plugin_runner_build";
    const auto build_dir = project_dir / "build";
    std::filesystem::create_directories(build_dir);
    std::ofstream(build_dir / "CTestTestfile.cmake") << "# test\n";

    teez::core::RunContext ctx{
        .command = "run",
        .target_path = project_dir,
        .runner_name = "ctest",
        .runner_options =
            teez::core::RunnerOptions{
                .exclude = {"slow"},
                .config = nlohmann::json::object({{"build_dir", "build"}}),
            },
    };

    const auto spec = teez::core::call_build_command(kCtestPlugin, ctx);

    REQUIRE(spec.command == "ctest");
    REQUIRE(spec.args[0] == "--test-dir");
    REQUIRE(spec.args[1] == build_dir.string());
    const auto exclude_it = std::find(spec.args.begin(), spec.args.end(), "-E");
    REQUIRE(exclude_it != spec.args.end());
    REQUIRE(*(exclude_it + 1) == "slow");
}

TEST_CASE("ctest plugin build_command prefers CLI regex over config", "[ctest][plugin]") {
    const auto build_dir = std::filesystem::temp_directory_path() / "teez_ctest_plugin_cli_build";
    std::filesystem::create_directories(build_dir);
    std::ofstream(build_dir / "CTestTestfile.cmake") << "# test\n";

    teez::core::RunContext ctx{
        .command = "run",
        .target_path = build_dir,
        .ctest =
            {
                .regex = "discovery",
                .exclude = "integration",
            },
    };

    const auto spec = teez::core::call_build_command(kCtestPlugin, ctx);

    REQUIRE(spec.command == "ctest");
    const auto regex_it = std::find(spec.args.begin(), spec.args.end(), "-R");
    REQUIRE(regex_it != spec.args.end());
    REQUIRE(*(regex_it + 1) == "discovery");
    const auto exclude_it = std::find(spec.args.begin(), spec.args.end(), "-E");
    REQUIRE(exclude_it != spec.args.end());
    REQUIRE(*(exclude_it + 1) == "integration");
}

TEST_CASE("ctest plugin build_coverage_run applies defaults without runner config", "[ctest][plugin][coverage]") {
    const auto build_dir = std::filesystem::temp_directory_path() / "teez_ctest_plugin_coverage_build";
    std::filesystem::create_directories(build_dir);
    std::ofstream(build_dir / "CTestTestfile.cmake") << "# test\n";

    teez::core::RunContext ctx{
        .command = "run",
        .target_path = build_dir,
    };

    teez::core::Plugin plugin(kCtestPlugin);
    REQUIRE(plugin.supports_coverage_run());

    const auto spec = plugin.build_coverage_run(ctx);
    REQUIRE(spec.has_value());
    REQUIRE(spec->command.command == "ctest");
    REQUIRE(spec->working_directory == build_dir);
    REQUIRE(spec->report_path == "coverage/lcov.info");
    REQUIRE(spec->output_path == "coverage/teez.msgpack");
    REQUIRE(spec->reporter == "msgpack");
    REQUIRE_FALSE(spec->min_line_rate.has_value());
    REQUIRE(spec->collect_command.has_value());
    REQUIRE(spec->collect_command->command == "gcovr");
    REQUIRE_FALSE(spec->collect_command->args.empty());
    REQUIRE(spec->collect_command->args.front() == "--root");
}

TEST_CASE("ctest plugin build_coverage_run merges runner coverage overrides", "[ctest][plugin][coverage]") {
    const auto build_dir =
        std::filesystem::temp_directory_path() / "teez_ctest_plugin_coverage_override_build";
    std::filesystem::create_directories(build_dir);
    std::ofstream(build_dir / "CTestTestfile.cmake") << "# test\n";

    teez::core::RunContext ctx{
        .command = "run",
        .target_path = build_dir,
        .runner_name = "ctest",
        .runner_options =
            teez::core::RunnerOptions{
                .config = nlohmann::json::object({
                    {"coverage",
                     nlohmann::json::object({
                         {"min_line_rate", 0.75},
                         {"output", "coverage/custom.msgpack"},
                     })},
                }),
            },
    };

    teez::core::Plugin plugin(kCtestPlugin);
    const auto spec = plugin.build_coverage_run(ctx);
    REQUIRE(spec.has_value());
    REQUIRE(spec->output_path == "coverage/custom.msgpack");
    REQUIRE(spec->min_line_rate.has_value());
    REQUIRE(*spec->min_line_rate == 0.75);
}

TEST_CASE("ctest plugin build_command appends runner args", "[ctest][plugin]") {
    const auto build_dir = std::filesystem::temp_directory_path() / "teez_ctest_plugin_args_build";
    std::filesystem::create_directories(build_dir);
    std::ofstream(build_dir / "CTestTestfile.cmake") << "# test\n";

    teez::core::RunContext ctx{
        .command = "run",
        .target_path = build_dir,
        .runner_name = "ctest",
        .runner_options =
            teez::core::RunnerOptions{
                .config = nlohmann::json::object({{"args", nlohmann::json::array({"-V", "--progress"})}}),
            },
    };

    const auto spec = teez::core::call_build_command(kCtestPlugin, ctx);

    REQUIRE(std::find(spec.args.begin(), spec.args.end(), "-V") != spec.args.end());
    REQUIRE(std::find(spec.args.begin(), spec.args.end(), "--progress") != spec.args.end());
}
