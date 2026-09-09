#include "teez/core/coverage_reporter.hpp"

#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>

#include "teez/core/coverage_msgpack.hpp"

namespace teez::core {

namespace {

std::optional<std::string> json_string_at(const nlohmann::json& data,
                                          const std::vector<std::string>& path) {
    const nlohmann::json* current = &data;
    for (const auto& segment : path) {
        if (!current->is_object() || !current->contains(segment)) {
            return std::nullopt;
        }
        current = &current->at(segment);
    }
    if (!current->is_string()) {
        return std::nullopt;
    }
    return current->get<std::string>();
}

std::optional<double> json_number_at(const nlohmann::json& data,
                                     const std::vector<std::string>& path) {
    const nlohmann::json* current = &data;
    for (const auto& segment : path) {
        if (!current->is_object() || !current->contains(segment)) {
            return std::nullopt;
        }
        current = &current->at(segment);
    }
    if (!current->is_number()) {
        return std::nullopt;
    }
    return current->get<double>();
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

class JsonCoverageReporter final : public CoverageReporter {
  public:
    std::string name() const override {
        return "json";
    }
    std::string default_extension() const override {
        return ".json";
    }

    void write(const CoverageTable& table, const std::filesystem::path& path) const override {
        std::ofstream out(path);
        if (!out.is_open()) {
            throw std::runtime_error("failed to write coverage json: " + path.string());
        }
        out << coverage_table_to_json_string(table);
    }
};

class LcovCoverageReporter final : public CoverageReporter {
  public:
    std::string name() const override {
        return "lcov";
    }
    std::string default_extension() const override {
        return ".lcov";
    }

    void write(const CoverageTable& table, const std::filesystem::path& path) const override {
        export_coverage_table_to_lcov(table, path);
    }
};

class CoberturaCoverageReporter final : public CoverageReporter {
  public:
    std::string name() const override {
        return "cobertura";
    }
    std::string default_extension() const override {
        return ".xml";
    }

    void write(const CoverageTable& table, const std::filesystem::path& path) const override {
        export_coverage_table_to_cobertura(table, path);
    }
};

class JunitCoverageReporter final : public CoverageReporter {
  public:
    explicit JunitCoverageReporter(std::optional<double> min_line_rate)
        : min_line_rate_(std::move(min_line_rate)) {}

    std::string name() const override {
        return "junit";
    }
    std::string default_extension() const override {
        return ".xml";
    }

    void write(const CoverageTable& table, const std::filesystem::path& path) const override {
        std::ofstream out(path);
        if (!out.is_open()) {
            throw std::runtime_error("failed to write coverage junit: " + path.string());
        }

        int failures = 0;
        if (min_line_rate_.has_value() && table.line_rate() + 1e-9 < *min_line_rate_) {
            ++failures;
        }

        for (const auto& [file_path, file] : table.files) {
            if (min_line_rate_.has_value() && file.line_rate() + 1e-9 < *min_line_rate_) {
                ++failures;
            }
        }

        const int tests = static_cast<int>(table.files.size()) + 1;
        out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        out << "<testsuites>\n";
        out << "  <testsuite name=\"coverage\" tests=\"" << tests << "\" failures=\"" << failures
            << "\" time=\"0\">\n";

        for (const auto& [file_path, file] : table.files) {
            out << "    <testcase name=\"" << xml_escape(file_path)
                << "\" classname=\"coverage.file\">\n";
            out << "      <properties>\n";
            out << "        <property name=\"line_rate\" value=\"" << file.line_rate() << "\"/>\n";
            out << "        <property name=\"branch_rate\" value=\"" << file.branch_rate()
                << "\"/>\n";
            out << "        <property name=\"function_rate\" value=\"" << file.function_rate()
                << "\"/>\n";
            out << "        <property name=\"lines_hit\" value=\"" << file.lines_hit() << "\"/>\n";
            out << "        <property name=\"lines_found\" value=\"" << file.lines_found()
                << "\"/>\n";
            out << "      </properties>\n";
            if (min_line_rate_.has_value() && file.line_rate() + 1e-9 < *min_line_rate_) {
                out << "      <failure message=\"line_rate " << file.line_rate()
                    << " below threshold " << *min_line_rate_ << "\"/>\n";
            }
            out << "    </testcase>\n";
        }

        out << "    <testcase name=\"summary\" classname=\"coverage.summary\">\n";
        out << "      <properties>\n";
        out << "        <property name=\"line_rate\" value=\"" << table.line_rate() << "\"/>\n";
        out << "        <property name=\"branch_rate\" value=\"" << table.branch_rate() << "\"/>\n";
        out << "        <property name=\"function_rate\" value=\"" << table.function_rate()
            << "\"/>\n";
        out << "      </properties>\n";
        if (min_line_rate_.has_value() && table.line_rate() + 1e-9 < *min_line_rate_) {
            out << "      <failure message=\"line_rate " << table.line_rate() << " below threshold "
                << *min_line_rate_ << "\"/>\n";
        }
        out << "    </testcase>\n";
        out << "  </testsuite>\n";
        out << "</testsuites>\n";
    }

  private:
    std::optional<double> min_line_rate_;
};

class MsgpackCoverageReporter final : public CoverageReporter {
  public:
    std::string name() const override {
        return "msgpack";
    }
    std::string default_extension() const override {
        return ".msgpack";
    }

    void write(const CoverageTable& table, const std::filesystem::path& path) const override {
        write_coverage_table_to_msgpack(table, path);
    }
};

const std::map<std::string, std::function<std::unique_ptr<CoverageReporter>()>>&
reporter_factories() {
    static const std::map<std::string, std::function<std::unique_ptr<CoverageReporter>()>>
        factories = {
            {"json", [] { return std::make_unique<JsonCoverageReporter>(); }},
            {"msgpack", [] { return std::make_unique<MsgpackCoverageReporter>(); }},
            {"lcov", [] { return std::make_unique<LcovCoverageReporter>(); }},
            {"cobertura", [] { return std::make_unique<CoberturaCoverageReporter>(); }},
            {"junit", [] { return std::make_unique<JunitCoverageReporter>(std::nullopt); }},
        };
    return factories;
}

std::filesystem::path default_output_path(const std::string& reporter_name) {
    const auto reporter = make_coverage_reporter(reporter_name);
    return std::filesystem::path("coverage" + reporter->default_extension());
}

} // namespace

std::vector<std::string> coverage_reporter_names() {
    std::vector<std::string> names;
    for (const auto& [name, _] : reporter_factories()) {
        names.push_back(name);
    }
    return names;
}

bool is_coverage_reporter_name(const std::string& name) {
    return reporter_factories().contains(name);
}

const CoverageReporter& coverage_reporter_by_name(const std::string& name) {
    static thread_local std::map<std::string, std::unique_ptr<CoverageReporter>> cache;
    if (!cache.contains(name)) {
        cache[name] = make_coverage_reporter(name);
    }
    return *cache.at(name);
}

std::unique_ptr<CoverageReporter> make_coverage_reporter(const std::string& name) {
    const auto& factories = reporter_factories();
    const auto it = factories.find(name);
    if (it == factories.end()) {
        throw std::runtime_error("unsupported coverage reporter: " + name);
    }
    return it->second();
}

void write_coverage_report(const CoverageTable& table, const std::string& reporter_name,
                           const std::filesystem::path& path) {
    if (reporter_name == "junit") {
        JunitCoverageReporter reporter(std::nullopt);
        reporter.write(table, path);
        return;
    }
    coverage_reporter_by_name(reporter_name).write(table, path);
}

CoverageReportOptions resolve_coverage_report_options(const nlohmann::json& config_data,
                                                      const CoverageReportCliOverrides& cli) {
    CoverageReportOptions resolved;

    if (const auto reporter = json_string_at(config_data, {"coverage", "reporter"});
        reporter.has_value()) {
        resolved.reporter = *reporter;
    }
    if (const auto input = json_string_at(config_data, {"coverage", "input"}); input.has_value()) {
        resolved.input = *input;
    }
    if (const auto output = json_string_at(config_data, {"coverage", "output"});
        output.has_value()) {
        resolved.output = *output;
    }
    if (const auto min_line_rate = json_number_at(config_data, {"coverage", "min_line_rate"});
        min_line_rate.has_value()) {
        resolved.min_line_rate = *min_line_rate;
    }

    if (cli.reporter.has_value()) {
        resolved.reporter = *cli.reporter;
    }
    if (cli.input.has_value()) {
        resolved.input = cli.input;
    }
    if (cli.output.has_value()) {
        resolved.output = cli.output;
    }
    if (cli.min_line_rate.has_value()) {
        resolved.min_line_rate = cli.min_line_rate;
    }

    if (!is_coverage_reporter_name(resolved.reporter)) {
        throw std::runtime_error("unsupported coverage reporter: " + resolved.reporter);
    }

    if (!resolved.output.has_value()) {
        resolved.output = default_output_path(resolved.reporter);
    }

    return resolved;
}

void export_coverage_report(const CoverageReportOptions& options) {
    if (!options.input.has_value()) {
        throw std::runtime_error("coverage input path is required");
    }
    if (!options.output.has_value()) {
        throw std::runtime_error("coverage output path is required");
    }

    if (const auto parent = options.output->parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    const auto table = load_coverage_table(*options.input);
    if (options.reporter == "junit") {
        JunitCoverageReporter reporter(options.min_line_rate);
        reporter.write(table, *options.output);
        return;
    }
    coverage_reporter_by_name(options.reporter).write(table, *options.output);
}

} // namespace teez::core
