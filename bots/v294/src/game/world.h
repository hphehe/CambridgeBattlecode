// static map model, camp-group knowledge and the per-turn observation.
#pragma once
#include "../common.h"
#include "champions.h"

// ---------------------------------------------------------------------------------------------
// Static map
// ---------------------------------------------------------------------------------------------
static Bits OWN_SPAWN, EN_SPAWN;
static int OSP_X0 = 1 << 20, OSP_Y0 = 1 << 20, OSP_X1 = -(1 << 20), OSP_Y1 = -(1 << 20);   // own spawn bounding box
static bool ANY_BUSH = false;
static std::vector<vec2> own_spawn, en_spawn, altars;
static vec2 away_c{0, 0};
static Field F_HOME, F_AWAY, F_ALTAR;   // distance to our spawn, to the enemy spawn's edge, to an altar-adjacent tile
static std::vector<std::pair<vec2, int>> BE_ALT;   // bank engine: the draft sim's enemy traffic per altar (x100)
static int be_traffic(vec2 a) { for (auto& e : BE_ALT) if (e.first == a) return e.second; return -1; }

// ---------------------------------------------------------------------------------------------
// Camp groups
// ---------------------------------------------------------------------------------------------
enum { ST_ALIVE = 0, ST_REWARD = 1, ST_RESPAWN = 2 };
enum { K_WOLF = 0, K_ELK, K_GEM, K_GOLD, K_MONO, K_GOLEM, K_N };

struct Mon {
    const bc::camp* c = nullptr;
    vec2 p{0, 0};
    int kind = 0;
    float mhp = 0, str = 0, hp = 0;
    int reach = 3;      // attack range from the engine
    bool vis = false;   // visible and alive this turn
    bool unseen = true; // its tile is out of our view (a bush, a wall, too far) at the last look
};

struct Group {
    int id = 0, n = 0;
    std::vector<Mon> m;   // its members, as many as the map has (was a fixed 40)
    vec2 center{0, 0};
    int gems = 0, gold = 0, respawn = 0;
    float hp_total = 0, str_total = 0;
    bool monolith = false;
    // knowledge
    int st = ST_ALIVE;
    int ready_at = 0;       // ST_RESPAWN: round alive again
    int seen_at = -1000;    // last round any spawn tile was in view
    int state_at = -1000;   // last round state() was read
    float hp_left = 0;      // ST_ALIVE: remaining monster health
    int enemy_at = -1000;   // last round an enemy champion was seen within 7
    int cap_us_at = -1000;   // round of our last capture of it I know of (an ally's claim paid, or my own payout)
    int cap_them_at = -1000; // round of the enemy's last capture of it I know of (seen respawning, not ours)
    int cap_claim_at = -1000;  // last round one of us was capturing it (an ally's claim flag, or my own capture)
    int vis_now = 0;        // spawn tiles in view this turn
    int fid = -1;           // field index
    int d_home = 999, d_away = 999, d_altar = 999;
    int awake_at = -1000;   // last round we saw it hurt (taking damage) - awake monsters hit
    int kmin = 1;           // champions needed (my estimate at the last plan)
    int skip_until = 0;     // gave up gathering a team for it
    int gather_since = -1;
    float p_wait = 0, p_clear = 0, p_value = 0;   // from the last scoring pass
};

static std::optional<std::vector<bc::map::camp_group>> GROUPS;
static std::vector<int> G_RAD;   // largest member distance from the centre, per group
static bool KNOW_CHANGED = true;   // some group's state changed this turn: re-plan
static bool ANY_GEM_GROUP = false;
static int DOM_G = -1;
static int GT_PARTY = 0;      // party size from the draft plan (0: none)
// the draft's farming race (plan.h pre_sim). Bit b of a group's mask: an enemy body predicted at it in rounds
// 10b..10b+9 of the opening (empty: no race)
static std::vector<uint32_t> PRE_MASK;
static constexpr int PRE_T = 320;
static inline bool pre_enemy(int gid, int at) {
    if (gid < 0 || gid >= (int)PRE_MASK.size() || at < 0 || at >= PRE_T + 10) return false;
    const int b = at / 10;
    uint32_t m = 0;
    for (int x = std::max(0, b - 1); x <= std::min(31, b + 1); ++x) m |= 1u << x;   // one bucket of slack each way
    return (PRE_MASK[gid] & m) != 0;
}
static float MONO_SHARE = 0.f;   // share of the map's gems in monolith groups
static float ORD_RATE = 0.f;     // gems per round of the map's ordinary gem groups (gems / (respawn + 8), summed)
static inline bool MFG_MAP() {
    return MONO_SHARE > 0.f && MONO_SHARE < MONOLITH_ONLY_SHARE && (ORD_RATE <= MONOLITH_MAX_ORDINARY_RATE);
}
// no rally beacon on maps whose gems all sit in monoliths, or (MFG_MAP) whose ordinary gem rate is small
static inline bool RALLY_ON() { return !(MONO_SHARE >= MONOLITH_ONLY_SHARE); }
static std::vector<Group> G;
static std::vector<Field> GF;     // fields rooted at each group's spawn tiles
static std::vector<int> group_at; // tile -> group id or -1 (spawn tiles)

static int kind_of(const bc::camp& c) {
    // the Golem (2026-10-19 patch) is its own kind; before, it fell through to the monolith
    if (c.is_golem()) return K_GOLEM;
    return c.is_wolf() ? K_WOLF : c.is_elk() ? K_ELK : c.is_gem_cluster() ? K_GEM : c.is_gold_cluster() ? K_GOLD : K_MONO;
}
// pre-patch table (clusters were 500): the fallback if the engine query fails
static float kind_hp(int k) { return k == K_WOLF ? 1350 : k == K_ELK ? 2400 : k == K_MONO ? 20000 : 500; }
static float kind_str(int k) { return k == K_WOLF ? 20 : k == K_ELK ? 60 : k == K_MONO ? 200 : 0; }
// a camp's health and strength from the engine's design stats (asked once per kind), so a balance patch
// cannot leave us with stale numbers (after 2026-10-19 the table above made a 120-hp cluster 500)
static float KIND_HP[K_N], KIND_STR[K_N];
static int KIND_REACH[K_N];
static bool KIND_OK[K_N];
static void camp_stats_of(const bc::camp& c, int k, float& hp, float& str) {
    if (!KIND_OK[k]) {
        KIND_OK[k] = true;
        KIND_HP[k] = kind_hp(k); KIND_STR[k] = kind_str(k);
        if (k == K_GOLEM) { KIND_HP[k] = 5000; KIND_STR[k] = 120; }
        // fallback: the 2026-10-19 patch values (verified in replays: elk hits at 4, monolith at 5)
        KIND_REACH[k] = k == K_ELK || k == K_GOLEM ? 4 : k == K_MONO ? 5 : 3;
        try {
            const auto b = c.base_stats();
            if (b.base_health > 0) { KIND_HP[k] = b.base_health; KIND_STR[k] = b.base_strength; }
            if (b.base_attack_range > 0) KIND_REACH[k] = b.base_attack_range;
        } catch (...) {}
    }
    hp = KIND_HP[k]; str = KIND_STR[k];
}

