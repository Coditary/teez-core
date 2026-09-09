#include "teez/core/test_report.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace teez::core {

namespace {

bool is_terminal_status(TestStatus status) {
    return status == TestStatus::Passed || status == TestStatus::Failed ||
           status == TestStatus::Skipped || status == TestStatus::Todo ||
           status == TestStatus::Error;
}

void increment_summary(TestRunSummary& summary, TestStatus status) {
    switch (status) {
    case TestStatus::Passed:
        ++summary.passed;
        break;
    case TestStatus::Failed:
        ++summary.failed;
        break;
    case TestStatus::Skipped:
        ++summary.skipped;
        break;
    case TestStatus::Todo:
        ++summary.todo;
        break;
    case TestStatus::Error:
        ++summary.errors;
        break;
    default:
        break;
    }
}

std::optional<double> json_optional_number(const nlohmann::json& value) {
    if (value.is_number()) {
        return value.get<double>();
    }
    return std::nullopt;
}

std::optional<int> json_optional_int(const nlohmann::json& value) {
    if (value.is_number_integer()) {
        return value.get<int>();
    }
    return std::nullopt;
}

nlohmann::json string_map_to_json(const std::map<std::string, std::string>& values) {
    nlohmann::json object = nlohmann::json::object();
    for (const auto& [key, value] : values) {
        object[key] = value;
    }
    return object;
}

std::map<std::string, std::string> string_map_from_json(const nlohmann::json& json) {
    std::map<std::string, std::string> values;
    if (!json.is_object()) {
        return values;
    }
    for (const auto& [key, value] : json.items()) {
        if (value.is_string()) {
            values[key] = value.get<std::string>();
        }
    }
    return values;
}

nlohmann::json failure_to_json(const TestFailureInfo& failure) {
    nlohmann::json json = nlohmann::json::object();
    if (failure.message.has_value()) {
        json["message"] = *failure.message;
    }
    if (failure.type.has_value()) {
        json["type"] = *failure.type;
    }
    if (failure.stacktrace.has_value()) {
        json["stacktrace"] = *failure.stacktrace;
    }
    return json;
}

std::optional<TestFailureInfo> failure_from_json(const nlohmann::json& json) {
    if (!json.is_object()) {
        return std::nullopt;
    }
    TestFailureInfo failure;
    if (json.contains("message") && json.at("message").is_string()) {
        failure.message = json.at("message").get<std::string>();
    }
    if (json.contains("type") && json.at("type").is_string()) {
        failure.type = json.at("type").get<std::string>();
    }
    if (json.contains("stacktrace") && json.at("stacktrace").is_string()) {
        failure.stacktrace = json.at("stacktrace").get<std::string>();
    }
    if (!failure.message.has_value() && !failure.type.has_value() &&
        !failure.stacktrace.has_value()) {
        return std::nullopt;
    }
    return failure;
}

nlohmann::json attempt_to_json(const TestAttemptResult& attempt) {
    nlohmann::json json = nlohmann::json::object();
    json["index"] = attempt.index;
    json["status"] = test_status_to_string(attempt.status);
    if (attempt.duration_seconds.has_value()) {
        json["duration_seconds"] = *attempt.duration_seconds;
    }
    if (attempt.failure.has_value()) {
        json["failure"] = failure_to_json(*attempt.failure);
    }
    return json;
}

TestAttemptResult attempt_from_json(const nlohmann::json& json) {
    TestAttemptResult attempt;
    if (json.contains("index")) {
        attempt.index = json.at("index").get<int>();
    }
    if (json.contains("status")) {
        attempt.status = test_status_from_string(json.at("status").get<std::string>());
    }
    attempt.duration_seconds =
        json_optional_number(json.value("duration_seconds", nlohmann::json()));
    attempt.failure = failure_from_json(json.value("failure", nlohmann::json()));
    return attempt;
}

nlohmann::json phase_to_json(const TestPhaseRecord& phase) {
    nlohmann::json json = nlohmann::json::object();
    json["name"] = phase.name;
    json["state"] = phase.state;
    if (phase.at.has_value()) {
        json["at"] = *phase.at;
    }
    if (phase.duration_seconds.has_value()) {
        json["duration_seconds"] = *phase.duration_seconds;
    }
    return json;
}

TestPhaseRecord phase_from_json(const nlohmann::json& json) {
    TestPhaseRecord phase;
    phase.name = json.value("name", "");
    phase.state = json.value("state", "");
    if (json.contains("at") && json.at("at").is_string()) {
        phase.at = json.at("at").get<std::string>();
    }
    phase.duration_seconds = json_optional_number(json.value("duration_seconds", nlohmann::json()));
    return phase;
}

nlohmann::json test_case_to_json(const TestCaseResult& test_case) {
    nlohmann::json entry = nlohmann::json::object();
    entry["id"] = test_case.id;
    entry["name"] = test_case.name;
    entry["status"] = test_status_to_string(test_case.status);
    if (test_case.classname.has_value()) {
        entry["classname"] = *test_case.classname;
    }
    if (!test_case.suite_path.empty()) {
        entry["suite_path"] = test_case.suite_path;
    }
    if (test_case.file.has_value()) {
        entry["file"] = *test_case.file;
    }
    if (test_case.line.has_value()) {
        entry["line"] = *test_case.line;
    }
    if (test_case.column.has_value()) {
        entry["column"] = *test_case.column;
    }
    if (test_case.started_at.has_value()) {
        entry["started_at"] = *test_case.started_at;
    }
    if (test_case.finished_at.has_value()) {
        entry["finished_at"] = *test_case.finished_at;
    }
    if (test_case.duration_seconds.has_value()) {
        entry["duration_seconds"] = *test_case.duration_seconds;
    }
    if (test_case.failure.has_value()) {
        entry["failure"] = failure_to_json(*test_case.failure);
    }
    if (test_case.skip.has_value() && test_case.skip->message.has_value()) {
        entry["skip"] = {{"message", *test_case.skip->message}};
    } else if (test_case.status == TestStatus::Skipped || test_case.status == TestStatus::Todo) {
        entry["skip"] = nlohmann::json::object();
    }
    if (!test_case.stdout_lines.empty()) {
        entry["stdout"] = test_case.stdout_lines;
    }
    if (!test_case.stderr_lines.empty()) {
        entry["stderr"] = test_case.stderr_lines;
    }
    if (test_case.retries > 0) {
        entry["retries"] = test_case.retries;
    }
    if (!test_case.attempts.empty()) {
        entry["attempts"] = nlohmann::json::array();
        for (const auto& attempt : test_case.attempts) {
            entry["attempts"].push_back(attempt_to_json(attempt));
        }
    }
    if (!test_case.phases.empty()) {
        entry["phases"] = nlohmann::json::array();
        for (const auto& phase : test_case.phases) {
            entry["phases"].push_back(phase_to_json(phase));
        }
    }
    if (!test_case.properties.empty()) {
        entry["properties"] = string_map_to_json(test_case.properties);
    }
    return entry;
}

TestCaseResult test_case_from_json(const nlohmann::json& entry) {
    TestCaseResult test_case;
    test_case.id = entry.at("id").get<std::string>();
    test_case.name = entry.value("name", test_case.id);
    test_case.status = test_status_from_string(entry.at("status").get<std::string>());
    if (entry.contains("classname") && entry.at("classname").is_string()) {
        test_case.classname = entry.at("classname").get<std::string>();
    }
    if (entry.contains("suite_path") && entry.at("suite_path").is_array()) {
        for (const auto& part : entry.at("suite_path")) {
            if (part.is_string()) {
                test_case.suite_path.push_back(part.get<std::string>());
            }
        }
    }
    if (entry.contains("file") && entry.at("file").is_string()) {
        test_case.file = entry.at("file").get<std::string>();
    }
    test_case.line = json_optional_int(entry.value("line", nlohmann::json()));
    test_case.column = json_optional_int(entry.value("column", nlohmann::json()));
    if (entry.contains("started_at") && entry.at("started_at").is_string()) {
        test_case.started_at = entry.at("started_at").get<std::string>();
    }
    if (entry.contains("finished_at") && entry.at("finished_at").is_string()) {
        test_case.finished_at = entry.at("finished_at").get<std::string>();
    }
    test_case.duration_seconds =
        json_optional_number(entry.value("duration_seconds", nlohmann::json()));
    test_case.failure = failure_from_json(entry.value("failure", nlohmann::json()));
    if (entry.contains("skip")) {
        TestSkipInfo skip;
        if (entry.at("skip").is_object() && entry.at("skip").contains("message") &&
            entry.at("skip").at("message").is_string()) {
            skip.message = entry.at("skip").at("message").get<std::string>();
        }
        test_case.skip = skip;
    }
    if (entry.contains("stdout") && entry.at("stdout").is_array()) {
        for (const auto& line : entry.at("stdout")) {
            if (line.is_string()) {
                test_case.stdout_lines.push_back(line.get<std::string>());
            }
        }
    }
    if (entry.contains("stderr") && entry.at("stderr").is_array()) {
        for (const auto& line : entry.at("stderr")) {
            if (line.is_string()) {
                test_case.stderr_lines.push_back(line.get<std::string>());
            }
        }
    }
    if (entry.contains("output") && entry.at("output").is_array()) {
        for (const auto& line : entry.at("output")) {
            if (line.is_string()) {
                test_case.stdout_lines.push_back(line.get<std::string>());
            }
        }
    }
    if (entry.contains("message") && entry.at("message").is_string()) {
        test_case.failure = TestFailureInfo{.message = entry.at("message").get<std::string>()};
    }
    if (entry.contains("retries") && entry.at("retries").is_number_integer()) {
        test_case.retries = entry.at("retries").get<int>();
    }
    if (entry.contains("attempts") && entry.at("attempts").is_array()) {
        for (const auto& attempt : entry.at("attempts")) {
            test_case.attempts.push_back(attempt_from_json(attempt));
        }
    }
    if (entry.contains("phases") && entry.at("phases").is_array()) {
        for (const auto& phase : entry.at("phases")) {
            test_case.phases.push_back(phase_from_json(phase));
        }
    }
    test_case.properties =
        string_map_from_json(entry.value("properties", nlohmann::json::object()));
    if (!test_case.classname.has_value() || test_case.name == test_case.id) {
        test_case.apply_identity(parse_test_case_id(test_case.id));
    }
    return test_case;
}

nlohmann::json summary_to_json(const TestRunSummary& summary) {
    return {{"suites", summary.suites}, {"tests", summary.tests},     {"passed", summary.passed},
            {"failed", summary.failed}, {"skipped", summary.skipped}, {"todo", summary.todo},
            {"errors", summary.errors}, {"retries", summary.retries}};
}

TestRunSummary summary_from_json(const nlohmann::json& json) {
    TestRunSummary summary;
    if (!json.is_object()) {
        return summary;
    }
    summary.suites = json.value("suites", 0);
    summary.tests = json.value("tests", 0);
    summary.passed = json.value("passed", 0);
    summary.failed = json.value("failed", 0);
    summary.skipped = json.value("skipped", 0);
    summary.todo = json.value("todo", 0);
    summary.errors = json.value("errors", 0);
    summary.retries = json.value("retries", 0);
    if (summary.tests == 0) {
        summary.tests = summary.total();
    }
    return summary;
}

double duration_seconds_from_event(const nlohmann::json& event,
                                   const std::chrono::steady_clock::time_point& started_at) {
    if (event.contains("duration_ms") && event.at("duration_ms").is_number()) {
        return event.at("duration_ms").get<double>() / 1000.0;
    }
    if (event.contains("duration_seconds") && event.at("duration_seconds").is_number()) {
        return event.at("duration_seconds").get<double>();
    }
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - started_at).count();
}

