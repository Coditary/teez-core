#include <catch2/catch_test_macros.hpp>

#include <sol/sol.hpp>

#include "teez/core/lua_json.hpp"

TEST_CASE("lua_to_json converts lua arrays to json arrays", "[lua_json]") {
    sol::state lua;
    const auto result = lua.safe_script(R"(
        return { "alpha", "beta", "gamma" }
    )");
    REQUIRE(result.valid());

    const auto json = teez::core::lua_to_json(result.get<sol::object>());
    REQUIRE(json.is_array());
    REQUIRE(json.size() == 3);
    REQUIRE(json[0] == "alpha");
    REQUIRE(json[2] == "gamma");
}

TEST_CASE("lua_to_json converts sparse tables to json objects", "[lua_json]") {
    sol::state lua;
    const auto result = lua.safe_script(R"(
        return { name = "teez", count = 2, enabled = true }
    )");
    REQUIRE(result.valid());

    const auto json = teez::core::lua_to_json(result.get<sol::object>());
    REQUIRE(json.is_object());
    REQUIRE(json.at("name") == "teez");
    REQUIRE(json.at("count") == 2);
    REQUIRE(json.at("enabled") == true);
}

TEST_CASE("lua_to_json converts scalar lua values", "[lua_json]") {
    sol::state lua;

    REQUIRE(teez::core::lua_to_json(sol::make_object(lua, std::string("hello"))).get<std::string>() ==
            "hello");
    REQUIRE(teez::core::lua_to_json(sol::make_object(lua, true)).get<bool>() == true);
    REQUIRE(teez::core::lua_to_json(sol::make_object(lua, 3.5)).get<double>() == 3.5);
    REQUIRE(teez::core::lua_to_json(sol::make_object(lua, sol::lua_nil)).is_null());
}

TEST_CASE("json_to_lua roundtrips nested structures", "[lua_json]") {
    sol::state lua;
    const nlohmann::json payload = {
        {"items", nlohmann::json::array({"a", "b"})},
        {"meta", {{"count", 2}, {"enabled", false}}},
    };

    const sol::object value = teez::core::json_to_lua(lua, payload);
    const auto roundtrip = teez::core::lua_to_json(value);

    REQUIRE(roundtrip.at("items") == payload.at("items"));
    REQUIRE(roundtrip.at("meta") == payload.at("meta"));
}

TEST_CASE("json_to_lua converts numeric and null json values", "[lua_json]") {
    sol::state lua;

    const auto integer = teez::core::json_to_lua(lua, nlohmann::json(42));
    REQUIRE(integer.get_type() == sol::type::number);
    REQUIRE(integer.as<std::int64_t>() == 42);

    const auto floating = teez::core::json_to_lua(lua, nlohmann::json(1.25));
    REQUIRE(floating.as<double>() == 1.25);

    const auto null_value = teez::core::json_to_lua(lua, nlohmann::json(nullptr));
    REQUIRE(null_value.get_type() == sol::type::lua_nil);
}
