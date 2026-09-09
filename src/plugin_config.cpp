#include "teez/core/plugin_config.hpp"

#include <algorithm>

#include <nlohmann/json.hpp>

namespace teez::core {

namespace {

std::filesystem::path absolute_under_root(const std::filesystem::path& root,
                                          const std::filesystem::path& relative) {
    if (relative.empty()) {
        return {};
    }
    if (relative.is_absolute()) {
        return relative;
    }
    return std::filesystem::absolute(root / relative).lexically_normal();
}

void append_unique_dir(std::vector<std::filesystem::path>& dirs, const std::filesystem::path& dir) {
    if (dir.empty()) {
        return;
    }
    const auto normalized = std::filesystem::absolute(dir).lexically_normal();
    if (std::find(dirs.begin(), dirs.end(), normalized) == dirs.end()) {
        dirs.push_back(normalized);
    }
}

} // namespace

DiscoveryContext DiscoveryContext::bundled_only(const std::filesystem::path& plugins_dir) {
    return DiscoveryContext{.bundled_plugins_dir = plugins_dir};
}

std::filesystem::path resolve_plugins_dir_relative(const nlohmann::json& config_data) {
    if (!config_data.is_object()) {
        return kDefaultPluginsDir;
    }
    if (config_data.contains("plugins_dir") && config_data.at("plugins_dir").is_string()) {
        return config_data.at("plugins_dir").get<std::string>();
    }
    return kDefaultPluginsDir;
}

std::filesystem::path resolve_project_plugins_dir(const TeezConfig& config) {
    const auto dirs = resolve_project_plugin_dirs(config);
    if (dirs.empty()) {
        return {};
    }
    return dirs.front();
}

std::vector<std::filesystem::path> resolve_project_plugin_dirs(const TeezConfig& config) {
    std::vector<std::filesystem::path> dirs;
    if (config.root().empty()) {
        return dirs;
    }

    const auto root = config.root();
    append_unique_dir(dirs, absolute_under_root(root, resolve_plugins_dir_relative(config.data())));

    const auto legacy_plugins = absolute_under_root(root, kLegacyPluginsDir);
    if (std::filesystem::exists(legacy_plugins)) {
        append_unique_dir(dirs, legacy_plugins);
    }

    return dirs;
}

std::optional<PluginSelection> parse_plugin_selection(const nlohmann::json& value) {
    if (value.is_string()) {
        const auto name = value.get<std::string>();
        if (name.empty()) {
            return std::nullopt;
        }
        return PluginSelection{.name = name};
    }

    if (!value.is_object() || !value.contains("name") || !value.at("name").is_string()) {
        return std::nullopt;
    }

    PluginSelection selection{.name = value.at("name").get<std::string>()};
    if (value.contains("version") && value.at("version").is_string()) {
        selection.version = value.at("version").get<std::string>();
    }
    return selection;
}

std::vector<PluginSelection> parse_plugin_selection_list(const nlohmann::json& value) {
    std::vector<PluginSelection> selections;
    if (value.is_array()) {
        for (const auto& entry : value) {
            if (const auto selection = parse_plugin_selection(entry)) {
                selections.push_back(*selection);
            }
        }
        return selections;
    }

    if (const auto selection = parse_plugin_selection(value)) {
        selections.push_back(*selection);
    }
    return selections;
}

std::vector<PluginSelection> parse_config_plugins(const nlohmann::json& config_data) {
    if (!config_data.is_object()) {
        return {};
    }

    if (config_data.contains("plugins")) {
        return parse_plugin_selection_list(config_data.at("plugins"));
    }

    if (config_data.contains("plugin")) {
        return parse_plugin_selection_list(config_data.at("plugin"));
    }

    return {};
}

std::vector<PluginSelection> parse_config_harnesses(const nlohmann::json& config_data) {
    if (!config_data.is_object()) {
        return {};
    }

    if (config_data.contains("harnesses")) {
        return parse_plugin_selection_list(config_data.at("harnesses"));
    }

    if (config_data.contains("harness")) {
        return parse_plugin_selection_list(config_data.at("harness"));
    }

    return {};
}

bool parse_parallel_runners(const nlohmann::json& config_data) {
    if (!config_data.is_object() || !config_data.contains("parallel_runners")) {
        return true;
    }

    const auto& value = config_data.at("parallel_runners");
    if (value.is_boolean()) {
        return value.get<bool>();
    }
    return true;
}

DiscoveryContext make_discovery_context(const TeezConfig& config,
                                        const std::filesystem::path& bundled_plugins_dir) {
    DiscoveryContext context{.bundled_plugins_dir = bundled_plugins_dir};
    if (config.root().empty()) {
        return context;
    }

    context.project_plugin_dirs = resolve_project_plugin_dirs(config);
    context.plugins = parse_config_plugins(config.data());
    context.harnesses = parse_config_harnesses(config.data());
    context.runners = parse_config_runners(config.data());
    context.parallel_runners = parse_parallel_runners(config.data());
    return context;
}

std::vector<std::filesystem::path>
project_harness_search_dirs(const DiscoveryContext& discovery,
                            const std::filesystem::path& project_root) {
    std::vector<std::filesystem::path> dirs = discovery.project_plugin_dirs;

    const auto legacy_harness =
        std::filesystem::absolute(project_root / kLegacyHarnessDir).lexically_normal();
    if (std::filesystem::exists(legacy_harness)) {
        append_unique_dir(dirs, legacy_harness);
    }

    return dirs;
}

} // namespace teez::core
