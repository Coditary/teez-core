#include "teez/core/config.hpp"

#include <ostream>

namespace teez::core {

namespace {

std::filesystem::path g_worker_binary = "teez-worker";
std::optional<std::string> g_worker_subcommand;
InProcessWorkerFn g_in_process_worker;
InProcessWorkerListFn g_in_process_worker_list;

} // namespace

void set_worker_binary(std::filesystem::path path) {
    g_worker_binary = std::move(path);
}

std::filesystem::path worker_binary() {
    return g_worker_binary;
}

void set_worker_subcommand(std::optional<std::string> subcommand) {
    g_worker_subcommand = std::move(subcommand);
}

std::optional<std::string> worker_subcommand() {
    return g_worker_subcommand;
}

void set_in_process_worker(InProcessWorkerFn runner) {
    g_in_process_worker = std::move(runner);
}

const InProcessWorkerFn* in_process_worker() {
    return g_in_process_worker ? &g_in_process_worker : nullptr;
}

void set_in_process_worker_list(InProcessWorkerListFn lister) {
    g_in_process_worker_list = std::move(lister);
}

const InProcessWorkerListFn* in_process_worker_list() {
    return g_in_process_worker_list ? &g_in_process_worker_list : nullptr;
}

} // namespace teez::core
