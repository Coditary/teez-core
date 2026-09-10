#pragma once

#include <filesystem>

namespace teez::core {

/// Absolute path of the running teez executable, or empty when unknown.
std::filesystem::path current_executable_path();

/// Bundled runner/harness plugins: install dir, env override, or compile-time dev path.
std::filesystem::path resolve_bundled_plugins_dir();

} // namespace teez::core
