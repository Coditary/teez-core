#include "teez/core/test_reporter.hpp"

#include <vector>

#include <nlohmann/json.hpp>

namespace teez::core {

namespace {

std::optional<std::string> json_string_at(const nlohmann::json& data,
                                          const std::vector<std::string>& path) {
    const nlohmann::json* current = &data;
    for (const auto& segment : path) {
        if (!current->is_object() || !current->contains(segment)) {
            return std::nullopt;
        }
        current = &current->at(segment);
    }
    if (!current->is_string()) {
        return std::nullopt;
    }
    return current->get<std::string>();
}

} // namespace

TestReportCliOverrides resolve_test_report_cli_overrides(const nlohmann::json& config_data,
                                                         const TestReportCliOverrides& cli) {
    TestReportCliOverrides resolved = cli;

    if (!resolved.reporter.has_value()) {
        if (const auto reporter = json_string_at(config_data, {"test_report", "reporter"})) {
            resolved.reporter = reporter;
        } else if (const auto reporter = json_string_at(config_data, {"report", "reporter"})) {
            resolved.reporter = reporter;
        }
    }

    if (!resolved.output.has_value()) {
        if (const auto output = json_string_at(config_data, {"test_report", "output"})) {
            resolved.output = *output;
        } else if (const auto output = json_string_at(config_data, {"report", "output"})) {
            resolved.output = *output;
        }
    }

    return resolved;
}

} // namespace teez::core
