#include "teez/core/test_filter.hpp"

#include <stdexcept>

#include <nlohmann/json.hpp>
#include <sol/sol.hpp>

#include <coditary/utils/glob.hpp>
#include <coditary/utils/string_match.hpp>

#include "teez/core/teez_config.hpp"

namespace teez::core {

namespace {

std::vector<std::string> read_string_array(const sol::table& table, const char* key) {
    std::vector<std::string> values;
    const sol::object field = table[key];
    if (field.get_type() != sol::type::table) {
        return values;
    }

    const sol::table array = field;
    for (const auto& pair : array) {
        if (pair.second.get_type() == sol::type::string) {
            values.push_back(pair.second.as<std::string>());
        }
    }
    return values;
}

std::optional<std::string> read_optional_string(const sol::table& table, const char* key) {
    const sol::object field = table[key];
    if (field.get_type() != sol::type::string) {
        return std::nullopt;
    }
    return field.as<std::string>();
}

} // namespace

bool TestFilters::empty() const {
    return !file_regex.has_value() && types.empty() && !id_substring.has_value() &&
           !name_glob.has_value() && !name_regex.has_value();
}

bool string_glob_match(const std::string& text, const std::string& pattern) {
    return coditary::utils::string_glob_match(text, pattern);
}

bool regex_match(const std::string& text, const std::string& pattern) {
    return coditary::utils::regex_search(text, pattern);
}

std::string serialize_id(const TestDescriptor& descriptor) {
    return serialize_id(descriptor, default_test_id_format());
}

namespace {

std::string join_suites(const std::vector<std::string>& suites, const std::string& separator) {
    std::string joined;
    for (std::size_t i = 0; i < suites.size(); ++i) {
        if (i > 0) {
            joined += separator;
        }
        joined += suites[i];
    }
    return joined;
}

void append_part(std::string& id, const std::string& part, const std::string& separator) {
    if (part.empty()) {
        return;
    }
    if (!id.empty()) {
        id += separator;
    }
    id += part;
}

std::optional<bool> read_optional_bool(const nlohmann::json& json, const char* key) {
    if (!json.contains(key) || !json.at(key).is_boolean()) {
        return std::nullopt;
    }
    return json.at(key).get<bool>();
}

std::optional<std::string> read_optional_string_json(const nlohmann::json& json, const char* key) {
    if (!json.contains(key) || !json.at(key).is_string()) {
        return std::nullopt;
    }
    return json.at(key).get<std::string>();
}

} // namespace

std::string apply_output_template(const std::string& template_text,
                                  const TestDescriptor& descriptor, const TestIdFormat& format) {
    std::string output = template_text;
    const auto replace = [&](const std::string& token, const std::string& value) {
        std::size_t pos = 0;
        while ((pos = output.find(token, pos)) != std::string::npos) {
            output.replace(pos, token.size(), value);
            pos += value.size();
        }
    };

    replace("${file}", descriptor.file);
    replace("${type}", descriptor.type);
    replace("${name}", descriptor.name);
    replace("${suites}", join_suites(descriptor.suites, format.suite_separator));
    return output;
}

std::string serialize_id(const TestDescriptor& descriptor, const TestIdFormat& format) {
    if (format.template_string.has_value()) {
        return apply_output_template(*format.template_string, descriptor, format);
    }

    std::string id;
    if (format.file) {
        append_part(id, descriptor.file, format.part_separator);
    }
    if (format.type) {
        append_part(id, descriptor.type, format.part_separator);
    }

    std::string tail;
    if (format.suites) {
        tail = join_suites(descriptor.suites, format.suite_separator);
    }
    if (format.name) {
        if (!tail.empty()) {
            tail += format.suite_separator;
        }
        tail += descriptor.name;
    }

    if (!tail.empty()) {
        append_part(id, tail, format.part_separator);
    }

    return id;
}

TestIdFormat default_test_id_format() {
    TestIdFormat format;
    const auto& config = active_config().data();
    if (!config.is_object() || !config.contains("output") || !config.at("output").is_object()) {
        return format;
    }

    const auto& output = config.at("output");
    if (!output.contains("test_id") || !output.at("test_id").is_object()) {
        return format;
    }

    const auto& test_id = output.at("test_id");
    if (const auto file = read_optional_bool(test_id, "file")) {
        format.file = *file;
    }
    if (const auto type = read_optional_bool(test_id, "type")) {
        format.type = *type;
    }
    if (const auto suites = read_optional_bool(test_id, "suites")) {
        format.suites = *suites;
    }
    if (const auto name = read_optional_bool(test_id, "name")) {
        format.name = *name;
    }
    if (const auto part_separator = read_optional_string_json(test_id, "part_separator")) {
        format.part_separator = *part_separator;
    }
    if (const auto suite_separator = read_optional_string_json(test_id, "suite_separator")) {
        format.suite_separator = *suite_separator;
    }
    if (const auto template_string = read_optional_string_json(test_id, "template")) {
        format.template_string = template_string;
    }

    return format;
}

namespace {

bool file_matches_type_patterns(const nlohmann::json& patterns, const std::string& file) {
    const auto& matcher = coditary::utils::default_glob_matcher();
    if (patterns.is_string()) {
        return matcher.matches(patterns.get<std::string>(), file);
    }
    if (!patterns.is_array()) {
        return false;
    }

    for (const auto& pattern : patterns) {
        if (pattern.is_string() && matcher.matches(pattern.get<std::string>(), file)) {
            return true;
        }
    }
    return false;
}

} // namespace

std::string resolve_type(const std::string& file, const std::optional<std::string>& explicit_type) {
    if (explicit_type.has_value() && !explicit_type->empty()) {
        return *explicit_type;
    }

    const auto& config = active_config().data();
    if (!config.is_object() || !config.contains("types")) {
        return "default";
    }

    const auto& types = config.at("types");
    if (types.is_object()) {
        for (const auto& [type_name, patterns] : types.items()) {
            if (file_matches_type_patterns(patterns, file)) {
                return type_name;
            }
        }
        return "default";
    }

    if (types.is_array()) {
        for (const auto& rule : types) {
            if (!rule.is_object()) {
                continue;
            }
            const auto match_it = rule.find("match");
            const auto type_it = rule.find("type");
            if (match_it == rule.end() || type_it == rule.end() || !match_it->is_string() ||
                !type_it->is_string()) {
                continue;
            }
            if (coditary::utils::default_glob_matcher().matches(match_it->get<std::string>(),
                                                                file)) {
                return type_it->get<std::string>();
            }
        }
    }

    return "default";
}

bool matches_filter(const TestDescriptor& descriptor, const TestFilters& filters) {
    if (filters.empty()) {
        return true;
    }

    if (filters.file_regex.has_value() && !regex_match(descriptor.file, *filters.file_regex)) {
        return false;
    }

    if (!filters.types.empty()) {
        bool type_matches = false;
        for (const auto& type : filters.types) {
            if (descriptor.type == type) {
                type_matches = true;
                break;
            }
        }
        if (!type_matches) {
            return false;
        }
    }

    if (filters.id_substring.has_value()) {
        const std::string id = serialize_id(descriptor);
        if (id.find(*filters.id_substring) == std::string::npos) {
            return false;
        }
    }

    if (filters.name_glob.has_value() && !string_glob_match(descriptor.name, *filters.name_glob)) {
        return false;
    }

    if (filters.name_regex.has_value() && !regex_match(descriptor.name, *filters.name_regex)) {
        return false;
    }

    return true;
}

TestFilters filters_from_json(const nlohmann::json& json) {
    TestFilters filters;
    if (!json.is_object()) {
        return filters;
    }

    if (json.contains("file_regex") && json.at("file_regex").is_string()) {
        filters.file_regex = json.at("file_regex").get<std::string>();
    }
    if (json.contains("types") && json.at("types").is_array()) {
        for (const auto& type : json.at("types")) {
            if (type.is_string()) {
                filters.types.push_back(type.get<std::string>());
            }
        }
    }
    if (json.contains("id_substring") && json.at("id_substring").is_string()) {
        filters.id_substring = json.at("id_substring").get<std::string>();
    }
    if (json.contains("name_glob") && json.at("name_glob").is_string()) {
        filters.name_glob = json.at("name_glob").get<std::string>();
    }
    if (json.contains("name_regex") && json.at("name_regex").is_string()) {
        filters.name_regex = json.at("name_regex").get<std::string>();
    }

    return filters;
}

nlohmann::json filters_to_json(const TestFilters& filters) {
    nlohmann::json json = nlohmann::json::object();
    if (filters.file_regex.has_value()) {
        json["file_regex"] = *filters.file_regex;
    }
    if (!filters.types.empty()) {
        json["types"] = filters.types;
    }
    if (filters.id_substring.has_value()) {
        json["id_substring"] = *filters.id_substring;
    }
    if (filters.name_glob.has_value()) {
        json["name_glob"] = *filters.name_glob;
    }
    if (filters.name_regex.has_value()) {
        json["name_regex"] = *filters.name_regex;
    }
    return json;
}

TestDescriptor descriptor_from_lua(const sol::table& table) {
    TestDescriptor descriptor;
    if (table["file"].valid() && table["file"].get_type() == sol::type::string) {
        descriptor.file = table["file"].get<std::string>();
    }
    if (table["type"].valid() && table["type"].get_type() == sol::type::string) {
        descriptor.type = table["type"].get<std::string>();
    }
    if (table["name"].valid() && table["name"].get_type() == sol::type::string) {
        descriptor.name = table["name"].get<std::string>();
    }
    if (table["suites"].valid() && table["suites"].get_type() == sol::type::table) {
        const sol::table suites = table["suites"];
        for (const auto& pair : suites) {
            if (pair.second.get_type() == sol::type::string) {
                descriptor.suites.push_back(pair.second.as<std::string>());
            }
        }
    }
    return descriptor;
}

TestFilters filters_from_lua(const sol::table& table) {
    TestFilters filters;
    filters.file_regex = read_optional_string(table, "file_regex");
    filters.types = read_string_array(table, "types");
    filters.id_substring = read_optional_string(table, "id_substring");
    filters.name_glob = read_optional_string(table, "name_glob");
    filters.name_regex = read_optional_string(table, "name_regex");
    return filters;
}

sol::table filters_to_lua(sol::state& lua, const TestFilters& filters) {
    sol::table table = lua.create_table();
    if (filters.file_regex.has_value()) {
        table["file_regex"] = *filters.file_regex;
    }
    if (!filters.types.empty()) {
        sol::table types = lua.create_table();
        std::size_t index = 1;
        for (const auto& type : filters.types) {
            types[index++] = type;
        }
        table["types"] = types;
    }
    if (filters.id_substring.has_value()) {
        table["id_substring"] = *filters.id_substring;
    }
    if (filters.name_glob.has_value()) {
        table["name_glob"] = *filters.name_glob;
    }
    if (filters.name_regex.has_value()) {
        table["name_regex"] = *filters.name_regex;
    }
    return table;
}

sol::table descriptor_to_lua(sol::state& lua, const TestDescriptor& descriptor) {
    sol::table table = lua.create_table();
    table["file"] = descriptor.file;
    table["type"] = descriptor.type;
    table["name"] = descriptor.name;

    sol::table suites = lua.create_table();
    std::size_t index = 1;
    for (const auto& suite : descriptor.suites) {
        suites[index++] = suite;
    }
    table["suites"] = suites;
    return table;
}

void register_filter_lua(sol::state& lua) {
    sol::table filter = lua.create_named_table("filter");
    filter["glob_match"] = [](const std::string& text, const std::string& pattern) {
        return string_glob_match(text, pattern);
    };
    filter["regex_match"] = [](const std::string& text, const std::string& pattern) {
        return regex_match(text, pattern);
    };
    filter["serialize_id"] = [](const sol::table& descriptor_table) {
        return serialize_id(descriptor_from_lua(descriptor_table));
    };
    filter["apply_template"] = [](const std::string& template_text,
                                  const sol::table& descriptor_table) {
        return apply_output_template(template_text, descriptor_from_lua(descriptor_table),
                                     default_test_id_format());
    };
    filter["resolve_type"] = [](const std::string& file, sol::optional<std::string> explicit_type) {
        std::optional<std::string> resolved_type;
        if (explicit_type.has_value()) {
            resolved_type = *explicit_type;
        }
        return resolve_type(file, resolved_type);
    };
    filter["matches"] = [](const sol::table& descriptor_table, const sol::table& filters_table) {
        return matches_filter(descriptor_from_lua(descriptor_table),
                              filters_from_lua(filters_table));
    };
}

} // namespace teez::core
