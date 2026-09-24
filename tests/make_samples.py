#!/usr/bin/env python3
"""Generates small sample files in every supported format (for tests and demos).

The same Minecraft build is written as Sponge .schem (v2 and v3), legacy .schematic,
Litematica .litematic and structure .nbt, so loaders can be cross-checked.
Usage: make_samples.py OUTPUT_DIR
"""
import gzip
import math
import random
import os
import struct
import sys

# ---------------------------------------------------------------- NBT writer
END, BYTE, SHORT, INT, LONG, FLOAT, DOUBLE, BYTE_ARRAY, STRING, LIST, COMPOUND, INT_ARRAY, LONG_ARRAY = range(13)


class T:
    """Typed NBT value."""
    def __init__(self, kind, value, list_kind=None):
        self.kind, self.value, self.list_kind = kind, value, list_kind


def _payload(t):
    k, v = t.kind, t.value
    if k == BYTE: return struct.pack(">b", v)
    if k == SHORT: return struct.pack(">h", v)
    if k == INT: return struct.pack(">i", v)
    if k == LONG: return struct.pack(">q", v)
    if k == BYTE_ARRAY: return struct.pack(">i", len(v)) + (v if isinstance(v, bytes) else bytes(b & 0xFF for b in v))
    if k == STRING:
        b = v.encode()
        return struct.pack(">H", len(b)) + b
    if k == LIST:
        return struct.pack(">bi", t.list_kind if v else END, len(v)) + b"".join(_payload(e) for e in v)
    if k == COMPOUND:
        out = b""
        for name, child in v.items():
            out += struct.pack(">b", child.kind) + _payload(T(STRING, name)) + _payload(child)
        return out + b"\x00"
    if k == INT_ARRAY: return struct.pack(">i", len(v)) + b"".join(struct.pack(">i", x) for x in v)
    if k == LONG_ARRAY: return struct.pack(">i", len(v)) + b"".join(struct.pack(">q", x) for x in v)
    raise ValueError(k)


def write_nbt(path, root_name, compound):
    data = struct.pack(">b", COMPOUND) + _payload(T(STRING, root_name)) + _payload(T(COMPOUND, compound))
    with gzip.open(path, "wb") as f:
        f.write(data)


def C(**kw): return T(COMPOUND, kw)
def S(s): return T(STRING, s)
def I(i): return T(INT, i)
def Sh(i): return T(SHORT, i)
def B(i): return T(BYTE, i)


# ---------------------------------------------------------------- the build
W, H, L = 13, 10, 11


def build():
    """Returns dict (x, y, z) -> (modern block state, legacy id, legacy data)."""
    blocks = {}
    for x in range(W):
        for z in range(L):
            blocks[(x, 0, z)] = ("minecraft:grass_block[snowy=false]", 2, 0)
    for x in range(1, 12):
        for z in range(1, 10):
            blocks[(x, 1, z)] = ("minecraft:oak_planks", 5, 0)
    for y in range(2, 6):
        for x in range(1, 12):
            for z in range(1, 10):
                if x in (1, 11) or z in (1, 9):
                    corner = x in (1, 11) and z in (1, 9)
                    state = ("minecraft:oak_log[axis=y]", 17, 0) if corner else ("minecraft:stone_bricks", 98, 0)
                    if not corner and y in (3, 4) and (x in (4, 8) or z == 5):
                        state = ("minecraft:light_blue_stained_glass", 95, 3)
                    blocks[(x, y, z)] = state
    blocks[(6, 2, 1)] = ("minecraft:oak_door[half=lower]", 64, 1)
    blocks[(6, 3, 1)] = ("minecraft:oak_door[half=upper]", 64, 9)
    for i, y in enumerate(range(6, 10)):
        for x in range(i, W - i):
            for z in range(i, L - i):
                if x in (i, W - 1 - i) or z in (i, L - 1 - i) or y == 9:
                    blocks[(x, y, z)] = ("minecraft:red_terracotta", 159, 14)
    blocks[(3, 2, 3)] = ("minecraft:torch", 50, 5)
    blocks[(9, 2, 7)] = ("minecraft:crafting_table", 58, 0)
    for z in range(3, 8):
        blocks[(0, 1, z)] = ("minecraft:water[level=0]", 9, 0)
    blocks[(12, 1, 2)] = ("minecraft:poppy", 38, 0)
    blocks[(12, 1, 4)] = ("minecraft:white_wool", 35, 0)
    blocks[(12, 1, 6)] = ("minecraft:lime_concrete", 251, 5)
    return blocks