TestFailureInfo failure_from_event(const nlohmann::json& event) {
    if (event.contains("failure") && event.at("failure").is_object()) {
        if (const auto failure = failure_from_json(event.at("failure"))) {
            return *failure;
        }
    }

    TestFailureInfo failure;
    if (event.contains("msg") && event.at("msg").is_string()) {
        failure.message = event.at("msg").get<std::string>();
    }
    if (event.contains("type") && event.at("type").is_string()) {
        failure.type = event.at("type").get<std::string>();
    }
    if (event.contains("stacktrace") && event.at("stacktrace").is_string()) {
        failure.stacktrace = event.at("stacktrace").get<std::string>();
    }
    return failure;
}

} // namespace

std::string test_status_to_string(TestStatus status) {
    switch (status) {
    case TestStatus::Passed:
        return "passed";
    case TestStatus::Failed:
        return "failed";
    case TestStatus::Skipped:
        return "skipped";
    case TestStatus::Todo:
        return "todo";
    case TestStatus::Error:
        return "error";
    default:
        return "unknown";
    }
}

TestStatus test_status_from_string(const std::string& status) {
    if (status == "passed") {
        return TestStatus::Passed;
    }
    if (status == "failed") {
        return TestStatus::Failed;
    }
    if (status == "skipped") {
        return TestStatus::Skipped;
    }
    if (status == "todo") {
        return TestStatus::Todo;
    }
    if (status == "error") {
        return TestStatus::Error;
    }
    return TestStatus::Unknown;
}

