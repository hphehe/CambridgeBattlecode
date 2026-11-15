#pragma once
// Included by banking.h: how big a bag should be before it is worth a trip. banking.h decides when a trip starts.
// ---------------------------------------------------------------------------------------------
// Bank engine: the draft sim + the live KDA + one adaptable curve. A bag carried away from banking dies with logit
// c0 - 3.859 share + 0.517 rate + 0.275 visited - 0.740 log(1 + altar distance) - 0.562 (3+ altars) on 242 games
// (fitted on 338639 carrying rounds).
// The KDA moves only on evidence: each game has its own kill share (their deaths / all) and intensity (deaths per 100
// rounds), and real games differ by sd 0.22 and 0.95 (death noise taken out), so the live estimates start at
// the average carrying round and the deaths pull them away: share = (E + 4.2 x 0.589) / (E + A + 4.2), rate =
// 100 (E + A + 2.45) / (round + 194). A 1-0 or 1-1 opening barely moves them; 8-2 or 22-16 by round 800 does.
// Carrying near an altar is the dangerous place (3.1 deaths / 1000 rounds within 5 tiles, 0.6 at 21-35): near one, bank
// smaller (the trip is short too); far from one, carry more. Maps with 3+ altars spread the enemies out. The risk is flat
// in the bag's size and a bag ~ 1 / sqrt(risk), so every threshold that starts a bank trip scales by
// exp(half the fitted terms, each from its carrying-round mean): 1 at an average carrying round, the opening included.
// Deaths are counted by every champion on every turn, alive or dead (main.cpp).
// ---------------------------------------------------------------------------------------------
static constexpr float BE_SH_K = 3.859f, BE_SH_M = 0.589f, BE_SH_N = 4.2f;      // kill share: slope, mean, prior (deaths)
static constexpr float BE_RT_K = 0.517f, BE_RT_M = 1.265f, BE_RT_N = 194.f;     // intensity: slope, mean, prior (rounds)
static constexpr float BE_RT_MAX = 3.2f;                                        // the carrying rounds' 99th percentile
static constexpr float BE_VIS_K = 0.275f, BE_VIS_REF = 0.74f;                   // 74% of carrying rounds: a visited altar
static constexpr float BE_DIST_K = 0.740f, BE_DIST_REF = 2.622f;                // log(1 + tiles to the nearest altar), mean 12.8
static constexpr float BE_MANY_K = 0.562f, BE_MANY_REF = 0.352f;                // 3+ altars: 35% of carrying rounds
static int BE_E = 0, BE_A = 0;   // deaths so far: theirs, ours
static bool BE_EDOWN[8] = {false}, BE_ADOWN[8] = {false};
static void be_count() {
    for (int i = 0; i < (int)ENEMY.size() && i < 8; ++i) {
        bool alive = true;
        try { alive = ENEMY[i].h.is_alive(); } catch (const std::exception&) {}
        if (!alive && !BE_EDOWN[i]) ++BE_E;
        BE_EDOWN[i] = !alive;
    }
    for (int i = 0; i < (int)ALLY.size() && i < 8; ++i) {
        bool alive = true;
        try { alive = i == MY_SLOT ? bc::self().is_alive() : ALLY[i].h.is_alive(); } catch (const std::exception&) {}
        if (!alive && !BE_ADOWN[i]) ++BE_A;
        BE_ADOWN[i] = !alive;
    }
}
static float be_share() { return ((float)BE_E + BE_SH_N * BE_SH_M) / ((float)(BE_E + BE_A) + BE_SH_N); }
static float be_rate() {
    const float r = 100.f * ((float)(BE_E + BE_A) + BE_RT_M / 100.f * BE_RT_N) / ((float)rnd() + BE_RT_N);
    return std::min(BE_RT_MAX, r);
}
// the altar we would bank at, by the draft sim: 1 enemy-visited, 0 quiet (no sim data: the games' mean)
static float be_visited() {
    const vec2 a = nearest_altar();
    const int t = a.x >= 0 ? be_traffic(a) : -1;
    return t < 0 ? BE_VIS_REF : t >= ALTAR_ENEMY_VISITED ? 1.f : 0.f;
}
static float be_scale(float visited) {
    const vec2 a = nearest_altar();
    const float d = a.x >= 0 ? (float)std::min(45, std::max(3, cheb(a, ME.p))) : 12.8f;   // the fitted range, 3-45 tiles
    const float many = altars.size() >= 3 ? 1.f : 0.f;
    return std::exp(0.5f * (BE_SH_K * (be_share() - BE_SH_M) - BE_RT_K * (be_rate() - BE_RT_M) - BE_VIS_K * (visited - BE_VIS_REF)
                            + BE_DIST_K * (std::log1p(d) - BE_DIST_REF) + BE_MANY_K * (many - BE_MANY_REF)));
}
static float be_scale() { return be_scale(be_visited()); }
// The counter and the scales cost fuel, and fuel spent before the decisions costs games whatever it buys: they run once
// at the end of the turn (main loop) and the thresholds read the result (the next turn starts where this one ends).
static float BE_SC = 1.f;   // the curve: exp(-1/2 the hazard's deviation) here, now (1 at an average carrying round)
// The ideal bag. A bag q carried a round is lost with the carry hazard lam = BE_LAM0 / BE_SC^2 and handed to the killer
// (2q in the race); a trip costs the farming of its rounds (at our rate while farming, f) and its death chance x the
// respawn's 60 rounds. Cost per gem banked K/Q + lam Q / f is least at Q* = f sqrt((trip rounds + 60 p_trip) / lam).
// A bag under it farms on (or banks when a trip is nearly free); over it, it goes. The live inputs are cached at the end
// of the turn; a decision costs one square root.
static constexpr float BE_LAM0 = 1.46e-3f;              // carry deaths per carrying round, the 242 games (495 / 338639)
static constexpr float BE_F0 = 0.3f, BE_F_N = 100.f;     // farm rate prior: 0.3 gems a farming round, worth 100 of them
static constexpr float BE_TRIP0 = -4.70f, BE_TRIP_V = 1.173f, BE_TRIP_W = 0.027f;   // trip death logit:
static constexpr float BE_TRIP_VREF = 0.73f, BE_TRIP_WREF = 25.8f;                 // mean 0.9%, visited, walk (steps)
static constexpr float BE_QMAX = 100.f;                  // bags past 100 are outside the fitted trips (p90 71-77)
static float BE_GOT = 0.f, BE_FARM = 0.f, BE_Q0 = 7.8f, BE_P60 = 0.5f;
static int BE_PREV_G = -1;
static void be_end_turn() {
    be_count();
    const bool alive = MY_SLOT >= 0 && MY_SLOT < 8 && !BE_ADOWN[MY_SLOT];
    // our rate while farming: gems gained over rounds alive and not on a bank trip, from the first gem of the game on
    // (the walk out of the spawn and the trips farm nothing whatever the bag; tributes drop the bag, no loss of rate)
    if (alive) {
        if (BE_PREV_G >= 0 && ME.gems > BE_PREV_G) BE_GOT += (float)(ME.gems - BE_PREV_G);
        BE_PREV_G = ME.gems;
        if (BE_GOT > 0.f && !banking) BE_FARM += 1.f;
    } else BE_PREV_G = 0;
    BE_SC = be_scale();
    const float f = (BE_GOT + BE_F0 * BE_F_N) / (BE_FARM + BE_F_N);
    BE_Q0 = f * BE_SC / std::sqrt(BE_LAM0);
    const int da = F_ALTAR.at(ME.p);
    const float w = da == UNREACH ? 80.f : (float)std::min(da, 80);
    BE_P60 = 60.f / (1.f + std::exp(-(BE_TRIP0 + BE_TRIP_V * (be_visited() - BE_TRIP_VREF) + BE_TRIP_W * (w - BE_TRIP_WREF))));
}
// the ideal bag for a trip that costs t rounds of farming from here
static float be_ideal(int t) { return std::clamp(BE_Q0 * std::sqrt((float)std::max(0, t) + BE_P60), 1.f, BE_QMAX); }
// a bank trip's rounds from here: there (a recall or the crystal may shorten it), the tribute, and on to where we farm
// next. Holding a job: there + (altar -> job) - (here -> job), the walk to the job being made anyway, so an altar on the
// way costs only its tribute and one the other way the detour; no job (or no field to it): the walk back here.
static int be_trip(int dw, int dt, int gems) {
    int on = dw;
    if (JOB >= 0 && JOB < (int)G.size() && G[JOB].fid >= 0) {
        const int dj = gdist(G[JOB], ME.p);
        const vec2 a = nearest_altar();
        if (dj < 900 && a.x >= 0) {
            int best = UNREACH;
            for (auto d : DIRS) { const vec2 q = a + d; if (inb(q)) best = std::min(best, (int)GF[G[JOB].fid].at(q)); }
            if (best != UNREACH) on = best - dj;
        }
    }
    return std::max(0, dt + on) + tribute_turns(gems);
}

