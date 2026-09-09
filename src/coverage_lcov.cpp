#include "teez/core/coverage.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace teez::core {

namespace {

struct LcovRecord {
    std::optional<std::string> test_name;
    std::optional<std::string> source_file;
    std::map<std::string, CoverageFunction> functions;
    std::map<int, CoverageLine> lines;
    std::optional<int> lines_found_reported;
    std::optional<int> lines_hit_reported;
    std::optional<int> branches_found_reported;
    std::optional<int> branches_hit_reported;
    std::optional<int> functions_found_reported;
    std::optional<int> functions_hit_reported;
};

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

void apply_record_to_table(const LcovRecord& record, CoverageTable& table) {
    if (!record.source_file.has_value()) {
        return;
    }

    auto& file = table.upsert_file(*record.source_file);
    file.lines_found_reported = record.lines_found_reported;
    file.lines_hit_reported = record.lines_hit_reported;
    file.branches_found_reported = record.branches_found_reported;
    file.branches_hit_reported = record.branches_hit_reported;
    file.functions_found_reported = record.functions_found_reported;
    file.functions_hit_reported = record.functions_hit_reported;

    for (const auto& [name, fn] : record.functions) {
        file.functions[name] = fn;
    }

    for (const auto& [line_number, other_line] : record.lines) {
        auto& line = file.lines[line_number];
        line.line_number = line_number;
        line.hit_count = other_line.hit_count;
        line.status = other_line.status;
        line.kind = other_line.kind;
        line.excluded = other_line.excluded;
        line.statement_id = other_line.statement_id;
        line.region = other_line.region;
        line.branches = other_line.branches;
    }
}

void flush_record(LcovRecord& record, CoverageTable& table) {
    if (!record.source_file.has_value()) {
        record = {};
        return;
    }

    CoverageTable slice;
    slice.source_format = CoverageSourceFormat::Lcov;
    if (record.test_name.has_value()) {
        slice.meta.test_name = record.test_name;
    }
    apply_record_to_table(record, slice);

    table.merge(slice);

    if (record.test_name.has_value() && !record.test_name->empty()) {
        table.by_test[*record.test_name].source_format = CoverageSourceFormat::Lcov;
        table.by_test[*record.test_name].meta.test_name = record.test_name;
        table.by_test[*record.test_name].merge(slice);
    }

    record = {};
}

void parse_lcov_content(const std::string& content, CoverageTable& table) {
    std::istringstream input(content);
    std::string line;
    LcovRecord record;

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }

        if (line.rfind("TN:", 0) == 0) {
            const auto name = line.substr(3);
            record.test_name = name.empty() ? std::optional<std::string>{} : std::optional{name};
            continue;
        }

        if (line.rfind("SF:", 0) == 0) {
            record.source_file = line.substr(3);
            continue;
        }

        if (line == "end_of_record") {
            flush_record(record, table);
            continue;
        }

        if (line.rfind("FN:", 0) == 0) {
            const auto payload = line.substr(3);
            const auto comma = payload.find(',');
            if (comma == std::string::npos) {
                continue;
            }
            const int start_line = std::stoi(payload.substr(0, comma));
            const std::string name = payload.substr(comma + 1);
            auto& fn = record.functions[name];
            fn.name = name;
            fn.start_line = start_line;
            continue;
        }

        if (line.rfind("FNL:", 0) == 0) {
            const auto payload = line.substr(4);
            const auto comma = payload.find(',');
            if (comma == std::string::npos) {
                continue;
            }
            const int end_line = std::stoi(payload.substr(0, comma));
            const std::string name = payload.substr(comma + 1);
            auto& fn = record.functions[name];
            fn.name = name;
            fn.end_line = end_line;
            continue;
        }

        if (line.rfind("FNDA:", 0) == 0) {
            const auto payload = line.substr(5);
            const auto comma = payload.find(',');
            if (comma == std::string::npos) {
                continue;
            }
            const auto hits = static_cast<std::uint64_t>(std::stoull(payload.substr(0, comma)));
            const std::string name = payload.substr(comma + 1);
            auto& fn = record.functions[name];
            fn.name = name;
            fn.hit_count += hits;
            continue;
        }

        if (line.rfind("DA:", 0) == 0) {
            const auto payload = line.substr(3);
            const auto comma = payload.find(',');
            if (comma == std::string::npos) {
                continue;
            }

            const int line_number = std::stoi(payload.substr(0, comma));
            const auto hits = static_cast<std::uint64_t>(std::stoull(payload.substr(comma + 1)));

            auto& entry = record.lines[line_number];
            entry.line_number = line_number;
            entry.hit_count = hits;
            entry.status = status_from_hits(hits, entry.excluded);
            entry.kind = kind_from_hits(hits, entry.excluded);
            continue;
        }

