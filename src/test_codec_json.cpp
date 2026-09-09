#include "teez/core/test_codec.hpp"

#include <nlohmann/json.hpp>

namespace teez::core {

std::string test_report_to_json_string(const TestRunReport& report) {
    return test_run_report_to_json(report).dump(2);
}

TestRunReport test_report_from_json_string(const std::string& content) {
    return test_run_report_from_json(nlohmann::json::parse(content));
}

} // namespace teez::core
