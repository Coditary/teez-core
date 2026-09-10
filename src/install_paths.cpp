#include "teez/core/install_paths.hpp"

#include <cstdlib>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace teez::core {

namespace {

std::filesystem::path canonical_if_exists(const std::filesystem::path& path) {
    if (path.empty() || !std::filesystem::exists(path)) {
        return {};
    }
    std::error_code ec;
    const auto canonical = std::filesystem::weakly_canonical(path, ec);
    return ec ? path : canonical;
}

bool plugins_dir_usable(const std::filesystem::path& path) {
    if (path.empty() || !std::filesystem::exists(path)) {
        return false;
    }
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
        if (ec) {
            return false;
        }
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            return true;
        }
    }
    return false;
}

std::filesystem::path install_relative_plugins_dir(const std::filesystem::path& executable) {
    if (executable.empty()) {
        return {};
    }
    const auto plugins =
        canonical_if_exists(executable.parent_path() / ".." / "share" / "teez" / "plugins");
    return plugins_dir_usable(plugins) ? plugins : std::filesystem::path{};
}

} // namespace

std::filesystem::path current_executable_path() {
#if defined(__linux__)
    std::error_code ec;
    const auto proc_exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        return canonical_if_exists(proc_exe);
    }
#endif

#if defined(__APPLE__)
    uint32_t size = 0;
    if (_NSGetExecutablePath(nullptr, &size) == -1) {
        std::string buffer(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
            return canonical_if_exists(buffer);
        }
    }
#endif

    return {};
}

std::filesystem::path resolve_bundled_plugins_dir() {
    if (const char* env = std::getenv("TEEZ_PLUGIN_DIR")) {
        if (*env != '\0') {
            const auto from_env = canonical_if_exists(env);
            if (plugins_dir_usable(from_env)) {
                return from_env;
            }
        }
    }

    const auto install_plugins = install_relative_plugins_dir(current_executable_path());
    if (!install_plugins.empty()) {
        return install_plugins;
    }

#ifdef TEEZ_PLUGIN_DIR
    const auto compile_time = canonical_if_exists(TEEZ_PLUGIN_DIR);
    if (plugins_dir_usable(compile_time)) {
        return compile_time;
    }
    return std::filesystem::path(TEEZ_PLUGIN_DIR);
#else
    return {};
#endif
}

} // namespace teez::core
