#pragma once

#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <sys/types.h>

#include "teez/core/plugin.hpp"

namespace teez::core {

struct ExecResult {
    int exit_code = 0;
    std::string stdout_text;
    std::string stderr_text;
};

class BackgroundProcessHandle {
  public:
    BackgroundProcessHandle();
    ~BackgroundProcessHandle();

    BackgroundProcessHandle(const BackgroundProcessHandle&) = delete;
    BackgroundProcessHandle& operator=(const BackgroundProcessHandle&) = delete;
    BackgroundProcessHandle(BackgroundProcessHandle&& other) noexcept;
    BackgroundProcessHandle& operator=(BackgroundProcessHandle&& other) noexcept;

    pid_t pid() const;
    bool running() const;
    void stop();
    const std::vector<std::string>& lines() const;
    std::size_t line_count() const;

  private:
    friend std::shared_ptr<BackgroundProcessHandle>
    spawn_background_process(const CommandSpec& spec, bool capture_output);

    void detach_reader();
    void join_reader();

    pid_t pid_ = -1;
    bool running_ = false;
    bool capture_output_ = false;
    int stdout_fd_ = -1;
    std::vector<std::string> lines_;
    std::thread reader_;
};

using BackgroundProcessHandlePtr = std::shared_ptr<BackgroundProcessHandle>;

/// Runs command in a child process, invoking on_line for each stdout line as it arrives.
/// Returns the child exit code. Throws on setup failures.
int run_command_streaming(const CommandSpec& spec,
                          const std::function<void(const std::string& line)>& on_line);

/// Runs command synchronously and captures stdout, stderr, and exit code separately.
ExecResult run_command_capture(const CommandSpec& spec);

/// Runs command synchronously and returns captured stdout and stderr combined.
std::string exec_command(const CommandSpec& spec);

/// Starts a child process in the background. When capture_output is true, stdout/stderr
/// lines are stored until stop() is called.
BackgroundProcessHandlePtr spawn_background_process(const CommandSpec& spec,
                                                    bool capture_output = true);

/// Returns true when a TCP connection to host:port succeeds within timeout_ms.
bool probe_tcp_open(const std::string& host, int port, int timeout_ms = 1000);

/// Returns true when command exists on PATH.
bool command_exists(const std::string& command);

} // namespace teez::core
