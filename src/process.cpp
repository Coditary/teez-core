#define _XOPEN_SOURCE 600

#include "teez/core/process.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <optional>
#include <poll.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "teez/core/exit_code.hpp"
#include "teez/core/signals.hpp"

namespace teez::core {

namespace {

void trim_trailing_newline(std::string& line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
}

bool read_file_line(FILE* stream, std::string& line) {
    line.clear();
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), stream) != nullptr) {
        line.append(buffer);
        if (line.find('\n') != std::string::npos) {
            return true;
        }
    }
    return !line.empty();
}

void build_argv(const CommandSpec& spec, std::vector<std::string>& argv_storage,
                std::vector<char*>& argv) {
    argv_storage.push_back(spec.command);
    argv_storage.insert(argv_storage.end(), spec.args.begin(), spec.args.end());

    argv.reserve(argv_storage.size() + 1);
    for (auto& arg : argv_storage) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
}

void append_pipe_output(int fd, std::string& buffer) {
    char chunk[4096];
    while (true) {
        const ssize_t read_bytes = read(fd, chunk, sizeof(chunk));
        if (read_bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (read_bytes == 0) {
            break;
        }
        buffer.append(chunk, static_cast<std::size_t>(read_bytes));
    }
}

int wait_for_child(pid_t pid) {
    if (interruption_requested()) {
        terminate_active_children();
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) { // GCOVR_EXCL_LINE
        unregister_active_child(pid);
        throw std::runtime_error("failed to wait for child process"); // GCOVR_EXCL_LINE
    }
    unregister_active_child(pid);

    if (interruption_requested()) {
        return kExitInterrupted;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return kExitInterrupted;
    }
    return kExitFailure;
}

void configure_child_environment(const CommandSpec& spec) {
    for (const auto& entry : spec.env) {
        setenv(entry.first.c_str(), entry.second.c_str(), 1);
    }
}

using PromptRule = std::pair<std::string, std::string>;

void write_all(int fd, const std::string& data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t written = write(fd, data.data() + offset, data.size() - offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error("failed to write to child stdin"); // GCOVR_EXCL_LINE
        }
        offset += static_cast<std::size_t>(written);
    }
}

bool process_auto_respond(std::string& match_buffer, int stdin_fd,
                          const std::vector<PromptRule>& rules) {
    bool responded = false;
    while (true) {
        std::optional<std::size_t> best_pos;
        std::optional<std::size_t> best_index;

        for (std::size_t index = 0; index < rules.size(); ++index) {
            const auto pos = match_buffer.find(rules[index].first);
            if (pos == std::string::npos) {
                continue;
            }
            if (!best_pos.has_value() || pos < *best_pos ||
                (pos == *best_pos && rules[index].first.size() > rules[*best_index].first.size())) {
                best_pos = pos;
                best_index = index;
            }
        }

        if (!best_pos.has_value()) {
            break;
        }

        const PromptRule& rule = rules[*best_index];
        write_all(stdin_fd, rule.second);
        match_buffer.erase(0, *best_pos + rule.first.size());
        responded = true;
    }
    return responded;
}

bool process_expect_stdout(std::string& match_buffer, int stdin_fd,
                           const std::vector<PromptRule>& rules, std::size_t& step_index) {
    if (step_index >= rules.size()) {
        return false;
    }

    const PromptRule& rule = rules[step_index];
    const auto pos = match_buffer.find(rule.first);
    if (pos == std::string::npos) {
        return false;
    }

    write_all(stdin_fd, rule.second);
    match_buffer.erase(0, pos + rule.first.size());
    ++step_index;
    return true;
}

void process_prompt_matches(std::string& match_buffer, int stdin_fd, const CommandSpec& spec,
                            std::size_t& expect_step_index) {
    if (!spec.auto_respond.empty()) {
        process_auto_respond(match_buffer, stdin_fd, spec.auto_respond);
        return;
    }

    while (process_expect_stdout(match_buffer, stdin_fd, spec.expect_stdout, expect_step_index)) {
    }
}

bool needs_interactive_loop(const CommandSpec& spec) {
    return !spec.auto_respond.empty() || !spec.expect_stdout.empty() || spec.timeout_ms > 0;
}

int compute_poll_timeout_ms(int timeout_ms,
                            const std::chrono::steady_clock::time_point& started_at) {
    if (timeout_ms <= 0) {
        return 100;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started_at)
                             .count();
    if (elapsed >= timeout_ms) {
        return 0;
    }
    return static_cast<int>(std::min<std::int64_t>(100, timeout_ms - elapsed));
}

