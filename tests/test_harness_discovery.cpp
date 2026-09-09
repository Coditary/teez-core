#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "teez/core/discovery.hpp"

#ifndef TEEZ_PLUGIN_DIR
#error "TEEZ_PLUGIN_DIR must be defined"
#endif

namespace {

const std::filesystem::path kPluginsDir = TEEZ_PLUGIN_DIR;

}  // namespace

TEST_CASE("load_harness_manifests finds bundled harness plugins", "[discovery][harness]") {
    const auto manifests = teez::core::load_harness_manifests(kPluginsDir);

    REQUIRE_FALSE(manifests.empty());

    const auto has_name = [&](const std::string& name) {
        return std::any_of(manifests.begin(), manifests.end(),
                           [&](const teez::core::PluginManifest& manifest) {
                               return manifest.name == name;
                           });
    };

    REQUIRE(has_name("process"));
    REQUIRE(has_name("docker"));
    REQUIRE(has_name("kubernetes"));
    REQUIRE(has_name("mqtt-tools"));
    REQUIRE(has_name("hyperfine"));
}

TEST_CASE("find_harness_manifest returns process plugin", "[discovery][harness]") {
    const auto manifest = teez::core::find_harness_manifest("process", kPluginsDir);

    REQUIRE(manifest.has_value());
    REQUIRE(manifest->kind == "harness");
    REQUIRE(manifest->plugin_file.filename() == "teez-harness-process.lua");
}

TEST_CASE("discover_plugin ignores harness manifests", "[discovery][harness]") {
    const auto project = std::filesystem::temp_directory_path() / "teez_discover_harness_only";
    std::filesystem::remove_all(project);
    std::filesystem::create_directories(project);
    std::ofstream(project / "docker-compose.yml") << "services: {}\n";

    const auto plugin = teez::core::discover_plugin(project, kPluginsDir);

    REQUIRE_FALSE(plugin.has_value());
}

TEST_CASE("find_harness_manifest loads project-local harness plugins", "[discovery][harness]") {
    const auto project_harness =
        std::filesystem::temp_directory_path() / "teez_project_harness_plugins";
    std::filesystem::remove_all(project_harness);
    std::filesystem::create_directories(project_harness);

    std::ofstream(project_harness / "custom.json") << R"({
        "kind": "harness",
        "name": "custom",
        "plugin": "custom.lua",
        "priority": 100
    })";
    std::ofstream(project_harness / "custom.lua") << "function register(api) api.provide('custom', {}) end\n";

    const auto manifest = teez::core::find_harness_manifest("custom", kPluginsDir, project_harness);

    REQUIRE(manifest.has_value());
    REQUIRE(manifest->plugin_file.filename() == "custom.lua");
}