static bool ensure_field(Group& g, long budget_left) {
    if (g.fid >= 0) return true;
    // a field costs ~6 fuel per tile (57k on 100x100), more than the usual 40k floor: it ran out mid-build
    budget_left = std::max(budget_left, 6L * N + 8000L);
    if ((long)bc::fuel_remaining() < budget_left) return false;
    bc::bitboard roots;
    for (int k = 0; k < g.n; ++k) roots.set(g.m[k].p, true);
    GF.push_back(make_field(STATIC_BB, roots));
    g.fid = (int)GF.size() - 1;
    // refine static distances now that we have a real field
    const Field& f = GF[g.fid];
    int dh = UNREACH;
    for (auto p : own_spawn) dh = std::min<int>(dh, f.at(p));
    g.d_home = dh;
    return true;
}
static const Field* gfield(const Group& g) { return g.fid >= 0 ? &GF[g.fid] : nullptr; }

// Distance from p to group g's spawn tiles (exact if its field exists, estimate otherwise).
static int gdist(const Group& g, vec2 p) {
    if (g.fid >= 0) { const int d = GF[g.fid].at(p); return d == UNREACH ? 999 : d; }
    int best = 999;
    for (int k = 0; k < g.n; ++k) best = std::min(best, cheb(g.m[k].p, p));
    return best + best / 8;
}

// ---------------------------------------------------------------------------------------------
// Champions
// ---------------------------------------------------------------------------------------------
// What one enemy can do before our next turn (micro_rules.h enemy_power).
struct EnemyPower {
    int turns = 1;            // its turns before our next one, after a stun takes one away
    float hit = 0.f;          // one basic attack, with the Heart of Ruin bonus
    int reach = 2;            // a walk and a hit: melee 2; ranged 3
    bool dash_ready = false;  // off cooldown on the turn it could use it
    float spell = 0.f, area = 0.f;   // damage of the spells it can really cast, 0 on cooldown
    // the worst it can land on a champion d tiles away (0..8; further: nothing), and the same table as runs of equal
    // values for the SIMD tile scores: run r holds the distances up to run_hi[r] not in an earlier run
    float at[9] = {};
    int runs = 0, run_hi[9];
    float run_v[9];
};
struct Champ {
    bc::champion h;
    CT type = CT::varg;
    bool ally = false;
    int slot = 0;
    bool vis = false, alive = true;
    vec2 p{-1, -1};
    float hp = 0, mhp = 1, str = 0, focus = 0;
    int gems = 0;
    bool stunned = false;    // it cannot act now. Read by read_ecd, once a round: call it first
    bool loses_turn = false; // a stun still has a turn to take from it (same read); stunned stays up after that turn
    bool channel = false, tribute = false;   // tribute: the channel is a gem submission
    EnemyPower power;        // enemy_power's cache
    int power_key = -1;      // ... good for this round and this DANGER
    int chan_rem = -1;       // rounds left on its channel at our last read (-1: none or unknown)
    bool chan_old = false;   // ... it went down by one since our previous turn: a channel kept up, not one restarted this turn
    int seen_at = -1000;
    vec2 last_p{-1, -1};
    bool hurt = false;       // lost health since our read in the previous round
    int so_until = -1;       // ignored (unless close) until this round after a stalemate with it
    int ecd[2] = {-1, -1};   // enemy ability cooldowns (-1 unknown), read by the micro in round ecd_at
    int ecd_at = -1000;
    int prio = 0;            // scheduling priority (higher acts first in its team), read while visible: this round
    int prio_n = 0;          // ... and the next round (what it asked for so far)
    int prio_at = -1000;     // the round of that read
    bool prio_known = false; // prio was read at least once
    bool type_known = true;
    float shards = -1;       // shards bought, estimated from the stats at the last sighting (-1 unknown)
    explicit Champ(const bc::champion& c) : h(c) {}
};
static std::vector<Champ> ALLY, ENEMY;
// Heart of Ruin's passive, all its damage x (1 + 0.5 x missing health / max health); 1 for everyone else
// a channelling enemy our danger checks may skip: never (1), or one whose channel kept running since our last turn (2)
static bool chan_ignored(const Champ& c) { return c.channel && c.chan_old; }
static bool is_hor(const Champ& c) { return champ(c.type).missing_hp; }
static float hor_mult(const Champ& c) { return 1.f + 0.5f * std::max(0.f, std::min(1.f, 1.f - c.hp / std::max(1.f, c.mhp))); }
static int ALIVE_ALLIES = 1;   // living bodies on our side, us included (refreshed when planning)
static int MY_SLOT = 0;
static bool enemies_loaded = false;

// ---------------------------------------------------------------------------------------------
// Me
// ---------------------------------------------------------------------------------------------
struct Ab {
    bool reset = false, dmg = false, aoe = false, cc = false, dash = false, shield = false, heal = false;
    bool ground = false, camps = false, enemies = false, excl = false;
    float range = 0;
    float land = 0;      // a dash whose landing deals damage (Focus x land, radius 1): Heart of Ruin
};
struct Me {
    CT type = CT::varg;
    bool melee = true;
    vec2 p{0, 0};
    float hp = 1, mhp = 1, str = 0, focus = 0;
    int gems = 0, gold = 0;
    uint32_t state = 0;
    bool can_move = false, can_attack = false, can_cast = false;
    bool walked = false, cast_excl = false;
    int cd[2] = {0, 0};
    Ab ab[2];
    float g_str = 5, g_focus = 10, g_hp = 100;   // growth per shard
    float hp_prev = -1;
    bool hurt = false;   // lost health since our previous turn
    float drop = 0;      // health lost since our previous turn
};
static Me ME;
static int LAST_CHAMP_HIT = -1000;   // last round we hit an enemy champion (attack or spell)
static int LAST_CAMP_HIT = -1000;    // last round we hit a camp (attack or targeted spell)

