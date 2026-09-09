#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "teez/core/runner.hpp"

#ifndef TEEZ_PLUGIN_DIR
#error "TEEZ_PLUGIN_DIR must be defined"
#endif

namespace {

const std::filesystem::path kDummyPlugin =
    std::filesystem::path(TEEZ_PLUGIN_DIR) / "dummy.lua";

std::filesystem::path write_script(const std::string& name, const std::string& content) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream file(path);
    file << content;
    file.close();
    std::filesystem::permissions(path, std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read |
                                             std::filesystem::perms::owner_write);
    return path;
}

bool line_is_ndjson(const std::string& line) {
    const auto json = nlohmann::json::parse(line, nullptr, false);
    return !json.is_discarded();
}

}  // namespace

TEST_CASE("run_with_plugin streams NDJSON lines", "[runner]") {
    const auto script = write_script(
        "teez_runner_demo.sh",
        "#!/usr/bin/env bash\n"
        "echo Test\n"
        "echo Test\n"
        "echo Test\n");

    teez::core::RunContext ctx{
        .command = "run",
        .target_path = script,
    };

    std::ostringstream output;
    const int exit_code = teez::core::run_with_plugin(kDummyPlugin, ctx, output);

    REQUIRE(exit_code == 0);

    std::istringstream lines(output.str());
    std::string line;
    int count = 0;
    while (std::getline(lines, line)) {
        REQUIRE(line_is_ndjson(line));
        const auto json = nlohmann::json::parse(line);
        REQUIRE(json.at("event") == "output");
        REQUIRE(json.at("text") == "LUA SAGT: Test");
        ++count;
    }
    REQUIRE(count == 3);
}

TEST_CASE("run_context throws when no plugin matches target", "[runner]") {
    const auto empty_dir = std::filesystem::temp_directory_path() / "teez_runner_no_plugin";
    std::filesystem::create_directories(empty_dir);

    teez::core::RunContext ctx{
        .command = "run",
        .target_path = empty_dir,
    };

    std::ostringstream output;
    REQUIRE_THROWS_AS(teez::core::run_context(
                          std::filesystem::path(TEEZ_PLUGIN_DIR), ctx, output),
                      std::runtime_error);
}

TEST_CASE("list_context throws when no plugin matches target", "[runner]") {
    const auto empty_dir = std::filesystem::temp_directory_path() / "teez_runner_list_no_plugin";
    std::filesystem::create_directories(empty_dir);

    teez::core::RunContext ctx{
        .command = "list",
        .target_path = empty_dir,
    };

    REQUIRE_THROWS_AS(
        teez::core::list_context(std::filesystem::path(TEEZ_PLUGIN_DIR), ctx),
        std::runtime_error);
}

TEST_CASE("run_with_plugin treats fail events as non-zero exit", "[runner]") {
    const auto plugin_path = std::filesystem::temp_directory_path() / "teez_runner_fail_plugin.lua";
    {
        std::ofstream file(plugin_path);
        file << R"(
function build_command(context)
    return { command = "bash", args = { "-c", "echo ok" } }
end

function parse_line(line)
    return { event = "fail", id = "demo", message = "boom" }
end
)";
    }

    teez::core::RunContext ctx{
        .command = "run",
        .target_path = ".",
    };

    std::ostringstream output;
    const int exit_code = teez::core::run_with_plugin(plugin_path, ctx, output);

    REQUIRE(exit_code == 1);
    REQUIRE(output.str().find("\"event\":\"fail\"") != std::string::npos);
}

TEST_CASE("run_with_plugin ignores invalid ndjson failure payloads", "[runner]") {
    const auto plugin_path =
        std::filesystem::temp_directory_path() / "teez_runner_invalid_fail_plugin.lua";
    {
        std::ofstream file(plugin_path);
        file << R"(
function build_command(context)
    return { command = "bash", args = { "-c", "echo ok" } }
end

function parse_line(line)
    return { event = "fail", id = "demo" }
end
)";
    }

    teez::core::RunContext ctx{
        .command = "run",
        .target_path = ".",
    };

    std::ostringstream output;
    const int exit_code = teez::core::run_with_plugin(plugin_path, ctx, output);

    REQUIRE(exit_code == 1);
}
