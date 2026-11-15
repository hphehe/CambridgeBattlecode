// Shared basics: the grid, bitboards and distance fields.
// The whole bot is one translation unit; every champion (and the draft) runs its own instance.
#pragma once
#include <cambc.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <variant>
#include <vector>

namespace bc = cambc;
using bc::team;

// A member function does not get the compiler's inlining of a function with one caller, and out of line a step keeps its
// values in memory: about 4% of a job scan, which the fuel floor then takes from the scan (about 300 gold a game on 2 seeds).
#define ALWAYS_INLINE [[gnu::always_inline]] inline

// how far a basic attack reaches
ALWAYS_INLINE constexpr int attack_reach(bool melee) { return melee ? 1 : 3; }
// rounds from the first body on a reward tile to the payout
ALWAYS_INLINE int capture_rounds() { return (int)bc::constants::capture_time + 1; }
// a recall, a tribute or a crystal: a channel ends if we act
ALWAYS_INLINE bool is_channelling(const bc::champion_state& st) {
    return st.contains(bc::champion_state::recalling) || st.contains(bc::champion_state::submitting_gems) ||
           st.contains(bc::champion_state::teleporting);
}
using bc::vec2;
using CT = bc::champion_type;
using CD = bc::champion_draft;

// ---------------------------------------------------------------------------------------------
// Grid
// ---------------------------------------------------------------------------------------------
static int W = 0, H = 0, N = 0;
static inline int idx(vec2 p) { return p.y * W + p.x; }
static inline bool inb(vec2 p) { return p.x >= 0 && p.y >= 0 && p.x < W && p.y < H; }
static inline int cheb(vec2 a, vec2 b) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }
static const vec2 DIRS[8] = {vec2::north, vec2::north_east, vec2::east, vec2::south_east,
                             vec2::south, vec2::south_west, vec2::west, vec2::north_west};
static constexpr uint16_t UNREACH = bc::distance_field::unreachable;

static int ROUND = 0;            // cached round number for this turn
static inline int rnd() { return ROUND; }
static int FORCE_FLEE_UNTIL = -1;   // we just broke off a tribute under fire: flee until this round

// A bitboard exported to local memory: packed row-major bits, least significant first.
struct Bits {
    std::vector<uint8_t> b;
    bool operator[](int i) const { return i >= 0 && (size_t)(i >> 3) < b.size() && ((b[i >> 3] >> (i & 7)) & 1); }
};
static Bits unpack(const bc::bitboard& bb) { Bits r; r.b = bb.export_linear_buffer(); return r; }

// ---------------------------------------------------------------------------------------------
// Distance fields, exported to local memory (a get() through the SDK costs ~120 fuel).
// ---------------------------------------------------------------------------------------------
struct Field {
    std::vector<uint16_t> d;
    std::optional<bc::bitboard> roots;
    bool ok() const { return !d.empty(); }
    uint16_t at(vec2 p) const { return inb(p) && !d.empty() ? d[idx(p)] : UNREACH; }
};

static bc::bitboard STATIC_BB;   // walls | holes | altars | enemy spawn: never walkable
static Bits BLOCK;               // same, exported
static Bits BUSH;                // bush tiles (they hide what stands in them from anyone outside)

// A dash (2 steps) may pass over holes and altars, never walls (rules 04). For a dash champion,
// holes that join two walkable tiles 2 apart with no walk of 2 between them ("bridge holes") are walkable in our
// distance fields (cost 2 = the dash's two steps; cooldown ignored) and dash_along may cross them. The other
// dash searches (flee, micro, fight) may also pass over holes and altars.
static bool HD_ON = false;       // this champion dashes
static int HD_N = 0;             // bridge holes on this map
static bc::bitboard HD_CLR;      // the bridge holes
static Bits HD_PASS;             // holes and altars: a dash may pass over them
// the 8 ring tiles around a hole (bridge test in world.h)
static constexpr int HD_OX[8] = {-1, 0, 1, -1, 1, -1, 0, 1}, HD_OY[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
// a dash's middle tile: open, or a hole or an altar.
static inline bool dash_mid_ok(vec2 a) {
    if (!inb(a)) return false;
    if (!BLOCK[idx(a)]) return true;
    return HD_ON && HD_PASS[idx(a)];
}

static Field make_field(const bc::bitboard& walls, const bc::bitboard& roots, uint16_t maxd = UNREACH) {
    bc::distance_field f;
    bc::bitboard visited;
    if (HD_ON && HD_N > 0) f.compute(walls & ~HD_CLR, roots, visited, maxd);
    else f.compute(walls, roots, visited, maxd);
    Field r;
    r.d = f.export_linear_buffer();
    r.roots = roots;
    return r;
}
