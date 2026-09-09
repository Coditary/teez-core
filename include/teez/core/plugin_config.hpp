#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "teez/core/runner_config.hpp"
#include "teez/core/teez_config.hpp"

namespace teez::core {

inline constexpr const char* kDefaultPluginsDir = "plugins";
inline constexpr const char* kLegacyPluginsDir = ".teez/plugins";
inline constexpr const char* kLegacyHarnessDir = ".teez/harness";

struct PluginSelection {
    std::string name;
    std::optional<std::string> version;
};

struct DiscoveryContext {
    std::filesystem::path bundled_plugins_dir;
    std::vector<std::filesystem::path> project_plugin_dirs;
    std::vector<PluginSelection> plugins;
    std::vector<PluginSelection> harnesses;
    RunnerConfigMap runners;
    bool parallel_runners = true;

    static DiscoveryContext bundled_only(const std::filesystem::path& plugins_dir);
};

std::filesystem::path resolve_plugins_dir_relative(const nlohmann::json& config_data);
std::filesystem::path resolve_project_plugins_dir(const TeezConfig& config);
std::vector<std::filesystem::path> resolve_project_plugin_dirs(const TeezConfig& config);
std::optional<PluginSelection> parse_plugin_selection(const nlohmann::json& value);
std::vector<PluginSelection> parse_plugin_selection_list(const nlohmann::json& value);
std::vector<PluginSelection> parse_config_plugins(const nlohmann::json& config_data);
std::vector<PluginSelection> parse_config_harnesses(const nlohmann::json& config_data);
bool parse_parallel_runners(const nlohmann::json& config_data);
DiscoveryContext make_discovery_context(const TeezConfig& config,
                                        const std::filesystem::path& bundled_plugins_dir);
std::vector<std::filesystem::path>
project_harness_search_dirs(const DiscoveryContext& discovery,
                            const std::filesystem::path& project_root);

} // namespace teez::core
