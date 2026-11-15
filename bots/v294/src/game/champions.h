// Everything the bot knows about the champions, one row per body (CHAMPS, indexed by champion_type).
// Patch-sensitive numbers live here only: on a balance patch, update this file (and check it against the engine with
// rule/probes/statdump). The rest of the code asks for traits (champ(t).splash, .taker, ...) and never names a champion.
// The camp/fight damage models below are hand fits made at different times (base_dps, champ_dps, goldroi's kit,
// siege_dps). They overlap but are kept as separate columns on purpose.
#pragma once

using IT = bc::item_type;

// ---- kit descriptions used by the micro (what each slot does to a target)
enum { SK_NONE = 0, SK_DMG, SK_AREA, SK_RESET, SK_DASH };
struct SlotK { int kind; float mult; bool stun; };
struct Kit { SlotK s[2]; };
namespace qm {   // the Q model's option-feature kit (qmodel.h)
enum { K0 = 0, KD, KA, KR, KH, KHD, KS, KL, KT };
struct Slot { int k = K0; float m = 0; int stun = 0; };
struct Kit { Slot s[2]; };
}

// Base stats and growth per shard (the balance patch of 5 Nov 2026, cambc 5.0.0; live stats are read when visible).
struct ChampionDesign { float h, gh, s, gs, f, gf; };

struct BuildStep { IT item; int n; };

struct ChampionInfo {
    const char* tag;                 // three letters for the logs
    ChampionDesign design;
    bool melee, dash;

    // ---- damage models (see the note at the top)
    float bd_s1, bd_s2, bd_f;        // base_dps: s x bd_s1 x bd_s2 + bd_f x f / 8 (sustained damage to one camp monster)
    float fd_f8, fd_s, shield_f;     // champ_dps on top of basic attacks: f x fd_f8 / 8 + s x fd_s + f x shield_f
                                     // (shield_f: 0 for every type. A self-shield is not damage: Varg's 2.5 / 8 and
                                     //  HoR's 1.5 / 8 do not count as dps; the fight Balance
                                     //  adds the shield to the body's hp side instead, see shield_hp() in champion_combat.h)
    float heal_f8;                   // champ_heal: f x heal_f8 / 8 (heal or self-shield per round)
    float siege_dps;                 // type_siege_dps: spell damage per round from out of a boss's reach
    bool dps_scales_focus;           // my_dps_est scales with Focus (casters) instead of Strength
    Kit kit;                         // what each slot does (micro)
    float burst;                     // biggest damaging spell: burst x Focus
    int area_slot; float area_k;     // area damage spell slot (-1: none) and its multiplier
    float target_mult;               // focus-fire priority (healers and burst casters die first)
    qm::Kit qkit;

    // ---- passives and kit traits
    bool splash;                     // basic attacks splash the tiles next to the target
    bool aoe_farm;                   // its area spell hits a whole camp group (job scores credit groups of 2+)
    bool stun_immune;
    bool kill_reset;                 // a kill resets the cooldowns
    bool remote_shop;                // shops anywhere, sells at spawn
    bool missing_hp;                 // more damage at low health
    float gold_bonus;                // gold multiplier from the passive

    // ---- roles
    bool taker;                      // takes the team's capture rewards
    float gold_prio;                 // counts as this many times poorer when gold is shared
    bool shop_eager;                 // Focus pays twice (clearing and fights): shopping trips count double

    // ---- item build (shop.h)
    const BuildStep* build;
    // the front line in fights (micro): takes hits for the others, stands between the enemy and
    // them, goes in first
    bool tank = false;
};

