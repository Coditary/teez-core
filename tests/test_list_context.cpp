#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>

#include "teez/core/plugin_config.hpp"
#include "teez/core/runner.hpp"
#include "teez/core/teez_config.hpp"
#include "worker_test_support.hpp"

#ifndef TEEZ_PLUGIN_DIR
#error "TEEZ_PLUGIN_DIR must be defined"
#endif

namespace {

const std::filesystem::path kPluginsDir = TEEZ_PLUGIN_DIR;
const std::filesystem::path kPytestFixture =
    std::filesystem::path(__FILE__).parent_path() / "fixtures" / "pytest-demo";
const std::filesystem::path kWorkerFixture =
    std::filesystem::path(__FILE__).parent_path() / "fixtures" / "teez-worker-demo";

}  // namespace

TEST_CASE("list_context returns worker test ids", "[runner]") {
    if (!teez::core::test_support::embedded_worker_available()) {
        SKIP("embedded worker not available (teez-worker sibling missing at configure time)");
    }
    if (!std::filesystem::exists(kWorkerFixture / "smoke.teez.lua")) {
        SKIP("worker demo fixture missing");
    }

    teez::core::test_support::configure_embedded_worker();

    const auto discovery = teez::core::DiscoveryContext::bundled_only(kPluginsDir);

    teez::core::RunContext ctx{
        .command = "list",
        .target_path = kWorkerFixture,
    };

    const auto tests = teez::core::list_context(discovery, ctx);

    REQUIRE_FALSE(tests.empty());
    const bool has_smoke_test = std::any_of(tests.begin(), tests.end(), [](const std::string& id) {
        return id.find("smoke.teez.lua") != std::string::npos;
    });
    REQUIRE(has_smoke_test);
}

TEST_CASE("list_context returns pytest test ids", "[runner]") {
    if (!std::filesystem::exists(kPytestFixture / "pytest.ini")) {
        SKIP("pytest demo fixture missing");
    }
    const int has_pytest =
        std::system("pytest --version > /dev/null 2>&1") == 0
            ? 1
            : std::system("python3 -m pytest --version > /dev/null 2>&1") == 0;
    if (has_pytest == 0) {
        SKIP("pytest not installed");
    }

    const auto config = teez::core::TeezConfig::load(kPytestFixture.parent_path());
    const auto discovery = teez::core::make_discovery_context(config, kPluginsDir);

    teez::core::RunContext ctx{
        .command = "list",
        .target_path = kPytestFixture,
    };

    const auto tests = teez::core::list_context(discovery, ctx);

    REQUIRE_FALSE(tests.empty());
    REQUIRE(tests.front().find("test_") != std::string::npos);
}
