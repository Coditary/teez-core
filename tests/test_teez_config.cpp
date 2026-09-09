#include <catch2/catch_test_macros.hpp>

#include <fstream>

#include "teez/core/teez_config.hpp"

namespace {

std::filesystem::path write_config(const std::filesystem::path& dir, const std::string& content) {
    std::filesystem::create_directories(dir);
    const auto path = dir / "teez.config.lua";
    std::ofstream(path) << content;
    return path;
}

}  // namespace

TEST_CASE("TeezConfig loads lua table with env-dependent values", "[teez_config]") {
    const auto project = std::filesystem::temp_directory_path() / "teez_config_test";
    std::filesystem::remove_all(project);
    write_config(project, R"(
return {
    profile = os.getenv("TEEZ_TEST_PROFILE") or "local",
    worker_bin = "/custom/teez-worker"
}
)");

    const auto config = teez::core::TeezConfig::load(project);

    REQUIRE_FALSE(config.empty());
    REQUIRE(config.get_string("worker_bin") == "/custom/teez-worker");
    REQUIRE(config.get_string("profile") == "local");
}

TEST_CASE("TeezConfig does not walk up to parent directory", "[teez_config]") {
    const auto root = std::filesystem::temp_directory_path() / "teez_config_parent";
    std::filesystem::remove_all(root);
    write_config(root, "return { found = true }\n");
    const auto nested = root / "nested" / "deep";
    std::filesystem::create_directories(nested);

    const auto config = teez::core::TeezConfig::load(nested);

    REQUIRE(config.empty());
}

TEST_CASE("TeezConfig resolve uses config from search_dir when target is in scope", "[teez_config]") {
    const auto root = std::filesystem::temp_directory_path() / "teez_config_resolve";
    std::filesystem::remove_all(root);
    write_config(root, "return { found = true }\n");
    const auto nested = root / "nested";
    std::filesystem::create_directories(nested);

    const auto config = teez::core::TeezConfig::resolve(
        {.search_dir = root, .target_path = nested, .config_file = std::nullopt});

    REQUIRE_FALSE(config.empty());
    REQUIRE(config.data().at("found").get<bool>());
    REQUIRE(config.root() == root);
}

TEST_CASE("TeezConfig resolve ignores config when target is outside config root", "[teez_config]") {
    const auto root = std::filesystem::temp_directory_path() / "teez_config_scope";
    const auto outside = std::filesystem::temp_directory_path() / "teez_config_outside";
    std::filesystem::remove_all(root);
    std::filesystem::remove_all(outside);
    write_config(root, "return { found = true }\n");
    std::filesystem::create_directories(outside);

    const auto config = teez::core::TeezConfig::resolve(
        {.search_dir = root, .target_path = outside, .config_file = std::nullopt});

    REQUIRE(config.empty());
}

TEST_CASE("TeezConfig resolve loads explicit config file", "[teez_config]") {
    const auto root = std::filesystem::temp_directory_path() / "teez_config_explicit";
    std::filesystem::remove_all(root);
    const auto config_path = write_config(root, "return { explicit = true }\n");

    const auto config = teez::core::TeezConfig::resolve(
        {.search_dir = root / "missing", .target_path = root, .config_file = config_path});

    REQUIRE_FALSE(config.empty());
    REQUIRE(config.data().at("explicit").get<bool>());
}

TEST_CASE("TeezConfig resolve_project_paths expands projects at config root", "[teez_config]") {
    const auto root = std::filesystem::temp_directory_path() / "teez_config_projects";
    std::filesystem::remove_all(root);
    write_config(root, R"(
return {
    projects = { "alpha", "beta" }
}
)");
    std::filesystem::create_directories(root / "alpha");
    std::filesystem::create_directories(root / "beta");

    const auto config = teez::core::TeezConfig::resolve(
        {.search_dir = root, .target_path = root, .config_file = std::nullopt});

    const auto paths = config.resolve_project_paths(root);

    REQUIRE(paths.size() == 2);
    REQUIRE(paths[0] == std::filesystem::absolute(root / "alpha").lexically_normal());
    REQUIRE(paths[1] == std::filesystem::absolute(root / "beta").lexically_normal());
}

