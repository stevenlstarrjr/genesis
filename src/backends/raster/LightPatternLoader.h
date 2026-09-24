#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

namespace genesis::raster {
std::vector<uint16_t> loadIesPattern(const std::filesystem::path& path, uint16_t size);
std::vector<uint16_t> loadCookiePattern(const std::filesystem::path& path, uint16_t size);
}
