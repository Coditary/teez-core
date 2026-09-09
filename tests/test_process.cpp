#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <csignal>
#include <thread>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "teez/core/exit_code.hpp"
#include "teez/core/process.hpp"
#include "teez/core/signals.hpp"

namespace {

std::filesystem::path write_script(const std::string& name, const std::string& content) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream file(path);
    file << content;
    file.close();
    std::filesystem::permissions(path, std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read |
                                             std::filesystem::perms::owner_write);
    return path;
}

}  // namespace

TEST_CASE("run_command_streaming captures stdout lines", "[process]") {
    const auto script = write_script("teez_proc_lines.sh", "#!/usr/bin/env bash\necho alpha\necho beta\n");

    std::vector<std::string> lines;
    const int exit_code = teez::core::run_command_streaming(
        {.command = "bash", .args = {script.string()}},
        [&](const std::string& line) { lines.push_back(line); });

    REQUIRE(exit_code == 0);
    REQUIRE(lines.size() == 2);
    REQUIRE(lines[0] == "alpha");
    REQUIRE(lines[1] == "beta");
}

TEST_CASE("run_command_capture separates stdout stderr and exit code", "[process]") {
    const auto script = write_script(
        "teez_proc_capture.sh",
        "#!/usr/bin/env bash\n"
        "echo out-line\n"
        "echo err-line >&2\n"
        "exit 42\n");

    const auto result = teez::core::run_command_capture(
        {.command = "bash", .args = {script.string()}});

    REQUIRE(result.exit_code == 42);
    REQUIRE(result.stdout_text.find("out-line") != std::string::npos);
    REQUIRE(result.stderr_text.find("err-line") != std::string::npos);
    REQUIRE(result.stdout_text.find("err-line") == std::string::npos);
}

TEST_CASE("run_command_capture reports success exit code", "[process]") {
    const auto result =
        teez::core::run_command_capture({.command = "bash", .args = {"-c", "echo ok"}});

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stdout_text.find("ok") != std::string::npos);
    REQUIRE(result.stderr_text.empty());
}

TEST_CASE("run_command_capture applies custom environment variables", "[process]") {
    const auto result = teez::core::run_command_capture({
        .command = "printenv",
        .args = {"TEEZ_MOCK_ENV"},
        .env = {{"TEEZ_MOCK_ENV", "mocked-value"}},
    });

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stdout_text.find("mocked-value") != std::string::npos);
}

TEST_CASE("run_command_streaming delivers lines while process is running", "[process]") {
    const auto script = write_script(
        "teez_proc_stream.sh",
        "#!/usr/bin/env bash\n"
        "echo first\n"
        "sleep 1\n"
        "echo second\n");

    std::vector<std::string> lines;
    const auto started = std::chrono::steady_clock::now();
    const int exit_code = teez::core::run_command_streaming(
        {.command = "bash", .args = {script.string()}},
        [&](const std::string& line) {
            lines.push_back(line);
            if (lines.size() == 1) {
                const auto elapsed = std::chrono::steady_clock::now() - started;
                REQUIRE(elapsed < std::chrono::milliseconds(900));
            }
        });

    REQUIRE(exit_code == 0);
    REQUIRE(lines.size() == 2);
    REQUIRE(lines[0] == "first");
    REQUIRE(lines[1] == "second");
}

TEST_CASE("run_command_capture auto_respond reacts to interactive prompts", "[process]") {
    const auto script = write_script(
        "teez_proc_auto_respond.sh",
        "#!/usr/bin/env bash\n"
        "read -r -p \"Username: \" username\n"
        "read -r -s -p \"Password: \" password\n"
        "echo\n"
        "read -r -p \"Do you want to continue? [y/N] \" answer\n"
        "echo \"Setup finished successfully\"\n");

    const auto result = teez::core::run_command_capture({
        .command = "bash",
        .args = {script.string()},
        .auto_respond = {{"Username:", "admin\n"},
                         {"Password:", "geheim123\n"},
                         {"Do you want to continue? [y/N]", "y\n"}},
    });

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stdout_text.find("Setup finished successfully") != std::string::npos);
}

TEST_CASE("run_command_capture auto_respond handles recurring prompts", "[process]") {
    const auto script = write_script(
        "teez_proc_auto_respond_repeat.sh",
        "#!/usr/bin/env bash\n"
        "for i in 1 2 3; do\n"
        "  read -r -p \"Delete file? (y/n) \" answer\n"
        "done\n"
        "echo \"deleted all\"\n");

    const auto result = teez::core::run_command_capture({
        .command = "bash",
        .args = {script.string()},
        .auto_respond = {{"(y/n)", "y\n"}},
    });

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stdout_text.find("deleted all") != std::string::npos);
}