ExecResult run_command_interactive_pty(const CommandSpec& spec) {
    int master_fd = -1;
    const pid_t pid = forkpty(&master_fd, nullptr, nullptr, nullptr);
    if (pid < 0) {
        throw std::runtime_error("failed to create interactive pseudo-terminal"); // GCOVR_EXCL_LINE
    }

    if (pid == 0) {
        std::vector<std::string> argv_storage;
        std::vector<char*> argv;
        build_argv(spec, argv_storage, argv);
        configure_child_environment(spec);
        execvp(argv_storage[0].c_str(), argv.data());
        _exit(127);
    }

    register_active_child(pid);

    ExecResult result;
    std::string match_buffer;
    std::size_t expect_step_index = 0;
    bool timed_out = false;
    bool pty_open = true;
    const auto started_at = std::chrono::steady_clock::now();

    while (pty_open) {
        if (interruption_requested()) {
            break;
        }

        if (spec.timeout_ms > 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started_at)
                                     .count();
            if (elapsed >= spec.timeout_ms) {
                timed_out = true;
                break;
            }
        }

        const int poll_timeout_ms = compute_poll_timeout_ms(spec.timeout_ms, started_at);

        struct pollfd fd_entry = {};
        fd_entry.fd = master_fd;
        fd_entry.events = POLLIN;
        const int poll_result = poll(&fd_entry, 1, poll_timeout_ms);
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (fd_entry.revents & (POLLIN | POLLHUP | POLLERR)) {
            char chunk[4096];
            const ssize_t read_bytes = read(master_fd, chunk, sizeof(chunk));
            if (read_bytes > 0) {
                const std::string piece(chunk, static_cast<std::size_t>(read_bytes));
                result.stdout_text.append(piece);
                match_buffer.append(piece);
            } else {
                pty_open = false;
            }
        }

        if (!match_buffer.empty()) {
            process_prompt_matches(match_buffer, master_fd, spec, expect_step_index);
        }
    }

    char chunk[4096];
    while (true) {
        const ssize_t read_bytes = read(master_fd, chunk, sizeof(chunk));
        if (read_bytes <= 0) {
            break;
        }
        const std::string piece(chunk, static_cast<std::size_t>(read_bytes));
        result.stdout_text.append(piece);
        match_buffer.append(piece);
    }

    if (!match_buffer.empty()) {
        process_prompt_matches(match_buffer, master_fd, spec, expect_step_index);
    }

    close(master_fd);

    if (timed_out) {
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
        unregister_active_child(pid);
        result.exit_code = 124;
        return result;
    }

    result.exit_code = wait_for_child(pid);
    return result;
}

ExecResult run_command_interactive_pipes(const CommandSpec& spec) {
    int stdout_pipe[2]{};
    int stderr_pipe[2]{};
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        throw std::runtime_error("failed to create interactive process pipes"); // GCOVR_EXCL_LINE
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        throw std::runtime_error("failed to fork child process"); // GCOVR_EXCL_LINE
    }

    if (pid == 0) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        if (dup2(stdout_pipe[1], STDOUT_FILENO) < 0) {
            _exit(126); // GCOVR_EXCL_LINE
        }
        if (dup2(stderr_pipe[1], STDERR_FILENO) < 0) {
            _exit(126); // GCOVR_EXCL_LINE
        }
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        std::vector<std::string> argv_storage;
        std::vector<char*> argv;
        build_argv(spec, argv_storage, argv);
        configure_child_environment(spec);
        execvp(argv_storage[0].c_str(), argv.data());
        _exit(127);
    }

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    register_active_child(pid);

    ExecResult result;
    bool stdout_open = true;
    bool stderr_open = true;
    bool timed_out = false;
    const auto started_at = std::chrono::steady_clock::now();

    while (stdout_open || stderr_open) {
        if (interruption_requested()) {
            break;
        }

        if (spec.timeout_ms > 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started_at)
                                     .count();
            if (elapsed >= spec.timeout_ms) {
                timed_out = true;
                break;
            }
        }

        const int poll_timeout_ms = compute_poll_timeout_ms(spec.timeout_ms, started_at);

        struct pollfd fds[2];
        int poll_count = 0;
        int stdout_poll_index = -1;
        int stderr_poll_index = -1;

        if (stdout_open) {
            stdout_poll_index = poll_count;
            fds[poll_count].fd = stdout_pipe[0];
            fds[poll_count].events = POLLIN;
            fds[poll_count].revents = 0;
            ++poll_count;
        }
        if (stderr_open) {
            stderr_poll_index = poll_count;
            fds[poll_count].fd = stderr_pipe[0];
            fds[poll_count].events = POLLIN;
            fds[poll_count].revents = 0;
            ++poll_count;
        }

        const int poll_result = poll(fds, poll_count, poll_timeout_ms);
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        auto drain_fd = [&](int fd, std::string& buffer, bool& is_open) {
            char chunk[4096];
            while (true) {
                const ssize_t read_bytes = read(fd, chunk, sizeof(chunk));
                if (read_bytes > 0) {
                    buffer.append(chunk, static_cast<std::size_t>(read_bytes));
                    continue;
                }
                if (read_bytes == 0) {
                    is_open = false;
                }
                break;
            }
        };

        if (stdout_open && (fds[stdout_poll_index].revents & (POLLIN | POLLHUP | POLLERR))) {
            drain_fd(stdout_pipe[0], result.stdout_text, stdout_open);
        }
        if (stderr_open && (fds[stderr_poll_index].revents & (POLLIN | POLLHUP | POLLERR))) {
            drain_fd(stderr_pipe[0], result.stderr_text, stderr_open);
        }
    }

    if (timed_out) {
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        unregister_active_child(pid);
        result.exit_code = 124;
        return result;
    }

    append_pipe_output(stdout_pipe[0], result.stdout_text);
    append_pipe_output(stderr_pipe[0], result.stderr_text);
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    result.exit_code = wait_for_child(pid);
    return result;
}