TEST_CASE("TeezConfig resolve_project_paths keeps explicit sub-target", "[teez_config]") {
    const auto root = std::filesystem::temp_directory_path() / "teez_config_projects_sub";
    std::filesystem::remove_all(root);
    write_config(root, "return { projects = { \"alpha\" } }\n");
    const auto nested = root / "nested";
    std::filesystem::create_directories(nested);

    const auto config = teez::core::TeezConfig::resolve(
        {.search_dir = root, .target_path = nested, .config_file = std::nullopt});

    const auto paths = config.resolve_project_paths(nested);

    REQUIRE(paths.size() == 1);
    REQUIRE(paths[0] == std::filesystem::absolute(nested).lexically_normal());
}

TEST_CASE("TeezConfig resolve discovers config in search_dir", "[teez_config]") {
    const auto root = std::filesystem::temp_directory_path() / "teez_config_search_dir";
    std::filesystem::remove_all(root);
    write_config(root, "return { marker = true }\n");

    const auto config = teez::core::TeezConfig::resolve(
        {.search_dir = root, .target_path = root, .config_file = std::nullopt});

    REQUIRE_FALSE(config.empty());
    REQUIRE(config.data().at("marker").get<bool>());
}

TEST_CASE("TeezConfig resolve loads workspace dev config when present", "[teez_config]") {
    const auto workspace = std::filesystem::path(__FILE__).parent_path().parent_path();
    const auto config_path = workspace / "teez.config.lua";
    if (!std::filesystem::exists(config_path)) {
        SKIP("workspace teez.config.lua missing");
    }

    const auto config = teez::core::TeezConfig::resolve(
        {.search_dir = workspace, .target_path = workspace, .config_file = std::nullopt});

    REQUIRE_FALSE(config.empty());

    const auto paths = config.resolve_project_paths(workspace);
    if (paths.size() >= 2) {
        std::filesystem::current_path(workspace);
        REQUIRE(config.resolve_project_paths(std::filesystem::path(".")).size() >= 2);
        return;
    }

    // Standalone teez-core repo (CI): bundled teez.config.lua has profile/runners only.
    REQUIRE(config.data().contains("profile"));
    REQUIRE(config.data().contains("runners"));
}

TEST_CASE("TeezConfig returns empty when no config exists", "[teez_config]") {
    const auto project = std::filesystem::temp_directory_path() / "teez_config_missing";
    std::filesystem::remove_all(project);
    std::filesystem::create_directories(project);

    const auto config = teez::core::TeezConfig::load(project);

    REQUIRE(config.empty());
}

TEST_CASE("validate_config reports missing config file", "[teez_config]") {
    const auto project = std::filesystem::temp_directory_path() / "teez_config_validate_missing";
    std::filesystem::remove_all(project);
    std::filesystem::create_directories(project);

    const auto summary = teez::core::validate_config(
        {.search_dir = project, .target_path = project, .config_file = std::nullopt});

    REQUIRE_FALSE(summary.errors.empty());
}

TEST_CASE("apply_config_profile merges active profile into base config", "[teez_config]") {
    const nlohmann::json raw = {
        {"profile", "ci"},
        {"worker_bin", "/base/worker"},
        {"test_report", {{"reporter", "json"}}},
        {"profiles",
         {{"ci", {{"test_report", {{"reporter", "junit"}, {"output", "out.xml"}}}}},
          {"local", {{"test_report", {{"reporter", "json"}}}}}}},
    };

    const auto merged = teez::core::apply_config_profile(raw, std::nullopt);

    REQUIRE(merged.at("test_report").at("reporter") == "junit");
    REQUIRE(merged.at("test_report").at("output") == "out.xml");
    REQUIRE(merged.at("worker_bin") == "/base/worker");
    REQUIRE(merged.at("profile") == "ci");
    REQUIRE_FALSE(merged.contains("profiles"));
}