static void refresh_flags() {
    const auto st = bc::self().state();
    ME.state = (uint32_t)st.bits();
    ME.can_move = st.contains(bc::champion_state::can_move);
    ME.can_attack = st.contains(bc::champion_state::can_attack);
    ME.can_cast = st.contains(bc::champion_state::can_cast);
}
static bool ready(int s) { return ME.cd[s] == 0 && ME.can_cast; }
// my own Heart of Ruin passive (1 + 0.5 x missing health fraction; rules 08-champions: basic attacks and abilities).
// Enemies and allies were already counted with it (hor_mult); the engine's ability damage_amount() already includes it,
// so only the basic attack (ME.str) and the table dps estimates need it.
static float my_mult() { return champ(ME.type).missing_hp ? 1.f + 0.5f * std::max(0.f, std::min(1.f, 1.f - ME.hp / std::max(1.f, ME.mhp))) : 1.f; }
static float my_str() { return ME.str * my_mult(); }

// ---------------------------------------------------------------------------------------------
// Initialisation (first play turn has double fuel)
// ---------------------------------------------------------------------------------------------
static void init_static() {
    W = bc::map::width(); H = bc::map::height(); N = W * H;
    const auto& own = bc::map::spawn_region(team::ally);
    const auto& en = bc::map::spawn_region(team::enemy);
    STATIC_BB = bc::map::walls() | bc::map::holes() | bc::map::altars() | en;
    BLOCK = unpack(STATIC_BB);
    {
        const CT t = bc::self().champion_name();
        HD_ON = has_dash(t);
        if (HD_ON) {
            const auto hb = bc::map::holes();
            HD_PASS = unpack(hb | bc::map::altars());
            // The bridge test as bitboard ops (a loop over every hole with a ring of 8 reads burned the first
            // 6 turns of each dash champion on hole maps). Ring tiles i, j two apart, both open, and no open ring tile
            // next to both. ring[k] at a hole = the open state of the tile at offset k (a mirror of the shift sign gives
            // the same set, the rule is symmetric)
            const bc::bitboard op = ~STATIC_BB;
            bc::bitboard ring[8];
            for (int k = 0; k < 8; ++k) ring[k] = op.shift_right(-HD_OX[k]).shift_up(-HD_OY[k]);
            bc::bitboard br;
            for (int i = 0; i < 8; ++i)
                for (int j = i + 1; j < 8; ++j) {
                    if (std::max(std::abs(HD_OX[i] - HD_OX[j]), std::abs(HD_OY[i] - HD_OY[j])) != 2) continue;
                    bc::bitboard c = ring[i] & ring[j];
                    for (int k = 0; k < 8; ++k)
                        if (k != i && k != j && std::max(std::abs(HD_OX[k] - HD_OX[i]), std::abs(HD_OY[k] - HD_OY[i])) <= 1 &&
                            std::max(std::abs(HD_OX[k] - HD_OX[j]), std::abs(HD_OY[k] - HD_OY[j])) <= 1) c = c & ~ring[k];
                    br = br | c;
                }
            HD_CLR = hb & br;
            HD_N = (int)HD_CLR.count();
        }
    }
    BUSH = unpack(bc::map::bushes());
    OWN_SPAWN = unpack(own);
    EN_SPAWN = unpack(en);
    const auto bb = bc::map::bushes(); ANY_BUSH = !bb.positions().empty(); if (ANY_BUSH) BUSH = unpack(bb);
    own_spawn = own.positions();
    for (auto q : own_spawn) { OSP_X0 = std::min(OSP_X0, q.x); OSP_Y0 = std::min(OSP_Y0, q.y); OSP_X1 = std::max(OSP_X1, q.x); OSP_Y1 = std::max(OSP_Y1, q.y); }
    en_spawn = en.positions();
    altars = bc::map::altars().positions();
    long ex = 0, ey = 0;
    for (auto p : en_spawn) { ex += p.x; ey += p.y; }
    if (!en_spawn.empty()) away_c = {(int)(ex / (long)en_spawn.size()), (int)(ey / (long)en_spawn.size())};

    F_HOME = make_field(STATIC_BB, own);
    GROUPS = bc::map::camp_groups();
    group_at.assign(N, -1);
    G.resize(GROUPS->size());
    int id = 0;
    for (const auto& cg : *GROUPS) {
        Group& g = G[id];
        g.id = id;
        long cx = 0, cy = 0;
        { int cnt = 0; for (const auto& c : cg) { (void)c; ++cnt; } g.m.assign((size_t)cnt, Mon{}); g.n = 0; }
        for (const auto& c : cg) {
            Mon& m = g.m[g.n++];
            m.c = &c;
            m.p = c.spawn_position();
            m.kind = kind_of(c);
            camp_stats_of(c, m.kind, m.mhp, m.str);
            m.reach = KIND_REACH[m.kind];
            m.hp = m.mhp;
            g.hp_total += m.mhp;
            g.str_total += m.str;
            if (m.kind == K_MONO) g.monolith = true;
            cx += m.p.x; cy += m.p.y;
            group_at[idx(m.p)] = id;
        }
        g.center = {(int)(cx / std::max(1, g.n)), (int)(cy / std::max(1, g.n))};
        {
            int rad = 0;
            for (int k = 0; k < g.n; ++k) rad = std::max(rad, cheb(g.m[k].p, g.center));
            if ((int)G_RAD.size() < (int)G.size()) G_RAD.assign(G.size(), 0);
            G_RAD[id] = rad;
        }
        g.gems = (int)cg.gem_value();
        g.gold = (int)cg.gold_value();
        g.respawn = cg.max_respawn_timer();
        g.hp_left = g.hp_total;
        const int dh = F_HOME.at(g.m[0].p);
        g.d_home = dh == UNREACH ? 999 : dh;
        g.d_away = cheb(away_c, g.center) + 2;
        g.d_altar = 12;
        if (g.gems > 0 && !g.monolith) ANY_GEM_GROUP = true;
        ++id;
    }
    {
        float mono = 0, all = 0;
        for (const auto& g : G) { all += g.gems; if (g.monolith) mono += g.gems; }
        MONO_SHARE = all > 0 ? mono / all : 0.f;
        // a small ordinary gem rate counts as an all-monolith map (MONO_SHARE 1: no rally beacon, RALLY_ON)
        // (a group respawns within 60 rounds, so a rate <= MONOLITH_MAX_ORDINARY_RATE needs ordinary gems <= 68 x MONOLITH_MAX_ORDINARY_RATE)
        if (MONO_SHARE > 0.f && MONO_SHARE < MONOLITH_ONLY_SHARE && all - mono <= 68.f * MONOLITH_MAX_ORDINARY_RATE) {
            for (const auto& g : G) if (g.gems > 0 && !g.monolith) ORD_RATE += g.gems / (float)(g.respawn + 8);
            if (MFG_MAP()) { MONO_SHARE = 1.f; }
        }
    }
    {
        int tot = 0, bi = -1, bv = 0;
        for (const auto& g : G) if (!g.monolith) { tot += g.gems; if (g.gems > bv) { bv = g.gems; bi = g.id; } }
        if (bi >= 0 && tot > 0 && bv * 2 >= tot && G[bi].str_total > 0 && G[bi].n >= 4) DOM_G = bi;
    }
}

