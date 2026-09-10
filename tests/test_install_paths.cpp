#include <catch2/catch_test_macros.hpp>

#include <fstream>

#include "teez/core/install_paths.hpp"

TEST_CASE("resolve_bundled_plugins_dir prefers install layout", "[install_paths]") {
    const auto executable = teez::core::current_executable_path();
    REQUIRE_FALSE(executable.empty());

    const auto share_root =
        std::filesystem::weakly_canonical(executable.parent_path() / ".." / "share");
    const auto install_plugins = share_root / "teez" / "plugins";
    std::filesystem::create_directories(install_plugins);
    std::ofstream(install_plugins / "dummy.json") << R"({"name":"dummy","plugin":"dummy.lua"})";

    const auto resolved = teez::core::resolve_bundled_plugins_dir();
    REQUIRE(resolved == std::filesystem::weakly_canonical(install_plugins));

    std::error_code ec;
    std::filesystem::remove_all(share_root, ec);
}

TEST_CASE("resolve_bundled_plugins_dir falls back to compile-time plugins", "[install_paths]") {
#ifndef TEEZ_PLUGIN_DIR
    SKIP("TEEZ_PLUGIN_DIR not defined");
#else
    const auto executable = teez::core::current_executable_path();
    REQUIRE_FALSE(executable.empty());

    const auto share_root =
        std::filesystem::weakly_canonical(executable.parent_path() / ".." / "share");
    std::error_code ec;
    std::filesystem::remove_all(share_root, ec);

    const auto compile_time = std::filesystem::path(TEEZ_PLUGIN_DIR);
    REQUIRE(std::filesystem::exists(compile_time));
    const auto resolved = teez::core::resolve_bundled_plugins_dir();
    REQUIRE(resolved == std::filesystem::weakly_canonical(compile_time));
#endif
}
