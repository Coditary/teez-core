#include "teez/core/coverage.hpp"

#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace teez::core {

namespace {

std::optional<std::string> extract_xml_attribute(const std::string& tag, const std::string& name) {
    const std::string needle = name + "=\"";
    const auto start = tag.find(needle);
    if (start == std::string::npos) {
        return std::nullopt;
    }

    const auto value_start = start + needle.size();
    const auto value_end = tag.find('"', value_start);
    if (value_end == std::string::npos) {
        return std::nullopt;
    }
    return tag.substr(value_start, value_end - value_start);
}

std::optional<double> parse_rate_attribute(const std::string& tag, const std::string& name) {
    const auto value = extract_xml_attribute(tag, name);
    if (!value.has_value()) {
        return std::nullopt;
    }
    return std::stod(*value);
}

std::optional<std::int64_t> parse_int64_attribute(const std::string& tag, const std::string& name) {
    const auto value = extract_xml_attribute(tag, name);
    if (!value.has_value()) {
        return std::nullopt;
    }
    return std::stoll(*value);
}

CoverageLineKind kind_from_hits(std::uint64_t hits, bool excluded) {
    if (excluded) {
        return CoverageLineKind::NonExecutable;
    }
    return hits > 0 ? CoverageLineKind::Covered : CoverageLineKind::Executable;
}

CoverageLineStatus status_from_hits(std::uint64_t hits, bool excluded) {
    if (excluded) {
        return CoverageLineStatus::Unknown;
    }
    return hits > 0 ? CoverageLineStatus::Covered : CoverageLineStatus::Uncovered;
}

std::optional<std::uint64_t> parse_percentage_hits(const std::string& text) {
    const auto open = text.find('(');
    const auto slash = text.find('/', open == std::string::npos ? 0 : open);
    if (open == std::string::npos || slash == std::string::npos) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(std::stoull(text.substr(open + 1, slash - open - 1)));
}

void attach_condition(CoverageLine& line, const std::string& tag) {
    if (line.branches.empty()) {
        CoverageBranch branch_entry;
        branch_entry.line_number = line.line_number;
        branch_entry.taken = line.hit_count > 0;
        branch_entry.taken_count = line.hit_count;
        line.branches.push_back(branch_entry);
    }

    CoverageCondition condition;
    if (const auto number = extract_xml_attribute(tag, "number"); number.has_value()) {
        condition.index = std::stoi(*number);
    }
    condition.type = extract_xml_attribute(tag, "type");
    if (const auto coverage = extract_xml_attribute(tag, "coverage"); coverage.has_value()) {
        condition.coverage_text = coverage;
        if (coverage->find('%') != std::string::npos) {
            if (const auto hits = parse_percentage_hits(*coverage); hits.has_value()) {
                condition.taken_count = *hits;
                condition.taken = *hits > 0;
            }
        } else if (*coverage == "100%" || *coverage == "100") {
            condition.taken = true;
            condition.taken_count = 1;
        } else if (*coverage == "0%" || *coverage == "0") {
            condition.taken = false;
            condition.taken_count = 0;
        }
    }
    line.branches.back().conditions.push_back(condition);
}

void parse_cobertura_content(const std::string& content, CoverageTable& table) {
    CoverageFile* current_file = nullptr;
    CoverageLine* current_line = nullptr;
    std::optional<std::string> current_package;

    std::size_t pos = 0;
    while (pos < content.size()) {
        const auto tag_start = content.find('<', pos);
        if (tag_start == std::string::npos) {
            break;
        }

        const auto tag_end = content.find('>', tag_start);
        if (tag_end == std::string::npos) {
            break;
        }

        const std::string tag = content.substr(tag_start, tag_end - tag_start + 1);
        pos = tag_end + 1;

        if (tag.rfind("<coverage ", 0) == 0) {
            table.meta.line_rate_reported = parse_rate_attribute(tag, "line-rate");
            table.meta.branch_rate_reported = parse_rate_attribute(tag, "branch-rate");
            table.meta.version = extract_xml_attribute(tag, "version");
            table.meta.timestamp = parse_int64_attribute(tag, "timestamp");
            continue;
        }

        if (tag.rfind("<source>", 0) == 0) {
            const auto close = content.find("</source>", pos);
            if (close == std::string::npos) {
                continue;
            }
            const std::string root = content.substr(pos, close - pos);
            table.add_source_root(root);
            pos = close + 9;
            continue;
        }

        if (tag.rfind("<package ", 0) == 0) {
            current_package = extract_xml_attribute(tag, "name");
            continue;
        }

        if (tag.rfind("<class ", 0) == 0) {
            const auto filename = extract_xml_attribute(tag, "filename");
            if (filename.has_value()) {
                current_file = &table.upsert_file(*filename);
                current_file->class_name = extract_xml_attribute(tag, "name");
                current_file->package_name = current_package;
                current_file->line_rate_reported = parse_rate_attribute(tag, "line-rate");
                current_file->branch_rate_reported = parse_rate_attribute(tag, "branch-rate");
            }
            current_line = nullptr;
            continue;
        }

        if (tag.rfind("<method ", 0) == 0 && current_file != nullptr) {
            const auto name = extract_xml_attribute(tag, "name");
            if (!name.has_value()) {
                continue;
            }

            auto& fn = current_file->functions[*name];
            fn.name = *name;

            if (const auto line_number = extract_xml_attribute(tag, "line");
                line_number.has_value()) {
                fn.start_line = std::stoi(*line_number);
            }
            if (const auto hits = extract_xml_attribute(tag, "hits"); hits.has_value()) {
                fn.hit_count = static_cast<std::uint64_t>(std::stoull(*hits));
            }
            fn.signature = extract_xml_attribute(tag, "signature");
            continue;
        }

        if (tag.rfind("<line ", 0) == 0 && current_file != nullptr) {
            const auto number = extract_xml_attribute(tag, "number");
            const auto hits = extract_xml_attribute(tag, "hits");
            if (!number.has_value() || !hits.has_value()) {
                continue;
            }

            const int line_number = std::stoi(*number);
            const auto hit_count = static_cast<std::uint64_t>(std::stoull(*hits));

            auto& entry = current_file->lines[line_number];
            entry.line_number = line_number;
            entry.hit_count = hit_count;
            entry.status = status_from_hits(hit_count, entry.excluded);
            entry.kind = kind_from_hits(hit_count, entry.excluded);
            current_line = &entry;

            const auto branch_flag = extract_xml_attribute(tag, "branch");
            if (branch_flag.has_value() && (*branch_flag == "true" || *branch_flag == "1")) {
                CoverageBranch branch_entry;
                branch_entry.line_number = line_number;
                branch_entry.taken = hit_count > 0;
                branch_entry.taken_count = hit_count;
                branch_entry.condition_coverage = extract_xml_attribute(tag, "condition-coverage");
                entry.branches.push_back(branch_entry);
            }
            continue;
        }

        if (tag.rfind("<condition ", 0) == 0 && current_line != nullptr) {
            attach_condition(*current_line, tag);
        }
    }
}

std::string xml_escape(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char ch : text) {
        switch (ch) {
        case '&':
            escaped += "&amp;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        default:
            escaped += ch;
            break;
        }
    }
    return escaped;
}

} // namespace

