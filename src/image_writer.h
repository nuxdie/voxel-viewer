#pragma once

#include <cstdint>
#include <string>

namespace vox {

// Writes 8-bit RGB pixels (rows top to bottom) as a PNG. Returns false on I/O error.
bool writePng(const std::string& path, int width, int height, const uint8_t* rgb);

}  // namespace vox
