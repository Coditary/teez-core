#include "teez/core/discovery.hpp"

#include "teez/core/runner_config.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <unistd.h>
#include <unordered_set>

#include <coditary/fs/glob_walk.hpp>
#include <nlohmann/json.hpp>

namespace teez::core {

namespace {

PluginManifest parse_manifest(const std::filesystem::path& path,
                              const std::filesystem::path& plugins_dir) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open manifest: " + path.string());
    }

    const auto json = nlohmann::json::parse(file);
    PluginManifest manifest;
    manifest.name = json.value("name", path.stem().string());
    manifest.kind = json.value("kind", "runner");
    manifest.priority = json.value("priority", 0);
    if (json.contains("version") && json.at("version").is_string()) {
        manifest.version = json.at("version").get<std::string>();
    }

    const std::string plugin_name = json.at("plugin").get<std::string>();
    manifest.plugin_file = plugins_dir / plugin_name;

    if (json.contains("anchors")) {
        for (const auto& anchor : json.at("anchors")) {
            manifest.anchors.push_back(anchor.get<std::string>());
        }
    }

    if (json.contains("glob_anchors")) {
        for (const auto& anchor : json.at("glob_anchors")) {
            manifest.glob_anchors.push_back(anchor.get<std::string>());
        }
    }

    if (json.contains("requires")) {
        for (const auto& requirement : json.at("requires")) {
            manifest.tool_requires.push_back(requirement.get<std::string>());
        }
    }

    if (json.contains("include")) {
        manifest.include = parse_runner_string_list(json.at("include"));
    }
    if (json.contains("exclude")) {
        manifest.exclude = parse_runner_string_list(json.at("exclude"));
    }

    return manifest;
}

} // namespace

bool is_runner_manifest(const PluginManifest& manifest) {
    return manifest.kind == "runner";
}

bool is_harness_manifest(const PluginManifest& manifest) {
    return manifest.kind == "harness";
}

namespace {

bool has_file_anchor(const std::filesystem::path& project_dir,
                     const std::vector<std::string>& anchors) {
    for (const auto& anchor : anchors) {
        if (std::filesystem::exists(project_dir / anchor)) {
            return true;
        }
    }
    return false;
}

bool has_glob_anchor(const std::filesystem::path& project_dir,
                     const std::vector<std::string>& glob_anchors) {
    for (const auto& pattern : glob_anchors) {
        if (!glob_match(project_dir, pattern).empty()) {
            return true;
        }
    }
    return false;
}

bool matches_manifest(const std::filesystem::path& project_dir, const PluginManifest& manifest) {
    if (!manifest.anchors.empty() && has_file_anchor(project_dir, manifest.anchors)) {
        return true;
    }
    if (!manifest.glob_anchors.empty() && has_glob_anchor(project_dir, manifest.glob_anchors)) {
        return true;
    }
    return false;
}

} // namespace

std::vector<DiscoveryMatchReason> collect_match_reasons(const std::filesystem::path& project_dir,
                                                        const PluginManifest& manifest) {
    std::vector<DiscoveryMatchReason> reasons;

    for (const auto& anchor : manifest.anchors) {
        if (std::filesystem::exists(project_dir / anchor)) {
            reasons.push_back({.kind = "anchor", .value = anchor});
        }
    }

    for (const auto& pattern : manifest.glob_anchors) {
        const auto matches = glob_match(project_dir, pattern);
        if (!matches.empty()) {
            reasons.push_back({.kind = "glob_anchor", .value = pattern});
        }
    }

    return reasons;
}

