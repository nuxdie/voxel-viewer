// Minimal NBT (Named Binary Tag) reader, as used by Minecraft / WorldEdit.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vox::nbt {

enum class TagType : uint8_t {
    End = 0, Byte, Short, Int, Long, Float, Double, ByteArray, String, List, Compound, IntArray, LongArray
};

struct Tag {
    TagType type = TagType::End;
    std::string name;

    int64_t i = 0;        // Byte, Short, Int, Long
    double f = 0.0;       // Float, Double
    std::string str;      // String
    std::vector<int8_t> bytes;
    std::vector<int32_t> ints;
    std::vector<int64_t> longs;
    TagType listType = TagType::End;
    std::vector<Tag> children;  // List elements or Compound members

    const Tag* get(const std::string& key) const;
    const Tag* get(const std::string& key, TagType t) const;
    // Convenience accessors with defaults.
    int64_t getInt(const std::string& key, int64_t def = 0) const;
    std::string getString(const std::string& key, const std::string& def = {}) const;
    bool isNumber() const { return type >= TagType::Byte && type <= TagType::Double; }
};

// Decompresses gzip/zlib if needed. Throws std::runtime_error.
std::vector<uint8_t> decompress(const std::vector<uint8_t>& data);
// Parses an (optionally compressed) NBT blob and returns the root compound.
Tag parse(const std::vector<uint8_t>& data);

}  // namespace vox::nbt