// ---- builds (each ends with a long stack, never run out)
namespace builds {
// Vesper Vale's own build: STR_CARRY's stats slot by slot (Strength, Health, Strength, Health, then the
// Strength stack), other families: Prospector (+25% of its cluster gold/gems), Treasure (+10% gold), Hunter (+50 gold a
// champion kill), Durability, then Power blocks bought as Bruiser
static constexpr BuildStep GOLD_CARRY[] = {{IT::prospector_shard_of_strength, 5}, {IT::treasure_shard_of_health, 5},
    {IT::hunter_shard_of_strength, 5}, {IT::durability_shard_of_health, 5}, {IT::power_shard_of_strength, 200}, {IT{}, 0}};
// Marcella's own build: Treasure Focus first (+10% of all her gold, tribute gold included), Prospector Focus, Capture Focus, Jungle Focus, Monster Focus, Bruiser Health, Durability Health, Collectors Focus, then Power Focus blocks (bought as Bruiser Focus)
// Capture Focus (captures she joins at their start take 3 turns, not 5) as her 2nd emblem
static constexpr BuildStep MARCELLA_MITE[] = {{IT::treasure_shard_of_focus, 5}, {IT::prospector_shard_of_focus, 5}, {IT::capture_shard_of_focus, 5}, {IT::jungle_shard_of_focus, 5}, {IT::monster_shard_of_focus, 5},
    {IT::bruiser_shard_of_health, 5}, {IT::durability_shard_of_health, 5}, {IT::collectors_shard_of_focus, 5},
    {IT::power_shard_of_focus, 200}, {IT{}, 0}};
static constexpr BuildStep STR_CARRY[] = {{IT::treasure_shard_of_strength, 5}, {IT::jungle_shard_of_health, 5},
    {IT::durability_shard_of_strength, 5}, {IT::bruiser_shard_of_health, 5}, {IT::power_shard_of_strength, 200}, {IT{}, 0}};
// Vendetta's own build = STR_CARRY with Prospector Strength (+25% of her cluster gold/gems) in place of
// Treasure Strength (+10% gold); same shards, so the same stats
static constexpr BuildStep VEN_CARRY[] = {{IT::prospector_shard_of_strength, 5}, {IT::jungle_shard_of_health, 5},
    {IT::durability_shard_of_strength, 5}, {IT::bruiser_shard_of_health, 5}, {IT::power_shard_of_strength, 200}, {IT{}, 0}};
static constexpr BuildStep FOCUS_CARRY[] = {{IT::treasure_shard_of_focus, 5}, {IT::jungle_shard_of_focus, 5},
    {IT::durability_shard_of_focus, 5}, {IT::bruiser_shard_of_health, 5}, {IT::power_shard_of_focus, 200}, {IT{}, 0}};
static constexpr BuildStep HEALTH_CARRY[] = {{IT::treasure_shard_of_health, 5}, {IT::jungle_shard_of_health, 5},
    {IT::durability_shard_of_health, 5}, {IT::power_shard_of_focus, 200}, {IT{}, 0}};
static constexpr BuildStep LINK_TANK[] = {{IT::jungle_shard_of_health, 5}, {IT::durability_shard_of_health, 5},
    {IT::power_shard_of_health, 200}, {IT{}, 0}};
// Ryth's MELEE families with every stat Health: Jungle (-30% camp damage), Monster (+15% camp damage),
// Power (bought as Bruiser: +5 Health +1 Strength +1 Focus), Durability (-10% all damage), Power stack. Ryth's passive turns 1% of
// bonus health into Strength (+1 a Health shard).
static constexpr BuildStep RYTH_TANK[] = {{IT::jungle_shard_of_health, 5}, {IT::monster_shard_of_health, 5},
    {IT::power_shard_of_health, 5}, {IT::durability_shard_of_health, 5}, {IT::power_shard_of_health, 200}, {IT{}, 0}};
static constexpr BuildStep HEALER[] = {{IT::power_shard_of_focus, 5}, {IT::jungle_shard_of_health, 5},
    {IT::power_shard_of_focus, 200}, {IT{}, 0}};
// Varg as the front line: Durability, Monster and Bruiser Health before the Strength stack
static constexpr BuildStep VARG_TANK[] = {{IT::jungle_shard_of_strength, 5}, {IT::durability_shard_of_health, 5},
    {IT::monster_shard_of_health, 5}, {IT::bruiser_shard_of_health, 5}, {IT::power_shard_of_strength, 200}, {IT{}, 0}};
// Heart of Ruin on Focus (landing and shield 1.5 x Focus; +9 a shard vs +3 Strength) and Health.
// Health first. The top-3 HoRs hold 5.6-6.4 Health shards by r400 (ours 2.9) and end with
// 16-28; a Health shard is +100 hp for good, a Focus shard +13.5 shield and +13.5 landing per 8 rounds. Durability (-10% damage),
// Bruiser Health, then Jungle Focus (-30% camp damage, landing), Monster Health, Power Focus stack (Mite's hybrid, the farm-first
// style nearest to this bot; Fish/Shington stack Health).
static constexpr BuildStep HOR_TANK[] = {{IT::durability_shard_of_health, 5}, {IT::bruiser_shard_of_health, 5},
    {IT::jungle_shard_of_focus, 5}, {IT::monster_shard_of_health, 5}, {IT::power_shard_of_focus, 200}, {IT{}, 0}};
}

