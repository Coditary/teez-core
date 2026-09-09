#include "teez/core/context.hpp"

#include <filesystem>

namespace teez::core {

bool path_exists(const std::filesystem::path& path) {
    return std::filesystem::exists(path);
}

std::string validate_run_context(const RunContext& context) {
    if (!path_exists(context.target_path)) {
        return "target path does not exist: " + context.target_path.string();
    }
    return {};
}

} // namespace teez::core