// The live situation: carry deaths per carrying round with enemies within 8 now, by 1 / 2+ enemies and 0 / 1 / 2+
// allies within 8 (fitted on 242 games; ~0 with none), against the average carrying round's BE_LAM0 the
// ideal bag is built on; both carry this game's kill balance and intensity. The ideal bag goes as 1 / sqrt(hazard).
static constexpr float BE_HZ[2][3] = {{4.23e-3f, 3.77e-3f, 2.90e-3f}, {10.08e-3f, 9.92e-3f, 6.07e-3f}};
static float be_near_scale() {
    int ne = 0, na = 0;
    for (auto& e : ENEMY) if (e.vis && e.alive && e.hp > 0 && !on_enemy_spawn(e.p) && cheb(e.p, ME.p) <= 8) ++ne;
    if (ne == 0) return 1.f;
    for (int i = 0; i < (int)ALLY.size(); ++i)
        if (i != MY_SLOT && ALLY[i].vis && ALLY[i].alive && !ALLY[i].channel && cheb(ALLY[i].p, ME.p) <= 8) ++na;
    const float kd = std::exp(-BE_SH_K * (be_share() - BE_SH_M) + BE_RT_K * (be_rate() - BE_RT_M));
    const float near = BE_HZ[ne >= 2 ? 1 : 0][std::min(na, 2)] * kd;
    const float avg = BE_LAM0 / std::max(0.05f, BE_SC * BE_SC);
    return near > avg ? std::sqrt(avg / near) : 1.f;
}
