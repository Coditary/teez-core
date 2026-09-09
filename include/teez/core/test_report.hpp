#pragma once

#include <chrono>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace teez::core {

inline constexpr const char* kTestReportSchemaVersion = "1.0";

enum class TestStatus {
    Unknown,
    Passed,
    Failed,
    Skipped,
    Todo,
    Error,
};

std::string test_status_to_string(TestStatus status);
TestStatus test_status_from_string(const std::string& status);
TestStatus test_status_from_event(const std::string& event_type);

struct TestFailureInfo {
    std::optional<std::string> message;
    std::optional<std::string> type;
    std::optional<std::string> stacktrace;
};

struct TestSkipInfo {
    std::optional<std::string> message;
};

struct TestAttemptResult {
    int index = 0;
    TestStatus status = TestStatus::Unknown;
    std::optional<double> duration_seconds;
    std::optional<TestFailureInfo> failure;
};

struct TestPhaseRecord {
    std::string name;
    std::string state;
    std::optional<std::string> at;
    std::optional<double> duration_seconds;
};

struct TestCaseIdentity {
    std::string name;
    std::optional<std::string> classname;
    std::vector<std::string> suite_path;
    std::optional<std::string> file;
};

TestCaseIdentity parse_test_case_id(const std::string& id);

struct TestCaseResult {
    std::string id;
    std::string name;
    std::optional<std::string> classname;
    std::vector<std::string> suite_path;
    std::optional<std::string> file;
    std::optional<int> line;
    std::optional<int> column;
    TestStatus status = TestStatus::Unknown;
    std::optional<std::string> started_at;
    std::optional<std::string> finished_at;
    std::optional<double> duration_seconds;
    std::optional<TestFailureInfo> failure;
    std::optional<TestSkipInfo> skip;
    std::vector<std::string> stdout_lines;
    std::vector<std::string> stderr_lines;
    int retries = 0;
    std::vector<TestAttemptResult> attempts;
    std::vector<TestPhaseRecord> phases;
    std::map<std::string, std::string> properties;

    void apply_identity(const TestCaseIdentity& identity);
};

struct TestRunSummary {
    int suites = 0;
    int tests = 0;
    int passed = 0;
    int failed = 0;
    int skipped = 0;
    int todo = 0;
    int errors = 0;
    int retries = 0;

    int total() const;
    bool has_failures() const;
};

struct TestPluginInfo {
    std::string name;
    std::filesystem::path file;
};

struct TestSuiteReport {
    std::string id;
    std::string name;
    std::filesystem::path path;
    std::optional<TestPluginInfo> plugin;
    std::optional<double> duration_seconds;
    std::vector<TestCaseResult> cases;
    std::map<std::string, std::string> properties;

    TestRunSummary summary() const;
};

struct TestToolInfo {
    std::string name = "teez";
    std::optional<std::string> version;
};

struct TestRunMetadata {
    std::optional<std::string> id;
    std::optional<std::string> started_at;
    std::optional<std::string> finished_at;
    std::optional<double> duration_seconds;
    std::optional<std::string> hostname;
    std::optional<std::filesystem::path> cwd;
    std::optional<std::string> command;
    std::optional<int> exit_code;
    std::map<std::string, std::string> environment;
};

struct TestConfigMetadata {
    std::optional<std::filesystem::path> path;
    std::vector<std::filesystem::path> projects;
};

struct TestRunReport {
    std::string schema_version = kTestReportSchemaVersion;
    TestToolInfo tool;
    std::optional<TestRunMetadata> run;
    std::optional<TestConfigMetadata> config;
    std::vector<TestSuiteReport> suites;

    TestRunSummary summary() const;
    std::vector<TestCaseResult> all_cases() const;
    void merge(const TestRunReport& other);
};

struct TestSuiteBegin {
    std::filesystem::path path;
    std::optional<TestPluginInfo> plugin;
};

/// Builds a canonical TestRunReport from streamed plugin/runner events.
class TestRunCollector {
  public:
    void set_tool(const TestToolInfo& tool);
    void set_config(const TestConfigMetadata& config);
    void begin_run(const std::optional<std::string>& command = std::nullopt);
    void begin_suite(const TestSuiteBegin& suite);
    void on_event(const nlohmann::json& event);
    void finish_run(int exit_code);
    const TestRunReport& build() const;
    void reset();

  private:
    struct ActiveCaseState {
        std::chrono::steady_clock::time_point started_at;
        std::optional<TestAttemptResult> open_attempt;
    };

    TestCaseResult& case_for(const std::string& id);
    void finalize_attempt(TestCaseResult& test_case, TestStatus status,
                          const nlohmann::json& event);
    void flush_current_suite();
    static std::string now_iso8601_utc();

    TestRunReport report_;
    TestSuiteReport* current_suite_ = nullptr;
    std::unordered_map<std::string, TestCaseResult> cases_;
    std::unordered_map<std::string, ActiveCaseState> active_;
    std::string last_active_id_;
    std::optional<std::chrono::steady_clock::time_point> run_started_at_;
};

nlohmann::json test_run_report_to_json(const TestRunReport& report);
TestRunReport test_run_report_from_json(const nlohmann::json& json);

} // namespace teez::core
