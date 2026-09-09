#include "teez/core/coverage_msgpack.hpp"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace teez::core {

namespace {

constexpr const char* kCoverageMsgpackFormat = "teez.coverage";
constexpr int kCoverageMsgpackVersion = 1;

template <typename T>
void put_optional(nlohmann::json& object, const char* key, const std::optional<T>& value) {
    if (value.has_value()) {
        object[key] = *value;
    }
}

nlohmann::json condition_to_json(const CoverageCondition& condition) {
    nlohmann::json payload = {
        {"index", condition.index},
        {"taken", condition.taken},
    };
    put_optional(payload, "type", condition.type);
    put_optional(payload, "taken_count", condition.taken_count);
    put_optional(payload, "coverage_text", condition.coverage_text);
    return payload;
}

nlohmann::json region_to_json(const CoverageRegion& region) {
    nlohmann::json payload = {
        {"start_line", region.start_line},
        {"end_line", region.end_line},
    };
    put_optional(payload, "start_column", region.start_column);
    put_optional(payload, "end_column", region.end_column);
    return payload;
}

nlohmann::json branch_to_json(const CoverageBranch& branch) {
    nlohmann::json payload = {
        {"line_number", branch.line_number},
        {"block", branch.block},
        {"branch", branch.branch},
        {"taken", branch.taken},
    };
    put_optional(payload, "taken_count", branch.taken_count);
    put_optional(payload, "condition_coverage", branch.condition_coverage);
    if (!branch.conditions.empty()) {
        nlohmann::json conditions = nlohmann::json::array();
        for (const auto& condition : branch.conditions) {
            conditions.push_back(condition_to_json(condition));
        }
        payload["conditions"] = conditions;
    }
    return payload;
}

nlohmann::json line_to_json(const CoverageLine& line) {
    nlohmann::json payload = {
        {"line_number", line.line_number},
        {"hit_count", line.hit_count},
        {"status", static_cast<int>(line.status)},
        {"kind", static_cast<int>(line.kind)},
        {"excluded", line.excluded},
    };
    put_optional(payload, "statement_id", line.statement_id);
    if (line.region.has_value()) {
        payload["region"] = region_to_json(*line.region);
    }
    if (!line.branches.empty()) {
        nlohmann::json branches = nlohmann::json::array();
        for (const auto& branch : line.branches) {
            branches.push_back(branch_to_json(branch));
        }
        payload["branches"] = branches;
    }
    return payload;
}

nlohmann::json function_to_json(const CoverageFunction& fn) {
    nlohmann::json payload = {
        {"name", fn.name},           {"start_line", fn.start_line}, {"end_line", fn.end_line},
        {"hit_count", fn.hit_count}, {"excluded", fn.excluded},
    };
    put_optional(payload, "signature", fn.signature);
    return payload;
}

nlohmann::json file_to_json(const CoverageFile& file) {
    nlohmann::json payload = {
        {"path", file.path},
        {"instrumented", file.instrumented},
    };
    put_optional(payload, "class_name", file.class_name);
    put_optional(payload, "package_name", file.package_name);
    put_optional(payload, "lines_found_reported", file.lines_found_reported);
    put_optional(payload, "lines_hit_reported", file.lines_hit_reported);
    put_optional(payload, "branches_found_reported", file.branches_found_reported);
    put_optional(payload, "branches_hit_reported", file.branches_hit_reported);
    put_optional(payload, "functions_found_reported", file.functions_found_reported);
    put_optional(payload, "functions_hit_reported", file.functions_hit_reported);
    put_optional(payload, "line_rate_reported", file.line_rate_reported);
    put_optional(payload, "branch_rate_reported", file.branch_rate_reported);

    nlohmann::json lines = nlohmann::json::object();
    for (const auto& [line_number, line] : file.lines) {
        lines[std::to_string(line_number)] = line_to_json(line);
    }
    payload["lines"] = lines;

    nlohmann::json functions = nlohmann::json::object();
    for (const auto& [name, fn] : file.functions) {
        functions[name] = function_to_json(fn);
    }
    payload["functions"] = functions;
    return payload;
}

nlohmann::json meta_to_json(const CoverageReportMeta& meta) {
    nlohmann::json payload = nlohmann::json::object();
    put_optional(payload, "test_name", meta.test_name);
    put_optional(payload, "timestamp", meta.timestamp);
    put_optional(payload, "version", meta.version);
    put_optional(payload, "line_rate_reported", meta.line_rate_reported);
    put_optional(payload, "branch_rate_reported", meta.branch_rate_reported);
    put_optional(payload, "source_report_path", meta.source_report_path);
    return payload;
}

nlohmann::json table_to_json(const CoverageTable& table, bool include_by_test) {
    nlohmann::json payload = {
        {"source_format", static_cast<int>(table.source_format)},
        {"meta", meta_to_json(table.meta)},
        {"source_roots", table.source_roots},
        {"path_aliases", table.path_aliases},
    };

    nlohmann::json files = nlohmann::json::object();
    for (const auto& [path, file] : table.files) {
        files[path] = file_to_json(file);
    }
    payload["files"] = files;

    if (include_by_test && !table.by_test.empty()) {
        nlohmann::json by_test = nlohmann::json::object();
        for (const auto& [test_name, slice] : table.by_test) {
            by_test[test_name] = table_to_json(slice, false);
        }
        payload["by_test"] = by_test;
    }

    return payload;
}

template <typename T>
std::optional<T> read_optional(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) {
        return std::nullopt;
    }
    return object.at(key).get<T>();
}