TEST_CASE("run_command_capture expect_stdout handles distinct sequential prompts", "[process]") {
    const auto script = write_script(
        "teez_proc_expect_stdout.sh",
        "#!/usr/bin/env bash\n"
        "read -r -p \"Enter password: \" p1\n"
        "read -r -p \"Confirm password: \" p2\n"
        "read -r -p \"Enter database password: \" p3\n"
        "echo \"configured\"\n");

    const auto result = teez::core::run_command_capture({
        .command = "bash",
        .args = {script.string()},
        .expect_stdout = {{"Enter password:", "geheim123\n"},
                          {"Confirm password:", "geheim123\n"},
                          {"Enter database password:", "db-pass\n"}},
    });

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stdout_text.find("configured") != std::string::npos);
}

TEST_CASE("run_command_capture timeout terminates long running process", "[process]") {
    const auto result = teez::core::run_command_capture({
        .command = "bash",
        .args = {"-c", "sleep 2; echo done"},
        .timeout_ms = 200,
    });

    REQUIRE(result.exit_code == 124);
    REQUIRE(result.stdout_text.find("done") == std::string::npos);
}

TEST_CASE("run_command_capture rejects empty command", "[process]") {
    REQUIRE_THROWS_AS(teez::core::run_command_capture({.command = ""}), std::runtime_error);
}

TEST_CASE("run_command_streaming rejects empty command", "[process]") {
    REQUIRE_THROWS_AS(
        teez::core::run_command_streaming({.command = ""}, [](const std::string&) {}),
        std::runtime_error);
}

TEST_CASE("run_command_capture reports exec failure for missing command", "[process]") {
    const auto result = teez::core::run_command_capture(
        {.command = "/tmp/teez-missing-command-xyz", .args = {}});

    REQUIRE(result.exit_code == 127);
}

TEST_CASE("exec_command merges stdout and stderr", "[process]") {
    const auto output = teez::core::exec_command(
        {.command = "bash", .args = {"-c", "echo stdout-line; echo stderr-line >&2"}});

    REQUIRE(output.find("stdout-line") != std::string::npos);
    REQUIRE(output.find("stderr-line") != std::string::npos);
}

TEST_CASE("run_command_streaming skips empty stdout lines", "[process]") {
    const auto script = write_script(
        "teez_proc_empty_line.sh",
        "#!/usr/bin/env bash\n"
        "echo\n"
        "echo content\n");

    std::vector<std::string> lines;
    const int exit_code = teez::core::run_command_streaming(
        {.command = "bash", .args = {script.string()}},
        [&](const std::string& line) { lines.push_back(line); });

    REQUIRE(exit_code == 0);
    REQUIRE(lines.size() == 1);
    REQUIRE(lines.front() == "content");
}

TEST_CASE("run_command_streaming honors interruption requests", "[process]") {
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
    teez::core::clear_interruption_request();
}

TEST_CASE("run_command_capture honors interruption requests", "[process]") {
    teez::core::install_signal_handlers();
    teez::core::clear_interruption_request();

    std::atomic<bool> started{false};
    teez::core::ExecResult result;
    std::thread worker([&]() {
        started = true;
        result = teez::core::run_command_capture(
            {.command = "bash", .args = {"-c", "sleep 30"}});
    });

    while (!started) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    raise(SIGINT);
    worker.join();

    REQUIRE(result.exit_code == teez::core::kExitInterrupted);
    teez::core::clear_interruption_request();
}

TEST_CASE("run_command_streaming applies custom environment variables", "[process]") {
    const int exit_code = teez::core::run_command_streaming(
        {.command = "printenv",
         .args = {"TEEZ_STREAM_ENV"},
         .env = {{"TEEZ_STREAM_ENV", "stream-value"}}},
        [](const std::string& line) { REQUIRE(line == "stream-value"); });

    REQUIRE(exit_code == 0);
}

TEST_CASE("command_exists detects binaries on PATH", "[process][harness]") {
    REQUIRE(teez::core::command_exists("sh"));
    REQUIRE_FALSE(teez::core::command_exists("teez-definitely-missing-binary"));
}

TEST_CASE("probe_tcp_open detects listening local port", "[process][harness]") {
    auto handle = teez::core::spawn_background_process(
        {.command = "python3",
         .args = {"-c", R"(import socket, time
s = socket.socket()
s.bind(('127.0.0.1', 0))
port = s.getsockname()[1]
s.listen(1)
print(port, flush=True)
time.sleep(5))"}},
        true);

    int port = 0;
    for (int attempt = 0; attempt < 20 && port == 0; ++attempt) {
        const auto lines = handle->lines();
        if (!lines.empty()) {
            port = std::stoi(lines.front());
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    REQUIRE(port > 0);
    REQUIRE(teez::core::probe_tcp_open("127.0.0.1", port, 1000));
    handle->stop();
}

TEST_CASE("spawn_background_process captures stdout lines", "[process][harness]") {
    auto handle = teez::core::spawn_background_process(
        {.command = "bash",
         .args = {"-c", "echo line-one; sleep 0.1; echo line-two"}},
        true);

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    handle->stop();

    const auto lines = handle->lines();
    REQUIRE(lines.size() >= 1);
    REQUIRE(lines.front() == "line-one");
}
