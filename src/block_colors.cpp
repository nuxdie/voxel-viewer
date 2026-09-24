#include "block_colors.h"

#include <array>
#include <cstring>
#include <unordered_map>

namespace vox {
namespace {

enum : uint8_t { T = kMatTransparent, E = kMatEmissive, D = kMatDecoration };

struct Entry {
    uint32_t rgb;
    uint8_t flags;
    uint8_t alpha;
};

Color rgb(uint32_t v, uint8_t a = 255) { return Color{uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v), a}; }

// Average texture colors. Not exact, but close enough for a recognizable voxel view.
const std::unordered_map<std::string, Entry>& table() {
    static const std::unordered_map<std::string, Entry> t = [] {
        std::unordered_map<std::string, Entry> m;
        auto add = [&](const char* n, uint32_t c, uint8_t f = 0, uint8_t a = 255) { m[n] = Entry{c, f, a}; };
        // Stone & terrain
        add("stone", 0x7D7D7D); add("smooth_stone", 0x9E9E9E); add("granite", 0x956755);
        add("polished_granite", 0x9A6A59); add("diorite", 0xBCBCBC); add("polished_diorite", 0xC0C0C1);
        add("andesite", 0x888888); add("polished_andesite", 0x848685); add("cobblestone", 0x7F7F7F);
        add("mossy_cobblestone", 0x6E7661); add("bedrock", 0x555555); add("gravel", 0x83807E);
        add("grass_block", 0x6E9E44); add("dirt", 0x866043); add("coarse_dirt", 0x77553B);
        add("podzol", 0x5B3F18); add("rooted_dirt", 0x90684D); add("mud", 0x3C393D);
        add("farmland", 0x6F4C2F); add("dirt_path", 0x947A41); add("grass_path", 0x947A41);
        add("mycelium", 0x6F6265); add("clay", 0xA0A6B3); add("sand", 0xDBCFA3); add("red_sand", 0xBE6621);
        add("suspicious_sand", 0xD5C99E); add("suspicious_gravel", 0x807C7A);
        add("sandstone", 0xD8CB9B); add("chiseled_sandstone", 0xD8CA9B); add("cut_sandstone", 0xD9CE9F);
        add("smooth_sandstone", 0xDFD6AA); add("red_sandstone", 0xBA631D); add("chiseled_red_sandstone", 0xB7601C);
        add("cut_red_sandstone", 0xBD6620); add("smooth_red_sandstone", 0xB5621F);
        add("stone_bricks", 0x7A797A); add("mossy_stone_bricks", 0x737962); add("cracked_stone_bricks", 0x767676);
        add("chiseled_stone_bricks", 0x777777); add("infested_stone", 0x7D7D7D); add("bricks", 0x976253);
        add("obsidian", 0x0F0B19); add("crying_obsidian", 0x200A3C, E);
        add("deepslate", 0x505053); add("cobbled_deepslate", 0x4D4D51); add("polished_deepslate", 0x484849);
        add("deepslate_bricks", 0x474747); add("cracked_deepslate_bricks", 0x404040); add("deepslate_tiles", 0x373737);
        add("cracked_deepslate_tiles", 0x343434); add("chiseled_deepslate", 0x373737); add("reinforced_deepslate", 0x505050);
        add("tuff", 0x6C6D66); add("polished_tuff", 0x626863); add("tuff_bricks", 0x636860); add("chiseled_tuff", 0x5C605A);
        add("chiseled_tuff_bricks", 0x62675F); add("calcite", 0xDFE0DC); add("dripstone_block", 0x866B5C);
        add("pointed_dripstone", 0x81664F, D); add("amethyst_block", 0x8662BF); add("budding_amethyst", 0x845EBA);
        add("amethyst_cluster", 0xA47FCF, D); add("moss_block", 0x596E2D); add("moss_carpet", 0x596E2D);
        add("pale_moss_block", 0x6B7069); add("pale_moss_carpet", 0x6B7069);
        add("mud_bricks", 0x89684F); add("packed_mud", 0x8E6B50); add("snow_block", 0xF9FEFE);
        add("snow", 0xF9FEFE, D); add("powder_snow", 0xF8FDFD); add("ice", 0x91B7FD, T, 190);
        add("frosted_ice", 0x8CB4FC, T, 190); add("packed_ice", 0x8DB4FA); add("blue_ice", 0x74A7FD);
        add("terracotta", 0x985E43);
        // Ores
        add("coal_ore", 0x6A6A6A); add("iron_ore", 0x88827E); add("copper_ore", 0x7C7D78); add("gold_ore", 0x8F8B7C);
        add("redstone_ore", 0x8C6E6E); add("emerald_ore", 0x6C8874); add("lapis_ore", 0x667085);
        add("diamond_ore", 0x798D8D); add("nether_gold_ore", 0x73362A); add("nether_quartz_ore", 0x75413E);
        add("ancient_debris", 0x5F4039); add("raw_iron_block", 0xA6876B); add("raw_copper_block", 0x9A6A4F);
        add("raw_gold_block", 0xDDA92F);
        // Mineral blocks
        add("coal_block", 0x101010); add("iron_block", 0xDCDCDC); add("gold_block", 0xF6D03D);
        add("diamond_block", 0x62EDE4); add("emerald_block", 0x2ACB57); add("lapis_block", 0x1E438C);
        add("redstone_block", 0xAF1805); add("netherite_block", 0x423D3F); add("quartz_block", 0xECE6DF);
        add("smooth_quartz", 0xEDE6E0); add("chiseled_quartz_block", 0xE7E2DA); add("quartz_pillar", 0xEBE6E0);
        add("quartz_bricks", 0xEAE5DD);
        add("copper_block", 0xC06C50); add("exposed_copper", 0xA17E68); add("weathered_copper", 0x6C996E);
        add("oxidized_copper", 0x52A284);
        // Wood
        add("oak_planks", 0xA2834F); add("spruce_planks", 0x735531); add("birch_planks", 0xC0AF79);
        add("jungle_planks", 0xA07351); add("acacia_planks", 0xA85A32); add("dark_oak_planks", 0x432B14);
        add("mangrove_planks", 0x763631); add("cherry_planks", 0xE3B3AD); add("bamboo_planks", 0xC3AD50);
        add("bamboo_mosaic", 0xBEA84C); add("crimson_planks", 0x653146); add("warped_planks", 0x2B6963);
        add("pale_oak_planks", 0xE0D5D1);
        add("oak_log", 0x6D5533); add("spruce_log", 0x3A2511); add("birch_log", 0xD8D7D2);
        add("jungle_log", 0x55441B); add("acacia_log", 0x676157); add("dark_oak_log", 0x3C2E1A);
        add("mangrove_log", 0x544329); add("cherry_log", 0x36212B); add("pale_oak_log", 0x57504C);
        add("crimson_stem", 0x5C1A1E); add("warped_stem", 0x3A3A4D); add("bamboo_block", 0x7F9030);
        add("stripped_oak_log", 0xB1904F); add("stripped_spruce_log", 0x74593A); add("stripped_birch_log", 0xC4B07A);
        add("stripped_jungle_log", 0xAB8555); add("stripped_acacia_log", 0xAF5D3C); add("stripped_dark_oak_log", 0x604C31);
        add("stripped_mangrove_log", 0x773630); add("stripped_cherry_log", 0xD7918C); add("stripped_pale_oak_log", 0xF5EEEC);
        add("stripped_crimson_stem", 0x893C5A); add("stripped_warped_stem", 0x3A9795); add("stripped_bamboo_block", 0xC4B04E);
        add("mangrove_roots", 0x4B3C27); add("muddy_mangrove_roots", 0x463B2E);
        // Leaves (biome tint approximated)
        add("oak_leaves", 0x4A7A22); add("spruce_leaves", 0x3D5E3D); add("birch_leaves", 0x5F8A3E);
        add("jungle_leaves", 0x3F8B1B); add("acacia_leaves", 0x4F7D1C); add("dark_oak_leaves", 0x3E6A1A);
        add("mangrove_leaves", 0x5A8A2A); add("cherry_leaves", 0xE5ADC2); add("azalea_leaves", 0x5A7627);
        add("flowering_azalea_leaves", 0x646F3D); add("pale_oak_leaves", 0xA0A69C);
        // Nether & End
        add("netherrack", 0x622626); add("soul_sand", 0x513E32); add("soul_soil", 0x4B3A2F);
        add("glowstone", 0xD0A857, E); add("nether_bricks", 0x2C1519); add("cracked_nether_bricks", 0x281417);
        add("chiseled_nether_bricks", 0x2F1718); add("red_nether_bricks", 0x450709); add("nether_wart_block", 0x720202);
        add("warped_wart_block", 0x167679); add("crimson_nylium", 0x831F1F); add("warped_nylium", 0x2B7265);
        add("shroomlight", 0xF09247, E); add("magma_block", 0x8E3F1F, E); add("basalt", 0x505155);
        add("polished_basalt", 0x636263); add("smooth_basalt", 0x48484E); add("blackstone", 0x2A2328);
        add("polished_blackstone", 0x353039); add("polished_blackstone_bricks", 0x302A30);
        add("cracked_polished_blackstone_bricks", 0x2C2629); add("chiseled_polished_blackstone", 0x353039);
        add("gilded_blackstone", 0x382B26); add("end_stone", 0xDBDE9E); add("end_stone_bricks", 0xDAE0A2);
        add("purpur_block", 0xA97DA9); add("purpur_pillar", 0xAB81AB); add("chorus_plant", 0x5E395E);
        add("chorus_flower", 0x977797); add("dragon_egg", 0x0C0910); add("end_portal_frame", 0x5B7A68);
        add("end_portal", 0x0B0B14, E); add("end_gateway", 0x0B0B14, E); add("nether_portal", 0x5A0EB5, T | E, 170);
        add("respawn_anchor", 0x211A31); add("lodestone", 0x939599); add("crimson_fungus", 0x8D2C1E, D);
        add("warped_fungus", 0x4A6D58, D); add("crimson_roots", 0x7E0819, D); add("warped_roots", 0x149480, D);
        add("nether_sprouts", 0x13977F, D); add("weeping_vines", 0x7B0000, D); add("twisting_vines", 0x148F7C, D);
        add("weeping_vines_plant", 0x7B0000, D); add("twisting_vines_plant", 0x148F7C, D);
        // Ocean
        add("prismarine", 0x63A290); add("prismarine_bricks", 0x63AC9E); add("dark_prismarine", 0x335B4B);
        add("sea_lantern", 0xACC7BE, E); add("sponge", 0xC3C04A); add("wet_sponge", 0xAB9E35);
        add("dried_kelp_block", 0x323B26); add("kelp", 0x57822A, D); add("kelp_plant", 0x57822A, D);
        add("seagrass", 0x2F7A18, D); add("tall_seagrass", 0x2F7A18, D); add("sea_pickle", 0x5A6128, D);
        add("conduit", 0x9F8B71, D); add("water", 0x3F76E4, T, 150); add("bubble_column", 0x3F76E4, T, 150);
        add("lava", 0xD45A12, E);
        add("tube_coral_block", 0x3257CC); add("brain_coral_block", 0xCF5B9F); add("bubble_coral_block", 0xA118A0);
        add("fire_coral_block", 0xA7262F); add("horn_coral_block", 0xD8C842);
        // Functional / misc
        add("glass", 0xC0DBE0, T, 90); add("tinted_glass", 0x2C2630, T, 200); add("glass_pane", 0xC0DBE0, T, 90);
        add("iron_bars", 0x8A8C8B, T, 170); add("chain", 0x3A3F4A, D);
        add("bookshelf", 0x75603D); add("chiseled_bookshelf", 0x70593A); add("crafting_table", 0x81633A);
        add("furnace", 0x6E6E6E); add("blast_furnace", 0x6C6B6B); add("smoker", 0x6B5B48);
        add("chest", 0x9C6F29); add("trapped_chest", 0x9C6F29); add("ender_chest", 0x2D3E3F); add("barrel", 0x7B5A33);
        add("dispenser", 0x6C6C6C); add("dropper", 0x6C6C6C); add("observer", 0x626262); add("hopper", 0x4A4A4A);
        add("piston", 0x6E6A62); add("sticky_piston", 0x6E6A62); add("piston_head", 0x9B8058);
        add("note_block", 0x5C3C2A); add("jukebox", 0x5D402D); add("tnt", 0xB53C28); add("target", 0xE2AA9D);
        add("redstone_lamp", 0x5F3A1D); add("beacon", 0x76DDD7, E); add("spawner", 0x243646);
        add("trial_spawner", 0x333C44); add("vault", 0x3C4750); add("crafter", 0x6E6563);
        add("enchanting_table", 0x5B3E3A); add("anvil", 0x444444); add("chipped_anvil", 0x444444);
        add("damaged_anvil", 0x444444); add("cauldron", 0x4A4A4A); add("water_cauldron", 0x4A4A4A);
        add("lava_cauldron", 0x4A4A4A); add("powder_snow_cauldron", 0x4A4A4A); add("composter", 0x77562F);
        add("loom", 0x93765A); add("cartography_table", 0x675342); add("fletching_table", 0xC5B58A);
        add("smithing_table", 0x3A3B47); add("stonecutter", 0x7D7873, D); add("grindstone", 0x8C8C8C, D);
        add("lectern", 0xAD8A54); add("bell", 0xF9C95B, D); add("brewing_stand", 0x7A6851, D);
        add("command_block", 0xB48A6E); add("repeating_command_block", 0x7D6AB3); add("chain_command_block", 0x86A695);
        add("structure_block", 0x5B4E5B); add("jigsaw", 0x564A56);
        add("hay_block", 0xA68B0C); add("bone_block", 0xD1CEB5); add("slime_block", 0x6FC05B, T, 200);
        add("honey_block", 0xFBB931, T, 200); add("honeycomb_block", 0xE5941E); add("bee_nest", 0xC9A34E);
        add("beehive", 0xB38F54); add("pumpkin", 0xC57618); add("carved_pumpkin", 0xC57618);
        add("jack_o_lantern", 0xD4981A, E); add("melon", 0x70921E); add("cactus", 0x557F2A);
        add("cake", 0xE4CDCE); add("sculk", 0x0D1E24); add("sculk_catalyst", 0x2C3B3A); add("sculk_sensor", 0x07434F);
        add("calibrated_sculk_sensor", 0x1E4750); add("sculk_shrieker", 0xC6CDA7); add("sculk_vein", 0x0D1E24, D);
        add("ochre_froglight", 0xF5E9B5, E); add("verdant_froglight", 0xE5F4E4, E); add("pearlescent_froglight", 0xF5F0EF, E);
        add("brown_mushroom_block", 0x957051); add("red_mushroom_block", 0xC82E2D); add("mushroom_stem", 0xCBC4B9);
        add("daylight_detector", 0x826C52, D); add("scaffolding", 0xAA8449); add("lantern", 0xE3A454, E | D);
        add("soul_lantern", 0x6FC0C4, E | D); add("campfire", 0xE0A040, E | D); add("soul_campfire", 0x6FD2D9, E | D);
        add("end_rod", 0xF0E6DB, E | D); add("fire", 0xE08A1E, E | D); add("soul_fire", 0x33C1C8, E | D);
        add("decorated_pot", 0x8C4F3F, D); add("flower_pot", 0x7C4535, D); add("heavy_core", 0x51535A, D);
        // Small / decorative
        add("torch", 0xFFD86C, E | D); add("wall_torch", 0xFFD86C, E | D); add("soul_torch", 0x70E3E6, E | D);
        add("soul_wall_torch", 0x70E3E6, E | D); add("redstone_torch", 0xFF3000, E | D);
        add("redstone_wall_torch", 0xFF3000, E | D); add("redstone_wire", 0xBD0000, D); add("lever", 0x6F5F4A, D);
        add("rail", 0x7C705C, D); add("powered_rail", 0x9A7C4A, D); add("detector_rail", 0x7C6A5C, D);
        add("activator_rail", 0x7C5A50, D); add("tripwire", 0xC8C8C8, D); add("tripwire_hook", 0x8A7A5C, D);
        add("ladder", 0x7C6036, D); add("vine", 0x3B6E1F, D); add("glow_lichen", 0x70837A, D);
        add("repeater", 0xA09D98, D); add("comparator", 0xA09D98, D); add("cobweb", 0xE6EAEB, D);
        add("lily_pad", 0x208030, D); add("short_grass", 0x6E9E44, D); add("grass", 0x6E9E44, D);
        add("tall_grass", 0x6E9E44, D); add("fern", 0x5E8A3A, D); add("large_fern", 0x5E8A3A, D);
        add("dead_bush", 0x6B4F29, D); add("sugar_cane", 0x94C065, D); add("bamboo", 0x5D8C23, D);
        add("bamboo_sapling", 0x5D8C23, D); add("wheat", 0xDCBB65, D); add("carrots", 0x3E9A1E, D);
        add("potatoes", 0x4C9A2A, D); add("beetroots", 0x5B8A33, D); add("sweet_berry_bush", 0x355E30, D);
        add("nether_wart", 0x8A1D1F, D); add("cocoa", 0x8A5A2C, D); add("pumpkin_stem", 0x8A9A3A, D);
        add("melon_stem", 0x8A9A3A, D); add("attached_pumpkin_stem", 0x8A9A3A, D); add("attached_melon_stem", 0x8A9A3A, D);
        add("brown_mushroom", 0x9A7559, D); add("red_mushroom", 0xD52F2B, D); add("cave_vines", 0x5A6D29, D);
        add("cave_vines_plant", 0x5A6D29, D); add("hanging_roots", 0xA1735C, D); add("spore_blossom", 0xCF639F, D);
        add("big_dripleaf", 0x6F9530, D); add("small_dripleaf", 0x6F9530, D); add("azalea", 0x667D30, D);
        add("flowering_azalea", 0x707A40, D); add("pink_petals", 0xF4B7D5, D); add("pitcher_plant", 0x6D87B7, D);
        add("torchflower", 0xD8963B, D); add("frogspawn", 0x6A5F5A, D); add("turtle_egg", 0xE4E1BF, D);
        add("sniffer_egg", 0x8B4A3A, D); add("dandelion", 0xF5DE24, D); add("poppy", 0xC8241E, D);
        add("blue_orchid", 0x2BA3E6, D); add("allium", 0xB37CE4, D); add("azure_bluet", 0xD6E8E8, D);
        add("red_tulip", 0xD22E1C, D); add("orange_tulip", 0xE4731A, D); add("white_tulip", 0xD6E8E8, D);
        add("pink_tulip", 0xE2B5D9, D); add("oxeye_daisy", 0xE8E8C3, D); add("cornflower", 0x4F72E0, D);
        add("lily_of_the_valley", 0xF0F0F0, D); add("wither_rose", 0x2A2D1C, D); add("sunflower", 0xF5D325, D);
        add("lilac", 0xC59BC7, D); add("rose_bush", 0xA62E1A, D); add("peony", 0xE7C0E8, D);
        add("item_frame", 0x8A6440, D); add("glow_item_frame", 0x8A6440, D); add("painting", 0x8A6440, D);
        add("light_weighted_pressure_plate", 0xF6D03D, D); add("heavy_weighted_pressure_plate", 0xDCDCDC, D);
        add("skeleton_skull", 0xC8C8C8, D); add("wither_skeleton_skull", 0x303030, D); add("zombie_head", 0x4C7A3A, D);
        add("player_head", 0x6A4A3A, D); add("creeper_head", 0x5DB24A, D); add("dragon_head", 0x1A1A1A, D);
        add("piglin_head", 0xD89A8A, D);
        return m;
    }();
    return t;
}

const std::array<const char*, 16> kDyes = {"white", "orange", "magenta", "light_blue", "yellow", "lime",
                                           "pink", "gray", "light_gray", "cyan", "purple", "blue",
                                           "brown", "green", "red", "black"};
const uint32_t kWool[16] = {0xE9ECEC, 0xF07613, 0xBD44B3, 0x3AAFD9, 0xF8C627, 0x70B919, 0xED8DAC, 0x3E4447,
                            0x8E8E86, 0x158991, 0x792AAC, 0x35399D, 0x724728, 0x546D1B, 0xA12722, 0x141519};
const uint32_t kConcrete[16] = {0xCFD5D6, 0xE06100, 0xA9309F, 0x2389C6, 0xF0AF15, 0x5EA818, 0xD5658E, 0x36393D,
                                0x7D7D73, 0x157788, 0x64209C, 0x2C2E8F, 0x603B1F, 0x495B24, 0x8E2020, 0x080A0F};
const uint32_t kTerracotta[16] = {0xD1B2A1, 0xA15325, 0x95586C, 0x716C89, 0xBA8523, 0x677534, 0xA14E4E, 0x392A23,
                                  0x876A61, 0x575B5B, 0x764656, 0x4A3B5B, 0x4D3323, 0x4C532A, 0x8F3D2E, 0x251610};
const uint32_t kDyeTint[16] = {0xFFFFFF, 0xD87F33, 0xB24CD8, 0x6699D8, 0xE5E533, 0x7FCC19, 0xF27FA5, 0x4C4C4C,
                               0x999999, 0x4C7F99, 0x7F3FB2, 0x334CB2, 0x664C33, 0x667F33, 0x993333, 0x191919};

bool startsWith(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }
bool endsWith(const std::string& s, const char* p) {
    size_t n = std::strlen(p);
    return s.size() >= n && s.compare(s.size() - n, n, p) == 0;
}
bool contains(const std::string& s, const char* p) { return s.find(p) != std::string::npos; }

uint32_t lighten(uint32_t c, float t) {
    auto ch = [&](int sh) { int v = (c >> sh) & 0xFF; return uint32_t(v + (255 - v) * t) << sh; };
    return ch(16) | ch(8) | ch(0);
}

BlockInfo make(uint32_t c, uint8_t flags = 0, uint8_t alpha = 255) {
    BlockInfo b;
    b.color = rgb(c, alpha);
    b.flags = flags;
    return b;
}

bool lookupExact(const std::string& n, BlockInfo& out) {
    const auto& t = table();
    auto it = t.find(n);
    if (it == t.end()) return false;
    out = make(it->second.rgb, it->second.flags, it->second.alpha);
    return true;
}

bool lookupDyed(const std::string& n, BlockInfo& out) {
    for (int i = 15; i >= 0; --i) {  // reverse so "light_gray" is tried before "gray"
        std::string prefix = std::string(kDyes[size_t(i)]) + "_";
        if (!startsWith(n, prefix.c_str())) continue;
        std::string rest = n.substr(prefix.size());
        if (rest == "wool" || rest == "bed" || rest == "banner" || rest == "wall_banner" || rest == "carpet")
            return out = make(kWool[i], (rest == "wool" || rest == "bed" || rest == "carpet") ? 0 : D), true;
        if (rest == "concrete") return out = make(kConcrete[i]), true;
        if (rest == "concrete_powder") return out = make(lighten(kConcrete[i], 0.2f)), true;
        if (rest == "terracotta") return out = make(kTerracotta[i]), true;
        if (rest == "glazed_terracotta") return out = make(lighten(kConcrete[i], 0.1f)), true;
        if (rest == "stained_glass") return out = make(kDyeTint[i], T, 150), true;
        if (rest == "stained_glass_pane") return out = make(kDyeTint[i], T, 150), true;
        if (rest == "shulker_box") return out = make(kWool[i]), true;
        if (rest == "candle" || rest == "candle_cake") return out = make(kWool[i], D), true;
        // Some Sponge files use legacy-ish names like "light_gray_stained_hardened_clay".
        if (rest == "stained_hardened_clay") return out = make(kTerracotta[i]), true;
    }
    // "silver" was the pre-1.13 name for light_gray.
    if (startsWith(n, "silver_")) return lookupDyed("light_gray_" + n.substr(7), out);
    return false;
}

const char* kWoods[] = {"oak", "spruce", "birch", "jungle", "acacia", "dark_oak", "mangrove", "cherry",
                        "bamboo", "crimson", "warped", "pale_oak"};

bool lookupRec(const std::string& n, BlockInfo& out, int depth);

bool lookupShape(const std::string& n, BlockInfo& out, int depth) {
    struct Suffix { const char* s; uint8_t flags; };
    static const Suffix suffixes[] = {
        {"_wall_hanging_sign", D}, {"_hanging_sign", D}, {"_wall_sign", D}, {"_sign", D}, {"_pressure_plate", D},
        {"_button", D}, {"_fence_gate", 0}, {"_fence", 0}, {"_trapdoor", 0}, {"_door", 0}, {"_stairs", 0},
        {"_slab", 0}, {"_wall", 0}, {"_pane", 0}, {"_wood", 0}, {"_hyphae", 0}, {"_carpet", 0},
        {"_coral_wall_fan", D}, {"_coral_fan", D}, {"_coral", D}, {"_sapling", D}, {"_leaf_litter", D},
    };
    for (const auto& suf : suffixes) {
        if (!endsWith(n, suf.s)) continue;
        std::string base = n.substr(0, n.size() - std::strlen(suf.s));
        if (base.empty()) continue;
        std::string s = suf.s;
        if (s == "_wood") base += "_log";
        if (s == "_hyphae") base += "_stem";
        if (s == "_pane") base = n.substr(0, n.size() - 5);
        if (s == "_coral_fan" || s == "_coral_wall_fan" || s == "_coral") base += "_coral_block";
        if (s == "_sapling") base += "_leaves";
        const std::string candidates[] = {base, base + "s", base + "_planks", base + "_block", base + "_bricks"};
        for (const auto& c : candidates) {
            if (lookupRec(c, out, depth + 1)) {
                out.flags |= suf.flags;
                return true;
            }
        }
    }
    return false;
}

bool lookupRec(const std::string& n, BlockInfo& out, int depth) {
    if (depth > 4) return false;
    if (lookupExact(n, out)) return true;
    if (lookupDyed(n, out)) return true;
    if (startsWith(n, "waxed_")) return lookupRec(n.substr(6), out, depth + 1);
    if (startsWith(n, "potted_")) return lookupExact("flower_pot", out);
    if (startsWith(n, "infested_")) return lookupRec(n.substr(9), out, depth + 1);
    if (startsWith(n, "dead_") && contains(n, "coral")) return out = make(0x857E79, contains(n, "block") ? 0 : D), true;
    if (lookupShape(n, out, depth)) return true;
    return false;
}

uint32_t hashColor(const std::string& n) {
    uint32_t h = 2166136261u;
    for (char c : n) h = (h ^ uint8_t(c)) * 16777619u;
    // Muted colors so unknown blocks do not scream.
    uint32_t r = 90 + (h & 0x7F), g = 90 + ((h >> 8) & 0x7F), b = 90 + ((h >> 16) & 0x7F);
    return r << 16 | g << 8 | b;
}

BlockInfo lookupHeuristic(const std::string& n) {
    // Copper variants (cut_copper, copper_grate, chiseled_copper, copper_bulb...)
    if (contains(n, "copper")) {
        if (contains(n, "oxidized")) return make(0x52A284);
        if (contains(n, "weathered")) return make(0x6C996E);
        if (contains(n, "exposed")) return make(0xA17E68);
        return make(0xC06C50, contains(n, "bulb") ? E : 0);
    }
    if (contains(n, "_ore")) return make(contains(n, "deepslate") ? 0x5A5A5C : 0x7D7D7D);
    if (contains(n, "deepslate")) return make(0x474749);
    if (contains(n, "blackstone")) return make(0x302A30);
    if (contains(n, "sandstone")) return make(contains(n, "red") ? 0xB8621F : 0xD9CE9F);
    if (contains(n, "quartz")) return make(0xECE6DF);
    if (contains(n, "prismarine")) return make(0x5DA38F);
    if (contains(n, "purpur")) return make(0xA97DA9);
    if (contains(n, "nether_brick")) return make(0x2C1519);
    if (contains(n, "end_stone")) return make(0xDAE0A2);
    if (contains(n, "mud_brick")) return make(0x89684F);
    if (contains(n, "tuff")) return make(0x646861);
    if (contains(n, "brick")) return make(0x7A797A);
    if (contains(n, "cobblestone")) return make(0x7F7F7F);
    if (contains(n, "stone")) return make(0x7D7D7D);
    if (contains(n, "leaves")) return make(0x4A7A22);
    if (contains(n, "glass")) return make(0xC0DBE0, T, 90);
    if (contains(n, "ice")) return make(0x91B7FD, T, 190);
    if (contains(n, "shulker_box")) return make(0x8B5F8B);
    if (contains(n, "candle")) return make(0xE3CFA6, D);
    if (contains(n, "bed")) return make(0xA12722);
    if (contains(n, "banner")) return make(0xE9ECEC, D);
    for (const char* w : kWoods)
        if (startsWith(n, (std::string(w) + "_").c_str())) {
            BlockInfo b;
            if (lookupExact(std::string(w) + "_planks", b)) return b;
        }
    return make(hashColor(n));
}

}  // namespace