ExecResult run_command_interactive(const CommandSpec& spec) {
    if (!spec.auto_respond.empty() || !spec.expect_stdout.empty()) {
        return run_command_interactive_pty(spec);
    }
    return run_command_interactive_pipes(spec);
}

} // namespace

int run_command_streaming(const CommandSpec& spec,
                          const std::function<void(const std::string& line)>& on_line) {
    if (spec.command.empty()) {
        throw std::runtime_error("command must not be empty");
    }

    int pipe_fds[2]{};
    if (pipe(pipe_fds) != 0) {
        throw std::runtime_error("failed to create stdout pipe"); // GCOVR_EXCL_LINE
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        throw std::runtime_error("failed to fork child process"); // GCOVR_EXCL_LINE
    }

    if (pid == 0) {
        close(pipe_fds[0]);
        if (dup2(pipe_fds[1], STDOUT_FILENO) < 0) {
            _exit(126); // GCOVR_EXCL_LINE
        }
        if (dup2(pipe_fds[1], STDERR_FILENO) < 0) {
            _exit(126); // GCOVR_EXCL_LINE
        }
        close(pipe_fds[1]);

        std::vector<std::string> argv_storage;
        argv_storage.push_back(spec.command);
        argv_storage.insert(argv_storage.end(), spec.args.begin(), spec.args.end());

        std::vector<char*> argv;
        argv.reserve(argv_storage.size() + 1);
        for (auto& arg : argv_storage) {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);

        configure_child_environment(spec);
        execvp(argv_storage[0].c_str(), argv.data());
        _exit(127);
    }

    close(pipe_fds[1]);
    register_active_child(pid);

    FILE* stream = fdopen(pipe_fds[0], "r");
    if (stream == nullptr) {
        close(pipe_fds[0]);
        terminate_active_children();
        waitpid(pid, nullptr, 0);
        unregister_active_child(pid);
        throw std::runtime_error("failed to open stdout pipe for reading"); // GCOVR_EXCL_LINE
    }

    while (!interruption_requested()) {
        errno = 0;
        std::string line;
        if (!read_file_line(stream, line)) {
            if (errno == EINTR && interruption_requested()) {
                break;
            }
            break;
        }

        trim_trailing_newline(line);
        if (!line.empty()) {
            on_line(line);
        }
    }

    fclose(stream);

    if (interruption_requested()) {
        terminate_active_children();
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) { // GCOVR_EXCL_LINE
        unregister_active_child(pid);
        throw std::runtime_error(
            "failed to wait for child process"); // GCOVR_EXCL_LINE  // GCOVR_EXCL_LINE
    }
    unregister_active_child(pid);

    if (interruption_requested()) {
        return kExitInterrupted;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return kExitInterrupted;
    }
    return kExitFailure;
}