// altar-adjacent walkable tiles that hold a camp, as (group, monster). Bit k of ALT_KEY_LIVE: that camp is
// alive as far as we know, so its tile is not a tribute tile (a monster stands on it; a bank walk there waits in its reach).
static std::vector<std::pair<int, int>> ALT_KEY;
static uint32_t ALT_KEY_LIVE = 0;
// the group on the tribute tiles when no altar can be reached without killing it (-1 = none)
static int GATE_G = -1;
// an ally saw the gate group all dead (byte 5 of shared memory, see gate_sm_tick)
static bool GATE_SM_OPEN = false;
static uint32_t alt_key_live() {
    uint32_t m = 0;
    for (size_t k = 0; k < ALT_KEY.size() && k < 32; ++k) {
        const Group& g = G[ALT_KEY[k].first];
        bool live = g.m[ALT_KEY[k].second].hp > 0;
        // the gate is open only once the whole group is down (a live neighbour hits whoever stands there)
        if (g.id == GATE_G) for (int q = 0; q < g.n; ++q) if (g.m[q].hp > 0) live = true;
        // what the claims told us (an ally captured it: respawning) or an ally saw it all dead: free until it is back
        if (g.st != ST_ALIVE) live = false;
        if (g.id == GATE_G && GATE_SM_OPEN) live = false;
        if (live) m |= 1u << k;
    }
    return m;
}
static void build_altar_field() {
    bc::bitboard ar;
    for (auto a : altars)
        for (auto d : DIRS) {
            vec2 q = a + d;
            if (!inb(q) || BLOCK[idx(q)]) continue;
            bool live = false;
            for (size_t k = 0; k < ALT_KEY.size() && k < 32; ++k)
                if (G[ALT_KEY[k].first].m[ALT_KEY[k].second].p == q && ((ALT_KEY_LIVE >> k) & 1)) live = true;
            if (!live) ar.set(q, true);
        }
    F_ALTAR = make_field(STATIC_BB, ar);
    for (auto& g : G) { const int c = F_ALTAR.at(g.m[0].p); g.d_altar = c == UNREACH ? 999 : c; }
}
// Second-stage static fields (spread over the first turns to stay inside the fuel budget).
static void init_altar_field() {
    for (auto a : altars)
        for (auto d : DIRS) {
            vec2 q = a + d;
            if (!inb(q) || BLOCK[idx(q)]) continue;
            const int gi = group_at[idx(q)];
            if (gi < 0) continue;
            for (int k = 0; k < G[gi].n; ++k)
                if (G[gi].m[k].p == q) {
                    bool dup = false;
                    for (auto& e : ALT_KEY) if (e.first == gi && e.second == k) dup = true;
                    if (!dup) ALT_KEY.push_back({gi, k});
                }
        }
    ALT_KEY_LIVE = alt_key_live();
    build_altar_field();
    // every tribute tile we could walk to holds a camp: its group is the only way to bank
    if (!ALT_KEY.empty() && !own_spawn.empty() && F_ALTAR.at(own_spawn[0]) == UNREACH) {
        GATE_G = ALT_KEY[0].first;
        ALT_KEY_LIVE = alt_key_live();
    }
}
// the rebuild waits for the end of the turn (spare fuel, after think) unless it has waited
// ALTAR_REBUILD_ROUNDS rounds; until then the old F_ALTAR stays in use (one round stale: a freed tile is missed or a tile that just
// got its camp back is still a goal; banking and ALTNONE re-check it on the next turn)
static int AD_SINCE = -1;           // first round the live mask differed from the field's (-1: none pending)
static long AD_EST = 40000;         // largest rebuild cost seen (seed before the first one)
static bool AD_SEEN = false;
static void altar_rebuild_now() {
    const long f0 = (long)bc::fuel_remaining();
    ALT_KEY_LIVE = alt_key_live();
    build_altar_field();
    const long c = f0 - (long)bc::fuel_remaining();
    if (c > 0 && (!AD_SEEN || c > AD_EST)) AD_EST = c;
    if (c > 0) AD_SEEN = true;
    AD_SINCE = -1;
}
// start of the turn (in place of refresh_altar_field)
static void refresh_altar_field_deferred(int r) {
    if (ALT_KEY.empty()) return;
    if (alt_key_live() == ALT_KEY_LIVE) { AD_SINCE = -1; return; }
    if (AD_SINCE < 0) AD_SINCE = r;
    if (r - AD_SINCE >= ALTAR_REBUILD_ROUNDS) altar_rebuild_now();
}
// end of the turn
static void altar_spare() {
    if (AD_SINCE < 0 || ALT_KEY.empty()) return;
    if (alt_key_live() == ALT_KEY_LIVE) { AD_SINCE = -1; return; }
    if ((long)bc::fuel_remaining() >= AD_EST + 12000) altar_rebuild_now();
}
static void init_away_field() {
    bc::bitboard er;   // walkable tiles touching the enemy spawn
    for (auto s : en_spawn)
        for (auto d : DIRS) { vec2 q = s + d; if (inb(q) && !BLOCK[idx(q)]) er.set(q, true); }
    F_AWAY = make_field(STATIC_BB, er);
    for (auto& g : G) { const int c = F_AWAY.at(g.m[0].p); g.d_away = c == UNREACH ? 999 : c + 1; }
}

static void init_me() {
    auto& me = bc::self();
    ME.type = me.champion_name();
    ME.melee = is_melee(ME.type);
    { const auto st = me.base_stats(); ME.g_str = st.strength_growth; ME.g_focus = st.focus_growth; ME.g_hp = st.health_growth; }
    for (int s = 0; s < 2; ++s) {
        auto ab = me.abilities()[s];
        Ab& a = ME.ab[s];
        a.reset = ab.is_attack_reset(); a.dmg = ab.is_damage(); a.aoe = ab.is_aoe_damage(); a.cc = ab.is_cc();
        a.dash = ab.is_dash(); a.shield = ab.is_shield(); a.heal = ab.is_heal();
        a.ground = ab.can_target_ground(); a.camps = ab.can_target_camps(); a.enemies = ab.can_target_enemies();
        const auto& inf = ab.info();
        if (inf) a.excl = std::visit([](const auto& v) { return v.exclusive_with_movement; }, *inf);
        if (inf) if (const auto* dd = std::get_if<bc::ability_dash>(&*inf)) a.land = dd->modifier;
        a.range = (a.dmg || a.aoe || a.cc || a.heal) ? 4.f : a.dash ? 2.f : 0.f;
    }
}

