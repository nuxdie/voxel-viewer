#include "loader.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace vox {

static std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open file: " + path);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

bool hasSupportedExtension(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext == "vox" || ext == "schem" || ext == "schematic" || ext == "litematic" || ext == "nbt";
}

std::unique_ptr<VoxelModel> loadModelFile(const std::string& path) {
    std::vector<uint8_t> data = readFile(path);
    if (data.size() >= 4 && std::equal(data.begin(), data.begin() + 4, "VOX ")) return loadVox(data);
    nbt::Tag root = nbt::parse(data);
    return loadMinecraftNbt(root);
}

}  // namespace vox
