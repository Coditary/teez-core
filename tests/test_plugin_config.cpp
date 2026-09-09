#include <catch2/catch_test_macros.hpp>

#include <fstream>

#include "teez/core/plugin_config.hpp"
#include "teez/core/teez_config.hpp"

namespace {

std::filesystem::path write_config(const std::filesystem::path& dir, const std::string& content) {
    std::filesystem::create_directories(dir);
    const auto path = dir / "teez.config.lua";
    std::ofstream(path) << content;
    return path;
}

}  // namespace

TEST_CASE("parse_config_plugins accepts plugins list and legacy plugin", "[plugin_config]") {
    const nlohmann::json plugins_list = {
        {"plugins", nlohmann::json::array({"pytest", {{"name", "vitest"}, {"version", "1"}}})},
    };
    const auto parsed_list = teez::core::parse_config_plugins(plugins_list);
    REQUIRE(parsed_list.size() == 2);
    REQUIRE(parsed_list[0].name == "pytest");
    REQUIRE_FALSE(parsed_list[0].version.has_value());
    REQUIRE(parsed_list[1].name == "vitest");
    REQUIRE(parsed_list[1].version == "1");

    const nlohmann::json legacy_plugin = {{"plugin", "worker"}};
    const auto parsed_legacy = teez::core::parse_config_plugins(legacy_plugin);
    REQUIRE(parsed_legacy.size() == 1);
    REQUIRE(parsed_legacy.front().name == "worker");
}

TEST_CASE("parse_config_harnesses accepts harnesses list and legacy harness", "[plugin_config]") {
    const nlohmann::json harnesses_list = {
        {"harnesses", nlohmann::json::array({"process", {{"name", "docker"}, {"version", "2"}}})},
    };
    const auto parsed_list = teez::core::parse_config_harnesses(harnesses_list);
    REQUIRE(parsed_list.size() == 2);
    REQUIRE(parsed_list[0].name == "process");
    REQUIRE(parsed_list[1].name == "docker");
    REQUIRE(parsed_list[1].version == "2");

    const nlohmann::json legacy_harness = {{"harness", "hyperfine"}};
    const auto parsed_legacy = teez::core::parse_config_harnesses(legacy_harness);
    REQUIRE(parsed_legacy.size() == 1);
    REQUIRE(parsed_legacy.front().name == "hyperfine");
}

TEST_CASE("make_discovery_context reads parallel_runners", "[plugin_config]") {
    const auto root = std::filesystem::temp_directory_path() / "teez_parallel_runners_root";
    std::filesystem::remove_all(root);
    write_config(root, "return { parallel_runners = false }\n");

    const auto config = teez::core::TeezConfig::resolve(
        {.search_dir = root, .target_path = root, .config_file = std::nullopt});
    const auto discovery = teez::core::make_discovery_context(config, "/bundled/plugins");

    REQUIRE_FALSE(discovery.parallel_runners);
    REQUIRE(teez::core::parse_parallel_runners(nlohmann::json::object()));
}

TEST_CASE("make_discovery_context parses runners table", "[plugin_config]") {
    const auto root = std::filesystem::temp_directory_path() / "teez_runner_config_root";
    std::filesystem::remove_all(root);
    write_config(root, R"(
return {
    runners = {
        vitest = {
            include = { "tests/**/*.test.ts" },
            exclude = { "**/*.teez.lua" },
        },
    },
}
)");

    const auto config = teez::core::TeezConfig::resolve(
        {.search_dir = root, .target_path = root, .config_file = std::nullopt});
    const auto discovery = teez::core::make_discovery_context(config, "/bundled/plugins");

    REQUIRE(discovery.runners.size() == 1);
    REQUIRE(discovery.runners.at("vitest").include.front() == "tests/**/*.test.ts");
    REQUIRE(discovery.runners.at("vitest").exclude.front() == "**/*.teez.lua");
}

TEST_CASE("make_discovery_context resolves project plugins dir", "[plugin_config]") {
    const auto root = std::filesystem::temp_directory_path() / "teez_plugin_config_root";
    std::filesystem::remove_all(root);
    write_config(root, R"(
return {
    plugins_dir = "custom-plugins",
    plugins = { { name = "worker" } },
    harnesses = { "process" },
}
)");

    const auto config = teez::core::TeezConfig::resolve(
        {.search_dir = root, .target_path = root, .config_file = std::nullopt});

    const auto discovery = teez::core::make_discovery_context(config, "/bundled/plugins");
    REQUIRE(discovery.bundled_plugins_dir == "/bundled/plugins");
    REQUIRE(discovery.project_plugin_dirs.size() == 1);
    REQUIRE(discovery.project_plugin_dirs.front() ==
            std::filesystem::absolute(root / "custom-plugins").lexically_normal());
    REQUIRE(discovery.plugins.size() == 1);
    REQUIRE(discovery.plugins.front().name == "worker");
    REQUIRE(discovery.harnesses.size() == 1);
    REQUIRE(discovery.harnesses.front().name == "process");
}

TEST_CASE("resolve_project_plugin_dirs includes legacy .teez/plugins when present", "[plugin_config]") {
    const auto root = std::filesystem::temp_directory_path() / "teez_legacy_plugins_dir";
    std::filesystem::remove_all(root);
    write_config(root, "return { profile = \"local\" }\n");
    std::filesystem::create_directories(root / ".teez" / "plugins");

    const auto config = teez::core::TeezConfig::resolve(
        {.search_dir = root, .target_path = root, .config_file = std::nullopt});
    const auto dirs = teez::core::resolve_project_plugin_dirs(config);

    REQUIRE(dirs.size() == 2);
    REQUIRE(dirs[0] == std::filesystem::absolute(root / "plugins").lexically_normal());
    REQUIRE(dirs[1] == std::filesystem::absolute(root / ".teez" / "plugins").lexically_normal());
}
