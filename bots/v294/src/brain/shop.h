// item builds and shopping.
#pragma once
#include "../game/world.h"

static std::vector<IT> BUILD;
static size_t build_pos = 0;

static void rep(IT it, int n) { for (int i = 0; i < n; ++i) BUILD.push_back(it); }
static size_t jungle_end = 10;   // build position after which the Jungle emblem is owned (old code: 10 for everyone)
static bool has_jungle() { return build_pos >= jungle_end; }

// Prospector pays 25% of what clusters drop, Treasure 10% of all gold. The first emblem is the one that pays more on the camps
// this team will farm: those nearer to us than to the enemy, each by how often it refills. A gem is priced as everywhere else
// (GEMS_PER_GOLD). A champion's own gold bonus multiplies Prospector's extra cluster gold.
static constexpr float CLUSTER_GOLD = 20.f, CLUSTER_GEMS = 7.f;   // what one cluster drops (rules, camps table)
static bool prospector_pays_more() {
    const float bonus = champ(ME.type).gold_bonus;
    float prospector = 0.f, treasure = 0.f;
    for (const Group& g : G) {
        if (g.d_home > g.d_away || g.respawn <= 0) continue;
        const float per_round = 1.f / (float)g.respawn;
        for (int k = 0; k < g.n; ++k) {
            const Mon& m = g.m[k];
            if (m.kind == K_GOLD) prospector += per_round * 0.25f * bonus * CLUSTER_GOLD;
            else if (m.kind == K_GEM) prospector += per_round * 0.25f * CLUSTER_GEMS / GEMS_PER_GOLD;
        }
        treasure += per_round * 0.10f * (float)g.gold;
    }
    return prospector > treasure;
}
static IT gold_emblem_swapped(IT it) {
    switch (it) {
        case IT::prospector_shard_of_strength: return IT::treasure_shard_of_strength;
        case IT::prospector_shard_of_focus: return IT::treasure_shard_of_focus;
        case IT::prospector_shard_of_health: return IT::treasure_shard_of_health;
        case IT::treasure_shard_of_strength: return IT::prospector_shard_of_strength;
        case IT::treasure_shard_of_focus: return IT::prospector_shard_of_focus;
        case IT::treasure_shard_of_health: return IT::prospector_shard_of_health;
        default: return it;
    }
}
// Put the better one first. A champion that buys both swaps the two families; each slot keeps its stat.
static void order_gold_emblems() {
    if (BUILD.empty() || gold_emblem_swapped(BUILD[0]) == BUILD[0]) return;
    const bool first_is_prospector = BUILD[0] == IT::prospector_shard_of_strength || BUILD[0] == IT::prospector_shard_of_focus || BUILD[0] == IT::prospector_shard_of_health;
    if (first_is_prospector == prospector_pays_more()) return;
    const size_t blocks = BUILD.size() > 5 && gold_emblem_swapped(BUILD[5]) != BUILD[5] ? 10 : 5;
    for (size_t i = 0; i < blocks && i < BUILD.size(); ++i) BUILD[i] = gold_emblem_swapped(BUILD[i]);
}

// The item build is the champion's row in champions.h.
static void init_build_plan() {
    BUILD.clear();
    const ChampionInfo& k = champ(ME.type);
    for (const BuildStep* b = k.build; b->n > 0; ++b) rep(b->item, b->n);
}

// Derive ownership from the actual plan. Monolith and no-Jungle caster builds
// must never receive the Jungle damage reduction just for buying ten items.
static void finish_build_plan() {
    jungle_end = (size_t)-1;
    int counts[3] = {};
    for (size_t i = 0; i < BUILD.size(); ++i) {
        int stat = BUILD[i] == IT::jungle_shard_of_strength ? 0 :
                   BUILD[i] == IT::jungle_shard_of_focus ? 1 :
                   BUILD[i] == IT::jungle_shard_of_health ? 2 : -1;
        if (stat >= 0 && ++counts[stat] == 5) { jungle_end = i + 1; break; }
    }
}
static void init_build() { init_build_plan(); order_gold_emblems(); finish_build_plan(); }

