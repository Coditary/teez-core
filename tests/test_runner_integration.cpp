#include <catch2/catch_test_macros.hpp>

#include <sstream>

#include "teez/core/runner.hpp"
#include "worker_test_support.hpp"

#ifndef TEEZ_PLUGIN_DIR
#error "TEEZ_PLUGIN_DIR must be defined"
#endif

namespace {

const std::filesystem::path kPluginsDir = TEEZ_PLUGIN_DIR;
const std::filesystem::path kWorkerFixture =
    std::filesystem::path(__FILE__).parent_path() / "fixtures" / "teez-worker-demo";

}  // namespace

TEST_CASE("run_context executes worker demo fixture end-to-end", "[runner][integration]") {
    if (!teez::core::test_support::embedded_worker_available()) {
        SKIP("embedded worker not available");
    }
    if (!std::filesystem::exists(kWorkerFixture / "smoke.teez.lua")) {
        SKIP("worker demo fixture missing");
    }

    teez::core::test_support::configure_embedded_worker();

    teez::core::RunContext context{
        .command = "run",
        .target_path = kWorkerFixture,
    };

    std::ostringstream output;
    const int exit_code = teez::core::run_context(kPluginsDir, context, output);

    REQUIRE(exit_code == 0);
    REQUIRE(output.str().find("\"event\":\"pass\"") != std::string::npos);
}

TEST_CASE("run_context streams events through callback", "[runner][integration]") {
    if (!teez::core::test_support::embedded_worker_available()) {
        SKIP("embedded worker not available");
    }
    if (!std::filesystem::exists(kWorkerFixture / "smoke.teez.lua")) {
        SKIP("worker demo fixture missing");
    }

    teez::core::test_support::configure_embedded_worker();

    teez::core::RunContext context{
        .command = "run",
        .target_path = kWorkerFixture,
    };

    int event_count = 0;
    const int exit_code = teez::core::run_context(
        teez::core::DiscoveryContext::bundled_only(kPluginsDir), context,
        [&](const nlohmann::json& event) {
            REQUIRE(event.contains("event"));
            ++event_count;
        });

    REQUIRE(exit_code == 0);
    REQUIRE(event_count > 0);
}
