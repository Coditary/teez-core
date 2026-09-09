#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "teez/core/runner_config.hpp"
#include "teez/core/test_filter.hpp"

namespace teez::core {

struct CtestFilters {
    std::optional<std::string> regex;
    std::optional<std::string> exclude;
    std::optional<std::string> label;
    std::optional<std::string> exclude_label;
};

struct RunContext {
    std::string command;
    std::filesystem::path target_path;
    CtestFilters ctest;
    TestFilters filters;
    std::string runner_name;
    RunnerOptions runner_options;
};

/// Returns true when path exists on the filesystem.
bool path_exists(const std::filesystem::path& path);

/// Validates that the run context has an existing target path.
/// Returns an error message on failure, empty string on success.
std::string validate_run_context(const RunContext& context);

} // namespace teez::core
