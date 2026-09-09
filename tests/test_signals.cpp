#include <catch2/catch_test_macros.hpp>

#include <csignal>
#include <atomic>
#include <chrono>
#include <thread>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "teez/core/exit_code.hpp"
#include "teez/core/process.hpp"
#include "teez/core/signals.hpp"

TEST_CASE("register_active_child tracks subprocess pid", "[signals]") {
    teez::core::clear_interruption_request();

    const int exit_code = teez::core::run_command_streaming(
        {.command = "bash", .args = {"-c", "echo signal-test"}},
        [](const std::string& line) { REQUIRE(line == "signal-test"); });

    REQUIRE(exit_code == teez::core::kExitSuccess);
}

TEST_CASE("terminate_active_children stops long running subprocess", "[signals]") {
    teez::core::clear_interruption_request();

    std::atomic<bool> started{false};
    std::thread worker([&]() {
        started = true;
        teez::core::run_command_streaming(
            {.command = "bash", .args = {"-c", "sleep 30"}},
            [](const std::string&) {});
    });

    while (!started) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    teez::core::terminate_active_children();
    worker.join();
}

TEST_CASE("install_signal_handlers interrupts active child on SIGINT", "[signals]") {
    teez::core::install_signal_handlers();
    teez::core::clear_interruption_request();

    std::atomic<bool> started{false};
    std::atomic<int> exit_code{-1};
    std::thread worker([&]() {
        started = true;
        exit_code = teez::core::run_command_streaming(
            {.command = "bash", .args = {"-c", "sleep 30"}},
            [](const std::string&) {});
    });

    while (!started) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    raise(SIGINT);
    worker.join();

    REQUIRE(exit_code.load() == teez::core::kExitInterrupted);
    REQUIRE(teez::core::interruption_requested());
    teez::core::clear_interruption_request();
}

TEST_CASE("unregister_active_child ignores stale pid", "[signals]") {
    teez::core::clear_interruption_request();
    teez::core::unregister_active_child(999999);
    REQUIRE_FALSE(teez::core::interruption_requested());
}
