// The job engine: which camp a champion works next.
//   when to plan       update_job (brain_turn.h)
//   which job          choose_job (the end of this file)
//   what a job is worth  job_score (brain_job_score.h)
//   doing the job      farm_turn (brain_turn.h)
// A strategy that picks for the team gets its own step in choose_job.
#pragma once

// ---------------------------------------------------------------------------------------------
// The leader: the lowest live slot (fight_mono5 lets it alone wait for the team at a reach-5 monolith gate).
// ---------------------------------------------------------------------------------------------
static bool is_leader() {
    for (int s = 0; s < (int)ALLY.size(); ++s) {
        if (!ALLY[s].alive && s != MY_SLOT) continue;
        return s == MY_SLOT;
    }
    return false;
}

// A camp fight round by round (jp_sim_clear): the camp's damage is shared by the bodies still in; a body leaves
// at max(30%, three rounds of its share) (the LOWHP rule) and takes its damage with it. Feasible = every monster dies.
static bool sim_core(float* hp, const float* dps, int nb, const Group& g) {
    static std::vector<float> st, h;
    if ((int)st.size() < g.n) { st.resize((size_t)g.n); h.resize((size_t)g.n); }
    int n = 0;
    for (int k = 0; k < g.n; ++k) {
        const Mon& m = g.m[k];
        if (m.mhp <= 0) continue;
        // In a live group, hp==0 means this member has already died. Do not
        // reinsert its full design health into the round simulation; that
        // made jackpot feasibility reject partially cleared camps.
        if (g.st == ST_ALIVE && m.hp <= 0) continue;
        st[n] = m.str; h[n] = g.st == ST_ALIVE ? m.hp : m.mhp;
        for (int j = n; j > 0 && st[j] * h[j - 1] > st[j - 1] * h[j]; --j) { std::swap(st[j], st[j - 1]); std::swap(h[j], h[j - 1]); }
        ++n;
    }
    float mh[8];
    bool in[8];
    for (int b = 0; b < nb; ++b) { mh[b] = hp[b]; in[b] = true; }
    int k = 0, r = 0;
    while (k < n && r < 400) {
        ++r;
        int cnt = 0;
        float d = 0;
        for (int b = 0; b < nb; ++b) if (in[b]) { ++cnt; d += dps[b]; }
        if (cnt == 0) break;
        float inc = 0;
        for (int j = k; j < n; ++j) inc += st[j];
        const float each = inc / cnt;
        for (int b = 0; b < nb; ++b) if (in[b]) {
            hp[b] -= each;
            if (hp[b] < std::max(0.3f * mh[b], 3.f * each)) in[b] = false;
        }
        while (d > 0 && k < n) { const float x = std::min(d, h[k]); h[k] -= x; d -= x; if (h[k] <= 0.5f) ++k; }
    }
    return k >= n;
}
// A type's design Strength (jp_sim_clear scales each body's damage by its Strength over this: items and shards count).
static float design_str(CT t) {
    static float v[16] = {};
    const int k = (int)t;
    if (k < 0 || k >= 16) return 40.f;
    if (v[k] <= 0.f) { v[k] = 40.f; try { const float b = bc::champion(t).base_stats().base_strength; if (b > 0) v[k] = b; } catch (...) {} }
    return v[k];
}
// The average body of our line-up at base stats (maximum health, camp damage per round). Every one of us knows the
// line-up, so we all count the same camps as team camps (allies' live stats are known only in vision).
static void lineup_body(float& H, float& D) {
    static float h = 0, d = 0;
    if (h <= 0) {
        int nb = 0;
        for (int s = 0; s < (int)ALLY.size(); ++s) {
            const CT ty = s == MY_SLOT ? ME.type : ALLY[s].type;
            float bh = 0;
            try { bh = bc::champion(ty).base_stats().base_health; } catch (...) {}
            h += bh > 0 ? bh : 800.f; d += base_dps(ty); ++nb;
        }
        if (nb > 0) { h /= nb; d /= nb; }
    }
    H = std::max(1.f, h); D = std::max(10.f, d);
}
// Bodies of that average body a camp needs (the team_needed count: k bodies clear it in hp / (k D) rounds and share its
// damage, each may lose CAMP_SOAK_SHARE of its health, so k^2 >= damage x hp / (soak H D)). Used by team_camp_group.
static int party_need(const Group& g, float hp) {
    if (g.str_total <= 0 || hp <= 0) return 1;
    float H, D;
    lineup_body(H, D);
    const float x = g.str_total * hp / (CAMP_SOAK_SHARE * H * D);
    return std::clamp((int)std::ceil(std::sqrt(x)), 1, 9);
}
// the jackpot fight (all_in_job): our whole roster with live Strength and maximum health (dead allies respawn) in sim_core.
static bool jp_sim_clear(const Group& g) {
    float hp[8], dps[8];
    int nb = 0;
    for (int s = 0; s < (int)ALLY.size() && nb < 8; ++s) {
        const CT ty = s == MY_SLOT ? ME.type : ALLY[s].type;
        const float cs = s == MY_SLOT ? ME.str : ALLY[s].str, cm = s == MY_SLOT ? ME.mhp : ALLY[s].mhp;
        const float st = cs > 0 ? cs : design_str(ty);
        dps[nb] = base_dps(ty) * std::max(1.f, st / design_str(ty)) * JACKPOT_SIM_DPS_PERCENT / 100.f;
        hp[nb] = cm > 1.f ? cm : 800.f;
        ++nb;
    }
    return sim_core(hp, dps, nb, g);
}
// Score nearest camps first when the fuel floor truncates the job pass.
// Cache each key once: querying group distance in a sort comparator was costly.
static std::vector<std::pair<int, int>> job_scan_order(vec2 p, int current) {
    std::vector<std::pair<int, int>> order;
    order.reserve(G.size());
    for (const auto& g : G) if (g.id != current) order.emplace_back(gdist(g, p), g.id);
    // std::sort on these pairs cost ~500 fuel a group in wasm. The list is in id order, so a
    // stable radix sort on the distance (two 5-bit passes, distances < 1024) gives the same (distance, id) order.
    for (const auto& o : order) if (o.first < 0 || o.first >= 1024) { std::sort(order.begin(), order.end()); return order; }
    static std::vector<std::pair<int, int>> tmp;
    tmp.resize(order.size());
    for (int shift = 0; shift < 10; shift += 5) {
        int cnt[33] = {};
        for (const auto& o : order) ++cnt[((o.first >> shift) & 31) + 1];
        for (int b = 0; b < 32; ++b) cnt[b + 1] += cnt[b];
        for (const auto& o : order) tmp[(size_t)cnt[(o.first >> shift) & 31]++] = o;
        order.swap(tmp);
    }
    return order;
}