def varint(v):
    out = bytearray()
    while True:
        b = v & 0x7F
        v >>= 7
        if v:
            out.append(b | 0x80)
        else:
            out.append(b)
            return out


def sponge(blocks, version):
    palette = {"minecraft:air": 0}
    if version == 3:
        # Unused entries push real ids past 127 so multi-byte varints are exercised.
        for i in range(140):
            palette["minecraft:unused_%d" % i] = len(palette)
    data = bytearray()
    for y in range(H):
        for z in range(L):
            for x in range(W):
                state = blocks.get((x, y, z), ("minecraft:air",))[0]
                if state not in palette:
                    palette[state] = len(palette)
                data += varint(palette[state])
    pal = T(COMPOUND, {k: I(v) for k, v in palette.items()})
    common = dict(Version=I(version), DataVersion=I(3465), Width=Sh(W), Height=Sh(H), Length=Sh(L),
                  Offset=T(INT_ARRAY, [0, 0, 0]))
    if version == 3:
        common["Blocks"] = C(Palette=pal, Data=T(BYTE_ARRAY, list(data)))
        return "", {"Schematic": T(COMPOUND, common)}
    common["PaletteMax"] = I(len(palette))
    common["Palette"] = pal
    common["BlockData"] = T(BYTE_ARRAY, list(data))
    return "Schematic", common


def legacy(blocks):
    ids, datas = [], []
    for y in range(H):
        for z in range(L):
            for x in range(W):
                _, i, d = blocks.get((x, y, z), (None, 0, 0))
                ids.append(i)
                datas.append(d)
    return "Schematic", dict(Width=Sh(W), Height=Sh(H), Length=Sh(L), Materials=S("Alpha"),
                             Blocks=T(BYTE_ARRAY, ids), Data=T(BYTE_ARRAY, datas),
                             Entities=T(LIST, [], COMPOUND), TileEntities=T(LIST, [], COMPOUND))


def state_compound(state):
    name, _, props = state.partition("[")
    c = {"Name": S(name)}
    if props:
        c["Properties"] = T(COMPOUND, {k: S(v) for k, v in (p.split("=") for p in props.rstrip("]").split(","))})
    return T(COMPOUND, c)


