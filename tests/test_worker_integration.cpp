#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "teez/core/runner.hpp"
#include "worker_test_support.hpp"

#ifndef TEEZ_PLUGIN_DIR
#error "TEEZ_PLUGIN_DIR must be defined"
#endif

namespace {

const std::filesystem::path kPluginsDir = TEEZ_PLUGIN_DIR;
const std::filesystem::path kFixtureDir =
    std::filesystem::path(__FILE__).parent_path() / "fixtures" / "teez-worker-demo";

}  // namespace

TEST_CASE("worker plugin end-to-end via teez-core", "[worker][integration]") {
    if (!teez::core::test_support::embedded_worker_available()) {
        SKIP("embedded worker not available (teez-worker sibling missing at configure time)");
    }
    if (!std::filesystem::exists(kFixtureDir / "smoke.teez.lua")) {
        SKIP("worker demo fixture missing");
    }

    teez::core::test_support::configure_embedded_worker();

    teez::core::RunContext ctx{
        .command = "run",
        .target_path = kFixtureDir,
    };

    std::ostringstream output;
    const int exit_code = teez::core::run_context(kPluginsDir, ctx, output);

    REQUIRE(exit_code == 0);

    std::istringstream lines(output.str());
    std::string line;
    bool saw_start = false;
    bool saw_pass = false;

    while (std::getline(lines, line)) {
        const auto json = nlohmann::json::parse(line);
        const std::string event = json.at("event").get<std::string>();
        if (event == "start") {
            saw_start = true;
        }
        if (event == "pass") {
            saw_pass = true;
        }
    }

    REQUIRE(saw_start);
    REQUIRE(saw_pass);
}
