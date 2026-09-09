#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "teez/core/context.hpp"

TEST_CASE("path_exists returns true for existing directory", "[filesystem]") {
    REQUIRE(teez::core::path_exists(std::filesystem::current_path()));
}

TEST_CASE("path_exists returns false for non-existing path", "[filesystem]") {
    REQUIRE_FALSE(teez::core::path_exists("/this/path/does/not/exist/teez"));
}

TEST_CASE("validate_run_context rejects missing target", "[filesystem]") {
    teez::core::RunContext ctx{
        .command = "run",
        .target_path = "/definitely/not/a/real/path/teez-test",
    };

    const auto error = teez::core::validate_run_context(ctx);
    REQUIRE_FALSE(error.empty());
    REQUIRE(error.find("does not exist") != std::string::npos);
}

TEST_CASE("validate_run_context accepts existing directory", "[filesystem]") {
    teez::core::RunContext ctx{
        .command = "run",
        .target_path = std::filesystem::current_path(),
    };

    REQUIRE(teez::core::validate_run_context(ctx).empty());
}