TestStatus test_status_from_event(const std::string& event_type) {
    if (event_type == "pass") {
        return TestStatus::Passed;
    }
    if (event_type == "fail") {
        return TestStatus::Failed;
    }
    if (event_type == "skip") {
        return TestStatus::Skipped;
    }
    if (event_type == "todo") {
        return TestStatus::Todo;
    }
    if (event_type == "error") {
        return TestStatus::Error;
    }
    return test_status_from_string(event_type);
}

TestCaseIdentity parse_test_case_id(const std::string& id) {
    TestCaseIdentity identity;
    identity.name = id;

    const auto first_sep = id.find("::");
    if (first_sep == std::string::npos) {
        return identity;
    }

    identity.file = id.substr(0, first_sep);
    std::string remainder = id.substr(first_sep + 2);

    const auto last_arrow = remainder.rfind(" > ");
    if (last_arrow != std::string::npos) {
        const std::string suite_text = remainder.substr(0, last_arrow);
        identity.name = remainder.substr(last_arrow + 3);
        std::size_t start = 0;
        while (start < suite_text.size()) {
            const auto next = suite_text.find(" > ", start);
            if (next == std::string::npos) {
                identity.suite_path.push_back(suite_text.substr(start));
                break;
            }
            identity.suite_path.push_back(suite_text.substr(start, next - start));
            start = next + 3;
        }
        identity.classname = *identity.file + "::" + suite_text;
        return identity;
    }

    const auto last_sep = remainder.rfind("::");
    if (last_sep != std::string::npos) {
        identity.classname = *identity.file + "::" + remainder.substr(0, last_sep);
        identity.name = remainder.substr(last_sep + 2);
        identity.suite_path.push_back(remainder.substr(0, last_sep));
        return identity;
    }

    identity.classname = *identity.file;
    identity.name = remainder;
    identity.suite_path.push_back(remainder);
    return identity;
}