ExecResult run_command_capture(const CommandSpec& spec) {
    if (spec.command.empty()) {
        throw std::runtime_error("command must not be empty");
    }
    if (needs_interactive_loop(spec)) {
        return run_command_interactive(spec);
    }

    int stdout_pipe[2]{};
    int stderr_pipe[2]{};
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        throw std::runtime_error("failed to create capture pipes"); // GCOVR_EXCL_LINE
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        throw std::runtime_error("failed to fork child process"); // GCOVR_EXCL_LINE
    }

    if (pid == 0) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        if (dup2(stdout_pipe[1], STDOUT_FILENO) < 0) {
            _exit(126); // GCOVR_EXCL_LINE
        }
        if (dup2(stderr_pipe[1], STDERR_FILENO) < 0) {
            _exit(126); // GCOVR_EXCL_LINE
        }
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        std::vector<std::string> argv_storage;
        std::vector<char*> argv;
        build_argv(spec, argv_storage, argv);
        configure_child_environment(spec);
        execvp(argv_storage[0].c_str(), argv.data());
        _exit(127);
    }

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    register_active_child(pid);

    ExecResult result;
    bool stdout_open = true;
    bool stderr_open = true;

    while (stdout_open || stderr_open) {
        if (interruption_requested()) {
            break;
        }

        struct pollfd fds[2];
        int poll_count = 0;
        int stdout_poll_index = -1;
        int stderr_poll_index = -1;

        if (stdout_open) {
            stdout_poll_index = poll_count;
            fds[poll_count].fd = stdout_pipe[0];
            fds[poll_count].events = POLLIN;
            fds[poll_count].revents = 0;
            ++poll_count;
        }
        if (stderr_open) {
            stderr_poll_index = poll_count;
            fds[poll_count].fd = stderr_pipe[0];
            fds[poll_count].events = POLLIN;
            fds[poll_count].revents = 0;
            ++poll_count;
        }

        const int poll_result = poll(fds, poll_count, 100);
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        auto drain_fd = [](int fd, std::string& buffer, bool& is_open) {
            char chunk[4096];
            while (true) {
                const ssize_t read_bytes = read(fd, chunk, sizeof(chunk));
                if (read_bytes > 0) {
                    buffer.append(chunk, static_cast<std::size_t>(read_bytes));
                    continue;
                }
                if (read_bytes == 0) {
                    is_open = false;
                }
                break;
            }
        };

        if (stdout_open && (fds[stdout_poll_index].revents & (POLLIN | POLLHUP | POLLERR))) {
            drain_fd(stdout_pipe[0], result.stdout_text, stdout_open);
        }
        if (stderr_open && (fds[stderr_poll_index].revents & (POLLIN | POLLHUP | POLLERR))) {
            drain_fd(stderr_pipe[0], result.stderr_text, stderr_open);
        }
    }

    append_pipe_output(stdout_pipe[0], result.stdout_text);
    append_pipe_output(stderr_pipe[0], result.stderr_text);
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    result.exit_code = wait_for_child(pid);
    return result;
}

std::string exec_command(const CommandSpec& spec) {
    const ExecResult result = run_command_capture(spec);
    std::string output = result.stdout_text;
    if (!result.stderr_text.empty()) {
        output += result.stderr_text;
    }
    return output;
}

BackgroundProcessHandle::BackgroundProcessHandle() = default;

BackgroundProcessHandle::~BackgroundProcessHandle() {
    stop();
}

BackgroundProcessHandle::BackgroundProcessHandle(BackgroundProcessHandle&& other) noexcept
    : pid_(other.pid_), running_(other.running_), capture_output_(other.capture_output_),
      stdout_fd_(other.stdout_fd_), lines_(std::move(other.lines_)),
      reader_(std::move(other.reader_)) {
    other.pid_ = -1;
    other.running_ = false;
    other.stdout_fd_ = -1;
}

BackgroundProcessHandle&
BackgroundProcessHandle::operator=(BackgroundProcessHandle&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    stop();
    pid_ = other.pid_;
    running_ = other.running_;
    capture_output_ = other.capture_output_;
    stdout_fd_ = other.stdout_fd_;
    lines_ = std::move(other.lines_);
    reader_ = std::move(other.reader_);
    other.pid_ = -1;
    other.running_ = false;
    other.stdout_fd_ = -1;
    return *this;
}

pid_t BackgroundProcessHandle::pid() const {
    return pid_;
}

bool BackgroundProcessHandle::running() const {
    return running_;
}

void BackgroundProcessHandle::join_reader() {
    if (reader_.joinable()) {
        reader_.join();
    }
}

void BackgroundProcessHandle::detach_reader() {
    if (reader_.joinable()) {
        reader_.detach();
    }
}

void BackgroundProcessHandle::stop() {
    if (!running_) {
        join_reader();
        return;
    }

    running_ = false;
    if (pid_ > 0) {
        kill(pid_, SIGTERM);
        int status = 0;
        waitpid(pid_, &status, 0);
        unregister_active_child(pid_);
        pid_ = -1;
    }

    if (stdout_fd_ >= 0) {
        close(stdout_fd_);
        stdout_fd_ = -1;
    }

    join_reader();
}