bool is_tool_available(const std::string& tool_name) {
    if (tool_name.empty()) {
        return false;
    }

    const char* path_env = std::getenv("PATH");
    if (path_env == nullptr) {
        return false;
    }

    std::string path = path_env;
    std::size_t start = 0;
    while (start <= path.size()) {
        const auto end = path.find(':', start);
        const auto dir =
            path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!dir.empty()) {
            const auto candidate = std::filesystem::path(dir) / tool_name;
            if (access(candidate.c_str(), X_OK) == 0) {
                return true;
            }
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    return false;
}

std::vector<std::filesystem::path> glob_match(const std::filesystem::path& project_dir,
                                              const std::string& pattern) {
    std::vector<std::filesystem::path> matches;
    if (!std::filesystem::exists(project_dir)) {
        return matches;
    }

    const auto root =
        std::filesystem::is_directory(project_dir) ? project_dir : project_dir.parent_path();

    for (const auto& relative : coditary::fs::expand_glob(root, pattern)) {
        matches.push_back(root / relative);
    }

    std::sort(matches.begin(), matches.end());
    return matches;
}

std::vector<PluginManifest> load_manifests(const std::filesystem::path& plugins_dir) {
    std::vector<PluginManifest> manifests;
    if (!std::filesystem::exists(plugins_dir)) {
        return manifests;
    }

    for (const auto& entry : std::filesystem::directory_iterator(plugins_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }
        manifests.push_back(parse_manifest(entry.path(), plugins_dir));
    }

    std::sort(
        manifests.begin(), manifests.end(),
        [](const PluginManifest& a, const PluginManifest& b) { return a.priority > b.priority; });

    return manifests;
}

namespace {

bool version_matches(const PluginManifest& manifest, const std::optional<std::string>& version) {
    if (!version.has_value()) {
        return true;
    }
    return manifest.version.has_value() && manifest.version == version;
}

void append_manifests(std::vector<PluginManifest>& manifests,
                      const std::vector<PluginManifest>& incoming) {
    manifests.insert(manifests.end(), incoming.begin(), incoming.end());
}

std::vector<PluginManifest> load_runner_manifests(const DiscoveryContext& discovery) {
    std::vector<PluginManifest> manifests = load_manifests(discovery.bundled_plugins_dir);
    for (const auto& dir : discovery.project_plugin_dirs) {
        append_manifests(manifests, load_manifests(dir));
    }

    std::sort(
        manifests.begin(), manifests.end(),
        [](const PluginManifest& a, const PluginManifest& b) { return a.priority > b.priority; });
    return manifests;
}

std::optional<DiscoveryMatch> match_from_manifest(const std::filesystem::path& project_dir,
                                                  const PluginManifest& manifest,
                                                  const std::string& reason_kind,
                                                  const std::string& reason_value) {
    if (!std::filesystem::exists(manifest.plugin_file)) {
        return std::nullopt;
    }

    std::vector<DiscoveryMatchReason> reasons;
    if (reason_kind == "anchor" || reason_kind == "glob_anchor") {
        reasons = collect_match_reasons(project_dir, manifest);
    } else {
        reasons.push_back({.kind = reason_kind, .value = reason_value});
    }

    return DiscoveryMatch{
        .manifest = manifest,
        .reasons = std::move(reasons),
    };
}

std::string format_plugin_selection(const PluginSelection& selection) {
    if (selection.version.has_value()) {
        return selection.name + "@" + *selection.version;
    }
    return selection.name;
}

} // namespace

std::optional<PluginManifest> find_manifest_by_name(const std::string& name,
                                                    const std::optional<std::string>& version,
                                                    const std::string& kind,
                                                    const DiscoveryContext& discovery) {
    std::optional<PluginManifest> best;
    const auto consider = [&](const std::vector<PluginManifest>& manifests) {
        for (const auto& manifest : manifests) {
            if (manifest.kind != kind || manifest.name != name) {
                continue;
            }
            if (!version_matches(manifest, version)) {
                continue;
            }
            if (!std::filesystem::exists(manifest.plugin_file)) {
                continue;
            }
            if (!best.has_value() || manifest.priority > best->priority) {
                best = manifest;
            }
        }
    };

    for (const auto& dir : discovery.project_plugin_dirs) {
        consider(load_manifests(dir));
    }
    consider(load_manifests(discovery.bundled_plugins_dir));
    return best;
}

namespace {

bool project_has_teez_lua_files(const std::filesystem::path& project_dir) {
    return !glob_match(project_dir, "*.teez.lua").empty();
}

void append_runner_match(std::vector<DiscoveryMatch>& matches,
                         std::unordered_set<std::string>& seen, const DiscoveryMatch& match) {
    if (seen.insert(match.manifest.name).second) {
        matches.push_back(match);
    }
}

std::optional<DiscoveryMatch> implicit_worker_match(const std::filesystem::path& project_dir,
                                                    const DiscoveryContext& discovery) {
    if (!project_has_teez_lua_files(project_dir)) {
        return std::nullopt;
    }

    const auto manifest =
        find_manifest_by_name(kWorkerRunnerName, std::nullopt, "runner", discovery);
    if (!manifest.has_value()) {
        return std::nullopt;
    }

    return match_from_manifest(project_dir, *manifest, "implicit", "teez-worker");
}

} // namespace

std::optional<std::filesystem::path> discover_plugin(const std::filesystem::path& project_dir,
                                                     const DiscoveryContext& discovery) {
    const auto match = find_runner_match(project_dir, discovery);
    if (!match.has_value()) {
        return std::nullopt;
    }
    return match->manifest.plugin_file;
}

std::optional<std::filesystem::path> discover_plugin(const std::filesystem::path& project_dir,
                                                     const std::filesystem::path& plugins_dir) {
    return discover_plugin(project_dir, DiscoveryContext::bundled_only(plugins_dir));
}

std::vector<DiscoveryMatch> find_all_runner_matches(const std::filesystem::path& project_dir,
                                                    const DiscoveryContext& discovery) {
    std::vector<DiscoveryMatch> matches;
    std::unordered_set<std::string> seen;

    if (!discovery.plugins.empty()) {
        std::vector<std::string> missing;
        for (const auto& selection : discovery.plugins) {
            const auto manifest =
                find_manifest_by_name(selection.name, selection.version, "runner", discovery);
            if (!manifest.has_value()) {
                missing.push_back(format_plugin_selection(selection));
                continue;
            }
            if (const auto match =
                    match_from_manifest(project_dir, *manifest, "config",
                                        "plugins=" + format_plugin_selection(selection))) {
                append_runner_match(matches, seen, *match);
            }
        }

        if (!missing.empty()) {
            std::string message = "configured plugins not found:";
            for (const auto& name : missing) {
                message += ' ';
                message += name;
            }
            if (!discovery.project_plugin_dirs.empty()) {
                message += " (search:";
                for (const auto& dir : discovery.project_plugin_dirs) {
                    message += ' ';
                    message += dir.string();
                }
                message += ", bundled)";
            }
            throw std::runtime_error(message);
        }

        if (const auto worker = implicit_worker_match(project_dir, discovery)) {
            append_runner_match(matches, seen, *worker);
        }
        return matches;
    }

    for (const auto& manifest : load_runner_manifests(discovery)) {
        if (!is_runner_manifest(manifest)) {
            continue;
        }
        if (!matches_manifest(project_dir, manifest)) {
            continue;
        }
        if (const auto match = match_from_manifest(project_dir, manifest, "anchor", "")) {
            append_runner_match(matches, seen, *match);
        }
    }

    if (matches.empty()) {
        if (const auto worker = implicit_worker_match(project_dir, discovery)) {
            append_runner_match(matches, seen, *worker);
        }
    }

    return matches;
}

std::vector<DiscoveryMatch> find_all_runner_matches(const std::filesystem::path& project_dir,
                                                    const std::filesystem::path& plugins_dir) {
    return find_all_runner_matches(project_dir, DiscoveryContext::bundled_only(plugins_dir));
}

std::optional<DiscoveryMatch> find_runner_match(const std::filesystem::path& project_dir,
                                                const DiscoveryContext& discovery) {
    const auto matches = find_all_runner_matches(project_dir, discovery);
    if (matches.empty()) {
        return std::nullopt;
    }
    return matches.front();
}

std::optional<DiscoveryMatch> find_runner_match(const std::filesystem::path& project_dir,
                                                const std::filesystem::path& plugins_dir) {
    return find_runner_match(project_dir, DiscoveryContext::bundled_only(plugins_dir));
}

namespace {

std::vector<PluginManifest> load_manifests_from_dir(const std::filesystem::path& dir,
                                                    const std::filesystem::path& plugin_root) {
    std::vector<PluginManifest> manifests;
    if (!std::filesystem::exists(dir)) {
        return manifests;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }
        auto manifest = parse_manifest(entry.path(), plugin_root);
        if (!is_harness_manifest(manifest)) {
            continue;
        }
        manifests.push_back(std::move(manifest));
    }

    return manifests;
}

} // namespace

