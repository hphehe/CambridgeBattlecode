// the draft plan: map analysis done once by the draft bot (first call has ~10x fuel) and handed to the
// champions through the 1024-byte draft memory.
// Layout (all single bytes):
//   [0] 0xC1  [1] version  [2] map flags  [3] group count  [4] team size
//   [5 + champion_type]   opening group + 1 for that champion (0 = none), 12 entries
//   [20 + 4*g + 0..3]     group g: path steps from our spawn, from the enemy spawn, to an altar-adjacent tile
//                         (255 = unreachable or more), and side (0 ours, 1 middle, 2 theirs)
//   [19] 1 = the farming race ran   [300 + 4*g + 0..3] group g's enemy mask from it (little-endian u32)
#pragma once
#include "../game/world.h"

enum { MF_GOLD_ONLY = 1, MF_BOSSES = 2, MF_ONE_ALTAR = 4, MF_SMALL = 8, MF_TEAM = 32, MF_CALT = 64 };
static constexpr uint8_t PLAN_MAGIC = 0xC1, PLAN_VERSION = 1;
static constexpr int PLAN_GROUPS = 60, PLAN_BASE = 20, PRE_BASE = 300;
struct GI { int dh, da, dl, side, gems, gold; float hp, str; bool mono; };

#include "opening.h"
#include "presim.h"

