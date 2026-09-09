#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "teez/core/test_report.hpp"

namespace teez::core {

/// Bidirectional codec between TestRunReport and an external test-report format.
class TestReportCodec {
  public:
    virtual ~TestReportCodec() = default;

    virtual std::string name() const = 0;
    virtual std::string default_extension() const = 0;

    virtual std::string encode(const TestRunReport& report) const = 0;
    virtual TestRunReport decode(const std::string& content) const = 0;

    virtual bool is_binary() const {
        return false;
    }

    void write(const TestRunReport& report, const std::filesystem::path& path) const;
    TestRunReport read(const std::filesystem::path& path) const;
};

using TestReporter = TestReportCodec;

std::vector<std::string> test_report_codec_names();
bool is_test_report_codec_name(const std::string& name);

const TestReportCodec& test_report_codec_by_name(const std::string& name);
std::unique_ptr<TestReportCodec> make_test_report_codec(const std::string& name);

std::string encode_test_report(const TestRunReport& report, const std::string& codec_name);
TestRunReport decode_test_report(const std::string& content, const std::string& codec_name);

void write_test_report(const TestRunReport& report, const std::string& codec_name,
                       const std::filesystem::path& path);
TestRunReport read_test_report(const std::filesystem::path& path, const std::string& codec_name);

// Format-specific transformers (canonical model <-> serialized form).
std::string test_report_to_json_string(const TestRunReport& report);
TestRunReport test_report_from_json_string(const std::string& content);

std::string test_report_to_junit_xml(const TestRunReport& report);
TestRunReport test_report_from_junit_xml(const std::string& content);

std::string test_report_to_msgpack_bytes(const TestRunReport& report);
TestRunReport test_report_from_msgpack_bytes(const std::string& content);

std::string test_report_to_zstd_msgpack_bytes(const TestRunReport& report);
TestRunReport test_report_from_zstd_msgpack_bytes(const std::string& content);

// Backward-compatible aliases for the reporter registry.
inline std::vector<std::string> test_reporter_names() {
    return test_report_codec_names();
}

inline bool is_test_reporter_name(const std::string& name) {
    return is_test_report_codec_name(name);
}

inline const TestReporter& test_reporter_by_name(const std::string& name) {
    return test_report_codec_by_name(name);
}

inline std::unique_ptr<TestReporter> make_test_reporter(const std::string& name) {
    return make_test_report_codec(name);
}

} // namespace teez::core