void TestCaseResult::apply_identity(const TestCaseIdentity& identity) {
    name = identity.name;
    classname = identity.classname;
    suite_path = identity.suite_path;
    file = identity.file;
}

int TestRunSummary::total() const {
    return passed + failed + skipped + todo + errors;
}

bool TestRunSummary::has_failures() const {
    return failed > 0 || errors > 0;
}

TestRunSummary TestSuiteReport::summary() const {
    TestRunSummary summary;
    summary.tests = static_cast<int>(cases.size());
    for (const auto& test_case : cases) {
        increment_summary(summary, test_case.status);
        summary.retries += test_case.retries;
    }
    return summary;
}

TestRunSummary TestRunReport::summary() const {
    TestRunSummary summary;
    summary.suites = static_cast<int>(suites.size());
    for (const auto& suite : suites) {
        const auto suite_summary = suite.summary();
        summary.tests += suite_summary.tests;
        summary.passed += suite_summary.passed;
        summary.failed += suite_summary.failed;
        summary.skipped += suite_summary.skipped;
        summary.todo += suite_summary.todo;
        summary.errors += suite_summary.errors;
        summary.retries += suite_summary.retries;
    }
    return summary;
}

std::vector<TestCaseResult> TestRunReport::all_cases() const {
    std::vector<TestCaseResult> cases;
    for (const auto& suite : suites) {
        cases.insert(cases.end(), suite.cases.begin(), suite.cases.end());
    }
    std::sort(cases.begin(), cases.end(),
              [](const TestCaseResult& a, const TestCaseResult& b) { return a.id < b.id; });
    return cases;
}

void TestRunReport::merge(const TestRunReport& other) {
    suites.insert(suites.end(), other.suites.begin(), other.suites.end());
}

void TestRunCollector::set_tool(const TestToolInfo& tool) {
    report_.tool = tool;
}

