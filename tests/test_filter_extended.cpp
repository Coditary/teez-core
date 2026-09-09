#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>
#include <sol/sol.hpp>

#include "teez/core/test_filter.hpp"
#include "teez/core/teez_config.hpp"

TEST_CASE("filters_from_json and filters_to_json roundtrip all fields", "[filter]") {
    const nlohmann::json json = {{"file_regex", R"(tests/unit/.*)"},
                                 {"types", {"unit", "integration"}},
                                 {"id_substring", "::unit::"},
                                 {"name_glob", "login*"},
                                 {"name_regex", R"(^login )"}};

    const auto filters = teez::core::filters_from_json(json);
    REQUIRE(filters.file_regex == R"(tests/unit/.*)");
    REQUIRE(filters.types == std::vector<std::string>{"unit", "integration"});
    REQUIRE(filters.id_substring == "::unit::");
    REQUIRE(filters.name_glob == "login*");
    REQUIRE(filters.name_regex == R"(^login )");
    REQUIRE_FALSE(filters.empty());

    const auto restored = teez::core::filters_to_json(filters);
    REQUIRE(restored == json);
}

TEST_CASE("filters_from_json ignores non-object input", "[filter]") {
    const auto filters = teez::core::filters_from_json(nlohmann::json::array({1, 2, 3}));
    REQUIRE(filters.empty());
}

TEST_CASE("string_glob_match handles question mark patterns", "[filter]") {
    REQUIRE(teez::core::string_glob_match("a", "?"));
    REQUIRE_FALSE(teez::core::string_glob_match("ab", "?"));
}

TEST_CASE("default_test_id_format reads output.test_id from active config", "[filter]") {
    const auto temp_dir =
        std::filesystem::temp_directory_path() / "teez_filter_format_config_test";
    std::filesystem::remove_all(temp_dir);
    std::filesystem::create_directories(temp_dir);
    std::ofstream(temp_dir / "teez.config.lua") << R"(
return {
    output = {
        test_id = {
            file = false,
            part_separator = " | ",
            suite_separator = " / ",
            template = "${type}::${name}",
        },
    },
}
)";

    teez::core::set_active_config(teez::core::TeezConfig::load(temp_dir));
    const auto format = teez::core::default_test_id_format();

    REQUIRE_FALSE(format.file);
    REQUIRE(format.part_separator == " | ");
    REQUIRE(format.suite_separator == " / ");
    REQUIRE(format.template_string == "${type}::${name}");
}

TEST_CASE("descriptor_from_lua and filters_from_lua roundtrip descriptor fields", "[filter]") {
    sol::state lua;
    const auto descriptor_table = lua.safe_script(R"(
        return {
            file = "demo.teez.lua",
            type = "unit",
            suites = { "Auth", "Login" },
            name = "works",
        }
    )");
    const auto filters_table = lua.safe_script(R"(
        return {
            types = { "unit" },
            id_substring = "::unit::",
            name_regex = "^works$",
        }
    )");

    const auto descriptor =
        teez::core::descriptor_from_lua(descriptor_table.get<sol::table>());
    REQUIRE(descriptor.file == "demo.teez.lua");
    REQUIRE(descriptor.type == "unit");
    REQUIRE(descriptor.name == "works");
    REQUIRE(descriptor.suites == std::vector<std::string>{"Auth", "Login"});

    const auto filters = teez::core::filters_from_lua(filters_table.get<sol::table>());
    REQUIRE(filters.types == std::vector<std::string>{"unit"});
    REQUIRE(filters.id_substring == "::unit::");
    REQUIRE(filters.name_regex == "^works$");
    REQUIRE(teez::core::matches_filter(descriptor, filters));

    const auto restored = teez::core::filters_to_lua(lua, filters);
    REQUIRE(restored["types"].get<sol::table>().size() == 1);
    REQUIRE(restored["name_regex"].get<std::string>() == "^works$");
}

TEST_CASE("matches_filter rejects descriptors on every configured constraint", "[filter]") {
    const teez::core::TestDescriptor descriptor{
        .file = "tests/unit/auth.teez.lua",
        .type = "integration",
        .suites = {"Auth"},
        .name = "login works",
    };

    teez::core::TestFilters filters;
    filters.file_regex = R"(tests/unit/.*)";
    filters.types = {"unit"};
    filters.id_substring = "::unit::";
    filters.name_glob = "logout*";
    filters.name_regex = R"(^logout )";

    REQUIRE_FALSE(teez::core::matches_filter(descriptor, filters));
}
