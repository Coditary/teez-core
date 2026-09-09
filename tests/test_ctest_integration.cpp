#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
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
    std::filesystem::path(__FILE__).parent_path() / "fixtures" / "ctest-demo";
const std::filesystem::path kFixtureBuild = kFixtureDir / "build";

bool ensure_fixture_build() {
    if (std::filesystem::exists(kFixtureBuild / "CTestTestfile.cmake")) {
        return true;
    }

    std::filesystem::create_directories(kFixtureBuild);
    const auto configure_cmd =
        "cmake -B \"" + kFixtureBuild.string() + "\" -S \"" + kFixtureDir.string() + "\"";
    return std::system(configure_cmd.c_str()) == 0;
}

}  // namespace

TEST_CASE("ctest plugin end-to-end via isolated fixture", "[ctest][integration]") {
    if (!std::filesystem::exists(kFixtureDir / "CMakeLists.txt")) {
        SKIP("ctest demo fixture missing");
    }
    if (!ensure_fixture_build()) {
        SKIP("failed to configure ctest demo fixture");
    }

    const auto config = teez::core::TeezConfig::load(kFixtureDir);
    teez::core::set_active_config(config);
    const auto discovery = teez::core::make_discovery_context(config, kPluginsDir);

    teez::core::RunContext ctx{
        .command = "run",
        .target_path = kFixtureDir,
    };

    std::ostringstream output;
    const int exit_code = teez::core::run_context(discovery, ctx, output);

    REQUIRE(exit_code == teez::core::kExitSuccess);

    std::istringstream lines(output.str());
    std::string line;
    bool saw_start = false;
    bool saw_pass = false;
    int start_count = 0;

    while (std::getline(lines, line)) {
        const auto json = nlohmann::json::parse(line);
        const std::string event = json.at("event").get<std::string>();
        if (event == "start") {
            saw_start = true;
            ++start_count;
        }
        if (event == "pass") {
            saw_pass = true;
        }
    }

    REQUIRE(saw_start);
    REQUIRE(saw_pass);
    REQUIRE(start_count == 1);
}
