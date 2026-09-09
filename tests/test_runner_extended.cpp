#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "teez/core/discovery.hpp"
#include "teez/core/runner.hpp"
#include "teez/core/teez_config.hpp"

#ifndef TEEZ_PLUGIN_DIR
#error "TEEZ_PLUGIN_DIR must be defined"
#endif

namespace {

const std::filesystem::path kPluginsDir = TEEZ_PLUGIN_DIR;

std::filesystem::path make_dual_plugin_project(const char* suffix) {
    const auto project =
        std::filesystem::temp_directory_path() / ("teez_runner_extended_" + std::string(suffix));
    std::filesystem::remove_all(project);
    const auto plugins_dir = project / "plugins";
    std::filesystem::create_directories(plugins_dir);
    std::ofstream(project / "teez.config.lua") << R"(
return {
    plugins = { "alpha", "beta" },
    parallel_runners = true,
}
)";
    std::ofstream(project / "marker.txt") << "marker\n";

    const auto write_plugin = [&](const std::string& name) {
        std::ofstream(plugins_dir / (name + ".json")) << R"({
            "kind": "runner",
            "name": ")" + name + R"(",
            "plugin": ")" + name + R"(.lua",
            "anchors": ["marker.txt"],
            "priority": 100
        })";
        std::ofstream(plugins_dir / (name + ".lua"))
            << "function list(context)\n"
            << "    return { \"" << name << "::case\" }\n"
            << "end\n"
            << "function build_command(context)\n"
            << "    return { command = \"bash\", args = { \"-c\", \"echo " << name << "\" } }\n"
            << "end\n"
            << "function parse_line(line)\n"
            << "    return { event = \"pass\", id = \"" << name << "::case\" }\n"
            << "end\n";
    };

    write_plugin("alpha");
    write_plugin("beta");
    return project;
}

}  // namespace

TEST_CASE("run_context executes multiple configured runners in parallel", "[runner][integration]") {
    const auto project = make_dual_plugin_project("parallel");
    const auto config = teez::core::TeezConfig::resolve(
        {.search_dir = project, .target_path = project, .config_file = std::nullopt});
    const auto discovery = teez::core::make_discovery_context(config, kPluginsDir);

    teez::core::RunContext context{
        .command = "run",
        .target_path = project,
    };

    int pass_events = 0;
    const int exit_code = teez::core::run_context(discovery, context,
                                                  [&](const nlohmann::json& event) {
                                                      if (event.contains("event") &&
                                                          event.at("event") == "pass") {
                                                          ++pass_events;
                                                      }
                                                  });

    REQUIRE(exit_code == 0);
    REQUIRE(pass_events >= 2);
}

TEST_CASE("list_context merges tests from multiple configured runners in parallel",
          "[runner][integration]") {
    const auto project = make_dual_plugin_project("parallel_list");
    const auto config = teez::core::TeezConfig::resolve(
        {.search_dir = project, .target_path = project, .config_file = std::nullopt});
    const auto discovery = teez::core::make_discovery_context(config, kPluginsDir);

    teez::core::RunContext context{
        .command = "list",
        .target_path = project,
    };

    const auto tests = teez::core::list_context(discovery, context);
    REQUIRE(tests.size() == 2);
    REQUIRE(std::find(tests.begin(), tests.end(), "alpha::case") != tests.end());
    REQUIRE(std::find(tests.begin(), tests.end(), "beta::case") != tests.end());
}

TEST_CASE("run_with_plugin streams partial stdout lines", "[runner]") {
    const auto plugin_path =
        std::filesystem::temp_directory_path() / "teez_runner_linebuf_plugin.lua";
    std::ofstream(plugin_path) << R"(
function build_command(context)
    return { command = "bash", args = { "-c", "printf 'partial'" } }
end

function parse_line(line)
    return { event = "output", text = line }
end
)";

    teez::core::RunContext context{
        .command = "run",
        .target_path = ".",
    };

    std::ostringstream output;
    const int exit_code = teez::core::run_with_plugin(plugin_path, context, output);

    REQUIRE(exit_code == 0);
    REQUIRE(output.str().find("partial") != std::string::npos);
}
