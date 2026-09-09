#include "teez/core/test_report_msgpack.hpp"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "teez/core/compression.hpp"
#include "teez/core/test_report.hpp"

namespace teez::core {

namespace {

constexpr const char* kTestReportMsgpackFormat = "teez.test-report";
constexpr int kTestReportMsgpackVersion = 1;

std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open test report file: " + path.string());
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void write_binary_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& data) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to write test report file: " + path.string());
    }
    file.write(reinterpret_cast<const char*>(data.data()),
               static_cast<std::streamsize>(data.size()));
}

std::vector<std::uint8_t> bytes_from_string(const std::string& content) {
    return {content.begin(), content.end()};
}

std::string string_from_bytes(const std::vector<std::uint8_t>& data) {
    return {reinterpret_cast<const char*>(data.data()), data.size()};
}

nlohmann::json envelope_to_json(const TestRunReport& report) {
    return {
        {"format", kTestReportMsgpackFormat},
        {"version", kTestReportMsgpackVersion},
        {"report", test_run_report_to_json(report)},
    };
}

TestRunReport report_from_envelope(const nlohmann::json& envelope) {
    if (!envelope.contains("format") || envelope.at("format") != kTestReportMsgpackFormat) {
        throw std::runtime_error("invalid test report msgpack format marker");
    }
    if (!envelope.contains("version") ||
        envelope.at("version").get<int>() != kTestReportMsgpackVersion) {
        throw std::runtime_error("unsupported test report msgpack version");
    }
    return test_run_report_from_json(envelope.at("report"));
}

} // namespace

std::vector<std::uint8_t> test_report_to_msgpack(const TestRunReport& report) {
    return nlohmann::json::to_msgpack(envelope_to_json(report));
}

TestRunReport test_report_from_msgpack(const std::vector<std::uint8_t>& data) {
    const auto envelope = nlohmann::json::from_msgpack(data);
    return report_from_envelope(envelope);
}

std::vector<std::uint8_t> test_report_to_zstd_msgpack(const TestRunReport& report) {
    return zstd_compress(test_report_to_msgpack(report));
}

TestRunReport test_report_from_zstd_msgpack(const std::vector<std::uint8_t>& data) {
    return test_report_from_msgpack(zstd_decompress(data));
}

void write_test_report_msgpack(const TestRunReport& report, const std::filesystem::path& path) {
    write_binary_file(path, test_report_to_msgpack(report));
}

TestRunReport load_test_report_msgpack(const std::filesystem::path& path) {
    return test_report_from_msgpack(read_binary_file(path));
}

void write_test_report_zstd_msgpack(const TestRunReport& report,
                                    const std::filesystem::path& path) {
    write_binary_file(path, test_report_to_zstd_msgpack(report));
}

TestRunReport load_test_report_zstd_msgpack(const std::filesystem::path& path) {
    return test_report_from_zstd_msgpack(read_binary_file(path));
}

std::string test_report_to_msgpack_bytes(const TestRunReport& report) {
    return string_from_bytes(test_report_to_msgpack(report));
}

TestRunReport test_report_from_msgpack_bytes(const std::string& content) {
    return test_report_from_msgpack(bytes_from_string(content));
}

std::string test_report_to_zstd_msgpack_bytes(const TestRunReport& report) {
    return string_from_bytes(test_report_to_zstd_msgpack(report));
}

TestRunReport test_report_from_zstd_msgpack_bytes(const std::string& content) {
    return test_report_from_zstd_msgpack(bytes_from_string(content));
}

} // namespace teez::core
