#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace teez::core {

void set_worker_binary(std::filesystem::path path);
std::filesystem::path worker_binary();

/// When set, the worker plugin prepends this subcommand (e.g. "worker" for embedded teez).
void set_worker_subcommand(std::optional<std::string> subcommand);
std::optional<std::string> worker_subcommand();

/// Runs the worker in-process instead of spawning a child process.
using InProcessWorkerFn =
    std::function<int(const std::filesystem::path& target_path, std::ostream& out)>;
void set_in_process_worker(InProcessWorkerFn runner);
const InProcessWorkerFn* in_process_worker();

/// Lists worker tests in-process (used for embedded teez builds).
using InProcessWorkerListFn =
    std::function<std::vector<std::string>(const std::filesystem::path& target_path)>;
void set_in_process_worker_list(InProcessWorkerListFn lister);
const InProcessWorkerListFn* in_process_worker_list();

} // namespace teez::core
