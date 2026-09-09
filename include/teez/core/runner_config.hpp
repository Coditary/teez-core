#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <sol/forward.hpp>

namespace teez::core {

inline constexpr const char* kWorkerRunnerName = "teez-worker";
inline constexpr const char* kWorkerRunnerAlias = "teez";

struct RunnerOptions {
    std::vector<std::string> include;
    std::vector<std::string> exclude;
    /// Plugin-specific options from teez.config.lua (everything except include/exclude).
    nlohmann::json config = nlohmann::json::object();

    bool empty() const;
    static RunnerOptions from_json(const nlohmann::json& value);
    nlohmann::json to_json() const;
};

using RunnerConfigMap = std::unordered_map<std::string, RunnerOptions>;

RunnerConfigMap parse_config_runners(const nlohmann::json& config_data);
RunnerOptions resolve_runner_options(const RunnerConfigMap& runners,
                                     const std::string& manifest_name);
RunnerOptions merge_runner_options(const RunnerOptions& defaults, const RunnerOptions& overrides);
RunnerOptions resolve_effective_runner_options(const std::vector<std::string>& manifest_include,
                                               const std::vector<std::string>& manifest_exclude,
                                               const RunnerConfigMap& runners,
                                               const std::string& manifest_name);
std::vector<std::string> parse_runner_string_list(const nlohmann::json& value);

bool path_matches_runner_glob(const std::filesystem::path& path, const std::filesystem::path& root,
                              const std::string& pattern);
std::vector<std::filesystem::path>
apply_runner_options(const std::vector<std::filesystem::path>& paths,
                     const std::filesystem::path& root, const RunnerOptions& options);

RunnerOptions runner_options_from_env();
sol::table runner_options_to_lua(sol::state& lua, const RunnerOptions& options);

} // namespace teez::core