void TestRunCollector::set_config(const TestConfigMetadata& config) {
    report_.config = config;
}

void TestRunCollector::begin_run(const std::optional<std::string>& command) {
    run_started_at_ = std::chrono::steady_clock::now();
    TestRunMetadata run;
    run.started_at = now_iso8601_utc();
    run.cwd = std::filesystem::current_path();
    if (command.has_value()) {
        run.command = *command;
    }

    char hostname[256] = {};
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        run.hostname = hostname;
    }

    report_.run = std::move(run);
}

void TestRunCollector::begin_suite(const TestSuiteBegin& suite) {
    flush_current_suite();

    TestSuiteReport suite_report;
    suite_report.path = suite.path;
    suite_report.id = suite.path.string();
    suite_report.name =
        suite.path.filename().empty() ? suite.path.string() : suite.path.filename().string();
    suite_report.plugin = suite.plugin;
    report_.suites.push_back(std::move(suite_report));
    current_suite_ = &report_.suites.back();
    cases_.clear();
    active_.clear();
    last_active_id_.clear();
}

void TestRunCollector::flush_current_suite() {
    if (current_suite_ == nullptr) {
        return;
    }

    for (auto& [_, test_case] : cases_) {
        if (test_case.status == TestStatus::Unknown) {
            continue;
        }
        current_suite_->cases.push_back(test_case);
    }

    std::sort(current_suite_->cases.begin(), current_suite_->cases.end(),
              [](const TestCaseResult& a, const TestCaseResult& b) { return a.id < b.id; });
    cases_.clear();
    active_.clear();
    last_active_id_.clear();
}

TestCaseResult& TestRunCollector::case_for(const std::string& id) {
    auto& test_case = cases_[id];
    if (test_case.id.empty()) {
        test_case.id = id;
        test_case.apply_identity(parse_test_case_id(id));
    }
    return test_case;
}

void TestRunCollector::finalize_attempt(TestCaseResult& test_case, TestStatus status,
                                        const nlohmann::json& event) {
    auto& active = active_[test_case.id];
    const double duration = duration_seconds_from_event(event, active.started_at);
    test_case.finished_at = now_iso8601_utc();
    test_case.duration_seconds = duration;
    test_case.status = status;

    TestAttemptResult attempt;
    attempt.index = static_cast<int>(test_case.attempts.size()) + 1;
    attempt.status = status;
    attempt.duration_seconds = duration;
    if (status == TestStatus::Failed || status == TestStatus::Error) {
        attempt.failure = failure_from_event(event);
        test_case.failure = attempt.failure;
    } else if (status == TestStatus::Skipped || status == TestStatus::Todo) {
        TestSkipInfo skip;
        if (event.contains("msg") && event.at("msg").is_string()) {
            skip.message = event.at("msg").get<std::string>();
        }
        test_case.skip = skip;
    }
    test_case.attempts.push_back(attempt);
}

void TestRunCollector::on_event(const nlohmann::json& event) {
    if (!event.is_object() || !event.contains("event")) {
        return;
    }

    const auto event_type = event.at("event").get<std::string>();

    if (event_type == "phase") {
        if (!event.contains("id")) {
            return;
        }
        auto& test_case = case_for(event.at("id").get<std::string>());
        TestPhaseRecord phase;
        phase.name = event.value("phase", "");
        phase.state = event.value("state", "");
        phase.at = now_iso8601_utc();
        if (event.contains("duration_ms") && event.at("duration_ms").is_number()) {
            phase.duration_seconds = event.at("duration_ms").get<double>() / 1000.0;
        }
        test_case.phases.push_back(std::move(phase));
        return;
    }

    if (event_type == "output") {
        const auto text = event.value("text", std::string{});
        if (text.empty()) {
            return;
        }
        if (event.contains("id")) {
            last_active_id_ = event.at("id").get<std::string>();
        }
        if (last_active_id_.empty()) {
            return;
        }
        auto& test_case = case_for(last_active_id_);
        const auto stream = event.value("stream", "stdout");
        if (stream == "stderr") {
            test_case.stderr_lines.push_back(text);
        } else {
            test_case.stdout_lines.push_back(text);
        }
        return;
    }

    if (!event.contains("id")) {
        return;
    }

    const auto id = event.at("id").get<std::string>();
    last_active_id_ = id;
    auto& test_case = case_for(id);

    if (event_type == "start") {
        active_[id].started_at = std::chrono::steady_clock::now();
        test_case.started_at = now_iso8601_utc();
        return;
    }

    if (event_type == "retry") {
        ++test_case.retries;
        return;
    }

    const auto status = test_status_from_event(event_type);
    if (!is_terminal_status(status)) {
        return;
    }

    finalize_attempt(test_case, status, event);
}

