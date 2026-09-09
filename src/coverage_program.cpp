#include "teez/core/coverage_program.hpp"

#include <stdexcept>

#include <sol/sol.hpp>

#include "teez/core/coverage_reporter.hpp"
#include "teez/core/lua_json.hpp"
#include "teez/core/teez_config.hpp"

namespace teez::core {

namespace {

nlohmann::json coverage_defaults_from_config(const nlohmann::json& config_data) {
    if (!config_data.is_object() || !config_data.contains("coverage")) {
        return nlohmann::json::object();
    }

    const auto& coverage = config_data["coverage"];
    if (!coverage.is_object()) {
        return nlohmann::json::object();
    }

    nlohmann::json defaults = coverage;
    defaults.erase("profiles");
    return defaults;
}

} // namespace

namespace {

std::filesystem::path default_output_path_for_reporter(const std::string& reporter) {
    return std::filesystem::path("coverage" +
                                 coverage_reporter_by_name(reporter).default_extension());
}

std::filesystem::path resolve_path(const std::optional<std::filesystem::path>& working_directory,
                                   const std::filesystem::path& path) {
    if (path.is_absolute() || !working_directory.has_value()) {
        return path;
    }
    return *working_directory / path;
}

} // namespace

CommandSpec command_spec_from_lua_table(const sol::table& spec_table) {
    CommandSpec spec;
    if (const sol::object command = spec_table["command"];
        command.get_type() == sol::type::string) {
        spec.command = command.as<std::string>();
    }

    if (const sol::object args = spec_table["args"]; args.get_type() == sol::type::table) {
        for (const auto& pair : args.as<sol::table>()) {
            if (pair.second.get_type() == sol::type::string) {
                spec.args.push_back(pair.second.as<std::string>());
            }
        }
    }

    if (const sol::object env = spec_table["env"]; env.get_type() == sol::type::table) {
        for (const auto& pair : env.as<sol::table>()) {
            if (pair.first.get_type() == sol::type::string &&
                pair.second.get_type() == sol::type::string) {
                spec.env.emplace_back(pair.first.as<std::string>(), pair.second.as<std::string>());
            }
        }
    }

    if (const sol::object timeout = spec_table["timeout"];
        timeout.get_type() == sol::type::number) {
        spec.timeout_ms = timeout.as<int>();
    }

    return spec;
}

ExecResult run_command_in_directory(const CommandSpec& spec,
                                    const std::optional<std::filesystem::path>& working_directory) {
    if (!working_directory.has_value()) {
        return run_command_capture(spec);
    }

    const auto previous = std::filesystem::current_path();
    std::filesystem::current_path(*working_directory);
    try {
        const auto result = run_command_capture(spec);
        std::filesystem::current_path(previous);
        return result;
    } catch (...) {
        std::filesystem::current_path(previous);
        throw;
    }
}

sol::table coverage_program_result_to_lua(sol::state& lua, const CoverageProgramResult& result) {
    sol::table payload = lua.create_table();
    payload["exit_code"] = result.execution.exit_code;
    payload["stdout"] = result.execution.stdout_text;
    payload["stderr"] = result.execution.stderr_text;
    payload["line_rate"] = result.table.line_rate();
    payload["branch_rate"] = result.table.branch_rate();
    payload["function_rate"] = result.table.function_rate();
    payload["threshold_passed"] = result.threshold.passed;
    if (result.exported_path.has_value()) {
        payload["exported_path"] = result.exported_path->string();
    }
    if (!result.threshold.failures.empty()) {
        sol::table failures = lua.create_table();
        std::size_t index = 1;
        for (const auto& failure : result.threshold.failures) {
            failures[index++] = failure;
        }
        payload["threshold_failures"] = failures;
    }
    return payload;
}

CoverageProgramSpec coverage_program_spec_from_lua(const sol::table& spec_table) {
    CoverageProgramSpec spec;
    spec.command = command_spec_from_lua_table(spec_table);

    if (const sol::object cwd = spec_table["cwd"]; cwd.get_type() == sol::type::string) {
        spec.working_directory = cwd.as<std::string>();
    } else if (const sol::object cwd = spec_table["working_directory"];
               cwd.get_type() == sol::type::string) {
        spec.working_directory = cwd.as<std::string>();
    }

    if (const sol::object report = spec_table["report"]; report.get_type() == sol::type::string) {
        spec.report_path = report.as<std::string>();
    } else if (const sol::object report = spec_table["report_path"];
               report.get_type() == sol::type::string) {
        spec.report_path = report.as<std::string>();
    }

    if (const sol::object collect = spec_table["collect"]; collect.get_type() == sol::type::table) {
        spec.collect_command = command_spec_from_lua_table(collect);
    } else if (const sol::object collect = spec_table["collect_command"];
               collect.get_type() == sol::type::table) {
        spec.collect_command = command_spec_from_lua_table(collect);
    }

    if (const sol::object reporter = spec_table["reporter"];
        reporter.get_type() == sol::type::string) {
        spec.reporter = reporter.as<std::string>();
    }

    if (const sol::object output = spec_table["output"]; output.get_type() == sol::type::string) {
        spec.output_path = output.as<std::string>();
    } else if (const sol::object output = spec_table["output_path"];
               output.get_type() == sol::type::string) {
        spec.output_path = output.as<std::string>();
    }

    if (const sol::object min_line_rate = spec_table["min_line_rate"];
        min_line_rate.get_type() == sol::type::number) {
        spec.min_line_rate = min_line_rate.as<double>();
    }

    return spec;
}

std::vector<std::string> coverage_profile_names() {
    std::vector<std::string> names;
    const auto& config_data = active_config().data();
    if (!config_data.is_object() || !config_data.contains("coverage")) {
        return names;
    }

    const auto& coverage = config_data["coverage"];
    if (!coverage.is_object() || !coverage.contains("profiles") ||
        !coverage["profiles"].is_object()) {
        return names;
    }

    for (const auto& [name, _] : coverage["profiles"].items()) {
        names.push_back(name);
    }
    return names;
}

CoverageProgramSpec coverage_program_spec_from_config(const std::string& profile_name,
                                                      const sol::optional<sol::table>& overrides,
                                                      sol::state& lua) {
    const auto& config_data = active_config().data();
    nlohmann::json merged = coverage_defaults_from_config(config_data);

    if (config_data.is_object() && config_data.contains("coverage") &&
        config_data["coverage"].is_object()) {
        const auto& coverage = config_data["coverage"];
        if (coverage.contains("profiles") && coverage["profiles"].is_object()) {
            const auto& profiles = coverage["profiles"];
            if (profiles.contains(profile_name)) {
                merged = json_deep_merge(merged, profiles[profile_name]);
            } else if (!merged.contains("command")) {
                std::string message = "coverage profile not found: " + profile_name;
                const auto names = coverage_profile_names();
                if (!names.empty()) {
                    message += " (available:";
                    for (std::size_t index = 0; index < names.size(); ++index) {
                        message += " " + names[index];
                        if (index + 1 < names.size()) {
                            message += ",";
                        }
                    }
                    message += ")";
                }
                throw std::runtime_error(message);
            }
        }
    }

    if (!merged.contains("command")) {
        throw std::runtime_error("coverage config has no command or profile '" + profile_name +
                                 "'");
    }

    if (overrides.has_value()) {
        merged = json_deep_merge(merged, lua_to_json(sol::object(*overrides)));
    }

    const sol::object spec_object = json_to_lua(lua, merged);
    if (spec_object.get_type() != sol::type::table) {
        throw std::runtime_error("invalid coverage profile configuration");
    }

    return coverage_program_spec_from_lua(spec_object.as<sol::table>());
}

CoverageProgramResult run_coverage_program(const CoverageProgramSpec& spec) {
    if (spec.command.command.empty()) {
        throw std::runtime_error("coverage program requires command");
    }

    CoverageProgramResult result;
    result.execution = run_command_in_directory(spec.command, spec.working_directory);

    if (spec.collect_command.has_value()) {
        const auto collect_result =
            run_command_in_directory(*spec.collect_command, spec.working_directory);
        if (collect_result.exit_code != 0) {
            throw std::runtime_error("coverage collect command failed with exit code " +
                                     std::to_string(collect_result.exit_code));
        }
    }

    if (!spec.report_path.has_value()) {
        throw std::runtime_error("coverage program requires report or report_path");
    }

    const auto report_file = resolve_path(spec.working_directory, *spec.report_path);
    if (!std::filesystem::exists(report_file)) {
        throw std::runtime_error("coverage report not found: " + report_file.string());
    }

    result.table = load_coverage_table(report_file);

    if (spec.min_line_rate.has_value()) {
        CoverageThresholds thresholds;
        thresholds.min_line_rate = spec.min_line_rate;
        result.threshold = check_coverage_thresholds(result.table, thresholds);
    } else {
        result.threshold.passed = true;
    }

    if (spec.reporter.has_value()) {
        auto output_path = spec.output_path;
        if (!output_path.has_value()) {
            output_path = default_output_path_for_reporter(*spec.reporter);
        }
        output_path = resolve_path(spec.working_directory, *output_path);
        if (const auto parent = output_path->parent_path(); !parent.empty()) {
            std::filesystem::create_directories(parent);
        }
        write_coverage_report(result.table, *spec.reporter, *output_path);
        result.exported_path = output_path;
    }

    return result;
}

void register_coverage_program_lua(sol::state& lua) {
    sol::table coverage = lua["coverage"];
    coverage["run_program"] = [&lua](sol::table spec_table) {
        const auto spec = coverage_program_spec_from_lua(spec_table);
        const auto result = run_coverage_program(spec);
        if (!result.threshold.passed) {
            std::string message = "coverage threshold failed";
            for (const auto& failure : result.threshold.failures) {
                message += ": ";
                message += failure;
            }
            throw std::runtime_error(message);
        }
        return coverage_program_result_to_lua(lua, result);
    };
    coverage["run_profile"] = [&lua](sol::optional<std::string> profile_name,
                                     sol::optional<sol::table> overrides) {
        const auto spec =
            coverage_program_spec_from_config(profile_name.value_or("default"), overrides, lua);
        const auto result = run_coverage_program(spec);
        if (!result.threshold.passed) {
            std::string message = "coverage threshold failed";
            for (const auto& failure : result.threshold.failures) {
                message += ": ";
                message += failure;
            }
            throw std::runtime_error(message);
        }
        return coverage_program_result_to_lua(lua, result);
    };
    coverage["profile_names"] = [&lua]() {
        sol::table names = lua.create_table();
        std::size_t index = 1;
        for (const auto& name : coverage_profile_names()) {
            names[index++] = name;
        }
        return names;
    };
}

} // namespace teez::core