std::string baseBlockName(const std::string& state) {
    std::string n = state;
    size_t br = n.find('[');
    if (br != std::string::npos) n.resize(br);
    size_t colon = n.find(':');
    if (colon != std::string::npos) n = n.substr(colon + 1);
    while (!n.empty() && (n.back() == ' ' || n.back() == '\n')) n.pop_back();
    return n;
}

// Color lookup only (no light or surface properties); defined below.
BlockInfo lookupBlockColor(const std::string& baseName);

namespace {

// Light levels from the Minecraft wiki (Java edition, default block states).
uint8_t lightLevel(const std::string& n) {
    static const std::unordered_map<std::string, uint8_t> levels = {
        {"torch", 14}, {"wall_torch", 14}, {"soul_torch", 10}, {"soul_wall_torch", 10},
        {"redstone_torch", 7}, {"redstone_wall_torch", 7}, {"lantern", 15}, {"soul_lantern", 10},
        {"glowstone", 15}, {"sea_lantern", 15}, {"jack_o_lantern", 15}, {"shroomlight", 15},
        {"ochre_froglight", 15}, {"verdant_froglight", 15}, {"pearlescent_froglight", 15},
        {"beacon", 15}, {"conduit", 15}, {"end_rod", 14}, {"end_gateway", 15}, {"end_portal", 15},
        {"fire", 15}, {"soul_fire", 10}, {"campfire", 15}, {"soul_campfire", 10}, {"lava", 15},
        {"magma_block", 3}, {"crying_obsidian", 10}, {"nether_portal", 11}, {"respawn_anchor", 15},
        {"redstone_lamp", 0}, {"sea_pickle", 6}, {"glow_lichen", 7}, {"brewing_stand", 1},
        {"brown_mushroom", 1}, {"dragon_egg", 1}, {"end_portal_frame", 1}, {"sculk_sensor", 1},
        {"enchanting_table", 7}, {"ender_chest", 7}, {"amethyst_cluster", 5}, {"cave_vines", 14},
        {"cave_vines_plant", 14}, {"light", 15}, {"copper_bulb", 15}, {"trial_spawner", 4}, {"vault", 6},
    };
    auto it = levels.find(n);
    if (it != levels.end()) return it->second;
    if (n.find("candle") != std::string::npos) return 3;
    if (n.find("copper_bulb") != std::string::npos) return n.find("oxidized") != std::string::npos ? 4 : 12;
    return 0;
}

// Surface finish for reflections.
void surface(const std::string& n, BlockInfo& b) {
    auto has = [&](const char* s) { return n.find(s) != std::string::npos; };
    if (n == "water" || n == "bubble_column") { b.roughness = 8; return; }
    if (has("glass")) { b.roughness = 12; return; }
    if (n == "ice" || n == "packed_ice" || n == "blue_ice" || n == "frosted_ice") { b.roughness = 30; return; }
    static const char* metals[] = {"iron_block", "gold_block", "diamond_block", "emerald_block", "netherite_block",
                                   "raw_iron_block", "raw_gold_block", "lapis_block", "iron_bars", "iron_door",
                                   "iron_trapdoor", "chain", "anvil", "bell", "heavy_core"};
    for (const char* m : metals)
        if (has(m)) {
            b.metallic = has("raw_") || n == "lapis_block" ? 0 : 255;
            b.roughness = has("raw_") ? 150 : n == "diamond_block" || n == "emerald_block" ? 45 : 70;
            return;
        }
    if (has("copper") && !has("ore")) {
        b.metallic = 255;
        b.roughness = has("oxidized") || has("weathered") ? 180 : has("waxed") ? 55 : 80;
        return;
    }
    if (has("glazed_terracotta") || has("prismarine") || n == "slime_block" || n == "honey_block") b.roughness = 70;
    else if (has("polished_") || has("quartz") || has("smooth_") || has("concrete") || has("obsidian") ||
             has("purpur") || (has("_bricks") && has("deepslate")))
        b.roughness = 120;
}

}  // namespace