        if (line.rfind("BRDA:", 0) == 0) {
            const auto payload = line.substr(5);
            const auto first = payload.find(',');
            const auto second = payload.find(',', first + 1);
            const auto third = payload.find(',', second + 1);
            if (first == std::string::npos || second == std::string::npos ||
                third == std::string::npos) {
                continue;
            }

            const int line_number = std::stoi(payload.substr(0, first));
            const int block = std::stoi(payload.substr(first + 1, second - first - 1));
            const int branch = std::stoi(payload.substr(second + 1, third - second - 1));
            const std::string taken_text = payload.substr(third + 1);

            CoverageBranch branch_entry;
            branch_entry.line_number = line_number;
            branch_entry.block = block;
            branch_entry.branch = branch;
            if (taken_text == "-" || taken_text.empty()) {
                branch_entry.taken = false;
                branch_entry.taken_count = 0;
            } else {
                branch_entry.taken_count = static_cast<std::uint64_t>(std::stoull(taken_text));
                branch_entry.taken = branch_entry.taken_count.value() > 0;
            }

            auto& entry = record.lines[line_number];
            entry.line_number = line_number;
            if (entry.kind == CoverageLineKind::Unknown && !entry.excluded) {
                entry.kind = CoverageLineKind::Executable;
                entry.status = CoverageLineStatus::Uncovered;
            }
            entry.branches.push_back(branch_entry);
            continue;
        }

        if (line.rfind("LF:", 0) == 0) {
            record.lines_found_reported = std::stoi(line.substr(3));
            continue;
        }
        if (line.rfind("LH:", 0) == 0) {
            record.lines_hit_reported = std::stoi(line.substr(3));
            continue;
        }
        if (line.rfind("BRF:", 0) == 0) {
            record.branches_found_reported = std::stoi(line.substr(4));
            continue;
        }
        if (line.rfind("BRH:", 0) == 0) {
            record.branches_hit_reported = std::stoi(line.substr(4));
            continue;
        }
        if (line.rfind("FNF:", 0) == 0) {
            record.functions_found_reported = std::stoi(line.substr(4));
            continue;
        }
        if (line.rfind("FNH:", 0) == 0) {
            record.functions_hit_reported = std::stoi(line.substr(4));
            continue;
        }
    }

    if (record.source_file.has_value()) {
        flush_record(record, table);
    }
}

} // namespace

CoverageTable load_coverage_table_from_lcov(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open LCOV file: " + path.string());
    }

    CoverageTable table;
    table.source_format = CoverageSourceFormat::Lcov;
    table.meta.source_report_path = path.string();

    const std::string content((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    parse_lcov_content(content, table);
    table.infer_line_kinds();
    return table;
}

void export_coverage_table_to_lcov(const CoverageTable& table, const std::filesystem::path& path) {
    std::ofstream out(path);
    if (!out.is_open()) {
        throw std::runtime_error("failed to write LCOV file: " + path.string());
    }

    for (const auto& [_, file] : table.files) {
        out << "SF:" << file.path << '\n';

        for (const auto& [name, fn] : file.functions) {
            if (fn.excluded) {
                continue;
            }
            if (fn.start_line > 0) {
                out << "FN:" << fn.start_line << ',' << name << '\n';
            }
            if (fn.end_line > 0) {
                out << "FNL:" << fn.end_line << ',' << name << '\n';
            }
            out << "FNDA:" << fn.hit_count << ',' << name << '\n';
        }

        for (const auto& [line_number, line] : file.lines) {
            if (line.excluded) {
                continue;
            }
            out << "DA:" << line_number << ',' << line.hit_count << '\n';
            for (const auto& branch : line.branches) {
                out << "BRDA:" << line_number << ',' << branch.block << ',' << branch.branch << ',';
                if (branch.taken_count.has_value()) {
                    out << *branch.taken_count;
                } else {
                    out << '-';
                }
                out << '\n';
            }
        }

        out << "LF:" << file.lines_found() << '\n';
        out << "LH:" << file.lines_hit() << '\n';
        out << "BRF:" << file.branches_found() << '\n';
        out << "BRH:" << file.branches_hit() << '\n';
        out << "FNF:" << file.functions_found() << '\n';
        out << "FNH:" << file.functions_hit() << '\n';
        out << "end_of_record\n";
    }
}

} // namespace teez::core