static void init_allies() {
    auto all = bc::champions(team::ally);
    auto& me = bc::self();
    int s = 0;
    for (const auto& c : all) {
        Champ ch(c);
        ch.ally = true;
        ch.slot = s;
        if (c == me) { MY_SLOT = s; ch.type = ME.type; }
        else { try { ch.type = c.champion_name(); } catch (const std::exception&) { ch.type_known = false; } }
        ALLY.push_back(ch);
        ++s;
    }
}

static void init_enemies() {
    auto all = bc::champions(team::enemy);
    int s = 0;
    for (const auto& c : all) {
        Champ ch(c);
        ch.ally = false;
        ch.slot = s++;
        try { ch.type = c.champion_name(); ch.type_known = true; } catch (const std::exception&) { ch.type_known = false; }
        ENEMY.push_back(ch);
    }
    enemies_loaded = true;
}

// ---------------------------------------------------------------------------------------------
// Per-turn observation
// ---------------------------------------------------------------------------------------------
static std::vector<vec2> OCC;    // tiles occupied by visible units (dynamic obstacles)
static bool occupied(vec2 p) {
    for (auto q : OCC) if (q == p) return true;
    return false;
}
static bool walkable(vec2 p) { return inb(p) && !BLOCK[idx(p)] && !occupied(p); }
// An enemy on its own spawn heals fully every round and cannot be stepped on: not worth hitting.
static bool on_enemy_spawn(vec2 p) { return inb(p) && EN_SPAWN[idx(p)]; }
// Chebyshev distance to the nearest enemy spawn tile, and the radius around it where we neither chase nor
// start fights: ENEMY_SPAWN_RADIUS, but at most a third of the distance between the two spawns (a small map keeps its middle)
static int espawn_d(vec2 p) { int d = 1 << 20; for (auto q : en_spawn) d = std::min(d, cheb(p, q)); return d; }
static int es_r() {
    static int r = -1;
    if (r < 0) {
        int gap = 1 << 20;
        for (auto a : own_spawn) for (auto b : en_spawn) gap = std::min(gap, cheb(a, b));
        r = (!en_spawn.empty()) ? std::min(ENEMY_SPAWN_RADIUS, gap / 3) : 0;
    }
    return r;
}
static bool in_es_zone(vec2 p) { return es_r() > 0 && espawn_d(p) <= es_r(); }

static void read_champ(Champ& c, int r) {
    c.vis = false;
    const int seen0 = c.seen_at;
    // Every live-state accessor below calls sdk_champion. Read its complete record once; gems use their separate API.
    bc::detail::champion_info ci{};
    const uint32_t h = static_cast<uint32_t>(static_cast<bc::handle_champion_t>(c.h));
    const int32_t code = bc::detail::sdk_champion(h, &ci);
    if (code == (int32_t)bc::error_code::api_not_in_vision) return;
    bc::detail::check(code);
    if (ci.x < 0 || ci.y < 0) return;   // champion::is_visible() applies the same position test
    const vec2 p{ci.x, ci.y};
    c.vis = true; c.alive = true;
    if (!c.type_known) { c.type = c.h.champion_name(); c.type_known = true; }
    c.hurt = c.seen_at == r - 1 && ci.health < c.hp - 0.5f;
    c.p = p; c.last_p = p; c.seen_at = r;
    c.hp = ci.health; c.mhp = ci.max_health;
    c.str = ci.strength;
    c.focus = ci.focus;
    c.gems = (int)c.h.gem_count();
    const auto st = bc::champion_state::from_bits_retain(ci.state);
    c.channel = is_channelling(st);
    c.tribute = st.contains(bc::champion_state::submitting_gems);
    if (seen0 != r) {
        int rem = -1;
        if (c.channel) { try { rem = (int)c.h.remaining_channel_time(); } catch (const std::exception&) { rem = -1; } }
        c.chan_old = rem >= 0 && seen0 == r - 1 && c.chan_rem == rem + 1;
        c.chan_rem = rem;
    }
    // enemies too. Five top-10 bots end every turn with a recall they cancel next turn; the enemy we just hit
    // is the only one whose recall broke, so a fresh "channel" pulled the next ally off it (focus kept 0.47 vs 0.69).
    if (c.channel && st.contains(bc::champion_state::recalling) && !c.chan_old) c.channel = false;
}