CoverageCondition condition_from_json(const nlohmann::json& json) {
    CoverageCondition condition;
    condition.index = json.at("index").get<int>();
    condition.taken = json.at("taken").get<bool>();
    condition.type = read_optional<std::string>(json, "type");
    condition.taken_count = read_optional<std::uint64_t>(json, "taken_count");
    condition.coverage_text = read_optional<std::string>(json, "coverage_text");
    return condition;
}

CoverageRegion region_from_json(const nlohmann::json& json) {
    CoverageRegion region;
    region.start_line = json.at("start_line").get<int>();
    region.end_line = json.at("end_line").get<int>();
    region.start_column = read_optional<int>(json, "start_column");
    region.end_column = read_optional<int>(json, "end_column");
    return region;
}

CoverageBranch branch_from_json(const nlohmann::json& json) {
    CoverageBranch branch;
    branch.line_number = json.at("line_number").get<int>();
    branch.block = json.at("block").get<int>();
    branch.branch = json.at("branch").get<int>();
    branch.taken = json.at("taken").get<bool>();
    branch.taken_count = read_optional<std::uint64_t>(json, "taken_count");
    branch.condition_coverage = read_optional<std::string>(json, "condition_coverage");
    if (json.contains("conditions") && json.at("conditions").is_array()) {
        for (const auto& entry : json.at("conditions")) {
            branch.conditions.push_back(condition_from_json(entry));
        }
    }
    return branch;
}

CoverageLine line_from_json(const nlohmann::json& json) {
    CoverageLine line;
    line.line_number = json.at("line_number").get<int>();
    line.hit_count = json.at("hit_count").get<std::uint64_t>();
    line.status = static_cast<CoverageLineStatus>(json.at("status").get<int>());
    line.kind = static_cast<CoverageLineKind>(json.at("kind").get<int>());
    line.excluded = json.at("excluded").get<bool>();
    line.statement_id = read_optional<int>(json, "statement_id");
    if (json.contains("region")) {
        line.region = region_from_json(json.at("region"));
    }
    if (json.contains("branches") && json.at("branches").is_array()) {
        for (const auto& entry : json.at("branches")) {
            line.branches.push_back(branch_from_json(entry));
        }
    }
    return line;
}

CoverageFunction function_from_json(const nlohmann::json& json) {
    CoverageFunction fn;
    fn.name = json.at("name").get<std::string>();
    fn.start_line = json.at("start_line").get<int>();
    fn.end_line = json.at("end_line").get<int>();
    fn.hit_count = json.at("hit_count").get<std::uint64_t>();
    fn.excluded = json.at("excluded").get<bool>();
    fn.signature = read_optional<std::string>(json, "signature");
    return fn;
}

