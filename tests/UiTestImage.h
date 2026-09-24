#pragma once
#include "ui/Toolkit.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>

// Optional CPU raster artifacts for visual layout review, without a GPU/window.
inline void saveUiImage(const genesis::ui::Surface& surface,const std::filesystem::path& file) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file,std::ios::binary);
    const uint32_t bytes=surface.width*surface.height*4;
    auto u16=[&](uint16_t v){out.put(char(v));out.put(char(v>>8));};
    auto u32=[&](uint32_t v){u16(uint16_t(v));u16(uint16_t(v>>16));};
    out.put('B');out.put('M');u32(54+bytes);u32(0);u32(54);u32(40);
    u32(surface.width);u32(uint32_t(-int32_t(surface.height)));u16(1);u16(32);
    u32(0);u32(bytes);u32(0);u32(0);u32(0);u32(0);
    out.write(reinterpret_cast<const char*>(surface.pixels.data()),bytes);
    if(!out) throw std::runtime_error("Unable to write UI image");
}
