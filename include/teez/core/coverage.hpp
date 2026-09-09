#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace teez::core {

enum class CoverageSourceFormat {
    Lcov,
    Cobertura,
};

enum class CoverageLineStatus {
    Unknown,
    Uncovered,
    Covered,
};

enum class CoverageLineKind {
    Unknown,
    NonExecutable,
    Executable,
    Covered,
};

struct CoverageCondition {
    int index = 0;
    std::optional<std::string> type;
    std::optional<std::uint64_t> taken_count;
    bool taken = false;
    std::optional<std::string> coverage_text;
};

struct CoverageRegion {
    int start_line = 0;
    int end_line = 0;
    std::optional<int> start_column;
    std::optional<int> end_column;
};

struct CoverageBranch {
    int line_number = 0;
    int block = 0;
    int branch = 0;
    std::optional<std::uint64_t> taken_count;
    bool taken = false;
    std::optional<std::string> condition_coverage;
    std::vector<CoverageCondition> conditions;
};

struct CoverageFunction {
    std::string name;
    int start_line = 0;
    int end_line = 0;
    std::optional<std::string> signature;
    std::uint64_t hit_count = 0;
    bool excluded = false;
};

struct CoverageLine {
    int line_number = 0;
    std::uint64_t hit_count = 0;
    CoverageLineStatus status = CoverageLineStatus::Unknown;
    CoverageLineKind kind = CoverageLineKind::Unknown;
    bool excluded = false;
    std::optional<int> statement_id;
    std::optional<CoverageRegion> region;
    std::vector<CoverageBranch> branches;
};

struct CoverageFile {
    std::string path;
    std::optional<std::string> class_name;
    std::optional<std::string> package_name;
    bool instrumented = true;
    std::map<int, CoverageLine> lines;
    std::map<std::string, CoverageFunction> functions;

    std::optional<int> lines_found_reported;
    std::optional<int> lines_hit_reported;
    std::optional<int> branches_found_reported;
    std::optional<int> branches_hit_reported;
    std::optional<int> functions_found_reported;
    std::optional<int> functions_hit_reported;
    std::optional<double> line_rate_reported;
    std::optional<double> branch_rate_reported;

    int lines_found() const;
    int lines_hit() const;
    double line_rate() const;

    int branches_found() const;
    int branches_hit() const;
    double branch_rate() const;

    int functions_found() const;
    int functions_hit() const;
    double function_rate() const;

    const CoverageLine* find_line(int line_number) const;
    const CoverageFunction* find_function(const std::string& name) const;

    void mark_line_excluded(int line_number, bool excluded = true);
    void infer_line_kinds();
};

struct CoverageReportMeta {
    std::optional<std::string> test_name;
    std::optional<std::int64_t> timestamp;
    std::optional<std::string> version;
    std::optional<double> line_rate_reported;
    std::optional<double> branch_rate_reported;
    std::optional<std::string> source_report_path;
};

struct CoverageThresholds {
    std::optional<double> min_line_rate;
    std::optional<double> min_branch_rate;
    std::optional<double> min_function_rate;
};

struct CoverageThresholdResult {
    bool passed = true;
    std::vector<std::string> failures;
};

struct CoverageDiffLineChange {
    std::string path;
    int line_number = 0;
    std::uint64_t before_hits = 0;
    std::uint64_t after_hits = 0;
};

struct CoverageDiff {
    double line_rate_before = 0.0;
    double line_rate_after = 0.0;
    double branch_rate_before = 0.0;
    double branch_rate_after = 0.0;
    double function_rate_before = 0.0;
    double function_rate_after = 0.0;
    std::vector<CoverageDiffLineChange> line_changes;
};

/// Unified in-memory coverage table for LCOV, Cobertura and merged reports.
struct CoverageTable {
    CoverageSourceFormat source_format = CoverageSourceFormat::Lcov;
    CoverageReportMeta meta;
    std::vector<std::string> source_roots;
    std::map<std::string, std::string> path_aliases;
    std::map<std::string, CoverageFile> files;

    /// Per-test slices when the source report contains test contexts (e.g. LCOV TN:).
    std::map<std::string, CoverageTable> by_test;

    int total_lines_found() const;
    int total_lines_hit() const;
    double line_rate() const;

    int total_branches_found() const;
    int total_branches_hit() const;
    double branch_rate() const;

    int total_functions_found() const;
    int total_functions_hit() const;
    double function_rate() const;

    std::string resolve_file_path(const std::string& path) const;
    void register_path_alias(const std::string& alias, const std::string& canonical);
    void add_source_root(const std::string& root);

    const CoverageFile* find_file(const std::string& path) const;
    CoverageFile& upsert_file(const std::string& path);

    void recompute_aggregates();
    void infer_line_kinds();

    /// Merges another table into this one (hits are summed per file/line/function/branch).
    void merge(const CoverageTable& other);

    std::vector<std::string> files_matching(const std::string& pattern) const;
    std::vector<int> uncovered_lines(const std::string& path) const;
    std::vector<std::string> never_hit_functions(const std::string& path) const;
};

std::string normalize_coverage_path(const std::string& path);
bool coverage_path_matches(const std::string& path, const std::string& pattern);

CoverageTable load_coverage_table_from_lcov(const std::filesystem::path& path);
CoverageTable load_coverage_table_from_cobertura(const std::filesystem::path& path);
CoverageTable load_coverage_table(const std::filesystem::path& path);

void export_coverage_table_to_lcov(const CoverageTable& table, const std::filesystem::path& path);
void export_coverage_table_to_cobertura(const CoverageTable& table,
                                        const std::filesystem::path& path);

CoverageThresholdResult check_coverage_thresholds(const CoverageTable& table,
                                                  const CoverageThresholds& thresholds);
CoverageDiff diff_coverage_tables(const CoverageTable& before, const CoverageTable& after);

std::string coverage_table_to_json_string(const CoverageTable& table);

/// NDJSON event payload for live UI / streaming consumers.
nlohmann::json coverage_table_to_event(const CoverageTable& table);

} // namespace teez::core
