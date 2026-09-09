#include "teez/core/plugin.hpp"

#include <nlohmann/json.hpp>
#include <sol/sol.hpp>
#include <stdexcept>

#include "teez/core/coverage_program.hpp"
#include "teez/core/lua_helpers.hpp"
#include "teez/core/lua_json.hpp"
#include "teez/core/runner_config.hpp"
#include "teez/core/test_filter.hpp"

namespace teez::core {

namespace {

sol::state make_lua_state() {
    sol::state lua;
    register_lua_helpers(lua);
    return lua;
}

void load_plugin(sol::state& lua, const std::filesystem::path& plugin_path) {
    if (!std::filesystem::exists(plugin_path)) {
        throw std::runtime_error("plugin not found: " + plugin_path.string());
    }
    const auto result = lua.script_file(plugin_path.string());
    if (!result.valid()) {
        sol::error err = result;
        throw std::runtime_error("failed to load plugin: " + std::string(err.what()));
    }
}

CommandSpec table_to_command_spec(const sol::table& spec_table) {
    CommandSpec spec;
    if (spec_table["command"].valid()) {
        spec.command = spec_table["command"].get<std::string>();
    }
    if (spec_table["args"].valid() && spec_table["args"].get_type() == sol::type::table) {
        sol::table args_table = spec_table["args"];
        for (const auto& pair : args_table) {
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
    return spec;
}

std::string table_to_json(const sol::table& event_table) {
    return lua_to_json(sol::object(event_table.lua_state(), sol::in_place, event_table)).dump();
}

sol::table make_context_table(sol::state& lua, const RunContext& context) {
    sol::table ctx = lua.create_table();
    ctx["command"] = context.command;
    ctx["target_path"] = context.target_path.string();

    const auto& filters = context.ctest;
    if (filters.regex || filters.exclude || filters.label || filters.exclude_label) {
        sol::table ctest = lua.create_table();
        if (filters.regex) {
            ctest["regex"] = *filters.regex;
        }
        if (filters.exclude) {
            ctest["exclude"] = *filters.exclude;
        }
        if (filters.label) {
            ctest["label"] = *filters.label;
        }
        if (filters.exclude_label) {
            ctest["exclude_label"] = *filters.exclude_label;
        }
        ctx["ctest"] = ctest;
    }

    if (!context.filters.empty()) {
        ctx["filters"] = filters_to_lua(lua, context.filters);
    }

    if (!context.runner_options.empty() || !context.runner_name.empty()) {
        sol::table runner = runner_options_to_lua(lua, context.runner_options);
        if (!context.runner_name.empty()) {
            runner["name"] = context.runner_name;
        }
        ctx["runner"] = runner;
    }

    return ctx;
}

} // namespace

Plugin::Plugin(std::filesystem::path plugin_path)
    : plugin_path_(std::move(plugin_path)), lua_(make_lua_state()) {
    load_plugin(lua_, plugin_path_);
}

std::vector<std::string> Plugin::list_tests(const RunContext& context) {
    sol::protected_function list_fn = lua_["list"];
    if (!list_fn.valid()) {
        return {};
    }

    const auto result = list_fn(make_context_table(lua_, context));
    if (!result.valid()) {
        sol::error err = result;
        throw std::runtime_error("list failed: " + std::string(err.what()));
    }

    std::vector<std::string> tests;
    if (result.get_type() != sol::type::table) {
        return tests;
    }

    sol::table tests_table = result;
    for (const auto& pair : tests_table) {
        if (pair.second.get_type() == sol::type::string) {
            tests.push_back(pair.second.as<std::string>());
        }
    }
    return tests;
}

CommandSpec Plugin::build_command(const RunContext& context) {
    sol::protected_function build_command_fn = lua_["build_command"];
    if (!build_command_fn.valid()) {
        throw std::runtime_error("plugin missing build_command function");
    }

    const auto result = build_command_fn(make_context_table(lua_, context));
    if (!result.valid()) {
        sol::error err = result;
        throw std::runtime_error("build_command failed: " + std::string(err.what()));
    }

    return table_to_command_spec(result);
}

std::optional<CommandSpec> Plugin::build_list_command(const RunContext& context) {
    sol::protected_function build_list_command_fn = lua_["build_list_command"];
    if (!build_list_command_fn.valid()) {
        return std::nullopt;
    }

    const auto result = build_list_command_fn(make_context_table(lua_, context));
    if (!result.valid()) {
        sol::error err = result;
        throw std::runtime_error("build_list_command failed: " + std::string(err.what()));
    }

    if (result.get_type() == sol::type::lua_nil || result.get_type() == sol::type::none) {
        return std::nullopt;
    }

    return table_to_command_spec(result);
}

std::string Plugin::parse_line_ndjson(const std::string& line) {
    sol::protected_function parse_line_fn = lua_["parse_line"];
    if (!parse_line_fn.valid()) {
        throw std::runtime_error("plugin missing parse_line function");
    }

    const auto result = parse_line_fn(line);
    if (!result.valid()) {
        sol::error err = result;
        throw std::runtime_error("parse_line failed: " + std::string(err.what()));
    }

    if (result.get_type() == sol::type::lua_nil || result.get_type() == sol::type::none) {
        return {};
    }

    if (result.get_type() != sol::type::table) {
        return {};
    }

    return table_to_json(result);
}

bool Plugin::supports_coverage_run() const {
    sol::protected_function build_coverage_run_fn = lua_["build_coverage_run"];
    return build_coverage_run_fn.valid();
}

std::optional<CoverageProgramSpec> Plugin::build_coverage_run(const RunContext& context) {
    sol::protected_function build_coverage_run_fn = lua_["build_coverage_run"];
    if (!build_coverage_run_fn.valid()) {
        return std::nullopt;
    }

    const auto result = build_coverage_run_fn(make_context_table(lua_, context));
    if (!result.valid()) {
        sol::error err = result;
        throw std::runtime_error("build_coverage_run failed: " + std::string(err.what()));
    }

    if (result.get_type() != sol::type::table) {
        throw std::runtime_error("build_coverage_run must return a table");
    }

    return coverage_program_spec_from_lua(result);
}

CoverageProgramResult Plugin::run_with_coverage(const RunContext& context) {
    const auto spec = build_coverage_run(context);
    if (!spec.has_value()) {
        throw std::runtime_error("plugin does not implement build_coverage_run");
    }
    return run_coverage_program(*spec);
}

CommandSpec call_build_command(const std::filesystem::path& plugin_path,
                               const RunContext& context) {
    Plugin plugin(plugin_path);
    return plugin.build_command(context);
}

std::string call_parse_line(const std::filesystem::path& plugin_path, const std::string& line) {
    Plugin plugin(plugin_path);
    return plugin.parse_line_ndjson(line);
}

} // namespace teez::core
