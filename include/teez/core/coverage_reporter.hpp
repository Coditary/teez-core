#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "teez/core/coverage.hpp"

namespace teez::core {

/// Writes a unified CoverageTable to a specific output format.
class CoverageReporter {
  public:
    virtual ~CoverageReporter() = default;

    virtual std::string name() const = 0;
    virtual std::string default_extension() const = 0;
    virtual void write(const CoverageTable& table, const std::filesystem::path& path) const = 0;
};

struct CoverageReportOptions {
    std::string reporter = "json";
    std::optional<std::filesystem::path> input;
    std::optional<std::filesystem::path> output;
    std::optional<double> min_line_rate;
};

struct CoverageReportCliOverrides {
    std::optional<std::string> reporter;
    std::optional<std::filesystem::path> input;
    std::optional<std::filesystem::path> output;
    std::optional<double> min_line_rate;
};

std::vector<std::string> coverage_reporter_names();
bool is_coverage_reporter_name(const std::string& name);

const CoverageReporter& coverage_reporter_by_name(const std::string& name);
std::unique_ptr<CoverageReporter> make_coverage_reporter(const std::string& name);

void write_coverage_report(const CoverageTable& table, const std::string& reporter_name,
                           const std::filesystem::path& path);

CoverageReportOptions resolve_coverage_report_options(const nlohmann::json& config_data,
                                                      const CoverageReportCliOverrides& cli);

void export_coverage_report(const CoverageReportOptions& options);

} // namespace teez::core
