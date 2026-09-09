#include "teez/core/compression.hpp"

#include <stdexcept>
#include <string>

#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

namespace teez::core {

std::vector<std::uint8_t> zstd_compress(const std::vector<std::uint8_t>& data) {
    if (data.empty()) {
        return {};
    }

    const std::size_t bound = ZSTD_compressBound(data.size());
    std::vector<std::uint8_t> compressed(bound);
    const std::size_t compressed_size =
        ZSTD_compress(compressed.data(), bound, data.data(), data.size(), ZSTD_defaultCLevel());
    if (ZSTD_isError(compressed_size)) {
        throw std::runtime_error(std::string("zstd compression failed: ") +
                                 ZSTD_getErrorName(compressed_size));
    }

    compressed.resize(compressed_size);
    return compressed;
}

std::vector<std::uint8_t> zstd_decompress(const std::vector<std::uint8_t>& data) {
    if (data.empty()) {
        return {};
    }

    const std::size_t bound = ZSTD_decompressBound(data.data(), data.size());
    if (ZSTD_isError(bound)) {
        throw std::runtime_error("zstd decompression failed: invalid frame");
    }

    std::vector<std::uint8_t> decompressed(bound);
    const std::size_t decompressed_size =
        ZSTD_decompress(decompressed.data(), bound, data.data(), data.size());
    if (ZSTD_isError(decompressed_size)) {
        throw std::runtime_error(std::string("zstd decompression failed: ") +
                                 ZSTD_getErrorName(decompressed_size));
    }

    decompressed.resize(decompressed_size);
    return decompressed;
}

} // namespace teez::core
