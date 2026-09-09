#include "teez/core/test_codec.hpp"

#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace teez::core {

namespace {

std::optional<std::string> extract_xml_attribute(const std::string& tag, const std::string& name) {
    std::size_t search_pos = 0;
    while (search_pos < tag.size()) {
        const auto attr_pos = tag.find(name, search_pos);
        if (attr_pos == std::string::npos) {
            return std::nullopt;
        }

        if (attr_pos > 0) {
            const char previous = tag[attr_pos - 1];
            if (previous != ' ' && previous != '\t' && previous != '\n' && previous != '<') {
                search_pos = attr_pos + name.size();
                continue;
            }
        }

        const auto equals_pos = attr_pos + name.size();
        if (equals_pos >= tag.size() || tag[equals_pos] != '=' || tag[equals_pos + 1] != '"') {
            search_pos = attr_pos + name.size();
            continue;
        }

        const auto value_start = equals_pos + 2;
        const auto value_end = tag.find('"', value_start);
        if (value_end == std::string::npos) {
            return std::nullopt;
        }
        return tag.substr(value_start, value_end - value_start);
    }

    return std::nullopt;
}

std::optional<double> parse_double_attribute(const std::string& tag, const std::string& name) {
    const auto value = extract_xml_attribute(tag, name);
    if (!value.has_value()) {
        return std::nullopt;
    }
    return std::stod(*value);
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

std::string xml_unescape(const std::string& text) {
    std::string unescaped;
    unescaped.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != '&') {
            unescaped += text[index];
            continue;
        }

        const auto semicolon = text.find(';', index);
        if (semicolon == std::string::npos) {
            unescaped += text[index];
            continue;
        }

        const std::string entity = text.substr(index, semicolon - index + 1);
        if (entity == "&amp;") {
            unescaped += '&';
        } else if (entity == "&quot;") {
            unescaped += '"';
        } else if (entity == "&apos;") {
            unescaped += '\'';
        } else if (entity == "&lt;") {
            unescaped += '<';
        } else if (entity == "&gt;") {
            unescaped += '>';
        } else {
            unescaped += entity;
        }
        index = semicolon;
    }
    return unescaped;
}

std::pair<std::string, std::string> split_test_id(const std::string& id) {
    if (const auto pos = id.rfind("::"); pos != std::string::npos) {
        return {id.substr(0, pos), id.substr(pos + 2)};
    }
    return {"teez", id};
}

std::string join_lines(const std::vector<std::string>& lines) {
    std::ostringstream joined;
    for (const auto& line : lines) {
        joined << line;
    }
    return joined.str();
}

std::string failure_body(const TestCaseResult& test_case) {
    std::ostringstream body;
    if (test_case.failure.has_value()) {
        if (test_case.failure->stacktrace.has_value()) {
            body << *test_case.failure->stacktrace;
        }
        if (test_case.failure->message.has_value()) {
            if (!body.str().empty()) {
                body << '\n';
            }
            body << *test_case.failure->message;
        }
    }
    for (const auto& line : test_case.stderr_lines) {
        if (!body.str().empty()) {
            body << '\n';
        }
        body << line;
    }
    return body.str();
}

std::string build_test_case_id(const std::optional<std::string>& classname,
                               const std::string& name) {
    if (classname.has_value() && !classname->empty() && !name.empty()) {
        return *classname + "::" + name;
    }
    return name.empty() ? classname.value_or("") : name;
}

TestSuiteReport& ensure_current_suite(TestRunReport& report, TestSuiteReport** current_suite) {
    if (*current_suite != nullptr) {
        return **current_suite;
    }

    TestSuiteReport suite;
    suite.id = ".";
    suite.name = ".";
    report.suites.push_back(std::move(suite));
    *current_suite = &report.suites.back();
    return **current_suite;
}

bool tag_is_self_closing(const std::string& tag) {
    return tag.size() >= 3 && tag[tag.size() - 2] == '/';
}