void TestRunCollector::finish_run(int exit_code) {
    flush_current_suite();

    if (!report_.run.has_value()) {
        report_.run = TestRunMetadata{};
    }
    report_.run->finished_at = now_iso8601_utc();
    report_.run->exit_code = exit_code;
    if (run_started_at_.has_value()) {
        report_.run->duration_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - *run_started_at_)
                .count();
    }
}

const TestRunReport& TestRunCollector::build() const {
    return report_;
}

void TestRunCollector::reset() {
    report_ = TestRunReport{};
    current_suite_ = nullptr;
    cases_.clear();
    active_.clear();
    last_active_id_.clear();
    run_started_at_.reset();
}

std::string TestRunCollector::now_iso8601_utc() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = {};
    gmtime_r(&time, &tm);

    std::ostringstream formatted;
    formatted << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return formatted.str();
}

nlohmann::json test_run_report_to_json(const TestRunReport& report) {
    nlohmann::json payload = nlohmann::json::object();
    payload["schema_version"] = report.schema_version;
    payload["tool"] = {{"name", report.tool.name}};
    if (report.tool.version.has_value()) {
        payload["tool"]["version"] = *report.tool.version;
    }

    if (report.run.has_value()) {
        nlohmann::json run = nlohmann::json::object();
        const auto& metadata = *report.run;
        if (metadata.id.has_value()) {
            run["id"] = *metadata.id;
        }
        if (metadata.started_at.has_value()) {
            run["started_at"] = *metadata.started_at;
        }
        if (metadata.finished_at.has_value()) {
            run["finished_at"] = *metadata.finished_at;
        }
        if (metadata.duration_seconds.has_value()) {
            run["duration_seconds"] = *metadata.duration_seconds;
        }
        if (metadata.hostname.has_value()) {
            run["hostname"] = *metadata.hostname;
        }
        if (metadata.cwd.has_value()) {
            run["cwd"] = metadata.cwd->string();
        }
        if (metadata.command.has_value()) {
            run["command"] = *metadata.command;
        }
        if (metadata.exit_code.has_value()) {
            run["exit_code"] = *metadata.exit_code;
        }
        if (!metadata.environment.empty()) {
            run["environment"] = string_map_to_json(metadata.environment);
        }
        payload["run"] = std::move(run);
    }

    if (report.config.has_value()) {
        nlohmann::json config = nlohmann::json::object();
        if (report.config->path.has_value()) {
            config["path"] = report.config->path->string();
        }
        config["projects"] = nlohmann::json::array();
        for (const auto& project : report.config->projects) {
            config["projects"].push_back(project.string());
        }
        payload["config"] = std::move(config);
    }

    payload["summary"] = summary_to_json(report.summary());
    payload["suites"] = nlohmann::json::array();
    for (const auto& suite : report.suites) {
        nlohmann::json suite_json = nlohmann::json::object();
        suite_json["id"] = suite.id;
        suite_json["name"] = suite.name;
        suite_json["path"] = suite.path.string();
        if (suite.plugin.has_value()) {
            suite_json["plugin"] = {{"name", suite.plugin->name},
                                    {"file", suite.plugin->file.string()}};
        }
        if (suite.duration_seconds.has_value()) {
            suite_json["duration_seconds"] = *suite.duration_seconds;
        }
        suite_json["summary"] = summary_to_json(suite.summary());
        if (!suite.properties.empty()) {
            suite_json["properties"] = string_map_to_json(suite.properties);
        }
        suite_json["cases"] = nlohmann::json::array();
        for (const auto& test_case : suite.cases) {
            suite_json["cases"].push_back(test_case_to_json(test_case));
        }
        payload["suites"].push_back(std::move(suite_json));
    }

    return payload;
}