// the camp part of observe() with the same results and fewer host calls (it was 17% of all fuel,
// ~17k a turn on 01yellow-brick-road):
//  - the visible-tile mask is exported once a turn (row-major bits, export_linear_buffer) and read in our memory
//    instead of one is_set host call per member;
//  - one camp record (the call behind is_visible, health and position: three calls before) per member; a member whose
//    spawn tile is not in view is not asked (monsters never move, and the engine answers "not in vision" there);
//  - the group state is read with the two engine calls state() makes after its own vision check (a whole-board read
//    and a member scan), which vis_now > 0 already guarantees; any other answer is ignored as the old catch did.
static bc::bitboard* OF_VB = nullptr;
static std::vector<uint8_t> OF_LIN;
static inline bool of_vis(vec2 p) { const int i = p.y * W + p.x; return (OF_LIN[i >> 3] >> (i & 7)) & 1; }
static int OF_NEAR_R = 9;   // groups read within this + radius (lower on fight turns)
static void obs_camps_fast(int r) {
    if (!OF_VB) { OF_VB = new bc::bitboard(bc::uninitialized); OF_LIN.assign(bc::bitboard::linear_buffer_size(), 0); }
    bc::vision::visible_tiles_into(*OF_VB);
    OF_VB->export_linear_buffer(OF_LIN);
    const vec2 mp = ME.p;
    for (auto& g : G) {
        // Only a previously observed group can have true member sight flags.
        // Expire that sight before early-outs; retain remembered HP/location.
        // This gate avoids walking every remote camp on every champion turn.
        if (g.vis_now > 0)
            for (int k = 0; k < g.n; ++k) g.m[k].vis = false;
        g.vis_now = 0;
        bool near = cheb(g.center, mp) <= OF_NEAR_R + std::max(2, g.id < (int)G_RAD.size() ? G_RAD[g.id] : 2);
        if (!near) continue;
        uint64_t inview = 0;   // members whose tile is in view (any distance)
        for (int k = 0; k < g.n; ++k) {
            const bool t = of_vis(g.m[k].p);
            if (t && k < 64) inview |= 1ull << k;
            g.m[k].unseen = !(t && cheb(g.m[k].p, mp) <= 9);
            if (!g.m[k].unseen) ++g.vis_now;
        }
        if (!g.vis_now) continue;
        g.seen_at = r;
        float hp = 0;
        int alive = 0;
        const bool longg = g.id < (int)G_RAD.size() && G_RAD[g.id] > 2;
        for (int k = 0; k < g.n; ++k) {
            Mon& m = g.m[k];
            m.vis = false;
            if (longg && m.unseen) { hp += m.hp; continue; }
            if (k < 64 ? ((inview >> k) & 1) != 0 : of_vis(g.m[k].p)) {
                bc::detail::camp_state cs{};
                const int32_t code = bc::detail::sdk_camp(static_cast<uint32_t>(static_cast<bc::handle_camp_t>(*m.c)), &cs);
                if (code == 0) {
                    const float h = cs.health;
                    if (h > 0) {
                        if (h < m.hp - 0.5f) g.awake_at = r;
                      // published pool, camp deaths 0.53 vs 0.73 a game, 590 vs 590 wins, margin +581 vs +566
                        // the engine's own awake flag for a hitting camp near me. An awake camp hits
                        // for as long as a champion stays in its reach (rules 05), but "awake" here was "seen hurt
                        // in the last 2 rounds": a camp woken out of our view, or still hitting 2 rounds after its last
                        // damage, read as asleep, so escapes, flee steps and bank channels stayed in its reach. 19% of
                        // our deaths on the published pool were to camps (elk 173, wolf 154, Monolith 113, Golem 58 of
                        // 498 in 686 games); in the logs 61% came during a low-health escape
                        if (m.str > 0 && g.awake_at != r && cheb(vec2{cs.x, cs.y}, ME.p) <= m.reach + 4) {
                            uint32_t st = 0;
                            if (bc::detail::sdk_camp_state(static_cast<uint32_t>(static_cast<bc::handle_camp_t>(*m.c)), &st) == 0 && st == 1)
                                g.awake_at = r;
                        }
                        m.vis = true; m.hp = h; hp += h; ++alive;
                        OCC.push_back(vec2{cs.x, cs.y});
                        continue;
                    }
                } else if (code != (int32_t)bc::error_code::api_not_in_vision && code != (int32_t)bc::error_code::api_not_found) {
                    bc::detail::check(code);   // as is_visible(): any other error throws
                }
            }
            if (!m.unseen) m.hp = 0;   // seen dead (in view and within 9)
            else hp += m.hp;
        }
        const int st_before = g.st;
        if (alive > 0) {
            if (g.st != ST_ALIVE) { for (int k = 0; k < g.n; ++k) if (!g.m[k].vis && g.m[k].hp <= 0) g.m[k].hp = g.m[k].mhp; }
            g.st = ST_ALIVE;
            g.hp_left = hp;
        } else {
            if (r - g.state_at >= 1) {
                g.state_at = r;
                // camp_group::state(): its vision check passes (a member tile is in view); 1 alive, 2 reward, 3 respawning
                // (+ the timer); 0, an error or any other value threw and was caught (no change)
                const uint32_t gh = static_cast<uint32_t>(static_cast<bc::handle_camp_group_t>((*GROUPS)[g.id]));
                uint32_t sv = 0;
                if (bc::detail::sdk_camp_group_state(gh, &sv) == 0) {
                    if (sv == 2) g.st = ST_REWARD;
                    else if (sv == 3) {
                        int32_t tm = 0;
                        if (bc::detail::sdk_camp_group_cooldown(gh, &tm) == 0) {
                            g.st = ST_RESPAWN; g.ready_at = r + tm;
                            const int c = g.ready_at - g.respawn, tol = (int)bc::constants::capture_time;
                            const bool ours = g.cap_claim_at >= c - tol - 3 || std::abs(c - g.cap_us_at) <= tol;
                            if (c > -1000 && !ours && std::abs(c - g.cap_them_at) > tol && c > g.cap_us_at - tol) g.cap_them_at = c;
                        }
                    }
                    else if (sv == 1) g.st = ST_ALIVE;
                }
            }
            // Back from a reward or a respawn it is whole again. A camp that was already alive and shows only dead members
            // here keeps what we remember: its live members are out of view (a wall, a bush), not healed.
            if (g.st == ST_ALIVE && st_before != ST_ALIVE) { g.hp_left = g.hp_total; for (int k = 0; k < g.n; ++k) g.m[k].hp = g.m[k].mhp; }
            else if (g.st == ST_ALIVE) g.hp_left = hp;
        }
        if (g.st != st_before) KNOW_CHANGED = true;
    }
}

