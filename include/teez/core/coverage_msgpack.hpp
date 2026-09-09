#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "teez/core/coverage.hpp"

namespace teez::core {

/// Serializes the in-memory CoverageTable fields 1:1 (no computed aggregates).
std::vector<std::uint8_t> coverage_table_to_msgpack(const CoverageTable& table);

/// Restores a CoverageTable from a MessagePack blob produced by coverage_table_to_msgpack.
CoverageTable coverage_table_from_msgpack(const std::vector<std::uint8_t>& data);

void write_coverage_table_to_msgpack(const CoverageTable& table, const std::filesystem::path& path);
CoverageTable load_coverage_table_from_msgpack(const std::filesystem::path& path);

} // namespace teez::core
