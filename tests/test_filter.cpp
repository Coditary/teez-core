#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "teez/core/test_filter.hpp"
#include "teez/core/teez_config.hpp"

namespace {

std::filesystem::path make_temp_dir(const std::string& prefix) {
    const auto base = std::filesystem::temp_directory_path() / (prefix + "-" + std::to_string(std::rand()));
    std::filesystem::create_directories(base);
    return base;
}

void write_config(const std::filesystem::path& dir, const std::string& body) {
    std::ofstream config_file(dir / "teez.config.lua");
    config_file << body;
}

}  // namespace

TEST_CASE("string_glob_match supports shell-style wildcards", "[filter]") {
    REQUIRE(teez::core::string_glob_match("return 9999", "return 999*"));
    REQUIRE(teez::core::string_glob_match("return 999", "return 999*"));
    REQUIRE_FALSE(teez::core::string_glob_match("xreturn 999", "return 999*"));
    REQUIRE(teez::core::string_glob_match("auth token", "*auth*"));
    REQUIRE(teez::core::string_glob_match("case 1", "case ?"));
    REQUIRE(teez::core::string_glob_match("tests/unit/a.teez.lua", "tests/unit/**"));
}

TEST_CASE("regex_match searches text with regex patterns", "[filter]") {
    REQUIRE(teez::core::regex_match("tests/system/smoke.teez.lua", R"(tests/system/.*)"));
    REQUIRE(teez::core::regex_match("case 9999", R"(^case \d+$)"));
    REQUIRE_FALSE(teez::core::regex_match("case abc", R"(^case \d+$)"));
}

TEST_CASE("serialize_id builds canonical descriptor strings", "[filter]") {
    const teez::core::TestDescriptor descriptor{
        .file = "tests/system/smoke.teez.lua",
        .type = "system",
        .suites = {"API", "Health"},
        .name = "returns 200",
    };

    teez::core::TestIdFormat format;
    REQUIRE(teez::core::serialize_id(descriptor, format) ==
            "tests/system/smoke.teez.lua::system::API > Health > returns 200");
    REQUIRE(teez::core::serialize_id(descriptor) ==
            "tests/system/smoke.teez.lua::system::API > Health > returns 200");
}

TEST_CASE("serialize_id supports configurable output parts", "[filter]") {
    const teez::core::TestDescriptor descriptor{
        .file = "benchmark.teez.lua",
        .type = "experiment",
        .suites = {"Hyperfine demo"},
        .name = "sleep 0.01 is faster than sleep 0.02",
    };

    teez::core::TestIdFormat without_file;
    without_file.file = false;
    REQUIRE(teez::core::serialize_id(descriptor, without_file) ==
            "experiment::Hyperfine demo > sleep 0.01 is faster than sleep 0.02");

    teez::core::TestIdFormat name_only;
    name_only.file = false;
    name_only.type = false;
    name_only.suites = false;
    REQUIRE(teez::core::serialize_id(descriptor, name_only) ==
            "sleep 0.01 is faster than sleep 0.02");
}

TEST_CASE("serialize_id supports output templates", "[filter]") {
    const teez::core::TestDescriptor descriptor{
        .file = "benchmark.teez.lua",
        .type = "experiment",
        .suites = {"Hyperfine demo"},
        .name = "sleep 0.01 is faster than sleep 0.02",
    };

    teez::core::TestIdFormat format;
    format.template_string = "${type} | ${name}";
    REQUIRE(teez::core::serialize_id(descriptor, format) ==
            "experiment | sleep 0.01 is faster than sleep 0.02");
    REQUIRE(teez::core::apply_output_template("${suites} > ${name}", descriptor, format) ==
            "Hyperfine demo > sleep 0.01 is faster than sleep 0.02");
}

TEST_CASE("matches_filter applies AND semantics across fields", "[filter]") {
    const teez::core::TestDescriptor descriptor{
        .file = "tests/system/smoke.teez.lua",
        .type = "system",
        .suites = {"API"},
        .name = "return 9999",
    };

    teez::core::TestFilters filters;
    filters.types = {"system"};
    filters.name_glob = "return 999*";
    REQUIRE(teez::core::matches_filter(descriptor, filters));

    filters = {};
    filters.types = {"unit"};
    REQUIRE_FALSE(teez::core::matches_filter(descriptor, filters));

    filters = {};
    filters.file_regex = R"(tests/integration/.*)";
    REQUIRE_FALSE(teez::core::matches_filter(descriptor, filters));

    filters = {};
    filters.id_substring = "::system::";
    REQUIRE(teez::core::matches_filter(descriptor, filters));

    filters = {};
    filters.name_regex = "^nomatch$";
    REQUIRE_FALSE(teez::core::matches_filter(descriptor, filters));
}

TEST_CASE("resolve_type prefers explicit values", "[filter]") {
    REQUIRE(teez::core::resolve_type("tests/unit/auth.teez.lua", "system") == "system");
    REQUIRE(teez::core::resolve_type("tests/unit/auth.teez.lua", std::nullopt) == "default");
}

TEST_CASE("resolve_type maps files from types config keyed by type name", "[filter]") {
    const auto temp_dir = make_temp_dir("resolve_type_map");
    write_config(temp_dir, R"(
return {
    types = {
        unit = "tests/unit/**",
        integration = { "tests/integration/**", "e2e/**" },
        system = "tests/system/**",
    },
}
)");

    teez::core::set_active_config(teez::core::TeezConfig::load(temp_dir));

    REQUIRE(teez::core::resolve_type("tests/unit/auth.teez.lua", std::nullopt) == "unit");
    REQUIRE(teez::core::resolve_type("tests/integration/api.teez.lua", std::nullopt) == "integration");
    REQUIRE(teez::core::resolve_type("e2e/checkout.teez.lua", std::nullopt) == "integration");
    REQUIRE(teez::core::resolve_type("tests/system/smoke.teez.lua", std::nullopt) == "system");
    REQUIRE(teez::core::resolve_type("misc/other.teez.lua", std::nullopt) == "default");
}

TEST_CASE("resolve_type uses first matching type from types config", "[filter]") {
    const auto temp_dir = make_temp_dir("resolve_type_order");
    write_config(temp_dir, R"(
return {
    types = {
        broad = "**/*.teez.lua",
        narrow = "tests/unit/**",
    },
}
)");

    teez::core::set_active_config(teez::core::TeezConfig::load(temp_dir));
    REQUIRE(teez::core::resolve_type("tests/unit/auth.teez.lua", std::nullopt) == "broad");
}

TEST_CASE("resolve_type supports legacy array rules", "[filter]") {
    const auto temp_dir = make_temp_dir("resolve_type_legacy");
    write_config(temp_dir, R"(
return {
    types = {
        { match = "tests/unit/**", type = "unit" },
    },
}
)");

    teez::core::set_active_config(teez::core::TeezConfig::load(temp_dir));
    REQUIRE(teez::core::resolve_type("tests/unit/auth.teez.lua", std::nullopt) == "unit");
}
