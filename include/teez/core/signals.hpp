#pragma once

#include <csignal>

namespace teez::core {

/// Installs SIGINT/SIGTERM handlers for child cleanup.
void install_signal_handlers();

/// Returns true after the user requested interruption.
bool interruption_requested();

void clear_interruption_request();

/// Tracks the currently running child process for signal cleanup.
void register_active_child(pid_t pid);
void unregister_active_child(pid_t pid);
void terminate_active_children();

} // namespace teez::core