const std::vector<std::string>& BackgroundProcessHandle::lines() const {
    return lines_;
}

std::size_t BackgroundProcessHandle::line_count() const {
    return lines_.size();
}

BackgroundProcessHandlePtr spawn_background_process(const CommandSpec& spec, bool capture_output) {
    if (spec.command.empty()) {
        throw std::runtime_error("command must not be empty");
    }

    auto handle = std::make_shared<BackgroundProcessHandle>();
    handle->capture_output_ = capture_output;

    int stdout_pipe[2]{};
    int dev_null = -1;
    if (capture_output) {
        if (pipe(stdout_pipe) != 0) {
            throw std::runtime_error("failed to create stdout pipe for background process");
        }
    } else {
        dev_null = open("/dev/null", O_RDWR);
        if (dev_null < 0) {
            throw std::runtime_error("failed to open /dev/null for background process");
        }
    }

    const pid_t pid = fork();
    if (pid < 0) {
        if (capture_output) {
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
        } else {
            close(dev_null);
        }
        throw std::runtime_error("failed to fork background process");
    }

    if (pid == 0) {
        if (capture_output) {
            close(stdout_pipe[0]);
            if (dup2(stdout_pipe[1], STDOUT_FILENO) < 0) {
                _exit(126);
            }
            if (dup2(stdout_pipe[1], STDERR_FILENO) < 0) {
                _exit(126);
            }
            close(stdout_pipe[1]);
        } else {
            if (dup2(dev_null, STDOUT_FILENO) < 0 || dup2(dev_null, STDERR_FILENO) < 0) {
                _exit(126);
            }
            close(dev_null);
        }

        std::vector<std::string> argv_storage;
        std::vector<char*> argv;
        build_argv(spec, argv_storage, argv);
        configure_child_environment(spec);
        execvp(argv_storage[0].c_str(), argv.data());
        _exit(127);
    }

    if (capture_output) {
        close(stdout_pipe[1]);
        handle->stdout_fd_ = stdout_pipe[0];
    } else {
        close(dev_null);
    }

    handle->pid_ = pid;
    handle->running_ = true;
    register_active_child(pid);

    if (capture_output) {
        const int read_fd = handle->stdout_fd_;
        handle->reader_ = std::thread([handle, read_fd]() {
            FILE* stream = fdopen(read_fd, "r");
            if (stream == nullptr) {
                return;
            }

            while (handle->running_) {
                std::string line;
                if (!read_file_line(stream, line)) {
                    break;
                }

                trim_trailing_newline(line);
                if (!line.empty()) {
                    handle->lines_.push_back(line);
                }
            }

            fclose(stream);
        });
    }

    return handle;
}

bool probe_tcp_open(const std::string& host, int port, int timeout_ms) {
    if (host.empty() || port <= 0 || port > 65535) {
        return false;
    }

    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* addresses = nullptr;
    const std::string port_string = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_string.c_str(), &hints, &addresses) != 0) {
        return false;
    }

    bool connected = false;
    for (addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
        const int socket_fd =
            socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket_fd < 0) {
            continue;
        }

        const int flags = fcntl(socket_fd, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);
        }

        const int connect_result = connect(socket_fd, address->ai_addr, address->ai_addrlen);
        if (connect_result == 0) {
            connected = true;
            close(socket_fd);
            break;
        }

        if (errno != EINPROGRESS) {
            close(socket_fd);
            continue;
        }

        pollfd poll_fd = {};
        poll_fd.fd = socket_fd;
        poll_fd.events = POLLOUT;

        const int poll_result = poll(&poll_fd, 1, timeout_ms);
        if (poll_result > 0 && (poll_fd.revents & POLLOUT)) {
            int socket_error = 0;
            socklen_t error_length = sizeof(socket_error);
            if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_length) == 0 &&
                socket_error == 0) {
                connected = true;
            }
        }

        close(socket_fd);
        if (connected) {
            break;
        }
    }

    freeaddrinfo(addresses);
    return connected;
}

bool command_exists(const std::string& command) {
    if (command.empty()) {
        return false;
    }

    std::string quoted;
    quoted.reserve(command.size() + 2);
    quoted.push_back('\'');
    for (const char ch : command) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('\'');

    const auto result = run_command_capture(
        {.command = "sh", .args = {"-c", "command -v " + quoted + " >/dev/null 2>&1"}});
    return result.exit_code == 0;
}

} // namespace teez::core