static constexpr int CT_N = 13;
// Order = champion_type: ryth, vesper_vale, vendetta, cassi, issac, stellar, prism, marcella, willow, ren, varg, tavi,
// heart_of_ruin.
static constexpr ChampionInfo CHAMPS[CT_N] = {
    // ryth: area stun (1 x Focus, r1) + attack reset; +1% bonus health as Strength
    {"RYT", {1200, 100, 45, 4.5f, 100, 10}, true, false,
     1.125f, 1.f, 1.f,  1.f, 0.f, 0.f,  0.f, 12,  false,
     {{{SK_AREA, 1.0f, true}, {SK_RESET, 0.f, false}}}, 1.0f, 0, 1.f, 1.f,
     {{{qm::KA, 1.0f, 1}, {qm::KR, 0, 0}}},
     false, false, false, false, false, false, 1.f,
     false, 1.f, false, builds::RYTH_TANK, false},   // Ryth Health build
    // vesper_vale: attack reset + dash; +33% gold
    {"VES", {1000, 100, 35, 5, 100, 10}, false, true,
     0.85f, 1.125f, 0.f,  0.f, 0.f, 0.f,  0.f, 0,  false,
     {{{SK_RESET, 0.f, false}, {SK_DASH, 0.f, false}}}, 0.f, -1, 0.f, 1.f,
     {{{qm::KR, 0, 0}, {qm::KH, 0, 0}}},
     false, false, false, false, false, false, 1.33f,
     true, GOLD_PRIO_STELLAR, false, builds::GOLD_CARRY},
    // vendetta: attack reset + dash; a kill resets her cooldowns
    {"VEN", {900, 100, 65, 6, 100, 10}, true, true,
     1.25f, 1.f, 0.f,  0.f, 0.15f, 0.f,  0.f, 0,  false,
     {{{SK_RESET, 0.f, false}, {SK_DASH, 0.f, false}}}, 0.f, -1, 0.f, 1.f,
     {{{qm::KR, 0, 0}, {qm::KH, 0, 0}}},
     false, false, false, true, false, false, 1.f,
     true, GOLD_PRIO_REN, false, builds::VEN_CARRY},
    // cassi: damage 2 x Focus + stun; linked to Issac, follows his job
    {"CAS", {600, 65, 30, 3, 100, 13}, false, false,
     0.8f, 1.f, 2.f,  2.f, 0.f, 0.f,  0.f, 17,  true,
     {{{SK_DMG, 2.0f, false}, {SK_DMG, 0.f, true}}}, 2.0f, -1, 0.f, 1.15f,
     {{{qm::KD, 2.0f, 0}, {qm::KT, 0, 1}}},
     false, false, false, false, false, false, 1.f,
     false, GOLD_PRIO_CASTER, true, builds::FOCUS_CARRY},
    // issac: attack reset + dash; linked to Cassi
    {"ISS", {900, 80, 45, 3, 100, 10}, true, true,
     1.125f, 1.f, 0.f,  0.f, 0.f, 0.f,  0.f, 0,  false,
     {{{SK_RESET, 0.f, false}, {SK_DASH, 0.f, false}}}, 0.f, -1, 0.f, 1.f,
     {{{qm::KR, 0, 0}, {qm::KH, 0, 0}}},
     false, false, false, false, false, false, 1.f,
     false, 1.f, false, builds::LINK_TANK},
    // stellar: damage 2 x Focus + area 1.4 x Focus
    {"STE", {650, 65, 30, 3.5f, 85, 8.5f}, false, false,
     0.8f, 1.f, 3.4f,  3.4f, 0.f, 0.f,  0.f, 42,  true,
     {{{SK_DMG, 2.0f, false}, {SK_AREA, 1.4f, false}}}, 2.0f, 1, 1.4f, 1.15f,
     {{{qm::KD, 2.0f, 0}, {qm::KA, 1.4f, 0}}},
     false, true, false, false, false, false, 1.f,
     false, GOLD_PRIO_STELLAR, true, builds::FOCUS_CARRY},
    // prism: stun 1.5 x Focus + area 1 x Focus; +1.5 Focus per carried gem
    {"PRI", {700, 65, 35, 2.5f, 90, 8}, false, false,
     0.8f, 1.f, 2.5f,  2.5f, 0.f, 0.f,  0.f, 31,  true,
     {{{SK_DMG, 1.5f, true}, {SK_AREA, 1.0f, false}}}, 1.5f, 1, 1.f, 1.15f,
     {{{qm::KD, 1.5f, 1}, {qm::KA, 1.0f, 0}}},
     false, true, false, false, false, false, 1.f,
     false, GOLD_PRIO_CASTER, true, builds::FOCUS_CARRY},
    // marcella: enhanced attack reset (its next hit stuns; the micro scores it as a plain second hit) + damage 2 x Focus;
    // stun immune (used only for enemy Marcellas), gold per gem tributed
    {"MAR", {1200, 100, 40, 3.5f, 60, 10}, true, false,
     1.125f, 1.f, 2.f,  2.f, 0.f, 0.f,  0.f, 12,  false,
     {{{SK_RESET, 0.f, true}, {SK_DMG, 2.0f, false}}}, 2.0f, -1, 0.f, 1.f,
     {{{qm::KR, 0, 0}, {qm::KD, 2.0f, 0}}},
     false, false, true, false, false, false, 1.f,
     true, 3.f, false, builds::MARCELLA_MITE},   // gold_prio 1 -> 3
    // willow: ally heal 3 x Focus + attack reset
    {"WIL", {700, 50, 55, 3, 140, 5}, false, false,
     0.8f, 1.125f, 0.f,  0.f, 0.f, 0.f,  3.f, 0,  false,
     {{{SK_NONE, 0.f, false}, {SK_RESET, 0.f, false}}}, 0.f, -1, 0.f, 1.3f,
     {{{qm::KL, 3.0f, 0}, {qm::KR, 0, 0}}},
     false, false, false, false, false, false, 1.f,
     false, 1.f, false, builds::HEALER},
    // ren: stun 1.1 x Focus + dash; retaliation
    {"REN", {900, 100, 35, 5, 100, 9}, true, true,
     1.f, 1.f, REN_STUN_VALUE,  REN_STUN_VALUE + 0.2f, 0.f, 0.f,  0.f, 16,  false,
     {{{SK_DMG, REN_STUN_VALUE, true}, {SK_DASH, 0.f, false}}}, REN_STUN_VALUE, -1, 0.f, 1.f,
     {{{qm::KD, REN_STUN_VALUE, 1}, {qm::KH, 0, 0}}},
     false, false, false, false, false, false, 1.f,
     true, GOLD_PRIO_REN, true, builds::HEALTH_CARRY},
    // varg: attack reset + self-shield 2.5 x Focus; basic attacks splash
    {"VAR", {1000, 100, 50, 5, 100, 10}, true, false,
     1.125f, 1.f, 0.f,  0.f, 0.f, 0.f,  2.5f, 0,  false,   // shield_f 0.3 -> 0 (shield on the hp side)
     {{{SK_RESET, 0.f, false}, {SK_NONE, 0.f, false}}}, 0.f, -1, 0.f, 1.f,
     {{{qm::KR, 0, 0}, {qm::KS, 2.5f, 0}}},
     true, false, false, false, false, false, 1.f,
     false, 1.f, false, builds::VARG_TANK, true},   // gold priority 0, tank build, tank
    // tavi: attack reset + damage/stun 1.6 x Focus; shops anywhere
    {"TAV", {900, 75, 45, 5, 110, 10}, false, false,
     0.8f, 1.125f, TAVI_STUN_VALUE,  TAVI_STUN_VALUE, 0.f, 0.f,  0.f, 27,  false,
     {{{SK_RESET, 0.f, false}, {SK_DMG, 1.6f, true}}}, 1.6f, -1, 0.f, 1.15f,
     {{{qm::KR, 0, 0}, {qm::KD, TAVI_STUN_VALUE, 1}}},
     false, false, false, false, true, false, 1.f,
     true, GOLD_PRIO_CASTER, false, builds::STR_CARRY},
    // heart_of_ruin: self-shield 1.5 x Focus + dash (landing 1.5 x Focus, stun r1); more damage at low health
    {"HOR", {1000, 100, 30, 3, 80, 9}, true, true,
     1.125f, 1.f, HOR_LANDING_VALUE,  HOR_LANDING_VALUE, 0.f, 0.f,  1.5f, 0,  false,   // shield_f 1.5/8 -> 0 (hp side)
     {{{SK_NONE, 0.f, false}, {SK_AREA, HOR_LANDING_VALUE, true}}}, HOR_LANDING_VALUE, -1, 0.f, 1.f,
     {{{qm::KS, 1.5f, 0}, {qm::KHD, 1.5f, 1}}},
     false, true, false, false, false, true, 1.f,   // aoe_farm (camp landings hit the group)
     true, 1.f, false, builds::HOR_TANK, false},  // Focus / Health build; not a tank
};

