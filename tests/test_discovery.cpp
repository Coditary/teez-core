#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <algorithm>
#include <fstream>

#include "teez/core/discovery.hpp"
#include "teez/core/plugin_config.hpp"
#include "teez/core/teez_config.hpp"

#ifndef TEEZ_PLUGIN_DIR
#error "TEEZ_PLUGIN_DIR must be defined"
#endif

namespace {

const std::filesystem::path kPluginsDir = TEEZ_PLUGIN_DIR;

std::filesystem::path make_project(const std::string& name, const std::string& anchor_content,
                                   const std::string& anchor_name) {
    const auto project_dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::create_directories(project_dir);
    std::ofstream(project_dir / anchor_name) << anchor_content;
    return project_dir;
}

}  // namespace

TEST_CASE("load_manifests reads pytest and worker plugin manifests", "[discovery]") {
    const auto manifests = teez::core::load_manifests(kPluginsDir);

    REQUIRE_FALSE(manifests.empty());

    const auto has_name = [&](const std::string& name) {
        return std::any_of(manifests.begin(), manifests.end(),
                           [&](const teez::core::PluginManifest& manifest) {
                               return manifest.name == name;
                           });
    };

    REQUIRE(has_name("pytest"));
    REQUIRE(has_name("ctest"));
    REQUIRE(has_name("teez-worker"));
    REQUIRE(manifests.front().name == "teez-worker");

    const auto worker = std::find_if(manifests.begin(), manifests.end(),
                                     [](const teez::core::PluginManifest& manifest) {
                                         return manifest.name == "teez-worker";
                                     });
    REQUIRE(worker != manifests.end());
    REQUIRE_FALSE(worker->include.empty());
    REQUIRE(worker->include.front() == "**/*.teez.lua");
    REQUIRE_FALSE(worker->exclude.empty());
}

TEST_CASE("discover_plugin matches pytest.ini in project root", "[discovery]") {
    const auto project = make_project("teez_discover_pytest", "[pytest]\n", "pytest.ini");

    const auto plugin = teez::core::discover_plugin(project, kPluginsDir);

    REQUIRE(plugin.has_value());
    REQUIRE(plugin->filename() == "teez-plugin-pytest.lua");
}

TEST_CASE("discover_plugin matches CTestTestfile.cmake in build directory", "[discovery]") {
    const auto project = make_project("teez_discover_ctest", "# cmake\n", "CTestTestfile.cmake");

    const auto plugin = teez::core::discover_plugin(project, kPluginsDir);

    REQUIRE(plugin.has_value());
    REQUIRE(plugin->filename() == "teez-plugin-ctest.lua");
}

TEST_CASE("discover_plugin returns nullopt when no anchors match", "[discovery]") {
    const auto project = std::filesystem::temp_directory_path() / "teez_discover_empty";
    std::filesystem::create_directories(project);

    const auto plugin = teez::core::discover_plugin(project, kPluginsDir);

    REQUIRE_FALSE(plugin.has_value());
}

TEST_CASE("discover_plugin matches glob anchor for teez.lua files", "[discovery]") {
    const auto project = std::filesystem::temp_directory_path() / "teez_discover_worker";
    std::filesystem::create_directories(project);
    std::ofstream(project / "smoke.teez.lua") << "-- test\n";

    const auto plugin = teez::core::discover_plugin(project, kPluginsDir);

    REQUIRE(plugin.has_value());
    REQUIRE(plugin->filename() == "teez-plugin-worker.lua");
}

TEST_CASE("find_runner_match reports matched glob anchors", "[discovery]") {
    const auto project = std::filesystem::temp_directory_path() / "teez_discover_reasons";
    std::filesystem::create_directories(project);
    std::ofstream(project / "smoke.teez.lua") << "-- test\n";

    const auto match = teez::core::find_runner_match(project, kPluginsDir);

    REQUIRE(match.has_value());
    REQUIRE(match->manifest.name == "teez-worker");
    REQUIRE_FALSE(match->reasons.empty());
    REQUIRE(match->reasons.front().kind == "glob_anchor");
    REQUIRE(match->reasons.front().value == "*.teez.lua");
}

TEST_CASE("collect_match_reasons lists file anchors", "[discovery]") {
    const auto project = make_project("teez_discover_anchor_reason", "[pytest]\n", "pytest.ini");

    const auto manifests = teez::core::load_manifests(kPluginsDir);
    const auto pytest_it = std::find_if(manifests.begin(), manifests.end(),
                                        [](const teez::core::PluginManifest& manifest) {
                                            return manifest.name == "pytest";
                                        });
    REQUIRE(pytest_it != manifests.end());

    const auto reasons = teez::core::collect_match_reasons(project, *pytest_it);
    REQUIRE(reasons.size() == 1);
    REQUIRE(reasons.front().kind == "anchor");
    REQUIRE(reasons.front().value == "pytest.ini");
}

