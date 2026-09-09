#include "teez/core/lua_helpers.hpp"

#include <filesystem>

#include <sol/sol.hpp>

#include "teez/core/config.hpp"
#include "teez/core/coverage_lua.hpp"
#include "teez/core/process.hpp"
#include "teez/core/teez_config.hpp"
#include "teez/core/test_filter.hpp"

namespace teez::core {

namespace {

CommandSpec command_spec_from_lua(const std::string& command, sol::table args_table) {
    CommandSpec spec;
    spec.command = command;
    if (args_table.valid() && args_table.get_type() == sol::type::table) {
        for (const auto& pair : args_table) {
            if (pair.second.get_type() == sol::type::string) {
                spec.args.push_back(pair.second.as<std::string>());
            }
        }
    }
    return spec;
}

void apply_run_options(CommandSpec& spec, const sol::table& options) {
    const sol::object env = options["env"];
    if (env.get_type() == sol::type::table) {
        const sol::table env_table = env;
        for (const auto& pair : env_table) {
            if (pair.second.get_type() != sol::type::string) {
                continue;
            }
            std::string key;
            if (pair.first.get_type() == sol::type::string) {
                key = pair.first.as<std::string>();
            } else if (pair.first.get_type() == sol::type::number) {
                key = std::to_string(pair.first.as<int>());
            } else {
                continue;
            }
            spec.env.emplace_back(key, pair.second.as<std::string>());
        }
    }

    const sol::object auto_respond = options["auto_respond"];
    if (auto_respond.get_type() == sol::type::table) {
        for (const auto& pair : auto_respond.as<sol::table>()) {
            if (pair.first.get_type() == sol::type::string &&
                pair.second.get_type() == sol::type::string) {
                spec.auto_respond.emplace_back(pair.first.as<std::string>(),
                                               pair.second.as<std::string>());
            }
        }
    }

    const sol::object expect_stdout = options["expect_stdout"];
    if (expect_stdout.get_type() == sol::type::table) {
        for (const auto& pair : expect_stdout.as<sol::table>()) {
            if (pair.second.get_type() != sol::type::table) {
                continue;
            }
            const sol::table step = pair.second;
            const sol::object match = step["match"];
            const sol::object respond = step["respond"];
            if (match.get_type() != sol::type::string || respond.get_type() != sol::type::string) {
                throw std::runtime_error("expect_stdout steps require string match and respond");
            }
            spec.expect_stdout.emplace_back(match.as<std::string>(), respond.as<std::string>());
        }
    }

    if (!spec.auto_respond.empty() && !spec.expect_stdout.empty()) {
        throw std::runtime_error("sys.run options cannot combine auto_respond and expect_stdout");
    }

    const sol::object timeout = options["timeout"];
    if (timeout.get_type() == sol::type::number) {
        spec.timeout_ms = timeout.as<int>();
    }
}

sol::table exec_result_to_lua(sol::state& lua, const ExecResult& result) {
    sol::table table = lua.create_table();
    table["exit_code"] = result.exit_code;
    table["stdout"] = result.stdout_text;
    table["stderr"] = result.stderr_text;
    return table;
}

} // namespace

void register_lua_helpers(sol::state& lua, const std::filesystem::path& project_root) {
    lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::string, sol::lib::table,
                       sol::lib::math, sol::lib::io, sol::lib::os);

    coditary::lua::RegisterOptions options;
    options.project_root = project_root;
    options.table_table_name = "tbl";
    coditary::lua::register_fs(lua, options);
    coditary::lua::register_data(lua, options);
    coditary::lua::register_table(lua, options);
    coditary::lua::register_text(lua, options);

    sol::table teez_table = lua.create_named_table("teez");
    teez_table["worker_bin"] = worker_binary().string();
    if (const auto subcommand = worker_subcommand(); subcommand.has_value()) {
        teez_table["worker_subcommand"] = *subcommand;
    }
    teez_table["load"] = lua["data"]["load"];

    sol::table sys = lua.create_named_table("sys");
    sys["exec"] = [](const std::string& command, sol::table args_table,
                     sol::optional<sol::table> options) -> std::string {
        CommandSpec spec = command_spec_from_lua(command, args_table);
        if (options.has_value()) {
            apply_run_options(spec, *options);
        }
        const auto result = run_command_capture(spec);
        std::string output = result.stdout_text;
        if (!result.stderr_text.empty()) {
            output += result.stderr_text;
        }
        return output;
    };
    sys["run"] = [&lua](const std::string& command, sol::table args_table,
                        sol::optional<sol::table> options) {
        CommandSpec spec = command_spec_from_lua(command, args_table);
        if (options.has_value()) {
            apply_run_options(spec, *options);
        }
        const auto result = run_command_capture(spec);
        return exec_result_to_lua(lua, result);
    };

    active_config().inject_into(lua);
    register_coverage_lua(lua);
    register_filter_lua(lua);
}

} // namespace teez::core
