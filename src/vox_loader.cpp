// MagicaVoxel .vox loader, including the scene graph (nTRN/nGRP/nSHP), layers and materials.
// Format reference: https://github.com/ephtracy/voxel-model
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>

#include "loader.h"

namespace vox {
namespace {

using Dict = std::map<std::string, std::string>;

class Reader {
public:
    Reader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
    bool eof() const { return pos_ >= n_; }
    size_t pos() const { return pos_; }
    void seek(size_t p) {
        if (p > n_) throw std::runtime_error("vox: chunk extends past end of file");
        pos_ = p;
    }
    void need(size_t k) const {
        if (pos_ + k > n_) throw std::runtime_error("vox: unexpected end of data");
    }
    uint8_t u8() { need(1); return p_[pos_++]; }
    int32_t i32() {
        need(4);
        uint32_t v = uint32_t(p_[pos_]) | uint32_t(p_[pos_ + 1]) << 8 | uint32_t(p_[pos_ + 2]) << 16 | uint32_t(p_[pos_ + 3]) << 24;
        pos_ += 4;
        return int32_t(v);
    }
    std::string id() {
        need(4);
        std::string s(reinterpret_cast<const char*>(p_ + pos_), 4);
        pos_ += 4;
        return s;
    }
    std::string str() {
        int32_t len = i32();
        if (len < 0) throw std::runtime_error("vox: bad string length");
        need(size_t(len));
        std::string s(reinterpret_cast<const char*>(p_ + pos_), size_t(len));
        pos_ += size_t(len);
        return s;
    }
    Dict dict() {
        Dict d;
        int32_t n = i32();
        if (n < 0 || n > 4096) throw std::runtime_error("vox: bad dictionary");
        for (int i = 0; i < n; ++i) {
            std::string k = str();
            d[k] = str();
        }
        return d;
    }

private:
    const uint8_t* p_;
    size_t n_;
    size_t pos_ = 0;
};

struct Model {
    int sx = 0, sy = 0, sz = 0;
    std::vector<uint8_t> xyzi;  // 4 bytes per voxel
};

struct Node {
    enum Kind { Transform, Group, Shape } kind = Transform;
    Dict attrs;
    int child = -1;             // Transform
    int layer = -1;             // Transform
    Dict frame;                 // Transform: first frame
    std::vector<int> children;  // Group
    std::vector<int> models;    // Shape
};

struct Xform {
    int m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    int t[3] = {0, 0, 0};

    Xform operator*(const Xform& o) const {
        Xform r;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                r.m[i][j] = 0;
                for (int k = 0; k < 3; ++k) r.m[i][j] += m[i][k] * o.m[k][j];
            }
            r.t[i] = t[i];
            for (int k = 0; k < 3; ++k) r.t[i] += m[i][k] * o.t[k];
        }
        return r;
    }
};

Xform parseFrame(const Dict& frame) {
    Xform x;
    auto r = frame.find("_r");
    if (r != frame.end()) {
        int bits = std::atoi(r->second.c_str());
        int i0 = bits & 3, i1 = (bits >> 2) & 3;
        if (i0 < 3 && i1 < 3 && i0 != i1) {
            int i2 = 3 - i0 - i1;
            std::memset(x.m, 0, sizeof(x.m));
            x.m[0][i0] = (bits & 16) ? -1 : 1;
            x.m[1][i1] = (bits & 32) ? -1 : 1;
            x.m[2][i2] = (bits & 64) ? -1 : 1;
        }
    }
    auto t = frame.find("_t");
    if (t != frame.end()) std::sscanf(t->second.c_str(), "%d %d %d", &x.t[0], &x.t[1], &x.t[2]);
    return x;
}

int floorDiv2(int v) { return v >= 0 ? v / 2 : -((-v + 1) / 2); }

uint32_t defaultPaletteEntry(int i) {
    // MagicaVoxel's default palette: a 6x6x6 color cube followed by R, G, B and gray ramps.
    // Stored as 0xAABBGGRR.
    if (i <= 0 || i > 255) return 0;
    if (i <= 215) {
        int k = i - 1;  // 0..214, skipping black
        int b = 5 - (k % 6), g = 5 - (k / 6 % 6), r = 5 - (k / 36);
        return 0xFF000000u | uint32_t(b * 0x33) << 16 | uint32_t(g * 0x33) << 8 | uint32_t(r * 0x33);
    }
    static const int ramp[10] = {0xEE, 0xDD, 0xBB, 0xAA, 0x88, 0x77, 0x55, 0x44, 0x22, 0x11};
    int k = i - 216;
    int v = ramp[k % 10];
    switch (k / 10) {
        case 0: return 0xFF000000u | uint32_t(v);                              // red
        case 1: return 0xFF000000u | uint32_t(v) << 8;                         // green
        case 2: return 0xFF000000u | uint32_t(v) << 16;                        // blue
        default: return 0xFF000000u | uint32_t(v) << 16 | uint32_t(v) << 8 | uint32_t(v);  // gray
    }
}

}  // namespace