// Draft side: analyse the map for the line-up we are about to pick and submit the plan.
static void write_plan(const std::vector<CD>& lineup) {
    W = bc::map::width(); H = bc::map::height(); N = W * H;
    const auto& own = bc::map::spawn_region(team::ally);
    const auto& en = bc::map::spawn_region(team::enemy);
    STATIC_BB = bc::map::walls() | bc::map::holes() | bc::map::altars() | en;
    BLOCK = unpack(STATIC_BB);
    const auto altar_tiles = bc::map::altars().positions();
    bc::bitboard er, ar;
    for (auto s : en.positions())
        for (auto d : DIRS) { const vec2 q = s + d; if (inb(q) && !BLOCK[idx(q)]) er.set(q, true); }
    for (auto a : altar_tiles)
        for (auto d : DIRS) { const vec2 q = a + d; if (inb(q) && !BLOCK[idx(q)]) ar.set(q, true); }
    const Field fh = make_field(STATIC_BB, own), fa = make_field(STATIC_BB, er), fl = make_field(STATIC_BB, ar);

    std::array<uint8_t, 1024> b{};
    b[0] = PLAN_MAGIC; b[1] = PLAN_VERSION;
    const auto groups = bc::map::camp_groups();
    const int n = std::min<int>((int)groups.size(), PLAN_GROUPS);
    b[3] = (uint8_t)n;
    b[4] = (uint8_t)bc::draft::team_size();

    std::vector<GI> info(n);
    float all_gems = 0, mono_gems = 0;
    bool any_gem = false;
    for (int i = 0; i < n; ++i) {
        GI& x = info[i];
        x = {9999, 9999, 9999, 1, (int)groups[i].gem_value(), (int)groups[i].gold_value(), 0, 0, false};
        for (const auto& c : groups[i]) {
            const vec2 p = c.spawn_position();
            const int k = kind_of(c);
            float khp, kstr;
            camp_stats_of(c, k, khp, kstr);
            x.hp += khp; x.str += kstr;
            if (k == K_MONO) x.mono = true;
            if (fh.at(p) != UNREACH) x.dh = std::min<int>(x.dh, fh.at(p));
            if (fa.at(p) != UNREACH) x.da = std::min<int>(x.da, fa.at(p) + 1);
            if (fl.at(p) != UNREACH) x.dl = std::min<int>(x.dl, fl.at(p) + 1);
        }
        x.side = x.dh * 10 < x.da * 7 ? 0 : x.da * 10 < x.dh * 7 ? 2 : 1;
        all_gems += x.gems;
        if (x.mono) mono_gems += x.gems; else if (x.gems > 0) any_gem = true;
        uint8_t* e = &b[PLAN_BASE + 4 * i];
        e[0] = (uint8_t)std::min(x.dh, 255); e[1] = (uint8_t)std::min(x.da, 255); e[2] = (uint8_t)std::min(x.dl, 255);
        e[3] = (uint8_t)x.side;
    }
    uint8_t flags = 0;
    if (!any_gem) flags |= MF_GOLD_ONLY;
    if (all_gems > 0 && mono_gems >= 0.4f * all_gems) flags |= MF_BOSSES;
    if (altar_tiles.size() == 1) flags |= MF_ONE_ALTAR;
    int spawn_gap = 9999;
    for (auto s : own.positions()) if (fa.at(s) != UNREACH) spawn_gap = std::min<int>(spawn_gap, fa.at(s));
    if (spawn_gap < 60) flags |= MF_SMALL;
    // CONTESTED_BANK_GEMS: our nearest reachable altar is about as far from the enemy spawn as from ours (a shared, central altar)
    int bh = 9999, ba = 9999;
    for (auto a : altar_tiles) {
        int dh = 9999, da = 9999;
        for (auto d : DIRS) {
            const vec2 q = a + d;
            if (!inb(q) || BLOCK[idx(q)]) continue;
            if (fh.at(q) != UNREACH) dh = std::min<int>(dh, fh.at(q));
            if (fa.at(q) != UNREACH) da = std::min<int>(da, fa.at(q) + 1);
        }
        if (dh < bh) { bh = dh; ba = da; }
    }
    if (bh < 9999 && ba < 9999 && bh >= CONTESTED_ALTAR_RATIO * ba) flags |= MF_CALT;
    // Opening jobs: each body gets its own nearby group that it can clear alone.
    std::vector<bool> used(n, false);
    std::vector<CT> bodies;
    for (auto c : lineup) {
        CT b2[2]; const int nb2 = draft_bodies(c, b2);
        for (int k = 0; k < nb2; ++k) bodies.push_back(b2[k]);
    }
    // Team-camp map: most of the gem rate sits in groups that none of our bodies can clear alone at the
    // start (the same soak test as the solo openings below), so single champions only wait for each other there.
    if (any_gem && !(flags & MF_BOSSES) && bodies.size() >= 3) {
        float rate = 0, multi = 0;
        for (int i = 0; i < n; ++i) {
            const GI& x = info[i];
            if (x.mono || x.gems <= 0 || x.dh >= 9999) continue;
            const float g = x.gems / (float)std::max(1, (int)groups[i].max_respawn_timer());
            rate += g;
            bool solo = x.str <= 0;
            for (CT t : bodies) {
                if (solo) continue;
                const float dps = base_dps(t) * (1.f);
                const float clear = x.hp / std::max(10.f, dps);
                if (x.str * 0.75f * clear <= bc::champion(t).base_stats().base_health * 0.8f) solo = true;
            }
            if (!solo) multi += g;
        }
        if (rate > 0 && multi >= TEAM_CAMP_RATE_SHARE * rate) flags |= MF_TEAM;
        // the party size the gem rate typically needs (rate-weighted median over the gem groups), for the
        // average body of our line-up at base stats (the same soak test as above)
        float ah = 0, ad = 0; int nb = 0;
        for (CT t : bodies) {
            ah += bc::champion(t).base_stats().base_health; ad += base_dps(t) * (1.f); ++nb;
        }
        if (nb > 0 && rate > 0) {
            ah /= nb; ad /= nb;
            std::vector<std::pair<int, float>> ks;
            for (int i = 0; i < n; ++i) {
                const GI& x = info[i];
                if (x.mono || x.gems <= 0 || x.dh >= 9999) continue;
                int nm = 0;
                for (const auto& c : groups[i]) { (void)c; ++nm; }
                int k = 1;
                while (k < 6) {
                    const float clear = x.hp / (k * std::max(10.f, ad));
                    if (x.str * 0.75f * clear / std::max(1, std::min(k, nm)) <= ah * 0.8f) break;
                    ++k;
                }
                ks.push_back({k, x.gems / (float)std::max(1, (int)groups[i].max_respawn_timer())});
            }
            std::sort(ks.begin(), ks.end());
            float acc = 0; int med = 1;
            for (auto& kv : ks) { acc += kv.second; if (acc >= rate * 0.5f) { med = kv.first; break; } }
            b[18] = (uint8_t)med;
        }
    }
    b[2] = flags;
    // the median value (gems + GEMS_PER_GOLD * gold) of the ordinary (non-monolith) gem groups: a gold-only group must be
    // worth at least that much to be an opening job
    // (a plain counting loop on the stack: no new allocation or template instance, which moved game-time fuel)
    float og_med = 0;
    {
        float vs[PLAN_GROUPS]; int nv = 0;
        for (int i = 0; i < n; ++i) if (!info[i].mono && info[i].gems > 0) vs[nv++] = info[i].gems + GEMS_PER_GOLD * info[i].gold;
        for (int a = 0; a < nv; ++a) {
            int lo = 0, le = 0;
            for (int c = 0; c < nv; ++c) { lo += vs[c] < vs[a]; le += vs[c] <= vs[a]; }
            if (lo <= nv / 2 && nv / 2 < le) { og_med = vs[a]; break; }
        }
    }
    // tm 1: the mirror line-up at the enemy spawn (its own side and spawn distances), for the farming race
    int open1[12]; std::fill(open1, open1 + 12, -1);
    for (int tm = 0; tm < 2; ++tm) {
    std::fill(used.begin(), used.end(), false);
    for (CT t : bodies) {
        const auto st = bc::champion(t).base_stats();
        const float dps = base_dps(t) * (1.f);   // Issac has Cassi with him
        int best = -1; float bs = 0;
        for (int i = 0; i < n; ++i) {
            GI x = info[i];
            if (tm == 1) { std::swap(x.dh, x.da); x.side = 2 - x.side; }
            if (used[i] || x.mono || x.dh >= 9999) continue;
            // a gold-only group the opening may take (only on maps with gem groups: without them c28 already allows it)
            const bool og_ok = any_gem && x.gems <= 0 && (x.str <= 0) &&
                               (GEMS_PER_GOLD * x.gold * champ(t).gold_bonus * grole_w(t, 0) >= og_med);
            if (any_gem && x.gems <= 0 && !og_ok) continue;
            if (x.side > (og_ok ? 1 : 0)) continue;
            const float clear = x.hp / std::max(10.f, dps);
            if (x.str * 0.75f * clear > st.base_health * 0.8f) continue;   // too much for one body at the start
            const float score = grole_value(t, 0, (float)x.gems, (float)x.gold, GEMS_PER_GOLD) / (x.dh + clear + 7.f);
            if (score > bs) { bs = score; best = i; }
        }
        if (tm == 1) { open1[(int)t] = best; if (best >= 0) used[best] = true; continue; }
        if (best < 0) continue;
        used[best] = true;
        b[5 + (int)t] = (uint8_t)(best + 1);
    }
    }
    {
        std::vector<vec2> h0, h1;
        for (auto q : own.positions()) h0.push_back(q);
        for (auto q : er.positions()) h1.push_back(q);
        pre_sim(b, info, groups, n, bodies, fh, fa, fl, h0, h1, open1);
    }
    bc::draft_memory = b;
}

