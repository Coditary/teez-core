#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <sol/forward.hpp>

#include "teez/core/coverage.hpp"
#include "teez/core/plugin.hpp"
#include "teez/core/process.hpp"

namespace teez::core {

/// Describes running an instrumented program and collecting a coverage report afterwards.
struct CoverageProgramSpec {
    CommandSpec command;
    std::optional<std::filesystem::path> working_directory;

    /// Coverage report file produced by the run (relative paths use working_directory).
    std::optional<std::filesystem::path> report_path;

    /// Optional post-run command that generates report_path (e.g. gcovr).
    std::optional<CommandSpec> collect_command;

    std::optional<std::string> reporter;
    std::optional<std::filesystem::path> output_path;
    std::optional<double> min_line_rate;
};

struct CoverageProgramResult {
    ExecResult execution;
    CoverageTable table;
    std::optional<std::filesystem::path> exported_path;
    CoverageThresholdResult threshold;
};

CoverageProgramSpec coverage_program_spec_from_lua(const sol::table& spec_table);
CoverageProgramSpec coverage_program_spec_from_config(const std::string& profile_name,
                                                      const sol::optional<sol::table>& overrides,
                                                      sol::state& lua);
std::vector<std::string> coverage_profile_names();
CoverageProgramResult run_coverage_program(const CoverageProgramSpec& spec);
void register_coverage_program_lua(sol::state& lua);

} // namespace teez::core
