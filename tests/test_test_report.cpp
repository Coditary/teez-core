#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include "teez/core/test_report.hpp"

TEST_CASE("TestRunCollector builds canonical report from events", "[test_report]") {
    teez::core::TestRunCollector collector;
    collector.begin_run("run");
    collector.begin_suite({.path = "/proj"});
    collector.on_event({{"event", "start"}, {"id", "alpha::one"}});
    collector.on_event({{"event", "pass"}, {"id", "alpha::one"}, {"duration_ms", 12}});
    collector.on_event({{"event", "start"}, {"id", "alpha::two"}});
    collector.on_event({{"event", "fail"}, {"id", "alpha::two"}, {"msg", "boom"}});
    collector.on_event({{"event", "output"}, {"text", "stderr line\n"}, {"stream", "stderr"}});
    collector.on_event({{"event", "skip"}, {"id", "alpha::three"}});
    collector.finish_run(0);

    const auto report = collector.build();
    const auto summary = report.summary();

    REQUIRE(report.schema_version == teez::core::kTestReportSchemaVersion);
    REQUIRE(report.suites.size() == 1);
    REQUIRE(report.all_cases().size() == 3);
    REQUIRE(summary.passed == 1);
    REQUIRE(summary.failed == 1);
    REQUIRE(summary.skipped == 1);
    REQUIRE(summary.total() == 3);
}

TEST_CASE("TestRunReport merge combines project results", "[test_report]") {
    teez::core::TestRunReport left;
    left.suites.push_back({.id = "left",
                             .name = "left",
                             .cases = {{.id = "a", .status = teez::core::TestStatus::Passed}}});
    teez::core::TestRunReport right;
    right.suites.push_back({.id = "right",
                              .name = "right",
                              .cases = {{.id = "b", .status = teez::core::TestStatus::Failed}}});

    left.merge(right);

    REQUIRE(left.suites.size() == 2);
    REQUIRE(left.summary().total() == 2);
}

TEST_CASE("test_run_report json roundtrip preserves cases", "[test_report]") {
    teez::core::TestRunReport report;
    report.suites.push_back({.id = "suite",
                             .name = "suite",
                             .cases = {{.id = "suite::case",
                                        .name = "case",
                                        .status = teez::core::TestStatus::Failed,
                                        .failure = teez::core::TestFailureInfo{.message = "nope"},
                                        .stderr_lines = {"line 1"}}}});

    const auto json = teez::core::test_run_report_to_json(report);
    const auto restored = teez::core::test_run_report_from_json(json);

    REQUIRE(restored.suites.size() == 1);
    REQUIRE(restored.suites.front().cases.size() == 1);
    REQUIRE(restored.suites.front().cases.front().id == "suite::case");
    REQUIRE(restored.suites.front().cases.front().status == teez::core::TestStatus::Failed);
    REQUIRE(restored.suites.front().cases.front().failure->message == "nope");
    REQUIRE(restored.suites.front().cases.front().stderr_lines ==
            std::vector<std::string>{"line 1"});
}

TEST_CASE("test_run_report_from_json imports legacy flat cases", "[test_report]") {
    const nlohmann::json legacy = {
        {"cases",
         nlohmann::json::array({{{"id", "legacy::one"}, {"status", "passed"}},
                                {{"id", "legacy::two"}, {"status", "failed"}, {"message", "x"}}})}};

    const auto report = teez::core::test_run_report_from_json(legacy);
    REQUIRE(report.suites.size() == 1);
    REQUIRE(report.all_cases().size() == 2);
}