// Champion side.
static bool PLAN_OK = false;
static uint8_t PLAN_FLAGS = 0;
static int OPEN_G = -1;
static bool GEO_OK = false;
static std::vector<uint8_t> GEO_NB;   // per group, GEO_K x (neighbour id + 1, steps) from the draft (presim.h geo_table)

static void read_plan() {
    const auto& dm = bc::draft_memory;
    if (dm.size() < 300) return;
    const auto byte = [&](int i) { return (int)std::to_integer<uint8_t>(dm[i]); };
    if (byte(0) != PLAN_MAGIC || byte(1) != PLAN_VERSION) return;
    const int n = byte(3);
    if (n != (int)G.size() && !(G.size() > (size_t)PLAN_GROUPS && n == PLAN_GROUPS)) return;
    PLAN_OK = true;
    PLAN_FLAGS = (uint8_t)byte(2);
    GT_PARTY = byte(18);
    for (int i = 0; i < n; ++i) {
        const int dh = byte(PLAN_BASE + 4 * i), da = byte(PLAN_BASE + 4 * i + 1), dl = byte(PLAN_BASE + 4 * i + 2);
        if (dh < 255) G[i].d_home = dh;
        if (da < 255) G[i].d_away = da;
        if (dl < 255) G[i].d_altar = dl;
    }
    if (byte(19) == 1 && dm.size() >= (size_t)(PRE_BASE + 4 * n)) {
        PRE_MASK.assign(G.size(), 0u);
        for (int i = 0; i < n; ++i)
            for (int x = 0; x < 4; ++x) PRE_MASK[i] |= (uint32_t)byte(PRE_BASE + 4 * i + x) << (8 * x);
    }
    // bank engine: the draft sim's enemy traffic per altar (presim.h)
    if (byte(19) == 1 && dm.size() > (size_t)(DRAFT_ALTAR_BYTES + 25) && byte(DRAFT_ALTAR_BYTES + 25) == 1) {
        BE_ALT.clear();
        const int na = std::min(8, byte(DRAFT_ALTAR_BYTES + 24));
        for (int a = 0; a < na; ++a) {
            vec2 p{0, 0}; p.x = byte(DRAFT_ALTAR_BYTES + 3 * a); p.y = byte(DRAFT_ALTAR_BYTES + 3 * a + 1);
            BE_ALT.push_back({p, byte(DRAFT_ALTAR_BYTES + 3 * a + 2)});
        }
    }
    if (dm.size() >= (size_t)(GEO_BASE + 2 + 8 * GEO_MAXG) && byte(GEO_BASE) == GEO_MAGIC && byte(GEO_BASE + 1) == GEO_K) {
        GEO_NB.assign(G.size() * 2 * GEO_K, 0);
        const int m = std::min(n, GEO_MAXG);
        for (int j = 0; j < m; ++j)
            for (int q = 0; q < 2 * GEO_K; ++q) GEO_NB[(size_t)j * 2 * GEO_K + q] = (uint8_t)byte(GEO_BASE + 2 + 8 * j + q);
        GEO_OK = true;
    }
    const int og = byte(5 + (int)ME.type) - 1;
    if (og >= 0 && og < n) OPEN_G = og;
    read_opening();
}