TEST_CASE("glob_match finds teez.lua files in directory", "[discovery]") {
    const auto project = std::filesystem::temp_directory_path() / "teez_glob_worker";
    std::filesystem::create_directories(project);
    std::ofstream(project / "api.teez.lua") << "-- test\n";

    const auto matches = teez::core::glob_match(project, "*.teez.lua");

    REQUIRE(matches.size() == 1);
    REQUIRE(matches.front().filename() == "api.teez.lua");
}

TEST_CASE("glob_match returns empty list for missing directory", "[discovery]") {
    const auto matches =
        teez::core::glob_match("/tmp/teez-discovery-missing-directory", "*.teez.lua");

    REQUIRE(matches.empty());
}

TEST_CASE("glob_match returns empty list for directory without teez.lua files", "[discovery]") {
    const auto project = std::filesystem::temp_directory_path() / "teez_glob_empty_dir";
    std::filesystem::remove_all(project);
    std::filesystem::create_directories(project);
    std::ofstream(project / "readme.txt") << "x\n";

    const auto matches = teez::core::glob_match(project, "*.teez.lua");

    REQUIRE(matches.empty());
    std::filesystem::remove_all(project);
}

TEST_CASE("find_all_runner_matches only returns pytest for pytest demo fixture", "[discovery]") {
    const auto fixture = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
                         "fixtures" / "pytest-demo";
    if (!std::filesystem::exists(fixture / "pytest.ini")) {
        SKIP("pytest demo fixture missing");
    }

    const auto matches =
        teez::core::find_all_runner_matches(fixture, teez::core::DiscoveryContext::bundled_only(kPluginsDir));

    REQUIRE(matches.size() == 1);
    REQUIRE(matches.front().manifest.name == "pytest");
}

TEST_CASE("glob_match matches direct file path", "[discovery]") {
    const auto file = std::filesystem::temp_directory_path() / "direct.teez.lua";
    std::ofstream(file) << "-- test\n";

    const auto matches = teez::core::glob_match(file, "*.teez.lua");

    REQUIRE(matches.size() == 1);
    REQUIRE(matches.front() == file);
}

TEST_CASE("glob_match supports exact filename patterns", "[discovery]") {
    const auto project = std::filesystem::temp_directory_path() / "teez_glob_exact";
    std::filesystem::create_directories(project);
    std::ofstream(project / "exact-match.txt") << "data\n";

    const auto matches = teez::core::glob_match(project, "exact-match.txt");

    REQUIRE(matches.size() == 1);
}

TEST_CASE("load_manifests returns empty list for missing plugins dir", "[discovery]") {
    const auto manifests = teez::core::load_manifests("/tmp/teez-missing-plugins-dir");

    REQUIRE(manifests.empty());
}

TEST_CASE("load_manifests throws on unreadable manifest file", "[discovery]") {
    const auto plugins_dir = std::filesystem::temp_directory_path() / "teez_unreadable_manifests";
    std::filesystem::remove_all(plugins_dir);
    std::filesystem::create_directories(plugins_dir);

    const auto manifest = plugins_dir / "broken.json";
    std::ofstream(manifest) << R"({"name":"broken","plugin":"x.lua"})";
    std::filesystem::permissions(manifest, std::filesystem::perms::none);

    REQUIRE_THROWS_AS(teez::core::load_manifests(plugins_dir), std::runtime_error);

    std::filesystem::permissions(manifest, std::filesystem::perms::owner_all);
}

TEST_CASE("find_all_runner_matches returns worker and pytest for mixed project", "[discovery]") {
    const auto project = std::filesystem::temp_directory_path() / "teez_mixed_runners";
    std::filesystem::remove_all(project);
    std::filesystem::create_directories(project);
    std::ofstream(project / "pytest.ini") << "[pytest]\n";
    std::ofstream(project / "smoke.teez.lua") << "-- test\n";

    const auto matches = teez::core::find_all_runner_matches(project, kPluginsDir);
    REQUIRE(matches.size() == 2);

    const auto has_name = [&](const std::string& name) {
        return std::any_of(matches.begin(), matches.end(),
                           [&](const teez::core::DiscoveryMatch& match) {
                               return match.manifest.name == name;
                           });
    };
    REQUIRE(has_name("teez-worker"));
    REQUIRE(has_name("pytest"));
}

