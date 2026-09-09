#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "teez/core/plugin_config.hpp"

namespace teez::core {

struct PluginManifest {
    std::string name;
    std::string kind = "runner";
    std::optional<std::string> version;
    std::filesystem::path plugin_file;
    std::vector<std::string> anchors;
    std::vector<std::string> glob_anchors;
    std::vector<std::string> tool_requires;
    std::vector<std::string> include;
    std::vector<std::string> exclude;
    int priority = 0;
};

struct DiscoveryMatchReason {
    std::string kind;
    std::string value;
};

struct DiscoveryMatch {
    PluginManifest manifest;
    std::vector<DiscoveryMatchReason> reasons;
};

bool is_runner_manifest(const PluginManifest& manifest);
bool is_harness_manifest(const PluginManifest& manifest);

/// Loads all *.json manifest files from plugins_dir.
std::vector<PluginManifest> load_manifests(const std::filesystem::path& plugins_dir);

/// Returns files in project_dir matching a glob like "*.teez.lua".
std::vector<std::filesystem::path> glob_match(const std::filesystem::path& project_dir,
                                              const std::string& pattern);

/// Returns the plugin .lua path for the best matching manifest in project_dir.
std::optional<std::filesystem::path> discover_plugin(const std::filesystem::path& project_dir,
                                                     const DiscoveryContext& discovery);

std::optional<std::filesystem::path> discover_plugin(const std::filesystem::path& project_dir,
                                                     const std::filesystem::path& plugins_dir);

/// Returns the best matching runner manifest and why it matched.
std::optional<DiscoveryMatch> find_runner_match(const std::filesystem::path& project_dir,
                                                const DiscoveryContext& discovery);

std::optional<DiscoveryMatch> find_runner_match(const std::filesystem::path& project_dir,
                                                const std::filesystem::path& plugins_dir);

/// Returns all runner plugins that should execute for project_dir.
std::vector<DiscoveryMatch> find_all_runner_matches(const std::filesystem::path& project_dir,
                                                    const DiscoveryContext& discovery);

std::vector<DiscoveryMatch> find_all_runner_matches(const std::filesystem::path& project_dir,
                                                    const std::filesystem::path& plugins_dir);

std::optional<PluginManifest> find_manifest_by_name(const std::string& name,
                                                    const std::optional<std::string>& version,
                                                    const std::string& kind,
                                                    const DiscoveryContext& discovery);

/// Lists concrete anchor/glob patterns that matched for a manifest.
std::vector<DiscoveryMatchReason> collect_match_reasons(const std::filesystem::path& project_dir,
                                                        const PluginManifest& manifest);

/// Returns true when an executable with this name exists on PATH.
bool is_tool_available(const std::string& tool_name);

/// Returns harness manifests from bundled and project plugin directories.
std::vector<PluginManifest> load_harness_manifests(const DiscoveryContext& discovery,
                                                   const std::filesystem::path& project_root = {});

std::vector<PluginManifest>
load_harness_manifests(const std::filesystem::path& plugins_dir,
                       const std::filesystem::path& project_harness_dir = {});

/// Finds a harness manifest by name.
std::optional<PluginManifest> find_harness_manifest(const std::string& name,
                                                    const DiscoveryContext& discovery,
                                                    const std::filesystem::path& project_root = {});

std::optional<PluginManifest>
find_harness_manifest(const std::string& name, const std::filesystem::path& plugins_dir,
                      const std::filesystem::path& project_harness_dir = {});

} // namespace teez::core
