#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "teez/core/plugin.hpp"

#ifndef TEEZ_PLUGIN_DIR
#error "TEEZ_PLUGIN_DIR must be defined"
#endif

namespace {

const std::filesystem::path kDummyPlugin =
    std::filesystem::path(TEEZ_PLUGIN_DIR) / "dummy.lua";

}  // namespace

TEST_CASE("dummy.lua exists at build-time plugin dir", "[plugin]") {
    REQUIRE(std::filesystem::exists(kDummyPlugin));
}

TEST_CASE("build_command returns echo command with target path", "[plugin]") {
    teez::core::RunContext ctx{
        .command = "run",
        .target_path = "/tmp/teez-test-target",
    };

    const auto spec = teez::core::call_build_command(kDummyPlugin, ctx);

    REQUIRE(spec.command == "echo");
    REQUIRE_FALSE(spec.args.empty());
    REQUIRE(spec.args[0].find("/tmp/teez-test-target") != std::string::npos);
}

TEST_CASE("parse_line returns NDJSON with LUA SAGT prefix", "[plugin]") {
    const auto json = teez::core::call_parse_line(kDummyPlugin, "hello world");

    REQUIRE(json.find("\"event\"") != std::string::npos);
    REQUIRE(json.find("\"output\"") != std::string::npos);
    REQUIRE(json.find("LUA SAGT: hello world") != std::string::npos);
}

TEST_CASE("build_command runs bash for shell scripts", "[plugin]") {
    const auto script = std::filesystem::temp_directory_path() / "teez_plugin_script.sh";
    {
        std::ofstream file(script);
        file << "#!/usr/bin/env bash\necho ok\n";
    }

    teez::core::RunContext ctx{
        .command = "run",
        .target_path = script,
    };

    const auto spec = teez::core::call_build_command(kDummyPlugin, ctx);

    REQUIRE(spec.command == "bash");
    REQUIRE_FALSE(spec.args.empty());
    REQUIRE(spec.args[0] == script.string());
}

TEST_CASE("parse_line returns empty json for nil plugin response", "[plugin]") {
    const auto plugin_path = std::filesystem::temp_directory_path() / "teez_nil_parse.lua";
    {
        std::ofstream file(plugin_path);
        file << R"(
function build_command(context)
    return { command = "bash", args = { "-c", "echo ok" } }
end

function parse_line(line)
    return nil
end
)";
    }

    teez::core::Plugin plugin(plugin_path);
    REQUIRE(plugin.parse_line_ndjson("ignored").empty());
}

TEST_CASE("Plugin constructor rejects missing plugin file", "[plugin]") {
    REQUIRE_THROWS_AS(
        teez::core::Plugin("/tmp/teez-plugin-does-not-exist.lua"),
        std::runtime_error);
}

TEST_CASE("Plugin constructor rejects invalid lua plugin", "[plugin]") {
    const auto plugin_path = std::filesystem::temp_directory_path() / "teez_invalid_plugin.lua";
    std::ofstream(plugin_path) << "this is not valid lua {{{";

    REQUIRE_THROWS_AS(teez::core::Plugin(plugin_path), std::runtime_error);
}

TEST_CASE("build_command propagates plugin runtime errors", "[plugin]") {
    const auto plugin_path = std::filesystem::temp_directory_path() / "teez_build_error.lua";
    {
        std::ofstream file(plugin_path);
        file << R"(
function build_command(context)
    error("build failed intentionally")
end

function parse_line(line)
    return { event = "output", text = line }
end
)";
    }

    teez::core::RunContext ctx{
        .command = "run",
        .target_path = ".",
    };

    teez::core::Plugin plugin(plugin_path);
    REQUIRE_THROWS_AS(plugin.build_command(ctx), std::runtime_error);
}

TEST_CASE("list_tests returns ids from plugin list function", "[plugin]") {
    const auto plugin_path = std::filesystem::temp_directory_path() / "teez_list_plugin.lua";
    {
        std::ofstream file(plugin_path);
        file << R"(
function list(context)
    return { "alpha", "beta" }
end

function build_command(context)
    return { command = "bash", args = { "-c", "echo ok" } }
end

function parse_line(line)
    return { event = "output", text = line }
end
)";
    }

    teez::core::RunContext ctx{
        .command = "list",
        .target_path = ".",
    };

    teez::core::Plugin plugin(plugin_path);
    const auto tests = plugin.list_tests(ctx);

    REQUIRE(tests.size() == 2);
    REQUIRE(tests[0] == "alpha");
    REQUIRE(tests[1] == "beta");
}

TEST_CASE("parse_line propagates plugin runtime errors", "[plugin]") {
    const auto plugin_path = std::filesystem::temp_directory_path() / "teez_parse_error.lua";
    {
        std::ofstream file(plugin_path);
        file << R"(
function build_command(context)
    return { command = "bash", args = { "-c", "echo ok" } }
end

function parse_line(line)
    error("parse failed intentionally")
end
)";
    }

    teez::core::Plugin plugin(plugin_path);
    REQUIRE_THROWS_AS(plugin.parse_line_ndjson("line"), std::runtime_error);
}

TEST_CASE("build_command passes ctest filters into lua context", "[plugin]") {
    const auto plugin_path = std::filesystem::temp_directory_path() / "teez_ctest_context.lua";
    {
        std::ofstream file(plugin_path);
        file << R"(
function build_command(context)
    assert(context.ctest.regex == "discovery")
    assert(context.ctest.exclude == "integration")
    assert(context.ctest.label == "fast")
    assert(context.ctest.exclude_label == "slow")
    return { command = "bash", args = { "-c", "echo ok" } }
end

function parse_line(line)
    return { event = "output", text = line }
end
)";
    }

    teez::core::RunContext ctx{
        .command = "run",
        .target_path = ".",
        .ctest =
            {
                .regex = std::string("discovery"),
                .exclude = std::string("integration"),
                .label = std::string("fast"),
                .exclude_label = std::string("slow"),
            },
    };

    teez::core::Plugin plugin(plugin_path);
    const auto spec = plugin.build_command(ctx);
    REQUIRE(spec.command == "bash");
}

TEST_CASE("Plugin rejects plugins without build_command", "[plugin]") {
    const auto plugin_path = std::filesystem::temp_directory_path() / "teez_missing_build.lua";
    std::ofstream(plugin_path) << R"(
function parse_line(line)
    return { event = "output", text = line }
end
)";

    teez::core::RunContext ctx{
        .command = "run",
        .target_path = ".",
    };

    teez::core::Plugin plugin(plugin_path);
    REQUIRE_THROWS_AS(plugin.build_command(ctx), std::runtime_error);
}

TEST_CASE("Plugin rejects plugins without parse_line", "[plugin]") {
    const auto plugin_path = std::filesystem::temp_directory_path() / "teez_missing_parse.lua";
    std::ofstream(plugin_path) << R"(
function build_command(context)
    return { command = "bash", args = { "-c", "echo ok" } }
end
)";

    teez::core::Plugin plugin(plugin_path);
    REQUIRE_THROWS_AS(plugin.parse_line_ndjson("line"), std::runtime_error);
}

TEST_CASE("parse_line ignores non-table plugin responses", "[plugin]") {
    const auto plugin_path = std::filesystem::temp_directory_path() / "teez_non_table_parse.lua";
    std::ofstream(plugin_path) << R"(
function build_command(context)
    return { command = "bash", args = { "-c", "echo ok" } }
end

function parse_line(line)
    return "not-a-table"
end
)";

    teez::core::Plugin plugin(plugin_path);
    REQUIRE(plugin.parse_line_ndjson("line").empty());
}
