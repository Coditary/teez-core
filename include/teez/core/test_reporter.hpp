#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <nlohmann/json_fwd.hpp>

#include "teez/core/test_codec.hpp"

namespace teez::core {

struct TestReportCliOverrides {
    std::optional<std::string> reporter;
    std::optional<std::filesystem::path> output;
};

TestReportCliOverrides resolve_test_report_cli_overrides(const nlohmann::json& config_data,
                                                         const TestReportCliOverrides& cli);

} // namespace teez::core
