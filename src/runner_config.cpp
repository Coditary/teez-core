#include "teez/core/runner_config.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

#include <sol/sol.hpp>

#include "teez/core/lua_json.hpp"
#include "teez/core/test_filter.hpp"

namespace teez::core {

namespace {

constexpr const char* kIncludeKey = "include";
constexpr const char* kExcludeKey = "exclude";

std::string relative_path_string(const std::filesystem::path& path,
                                 const std::filesystem::path& root) {
    std::error_code ec;
    const auto relative = std::filesystem::relative(path, root, ec);
    if (!ec) {
        return relative.generic_string();
    }
    return path.generic_string();
}

void append_string_list_to_lua(sol::state& lua, sol::table& runner, const char* key,
                               const std::vector<std::string>& values) {
    if (values.empty()) {
        return;
    }

    sol::table list = lua.create_table();
    for (std::size_t index = 0; index < values.size(); ++index) {
        list[index + 1] = values[index];
    }
    runner[key] = list;
}

} // namespace

std::vector<std::string> parse_runner_string_list(const nlohmann::json& value) {
    std::vector<std::string> items;
    if (value.is_string()) {
        items.push_back(value.get<std::string>());
        return items;
    }
    if (!value.is_array()) {
        return items;
    }
    for (const auto& entry : value) {
        if (entry.is_string()) {
            items.push_back(entry.get<std::string>());
        }
    }
    return items;
}

bool RunnerOptions::empty() const {
    return include.empty() && exclude.empty() &&
           (config.is_null() || (config.is_object() && config.empty()));
}

RunnerOptions RunnerOptions::from_json(const nlohmann::json& value) {
    if (!value.is_object()) {
        throw std::runtime_error("runner options must be a table");
    }

    RunnerOptions options;
    options.config = value;
    if (options.config.contains(kIncludeKey)) {
        options.include = parse_runner_string_list(options.config.at(kIncludeKey));
        options.config.erase(kIncludeKey);
    }
    if (options.config.contains(kExcludeKey)) {
        options.exclude = parse_runner_string_list(options.config.at(kExcludeKey));
        options.config.erase(kExcludeKey);
    }
    if (!options.config.is_object()) {
        options.config = nlohmann::json::object();
    }
    return options;
}

nlohmann::json RunnerOptions::to_json() const {
    nlohmann::json value = config.is_object() ? config : nlohmann::json::object();
    if (!include.empty()) {
        value[kIncludeKey] = include;
    }
    if (!exclude.empty()) {
        value[kExcludeKey] = exclude;
    }
    return value;
}

RunnerConfigMap parse_config_runners(const nlohmann::json& config_data) {
    RunnerConfigMap runners;
    if (!config_data.is_object() || !config_data.contains("runners")) {
        return runners;
    }

    const auto& runners_json = config_data.at("runners");
    if (!runners_json.is_object()) {
        throw std::runtime_error("runners must be a table");
    }

    for (const auto& [name, value] : runners_json.items()) {
        if (!value.is_object()) {
            continue;
        }
        runners.emplace(name, RunnerOptions::from_json(value));
    }
    return runners;
}

RunnerOptions resolve_runner_options(const RunnerConfigMap& runners,
                                     const std::string& manifest_name) {
    if (const auto direct = runners.find(manifest_name); direct != runners.end()) {
        return direct->second;
    }
    if (manifest_name == kWorkerRunnerName) {
        for (const char* alias : {kWorkerRunnerAlias, "worker"}) {
            if (const auto found = runners.find(alias); found != runners.end()) {
                return found->second;
            }
        }
    }
    return {};
}

RunnerOptions merge_runner_options(const RunnerOptions& defaults, const RunnerOptions& overrides) {
    RunnerOptions merged = defaults;
    if (!overrides.include.empty()) {
        merged.include = overrides.include;
    }

    for (const auto& pattern : overrides.exclude) {
        if (std::find(merged.exclude.begin(), merged.exclude.end(), pattern) ==
            merged.exclude.end()) {
            merged.exclude.push_back(pattern);
        }
    }

    if (overrides.config.is_object() && !overrides.config.empty()) {
        if (merged.config.is_object()) {
            for (const auto& [key, value] : overrides.config.items()) {
                merged.config[key] = value;
            }
        } else {
            merged.config = overrides.config;
        }
    }

    return merged;
}

RunnerOptions resolve_effective_runner_options(const std::vector<std::string>& manifest_include,
                                               const std::vector<std::string>& manifest_exclude,
                                               const RunnerConfigMap& runners,
                                               const std::string& manifest_name) {
    RunnerOptions defaults;
    defaults.include = manifest_include;
    defaults.exclude = manifest_exclude;
    return merge_runner_options(defaults, resolve_runner_options(runners, manifest_name));
}

bool path_matches_runner_glob(const std::filesystem::path& path, const std::filesystem::path& root,
                              const std::string& pattern) {
    const auto relative = relative_path_string(path, root);
    if (string_glob_match(relative, pattern)) {
        return true;
    }
    return string_glob_match(path.filename().string(), pattern);
}

std::vector<std::filesystem::path>
apply_runner_options(const std::vector<std::filesystem::path>& paths,
                     const std::filesystem::path& root, const RunnerOptions& options) {
    if (options.include.empty() && options.exclude.empty()) {
        return paths;
    }

    std::vector<std::filesystem::path> filtered;
    for (const auto& path : paths) {
        if (!options.include.empty()) {
            bool included = false;
            for (const auto& pattern : options.include) {
                if (path_matches_runner_glob(path, root, pattern)) {
                    included = true;
                    break;
                }
            }
            if (!included) {
                continue;
            }
        }

        bool excluded = false;
        for (const auto& pattern : options.exclude) {
            if (path_matches_runner_glob(path, root, pattern)) {
                excluded = true;
                break;
            }
        }
        if (!excluded) {
            filtered.push_back(path);
        }
    }
    return filtered;
}

RunnerOptions runner_options_from_env() {
    const char* raw = std::getenv("TEEZ_RUNNER_OPTIONS");
    if (raw == nullptr || *raw == '\0') {
        return {};
    }

    const auto json = nlohmann::json::parse(raw, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        return {};
    }
    return RunnerOptions::from_json(json);
}

sol::table runner_options_to_lua(sol::state& lua, const RunnerOptions& options) {
    sol::table runner =
        options.config.is_object() ? json_to_lua(lua, options.config) : lua.create_table();
    append_string_list_to_lua(lua, runner, kIncludeKey, options.include);
    append_string_list_to_lua(lua, runner, kExcludeKey, options.exclude);
    return runner;
}

} // namespace teez::core
