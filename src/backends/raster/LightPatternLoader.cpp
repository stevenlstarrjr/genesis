#include "backends/raster/LightPatternLoader.h"

#include <bx/math.h>
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

namespace genesis::raster {
namespace {

float interpolate(const std::vector<float>& coordinates,
    const std::vector<float>& values, float coordinate) {
    if (coordinates.empty() || values.empty()) return 1.0f;
    if (coordinate <= coordinates.front()) return values.front();
    if (coordinate >= coordinates.back()) return values.back();
    const auto upper = std::upper_bound(coordinates.begin(), coordinates.end(), coordinate);
    const size_t high = size_t(upper - coordinates.begin());
    const size_t low = high - 1;
    const float amount = (coordinate - coordinates[low]) /
        std::max(coordinates[high] - coordinates[low], 1e-6f);
    return values[low] + (values[high] - values[low]) * amount;
}

}

std::vector<uint16_t> loadIesPattern(const std::filesystem::path& path, uint16_t size) {
    std::ifstream input(path);
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::string upperText = text;
    std::transform(upperText.begin(), upperText.end(), upperText.begin(),
        [](unsigned char c) { return char(std::toupper(c)); });
    const size_t tilt = upperText.find("TILT=NONE");
    if (tilt == std::string::npos) return {};
    const size_t valuesStart = text.find_first_of("\r\n", tilt);
    if (valuesStart == std::string::npos) return {};
    std::istringstream stream(text.substr(valuesStart));
    std::vector<float> numbers;
    float number = 0.0f;
    while (stream >> number) numbers.push_back(number);
    if (numbers.size() < 13) return {};
    const size_t verticalCount = size_t(std::max(numbers[3], 0.0f));
    const size_t horizontalCount = size_t(std::max(numbers[4], 0.0f));
    if (verticalCount < 2 || horizontalCount < 1 || int(numbers[5]) != 1 ||
        numbers.size() < 13 + verticalCount + horizontalCount + verticalCount * horizontalCount)
        return {};
    const float multiplier = numbers[2];
    size_t cursor = 13;
    std::vector<float> vertical(numbers.begin() + cursor, numbers.begin() + cursor + verticalCount);
    cursor += verticalCount;
    std::vector<float> horizontal(numbers.begin() + cursor, numbers.begin() + cursor + horizontalCount);
    cursor += horizontalCount;
    std::vector<float> candela(numbers.begin() + cursor,
        numbers.begin() + cursor + verticalCount * horizontalCount);
    float maximum = 0.0f;
    for (float& value : candela) { value *= multiplier; maximum = std::max(maximum, value); }
    if (maximum <= 0.0f) return {};
    for (float& value : candela) value /= maximum;
    std::vector<uint16_t> pixels(size_t(size) * size * 4);
    for (uint16_t y = 0; y < size; ++y) for (uint16_t x = 0; x < size; ++x) {
        const float verticalAngle = 180.0f * (float(y) + 0.5f) / float(size);
        float horizontalAngle = 360.0f * (float(x) + 0.5f) / float(size);
        const float horizontalMaximum = horizontal.back();
        if (horizontalMaximum <= 0.0f) horizontalAngle = 0.0f;
        else if (horizontalMaximum <= 90.0f) {
            horizontalAngle = std::fmod(horizontalAngle, 180.0f);
            if (horizontalAngle > 90.0f) horizontalAngle = 180.0f - horizontalAngle;
        } else if (horizontalMaximum <= 180.0f && horizontalAngle > 180.0f) {
            horizontalAngle = 360.0f - horizontalAngle;
        }
        std::vector<float> verticalSlice(horizontalCount);
        for (size_t horizontalIndex = 0; horizontalIndex < horizontalCount; ++horizontalIndex) {
            const auto begin = candela.begin() + horizontalIndex * verticalCount;
            std::vector<float> row(begin, begin + verticalCount);
            verticalSlice[horizontalIndex] = interpolate(vertical, row, verticalAngle);
        }
        const float value = std::clamp(interpolate(horizontal, verticalSlice, horizontalAngle), 0.0f, 1.0f);
        const size_t pixel = (size_t(y) * size + x) * 4;
        pixels[pixel + 0] = pixels[pixel + 1] = pixels[pixel + 2] = bx::halfFromFloat(value);
        pixels[pixel + 3] = bx::halfFromFloat(1.0f);
    }
    return pixels;
}

std::vector<uint16_t> loadCookiePattern(const std::filesystem::path& path, uint16_t size) {
    int width = 0, height = 0, channels = 0;
    stbi_uc* source = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (!source || width <= 0 || height <= 0) {
        if (source) stbi_image_free(source);
        return {};
    }
    std::vector<uint16_t> pixels(size_t(size) * size * 4);
    for (uint16_t y = 0; y < size; ++y) for (uint16_t x = 0; x < size; ++x) {
        const int sourceX = std::min(int((float(x) + 0.5f) * width / size), width - 1);
        const int sourceY = std::min(int((float(y) + 0.5f) * height / size), height - 1);
        const stbi_uc* sample = source + (sourceY * width + sourceX) * 4;
        const float alpha = float(sample[3]) / 255.0f;
        const size_t pixel = (size_t(y) * size + x) * 4;
        for (int channel = 0; channel < 3; ++channel)
            pixels[pixel + channel] = bx::halfFromFloat(
                std::pow(float(sample[channel]) / 255.0f, 2.2f) * alpha);
        pixels[pixel + 3] = bx::halfFromFloat(1.0f);
    }
    stbi_image_free(source);
    return pixels;
}

}