// The best follow-up from group j's draft neighbours, as a rate and its rounds: the value at P(get) 0.75, the walk
// at weight 1 (the two-step rate stands in for JOB_TRAVEL_WEIGHT's "a far job also leaves us far from the next one"), the
// wait for a respawn after j's end, my clear and the capture. Ally-claimed, monolith and one-body-unsafe camps are out.
static float geo_follow(int j, float& rounds) {
    rounds = 0.f;
    if (j < 0 || (size_t)(j + 1) * 2 * GEO_K > GEO_NB.size()) return 0.f;
    const int r = rnd();
    const float gw = ANY_GEM_GROUP ? groi::gw_job(r) : 1.0f;
    const int reach = attack_reach(ME.melee);
    const float end = (float)r + GEO_DEN[(size_t)j];
    float best = 0.f;
    for (int q = 0; q < GEO_K; ++q) {
        const int k = (int)GEO_NB[(size_t)j * 2 * GEO_K + 2 * q] - 1, d = GEO_NB[(size_t)j * 2 * GEO_K + 2 * q + 1];
        if (k < 0 || k >= (int)G.size() || k == j) continue;
        const Group& h = G[(size_t)k];
        if (h.monolith || (k < (int)HELP.size() && HELP[(size_t)k].n > 0)) continue;
        const float walk = (float)std::max(0, d - reach);
        float wait = 0.f;
        if (h.st == ST_RESPAWN) { wait = (float)h.ready_at - (end + walk); if (wait > JOB_PRICE_WAIT) continue; wait = std::max(0.f, wait); }
        const float hp = h.st == ST_ALIVE ? h.hp_left : h.hp_total;
        const float clear = hp / std::max(10.f, MYDPS);
        if (h.str_total > 0 && h.str_total * 0.75f * clear > ME.mhp * 0.8f) continue;   // not one body's camp (the opening's test)
        float v = 0.75f * grole_value(ME.type, r, (float)h.gems, (float)h.gold, gw);
        // a camp this pass scored counts at its own net value (the job engine's price), never above it
        if (k < (int)GEO_AT.size() && GEO_AT[(size_t)k] == GEO_PASS && k < (int)SC.size()) {
            if (SC[(size_t)k] <= 0.f) continue;
            v = std::min(v, SC[(size_t)k] * GEO_DEN[(size_t)k]);
        }
        if (v <= 0) continue;
        const float t = walk * GEO_WALK_WEIGHT + wait + clear + (float)capture_rounds() + 2.f;
        if (v / t > best) { best = v / t; rounds = t; }
    }
    return best;
}
// The scan's best GEO_RERANK_COUNT camps priced in full, re-ranked by the rate of the job and then its best follow-up:
// (score x time + gamma x rate_f x rounds_f) / (time - 0.4 walk + gamma x rounds_f). A follow-up never counts below
// GEO_FOLLOWUP_FLOOR x the pass's median positive score over GEO_FOLLOWUP_ROUNDS rounds (we can always go and earn about that).
static int geo_rerank(int best) {
    if (!GEO_OK || best < 0 || best >= (int)GEO_AT.size() || GEO_AT[(size_t)best] != GEO_PASS) return best;
    int top[GEO_RERANK_COUNT], nt = 0;
    static std::vector<float> pos;
    pos.clear();
    for (int g = 0; g < (int)SC.size() && g < (int)GEO_AT.size(); ++g) {
        const float s = SC[(size_t)g];
        if (GEO_AT[(size_t)g] != GEO_PASS || s <= 0.f) continue;
        pos.push_back(s);
        int q;
        if (nt < GEO_RERANK_COUNT) q = nt++;
        else if (s > SC[(size_t)top[GEO_RERANK_COUNT - 1]]) q = GEO_RERANK_COUNT - 1;
        else continue;
        while (q > 0 && SC[(size_t)top[q - 1]] < s) { top[q] = top[q - 1]; --q; }
        top[q] = g;
    }
    if (nt == 0) return best;
    std::nth_element(pos.begin(), pos.begin() + (long)(pos.size() / 2), pos.end());
    const float floor_rate = GEO_FOLLOWUP_FLOOR * pos[pos.size() / 2];
    int pick = best;
    float ps = -1e30f;
    for (int i = 0; i < nt; ++i) {
        const int j = top[i];
        float ft = 0.f, fr = geo_follow(j, ft);
        if (fr < floor_rate) { fr = floor_rate; ft = GEO_FOLLOWUP_ROUNDS; }
        const float den = GEO_DEN[(size_t)j];
        const float s2 = (SC[(size_t)j] * den + GEO_FOLLOWUP_WEIGHT * fr * ft) /
                         std::max(1.f, den - (JOB_TRAVEL_WEIGHT - GEO_WALK_WEIGHT) * GEO_TW[(size_t)j] + GEO_FOLLOWUP_WEIGHT * ft);
        if (s2 > ps) { ps = s2; pick = j; }
    }
    return pick;
}

