#include "nbt.h"

#include <zlib.h>

#include <cstring>
#include <stdexcept>

namespace vox::nbt {

const Tag* Tag::get(const std::string& key) const {
    if (type != TagType::Compound) return nullptr;
    for (const Tag& c : children)
        if (c.name == key) return &c;
    return nullptr;
}

const Tag* Tag::get(const std::string& key, TagType t) const {
    const Tag* c = get(key);
    return (c && c->type == t) ? c : nullptr;
}

int64_t Tag::getInt(const std::string& key, int64_t def) const {
    const Tag* c = get(key);
    if (!c || !c->isNumber()) return def;
    if (c->type == TagType::Float || c->type == TagType::Double) return int64_t(c->f);
    return c->i;
}

std::string Tag::getString(const std::string& key, const std::string& def) const {
    const Tag* c = get(key, TagType::String);
    return c ? c->str : def;
}

std::vector<uint8_t> decompress(const std::vector<uint8_t>& data) {
    bool gzip = data.size() >= 2 && data[0] == 0x1f && data[1] == 0x8b;
    bool zlib = data.size() >= 2 && (data[0] & 0x0f) == 8 && ((data[0] << 8) | data[1]) % 31 == 0;
    if (!gzip && !zlib) return data;

    z_stream zs{};
    if (inflateInit2(&zs, 15 + 32) != Z_OK) throw std::runtime_error("zlib init failed");
    std::vector<uint8_t> out;
    out.resize(data.size() * 4 + 1024);
    zs.next_in = const_cast<Bytef*>(data.data());
    zs.avail_in = uInt(data.size());
    int ret;
    do {
        if (zs.total_out >= out.size()) out.resize(out.size() * 2);
        zs.next_out = out.data() + zs.total_out;
        zs.avail_out = uInt(out.size() - zs.total_out);
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret == Z_STREAM_END && zs.avail_in > 0 && gzip) {
            // Concatenated gzip members: keep going.
            size_t produced = zs.total_out;
            inflateReset(&zs);
            zs.total_out = produced;
            ret = Z_OK;
        }
    } while (ret == Z_OK || (ret == Z_BUF_ERROR && zs.avail_in > 0));
    size_t total = zs.total_out;
    inflateEnd(&zs);
    if (ret != Z_STREAM_END) throw std::runtime_error("corrupt compressed data");
    out.resize(total);
    return out;
}

namespace {

class Reader {
public:
    Reader(const uint8_t* p, size_t n) : p_(p), n_(n) {}

    void need(size_t k) {
        if (pos_ + k > n_) throw std::runtime_error("unexpected end of NBT data");
    }
    uint8_t u8() { need(1); return p_[pos_++]; }
    uint16_t u16() { need(2); uint16_t v = uint16_t(p_[pos_] << 8 | p_[pos_ + 1]); pos_ += 2; return v; }
    uint32_t u32() {
        need(4);
        uint32_t v = uint32_t(p_[pos_]) << 24 | uint32_t(p_[pos_ + 1]) << 16 | uint32_t(p_[pos_ + 2]) << 8 | p_[pos_ + 3];
        pos_ += 4;
        return v;
    }
    uint64_t u64() { uint64_t hi = u32(); return hi << 32 | u32(); }
    std::string str() {
        uint16_t len = u16();
        need(len);
        std::string s(reinterpret_cast<const char*>(p_ + pos_), len);
        pos_ += len;
        return s;
    }
    int32_t count() {
        int32_t c = int32_t(u32());
        if (c < 0) throw std::runtime_error("negative NBT array length");
        return c;
    }

    void payload(Tag& t, int depth) {
        if (depth > 512) throw std::runtime_error("NBT nesting too deep");
        switch (t.type) {
            case TagType::End: break;
            case TagType::Byte: t.i = int8_t(u8()); break;
            case TagType::Short: t.i = int16_t(u16()); break;
            case TagType::Int: t.i = int32_t(u32()); break;
            case TagType::Long: t.i = int64_t(u64()); break;
            case TagType::Float: { uint32_t b = u32(); float f; std::memcpy(&f, &b, 4); t.f = f; t.i = int64_t(f); break; }
            case TagType::Double: { uint64_t b = u64(); double d; std::memcpy(&d, &b, 8); t.f = d; t.i = int64_t(d); break; }
            case TagType::ByteArray: {
                int32_t c = count();
                need(size_t(c));
                t.bytes.assign(reinterpret_cast<const int8_t*>(p_ + pos_), reinterpret_cast<const int8_t*>(p_ + pos_ + c));
                pos_ += size_t(c);
                break;
            }
            case TagType::String: t.str = str(); break;
            case TagType::List: {
                t.listType = TagType(u8());
                int32_t c = count();
                if (uint8_t(t.listType) > 12) throw std::runtime_error("bad NBT list type");
                // Reject counts that cannot fit in the remaining data before allocating.
                static const uint8_t kMinSize[13] = {0, 1, 2, 4, 8, 4, 8, 4, 2, 5, 1, 4, 4};
                if (t.listType == TagType::End && c > 0) throw std::runtime_error("bad NBT list");
                need(size_t(c) * kMinSize[uint8_t(t.listType)]);
                t.children.resize(size_t(c));
                for (Tag& ch : t.children) {
                    ch.type = t.listType;
                    payload(ch, depth + 1);
                }
                break;
            }
            case TagType::Compound: {
                for (;;) {
                    TagType ct = TagType(u8());
                    if (ct == TagType::End) break;
                    if (uint8_t(ct) > 12) throw std::runtime_error("bad NBT tag type");
                    Tag ch;
                    ch.type = ct;
                    ch.name = str();
                    payload(ch, depth + 1);
                    t.children.push_back(std::move(ch));
                }
                break;
            }
            case TagType::IntArray: {
                int32_t c = count();
                need(size_t(c) * 4);
                t.ints.resize(size_t(c));
                for (auto& v : t.ints) v = int32_t(u32());
                break;
            }
            case TagType::LongArray: {
                int32_t c = count();
                need(size_t(c) * 8);
                t.longs.resize(size_t(c));
                for (auto& v : t.longs) v = int64_t(u64());
                break;
            }
            default: throw std::runtime_error("bad NBT tag type");
        }
    }

private:
    const uint8_t* p_;
    size_t n_;
    size_t pos_ = 0;
};

}  // namespace

Tag parse(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> data = decompress(raw);
    Reader r(data.data(), data.size());
    Tag root;
    root.type = TagType(r.u8());
    if (root.type != TagType::Compound) throw std::runtime_error("not an NBT file (root is not a compound)");
    root.name = r.str();
    r.payload(root, 0);
    return root;
}

}  // namespace vox::nbt
