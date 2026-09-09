#include "teez/core/coverage_lua.hpp"

#include <memory>

#include <sol/sol.hpp>

#include "teez/core/coverage.hpp"
#include "teez/core/coverage_msgpack.hpp"
#include "teez/core/coverage_program.hpp"
#include "teez/core/coverage_reporter.hpp"
#include "teez/core/lua_json.hpp"

namespace teez::core {

namespace {

using CoverageHandle = std::shared_ptr<CoverageTable>;

CoverageHandle require_handle(const sol::object& value, const char* label) {
    if (value.get_type() != sol::type::userdata) {
        throw std::runtime_error(std::string(label) + " expects a coverage table handle");
    }
    return value.as<CoverageHandle>();
}

CoverageThresholds thresholds_from_lua(const sol::table& options) {
    CoverageThresholds thresholds;
    const sol::object min_line_rate = options["min_line_rate"];
    if (min_line_rate.get_type() == sol::type::number) {
        thresholds.min_line_rate = min_line_rate.as<double>();
    }
    const sol::object min_branch_rate = options["min_branch_rate"];
    if (min_branch_rate.get_type() == sol::type::number) {
        thresholds.min_branch_rate = min_branch_rate.as<double>();
    }
    const sol::object min_function_rate = options["min_function_rate"];
    if (min_function_rate.get_type() == sol::type::number) {
        thresholds.min_function_rate = min_function_rate.as<double>();
    }
    return thresholds;
}

sol::table int_vector_to_lua(sol::state& lua, const std::vector<int>& values) {
    sol::table table = lua.create_table();
    std::size_t index = 1;
    for (const int value : values) {
        table[index++] = value;
    }
    return table;
}

sol::table string_vector_to_lua(sol::state& lua, const std::vector<std::string>& values) {
    sol::table table = lua.create_table();
    std::size_t index = 1;
    for (const auto& value : values) {
        table[index++] = value;
    }
    return table;
}

} // namespace

void register_coverage_lua(sol::state& lua) {
    sol::table coverage = lua.create_named_table("coverage");
    coverage["load"] = [&lua](const std::string& path) {
        auto handle = std::make_shared<CoverageTable>(load_coverage_table(path));
        return sol::make_object(lua, handle);
    };
    coverage["merge"] = [](const CoverageHandle& left, const CoverageHandle& right) {
        left->merge(*right);
        return left;
    };
    coverage["to_json"] = [](const CoverageHandle& handle) {
        return coverage_table_to_json_string(*handle);
    };
    coverage["to_msgpack"] = [](const CoverageHandle& handle) {
        const auto bytes = coverage_table_to_msgpack(*handle);
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    };
    coverage["line_rate"] = [](const CoverageHandle& handle) { return handle->line_rate(); };
    coverage["branch_rate"] = [](const CoverageHandle& handle) { return handle->branch_rate(); };
    coverage["function_rate"] = [](const CoverageHandle& handle) {
        return handle->function_rate();
    };
    coverage["assert_threshold"] = [](const CoverageHandle& handle, sol::table options) {
        const auto result = check_coverage_thresholds(*handle, thresholds_from_lua(options));
        if (!result.passed) {
            std::string message = "coverage threshold failed";
            for (const auto& failure : result.failures) {
                message += ": ";
                message += failure;
            }
            throw std::runtime_error(message);
        }
        return true;
    };
    coverage["check_threshold"] = [&lua](const CoverageHandle& handle, sol::table options) {
        const auto result = check_coverage_thresholds(*handle, thresholds_from_lua(options));
        sol::table table = lua.create_table();
        table["passed"] = result.passed;
        sol::table failures = lua.create_table();
        std::size_t index = 1;
        for (const auto& failure : result.failures) {
            failures[index++] = failure;
        }
        table["failures"] = failures;
        return table;
    };
    coverage["diff"] = [&lua](const CoverageHandle& before, const CoverageHandle& after) {
        const auto diff = diff_coverage_tables(*before, *after);
        sol::table table = lua.create_table();
        table["line_rate_before"] = diff.line_rate_before;
        table["line_rate_after"] = diff.line_rate_after;
        table["branch_rate_before"] = diff.branch_rate_before;
        table["branch_rate_after"] = diff.branch_rate_after;
        table["function_rate_before"] = diff.function_rate_before;
        table["function_rate_after"] = diff.function_rate_after;
        sol::table changes = lua.create_table();
        std::size_t index = 1;
        for (const auto& change : diff.line_changes) {
            sol::table entry = lua.create_table();
            entry["path"] = change.path;
            entry["line_number"] = change.line_number;
            entry["before_hits"] = change.before_hits;
            entry["after_hits"] = change.after_hits;
            changes[index++] = entry;
        }
        table["line_changes"] = changes;
        return table;
    };
    coverage["files_matching"] = [&lua](const CoverageHandle& handle, const std::string& pattern) {
        return string_vector_to_lua(lua, handle->files_matching(pattern));
    };
    coverage["uncovered_lines"] = [&lua](const CoverageHandle& handle, const std::string& path) {
        return int_vector_to_lua(lua, handle->uncovered_lines(path));
    };
    coverage["never_hit_functions"] = [&lua](const CoverageHandle& handle,
                                             const std::string& path) {
        return string_vector_to_lua(lua, handle->never_hit_functions(path));
    };
    coverage["register_alias"] = [](const CoverageHandle& handle, const std::string& alias,
                                    const std::string& canonical) {
        handle->register_path_alias(alias, canonical);
        return handle;
    };
    coverage["add_source_root"] = [](const CoverageHandle& handle, const std::string& root) {
        handle->add_source_root(root);
        return handle;
    };
    coverage["mark_excluded"] = [](const CoverageHandle& handle, const std::string& path,
                                   int line_number) {
        handle->upsert_file(path).mark_line_excluded(line_number, true);
        return handle;
    };
    coverage["export_lcov"] = [](const CoverageHandle& handle, const std::string& path) {
        export_coverage_table_to_lcov(*handle, path);
        return true;
    };
    coverage["export_cobertura"] = [](const CoverageHandle& handle, const std::string& path) {
        export_coverage_table_to_cobertura(*handle, path);
        return true;
    };
    coverage["export"] = [](const CoverageHandle& handle, const std::string& reporter,
                            const std::string& path) {
        write_coverage_report(*handle, reporter, path);
        return true;
    };
    coverage["reporters"] = [&lua]() {
        sol::table reporters = lua.create_table();
        std::size_t index = 1;
        for (const auto& name : coverage_reporter_names()) {
            reporters[index++] = name;
        }
        return reporters;
    };
    register_coverage_program_lua(lua);
}

} // namespace teez::core