CoverageTable load_coverage_table_from_cobertura(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open Cobertura file: " + path.string());
    }

    CoverageTable table;
    table.source_format = CoverageSourceFormat::Cobertura;
    table.meta.source_report_path = path.string();

    const std::string content((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    parse_cobertura_content(content, table);
    table.infer_line_kinds();
    return table;
}

void export_coverage_table_to_cobertura(const CoverageTable& table,
                                        const std::filesystem::path& path) {
    std::ofstream out(path);
    if (!out.is_open()) {
        throw std::runtime_error("failed to write Cobertura file: " + path.string());
    }

    out << "<?xml version=\"1.0\" ?>\n";
    out << "<coverage line-rate=\"" << table.line_rate() << "\" branch-rate=\""
        << table.branch_rate() << "\"";
    if (table.meta.version.has_value()) {
        out << " version=\"" << xml_escape(*table.meta.version) << "\"";
    }
    if (table.meta.timestamp.has_value()) {
        out << " timestamp=\"" << *table.meta.timestamp << "\"";
    }
    out << ">\n";

    if (!table.source_roots.empty()) {
        out << "  <sources>\n";
        for (const auto& root : table.source_roots) {
            out << "    <source>" << xml_escape(root) << "</source>\n";
        }
        out << "  </sources>\n";
    }

    out << "  <packages>\n";
    out << "    <package name=\"coverage\" line-rate=\"" << table.line_rate() << "\" branch-rate=\""
        << table.branch_rate() << "\">\n";
    out << "      <classes>\n";

    for (const auto& [_, file] : table.files) {
        out << "        <class name=\"" << xml_escape(file.class_name.value_or(file.path))
            << "\" filename=\"" << xml_escape(file.path) << "\" line-rate=\"" << file.line_rate()
            << "\" branch-rate=\"" << file.branch_rate() << "\">\n";
        out << "          <methods>\n";
        for (const auto& [name, fn] : file.functions) {
            if (fn.excluded) {
                continue;
            }
            out << "            <method name=\"" << xml_escape(name) << "\" signature=\""
                << xml_escape(fn.signature.value_or("()")) << "\" hits=\"" << fn.hit_count
                << "\" line=\"" << fn.start_line << "\"/>\n";
        }
        out << "          </methods>\n";
        out << "          <lines>\n";
        for (const auto& [line_number, line] : file.lines) {
            if (line.excluded) {
                continue;
            }
            out << "            <line number=\"" << line_number << "\" hits=\"" << line.hit_count
                << "\" branch=\"" << (line.branches.empty() ? "false" : "true") << "\"";
            if (!line.branches.empty() && line.branches.front().condition_coverage.has_value()) {
                out << " condition-coverage=\""
                    << xml_escape(*line.branches.front().condition_coverage) << "\"";
            }
            out << "/>\n";
        }
        out << "          </lines>\n";
        out << "        </class>\n";
    }

    out << "      </classes>\n";
    out << "    </package>\n";
    out << "  </packages>\n";
    out << "</coverage>\n";
}

} // namespace teez::core
