#pragma once

#include <cstdint>
#include <vector>

namespace teez::core {

std::vector<std::uint8_t> zstd_compress(const std::vector<std::uint8_t>& data);
std::vector<std::uint8_t> zstd_decompress(const std::vector<std::uint8_t>& data);

} // namespace teez::core
