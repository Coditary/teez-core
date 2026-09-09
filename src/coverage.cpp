#include "teez/core/coverage.hpp"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <stdexcept>

#include "teez/core/coverage_msgpack.hpp"

namespace teez::core {

namespace {

bool looks_like_cobertura(const std::string& content) {
    const auto xml_pos = content.find("<?xml");
    if (xml_pos == std::string::npos) {
        return false;
    }
    return content.find("<coverage", xml_pos) != std::string::npos ||
           content.find(":coverage", xml_pos) != std::string::npos;
}

bool looks_like_lcov(const std::string& content) {
    return content.find("SF:") != std::string::npos ||
           content.find("end_of_record") != std::string::npos;
}

std::string read_file_text(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open coverage file: " + path.string());
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void apply_line_hits(CoverageLine& line, std::uint64_t hits) {
    if (line.excluded) {
        return;
    }
    line.hit_count = hits;
    if (hits > 0) {
        line.status = CoverageLineStatus::Covered;
        line.kind = CoverageLineKind::Covered;
    } else {
        line.status = CoverageLineStatus::Uncovered;
        if (line.kind != CoverageLineKind::NonExecutable) {
            line.kind = CoverageLineKind::Executable;
        }
    }
}

void add_line_hits(CoverageLine& line, std::uint64_t hits) {
    if (line.excluded) {
        return;
    }
    line.hit_count += hits;
    apply_line_hits(line, line.hit_count);
}

CoverageBranch* find_branch(CoverageLine& line, int block, int branch) {
    for (auto& entry : line.branches) {
        if (entry.block == block && entry.branch == branch) {
            return &entry;
        }
    }
    return nullptr;
}

void merge_conditions(std::vector<CoverageCondition>& target,
                      const std::vector<CoverageCondition>& other) {
    for (const auto& condition : other) {
        auto it = std::find_if(target.begin(), target.end(), [&](const CoverageCondition& entry) {
            return entry.index == condition.index;
        });
        if (it == target.end()) {
            target.push_back(condition);
            continue;
        }
        if (condition.taken_count.has_value()) {
            it->taken_count = it->taken_count.value_or(0) + *condition.taken_count;
            it->taken = it->taken_count.value() > 0;
        }
        if (condition.type.has_value()) {
            it->type = condition.type;
        }
        if (condition.coverage_text.has_value()) {
            it->coverage_text = condition.coverage_text;
        }
    }
}

void merge_branch(CoverageLine& line, const CoverageBranch& other) {
    if (auto* existing = find_branch(line, other.block, other.branch)) {
        if (other.taken_count.has_value()) {
            existing->taken_count = existing->taken_count.value_or(0) + *other.taken_count;
            existing->taken = existing->taken_count.value() > 0;
        } else {
            existing->taken = false;
            existing->taken_count = 0;
        }
        if (other.condition_coverage.has_value()) {
            existing->condition_coverage = other.condition_coverage;
        }
        merge_conditions(existing->conditions, other.conditions);
        return;
    }

    line.branches.push_back(other);
}

int compute_lines_hit(const CoverageFile& file) {
    int hit = 0;
    for (const auto& [_, line] : file.lines) {
        if (line.excluded) {
            continue;
        }
        if (line.hit_count > 0) {
            ++hit;
        }
    }
    return hit;
}

int compute_lines_found(const CoverageFile& file) {
    int found = 0;
    for (const auto& [_, line] : file.lines) {
        if (!line.excluded) {
            ++found;
        }
    }
    return found;
}

int compute_branches_found(const CoverageFile& file) {
    int total = 0;
    for (const auto& [_, line] : file.lines) {
        if (line.excluded) {
            continue;
        }
        total += static_cast<int>(line.branches.size());
    }
    return total;
}

int compute_branches_hit(const CoverageFile& file) {
    int hit = 0;
    for (const auto& [_, line] : file.lines) {
        if (line.excluded) {
            continue;
        }
        for (const auto& branch : line.branches) {
            if (branch.taken) {
                ++hit;
            }
        }
    }
    return hit;
}

int compute_functions_found(const CoverageFile& file) {
    int found = 0;
    for (const auto& [_, fn] : file.functions) {
        if (!fn.excluded) {
            ++found;
        }
    }
    return found;
}

int compute_functions_hit(const CoverageFile& file) {
    int hit = 0;
    for (const auto& [_, fn] : file.functions) {
        if (!fn.excluded) {
            if (fn.hit_count > 0) {
                ++hit;
            }
        }
    }
    return hit;
}

void recompute_file_aggregates(CoverageFile& file) {
    file.lines_found_reported = compute_lines_found(file);
    file.lines_hit_reported = compute_lines_hit(file);
    file.branches_found_reported = compute_branches_found(file);
    file.branches_hit_reported = compute_branches_hit(file);
    file.functions_found_reported = compute_functions_found(file);
    file.functions_hit_reported = compute_functions_hit(file);
    file.line_rate_reported.reset();
    file.branch_rate_reported.reset();
}

void merge_file(CoverageFile& target, const CoverageFile& other) {
    if (other.class_name.has_value()) {
        target.class_name = other.class_name;
    }
    if (other.package_name.has_value()) {
        target.package_name = other.package_name;
    }
    target.instrumented = target.instrumented || other.instrumented;

    for (const auto& [line_number, other_line] : other.lines) {
        auto& line = target.lines[line_number];
        line.line_number = line_number;
        if (other_line.excluded) {
            line.excluded = true;
            line.kind = CoverageLineKind::NonExecutable;
            line.status = CoverageLineStatus::Unknown;
            line.hit_count = 0;
        } else {
            add_line_hits(line, other_line.hit_count);
        }
        if (other_line.statement_id.has_value()) {
            line.statement_id = other_line.statement_id;
        }
        if (other_line.region.has_value()) {
            line.region = other_line.region;
        }

        for (const auto& branch : other_line.branches) {
            merge_branch(line, branch);
        }
    }

    for (const auto& [name, other_fn] : other.functions) {
        auto& fn = target.functions[name];
        fn.name = name;
        if (fn.start_line == 0) {
            fn.start_line = other_fn.start_line;
        }
        if (other_fn.end_line > fn.end_line) {
            fn.end_line = other_fn.end_line;
        }
        if (other_fn.signature.has_value()) {
            fn.signature = other_fn.signature;
        }
        fn.excluded = fn.excluded || other_fn.excluded;
        fn.hit_count += other_fn.hit_count;
    }

    recompute_file_aggregates(target);
}

void merge_meta(CoverageReportMeta& target, const CoverageReportMeta& other) {
    const auto prefer = [&](auto& field, const auto& value) {
        if (value.has_value()) {
            field = value;
        }
    };

    prefer(target.test_name, other.test_name);
    prefer(target.timestamp, other.timestamp);
    prefer(target.version, other.version);
    prefer(target.line_rate_reported, other.line_rate_reported);
    prefer(target.branch_rate_reported, other.branch_rate_reported);
    prefer(target.source_report_path, other.source_report_path);
}

double rate_from_counts(int hit, int found) {
    if (found == 0) {
        return 0.0;
    }
    return static_cast<double>(hit) / static_cast<double>(found);
}

double rate_from_reported_or_computed(std::optional<double> reported, int hit, int found) {
    if (reported.has_value()) {
        return *reported;
    }
    return rate_from_counts(hit, found);
}

bool match_wildcard(const std::string& text, const std::string& pattern) {
    const auto star = pattern.find('*');
    if (star == std::string::npos) {
        return text == pattern;
    }
    const std::string prefix = pattern.substr(0, star);
    const std::string suffix = pattern.substr(star + 1);
    if (text.size() < prefix.size() + suffix.size()) {
        return false;
    }
    if (!prefix.empty() && text.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    if (!suffix.empty() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return false;
    }
    return true;
}

nlohmann::json line_to_json(const CoverageLine& line) {
    nlohmann::json payload = {
        {"line_number", line.line_number},
        {"hit_count", line.hit_count},
        {"status", static_cast<int>(line.status)},
        {"kind", static_cast<int>(line.kind)},
        {"excluded", line.excluded},
    };
    if (line.statement_id.has_value()) {
        payload["statement_id"] = *line.statement_id;
    }
    if (line.region.has_value()) {
        payload["region"] = {
            {"start_line", line.region->start_line},
            {"end_line", line.region->end_line},
        };
        if (line.region->start_column.has_value()) {
            payload["region"]["start_column"] = *line.region->start_column;
        }
        if (line.region->end_column.has_value()) {
            payload["region"]["end_column"] = *line.region->end_column;
        }
    }
    if (!line.branches.empty()) {
        nlohmann::json branches = nlohmann::json::array();
        for (const auto& branch : line.branches) {
            nlohmann::json branch_json = {
                {"line_number", branch.line_number},
                {"block", branch.block},
                {"branch", branch.branch},
                {"taken", branch.taken},
            };
            if (branch.taken_count.has_value()) {
                branch_json["taken_count"] = *branch.taken_count;
            }
            if (branch.condition_coverage.has_value()) {
                branch_json["condition_coverage"] = *branch.condition_coverage;
            }
            if (!branch.conditions.empty()) {
                nlohmann::json conditions = nlohmann::json::array();
                for (const auto& condition : branch.conditions) {
                    nlohmann::json condition_json = {{"index", condition.index},
                                                     {"taken", condition.taken}};
                    if (condition.type.has_value()) {
                        condition_json["type"] = *condition.type;
                    }
                    if (condition.taken_count.has_value()) {
                        condition_json["taken_count"] = *condition.taken_count;
                    }
                    if (condition.coverage_text.has_value()) {
                        condition_json["coverage_text"] = *condition.coverage_text;
                    }
                    conditions.push_back(condition_json);
                }
                branch_json["conditions"] = conditions;
            }
            branches.push_back(branch_json);
        }
        payload["branches"] = branches;
    }
    return payload;
}

nlohmann::json file_to_json(const CoverageFile& file) {
    nlohmann::json payload = {
        {"path", file.path},
        {"instrumented", file.instrumented},
        {"lines_found", file.lines_found()},
        {"lines_hit", file.lines_hit()},
        {"line_rate", file.line_rate()},
        {"branches_found", file.branches_found()},
        {"branches_hit", file.branches_hit()},
        {"branch_rate", file.branch_rate()},
        {"functions_found", file.functions_found()},
        {"functions_hit", file.functions_hit()},
        {"function_rate", file.function_rate()},
    };
    if (file.class_name.has_value()) {
        payload["class_name"] = *file.class_name;
    }
    if (file.package_name.has_value()) {
        payload["package_name"] = *file.package_name;
    }

    nlohmann::json lines = nlohmann::json::array();
    for (const auto& [_, line] : file.lines) {
        lines.push_back(line_to_json(line));
    }
    payload["lines"] = lines;

    nlohmann::json functions = nlohmann::json::array();
    for (const auto& [_, fn] : file.functions) {
        nlohmann::json fn_json = {
            {"name", fn.name},           {"start_line", fn.start_line}, {"end_line", fn.end_line},
            {"hit_count", fn.hit_count}, {"excluded", fn.excluded},
        };
        if (fn.signature.has_value()) {
            fn_json["signature"] = *fn.signature;
        }
        functions.push_back(fn_json);
    }
    payload["functions"] = functions;
    return payload;
}

nlohmann::json table_to_json(const CoverageTable& table, bool include_by_test) {
    nlohmann::json payload = {
        {"source_format", table.source_format == CoverageSourceFormat::Lcov ? "lcov" : "cobertura"},
        {"line_rate", table.line_rate()},
        {"branch_rate", table.branch_rate()},
        {"function_rate", table.function_rate()},
        {"total_lines_found", table.total_lines_found()},
        {"total_lines_hit", table.total_lines_hit()},
        {"total_branches_found", table.total_branches_found()},
        {"total_branches_hit", table.total_branches_hit()},
        {"total_functions_found", table.total_functions_found()},
        {"total_functions_hit", table.total_functions_hit()},
    };

    if (table.meta.test_name.has_value()) {
        payload["meta"]["test_name"] = *table.meta.test_name;
    }
    if (table.meta.timestamp.has_value()) {
        payload["meta"]["timestamp"] = *table.meta.timestamp;
    }
    if (table.meta.version.has_value()) {
        payload["meta"]["version"] = *table.meta.version;
    }
    if (table.meta.source_report_path.has_value()) {
        payload["meta"]["source_report_path"] = *table.meta.source_report_path;
    }
    if (!table.source_roots.empty()) {
        payload["source_roots"] = table.source_roots;
    }
    if (!table.path_aliases.empty()) {
        payload["path_aliases"] = table.path_aliases;
    }

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

} // namespace

std::string normalize_coverage_path(const std::string& path) {
    if (path.empty()) {
        return path;
    }
    return std::filesystem::path(path).lexically_normal().generic_string();
}

bool coverage_path_matches(const std::string& path, const std::string& pattern) {
    const auto normalized_path = normalize_coverage_path(path);
    const auto normalized_pattern = normalize_coverage_path(pattern);

    if (normalized_pattern.find('/') == std::string::npos) {
        const auto filename = std::filesystem::path(normalized_path).filename().string();
        return match_wildcard(filename, normalized_pattern);
    }

    if (normalized_pattern.size() >= 3 &&
        normalized_pattern.compare(normalized_pattern.size() - 3, 3, "/**") == 0) {
        const std::string prefix = normalized_pattern.substr(0, normalized_pattern.size() - 2);
        if (prefix.empty()) {
            return true;
        }
        return normalized_path.rfind(prefix, 0) == 0;
    }

    std::vector<std::string> pattern_parts;
    std::vector<std::string> path_parts;
    {
        std::stringstream pattern_stream(normalized_pattern);
        std::string segment;
        while (std::getline(pattern_stream, segment, '/')) {
            if (!segment.empty()) {
                pattern_parts.push_back(segment);
            }
        }
        std::stringstream path_stream(normalized_path);
        while (std::getline(path_stream, segment, '/')) {
            if (!segment.empty()) {
                path_parts.push_back(segment);
            }
        }
    }

    if (pattern_parts.size() != path_parts.size()) {
        return false;
    }
    for (std::size_t index = 0; index < pattern_parts.size(); ++index) {
        if (!match_wildcard(path_parts[index], pattern_parts[index])) {
            return false;
        }
    }
    return true;
}

int CoverageFile::lines_found() const {
    const int computed = compute_lines_found(*this);
    if (computed > 0) {
        return computed;
    }
    if (lines_found_reported.has_value()) {
        return *lines_found_reported;
    }
    return 0;
}

int CoverageFile::lines_hit() const {
    const int computed = compute_lines_hit(*this);
    if (compute_lines_found(*this) > 0) {
        return computed;
    }
    if (lines_hit_reported.has_value()) {
        return *lines_hit_reported;
    }
    return computed;
}

double CoverageFile::line_rate() const {
    return rate_from_reported_or_computed(line_rate_reported, lines_hit(), lines_found());
}

int CoverageFile::branches_found() const {
    const int computed = compute_branches_found(*this);
    if (computed > 0) {
        return computed;
    }
    if (branches_found_reported.has_value()) {
        return *branches_found_reported;
    }
    return 0;
}

int CoverageFile::branches_hit() const {
    const int computed = compute_branches_hit(*this);
    if (compute_branches_found(*this) > 0) {
        return computed;
    }
    if (branches_hit_reported.has_value()) {
        return *branches_hit_reported;
    }
    return computed;
}

double CoverageFile::branch_rate() const {
    return rate_from_reported_or_computed(branch_rate_reported, branches_hit(), branches_found());
}

int CoverageFile::functions_found() const {
    const int computed = compute_functions_found(*this);
    if (computed > 0) {
        return computed;
    }
    if (functions_found_reported.has_value()) {
        return *functions_found_reported;
    }
    return 0;
}

int CoverageFile::functions_hit() const {
    const int computed = compute_functions_hit(*this);
    if (compute_functions_found(*this) > 0) {
        return computed;
    }
    if (functions_hit_reported.has_value()) {
        return *functions_hit_reported;
    }
    return computed;
}

double CoverageFile::function_rate() const {
    return rate_from_counts(functions_hit(), functions_found());
}

const CoverageLine* CoverageFile::find_line(int line_number) const {
    const auto it = lines.find(line_number);
    if (it == lines.end()) {
        return nullptr;
    }
    return &it->second;
}

const CoverageFunction* CoverageFile::find_function(const std::string& name) const {
    const auto it = functions.find(name);
    if (it == functions.end()) {
        return nullptr;
    }
    return &it->second;
}

void CoverageFile::mark_line_excluded(int line_number, bool excluded) {
    auto& line = lines[line_number];
    line.line_number = line_number;
    line.excluded = excluded;
    if (excluded) {
        line.kind = CoverageLineKind::NonExecutable;
        line.status = CoverageLineStatus::Unknown;
        line.hit_count = 0;
    } else if (line.hit_count > 0) {
        line.kind = CoverageLineKind::Covered;
        line.status = CoverageLineStatus::Covered;
    } else {
        line.kind = CoverageLineKind::Executable;
        line.status = CoverageLineStatus::Uncovered;
    }
}

void CoverageFile::infer_line_kinds() {
    for (auto& [_, line] : lines) {
        if (line.excluded) {
            line.kind = CoverageLineKind::NonExecutable;
            continue;
        }
        if (line.hit_count > 0) {
            line.kind = CoverageLineKind::Covered;
            line.status = CoverageLineStatus::Covered;
        } else if (line.kind == CoverageLineKind::Unknown) {
            line.kind = CoverageLineKind::Executable;
            line.status = CoverageLineStatus::Uncovered;
        }
    }
}

int CoverageTable::total_lines_found() const {
    int total = 0;
    for (const auto& [_, file] : files) {
        total += file.lines_found();
    }
    return total;
}

int CoverageTable::total_lines_hit() const {
    int total = 0;
    for (const auto& [_, file] : files) {
        total += file.lines_hit();
    }
    return total;
}

double CoverageTable::line_rate() const {
    return rate_from_reported_or_computed(meta.line_rate_reported, total_lines_hit(),
                                          total_lines_found());
}

int CoverageTable::total_branches_found() const {
    int total = 0;
    for (const auto& [_, file] : files) {
        total += file.branches_found();
    }
    return total;
}

int CoverageTable::total_branches_hit() const {
    int total = 0;
    for (const auto& [_, file] : files) {
        total += file.branches_hit();
    }
    return total;
}

double CoverageTable::branch_rate() const {
    return rate_from_reported_or_computed(meta.branch_rate_reported, total_branches_hit(),
                                          total_branches_found());
}

int CoverageTable::total_functions_found() const {
    int total = 0;
    for (const auto& [_, file] : files) {
        total += file.functions_found();
    }
    return total;
}

int CoverageTable::total_functions_hit() const {
    int total = 0;
    for (const auto& [_, file] : files) {
        total += file.functions_hit();
    }
    return total;
}

double CoverageTable::function_rate() const {
    return rate_from_counts(total_functions_hit(), total_functions_found());
}

std::string CoverageTable::resolve_file_path(const std::string& path) const {
    const auto normalized = normalize_coverage_path(path);
    if (const auto alias = path_aliases.find(normalized); alias != path_aliases.end()) {
        return alias->second;
    }

    if (files.contains(normalized)) {
        return normalized;
    }

    for (const auto& root : source_roots) {
        const auto rooted = normalize_coverage_path(root + "/" + normalized);
        if (files.contains(rooted)) {
            return rooted;
        }
        if (const auto stripped = normalize_coverage_path(
                std::filesystem::path(normalized).lexically_relative(root).generic_string());
            !stripped.empty() && stripped != normalized && files.contains(stripped)) {
            return stripped;
        }
    }

    const auto filename = std::filesystem::path(normalized).filename().string();
    std::vector<std::string> candidates;
    for (const auto& [existing, _] : files) {
        if (std::filesystem::path(existing).filename().string() == filename) {
            candidates.push_back(existing);
        }
    }
    if (candidates.size() == 1) {
        return candidates.front();
    }

    return normalized;
}

void CoverageTable::register_path_alias(const std::string& alias, const std::string& canonical) {
    path_aliases[normalize_coverage_path(alias)] = normalize_coverage_path(canonical);
}

void CoverageTable::add_source_root(const std::string& root) {
    source_roots.push_back(normalize_coverage_path(root));
}

const CoverageFile* CoverageTable::find_file(const std::string& path) const {
    const auto resolved = resolve_file_path(path);
    const auto it = files.find(resolved);
    if (it == files.end()) {
        return nullptr;
    }
    return &it->second;
}

CoverageFile& CoverageTable::upsert_file(const std::string& path) {
    const auto normalized = normalize_coverage_path(path);
    const auto resolved = resolve_file_path(path);
    auto& file = files[resolved];
    if (file.path.empty()) {
        file.path = resolved;
    }
    if (normalized != resolved) {
        path_aliases[normalized] = resolved;
    }
    return file;
}

void CoverageTable::recompute_aggregates() {
    for (auto& [_, file] : files) {
        recompute_file_aggregates(file);
    }
    meta.line_rate_reported.reset();
    meta.branch_rate_reported.reset();
}

void CoverageTable::infer_line_kinds() {
    for (auto& [_, file] : files) {
        file.infer_line_kinds();
    }
    for (auto& [_, slice] : by_test) {
        slice.infer_line_kinds();
    }
}

void CoverageTable::merge(const CoverageTable& other) {
    merge_meta(meta, other.meta);

    for (const auto& root : other.source_roots) {
        add_source_root(root);
    }
    for (const auto& [alias, canonical] : other.path_aliases) {
        register_path_alias(alias, canonical);
    }

    for (const auto& [path, other_file] : other.files) {
        merge_file(upsert_file(path), other_file);
    }

    for (const auto& [test_name, other_slice] : other.by_test) {
        by_test[test_name].source_format = other_slice.source_format;
        by_test[test_name].merge(other_slice);
    }
}

std::vector<std::string> CoverageTable::files_matching(const std::string& pattern) const {
    std::vector<std::string> matches;
    for (const auto& [path, _] : files) {
        if (coverage_path_matches(path, pattern)) {
            matches.push_back(path);
        }
    }
    std::sort(matches.begin(), matches.end());
    return matches;
}

std::vector<int> CoverageTable::uncovered_lines(const std::string& path) const {
    const auto* file = find_file(path);
    if (file == nullptr) {
        return {};
    }

    std::vector<int> uncovered;
    for (const auto& [line_number, line] : file->lines) {
        if (!line.excluded && line.hit_count == 0 &&
            (line.kind == CoverageLineKind::Executable || line.kind == CoverageLineKind::Unknown)) {
            uncovered.push_back(line_number);
        }
    }
    std::sort(uncovered.begin(), uncovered.end());
    return uncovered;
}

std::vector<std::string> CoverageTable::never_hit_functions(const std::string& path) const {
    const auto* file = find_file(path);
    if (file == nullptr) {
        return {};
    }

    std::vector<std::string> names;
    for (const auto& [name, fn] : file->functions) {
        if (!fn.excluded && fn.hit_count == 0) {
            names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

CoverageTable load_coverage_table(const std::filesystem::path& path) {
    const auto extension = path.extension().string();
    if (extension == ".msgpack") {
        return load_coverage_table_from_msgpack(path);
    }
    if (extension == ".xml") {
        return load_coverage_table_from_cobertura(path);
    }
    if (extension == ".info" || extension == ".lcov") {
        return load_coverage_table_from_lcov(path);
    }

    const auto content = read_file_text(path);
    if (looks_like_cobertura(content)) {
        return load_coverage_table_from_cobertura(path);
    }
    if (looks_like_lcov(content)) {
        return load_coverage_table_from_lcov(path);
    }

    throw std::runtime_error("unsupported coverage format: " + path.string());
}

CoverageThresholdResult check_coverage_thresholds(const CoverageTable& table,
                                                  const CoverageThresholds& thresholds) {
    CoverageThresholdResult result;
    result.passed = true;

    if (thresholds.min_line_rate.has_value()) {
        const double actual = table.line_rate();
        if (actual + 1e-9 < *thresholds.min_line_rate) {
            result.passed = false;
            result.failures.push_back("line_rate " + std::to_string(actual) + " < " +
                                      std::to_string(*thresholds.min_line_rate));
        }
    }
    if (thresholds.min_branch_rate.has_value()) {
        const double actual = table.branch_rate();
        if (actual + 1e-9 < *thresholds.min_branch_rate) {
            result.passed = false;
            result.failures.push_back("branch_rate " + std::to_string(actual) + " < " +
                                      std::to_string(*thresholds.min_branch_rate));
        }
    }
    if (thresholds.min_function_rate.has_value()) {
        const double actual = table.function_rate();
        if (actual + 1e-9 < *thresholds.min_function_rate) {
            result.passed = false;
            result.failures.push_back("function_rate " + std::to_string(actual) + " < " +
                                      std::to_string(*thresholds.min_function_rate));
        }
    }

    return result;
}

CoverageDiff diff_coverage_tables(const CoverageTable& before, const CoverageTable& after) {
    CoverageDiff diff;
    diff.line_rate_before = before.line_rate();
    diff.line_rate_after = after.line_rate();
    diff.branch_rate_before = before.branch_rate();
    diff.branch_rate_after = after.branch_rate();
    diff.function_rate_before = before.function_rate();
    diff.function_rate_after = after.function_rate();

    std::map<std::string, std::set<int>> paths;
    for (const auto& [path, file] : before.files) {
        for (const auto& [line_number, _] : file.lines) {
            paths[path].insert(line_number);
        }
    }
    for (const auto& [path, file] : after.files) {
        for (const auto& [line_number, _] : file.lines) {
            paths[path].insert(line_number);
        }
    }

    for (const auto& [path, line_numbers] : paths) {
        const auto* before_file = before.find_file(path);
        const auto* after_file = after.find_file(path);
        for (const int line_number : line_numbers) {
            const std::uint64_t before_hits =
                before_file != nullptr && before_file->find_line(line_number) != nullptr
                    ? before_file->find_line(line_number)->hit_count
                    : 0;
            const std::uint64_t after_hits =
                after_file != nullptr && after_file->find_line(line_number) != nullptr
                    ? after_file->find_line(line_number)->hit_count
                    : 0;
            if (before_hits != after_hits) {
                diff.line_changes.push_back(
                    CoverageDiffLineChange{path, line_number, before_hits, after_hits});
            }
        }
    }

    return diff;
}

std::string coverage_table_to_json_string(const CoverageTable& table) {
    return table_to_json(table, true).dump(2);
}

nlohmann::json coverage_table_to_event(const CoverageTable& table) {
    return nlohmann::json{
        {"event", "coverage"},
        {"line_rate", table.line_rate()},
        {"branch_rate", table.branch_rate()},
        {"function_rate", table.function_rate()},
        {"lines_hit", table.total_lines_hit()},
        {"lines_found", table.total_lines_found()},
        {"branches_hit", table.total_branches_hit()},
        {"branches_found", table.total_branches_found()},
        {"functions_hit", table.total_functions_hit()},
        {"functions_found", table.total_functions_found()},
    };
}

} // namespace teez::core
