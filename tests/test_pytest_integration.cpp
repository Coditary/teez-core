#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "teez/core/exit_code.hpp"
#include "teez/core/plugin_config.hpp"
#include "teez/core/runner.hpp"
#include "teez/core/teez_config.hpp"

#ifndef TEEZ_PLUGIN_DIR
#error "TEEZ_PLUGIN_DIR must be defined"
#endif

namespace {

const std::filesystem::path kPluginsDir = TEEZ_PLUGIN_DIR;
const std::filesystem::path kFixtureDir =
    std::filesystem::path(__FILE__).parent_path() / "fixtures" / "pytest-demo";

bool has_pytest() {
    const int code = std::system("pytest --version > /dev/null 2>&1");
    if (code == 0) {
        return true;
    }
    return std::system("python3 -m pytest --version > /dev/null 2>&1") == 0;
}

}  // namespace

TEST_CASE("pytest plugin end-to-end emits NDJSON start/pass/fail", "[pytest][integration]") {
    if (!has_pytest()) {
        SKIP("pytest not installed");
    }
    if (!std::filesystem::exists(kFixtureDir / "pytest.ini")) {
        SKIP("pytest demo fixture missing");
    }

    const auto config = teez::core::TeezConfig::load(kFixtureDir.parent_path());
    teez::core::set_active_config(config);
    const auto discovery = teez::core::make_discovery_context(config, kPluginsDir);

    teez::core::RunContext ctx{
        .command = "run",
        .target_path = kFixtureDir,
    };

    std::ostringstream output;
    const int exit_code = teez::core::run_context(discovery, ctx, output);
    REQUIRE(exit_code == teez::core::kExitFailure);

    std::istringstream lines(output.str());
    std::string line;
    bool saw_start = false;
    bool saw_pass = false;
    bool saw_fail = false;

    while (std::getline(lines, line)) {
        const auto json = nlohmann::json::parse(line);
        const std::string event = json.at("event").get<std::string>();

        if (event == "start") {
            saw_start = true;
            REQUIRE(json.contains("id"));
        } else if (event == "pass") {
            saw_pass = true;
            REQUIRE(json.contains("id"));
        } else if (event == "fail") {
            saw_fail = true;
            REQUIRE(json.contains("id"));
            REQUIRE(json.contains("msg"));
        }
    }

    REQUIRE(saw_start);
    REQUIRE(saw_pass);
    REQUIRE(saw_fail);
}
