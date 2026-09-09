#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "teez/core/discovery.hpp"
#include "teez/core/runner.hpp"
#include "worker_test_support.hpp"

#ifndef TEEZ_PLUGIN_DIR
#error "TEEZ_PLUGIN_DIR must be defined"
#endif

namespace {

const std::filesystem::path kPluginsDir = TEEZ_PLUGIN_DIR;
const std::filesystem::path kCoverageFixtures =
    std::filesystem::path(__FILE__).parent_path() / "fixtures" / "coverage";
const std::filesystem::path kWorkerFixture =
    std::filesystem::path(__FILE__).parent_path() / "fixtures" / "teez-worker-demo";

std::filesystem::path make_temp_dir(const char* suffix) {
    const auto dir =
        std::filesystem::temp_directory_path() / ("teez_runner_coverage_" + std::string(suffix));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

}  // namespace

TEST_CASE("run_coverage_context executes plugin coverage program", "[runner][coverage]") {
    const auto temp_dir = make_temp_dir("context");
    std::ofstream(temp_dir / "coverage.json") << R"({
        "name": "coverage-test",
        "plugin": "coverage_plugin.lua",
        "anchors": ["marker.txt"],
        "priority": 9999
    })";
    std::ofstream(temp_dir / "marker.txt") << "marker\n";
    std::ofstream(temp_dir / "coverage_plugin.lua") << R"(
function build_coverage_run(context)
    return {
        command = "true",
        cwd = context.target_path,
        report = "coverage.lcov",
        reporter = "json",
        output = "coverage/report.json",
        min_line_rate = 0.1,
    }
end

function parse_line(line)
    return { event = "output", text = line }
end
)";

    std::filesystem::copy_file(kCoverageFixtures / "sample.lcov", temp_dir / "coverage.lcov");

    teez::core::DiscoveryContext discovery;
    discovery.bundled_plugins_dir = kPluginsDir;
    discovery.project_plugin_dirs = {temp_dir};

    teez::core::RunContext context{
        .command = "run",
        .target_path = temp_dir,
    };

    std::vector<nlohmann::json> events;
    const int exit_code = teez::core::run_coverage_context(
        discovery, context, [&](const nlohmann::json& event) { events.push_back(event); });

    REQUIRE(exit_code == 0);
    REQUIRE(std::filesystem::exists(temp_dir / "coverage" / "report.json"));
    REQUIRE_FALSE(events.empty());
}

TEST_CASE("run_coverage_context throws when plugin lacks coverage support", "[runner][coverage]") {
    const auto temp_dir = make_temp_dir("no_coverage");
    std::ofstream(temp_dir / "fuzz.teez.lua") << "-- test\n";

    teez::core::RunContext context{
        .command = "run",
        .target_path = temp_dir,
    };

    REQUIRE_THROWS_AS(teez::core::run_coverage_context(kPluginsDir, context), std::runtime_error);
}

#ifdef TEEZ_EMBEDDED_WORKER
TEST_CASE("list_context returns worker demo tests when embedded worker is available",
          "[runner][list]") {
    if (!std::filesystem::exists(kWorkerFixture / "smoke.teez.lua")) {
        SKIP("worker demo fixture missing");
    }

    teez::core::test_support::configure_embedded_worker();

    teez::core::RunContext context{
        .command = "list",
        .target_path = kWorkerFixture,
    };

    const auto tests = teez::core::list_context(kPluginsDir, context);
    REQUIRE_FALSE(tests.empty());
    REQUIRE(std::find(tests.begin(), tests.end(), std::string{}) == tests.end());
}
#endif

TEST_CASE("parse_plugin_event_json accepts compact json payloads", "[runner]") {
    const auto event = teez::core::parse_plugin_event_json(R"({"event":"pass","id":"alpha"})");
    REQUIRE(event.has_value());
    REQUIRE(event->at("event") == "pass");
}

TEST_CASE("make_ndjson_event_writer emits one json object per line", "[runner]") {
    std::ostringstream out;
    const auto on_event = teez::core::make_ndjson_event_writer(out);
    on_event({{"event", "start"}, {"id", "demo"}});
    on_event({{"event", "pass"}, {"id", "demo"}});

    std::istringstream lines(out.str());
    std::string line;
    int count = 0;
    while (std::getline(lines, line)) {
        const auto json = nlohmann::json::parse(line);
        REQUIRE(json.is_object());
        ++count;
    }
    REQUIRE(count == 2);
}
