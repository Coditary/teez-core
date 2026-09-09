#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

#include "teez/core/process.hpp"

TEST_CASE("probe_tcp_open returns false for closed port", "[process]") {
    REQUIRE_FALSE(teez::core::probe_tcp_open("127.0.0.1", 9, 50));
}

TEST_CASE("spawn_background_process without capture still tracks process state", "[process]") {
    auto handle = teez::core::spawn_background_process(
        {.command = "bash", .args = {"-c", "sleep 0.2; echo done"}}, false);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE(handle->running());
    REQUIRE(handle->pid() > 0);

    handle->stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE_FALSE(handle->running());
    REQUIRE(handle->line_count() == 0);
}

TEST_CASE("run_command_capture honors timeout for long running commands", "[process]") {
    const auto result = teez::core::run_command_capture(
        {.command = "bash", .args = {"-c", "sleep 2"}, .timeout_ms = 100});

    REQUIRE(result.exit_code != 0);
}

TEST_CASE("run_command_capture kills timed out commands quickly", "[process]") {
    const auto started = std::chrono::steady_clock::now();
    const auto result = teez::core::run_command_capture(
        {.command = "bash", .args = {"-c", "sleep 5"}, .timeout_ms = 100});
    const auto elapsed = std::chrono::steady_clock::now() - started;

    REQUIRE(result.exit_code != 0);
    REQUIRE(elapsed < std::chrono::seconds(2));
}
