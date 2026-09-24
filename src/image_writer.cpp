#include "image_writer.h"

#include <zlib.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace vox {

bool writePng(const std::string& path, int width, int height, const uint8_t* rgb) {
    std::vector<uint8_t> raw;
    raw.reserve(size_t(height) * (size_t(width) * 3 + 1));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);  // filter: none
        raw.insert(raw.end(), rgb + size_t(y) * width * 3, rgb + size_t(y + 1) * width * 3);
    }
    uLongf zlen = compressBound(uLong(raw.size()));
    std::vector<uint8_t> z(zlen);
    if (compress2(z.data(), &zlen, raw.data(), uLong(raw.size()), 6) != Z_OK) return false;
    z.resize(zlen);

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    auto be32 = [](uint8_t* p, uint32_t v) { p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v); };
    auto chunk = [&](const char* type, const uint8_t* data, size_t len) {
        uint8_t hdr[8];
        be32(hdr, uint32_t(len));
        std::memcpy(hdr + 4, type, 4);
        std::fwrite(hdr, 1, 8, f);
        if (len) std::fwrite(data, 1, len, f);
        uLong crc = crc32(0, hdr + 4, 4);
        if (len) crc = crc32(crc, data, uInt(len));
        uint8_t c[4];
        be32(c, uint32_t(crc));
        std::fwrite(c, 1, 4, f);
    };
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, uint32_t(width));
    be32(ihdr + 4, uint32_t(height));
    ihdr[8] = 8;   // bit depth
    ihdr[9] = 2;   // color type RGB
    ihdr[10] = 0;  // compression
    ihdr[11] = 0;  // filter
    ihdr[12] = 0;  // interlace
    chunk("IHDR", ihdr, 13);
    chunk("IDAT", z.data(), z.size());
    chunk("IEND", nullptr, 0);
    return std::fclose(f) == 0;
}

}  // namespace vox
