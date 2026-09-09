#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include "teez/core/test_report.hpp"

TEST_CASE("test status helpers cover all states", "[test_report]") {
    REQUIRE(teez::core::test_status_to_string(teez::core::TestStatus::Passed) == "passed");
    REQUIRE(teez::core::test_status_to_string(teez::core::TestStatus::Failed) == "failed");
    REQUIRE(teez::core::test_status_to_string(teez::core::TestStatus::Skipped) == "skipped");
    REQUIRE(teez::core::test_status_to_string(teez::core::TestStatus::Todo) == "todo");
    REQUIRE(teez::core::test_status_to_string(teez::core::TestStatus::Error) == "error");
    REQUIRE(teez::core::test_status_to_string(teez::core::TestStatus::Unknown) == "unknown");

    REQUIRE(teez::core::test_status_from_string("passed") == teez::core::TestStatus::Passed);
    REQUIRE(teez::core::test_status_from_string("todo") == teez::core::TestStatus::Todo);
    REQUIRE(teez::core::test_status_from_string("error") == teez::core::TestStatus::Error);
    REQUIRE(teez::core::test_status_from_event("todo") == teez::core::TestStatus::Todo);
    REQUIRE(teez::core::test_status_from_event("error") == teez::core::TestStatus::Error);
}

TEST_CASE("parse_test_case_id supports suite and type layouts", "[test_report]") {
    const auto nested = teez::core::parse_test_case_id("smoke.teez.lua::API > Health > returns 200");
    REQUIRE(nested.file == "smoke.teez.lua");
    REQUIRE(nested.name == "returns 200");
    REQUIRE(nested.suite_path == std::vector<std::string>{"API", "Health"});
    REQUIRE(nested.classname == "smoke.teez.lua::API > Health");

    const auto typed = teez::core::parse_test_case_id("demo.teez.lua::unit::checks basics");
    REQUIRE(typed.file == "demo.teez.lua");
    REQUIRE(typed.name == "checks basics");
    REQUIRE(typed.suite_path == std::vector<std::string>{"unit"});
    REQUIRE(typed.classname == "demo.teez.lua::unit");

    const auto plain = teez::core::parse_test_case_id("plain-id");
    REQUIRE(plain.name == "plain-id");
    REQUIRE_FALSE(plain.file.has_value());
}

TEST_CASE("TestRunCollector records retries phases stdout stderr and errors", "[test_report]") {
    teez::core::TestRunCollector collector;
    collector.set_tool({.name = "teez", .version = "0.1.0"});
    collector.set_config({.path = "/proj/teez.config.lua",
                            .projects = {std::filesystem::path("/proj/demo")}});
    collector.begin_run("run ./tests");
    collector.begin_suite({.path = "/proj/tests/smoke.teez.lua"});

    collector.on_event({{"event", "start"}, {"id", "smoke.teez.lua::Suite > case"}});
    collector.on_event({{"event", "output"}, {"id", "smoke.teez.lua::Suite > case"}, {"text", "log"},
                        {"stream", "stdout"}});
    collector.on_event({{"event", "output"}, {"id", "smoke.teez.lua::Suite > case"},
                        {"text", "stderr line"}, {"stream", "stderr"}});
    collector.on_event({{"event", "phase"}, {"id", "smoke.teez.lua::Suite > case"}, {"phase", "setup"},
                        {"state", "start"}, {"duration_ms", 5}});
    collector.on_event({{"event", "retry"}, {"id", "smoke.teez.lua::Suite > case"}});
    collector.on_event({{"event", "error"}, {"id", "smoke.teez.lua::Suite > case"}, {"msg", "boom"},
                        {"type", "runtime"}, {"stacktrace", "trace"}});
    collector.on_event({{"event", "start"}, {"id", "smoke.teez.lua::Suite > todo case"}});
    collector.on_event({{"event", "todo"}, {"id", "smoke.teez.lua::Suite > todo case"},
                        {"msg", "later"}});
    collector.finish_run(1);

    const auto report = collector.build();
    REQUIRE(report.tool.version == "0.1.0");
    REQUIRE(report.config.has_value());
    REQUIRE(report.config->projects.size() == 1);
    REQUIRE(report.run.has_value());
    REQUIRE(report.run->command == "run ./tests");
    REQUIRE(report.run->exit_code == 1);
    REQUIRE(report.run->hostname.has_value());
    REQUIRE(report.run->cwd.has_value());
    REQUIRE(report.summary().errors == 1);
    REQUIRE(report.summary().todo == 1);
    REQUIRE(report.summary().has_failures());

    const auto& error_case = report.suites.front().cases.front();
    REQUIRE(error_case.retries == 1);
    REQUIRE(error_case.phases.size() == 1);
    REQUIRE(error_case.stdout_lines == std::vector<std::string>{"log"});
    REQUIRE(error_case.stderr_lines == std::vector<std::string>{"stderr line"});
    REQUIRE(error_case.failure.has_value());
    REQUIRE(error_case.failure->type == "runtime");
    REQUIRE(error_case.failure->stacktrace == "trace");
    REQUIRE(error_case.attempts.size() == 1);

    const auto& todo_case = report.suites.front().cases.back();
    REQUIRE(todo_case.status == teez::core::TestStatus::Todo);
    REQUIRE(todo_case.skip.has_value());
    REQUIRE(todo_case.skip->message == "later");
}