static void observe() {
    auto& me = bc::self();
    const int r = rnd();
    // position, health, stats and state are fields of one champion_info record.
    bc::detail::champion_info ci{};
    const uint32_t mh = static_cast<uint32_t>(static_cast<bc::handle_champion_t>(me));
    bc::detail::check(bc::detail::sdk_champion(mh, &ci));
    ME.p = vec2{ci.x, ci.y};
    ME.hp = ci.health; ME.mhp = ci.max_health;
    ME.hurt = ME.hp_prev >= 0 && ME.hp < ME.hp_prev - 0.5f;
    ME.drop = ME.hp_prev >= 0 ? std::max(0.f, ME.hp_prev - ME.hp) : 0.f;
    ME.hp_prev = ME.hp;
    ME.str = ci.strength; ME.focus = ci.focus;
    ME.gems = (int)me.gem_count();
    ME.gold = (int)me.gold_count();
    { const auto ab = me.abilities(); ME.cd[0] = ab[0].cooldown(); ME.cd[1] = ab[1].cooldown(); }
    ME.walked = false; ME.cast_excl = false;
    const auto st = bc::champion_state::from_bits_retain(ci.state);
    ME.state = (uint32_t)st.bits();
    ME.can_move = st.contains(bc::champion_state::can_move);
    ME.can_attack = st.contains(bc::champion_state::can_attack);
    ME.can_cast = st.contains(bc::champion_state::can_cast);

    OCC.clear();
    for (int i = 0; i < (int)ALLY.size(); ++i) {
        if (i == MY_SLOT) { ALLY[i].vis = false; continue; }
        // linked partner and nearby allies matter; others are cheap to test anyway
        read_champ(ALLY[i], r);
        if (ALLY[i].vis) OCC.push_back(ALLY[i].p);
        // out of view it may have just died, and the fight Balance still counts it for FIGHT_MEMORY_ROUNDS rounds
        else if (r - ALLY[i].seen_at <= FIGHT_MEMORY_ROUNDS) { try { ALLY[i].alive = ALLY[i].h.is_alive(); } catch (const std::exception&) {} }
    }
    for (auto& e : ENEMY) {
        const int seen0 = e.seen_at;
        read_champ(e, r);
        // out of view, is_alive() needs no sight. read_champ only ever set
        // alive = true, so a dead enemy stayed alive at its death tile: a G2 ghost in the fight Balance for 20 rounds, and
        // every other `!e.alive` test on an enemy out of view was a no-op
        if (!e.vis) { try { e.alive = e.h.is_alive(); } catch (const std::exception&) {} }
        // an enemy that was out of view last round: the job and its risk were priced without it
        if (e.vis && seen0 < r - 1 && !on_enemy_spawn(e.p)) { KNOW_CHANGED = true; }
        if (e.vis) {
            OCC.push_back(e.p);
            // the spawn test does not depend on the group: once per enemy (the same marks)
            if (!EN_SPAWN[idx(e.p)])
                for (auto& g : G) if (cheb(e.p, g.center) <= 7) g.enemy_at = r;
        }
    }

    // camp groups in or near view (a fight turn reads the close ones only; the micro needs those)
    {
        int ne = 0;
        for (auto& e : ENEMY) if (e.vis && e.alive && cheb(e.p, ME.p) <= 8 && !on_enemy_spawn(e.p)) ++ne;
        OF_NEAR_R = ne >= 2 ? 6 : 9;
    }
    obs_camps_fast(r);
    // remembered respawns come back
    for (auto& g : G)
        if (g.st == ST_RESPAWN && r >= g.ready_at && g.seen_at != r) {
            g.st = ST_ALIVE; g.hp_left = g.hp_total; KNOW_CHANGED = true;
            for (int k = 0; k < g.n; ++k) g.m[k].hp = g.m[k].mhp;
        }
    // an awake camp is one that hits: it stays awake while a champion in its reach is losing health, not
    // only while it takes damage itself. "Awake" was "hurt in the last 2 rounds", so an elk still hitting a low Marcella
    // at distance 1 read as asleep 3 rounds after its own last damage: she recalled there and died in the channel
    // (swirl). A champion merely standing in reach is no proof: three of ours waited unhit at an altar by an elk
    // group, and "in reach" kept it awake for each of the others (TRIBHOLD 40 rounds, starfish)
    for (auto& g : G) {
        if (g.st != ST_ALIVE || r - g.awake_at > 2 || g.awake_at == r) continue;
        bool held = false;
        for (int k = 0; k < g.n && !held; ++k) {
            const Mon& m = g.m[k];
            if (m.hp <= 0 || m.str <= 0) continue;
            held = ME.hurt && cheb(m.p, ME.p) <= m.reach;
            for (const auto& a : ALLY) held = held || (a.vis && a.hurt && cheb(a.p, m.p) <= m.reach);
            for (const auto& e : ENEMY) held = held || (e.vis && e.alive && e.hurt && cheb(e.p, m.p) <= m.reach);
        }
        if (held) g.awake_at = r;
    }
    // teleport destinations block landing
    // (rare; skipped for fuel)
}

