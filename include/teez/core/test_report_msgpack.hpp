#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "teez/core/test_report.hpp"

namespace teez::core {

std::vector<std::uint8_t> test_report_to_msgpack(const TestRunReport& report);
TestRunReport test_report_from_msgpack(const std::vector<std::uint8_t>& data);

std::vector<std::uint8_t> test_report_to_zstd_msgpack(const TestRunReport& report);
TestRunReport test_report_from_zstd_msgpack(const std::vector<std::uint8_t>& data);

void write_test_report_msgpack(const TestRunReport& report, const std::filesystem::path& path);
TestRunReport load_test_report_msgpack(const std::filesystem::path& path);

void write_test_report_zstd_msgpack(const TestRunReport& report, const std::filesystem::path& path);
TestRunReport load_test_report_zstd_msgpack(const std::filesystem::path& path);

std::string test_report_to_msgpack_bytes(const TestRunReport& report);
TestRunReport test_report_from_msgpack_bytes(const std::string& content);

std::string test_report_to_zstd_msgpack_bytes(const TestRunReport& report);
TestRunReport test_report_from_zstd_msgpack_bytes(const std::string& content);

} // namespace teez::core
