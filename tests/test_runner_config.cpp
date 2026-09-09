#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include "teez/core/runner_config.hpp"

TEST_CASE("parse_config_runners reads include and exclude lists", "[runner_config]") {
    const nlohmann::json config = {
        {"runners",
         {
             {"vitest",
              {
                  {"include", nlohmann::json::array({"tests/**/*.test.ts"})},
                  {"exclude", nlohmann::json::array({"**/*.teez.lua"})},
              }},
             {"teez", {{"include", "smoke.teez.lua"}}},
         }},
    };

    const auto runners = teez::core::parse_config_runners(config);
    REQUIRE(runners.size() == 2);
    REQUIRE(runners.at("vitest").include.front() == "tests/**/*.test.ts");
    REQUIRE(runners.at("vitest").exclude.front() == "**/*.teez.lua");
    REQUIRE(runners.at("teez").include.front() == "smoke.teez.lua");
}

TEST_CASE("resolve_runner_options maps teez alias to teez-worker", "[runner_config]") {
    teez::core::RunnerConfigMap runners = {
        {"teez", teez::core::RunnerOptions{.include = {"api.teez.lua"}}},
    };

    const auto options =
        teez::core::resolve_runner_options(runners, teez::core::kWorkerRunnerName);
    REQUIRE(options.include.front() == "api.teez.lua");
}

TEST_CASE("resolve_runner_options still accepts legacy worker alias", "[runner_config]") {
    teez::core::RunnerConfigMap runners = {
        {"worker", teez::core::RunnerOptions{.include = {"legacy.teez.lua"}}},
    };

    const auto options =
        teez::core::resolve_runner_options(runners, teez::core::kWorkerRunnerName);
    REQUIRE(options.include.front() == "legacy.teez.lua");
}

TEST_CASE("parse_config_runners passes through plugin-specific fields", "[runner_config]") {
    const nlohmann::json config = {
        {"runners",
         {{"vitest",
           {{"include", nlohmann::json::array({"tests/**/*.test.ts"})},
            {"pattern", "unit"},
            {"args", nlohmann::json::array({"--pool=threads"})}}},
          {"pytest", {{"markers", "not slow"}}}}},
    };

    const auto runners = teez::core::parse_config_runners(config);
    REQUIRE(runners.at("vitest").include.front() == "tests/**/*.test.ts");
    REQUIRE(runners.at("vitest").config.at("pattern") == "unit");
    REQUIRE(runners.at("vitest").config.at("args")[0] == "--pool=threads");
    REQUIRE_FALSE(runners.at("vitest").config.contains("include"));
    REQUIRE(runners.at("pytest").config.at("markers") == "not slow");

    const auto roundtrip = runners.at("vitest").to_json();
    REQUIRE(roundtrip.at("include")[0] == "tests/**/*.test.ts");
    REQUIRE(roundtrip.at("pattern") == "unit");
}

TEST_CASE("merge_runner_options applies manifest defaults and user overrides", "[runner_config]") {
    const teez::core::RunnerOptions defaults{
        .include = {"**/*.teez.lua"},
        .exclude = {"**/*.py"},
    };
    const teez::core::RunnerOptions overrides{
        .include = {"smoke.teez.lua"},
        .exclude = {"**/draft/**"},
    };

    const auto merged = teez::core::merge_runner_options(defaults, overrides);
    REQUIRE(merged.include.size() == 1);
    REQUIRE(merged.include.front() == "smoke.teez.lua");
    REQUIRE(merged.exclude.size() == 2);
    REQUIRE(merged.exclude[0] == "**/*.py");
    REQUIRE(merged.exclude[1] == "**/draft/**");
}

TEST_CASE("resolve_effective_runner_options merges manifest with runners config", "[runner_config]") {
    teez::core::RunnerConfigMap runners = {
        {"teez-worker",
         teez::core::RunnerOptions{.exclude = std::vector<std::string>{"**/draft/**"}}},
    };

    const auto options = teez::core::resolve_effective_runner_options(
        {"**/*.teez.lua"}, {"**/*.py"}, runners, teez::core::kWorkerRunnerName);

    REQUIRE(options.include.front() == "**/*.teez.lua");
    REQUIRE(options.exclude.size() == 2);
    REQUIRE(options.exclude[0] == "**/*.py");
    REQUIRE(options.exclude[1] == "**/draft/**");
}

TEST_CASE("apply_runner_options filters paths by include and exclude", "[runner_config]") {
    const std::filesystem::path root = "/tmp/teez_runner_filter";
    const std::vector<std::filesystem::path> files = {
        root / "api.teez.lua",
        root / "smoke.teez.lua",
        root / "legacy.teez.lua",
    };

    const teez::core::RunnerOptions options{
        .include = {"api.teez.lua", "smoke.teez.lua"},
        .exclude = {"legacy.teez.lua"},
    };

    const auto filtered = teez::core::apply_runner_options(files, root, options);
    REQUIRE(filtered.size() == 2);
    REQUIRE(filtered[0].filename() == "api.teez.lua");
    REQUIRE(filtered[1].filename() == "smoke.teez.lua");
}
