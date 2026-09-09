#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>
#include <sol/forward.hpp>

namespace teez::core {

struct TestDescriptor {
    std::string file;
    std::string type;
    std::vector<std::string> suites;
    std::string name;
};

struct TestFilters {
    std::optional<std::string> file_regex;
    std::vector<std::string> types;
    std::optional<std::string> id_substring;
    std::optional<std::string> name_glob;
    std::optional<std::string> name_regex;

    bool empty() const;
};

struct TestIdFormat {
    bool file = true;
    bool type = true;
    bool suites = true;
    bool name = true;
    std::string part_separator = "::";
    std::string suite_separator = " > ";
    std::optional<std::string> template_string;
};

/// Shell-style glob on a single string (`*`, `?`).
bool string_glob_match(const std::string& text, const std::string& pattern);

/// POSIX extended regex match.
bool regex_match(const std::string& text, const std::string& pattern);

std::string serialize_id(const TestDescriptor& descriptor);
std::string serialize_id(const TestDescriptor& descriptor, const TestIdFormat& format);
TestIdFormat default_test_id_format();
std::string apply_output_template(const std::string& template_text,
                                  const TestDescriptor& descriptor, const TestIdFormat& format);

/// Resolves descriptor.type from explicit value or teez.config.lua `types` map
/// (`unit = "tests/unit/**"` or `integration = { "a/**", "b/**" }`).
std::string resolve_type(const std::string& file, const std::optional<std::string>& explicit_type);

bool matches_filter(const TestDescriptor& descriptor, const TestFilters& filters);

TestFilters filters_from_json(const nlohmann::json& json);
nlohmann::json filters_to_json(const TestFilters& filters);

TestDescriptor descriptor_from_lua(const sol::table& table);
TestFilters filters_from_lua(const sol::table& table);
sol::table filters_to_lua(sol::state& lua, const TestFilters& filters);
sol::table descriptor_to_lua(sol::state& lua, const TestDescriptor& descriptor);

void register_filter_lua(sol::state& lua);

} // namespace teez::core