// ---------------------------------------------------------------------------------------------
// Team memory: the 8 shared bytes as one 64-bit word (little-endian), read once a turn (sm_read) and written only
// through the helpers below. Slots 0-4 (draft order) own 12 bits each, at 12 x slot:
//   bits 0-5  claimed group + 1 (0 none)
//   bit 6     capturing it          bit 7  DANGER (retreating; own_prios also reads it as a priority guess)
//   (bits 0-7 are the claim byte: CLAIM_GROUP / CLAIM_CAPTURING / CLAIM_DANGER)
//   bit 8     this slot holds the team's battle beacon (at most one slot at a time, see raise_beacon)
//   bits 9-11 team-wide fields (they were this slot's carried gems, but only the team's total
//            was ever read): slots 0 + 1 the team's carried gems (6 bits, SM_GT_*), slot 2 the beacon's high bits
//            (SM_BC_HI), slots 3 + 4 the respawn appointment (SM_AP_*, mid_plan.h)
// Bits 60-62: the monolith the leader names (its index among the map's monoliths + 1; the first 7 only). On maps with
//   a rally beacon (RALLY_ON, no monolith plan) they are the beacon's low bits: beacon group + 1 = SM_BC_HI x 8 + bits
//   60-62.
// Bit 63: the gate group was last seen all dead. A sixth champion (slot 5) has no bits: it neither writes nor is read.
// ---------------------------------------------------------------------------------------------
static constexpr int SM_SLOTS = 5, SLOT_BITS = 12;
static constexpr int SB_CLAIM = 0, SB_FIGHT = 8, SB_GEMS = 9;
static constexpr int SM_MONO = 60, SM_GATE = 63;
static constexpr uint8_t CLAIM_GROUP = 0x3f, CLAIM_CAPTURING = 0x40, CLAIM_DANGER = 0x80;
static constexpr int SM_GT_LO = 0 * SLOT_BITS + SB_GEMS, SM_GT_HI = 1 * SLOT_BITS + SB_GEMS;   // team carried gems
static constexpr int SM_BC_HI = 2 * SLOT_BITS + SB_GEMS;                                       // beacon group high bits
static constexpr int SM_AP_WHO = 3 * SLOT_BITS + SB_GEMS, SM_AP_WHEN = 4 * SLOT_BITS + SB_GEMS;   // the appointment
static uint8_t SM[8];
static uint64_t SMW = 0;   // SM as one word, kept in step with it
static void sm_read() {
    SMW = 0;
    for (int i = 7; i >= 0; --i) { SM[i] = (uint8_t)bc::shared_memory[i]; SMW = (SMW << 8) | SM[i]; }
}
static void sm_put(int i, uint8_t v) { if (SM[i] != v) { SM[i] = v; bc::shared_memory[i] = v; } }
static inline int sm_get(int off, int n) { return (int)((SMW >> off) & ((1ull << n) - 1)); }
// only the bytes that change are written; the other slots' bits are kept
static void sm_set(int off, int n, int v) {
    const uint64_t mask = ((1ull << n) - 1) << off;
    SMW = (SMW & ~mask) | (((uint64_t)v << off) & mask);
    for (int i = off / 8; i <= (off + n - 1) / 8; ++i) sm_put(i, (uint8_t)(SMW >> (8 * i)));
}
static inline bool slot_ok(int slot) { return slot >= 0 && slot < SM_SLOTS; }
static inline int slot_off(int slot) { return slot * SLOT_BITS; }
static inline uint8_t claim_byte(int slot) { return slot_ok(slot) ? (uint8_t)sm_get(slot_off(slot) + SB_CLAIM, 8) : 0; }
static int claim_group(int slot) { const int v = claim_byte(slot) & CLAIM_GROUP; return v ? v - 1 : -1; }
static bool claim_capturing(int slot) { return claim_byte(slot) & CLAIM_CAPTURING; }
// the team counter's 5 bits (team_counter.h) are the DANGER bits when the team has a slot 4 (5 or 6 bodies)
static inline bool TC_DB() { return ALLY.size() >= 5; }
static bool claim_danger(int slot) { return !TC_DB() && (claim_byte(slot) & CLAIM_DANGER); }
static bool slot_fighting(int slot) { return slot_ok(slot) && sm_get(slot_off(slot) + SB_FIGHT, 1); }
static void set_slot_fighting(int slot, bool f) { if (slot_ok(slot)) sm_set(slot_off(slot) + SB_FIGHT, 1, f ? 1 : 0); }
// the slot holding the battle beacon (-1: none)
static int beacon_slot() { for (int s = 0; s < SM_SLOTS; ++s) if (slot_fighting(s)) return s; return -1; }
// the beacon's group (-1: none); only RALLY_ON maps have a beacon
static int beacon_gid() {
    const int hs = RALLY_ON() ? beacon_slot() : -1;
    if (hs < 0) return -1;
    const int v = sm_get(SM_BC_HI, 3) << 3 | sm_get(SM_MONO, 3);
    return v ? v - 1 : -1;
}
// we hold the beacon at group g (0..62)
static void set_beacon_gid(int g) {
    set_slot_fighting(MY_SLOT, true);
    sm_set(SM_BC_HI, 3, (g + 1) >> 3);
    sm_set(SM_MONO, 3, (g + 1) & 7);
}
// slot hs no longer holds it
static void clear_beacon_gid(int hs) {
    set_slot_fighting(hs, false);
    sm_set(SM_BC_HI, 3, 0);
    sm_set(SM_MONO, 3, 0);
}
// the team's carried gems: one shared 6-bit total in units of GT_UNIT (every champion reads the same number). Each
// champion adds the change of its own share; a share is capped at GT_CAP units, so the total (at most 5 x 12 = 60) fits.
// Rounding is to the nearest unit (a champion carrying 1-12 gems counts 0; the old bands read 1-19 as 10).
static constexpr int GT_UNIT = 25, GT_CAP = 12;
static int GT_MINE = 0;   // the units I have added to the shared total
static inline int gt_units() { return sm_get(SM_GT_LO, 3) | sm_get(SM_GT_HI, 3) << 3; }
static int team_carried() { return gt_units() * GT_UNIT; }
static void report_gems(int now) {
    if (!slot_ok(MY_SLOT)) return;   // a sixth champion has no bits: it neither writes nor is read (as before)
    const int u = std::min(GT_CAP, (std::max(0, now) + GT_UNIT / 2) / GT_UNIT);
    if (u != GT_MINE) {
        sm_read();   // the total fresh from the buffer: a turn continued after running out of fuel would add to a stale one
        const int t = std::clamp(gt_units() + u - GT_MINE, 0, 63);
        sm_set(SM_GT_LO, 3, t & 7); sm_set(SM_GT_HI, 3, t >> 3);
        GT_MINE = u;
    }
}
static uint8_t my_claim = 0;
static bool DANGER = false;   // we are retreating from enemies (CLAIM_DANGER of our claim byte)
static void set_claim(int g, bool capturing) {
    uint8_t v = g < 0 || g >= 63 ? 0 : (uint8_t)((g + 1) | (capturing ? CLAIM_CAPTURING : 0));
    if (DANGER && !TC_DB()) v |= CLAIM_DANGER;
    my_claim = v;
    if (slot_ok(MY_SLOT)) sm_set(slot_off(MY_SLOT) + SB_CLAIM, TC_DB() ? 7 : 8, v);
}
static void set_danger(bool d) {
    DANGER = d;
    if (slot_ok(MY_SLOT) && !TC_DB()) sm_set(slot_off(MY_SLOT) + SB_CLAIM + 7, 1, d ? 1 : 0);
}
// Previous claims, to notice an ally finishing a capture.
static uint8_t prev_claim[SM_SLOTS] = {0, 0, 0, 0, 0};
static int claim_cap_since[SM_SLOTS] = {-1, -1, -1, -1, -1};   // round we first read each slot's capturing claim on its group
static void infer_from_claims() {
    const int r = rnd();
    for (int s = 0; s < SM_SLOTS && s < (int)ALLY.size(); ++s) {
        if (s == MY_SLOT) continue;
        const uint8_t was = prev_claim[s], now = claim_byte(s);
        if ((now & CLAIM_CAPTURING) && (now & CLAIM_GROUP) && (now & CLAIM_GROUP) - 1 < (int)G.size()) G[(now & CLAIM_GROUP) - 1].cap_claim_at = r;
        if ((was & CLAIM_GROUP) != (now & CLAIM_GROUP)) KNOW_CHANGED = true;
        if ((now & CLAIM_CAPTURING) && (now & CLAIM_GROUP) && !((was & CLAIM_CAPTURING) && (was & CLAIM_GROUP) == (now & CLAIM_GROUP)))
            claim_cap_since[s] = r;
        // the claim also moves when the capturer dies, flees or switches, which read as a payout and
        // hid a reward still on the tiles (or an enemy's capture) for the whole respawn. Paid only if a living ally held it
        // for a capture's shortest time: 3 turns with a Capture emblem (shop.h), less one for the turn order of our reads.
        const bool paid = claim_cap_since[s] >= 0 && r - claim_cap_since[s] >= std::min(3, (int)bc::constants::capture_time) - 1 &&
                          ALLY[s].alive;
        if (paid && (was & CLAIM_CAPTURING) && (was & CLAIM_GROUP) && (now & CLAIM_GROUP) != (was & CLAIM_GROUP) &&
            (was & CLAIM_GROUP) - 1 < (int)G.size()) {
            Group& g = G[(was & CLAIM_GROUP) - 1];
            g.cap_us_at = r;   // an ally was paid for it
            if (g.seen_at != r && g.st != ST_RESPAWN) { g.st = ST_RESPAWN; g.ready_at = r + g.respawn; }
        }
        prev_claim[s] = now;
    }
}
// bit 63 (kept off with six bodies, as when it lived in the sixth slot's byte): whoever has the gate group in view writes
// whether it is all dead; the others read it, so a champion far away rebuilds F_ALTAR when the gate opens (its own
// memory would keep the blocker alive until it walked there).
static void gate_sm_tick() {
    if (GATE_G < 0 || ALLY.size() > 5) { GATE_SM_OPEN = false; return; }
    const Group& g = G[GATE_G];
    if (g.seen_at == rnd()) {
        bool dead = true;
        for (int q = 0; q < g.n; ++q) if (g.m[q].hp > 0) dead = false;
        sm_set(SM_GATE, 1, dead ? 1 : 0);
    }
    GATE_SM_OPEN = sm_get(SM_GATE, 1) == 1;
}