BlockInfo lookupBlock(const std::string& state) {
    std::string n = baseBlockName(state);
    BlockInfo b = lookupBlockColor(n);
    if (!b.invisible) {
        b.emission = lightLevel(n);
        if (b.emission) b.flags |= kMatEmissive;
        surface(n, b);
    }
    return b;
}

BlockInfo lookupBlockColor(const std::string& n) {
    BlockInfo b;
    if (n.empty() || n == "air" || n == "cave_air" || n == "void_air" || n == "structure_void" || n == "barrier" ||
        n == "light" || n == "moving_piston") {
        b.invisible = true;
        return b;
    }
    if (lookupRec(n, b, 0)) return b;
    return lookupHeuristic(n);
}

std::string legacyBlockName(int id, int data) {
    static const char* const kNames[256] = {
        "air", "stone", "grass_block", "dirt", "cobblestone", "oak_planks", "oak_sapling", "bedrock",
        "water", "water", "lava", "lava", "sand", "gravel", "gold_ore", "iron_ore",
        "coal_ore", "oak_log", "oak_leaves", "sponge", "glass", "lapis_ore", "lapis_block", "dispenser",
        "sandstone", "note_block", "red_bed", "powered_rail", "detector_rail", "sticky_piston", "cobweb", "short_grass",
        "dead_bush", "piston", "piston_head", "white_wool", "moving_piston", "dandelion", "poppy", "brown_mushroom",
        "red_mushroom", "gold_block", "iron_block", "smooth_stone", "smooth_stone_slab", "bricks", "tnt", "bookshelf",
        "mossy_cobblestone", "obsidian", "torch", "fire", "spawner", "oak_stairs", "chest", "redstone_wire",
        "diamond_ore", "diamond_block", "crafting_table", "wheat", "farmland", "furnace", "furnace", "oak_sign",
        "oak_door", "ladder", "rail", "cobblestone_stairs", "oak_wall_sign", "lever", "stone_pressure_plate", "iron_door",
        "oak_pressure_plate", "redstone_ore", "redstone_ore", "redstone_torch", "redstone_torch", "stone_button", "snow", "ice",
        "snow_block", "cactus", "clay", "sugar_cane", "jukebox", "oak_fence", "carved_pumpkin", "netherrack",
        "soul_sand", "glowstone", "nether_portal", "jack_o_lantern", "cake", "repeater", "repeater", "white_stained_glass",
        "oak_trapdoor", "infested_stone", "stone_bricks", "brown_mushroom_block", "red_mushroom_block", "iron_bars", "glass_pane", "melon",
        "pumpkin_stem", "melon_stem", "vine", "oak_fence_gate", "brick_stairs", "stone_brick_stairs", "mycelium", "lily_pad",
        "nether_bricks", "nether_brick_fence", "nether_brick_stairs", "nether_wart", "enchanting_table", "brewing_stand", "cauldron", "end_portal",
        "end_portal_frame", "end_stone", "dragon_egg", "redstone_lamp", "redstone_lamp", "oak_slab", "oak_slab", "cocoa",
        "sandstone_stairs", "emerald_ore", "ender_chest", "tripwire_hook", "tripwire", "emerald_block", "spruce_stairs", "birch_stairs",
        "jungle_stairs", "command_block", "beacon", "cobblestone_wall", "flower_pot", "carrots", "potatoes", "oak_button",
        "skeleton_skull", "anvil", "trapped_chest", "light_weighted_pressure_plate", "heavy_weighted_pressure_plate", "comparator", "comparator", "daylight_detector",
        "redstone_block", "nether_quartz_ore", "hopper", "quartz_block", "quartz_stairs", "activator_rail", "dropper", "white_terracotta",
        "white_stained_glass_pane", "acacia_leaves", "acacia_log", "acacia_stairs", "dark_oak_stairs", "slime_block", "barrier", "iron_trapdoor",
        "prismarine", "sea_lantern", "hay_block", "white_carpet", "terracotta", "coal_block", "packed_ice", "tall_grass",
        "white_banner", "white_wall_banner", "daylight_detector", "red_sandstone", "red_sandstone_stairs", "red_sandstone_slab", "red_sandstone_slab", "spruce_fence_gate",
        "birch_fence_gate", "jungle_fence_gate", "dark_oak_fence_gate", "acacia_fence_gate", "spruce_fence", "birch_fence", "jungle_fence", "dark_oak_fence",
        "acacia_fence", "spruce_door", "birch_door", "jungle_door", "acacia_door", "dark_oak_door", "end_rod", "chorus_plant",
        "chorus_flower", "purpur_block", "purpur_pillar", "purpur_stairs", "purpur_slab", "purpur_slab", "end_stone_bricks", "beetroots",
        "dirt_path", "end_gateway", "repeating_command_block", "chain_command_block", "frosted_ice", "magma_block", "nether_wart_block", "red_nether_bricks",
        "bone_block", "structure_void", "observer", "white_shulker_box", "orange_shulker_box", "magenta_shulker_box", "light_blue_shulker_box", "yellow_shulker_box",
        "lime_shulker_box", "pink_shulker_box", "gray_shulker_box", "light_gray_shulker_box", "cyan_shulker_box", "purple_shulker_box", "blue_shulker_box", "brown_shulker_box",
        "green_shulker_box", "red_shulker_box", "black_shulker_box", "white_glazed_terracotta", "orange_glazed_terracotta", "magenta_glazed_terracotta", "light_blue_glazed_terracotta", "yellow_glazed_terracotta",
        "lime_glazed_terracotta", "pink_glazed_terracotta", "gray_glazed_terracotta", "light_gray_glazed_terracotta", "cyan_glazed_terracotta", "purple_glazed_terracotta", "blue_glazed_terracotta", "brown_glazed_terracotta",
        "green_glazed_terracotta", "red_glazed_terracotta", "black_glazed_terracotta", "white_concrete", "white_concrete_powder", "air", "air", "structure_block",
    };
    if (id < 0 || id > 255) return "unknown_" + std::to_string(id);
    auto dyed = [&](const char* what) { return std::string(kDyes[size_t(data & 15)]) + "_" + what; };
    static const char* const kPlankWoods[] = {"oak", "spruce", "birch", "jungle", "acacia", "dark_oak", "oak", "oak"};
    switch (id) {
        case 1: {
            static const char* const s[] = {"stone", "granite", "polished_granite", "diorite", "polished_diorite",
                                            "andesite", "polished_andesite", "stone"};
            return s[data & 7];
        }
        case 3: return data == 1 ? "coarse_dirt" : data == 2 ? "podzol" : "dirt";
        case 5: case 125: return std::string(kPlankWoods[data & 7]) + "_planks";
        case 6: return std::string(kPlankWoods[data & 7]) + "_sapling";
        case 12: return data == 1 ? "red_sand" : "sand";
        case 17: return std::string(kPlankWoods[data & 3]) + "_log";
        case 18: return std::string(kPlankWoods[data & 3]) + "_leaves";
        case 19: return data == 1 ? "wet_sponge" : "sponge";
        case 24: return data == 1 ? "chiseled_sandstone" : data == 2 ? "cut_sandstone" : "sandstone";
        case 31: return data == 0 ? "dead_bush" : data == 2 ? "fern" : "short_grass";
        case 35: return dyed("wool");
        case 38: {
            static const char* const s[] = {"poppy", "blue_orchid", "allium", "azure_bluet", "red_tulip",
                                            "orange_tulip", "white_tulip", "pink_tulip", "oxeye_daisy"};
            return (data >= 0 && data <= 8) ? s[data] : "poppy";
        }
        case 43: case 44: {
            static const char* const s[] = {"smooth_stone", "sandstone", "oak_planks", "cobblestone",
                                            "bricks", "stone_bricks", "nether_bricks", "quartz_block"};
            return s[data & 7];
        }
        case 95: return dyed("stained_glass");
        case 97: return "infested_stone";
        case 98: {
            static const char* const s[] = {"stone_bricks", "mossy_stone_bricks", "cracked_stone_bricks", "chiseled_stone_bricks"};
            return s[data & 3];
        }
        case 126: return std::string(kPlankWoods[data & 7]) + "_slab";
        case 139: return data == 1 ? "mossy_cobblestone_wall" : "cobblestone_wall";
        case 155: return data == 1 ? "chiseled_quartz_block" : data >= 2 ? "quartz_pillar" : "quartz_block";
        case 159: return dyed("terracotta");
        case 160: return dyed("stained_glass_pane");
        case 161: return (data & 1) ? "dark_oak_leaves" : "acacia_leaves";
        case 162: return (data & 1) ? "dark_oak_log" : "acacia_log";
        case 168: return data == 1 ? "prismarine_bricks" : data == 2 ? "dark_prismarine" : "prismarine";
        case 171: return dyed("carpet");
        case 175: {
            if (data & 8) return "tall_grass";  // upper half: plant type is stored in the lower half
            static const char* const s[] = {"sunflower", "lilac", "tall_grass", "large_fern", "rose_bush", "peony", "tall_grass", "tall_grass"};
            return s[data & 7];
        }
        case 179: return data == 1 ? "chiseled_red_sandstone" : data == 2 ? "cut_red_sandstone" : "red_sandstone";
        case 251: return dyed("concrete");
        case 252: return dyed("concrete_powder");
        default: return kNames[id];
    }
}

}  // namespace vox