std::optional<std::string> read_element_text(const std::string& content, std::size_t& pos,
                                             const std::string& tag_name) {
    const std::string close = "</" + tag_name + ">";
    const auto close_pos = content.find(close, pos);
    if (close_pos == std::string::npos) {
        return std::nullopt;
    }

    const std::string text = content.substr(pos, close_pos - pos);
    pos = close_pos + close.size();
    return xml_unescape(text);
}

void append_stream_lines(std::vector<std::string>& lines, const std::string& text) {
    if (text.empty()) {
        return;
    }
    lines.push_back(text);
}

TestRunReport parse_junit_xml(const std::string& content) {
    TestRunReport report;
    TestSuiteReport* current_suite = nullptr;
    TestCaseResult* current_case = nullptr;

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

        if (tag.rfind("<?", 0) == 0 || tag.rfind("<!--", 0) == 0) {
            continue;
        }

        if (tag.rfind("<testsuite ", 0) == 0 || tag == "<testsuite>") {
            TestSuiteReport suite;
            suite.name = extract_xml_attribute(tag, "name").value_or(".");
            suite.id = suite.name;
            if (const auto path = extract_xml_attribute(tag, "filepath"); path.has_value()) {
                suite.path = *path;
            }
            suite.duration_seconds = parse_double_attribute(tag, "time");
            report.suites.push_back(std::move(suite));
            current_suite = &report.suites.back();
            current_case = nullptr;
            continue;
        }

        if (tag.rfind("<testcase ", 0) == 0 || tag == "<testcase>") {
            auto& suite = ensure_current_suite(report, &current_suite);
            TestCaseResult test_case;
            test_case.classname = extract_xml_attribute(tag, "classname");
            test_case.name = extract_xml_attribute(tag, "name").value_or("");
            test_case.duration_seconds = parse_double_attribute(tag, "time");
            test_case.id = build_test_case_id(test_case.classname, test_case.name);
            test_case.apply_identity(parse_test_case_id(test_case.id));
            if (const auto file = extract_xml_attribute(tag, "file"); file.has_value()) {
                test_case.file = *file;
            }
            suite.cases.push_back(std::move(test_case));
            current_case = &suite.cases.back();
            continue;
        }

        if (current_case == nullptr) {
            continue;
        }

        if (tag.rfind("<failure", 0) == 0) {
            current_case->status = TestStatus::Failed;
            TestFailureInfo failure;
            failure.message = extract_xml_attribute(tag, "message");
            if (tag_is_self_closing(tag)) {
                current_case->failure = failure;
                continue;
            }
            if (const auto body = read_element_text(content, pos, "failure")) {
                if (!failure.message.has_value()) {
                    failure.message = *body;
                } else if (!body->empty()) {
                    failure.stacktrace = *body;
                }
            }
            current_case->failure = failure;
            continue;
        }

        if (tag.rfind("<error", 0) == 0) {
            current_case->status = TestStatus::Error;
            TestFailureInfo failure;
            failure.message = extract_xml_attribute(tag, "message");
            if (!tag_is_self_closing(tag)) {
                if (const auto body = read_element_text(content, pos, "error")) {
                    if (!failure.message.has_value()) {
                        failure.message = *body;
                    } else if (!body->empty()) {
                        failure.stacktrace = *body;
                    }
                }
            }
            current_case->failure = failure;
            continue;
        }

        if (tag.rfind("<skipped", 0) == 0) {
            current_case->status = TestStatus::Skipped;
            TestSkipInfo skip;
            skip.message = extract_xml_attribute(tag, "message");
            current_case->skip = skip;
            continue;
        }

        if (tag.rfind("<system-out", 0) == 0) {
            if (!tag_is_self_closing(tag)) {
                if (const auto body = read_element_text(content, pos, "system-out")) {
                    append_stream_lines(current_case->stdout_lines, *body);
                }
            }
            continue;
        }

        if (tag.rfind("<system-err", 0) == 0) {
            if (!tag_is_self_closing(tag)) {
                if (const auto body = read_element_text(content, pos, "system-err")) {
                    append_stream_lines(current_case->stderr_lines, *body);
                }
            }
            continue;
        }

        if (tag == "</testcase>") {
            if (current_case->status == TestStatus::Unknown) {
                current_case->status = TestStatus::Passed;
            }
            current_case = nullptr;
        }
    }

    for (auto& suite : report.suites) {
        for (auto& test_case : suite.cases) {
            if (test_case.status == TestStatus::Unknown) {
                test_case.status = TestStatus::Passed;
            }
        }
    }

    return report;
}

} // namespace

