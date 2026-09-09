#include "teez/core/teez_config.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include <sol/sol.hpp>

#include "teez/core/lua_helpers.hpp"
#include "teez/core/lua_json.hpp"

namespace teez::core {

namespace {

constexpr const char* kConfigFileName = "teez.config.lua";

TeezConfig g_active_config;

bool path_is_within(const std::filesystem::path& target, const std::filesystem::path& root) {
    std::error_code ec;
    const auto abs_target = std::filesystem::absolute(target, ec);
    const auto abs_root = std::filesystem::absolute(root, ec);
    if (ec) {
        return false;
    }

    const auto norm_target = abs_target.lexically_normal();
    const auto norm_root = abs_root.lexically_normal();
    if (norm_target == norm_root) {
        return true;
    }

    const auto rel = norm_target.lexically_relative(norm_root);
    return !rel.empty() && rel.begin()->string() != "..";
}

} // namespace

nlohmann::json json_deep_merge(const nlohmann::json& base, const nlohmann::json& overlay) {
    if (!overlay.is_object()) {
        return overlay.is_null() ? base : overlay;
    }
    if (!base.is_object()) {
        return overlay;
    }

    nlohmann::json result = base;
    for (const auto& [key, value] : overlay.items()) {
        if (result.contains(key) && result[key].is_object() && value.is_object()) {
            result[key] = json_deep_merge(result[key], value);
        } else {
            result[key] = value;
        }
    }
    return result;
}

std::vector<std::string> config_profile_names(const nlohmann::json& raw_data) {
    std::vector<std::string> names;
    if (!raw_data.is_object() || !raw_data.contains("profiles") ||
        !raw_data.at("profiles").is_object()) {
        return names;
    }

    for (const auto& [key, _] : raw_data.at("profiles").items()) {
        names.push_back(key);
    }
    return names;
}

nlohmann::json apply_config_profile(const nlohmann::json& raw_data,
                                    const std::optional<std::string>& cli_profile) {
    if (!raw_data.is_object()) {
        return raw_data;
    }

    nlohmann::json base = raw_data;
    nlohmann::json profiles;
    if (base.contains("profiles") && base.at("profiles").is_object()) {
        profiles = base.at("profiles");
        base.erase("profiles");
    }

    std::optional<std::string> active_profile;
    if (cli_profile.has_value() && !cli_profile->empty()) {
        active_profile = *cli_profile;
    } else if (base.contains("profile") && base.at("profile").is_string()) {
        active_profile = base.at("profile").get<std::string>();
    }

    if (!active_profile.has_value() || profiles.empty()) {
        return base;
    }

    if (!profiles.contains(*active_profile)) {
        std::ostringstream message;
        message << "unknown config profile: " << *active_profile;
        const auto names = config_profile_names(raw_data);
        if (!names.empty()) {
            message << " (available:";
            for (const auto& name : names) {
                message << ' ' << name;
            }
            message << ')';
        }
        throw std::runtime_error(message.str());
    }

    nlohmann::json merged = json_deep_merge(base, profiles.at(*active_profile));
    merged["profile"] = *active_profile;
    return merged;
}

TeezConfig TeezConfig::load_file(const std::filesystem::path& config_path) {
    TeezConfig config;

    sol::state lua;
    register_lua_helpers(lua);
    lua.open_libraries(sol::lib::os);

    const auto result = lua.safe_script_file(config_path.string());
    if (!result.valid()) {
        sol::error err = result;
        throw std::runtime_error("failed to load teez.config.lua: " + std::string(err.what()));
    }

    if (result.get_type() == sol::type::table) {
        config.data_ = lua_to_json(result);
    }

    config.root_ = config_path.parent_path();
    return config;
}

TeezConfig TeezConfig::load(const std::filesystem::path& project_dir) {
    return resolve({project_dir, project_dir, std::nullopt});
}

TeezConfig TeezConfig::resolve(const TeezConfigResolveOptions& options) {
    std::filesystem::path config_path;
    if (options.config_file.has_value()) {
        config_path = std::filesystem::absolute(*options.config_file);
        if (!std::filesystem::exists(config_path)) {
            throw std::runtime_error("config file not found: " + config_path.string());
        }
    } else {
        config_path = std::filesystem::absolute(options.search_dir / kConfigFileName);
        if (!std::filesystem::exists(config_path)) {
            return {};
        }
    }

    auto config = load_file(config_path);
    if (!config.applies_to(options.target_path)) {
        return {};
    }

    config.data_ = apply_config_profile(config.data_, options.profile);
    return config;
}

bool TeezConfig::empty() const {
    return data_.is_null() || (data_.is_object() && data_.empty());
}

const nlohmann::json& TeezConfig::data() const {
    return data_;
}

const std::filesystem::path& TeezConfig::root() const {
    return root_;
}

bool TeezConfig::applies_to(const std::filesystem::path& target) const {
    if (empty() || root_.empty()) {
        return false;
    }
    return path_is_within(target, root_);
}

std::vector<std::filesystem::path>
TeezConfig::resolve_project_paths(const std::filesystem::path& target) const {
    std::error_code ec;
    const auto abs_target =
        std::filesystem::weakly_canonical(std::filesystem::absolute(target), ec);
    const auto norm_target =
        ec ? std::filesystem::absolute(target).lexically_normal() : abs_target.lexically_normal();

    if (empty() || root_.empty() || !data_.is_object()) {
        return {norm_target};
    }

    const nlohmann::json* entries = nullptr;
    if (data_.contains("projects") && data_.at("projects").is_array()) {
        entries = &data_.at("projects");
    } else if (data_.contains("includes") && data_.at("includes").is_array()) {
        entries = &data_.at("includes");
    }

    if (entries == nullptr) {
        return {norm_target};
    }

    const auto abs_root = std::filesystem::weakly_canonical(std::filesystem::absolute(root_), ec);
    const auto norm_root =
        ec ? std::filesystem::absolute(root_).lexically_normal() : abs_root.lexically_normal();
    if (norm_target != norm_root) {
        return {norm_target};
    }

    std::vector<std::filesystem::path> paths;
    for (const auto& entry : *entries) {
        if (!entry.is_string()) {
            continue;
        }
        paths.push_back((norm_root / entry.get<std::string>()).lexically_normal());
    }

    if (paths.empty()) {
        return {norm_target};
    }
    return paths;
}

std::optional<std::string> TeezConfig::get_string(const std::string& key) const {
    if (!data_.is_object() || !data_.contains(key) || !data_.at(key).is_string()) {
        return std::nullopt;
    }
    return data_.at(key).get<std::string>();
}

void TeezConfig::inject_into(sol::state& lua) const {
    if (empty()) {
        lua["config"] = lua.create_table();
        return;
    }
    lua["config"] = json_to_lua(lua, data_);
}

void set_active_config(TeezConfig config) {
    g_active_config = std::move(config);
}

const TeezConfig& active_config() {
    return g_active_config;
}

TeezConfigSummary summarize_config(const TeezConfig& config,
                                   const std::filesystem::path& target_path) {
    TeezConfigSummary summary;
    summary.loaded = !config.empty();
    summary.applies_to_target = config.applies_to(target_path);
    summary.resolved_projects = config.resolve_project_paths(target_path);

    if (!summary.loaded) {
        return summary;
    }

    summary.root = config.root();
    summary.profile = config.get_string("profile");
    return summary;
}

TeezConfigSummary validate_config(const TeezConfigResolveOptions& options) {
    TeezConfigSummary summary;

    std::filesystem::path config_path;
    if (options.config_file.has_value()) {
        config_path = std::filesystem::absolute(*options.config_file);
        if (!std::filesystem::exists(config_path)) {
            summary.errors.push_back("config file not found: " + config_path.string());
            return summary;
        }
    } else {
        config_path = std::filesystem::absolute(options.search_dir / kConfigFileName);
        if (!std::filesystem::exists(config_path)) {
            summary.errors.push_back("no teez.config.lua in " +
                                     std::filesystem::absolute(options.search_dir).string());
            return summary;
        }
    }

    summary.config_path = config_path;

    try {
        const auto config = TeezConfig::resolve(options);
        summary = summarize_config(config, options.target_path);
        summary.config_path = config_path;

        if (!summary.loaded) {
            summary.errors.push_back("config does not apply to target: " +
                                     std::filesystem::absolute(options.target_path).string());
            return summary;
        }

        for (const auto& project_path : summary.resolved_projects) {
            if (!std::filesystem::exists(project_path)) {
                summary.errors.push_back("project path does not exist: " + project_path.string());
            }
        }
    } catch (const std::exception& ex) {
        summary.errors.push_back(ex.what());
    }

    return summary;
}

} // namespace teez::core
