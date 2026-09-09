#include <catch2/catch_test_macros.hpp>

#include "teez/core/exit_code.hpp"

TEST_CASE("normalize_exit_code returns 0 on success", "[exit_code]") {
    REQUIRE(teez::core::normalize_exit_code(false, false, 0) == teez::core::kExitSuccess);
}

TEST_CASE("normalize_exit_code returns 1 on child failure", "[exit_code]") {
    REQUIRE(teez::core::normalize_exit_code(false, false, 2) == teez::core::kExitFailure);
}

TEST_CASE("normalize_exit_code returns 1 on streamed failure event", "[exit_code]") {
    REQUIRE(teez::core::normalize_exit_code(false, true, 0) == teez::core::kExitFailure);
}

TEST_CASE("normalize_exit_code returns 130 on interruption", "[exit_code]") {
    REQUIRE(teez::core::normalize_exit_code(true, false, 0) == teez::core::kExitInterrupted);
}