// The roaming team (team-camp maps): everyone on the same group.
static int team_camp_job() {
    const bool on = (PLAN_FLAGS & MF_TEAM) && rnd() < TEAM_CAMP_UNTIL_ROUND && DOM_G < 0 &&
                    GT_PARTY >= TEAM_CAMP_MIN_PARTY && (int)ALLY.size() >= TEAM_CAMP_MIN_BODIES;
    if (!on) return -1;
    const int tg = team_camp_group();
    if (tg < 0) return -1;
    Group& g = G[tg];
    const float hp_left = g.st == ST_ALIVE ? g.hp_left : 0.f;
    g.kmin = std::clamp(team_needed(g, hp_left, MYDPS), 1, std::max(1, TEAM_CAMP_BODIES > 0 ? std::min(TEAM_CAMP_BODIES, ALIVE_ALLIES) : ALIVE_ALLIES));
    return tg;
}

// Can the whole team win the fight at the jackpot? Asked every ALLIN_RECHECK_ROUNDS rounds.
static void recheck_all_in_fight() {
    static int checked_at = -100000;
    if (rnd() - checked_at < ALLIN_RECHECK_ROUNDS) return;
    checked_at = rnd();
    const Group& dg = G[DOM_G];
    bool ok = !dg.monolith;
    for (int k = 0; k < dg.n && ok; ++k) if (dg.m[k].kind == K_MONO || dg.m[k].kind == K_GOLEM) ok = false;
    JP_FIGHT = ok && jp_sim_clear(dg);
}
// All in on the one group that holds most of the map's gems: everyone, once the first gold is spent.
static int all_in_job() {
    if (DOM_G < 0) return -1;
    recheck_all_in_fight();
    if (!JP_FIGHT || rnd() < ALLIN_FROM_ROUND || rnd() < G[DOM_G].skip_until) return -1;
    Group& dg = G[DOM_G];
    const int t = gdist(dg, ME.p);
    if ((dg.st != ST_RESPAWN || dg.ready_at - rnd() <= t + 5) && job_score(dg) > 0.f) {
        dg.kmin = std::max(2, ALIVE_ALLIES - 1);
        return DOM_G;
    }
    return -1;
}