// a fight is worth about damage x health, so the next block goes to the stat with the larger relative gain
// (damage weighted HEALTH_BALANCE_RATIO, since farming needs only damage). Decided at each Power block start, health blocks are
// inserted before the build's next Power damage block.
static int hpb_left = 0;    // Power Health shards still to buy in the current block
static int pdmg_run = 0;    // Power damage shards bought (block start when % 5 == 0)
static bool is_pdmg(IT it) { return it == IT::power_shard_of_strength || it == IT::power_shard_of_focus; }
// One live record at each policy decision, including all completed-emblem bonuses.
// Partial blocks keep their decision; no stat estimate is mutated before a purchase.
struct ShopStats { float health, strength, focus; };
static ShopStats shop_stats() {
    const auto& me = bc::self();
    bc::detail::champion_info ci{};
    const auto handle = static_cast<uint32_t>(static_cast<bc::handle_champion_t>(me));
    bc::detail::check(bc::detail::sdk_champion(handle, &ci));
    return {ci.max_health, ci.strength, ci.focus};
}
// Purchases change the live champion immediately. Keep the combat observation
// current for main.cpp's same-turn think(), without resetting action allowances
// or the previous observation used to infer damage between turns.
static void refresh_shop_combat_stats() {
    const auto& me = bc::self();
    bc::detail::champion_info ci{};
    const auto handle = static_cast<uint32_t>(static_cast<bc::handle_champion_t>(me));
    bc::detail::check(bc::detail::sdk_champion(handle, &ci));
    ME.hp = ci.health; ME.mhp = ci.max_health;
    ME.str = ci.strength; ME.focus = ci.focus;
}
// Buy as much of the build as gold allows. Only valid on our spawn, while dead, or for Tavi anywhere.
static int shop_now() {
    auto& me = bc::self();
    int bought = 0;
    // one teleport crystal (10 gold) before anything else, on spawn or while dead: when it shortens the bank trip from
    // our spawn (lfield.h)
    if (lf::tp_worth() && !HAS_TP && (!me.is_alive() || (inb(ME.p) && OWN_SPAWN[idx(ME.p)]))) {
        try {
            HAS_TP = me.consumables()[bc::consumable_type::tp_crystal].count() > 0;
            if (!HAS_TP && me.can_buy(bc::consumable_type::tp_crystal)) {
                me.buy(bc::consumable_type::tp_crystal); HAS_TP = true;
            }
        } catch (const std::exception&) {}
    }
    float gh = 0, gs = 0, gf = 0;
    try { const auto st = bc::champion(ME.type).base_stats(); gh = st.health_growth; gs = st.strength_growth; gf = st.focus_growth; } catch (...) {}
    for (int guard = 0; guard < 40 && build_pos < BUILD.size(); ++guard) {
        if ((int)me.gold_count() < 30) break;
        const IT planned_item = BUILD[build_pos];
        IT it = planned_item;
        bool hpbuy = false;
        int next_hp_left = hpb_left;
        if (is_pdmg(planned_item) && gh > 0) {
            if (next_hp_left == 0 && pdmg_run % 5 == 0) {
                const ShopStats st = shop_stats();
                const bool foc = planned_item == IT::power_shard_of_focus;
                const float dv = foc ? st.focus : st.strength, gd = foc ? gf : gs;
                if (dv > 0 && gd > 0 && gh / std::max(1.f, st.health) > HEALTH_BALANCE_RATIO * gd / dv)
                    next_hp_left = 5;
            }
            if (next_hp_left > 0) { it = IT::power_shard_of_health; hpbuy = true; }
        }
        if (it == IT::power_shard_of_strength) it = IT::bruiser_shard_of_strength;
        else if (it == IT::power_shard_of_health) it = IT::bruiser_shard_of_health;
        else if (it == IT::power_shard_of_focus) it = IT::bruiser_shard_of_focus;
        bool ok = false;
        try { ok = me.can_buy(it, 1); } catch (const std::exception&) { ok = false; }
        if (!ok) break;
        try { me.buy(it, 1); } catch (const std::exception&) { break; }
        ++bought;
        if (hpbuy) {
            hpb_left = next_hp_left - 1;
            continue; // Inserted Health shards do not consume the planned damage item.
        }
        // Progress belongs to the planned damage block, even when Power becomes Bruiser.
        if (is_pdmg(planned_item)) ++pdmg_run;
        ++build_pos;
    }
    // carry one altar lock (10 gold): it cancels an enemy tribute in progress
    if (!champ(ME.type).remote_shop) {
        try {
            if (me.can_buy(bc::consumable_type::altar_lock)) { me.buy(bc::consumable_type::altar_lock); }
        } catch (const std::exception&) {}
    }
    if (bought) refresh_shop_combat_stats();
    ME.gold = (int)me.gold_count();
    return bought;
}