TestRunReport test_run_report_from_json(const nlohmann::json& json) {
    if (!json.is_object()) {
        throw std::runtime_error("test report json must be an object");
    }

    TestRunReport report;
    report.schema_version = json.value("schema_version", kTestReportSchemaVersion);
    if (json.contains("tool") && json.at("tool").is_object()) {
        report.tool.name = json.at("tool").value("name", "teez");
        if (json.at("tool").contains("version") && json.at("tool").at("version").is_string()) {
            report.tool.version = json.at("tool").at("version").get<std::string>();
        }
    }

    if (json.contains("run") && json.at("run").is_object()) {
        TestRunMetadata run;
        const auto& run_json = json.at("run");
        if (run_json.contains("id") && run_json.at("id").is_string()) {
            run.id = run_json.at("id").get<std::string>();
        }
        if (run_json.contains("started_at") && run_json.at("started_at").is_string()) {
            run.started_at = run_json.at("started_at").get<std::string>();
        }
        if (run_json.contains("finished_at") && run_json.at("finished_at").is_string()) {
            run.finished_at = run_json.at("finished_at").get<std::string>();
        }
        run.duration_seconds =
            json_optional_number(run_json.value("duration_seconds", nlohmann::json()));
        if (run_json.contains("hostname") && run_json.at("hostname").is_string()) {
            run.hostname = run_json.at("hostname").get<std::string>();
        }
        if (run_json.contains("cwd") && run_json.at("cwd").is_string()) {
            run.cwd = run_json.at("cwd").get<std::string>();
        }
        if (run_json.contains("command") && run_json.at("command").is_string()) {
            run.command = run_json.at("command").get<std::string>();
        }
        if (run_json.contains("exit_code") && run_json.at("exit_code").is_number_integer()) {
            run.exit_code = run_json.at("exit_code").get<int>();
        }
        run.environment =
            string_map_from_json(run_json.value("environment", nlohmann::json::object()));
        report.run = std::move(run);
    }

    if (json.contains("config") && json.at("config").is_object()) {
        TestConfigMetadata config;
        const auto& config_json = json.at("config");
        if (config_json.contains("path") && config_json.at("path").is_string()) {
            config.path = config_json.at("path").get<std::string>();
        }
        if (config_json.contains("projects") && config_json.at("projects").is_array()) {
            for (const auto& project : config_json.at("projects")) {
                if (project.is_string()) {
                    config.projects.emplace_back(project.get<std::string>());
                }
            }
        }
        report.config = std::move(config);
    }

    if (json.contains("suites") && json.at("suites").is_array()) {
        for (const auto& suite_json : json.at("suites")) {
            if (!suite_json.is_object()) {
                continue;
            }
            TestSuiteReport suite;
            suite.id = suite_json.value("id", "");
            suite.name = suite_json.value("name", suite.id);
            if (suite_json.contains("path") && suite_json.at("path").is_string()) {
                suite.path = suite_json.at("path").get<std::string>();
            }
            if (suite_json.contains("plugin") && suite_json.at("plugin").is_object()) {
                TestPluginInfo plugin;
                plugin.name = suite_json.at("plugin").value("name", "");
                if (suite_json.at("plugin").contains("file") &&
                    suite_json.at("plugin").at("file").is_string()) {
                    plugin.file = suite_json.at("plugin").at("file").get<std::string>();
                }
                suite.plugin = plugin;
            }
            suite.duration_seconds =
                json_optional_number(suite_json.value("duration_seconds", nlohmann::json()));
            suite.properties =
                string_map_from_json(suite_json.value("properties", nlohmann::json::object()));
            if (suite_json.contains("cases") && suite_json.at("cases").is_array()) {
                for (const auto& entry : suite_json.at("cases")) {
                    if (entry.is_object() && entry.contains("id") && entry.contains("status")) {
                        suite.cases.push_back(test_case_from_json(entry));
                    }
                }
            }
            report.suites.push_back(std::move(suite));
        }
        return report;
    }

    if (json.contains("cases") && json.at("cases").is_array()) {
        TestSuiteReport suite;
        suite.id = ".";
        suite.name = ".";
        for (const auto& entry : json.at("cases")) {
            if (entry.is_object() && entry.contains("id") && entry.contains("status")) {
                suite.cases.push_back(test_case_from_json(entry));
            }
        }
        report.suites.push_back(std::move(suite));
    }

    return report;
}

} // namespace teez::core