std::unique_ptr<VoxelModel> loadVox(const std::vector<uint8_t>& data) {
    Reader r(data.data(), data.size());
    if (r.id() != "VOX ") throw std::runtime_error("not a MagicaVoxel file");
    int version = r.i32();
    if (r.id() != "MAIN") throw std::runtime_error("vox: missing MAIN chunk");
    int mainContent = r.i32();
    int mainChildren = r.i32();
    if (mainContent < 0 || mainChildren < 0) throw std::runtime_error("vox: bad MAIN chunk");
    r.seek(r.pos() + size_t(mainContent));
    size_t end = std::min(data.size(), r.pos() + size_t(mainChildren));

    std::vector<Model> models;
    uint32_t palette[256];
    for (int i = 0; i < 256; ++i) palette[i] = defaultPaletteEntry(i);
    std::map<int, Dict> materials;
    std::map<int, Node> nodes;
    std::map<int, bool> layerHidden;

    while (r.pos() + 12 <= end) {
        std::string id = r.id();
        int content = r.i32();
        int children = r.i32();
        if (content < 0 || children < 0) throw std::runtime_error("vox: bad chunk size");
        size_t start = r.pos();
        size_t next = start + size_t(content) + size_t(children);
        if (start + size_t(content) > data.size()) throw std::runtime_error("vox: truncated chunk " + id);

        if (id == "SIZE") {
            Model m;
            m.sx = r.i32(); m.sy = r.i32(); m.sz = r.i32();
            models.push_back(std::move(m));
        } else if (id == "XYZI") {
            if (models.empty()) throw std::runtime_error("vox: XYZI without SIZE");
            int n = r.i32();
            if (n < 0) throw std::runtime_error("vox: bad voxel count");
            r.need(size_t(n) * 4);
            auto& xyzi = models.back().xyzi;
            xyzi.assign(data.begin() + long(r.pos()), data.begin() + long(r.pos() + size_t(n) * 4));
        } else if (id == "RGBA") {
            for (int i = 0; i < 256; ++i) {
                uint32_t c = uint32_t(r.i32());
                if (i < 255) palette[i + 1] = c;
            }
        } else if (id == "MATL") {
            int mid = r.i32();
            materials[mid] = r.dict();
        } else if (id == "nTRN") {
            int nid = r.i32();
            Node n;
            n.kind = Node::Transform;
            n.attrs = r.dict();
            n.child = r.i32();
            r.i32();  // reserved
            n.layer = r.i32();
            int frames = r.i32();
            for (int f = 0; f < frames; ++f) {
                Dict d = r.dict();
                if (f == 0) n.frame = d;
            }
            nodes[nid] = std::move(n);
        } else if (id == "nGRP") {
            int nid = r.i32();
            Node n;
            n.kind = Node::Group;
            n.attrs = r.dict();
            int count = r.i32();
            if (count < 0) throw std::runtime_error("vox: bad group");
            for (int i = 0; i < count; ++i) n.children.push_back(r.i32());
            nodes[nid] = std::move(n);
        } else if (id == "nSHP") {
            int nid = r.i32();
            Node n;
            n.kind = Node::Shape;
            n.attrs = r.dict();
            int count = r.i32();
            if (count < 0) throw std::runtime_error("vox: bad shape");
            for (int i = 0; i < count; ++i) {
                n.models.push_back(r.i32());
                r.dict();
            }
            nodes[nid] = std::move(n);
        } else if (id == "LAYR") {
            int lid = r.i32();
            Dict d = r.dict();
            auto h = d.find("_hidden");
            layerHidden[lid] = h != d.end() && h->second == "1";
        }
        // Chunks may nest; we flatten by visiting children in sequence.
        r.seek(children > 0 ? start + size_t(content) : next);
    }

    auto model = std::make_unique<VoxelModel>();
    model->format = "MagicaVoxel .vox (v" + std::to_string(version) + ")";
    for (int i = 1; i < 256; ++i) {
        Material m;
        uint32_t c = palette[i];
        m.color = Color{uint8_t(c), uint8_t(c >> 8), uint8_t(c >> 16), 255};
        m.name = "#" + std::to_string(i);
        auto it = materials.find(i);
        if (it != materials.end()) {
            const Dict& d = it->second;
            auto get = [&](const char* k) { auto f = d.find(k); return f == d.end() ? std::string() : f->second; };
            std::string type = get("_type");
            if (type == "_glass" || type == "_blend") {
                float trans = get("_trans").empty() ? (get("_alpha").empty() ? 0.5f : 1.0f - float(std::atof(get("_alpha").c_str())))
                                                    : float(std::atof(get("_trans").c_str()));
                float a = std::min(0.9f, std::max(0.15f, 1.0f - trans));
                m.color.a = uint8_t(a * 255.0f);
                m.flags |= kMatTransparent;
            } else if (type == "_emit") {
                // _emit is the strength (0-1) and _flux an extra power step; map both to a light level.
                float emit = get("_emit").empty() ? 1.0f : float(std::atof(get("_emit").c_str()));
                float flux = float(std::atof(get("_flux").c_str()));
                if (emit > 0.0f) {
                    m.flags |= kMatEmissive;
                    long level = std::lround(7.0f + 8.0f * std::min(emit, 1.0f) + flux);
                    m.emission = uint8_t(std::clamp(level, 1L, 15L));
                }
            }
            // Surface finish: metal and glass keep MagicaVoxel's own roughness.
            auto num = [&](const char* k, float def) {
                std::string v = get(k);
                return v.empty() ? def : float(std::atof(v.c_str()));
            };
            if (type == "_metal") {
                m.metallic = uint8_t(std::clamp(num("_metal", 1.0f), 0.0f, 1.0f) * 255.0f);
                m.roughness = uint8_t(std::clamp(num("_rough", 0.2f), 0.0f, 1.0f) * 255.0f);
            } else if (type == "_glass" || type == "_blend") {
                m.roughness = uint8_t(std::clamp(num("_rough", 0.05f), 0.0f, 1.0f) * 255.0f);
            }
        }
        model->materials.push_back(m);
    }

    // MagicaVoxel is Z-up; convert to Y-up: (x, y, z) -> (x, z, -y - 1).
    auto place = [&](const Model& m, const Xform& xf) {
        const uint8_t* v = m.xyzi.data();
        for (size_t i = 0; i + 3 < m.xyzi.size(); i += 4) {
            int lx = v[i], ly = v[i + 1], lz = v[i + 2];
            uint8_t ci = v[i + 3];
            if (ci == 0) continue;
            // Rotate voxel centers about the model center, working in doubled coordinates.
            int c[3] = {2 * lx + 1 - m.sx, 2 * ly + 1 - m.sy, 2 * lz + 1 - m.sz};
            int w[3];
            for (int k = 0; k < 3; ++k) {
                int s = xf.m[k][0] * c[0] + xf.m[k][1] * c[1] + xf.m[k][2] * c[2] + 2 * xf.t[k];
                w[k] = floorDiv2(s);
            }
            model->set(w[0], w[2], -w[1] - 1, ci);
        }
    };

    bool haveScene = nodes.count(0) && nodes[0].kind == Node::Transform;
    if (!haveScene) {
        for (const Model& m : models) place(m, Xform{});
        return model;
    }

    // Walk the scene graph from the root transform.
    struct Walker {
        std::map<int, Node>& nodes;
        std::map<int, bool>& layerHidden;
        std::vector<Model>& models;
        decltype(place)& placeFn;
        std::vector<int> path;  // nodes on the current path, to break cycles in corrupt files
        size_t visits = 0;
        void walk(int id, const Xform& parent, int depth) {
            if (depth > 256 || ++visits > 1000000) return;
            if (std::find(path.begin(), path.end(), id) != path.end()) return;
            auto it = nodes.find(id);
            if (it == nodes.end()) return;
            path.push_back(id);
            walkNode(it->second, parent, depth);
            path.pop_back();
        }
        void walkNode(const Node& n, const Xform& parent, int depth) {
            auto h = n.attrs.find("_hidden");
            if (h != n.attrs.end() && h->second == "1") return;
            switch (n.kind) {
                case Node::Transform: {
                    auto lh = layerHidden.find(n.layer);
                    if (lh != layerHidden.end() && lh->second) return;
                    walk(n.child, parent * parseFrame(n.frame), depth + 1);
                    break;
                }
                case Node::Group:
                    for (int c : n.children) walk(c, parent, depth + 1);
                    break;
                case Node::Shape:
                    if (!n.models.empty() && n.models[0] >= 0 && size_t(n.models[0]) < models.size())
                        placeFn(models[size_t(n.models[0])], parent);
                    break;
            }
        }
    } walker{nodes, layerHidden, models, place, {}, 0};
    walker.walk(0, Xform{}, 0);
    return model;
}

}  // namespace vox
