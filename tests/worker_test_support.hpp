#pragma once

#include <filesystem>
#include <iostream>
#include <vector>

#include "teez/core/config.hpp"

#ifdef TEEZ_EMBEDDED_WORKER
#include "teez/worker/runner.hpp"
#include "teez/worker/runtime_config.hpp"
#endif

namespace teez::core::test_support {

inline bool embedded_worker_available() {
#ifdef TEEZ_EMBEDDED_WORKER
    return true;
#else
    return false;
#endif
}

inline void configure_embedded_worker() {
#ifdef TEEZ_EMBEDDED_WORKER
    static bool configured = false;
    if (configured) {
        return;
    }
    configured = true;

    teez::worker::set_use_embedded_runtime(true);
    teez::core::set_worker_binary("teez");
    teez::core::set_worker_subcommand("worker");
    teez::core::set_in_process_worker(
        [](const std::filesystem::path& target_path, std::ostream& out) -> int {
            return teez::worker::run_worker(target_path, TEEZ_WORKER_RUNTIME_DIR, out);
        });
    teez::core::set_in_process_worker_list(
        [](const std::filesystem::path& target_path) -> std::vector<std::string> {
            return teez::worker::list_worker(target_path, TEEZ_WORKER_RUNTIME_DIR);
        });
#endif
}

}  // namespace teez::core::test_support