def litematic(blocks):
    palette = ["minecraft:air"]
    idx = []
    # Negative size along X exercises the "size < 0" corner handling.
    for y in range(H):
        for z in range(L):
            for x in range(W):
                s = blocks.get((x, y, z), ("minecraft:air",))[0]
                if s not in palette:
                    palette.append(s)
                idx.append(palette.index(s))
    bits = max(2, (len(palette) - 1).bit_length())
    total = len(idx) * bits
    longs = [0] * ((total + 63) // 64)
    for i, v in enumerate(idx):
        bit = i * bits
        longs[bit >> 6] |= (v << (bit & 63)) & 0xFFFFFFFFFFFFFFFF
        if (bit & 63) + bits > 64:
            longs[(bit >> 6) + 1] |= v >> (64 - (bit & 63))
    longs = [l - (1 << 64) if l >= (1 << 63) else l for l in longs]
    region = C(Position=C(x=I(W - 1), y=I(0), z=I(0)), Size=C(x=I(-W), y=I(H), z=I(L)),
               BlockStatePalette=T(LIST, [state_compound(p) for p in palette], COMPOUND),
               BlockStates=T(LONG_ARRAY, longs), Entities=T(LIST, [], COMPOUND),
               TileEntities=T(LIST, [], COMPOUND))
    return "", dict(Version=I(6), MinecraftDataVersion=I(3465),
                    Metadata=C(Name=S("house"), Author=S("voxel-viewer"),
                               EnclosingSize=C(x=I(W), y=I(H), z=I(L))),
                    Regions=C(house=region))


def structure(blocks):
    palette, entries = [], []
    for (x, y, z), (s, _, _) in sorted(blocks.items()):
        if s not in palette:
            palette.append(s)
        entries.append(C(pos=T(LIST, [I(x), I(y), I(z)], INT), state=I(palette.index(s))))
    return "", dict(DataVersion=I(3465), size=T(LIST, [I(W), I(H), I(L)], INT),
                    palette=T(LIST, [state_compound(p) for p in palette], COMPOUND),
                    blocks=T(LIST, entries, COMPOUND), entities=T(LIST, [], COMPOUND))


# ---------------------------------------------------------------- MagicaVoxel
def vox_chunk(cid, content, children=b""):
    return cid + struct.pack("<ii", len(content), len(children)) + content + children


def vox_str(s):
    b = s.encode()
    return struct.pack("<i", len(b)) + b


def vox_dict(d):
    return struct.pack("<i", len(d)) + b"".join(vox_str(k) + vox_str(v) for k, v in d.items())


def write_vox(path):
    """Two models placed by a scene graph: a 4x6x3 red block and a rotated glass slab."""
    m0 = [(x, y, z, 1) for x in range(4) for y in range(6) for z in range(3)]
    m1 = [(x, y, 0, 2) for x in range(8) for y in range(2)]
    children = b""
    children += vox_chunk(b"SIZE", struct.pack("<iii", 4, 6, 3))
    children += vox_chunk(b"XYZI", struct.pack("<i", len(m0)) + b"".join(struct.pack("<BBBB", *v) for v in m0))
    children += vox_chunk(b"SIZE", struct.pack("<iii", 8, 2, 1))
    children += vox_chunk(b"XYZI", struct.pack("<i", len(m1)) + b"".join(struct.pack("<BBBB", *v) for v in m1))
    # Scene: T0 -> G1 -> [T2 -> S3(model 0), T4 -> S5(model 1), T6(hidden layer) -> S7(model 0)]
    def ntrn(nid, child, layer, frame):
        return vox_chunk(b"nTRN", struct.pack("<i", nid) + vox_dict({}) + struct.pack("<iiii", child, -1, layer, 1) + vox_dict(frame))
    children += ntrn(0, 1, -1, {})
    children += vox_chunk(b"nGRP", struct.pack("<i", 1) + vox_dict({}) + struct.pack("<iiii", 3, 2, 4, 6))
    children += ntrn(2, 3, 0, {"_t": "0 0 3"})
    children += vox_chunk(b"nSHP", struct.pack("<i", 3) + vox_dict({}) + struct.pack("<i", 1) + struct.pack("<i", 0) + vox_dict({}))
    # Rotate 90 degrees about Z: row0 = (0,-1,0) -> index 1, negative; row1 = (1,0,0) -> index 0.
    rot = 1 | (0 << 2) | (1 << 4)
    children += ntrn(4, 5, 0, {"_t": "10 0 0", "_r": str(rot)})
    children += vox_chunk(b"nSHP", struct.pack("<i", 5) + vox_dict({}) + struct.pack("<i", 1) + struct.pack("<i", 1) + vox_dict({}))
    children += ntrn(6, 7, 1, {"_t": "-20 0 0"})
    children += vox_chunk(b"nSHP", struct.pack("<i", 7) + vox_dict({}) + struct.pack("<i", 1) + struct.pack("<i", 0) + vox_dict({}))
    children += vox_chunk(b"LAYR", struct.pack("<i", 0) + vox_dict({"_name": "visible"}) + struct.pack("<i", -1))
    children += vox_chunk(b"LAYR", struct.pack("<i", 1) + vox_dict({"_hidden": "1"}) + struct.pack("<i", -1))
    pal = [(0xD0, 0x30, 0x30, 0xFF), (0x80, 0xC0, 0xFF, 0xFF)] + [(0x80, 0x80, 0x80, 0xFF)] * 254
    children += vox_chunk(b"RGBA", b"".join(struct.pack("<BBBB", *c) for c in pal))
    children += vox_chunk(b"MATL", struct.pack("<i", 2) + vox_dict({"_type": "_glass", "_trans": "0.5"}))
    with open(path, "wb") as f:
        f.write(b"VOX " + struct.pack("<i", 150) + vox_chunk(b"MAIN", b"", children))


def write_forest(path):
    """A small forest scene (terrain, trees, flowers, pond, rock) for showing off render modes."""
    rng = random.Random(3)
    W, H, L = 96, 48, 96
    grid = {}
    def h(x, z): return int(10 + 4*math.sin(x/11.0) + 3*math.cos(z/9.0) + 2*math.sin((x+z)/7.0))
    for x in range(W):
        for z in range(L):
            top = h(x, z)
            pond = (x-60)**2 + (z-55)**2 < 150
            for y in range(top):
                grid[(x,y,z)] = "minecraft:stone" if y < top-3 else "minecraft:dirt"
            if pond:
                for y in range(top-3, 13): grid[(x,y,z)] = "minecraft:water"
            else:
                grid[(x,top,z)] = "minecraft:grass_block"
                r = rng.random()
                if r < 0.04: grid[(x,top+1,z)] = rng.choice(["minecraft:poppy","minecraft:dandelion","minecraft:cornflower","minecraft:allium"])
                elif r < 0.12: grid[(x,top+1,z)] = "minecraft:short_grass"
    leaves = ["minecraft:oak_leaves", "minecraft:birch_leaves", "minecraft:acacia_leaves", "minecraft:red_concrete", "minecraft:yellow_concrete"]
    for i in range(14):
        x, z = rng.randrange(8, W-8), rng.randrange(8, L-8)
        if (x-60)**2 + (z-55)**2 < 260: continue
        top = h(x, z) + 1
        th = rng.randint(8, 14)
        for y in range(top, top+th): grid[(x,y,z)] = "minecraft:oak_log" if i % 3 else "minecraft:birch_log"
        lv = rng.choice(leaves); R = rng.randint(4, 6)
        cy = top + th
        for dx in range(-R, R+1):
            for dy in range(-R, R+1):
                for dz in range(-R, R+1):
                    if dx*dx + dy*dy*1.4 + dz*dz <= R*R and rng.random() < 0.85:
                        p = (x+dx, cy+dy, z+dz)
                        if 0 <= p[0] < W and 0 <= p[2] < L and p[1] < H and p not in grid: grid[p] = lv
    # a rock with a waterfall
    for x in range(10, 30):
        for z in range(60, 80):
            top = h(x, z) + int(22 * max(0, 1 - ((x-20)**2 + (z-70)**2) / 110.0))
            for y in range(h(x, z), top): grid[(x,y,z)] = "minecraft:granite" if (x+y) % 7 else "minecraft:dirt"
    pal = {"minecraft:air": 0}
    data = bytearray()
    for y in range(H):
        for z in range(L):
            for x in range(W):
                s = grid.get((x,y,z), "minecraft:air")
                if s not in pal: pal[s] = len(pal)
                data.append(pal[s])
    write_nbt(path, "Schematic", dict(Version=I(2), DataVersion=I(3465), Width=Sh(W), Height=Sh(H), Length=Sh(L),
        PaletteMax=I(len(pal)), Palette=T(COMPOUND, {k: I(v) for k, v in pal.items()}), BlockData=T(BYTE_ARRAY, bytes(data))))


def write_courtyard(path):
    """A courtyard with light sources, water, glass and metal blocks: for block light and reflections."""
    W, H, L = 44, 16, 44
    g = {}
    for x in range(W):
        for z in range(L):
            g[(x, 0, z)] = "minecraft:stone"
            edge = x < 3 or z < 3 or x >= W - 3 or z >= L - 3
            g[(x, 1, z)] = "minecraft:grass_block" if edge else (
                "minecraft:polished_andesite" if (x + z) % 2 else "minecraft:stone_bricks")
    # Pond with a quartz rim.
    for x in range(14, 30):
        for z in range(16, 28):
            rim = x in (14, 29) or z in (16, 27)
            g[(x, 1, z)] = "minecraft:quartz_block" if rim else "minecraft:water"
            if not rim:
                g[(x, 0, z)] = "minecraft:sand"
    # Metal and gem pillars.
    for i, block in enumerate(["iron_block", "gold_block", "diamond_block", "copper_block", "emerald_block"]):
        x = 8 + i * 7
        for y in range(2, 6):
            g[(x, y, 8)] = "minecraft:" + block
    # A glass wall.
    for x in range(33, 41):
        for y in range(2, 7):
            g[(x, y, 20)] = "minecraft:light_blue_stained_glass" if (x + y) % 3 else "minecraft:glass"
    # Lamp posts: fence + torch / lantern / soul torch.
    for (x, z, top) in [(6, 16, "torch"), (6, 28, "lantern"), (37, 30, "soul_lantern"), (37, 12, "torch"),
                        (22, 34, "lantern")]:
        for y in range(2, 5):
            g[(x, y, z)] = "minecraft:oak_fence"
        g[(x, 5, z)] = "minecraft:" + top
    # Lights set into the floor and the pond rim.
    for (x, z) in [(10, 22), (33, 22), (22, 12), (22, 31)]:
        g[(x, 1, z)] = "minecraft:glowstone"
    g[(14, 1, 21)] = "minecraft:sea_lantern"
    g[(29, 1, 21)] = "minecraft:sea_lantern"
    for i, f in enumerate(["ochre_froglight", "verdant_froglight", "pearlescent_froglight"]):
        g[(18 + i * 4, 2, 36)] = "minecraft:" + f
    # A small house lit from inside, with a doorway and windows.
    for x in range(4, 13):
        for z in range(33, 41):
            for y in range(2, 7):
                wall = x in (4, 12) or z in (33, 40)
                if y == 6:
                    g[(x, y, z)] = "minecraft:dark_oak_planks"
                elif wall:
                    door = z == 33 and x == 8 and y in (2, 3)
                    window = y == 4 and (x in (6, 10) or z in (36, 37))
                    if not door:
                        g[(x, y, z)] = "minecraft:glass" if window else "minecraft:spruce_planks"
    g[(8, 5, 37)] = "minecraft:glowstone"
    write_sponge(path, W, H, L, g)


def write_sponge(path, W, H, L, grid):
    palette = {"minecraft:air": 0}
    data = bytearray()
    for y in range(H):
        for z in range(L):
            for x in range(W):
                st = grid.get((x, y, z), "minecraft:air")
                if st not in palette:
                    palette[st] = len(palette)
                data += varint(palette[st])
    write_nbt(path, "Schematic", dict(Version=I(2), DataVersion=I(3465), Width=Sh(W), Height=Sh(H), Length=Sh(L),
                                      PaletteMax=I(len(palette)), Palette=T(COMPOUND, {k: I(v) for k, v in palette.items()}),
                                      BlockData=T(BYTE_ARRAY, bytes(data))))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "samples"
    os.makedirs(out, exist_ok=True)
    blocks = build()
    write_nbt(os.path.join(out, "house.schem"), *sponge(blocks, 2))
    write_nbt(os.path.join(out, "house_v3.schem"), *sponge(blocks, 3))
    write_nbt(os.path.join(out, "house.schematic"), *legacy(blocks))
    write_nbt(os.path.join(out, "house.litematic"), *litematic(blocks))
    write_nbt(os.path.join(out, "house.nbt"), *structure(blocks))
    write_vox(os.path.join(out, "scene.vox"))
    write_forest(os.path.join(out, "forest.schem"))
    write_courtyard(os.path.join(out, "courtyard.schem"))
    print("wrote samples to", out)


if __name__ == "__main__":
    main()