// The opening job from the draft plan: distinct groups on our side, one per body.
static int opening_job() {
    if (OPEN_G < 0 || opening_done) return -1;
    Group& og = G[OPEN_G];
    if (rnd() < OPENING_JOB_UNTIL_ROUND && og.st != ST_RESPAWN && job_score(og) > 0) return OPEN_G;
    opening_done = true;
    return -1;
}

// Every camp scored into SC, the current job first and then nearest first, down to the fuel floor (job_score costs about
// 2.8k fuel a camp). Nearest first also keeps a fuel-limited scan from depending on the map's group numbering.
// The best positive score's camp, -1 if none.
static int scan_jobs() {
    int best = -1;
    float bs = 0;
    SC.assign(G.size(), -1.f);
    for (auto& g : G) g.p_wait = -1;
    const auto order = job_scan_order(ME.p, JOB);
    int scored = 0;
    if (JOB >= 0 && JOB < (int)G.size()) {
        const float s = job_score(G[JOB]);
        ++scored;
        SC[JOB] = s;
        if (s <= 0) G[JOB].p_wait = -1;
        if (s > bs) { bs = s; best = JOB; }
    }
    JM_PRUNED.assign(G.size(), 0);
    JM_EARLY.assign(G.size(), 0);
    for (const auto& key : order) {
        if ((scored >= 8 && (long)bc::fuel_remaining() < (long)JOB_SCAN_FUEL_FLOOR) || (long)bc::fuel_remaining() < JOB_SCAN_FUEL_HARD_FLOOR) break;
        auto& g = G[key.second];
        JM_BAR = bs;
        const float s = job_score(g);
        JM_BAR = JM_NO_BAR;
        ++scored;
        SC[g.id] = s;
        if (s <= 0) g.p_wait = -1;
        if (s > bs) { bs = s; best = g.id; }
    }
    return best;
}

// The best job is still respawning: a side job that fits into the wait and gets us back in time. -1 if none.
static int side_job(int best) {
    const Group& A = G[best];
    int side = -1;
    float sv = 0;
    for (auto& g : G) {
        if ((long)bc::fuel_remaining() < JOB_SCAN_FUEL_FLOOR) break;
        if (g.id < (int)JM_EARLY.size() && JM_EARLY[g.id] && g.id != best) {
            // the early bound skipped it: score it in full as the scan would have (only if the trip can fit)
            JM_EARLY[g.id] = 0;
            if ((float)gdist(g, ME.p) + capture_rounds() + gdist(A, g.m[0].p) > A.p_wait + SIDE_JOB_SLACK) continue;
            if (job_score(g) <= 0.f) g.p_wait = -1;
        }
        if (g.id == best || g.p_wait != 0 || g.kmin > 1 || HELP[g.id].n > 0) continue;
        const float t_s = (float)gdist(g, ME.p) + g.p_clear + capture_rounds() + gdist(A, g.m[0].p);
        if (t_s > A.p_wait + SIDE_JOB_SLACK) continue;
        if (g.id < (int)JM_PRUNED.size() && JM_PRUNED[g.id] && job_score(g) <= 0.f) continue;   // pruned, scored in full
        const float v = g.p_value / std::max(1.f, t_s);
        if (v > sv) { sv = v; side = g.id; }
    }
    return side;
}

// The team's strategies pick first, in this order. Otherwise every camp is scored and the best one wins.
static int choose_job() {
    ++GEO_PASS;
    tally_help();
    MYDPS = my_dps_est();
    int pick = squad_job();
    if (pick < 0) pick = all_in_job();
    if (pick >= 0) return pick;
    int best = scan_jobs();
    if (best < 0) return -1;
    best = geo_rerank(best);
    if (G[best].p_wait > SIDE_JOB_MIN_WAIT) {
        const int side = side_job(best);
        if (side >= 0) return side;
    }
    return best;
}