std::string test_report_to_junit_xml(const TestRunReport& report) {
    std::ostringstream out;
    const auto summary = report.summary();
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<testsuites tests=\"" << summary.total() << "\" failures=\"" << summary.failed
        << "\" skipped=\"" << summary.skipped << "\" errors=\"" << summary.errors << "\">\n";

    for (const auto& suite : report.suites) {
        const auto suite_summary = suite.summary();
        out << "  <testsuite name=\"" << xml_escape(suite.name) << "\" tests=\""
            << suite_summary.tests << "\" failures=\"" << suite_summary.failed << "\" skipped=\""
            << suite_summary.skipped << "\" errors=\"" << suite_summary.errors << "\"";
        if (!suite.path.empty()) {
            out << " filepath=\"" << xml_escape(suite.path.string()) << "\"";
        }
        if (suite.duration_seconds.has_value()) {
            out << " time=\"" << *suite.duration_seconds << "\"";
        }
        out << ">\n";

        for (const auto& test_case : suite.cases) {
            const auto [fallback_classname, fallback_name] = split_test_id(test_case.id);
            const std::string& classname =
                test_case.classname.has_value() ? *test_case.classname : fallback_classname;
            const std::string& test_name = test_case.name.empty() ? fallback_name : test_case.name;

            out << "    <testcase classname=\"" << xml_escape(classname) << "\" name=\""
                << xml_escape(test_name) << "\"";
            if (test_case.file.has_value()) {
                out << " file=\"" << xml_escape(*test_case.file) << "\"";
            }
            if (test_case.duration_seconds.has_value()) {
                out << " time=\"" << std::fixed << std::setprecision(6)
                    << *test_case.duration_seconds << "\"";
            }
            out << ">";

            if (test_case.status == TestStatus::Failed || test_case.status == TestStatus::Error) {
                const std::string message =
                    test_case.failure.has_value() && test_case.failure->message.has_value()
                        ? *test_case.failure->message
                        : "test failed";
                const std::string tag = test_case.status == TestStatus::Error ? "error" : "failure";
                out << "\n      <" << tag << " message=\"" << xml_escape(message) << "\">"
                    << xml_escape(failure_body(test_case)) << "</" << tag << ">";
            } else if (test_case.status == TestStatus::Skipped ||
                       test_case.status == TestStatus::Todo) {
                if (test_case.skip.has_value() && test_case.skip->message.has_value()) {
                    out << "\n      <skipped message=\"" << xml_escape(*test_case.skip->message)
                        << "\"/>";
                } else {
                    out << "\n      <skipped/>";
                }
            }

            const auto stdout_text = join_lines(test_case.stdout_lines);
            if (!stdout_text.empty()) {
                out << "\n      <system-out>" << xml_escape(stdout_text) << "</system-out>";
            }
            const auto stderr_text = join_lines(test_case.stderr_lines);
            if (!stderr_text.empty()) {
                out << "\n      <system-err>" << xml_escape(stderr_text) << "</system-err>";
            }

            out << "\n    </testcase>\n";
        }

        out << "  </testsuite>\n";
    }

    out << "</testsuites>\n";
    return out.str();
}

TestRunReport test_report_from_junit_xml(const std::string& content) {
    return parse_junit_xml(content);
}

} // namespace teez::core
