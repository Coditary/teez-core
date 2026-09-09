#include <filesystem>

#include <sol/sol.hpp>

#include <coditary/lua/register.hpp>

namespace teez::core {

/// Registers shared Coditary Lua helpers plus teez-specific `sys`, `coverage`, and `filter`.
void register_lua_helpers(
    sol::state& lua, const std::filesystem::path& project_root = std::filesystem::current_path());

} // namespace teez::core
