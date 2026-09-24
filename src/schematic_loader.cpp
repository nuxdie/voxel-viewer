// Minecraft formats: MCEdit/WorldEdit legacy .schematic, Sponge .schem (v1, v2, v3),
// Litematica .litematic and vanilla structure .nbt files.
#include <stdexcept>
#include <unordered_map>

#include "block_colors.h"
#include "loader.h"

namespace vox {
namespace {

using nbt::Tag;
using nbt::TagType;

// Maps block states to model palette indices. Block states are collapsed to their
// base name since color does not depend on properties.
class PaletteBuilder {
public:
    explicit PaletteBuilder(VoxelModel& m) : model_(m) {}

    uint16_t index(const std::string& blockState) {
        std::string base = baseBlockName(blockState);
        auto it = byName_.find(base);
        if (it != byName_.end()) return it->second;
        BlockInfo info = lookupBlock(base);
        uint16_t idx = 0;
        if (!info.invisible) {
            if (model_.materials.size() >= 65535) throw std::runtime_error("too many distinct blocks");
            idx = uint16_t(model_.materials.size());
            Material m{info.color, info.flags, "minecraft:" + base};
            m.emission = info.emission;
            m.roughness = info.roughness;
            m.metallic = info.metallic;
            model_.materials.push_back(m);
        }
        byName_[base] = idx;
        return idx;
    }

private:
    VoxelModel& model_;
    std::unordered_map<std::string, uint16_t> byName_;
};

int dim(const Tag& t, const char* key) {
    const Tag* v = t.get(key);
    if (!v || !v->isNumber()) throw std::runtime_error(std::string("schematic: missing ") + key);
    int d = int(v->type == TagType::Short ? (v->i & 0xFFFF) : v->i);
    if (d < 0 || d > 1 << 20) throw std::runtime_error("schematic: bad dimensions");
    return d;
}

std::string stateString(const Tag& c) {
    std::string name = c.getString("Name");
    const Tag* props = c.get("Properties", TagType::Compound);
    if (props && !props->children.empty()) {
        name += '[';
        for (size_t i = 0; i < props->children.size(); ++i) {
            if (i) name += ',';
            name += props->children[i].name + '=' + props->children[i].str;
        }
        name += ']';
    }
    return name;
}

// ---- MCEdit / WorldEdit legacy .schematic --------------------------------------------------

std::unique_ptr<VoxelModel> loadLegacy(const Tag& root) {
    auto model = std::make_unique<VoxelModel>();
    model->format = "MCEdit/WorldEdit .schematic (legacy)";
    int w = dim(root, "Width"), h = dim(root, "Height"), l = dim(root, "Length");
    const Tag* blocks = root.get("Blocks", TagType::ByteArray);
    const Tag* data = root.get("Data", TagType::ByteArray);
    const Tag* add = root.get("AddBlocks", TagType::ByteArray);
    const Tag* mapping = root.get("SchematicaMapping", TagType::Compound);
    size_t volume = size_t(w) * size_t(h) * size_t(l);
    if (!blocks || blocks->bytes.size() < volume) throw std::runtime_error("schematic: Blocks array missing or too short");

    std::unordered_map<int, std::string> modded;  // Schematica id -> non-vanilla name
    if (mapping)
        for (const Tag& m : mapping->children)
            if (m.isNumber() && m.name.rfind("minecraft:", 0) != 0) modded[int(m.i)] = m.name;

    PaletteBuilder pb(*model);
    std::unordered_map<int, uint16_t> cache;  // (id << 4 | data) -> palette index
    for (int y = 0; y < h; ++y)
        for (int z = 0; z < l; ++z)
            for (int x = 0; x < w; ++x) {
                size_t i = (size_t(y) * size_t(l) + size_t(z)) * size_t(w) + size_t(x);
                int id = uint8_t(blocks->bytes[i]);
                if (add && (i >> 1) < add->bytes.size()) {
                    uint8_t a = uint8_t(add->bytes[i >> 1]);
                    id |= ((i & 1) == 0 ? (a & 0x0F) : (a >> 4)) << 8;
                }
                if (id == 0) continue;
                int d = (data && i < data->bytes.size()) ? (uint8_t(data->bytes[i]) & 15) : 0;
                int key = id << 4 | d;
                auto it = cache.find(key);
                uint16_t idx;
                if (it != cache.end()) {
                    idx = it->second;
                } else {
                    auto mod = modded.find(id);
                    idx = pb.index(mod != modded.end() ? mod->second : legacyBlockName(id, d));
                    cache[key] = idx;
                }
                if (idx) model->set(x, y, z, idx);
            }
    return model;
}

// ---- Sponge schematic (.schem) v1, v2 and v3 -------------------------------------------------

std::unique_ptr<VoxelModel> loadSponge(const Tag& schem) {
    auto model = std::make_unique<VoxelModel>();
    int version = int(schem.getInt("Version", 1));
    model->format = "Sponge .schem (v" + std::to_string(version) + ")";
    int w = dim(schem, "Width"), h = dim(schem, "Height"), l = dim(schem, "Length");

    const Tag* palette = nullptr;
    const Tag* blockData = nullptr;
    if (const Tag* blocks = schem.get("Blocks", TagType::Compound)) {  // v3
        palette = blocks->get("Palette", TagType::Compound);
        blockData = blocks->get("Data", TagType::ByteArray);
    } else {  // v1/v2
        palette = schem.get("Palette", TagType::Compound);
        blockData = schem.get("BlockData", TagType::ByteArray);
    }
    if (!palette || !blockData) return model;  // biome-only or empty schematic

    PaletteBuilder pb(*model);
    std::vector<uint16_t> remap;  // palette id -> model material
    for (const Tag& p : palette->children) {
        if (!p.isNumber() || p.i < 0 || p.i > 1 << 20) continue;
        if (size_t(p.i) >= remap.size()) remap.resize(size_t(p.i) + 1, 0);
        remap[size_t(p.i)] = pb.index(p.name);
    }

    const auto& bytes = blockData->bytes;
    size_t pos = 0;
    size_t volume = size_t(w) * size_t(h) * size_t(l);
    for (size_t i = 0; i < volume && pos < bytes.size(); ++i) {
        // Unsigned LEB128 varint.
        int64_t value = 0;
        int shift = 0;
        for (;;) {
            if (pos >= bytes.size()) throw std::runtime_error("schem: truncated block data");
            uint8_t b = uint8_t(bytes[pos++]);
            value |= int64_t(b & 0x7F) << shift;
            if (!(b & 0x80)) break;
            shift += 7;
            if (shift > 35) throw std::runtime_error("schem: bad varint");
        }
        if (size_t(value) >= remap.size() || remap[size_t(value)] == 0) continue;
        int x = int(i % size_t(w));
        int z = int(i / size_t(w) % size_t(l));
        int y = int(i / (size_t(w) * size_t(l)));
        model->set(x, y, z, remap[size_t(value)]);
    }
    return model;
}

// ---- Litematica (.litematic) -------------------------------------------------------------

std::unique_ptr<VoxelModel> loadLitematic(const Tag& root) {
    auto model = std::make_unique<VoxelModel>();
    model->format = "Litematica .litematic (v" + std::to_string(root.getInt("Version", 0)) + ")";
    const Tag* regions = root.get("Regions", TagType::Compound);
    PaletteBuilder pb(*model);
    for (const Tag& reg : regions->children) {
        if (reg.type != TagType::Compound) continue;
        const Tag* pos = reg.get("Position", TagType::Compound);
        const Tag* size = reg.get("Size", TagType::Compound);
        const Tag* pal = reg.get("BlockStatePalette", TagType::List);
        const Tag* states = reg.get("BlockStates", TagType::LongArray);
        if (!pos || !size || !pal || !states || pal->children.empty()) continue;

        int sx = int(size->getInt("x")), sy = int(size->getInt("y")), sz = int(size->getInt("z"));
        int ox = int(pos->getInt("x")) + (sx < 0 ? sx + 1 : 0);
        int oy = int(pos->getInt("y")) + (sy < 0 ? sy + 1 : 0);
        int oz = int(pos->getInt("z")) + (sz < 0 ? sz + 1 : 0);
        sx = std::abs(sx); sy = std::abs(sy); sz = std::abs(sz);

        std::vector<uint16_t> remap;
        for (const Tag& e : pal->children) remap.push_back(e.type == TagType::Compound ? pb.index(stateString(e)) : 0);

        int bits = 2;
        while ((size_t(1) << bits) < remap.size()) ++bits;
        const uint64_t mask = (uint64_t(1) << bits) - 1;
        const auto& longs = states->longs;
        size_t volume = size_t(sx) * size_t(sy) * size_t(sz);
        if ((volume * size_t(bits) + 63) / 64 > longs.size()) throw std::runtime_error("litematic: BlockStates too short");

        for (size_t i = 0; i < volume; ++i) {
            size_t startBit = i * size_t(bits);
            size_t startLong = startBit >> 6, endLong = (startBit + size_t(bits) - 1) >> 6;
            unsigned off = unsigned(startBit & 63);
            uint64_t v = uint64_t(longs[startLong]) >> off;
            if (endLong != startLong) v |= uint64_t(longs[endLong]) << (64 - off);
            v &= mask;
            if (v >= remap.size() || remap[v] == 0) continue;
            int x = int(i % size_t(sx));
            int z = int(i / size_t(sx) % size_t(sz));
            int y = int(i / (size_t(sx) * size_t(sz)));
            model->set(ox + x, oy + y, oz + z, remap[v]);
        }
    }
    return model;
}

// ---- Vanilla structure block files (.nbt) -------------------------------------------------

std::unique_ptr<VoxelModel> loadStructure(const Tag& root) {
    auto model = std::make_unique<VoxelModel>();
    model->format = "Minecraft structure .nbt";
    const Tag* pal = root.get("palette", TagType::List);
    if (!pal) {
        const Tag* pals = root.get("palettes", TagType::List);
        if (pals && !pals->children.empty()) pal = &pals->children[0];
    }
    const Tag* blocks = root.get("blocks", TagType::List);
    if (!pal || !blocks) throw std::runtime_error("structure: missing palette or blocks");

    PaletteBuilder pb(*model);
    std::vector<uint16_t> remap;
    for (const Tag& e : pal->children) remap.push_back(e.type == TagType::Compound ? pb.index(stateString(e)) : 0);

    for (const Tag& b : blocks->children) {
        const Tag* p = b.get("pos", TagType::List);
        int64_t s = b.getInt("state", -1);
        if (!p || p->children.size() < 3 || s < 0 || size_t(s) >= remap.size() || remap[size_t(s)] == 0) continue;
        model->set(int(p->children[0].i), int(p->children[1].i), int(p->children[2].i), remap[size_t(s)]);
    }
    return model;
}

}  // namespace

std::unique_ptr<VoxelModel> loadMinecraftNbt(const Tag& root) {
    if (const Tag* s = root.get("Schematic", TagType::Compound)) return loadSponge(*s);  // Sponge v3
    if (root.get("Blocks", TagType::ByteArray)) return loadLegacy(root);
    if (root.get("BlockData", TagType::ByteArray) || root.get("Palette", TagType::Compound) ||
        (root.name == "Schematic" && root.get("Width")))
        return loadSponge(root);
    if (root.get("Regions", TagType::Compound)) return loadLitematic(root);
    if (root.get("blocks", TagType::List) && (root.get("palette") || root.get("palettes"))) return loadStructure(root);
    throw std::runtime_error("unrecognized NBT file (not a schematic, litematic or structure)");
}

}  // namespace vox
