#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <sol/forward.hpp>

namespace teez::core {

struct TeezConfigResolveOptions {
    /// Directory to look for teez.config.lua when config_file is not set (typically cwd).
    std::filesystem::path search_dir;
    /// Test target path passed on the CLI.
    std::filesystem::path target_path;
    /// Explicit config path from --config (optional).
    std::optional<std::filesystem::path> config_file;
    /// Active profile from --profile (overrides teez.config.lua `profile`).
    std::optional<std::string> profile;
};

struct TeezConfigSummary {
    bool loaded = false;
    std::filesystem::path config_path;
    std::filesystem::path root;
    bool applies_to_target = false;
    std::optional<std::string> profile;
    std::vector<std::filesystem::path> resolved_projects;
    std::vector<std::string> errors;
};

/// Loads teez.config.lua from search_dir only (no parent walk); target must lie under the config
/// root.
class TeezConfig {
  public:
    static TeezConfig load(const std::filesystem::path& project_dir);
    static TeezConfig resolve(const TeezConfigResolveOptions& options);

    bool empty() const;
    const nlohmann::json& data() const;
    /// Directory containing the loaded teez.config.lua (empty when no config is active).
    const std::filesystem::path& root() const;
    bool applies_to(const std::filesystem::path& target) const;
    /// When target is the config root and projects/includes is set, returns those paths; otherwise
    /// {target}.
    std::vector<std::filesystem::path>
    resolve_project_paths(const std::filesystem::path& target) const;

    std::optional<std::string> get_string(const std::string& key) const;
    void inject_into(sol::state& lua) const;

  private:
    static TeezConfig load_file(const std::filesystem::path& config_path);

    nlohmann::json data_;
    std::filesystem::path root_;
};

/// Active config for the current run (used by plugins/helpers).
void set_active_config(TeezConfig config);
const TeezConfig& active_config();

TeezConfigSummary summarize_config(const TeezConfig& config,
                                   const std::filesystem::path& target_path);
TeezConfigSummary validate_config(const TeezConfigResolveOptions& options);

/// Deep-merge overlay into base (objects recurse; other types replace).
nlohmann::json json_deep_merge(const nlohmann::json& base, const nlohmann::json& overlay);

/// Apply `profiles[active]` onto base config; strips top-level `profiles`. Throws when profile is
/// unknown.
nlohmann::json apply_config_profile(const nlohmann::json& raw_data,
                                    const std::optional<std::string>& cli_profile);

/// Names of entries in top-level `profiles` (empty when missing or not an object).
std::vector<std::string> config_profile_names(const nlohmann::json& raw_data);

} // namespace teez::core