TEST_CASE("apply_config_profile honors cli profile override", "[teez_config]") {
    const nlohmann::json raw = {
        {"profile", "ci"},
        {"test_report", {{"reporter", "json"}}},
        {"profiles",
         {{"ci", {{"test_report", {{"reporter", "junit"}}}}},
          {"local", {{"test_report", {{"reporter", "json"}, {"output", "local.json"}}}}}}},
    };

    const auto merged = teez::core::apply_config_profile(raw, std::string{"local"});

    REQUIRE(merged.at("test_report").at("reporter") == "json");
    REQUIRE(merged.at("test_report").at("output") == "local.json");
    REQUIRE(merged.at("profile") == "local");
}

TEST_CASE("TeezConfig resolve applies profile overrides from config", "[teez_config]") {
    const auto root = std::filesystem::temp_directory_path() / "teez_config_profiles";
    std::filesystem::remove_all(root);
    write_config(root, R"(
return {
    profile = "ci",
    test_report = { reporter = "json" },
    profiles = {
        ci = {
            test_report = { reporter = "junit", output = "reports/junit.xml" },
        },
        ["local"] = {
            test_report = { reporter = "json" },
        },
    },
}
)");

    const auto config = teez::core::TeezConfig::resolve(
        {.search_dir = root, .target_path = root, .config_file = std::nullopt});

    REQUIRE_FALSE(config.empty());
    REQUIRE(config.data().at("test_report").at("reporter") == "junit");
    REQUIRE(config.data().at("test_report").at("output") == "reports/junit.xml");
    REQUIRE(config.get_string("profile") == "ci");
    REQUIRE_FALSE(config.data().contains("profiles"));
}

TEST_CASE("TeezConfig resolve applies cli profile override", "[teez_config]") {
    const auto root = std::filesystem::temp_directory_path() / "teez_config_profiles_cli";
    std::filesystem::remove_all(root);
    write_config(root, R"(
return {
    profile = "ci",
    profiles = {
        ci = { test_report = { reporter = "junit" } },
        ["local"] = { test_report = { reporter = "json", output = "local.json" } },
    },
}
)");

    const auto config = teez::core::TeezConfig::resolve(
        {.search_dir = root,
         .target_path = root,
         .config_file = std::nullopt,
         .profile = std::string{"local"}});

    REQUIRE(config.data().at("test_report").at("reporter") == "json");
    REQUIRE(config.data().at("test_report").at("output") == "local.json");
    REQUIRE(config.get_string("profile") == "local");
}

TEST_CASE("TeezConfig resolve throws for unknown profile", "[teez_config]") {
    const auto root = std::filesystem::temp_directory_path() / "teez_config_profiles_unknown";
    std::filesystem::remove_all(root);
    write_config(root, R"(
return {
    profile = "missing",
    profiles = {
        ["local"] = {},
    },
}
)");

    REQUIRE_THROWS_AS(teez::core::TeezConfig::resolve(
                          {.search_dir = root,
                           .target_path = root,
                           .config_file = std::nullopt,
                           .profile = std::string{"staging"}}),
                      std::runtime_error);
}

TEST_CASE("validate_config reports missing project paths", "[teez_config]") {
    const auto project = std::filesystem::temp_directory_path() / "teez_config_validate_projects";
    std::filesystem::remove_all(project);
    std::filesystem::create_directories(project);
    std::ofstream(project / "teez.config.lua")
        << "return { projects = { \"missing-project\" } }\n";

    const auto summary = teez::core::validate_config(
        {.search_dir = project, .target_path = project, .config_file = std::nullopt});

    REQUIRE(summary.loaded);
    REQUIRE_FALSE(summary.errors.empty());
}
