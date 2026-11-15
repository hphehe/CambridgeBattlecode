// Draft: one fixed line-up for every map (LINEUP below), and the map plan for the champions (plan.h).
#pragma once
#include "plan.h"

// Our line-up, picked in this order (a map with fewer picks takes the first ones).
// the same line-up on every map. The KIT (Ren lead, kit_ratio < 1) and STAMP (Prism, dom_map) rows are gone:
// on their 11 maps the NORMAL line-up was level with them (125 vs 129 of 198 in benches, head-to-head
// 72/132 for NORMAL).
// Heart of Ruin in every line-up (5v5 for Ryth, 3v3 for Varg, 4v4 for Vendetta), as the ladder teams run it
// no Varg in any line-up, as the ladder teams above us run it (all HoR, almost no Varg)
static const CD LINEUP[5] = {CD::vesper_vale, CD::marcella, CD::heart_of_ruin, CD::vendetta, CD::ryth};   // Marcella 2nd, Vendetta 4th

// keep play_draft out of main() (a bigger write_plan let the compiler inline play_draft
// into main, which changed main's stack frame and so the games: a fuel/stack butterfly, not a behaviour change)
__attribute__((noinline))
static void play_draft() {
    static bool plan_written = false;
    const auto mine = bc::draft::current_draft(team::ally);
    const int size = (int)bc::draft::team_size();
    std::vector<CD> lineup(LINEUP, LINEUP + std::min(5, std::max(0, size)));
    static bool logged = false;
    if (!logged) { logged = true; }
    // the first call has 100x the fuel (engine 2.0.0): analyse the map before picking (the pick ends the turn's actions)
    if (!plan_written && bc::fuel_remaining() > 400000) {
        plan_written = true;
        try { write_plan(lineup); } catch (const std::exception& e) { }
    }
    static const CD fallback[] = {CD::varg, CD::vendetta, CD::cassi_and_issac, CD::ren, CD::stellar, CD::ryth, CD::tavi,
                                  CD::marcella, CD::vesper_vale, CD::prism, CD::willow};
    auto taken = [&](CD c) { return std::find(mine.begin(), mine.end(), c) != mine.end(); };
    bool picked = false;
    for (auto c : lineup) if (!taken(c)) { bc::draft::pick(c); picked = true; break; }
    if (!picked) for (auto c : fallback) if (!taken(c)) { bc::draft::pick(c); break; }
}