CoverageFile file_from_json(const nlohmann::json& json) {
    CoverageFile file;
    file.path = json.at("path").get<std::string>();
    file.instrumented = json.at("instrumented").get<bool>();
    file.class_name = read_optional<std::string>(json, "class_name");
    file.package_name = read_optional<std::string>(json, "package_name");
    file.lines_found_reported = read_optional<int>(json, "lines_found_reported");
    file.lines_hit_reported = read_optional<int>(json, "lines_hit_reported");
    file.branches_found_reported = read_optional<int>(json, "branches_found_reported");
    file.branches_hit_reported = read_optional<int>(json, "branches_hit_reported");
    file.functions_found_reported = read_optional<int>(json, "functions_found_reported");
    file.functions_hit_reported = read_optional<int>(json, "functions_hit_reported");
    file.line_rate_reported = read_optional<double>(json, "line_rate_reported");
    file.branch_rate_reported = read_optional<double>(json, "branch_rate_reported");

    if (json.contains("lines") && json.at("lines").is_object()) {
        for (const auto& [line_key, entry] : json.at("lines").items()) {
            const int line_number = std::stoi(line_key);
            file.lines[line_number] = line_from_json(entry);
        }
    }

    if (json.contains("functions") && json.at("functions").is_object()) {
        for (const auto& [name, entry] : json.at("functions").items()) {
            file.functions[name] = function_from_json(entry);
        }
    }

    return file;
}

CoverageReportMeta meta_from_json(const nlohmann::json& json) {
    CoverageReportMeta meta;
    meta.test_name = read_optional<std::string>(json, "test_name");
    meta.timestamp = read_optional<std::int64_t>(json, "timestamp");
    meta.version = read_optional<std::string>(json, "version");
    meta.line_rate_reported = read_optional<double>(json, "line_rate_reported");
    meta.branch_rate_reported = read_optional<double>(json, "branch_rate_reported");
    meta.source_report_path = read_optional<std::string>(json, "source_report_path");
    return meta;
}

CoverageTable table_from_json(const nlohmann::json& json) {
    CoverageTable table;
    table.source_format = static_cast<CoverageSourceFormat>(json.at("source_format").get<int>());
    table.meta = meta_from_json(json.at("meta"));
    table.source_roots = json.at("source_roots").get<std::vector<std::string>>();
    table.path_aliases = json.at("path_aliases").get<std::map<std::string, std::string>>();

    if (json.contains("files") && json.at("files").is_object()) {
        for (const auto& [path, entry] : json.at("files").items()) {
            table.files[path] = file_from_json(entry);
        }
    }

    if (json.contains("by_test") && json.at("by_test").is_object()) {
        for (const auto& [test_name, entry] : json.at("by_test").items()) {
            table.by_test[test_name] = table_from_json(entry);
        }
    }

    return table;
}

nlohmann::json envelope_to_json(const CoverageTable& table) {
    return {
        {"format", kCoverageMsgpackFormat},
        {"version", kCoverageMsgpackVersion},
        {"table", table_to_json(table, true)},
    };
}

CoverageTable table_from_envelope(const nlohmann::json& envelope) {
    if (!envelope.contains("format") || envelope.at("format") != kCoverageMsgpackFormat) {
        throw std::runtime_error("invalid coverage msgpack format marker");
    }
    if (!envelope.contains("version") ||
        envelope.at("version").get<int>() != kCoverageMsgpackVersion) {
        throw std::runtime_error("unsupported coverage msgpack version");
    }
    return table_from_json(envelope.at("table"));
}

std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open coverage msgpack file: " + path.string());
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

} // namespace

std::vector<std::uint8_t> coverage_table_to_msgpack(const CoverageTable& table) {
    const auto envelope = envelope_to_json(table);
    return nlohmann::json::to_msgpack(envelope);
}

CoverageTable coverage_table_from_msgpack(const std::vector<std::uint8_t>& data) {
    const auto envelope = nlohmann::json::from_msgpack(data);
    return table_from_envelope(envelope);
}

void write_coverage_table_to_msgpack(const CoverageTable& table,
                                     const std::filesystem::path& path) {
    const auto bytes = coverage_table_to_msgpack(table);
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        throw std::runtime_error("failed to write coverage msgpack: " + path.string());
    }
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

CoverageTable load_coverage_table_from_msgpack(const std::filesystem::path& path) {
    return coverage_table_from_msgpack(read_binary_file(path));
}

} // namespace teez::core
