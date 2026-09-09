#pragma once

#include <nlohmann/json.hpp>
#include <sol/sol.hpp>

namespace teez::core {

nlohmann::json lua_to_json(const sol::object& value);
sol::object json_to_lua(sol::state& lua, const nlohmann::json& value);

} // namespace teez::core
