#pragma once

#include <sol/sol.hpp>

namespace teez::core {

void register_coverage_lua(sol::state& lua);
void register_coverage_program_lua(sol::state& lua);

} // namespace teez::core