static constexpr const ChampionInfo& champ(CT t) { return CHAMPS[(int)t]; }
// self-shield size per Focus from the kit (Varg 2.5, Heart of Ruin 1.5, 0 without one), and its slot
static inline float shield_mult(CT t) {
    const qm::Kit& q = champ(t).qkit;
    for (int k = 0; k < 2; ++k) if (q.s[k].k == qm::KS) return q.s[k].m;
    return 0.f;
}
static inline int shield_slot_of(CT t) {
    const qm::Kit& q = champ(t).qkit;
    for (int k = 0; k < 2; ++k) if (q.s[k].k == qm::KS) return k;
    return -1;
}
static inline float heal_mult(CT t) {
    const qm::Kit& q = champ(t).qkit;
    for (int k = 0; k < 2; ++k) if (q.s[k].k == qm::KL) return q.s[k].m;
    return 0.f;
}
static inline int heal_slot_of(CT t) {
    const qm::Kit& q = champ(t).qkit;
    for (int k = 0; k < 2; ++k) if (q.s[k].k == qm::KL) return k;
    return -1;
}
// gold a champion earns per gem it tributes (Marcella's passive)
static inline float tribute_gold(CT t) { return t == CT::marcella ? 2.f : 0.f; }
// a self-shield's Focus multiplier, from the kit (Varg 2.5, Heart of Ruin 1.5; camp_calc.h reads it the same way)
static inline float shield_k(CT t) {
    const qm::Kit& q = champ(t).qkit;
    for (int k = 0; k < 2; ++k) if (q.s[k].k == qm::KS) return q.s[k].m;
    return 0.f;
}
// how much one gold is worth to this champion early, against the same gold to an average one: the takers by
// gold_prio (Vesper 1.5, Marcella 1.5, Vendetta 1.25, HoR 1), the rest GOLD_ROLE_NON_TAKER_WEIGHT; it fades to 1 by GOLD_ROLE_FADE_ROUND
static inline float grole_w(CT t, int r) {
    const float e = std::max(0.f, 1.f - r / (float)GOLD_ROLE_FADE_ROUND);
    const float w = champ(t).taker ? 1.f + 0.25f * (champ(t).gold_prio - 1.f) : GOLD_ROLE_NON_TAKER_WEIGHT;
    return 1.f + (w - 1.f) * e;
}
// one group's value to champion t in gems: gems (and the tribute gold they pay) plus gold at gw, weighted for t
static inline float grole_value(CT t, int r, float gems, float gold, float gw) {
    return gems * (1.f + gw * tribute_gold(t)) + gw * gold * champ(t).gold_bonus * grole_w(t, r);
}
static constexpr ChampionDesign champion_design(CT t) { return champ(t).design; }
static bool is_melee(CT t) { return champ(t).melee; }
static bool has_dash(CT t) { return champ(t).dash; }
static bool is_hor_kit(CT t) { return champ(t).missing_hp; }                         // Heart of Ruin (log feature)

