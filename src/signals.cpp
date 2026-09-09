#include "teez/core/signals.hpp"

#include <atomic>
#include <csignal>
#include <mutex>
#include <unistd.h>
#include <unordered_set>

namespace teez::core {

namespace {

volatile sig_atomic_t g_interrupted = 0;
std::mutex g_children_mutex;
std::unordered_set<pid_t> g_active_children;

void signal_handler(int) {
    g_interrupted = 1;
}

} // namespace

void install_signal_handlers() {
    struct sigaction action{};
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
}

bool interruption_requested() {
    return g_interrupted != 0;
}

void clear_interruption_request() {
    g_interrupted = 0;
}

void register_active_child(pid_t pid) {
    if (pid <= 0) {
        return;
    }
    std::lock_guard lock(g_children_mutex);
    g_active_children.insert(pid);
}

void unregister_active_child(pid_t pid) {
    std::lock_guard lock(g_children_mutex);
    g_active_children.erase(pid);
}

void terminate_active_children() {
    std::lock_guard lock(g_children_mutex);
    for (const pid_t pid : g_active_children) {
        kill(pid, SIGTERM);
    }
}

} // namespace teez::core
