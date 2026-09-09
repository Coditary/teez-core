#include "teez/core/lua_json.hpp"

namespace teez::core {

namespace {

nlohmann::json lua_table_to_json(const sol::table& table) {
    bool is_array = true;
    std::size_t expected_index = 1;
    for (const auto& pair : table) {
        if (pair.first.get_type() != sol::type::number ||
            pair.first.as<int>() != static_cast<int>(expected_index)) {
            is_array = false;
            break;
        }
        ++expected_index;
    }

    if (is_array && expected_index > 1) {
        nlohmann::json array = nlohmann::json::array();
        for (std::size_t index = 1; index < expected_index; ++index) {
            array.push_back(lua_to_json(table[index]));
        }
        return array;
    }

    nlohmann::json object = nlohmann::json::object();
    for (const auto& pair : table) {
        if (pair.first.get_type() == sol::type::string) {
            object[pair.first.as<std::string>()] = lua_to_json(pair.second);
        }
    }
    return object;
}

} // namespace

nlohmann::json lua_to_json(const sol::object& value) {
    switch (value.get_type()) {
    case sol::type::table:
        return lua_table_to_json(value.as<sol::table>());
    case sol::type::string:
        return value.as<std::string>();
    case sol::type::boolean:
        return value.as<bool>();
    case sol::type::number:
        return value.as<double>();
    case sol::type::lua_nil:
    case sol::type::none:
    default:
        return nullptr;
    }
}

sol::object json_to_lua(sol::state& lua, const nlohmann::json& value) {
    if (value.is_object()) {
        sol::table table = lua.create_table();
        for (const auto& [key, nested] : value.items()) {
            table[key] = json_to_lua(lua, nested);
        }
        return sol::object(lua, sol::in_place, table);
    }
    if (value.is_array()) {
        sol::table table = lua.create_table();
        std::size_t index = 1;
        for (const auto& nested : value) {
            table[index++] = json_to_lua(lua, nested);
        }
        return sol::object(lua, sol::in_place, table);
    }
    if (value.is_string()) {
        return sol::object(lua, sol::in_place, value.get<std::string>());
    }
    if (value.is_boolean()) {
        return sol::object(lua, sol::in_place, value.get<bool>());
    }
    if (value.is_number_integer()) {
        return sol::object(lua, sol::in_place, value.get<std::int64_t>());
    }
    if (value.is_number_unsigned()) {
        return sol::object(lua, sol::in_place, value.get<std::uint64_t>());
    }
    if (value.is_number_float()) {
        return sol::object(lua, sol::in_place, value.get<double>());
    }
    return sol::object(lua, sol::in_place, sol::lua_nil);
}

} // namespace teez::core