// The body (bodies: Cassi and Issac) a draft pick brings.
static CT body_of(CD c) {
    switch (c) {
        case CD::ryth: return CT::ryth; case CD::vesper_vale: return CT::vesper_vale; case CD::vendetta: return CT::vendetta;
        case CD::stellar: return CT::stellar; case CD::prism: return CT::prism; case CD::marcella: return CT::marcella;
        case CD::willow: return CT::willow; case CD::ren: return CT::ren; case CD::varg: return CT::varg;
        case CD::tavi: return CT::tavi; case CD::heart_of_ruin: return CT::heart_of_ruin;
        default: return CT::issac;
    }
}
// Every body of a draft pick, the leading one first (Issac, then Cassi). Returns the count.
static int draft_bodies(CD c, CT out[2]) {
    if (c == CD::cassi_and_issac) { out[0] = CT::issac; out[1] = CT::cassi; return 2; }
    out[0] = body_of(c); return 1;
}

// ---- damage models
// Base sustained damage per round against one camp monster: basic attacks + resets + spells / cooldown.
static float base_dps(CT t) {
    const ChampionInfo& k = champ(t);
    const ChampionDesign& b = k.design;
    return b.s * k.bd_s1 * k.bd_s2 + k.bd_f * b.f / 8.f;
}
// Damage per round in a champion fight: basic attacks plus roughly two focus-scaled casts per eight rounds.
// the attack reset priced by the kit, one extra hit per 8-round cooldown = x1.125, none without a reset
// (was x1.1 for every type; replays, data/dps1029: reset champions hit twice in 7-16% of their rounds in reach)
static bool has_reset(CT t) { const Kit& k = champ(t).kit; return k.s[0].kind == SK_RESET || k.s[1].kind == SK_RESET; }
static float champ_dps(CT t, float str, float focus) {
    const ChampionInfo& k = champ(t);
    float d = str * (k.melee ? 1.0f : 0.9f) * (has_reset(t) ? 1.125f : 1.0f);
    if (k.fd_f8 != 0.f) d += focus * k.fd_f8 / 8;
    if (k.fd_s != 0.f) d += str * k.fd_s;
    if (k.shield_f != 0.f) d += focus * k.shield_f;   // a self-shield, as effective damage
    return d;
}
// Healing per round a champion provides to its side (an ally heal, or a self-shield).
static float champ_heal(CT t, float focus) {
    const float k = champ(t).heal_f8;
    return k != 0.f ? focus * k / 8.f : 0.f;
}
static float type_siege_dps(CT t) { return champ(t).siege_dps; }
static Kit kit_of(CT t) { return champ(t).kit; }
// the slot of this type's dash; Heart of Ruin's landing is its dash (-1: none)
static int dash_slot_of(CT t) {
    if (!has_dash(t)) return -1;
    const Kit k = kit_of(t);
    for (int s = 0; s < 2; ++s)
        if (k.s[s].kind == SK_DASH || (k.s[s].kind == SK_AREA && champ(t).missing_hp)) return s;
    return -1;
}
static float spell_burst(CT t, float focus) { const float k = champ(t).burst; return k != 0.f ? k * focus : 0.f; }
static int area_slot(CT t) { return champ(t).area_slot; }
static float area_burst(CT t, float focus) { const float k = champ(t).area_k; return k != 0.f ? k * focus : 0.f; }
// Average spell damage per round (a self-shield is not a spell).
// the spell part of champ_dps only (Focus x fd_f8 / 8). It returned 0 for any type with a self-shield, which
// giving HoR shield_f would drop its dash landing from the micro (sp_avg, target values, FINM).
static float spell_avg(CT t, float focus) { return focus * champ(t).fd_f8 / 8; }
static float type_mult(CT t) { return champ(t).target_mult; }