TEST_CASE("find_all_runner_matches adds implicit worker for explicit plugins", "[discovery]") {
    const auto root = std::filesystem::temp_directory_path() / "teez_plugins_with_worker";
    std::filesystem::remove_all(root);
    const auto plugins_dir = root / "plugins";
    std::filesystem::create_directories(plugins_dir);
    std::ofstream(root / "teez.config.lua")
        << "return { plugins = { \"custom\" } }\n";
    std::ofstream(root / "smoke.teez.lua") << "-- test\n";
    std::ofstream(plugins_dir / "custom.json") << R"({
        "kind": "runner",
        "name": "custom",
        "plugin": "custom.lua",
        "priority": 200
    })";
    std::ofstream(plugins_dir / "custom.lua")
        << "function build_command() return { command = \"true\", args = {} } end\n"
        << "function parse_line() return nil end\n";

    const auto config = teez::core::TeezConfig::resolve(
        {.search_dir = root, .target_path = root, .config_file = std::nullopt});
    const auto discovery = teez::core::make_discovery_context(config, kPluginsDir);
    const auto matches = teez::core::find_all_runner_matches(root, discovery);

    REQUIRE(matches.size() == 2);
    REQUIRE(matches.front().manifest.name == "custom");
    REQUIRE(matches.back().manifest.name == "teez-worker");
}

TEST_CASE("find_runner_match uses configured plugin from project plugins dir", "[discovery]") {
    const auto root = std::filesystem::temp_directory_path() / "teez_configured_plugin";
    std::filesystem::remove_all(root);
    const auto plugins_dir = root / "plugins";
    std::filesystem::create_directories(plugins_dir);
    std::ofstream(root / "teez.config.lua")
        << "return { plugins = { { name = \"custom\", version = \"1\" } } }\n";
    std::ofstream(root / "pytest.ini") << "[pytest]\n";
    std::ofstream(plugins_dir / "custom.json") << R"({
        "kind": "runner",
        "name": "custom",
        "version": "1",
        "plugin": "custom.lua",
        "priority": 200
    })";
    std::ofstream(plugins_dir / "custom.lua")
        << "function build_command() return { command = \"true\", args = {} } end\n"
        << "function parse_line() return nil end\n";

    const auto config = teez::core::TeezConfig::resolve(
        {.search_dir = root, .target_path = root, .config_file = std::nullopt});
    const auto discovery = teez::core::make_discovery_context(config, kPluginsDir);
    const auto match = teez::core::find_runner_match(root, discovery);

    REQUIRE(match.has_value());
    REQUIRE(match->manifest.name == "custom");
    REQUIRE(match->manifest.version == "1");
    REQUIRE(match->reasons.front().kind == "config");
}

TEST_CASE("find_harness_manifest loads harness from project plugins dir", "[discovery]") {
    const auto root = std::filesystem::temp_directory_path() / "teez_plugins_harness";
    std::filesystem::remove_all(root);
    const auto plugins_dir = root / "plugins";
    std::filesystem::create_directories(plugins_dir);
    std::ofstream(root / "teez.config.lua") << "return { harnesses = { \"custom\" } }\n";
    std::ofstream(plugins_dir / "custom.json") << R"({
        "kind": "harness",
        "name": "custom",
        "plugin": "custom.lua",
        "priority": 100
    })";
    std::ofstream(plugins_dir / "custom.lua")
        << "function register(api) api.provide('custom', {}) end\n";

    const auto config = teez::core::TeezConfig::resolve(
        {.search_dir = root, .target_path = root, .config_file = std::nullopt});
    const auto discovery = teez::core::make_discovery_context(config, kPluginsDir);
    const auto manifest = teez::core::find_harness_manifest("custom", discovery, root);

    REQUIRE(manifest.has_value());
    REQUIRE(manifest->plugin_file.filename() == "custom.lua");
}

TEST_CASE("discover_plugin skips manifests with missing plugin files", "[discovery]") {
    const auto plugins_dir = std::filesystem::temp_directory_path() / "teez_broken_manifests";
    std::filesystem::remove_all(plugins_dir);
    std::filesystem::create_directories(plugins_dir);

    const auto project = std::filesystem::temp_directory_path() / "teez_broken_manifest_project";
    std::filesystem::create_directories(project);
    std::ofstream(project / "pytest.ini") << "[pytest]\n";

    std::ofstream(plugins_dir / "broken.json") << R"({
        "name": "broken",
        "plugin": "missing-plugin.lua",
        "anchors": ["pytest.ini"],
        "priority": 999
    })";

    const auto plugin = teez::core::discover_plugin(project, plugins_dir);

    REQUIRE_FALSE(plugin.has_value());
}
