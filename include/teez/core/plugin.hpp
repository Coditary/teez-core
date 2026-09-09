#pragma once

#include <filesystem>
#include <iostream>
#include <ostream>
#include <string>
#include <vector>

#include <sol/sol.hpp>

#include "teez/core/context.hpp"

namespace teez::core {

struct CoverageProgramSpec;
struct CoverageProgramResult;

struct CommandSpec {
    std::string command;
    std::vector<std::string> args;
    std::vector<std::pair<std::string, std::string>> env;
    std::vector<std::pair<std::string, std::string>> auto_respond;
    std::vector<std::pair<std::string, std::string>> expect_stdout;
    int timeout_ms = 0;
};

/// Loaded Lua plugin with C++ helper APIs available.
class Plugin {
  public:
    explicit Plugin(std::filesystem::path plugin_path);

    std::vector<std::string> list_tests(const RunContext& context);
    CommandSpec build_command(const RunContext& context);
    std::optional<CommandSpec> build_list_command(const RunContext& context);
    std::optional<CoverageProgramSpec> build_coverage_run(const RunContext& context);
    bool supports_coverage_run() const;
    CoverageProgramResult run_with_coverage(const RunContext& context);
    std::string parse_line_ndjson(const std::string& line);

  private:
    std::filesystem::path plugin_path_;
    sol::state lua_;
};

/// Convenience wrappers (create a fresh Plugin per call).
CommandSpec call_build_command(const std::filesystem::path& plugin_path, const RunContext& context);
std::string call_parse_line(const std::filesystem::path& plugin_path, const std::string& line);

} // namespace teez::core