std::vector<PluginManifest> load_harness_manifests(const DiscoveryContext& discovery,
                                                   const std::filesystem::path& project_root) {
    std::vector<PluginManifest> manifests;
    const auto append_harness_dir = [&](const std::filesystem::path& dir) {
        if (dir.empty() || !std::filesystem::exists(dir)) {
            return;
        }
        for (const auto& manifest : load_manifests(dir)) {
            if (is_harness_manifest(manifest)) {
                manifests.push_back(manifest);
            }
        }
    };

    for (const auto& dir : project_harness_search_dirs(discovery, project_root)) {
        append_harness_dir(dir);
    }
    append_harness_dir(discovery.bundled_plugins_dir);

    std::sort(
        manifests.begin(), manifests.end(),
        [](const PluginManifest& a, const PluginManifest& b) { return a.priority > b.priority; });
    return manifests;
}

std::vector<PluginManifest>
load_harness_manifests(const std::filesystem::path& plugins_dir,
                       const std::filesystem::path& project_harness_dir) {
    std::vector<PluginManifest> manifests = load_manifests_from_dir(plugins_dir, plugins_dir);
    if (!project_harness_dir.empty()) {
        append_manifests(manifests,
                         load_manifests_from_dir(project_harness_dir, project_harness_dir));
    }

    std::sort(
        manifests.begin(), manifests.end(),
        [](const PluginManifest& a, const PluginManifest& b) { return a.priority > b.priority; });
    return manifests;
}

std::optional<PluginManifest> find_harness_manifest(const std::string& name,
                                                    const DiscoveryContext& discovery,
                                                    const std::filesystem::path& project_root) {
    std::optional<PluginSelection> configured;
    for (const auto& harness : discovery.harnesses) {
        if (harness.name == name) {
            configured = harness;
            break;
        }
    }

    const auto version = configured.has_value() ? configured->version : std::nullopt;
    return find_manifest_by_name(name, version, "harness", discovery);
}

std::optional<PluginManifest>
find_harness_manifest(const std::string& name, const std::filesystem::path& plugins_dir,
                      const std::filesystem::path& project_harness_dir) {
    DiscoveryContext discovery{.bundled_plugins_dir = plugins_dir};
    if (!project_harness_dir.empty()) {
        discovery.project_plugin_dirs.push_back(project_harness_dir);
    }
    return find_harness_manifest(name, discovery);
}

} // namespace teez::core