TEST_CASE("TestRunCollector reset clears previous run state", "[test_report]") {
    teez::core::TestRunCollector collector;
    collector.begin_run("first");
    collector.begin_suite({.path = "/first"});
    collector.on_event({{"event", "start"}, {"id", "first::case"}});
    collector.on_event({{"event", "pass"}, {"id", "first::case"}});
    collector.finish_run(0);

    collector.reset();
    collector.begin_run("second");
    collector.begin_suite({.path = "/second"});
    collector.on_event({{"event", "start"}, {"id", "second::case"}});
    collector.on_event({{"event", "fail"}, {"id", "second::case"}, {"msg", "nope"}});
    collector.finish_run(1);

    const auto report = collector.build();
    REQUIRE(report.suites.size() == 1);
    REQUIRE(report.suites.front().path == "/second");
    REQUIRE(report.summary().failed == 1);
}

TEST_CASE("test_run_report json roundtrip preserves metadata and attempts", "[test_report]") {
    teez::core::TestRunReport report;
    report.tool = {.name = "teez", .version = "0.1.0"};
    report.run = teez::core::TestRunMetadata{
        .id = "run-1",
        .command = "teez run .",
        .exit_code = 0,
        .environment = {{"CI", "true"}},
    };
    report.config = teez::core::TestConfigMetadata{
        .path = "/proj/teez.config.lua",
        .projects = {std::filesystem::path("/proj/demo")},
    };
    report.suites.push_back(
        {.id = "suite",
         .name = "suite",
         .path = "/proj/demo",
         .plugin = teez::core::TestPluginInfo{.name = "teez-worker", .file = "/plugins/worker.lua"},
         .duration_seconds = 1.5,
         .cases = {{.id = "suite::case",
                    .name = "case",
                    .classname = "suite",
                    .suite_path = {"suite"},
                    .file = "case.teez.lua",
                    .line = 12,
                    .column = 3,
                    .status = teez::core::TestStatus::Failed,
                    .duration_seconds = 0.5,
                    .failure = teez::core::TestFailureInfo{.message = "boom", .type = "assert"},
                    .stdout_lines = {"out"},
                    .stderr_lines = {"err"},
                    .retries = 2,
                    .attempts = {{.index = 1,
                                  .status = teez::core::TestStatus::Failed,
                                  .duration_seconds = 0.5,
                                  .failure = teez::core::TestFailureInfo{.message = "boom"}}},
                    .phases = {{.name = "setup", .state = "end", .duration_seconds = 0.1}},
                    .properties = {{"owner", "qa"}}}}});

    const auto json = teez::core::test_run_report_to_json(report);
    const auto restored = teez::core::test_run_report_from_json(json);

    REQUIRE(restored.tool.version == "0.1.0");
    REQUIRE(restored.run->id == "run-1");
    REQUIRE(restored.run->environment.at("CI") == "true");
    REQUIRE(restored.config->projects.front() == "/proj/demo");
    REQUIRE(restored.suites.front().plugin->name == "teez-worker");
    REQUIRE(restored.suites.front().cases.front().line == 12);
    REQUIRE(restored.suites.front().cases.front().retries == 2);
    REQUIRE(restored.suites.front().cases.front().properties.at("owner") == "qa");
    REQUIRE(restored.suites.front().cases.front().phases.front().name == "setup");
}
