#pragma once

#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "teez/core/context.hpp"
#include "teez/core/plugin_config.hpp"

namespace teez::core {

using TestEventCallback = std::function<void(const nlohmann::json&)>;

TestEventCallback make_ndjson_event_writer(std::ostream& out);

/// Parses plugin-emitted NDJSON event strings (used by the runner and fuzzers).
std::optional<nlohmann::json> parse_plugin_event_json(const std::string& event_json);

int run_coverage_context(const DiscoveryContext& discovery, const RunContext& context);

int run_coverage_context(const DiscoveryContext& discovery, const RunContext& context,
                         const TestEventCallback& on_event);

int run_coverage_context(const std::filesystem::path& plugins_dir, const RunContext& context);

int run_coverage_context(const std::filesystem::path& plugins_dir, const RunContext& context,
                         const TestEventCallback& on_event);

/// Discovers plugin, emits test events, runs tests.
int run_context(const DiscoveryContext& discovery, const RunContext& context,
                const TestEventCallback& on_event);

int run_context(const DiscoveryContext& discovery, const RunContext& context,
                std::ostream& out = std::cout);

int run_context(const std::filesystem::path& plugins_dir, const RunContext& context,
                const TestEventCallback& on_event);

int run_context(const std::filesystem::path& plugins_dir, const RunContext& context,
                std::ostream& out = std::cout);

/// Discovers plugin and returns test IDs without executing them.
std::vector<std::string> list_context(const DiscoveryContext& discovery, const RunContext& context);

std::vector<std::string> list_context(const std::filesystem::path& plugins_dir,
                                      const RunContext& context);

/// Runs a specific plugin file (used by tests and explicit overrides).
int run_with_plugin(const std::filesystem::path& plugin_path, const RunContext& context,
                    const TestEventCallback& on_event);

int run_with_plugin(const std::filesystem::path& plugin_path, const RunContext& context,
                    std::ostream& out = std::cout);

} // namespace teez::core
