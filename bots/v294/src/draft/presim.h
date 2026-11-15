// the draft's farming race, played the way our champions farm (choose_job / job_score / camp_combat / banking /
// shopping), for our line-up and its mirror at the enemy spawn, round by round over the first PRESIM_ROUNDS rounds, with the
// draft's spare fuel. Bodies walk the static distance fields; a team sees a group only when a body is within 7 of it
// (no shared vision with the other team). Where bodies of both teams work the same group in a round, that 10-round
// bucket goes into the group's mask: where we meet an enemy body if we come then.
// Fuel: every 30 rounds the sim projects the fuel to PRESIM_ROUNDS from the last 30 rounds; when it does not fit, it goes one
// level coarser (replans and looks less often, then 2- and 3-round steps). Below PRESIM_FUEL_FLOOR it stops; the buckets after
// that stay empty (no flag).
#pragma once

struct PSB {   // a body
    int team = 0, slot = 0; CT t{}; vec2 p{0, 0};
    float hp = 800, mhp = 800, gold = 0, spent = 0; int gems = 0;
    int job = -1, mode = 0, until = 0, last_plan = -100, open = -1, kmin = 1, gather_since = -1, reach = 1;
    bool open_done = false, melee = true;
};
enum { PS_FREE = 0, PS_GO, PS_CAP, PS_BANK, PS_TRIB, PS_RECALL, PS_HOME };
struct PSG { int st = 0; float hp = 0; int ready = 0; };   // st 0 alive, 1 reward, 2 respawning
struct PSV { int st = 0; float hp = 0; int ready = 0; int enemy_at = -1000; };

// geo: per group (the first GEO_MAXG), its GEO_K best follow-up groups by a static rate (gems + 0.5 gold) over
// (1.4 steps + hp / 60 + 6), with the steps between the two groups' nearest member tiles from the race's fields.
// Draft bytes [GEO_BASE] magic, [+1] K, [+2 + 8j + 2q] neighbour id + 1 (0 none), [+3 + 8j + 2q] steps (255 cap).
static constexpr int GEO_BASE = 586, GEO_K = 4, GEO_MAXG = 54;
static constexpr uint8_t GEO_MAGIC = 0x47;
__attribute__((noinline))
static void geo_table(std::array<uint8_t, 1024>& b, const std::vector<GI>& info, const std::vector<bc::map::camp_group>& groups,
                      int n, const std::vector<Field>& fg) {
    const int m = std::min(n, GEO_MAXG);
    std::vector<std::vector<vec2>> tiles(n);
    for (int i = 0; i < n; ++i) for (const auto& c : groups[i]) tiles[i].push_back(c.spawn_position());
    // steps from j's tiles to k's: k's field, else j's (a monolith or unreachable group has none)
    auto steps = [&](int j, int k) {
        int best = UNREACH;
        if (!fg[k].d.empty()) { for (auto p : tiles[j]) best = std::min<int>(best, fg[k].at(p)); }
        else if (!fg[j].d.empty()) { for (auto p : tiles[k]) best = std::min<int>(best, fg[j].at(p)); }
        return best;
    };
    for (int j = 0; j < m; ++j) {
        int id[GEO_K], dd[GEO_K]; float sc[GEO_K];
        for (int q = 0; q < GEO_K; ++q) { id[q] = -1; dd[q] = 255; sc[q] = 0.f; }
        for (int k = 0; k < n; ++k) {
            if (k == j || info[k].mono) continue;
            const float v = info[k].gems + 0.5f * info[k].gold;
            if (v <= 0) continue;
            const int d = steps(j, k);
            if (d >= 255) continue;
            const float s = v / (1.4f * d + info[k].hp / 60.f + 6.f);
            int q = GEO_K;
            while (q > 0 && s > sc[q - 1]) --q;
            if (q >= GEO_K) continue;
            for (int z = GEO_K - 1; z > q; --z) { id[z] = id[z - 1]; dd[z] = dd[z - 1]; sc[z] = sc[z - 1]; }
            id[q] = k; dd[q] = d; sc[q] = s;
        }
        for (int q = 0; q < GEO_K; ++q) {
            b[GEO_BASE + 2 + 8 * j + 2 * q] = (uint8_t)(id[q] + 1);
            b[GEO_BASE + 3 + 8 * j + 2 * q] = (uint8_t)std::min(dd[q], 255);
        }
    }
    b[GEO_BASE] = GEO_MAGIC; b[GEO_BASE + 1] = (uint8_t)GEO_K;
}

__attribute__((noinline))
static void pre_sim(std::array<uint8_t, 1024>& b, const std::vector<GI>& info, const std::vector<bc::map::camp_group>& groups,
                    int n, const std::vector<CT>& bodies, const Field& fh, const Field& fa, const Field& fl,
                    const std::vector<vec2>& home0, const std::vector<vec2>& home1, const int* open1) {
    if (n <= 0 || bodies.empty() || home0.empty() || home1.empty()) return;
    // one distance field per group (to its spawn tiles), group centres, monster counts
    std::vector<Field> fg(n);
    std::vector<vec2> ctr(n);
    std::vector<int> nm(n, 0), resp(n, 50);
    for (int i = 0; i < n; ++i) {
        bc::bitboard roots;
        int sx = 0, sy = 0;
        for (const auto& c : groups[i]) { const vec2 q = c.spawn_position(); roots.set(q, true); sx += q.x; sy += q.y; ++nm[i]; }
        if (nm[i]) ctr[i] = vec2{sx / nm[i], sy / nm[i]};
        resp[i] = (int)groups[i].max_respawn_timer();
        if (info[i].mono || info[i].dh >= 9999) continue;
        if ((long)bc::fuel_remaining() < 2000000) { return; }
        fg[i] = make_field(STATIC_BB, roots);
    }
    // before the race: the race fits itself to the fuel it is given, the opening needs its search
    if ((long)bc::fuel_remaining() > OP_FUEL_NEEDED) plan_opening(b, info, groups, n, bodies, fg);
    const int B = (int)bodies.size();   // team tm's bodies are sb[tm * B .. tm * B + B)
    const int capt = (int)bc::constants::capture_time;
    std::vector<PSB> sb;
    for (int tm = 0; tm < 2; ++tm)
        for (int k = 0; k < B; ++k) {
            PSB s; s.team = tm; s.slot = k; s.t = bodies[k];
            const auto& hv = tm == 0 ? home0 : home1;
            s.p = hv[k % hv.size()];
            s.mhp = s.hp = (float)bc::champion(bodies[k]).base_stats().base_health;
            s.melee = is_melee(bodies[k]); s.reach = attack_reach(s.melee);
            const int og = tm == 0 ? (int)b[5 + (int)bodies[k]] - 1 : open1[(int)bodies[k]];
            s.open = og >= 0 && og < n ? og : -1;
            sb.push_back(s);
        }
    std::vector<PSG> gs(n);
    std::vector<PSV> view[2] = {std::vector<PSV>(n), std::vector<PSV>(n)};
    for (int i = 0; i < n; ++i) { gs[i].hp = info[i].hp; view[0][i].hp = view[1][i].hp = info[i].hp; }
    std::vector<int> skip(2 * B * n, 0);
    std::vector<uint32_t> mask(n, 0), pres[2] = {std::vector<uint32_t>(n, 0), std::vector<uint32_t>(n, 0)};
    // per team: distances from its home and from the other home
    auto dh_of = [&](int tm, int g) { return tm == 0 ? info[g].dh : info[g].da; };
    auto da_of = [&](int tm, int g) { return tm == 0 ? info[g].da : info[g].dh; };
    const Field* home_f[2] = {&fh, &fa};
    // static per group: design team size and whether the line-up can clear it (brain_jobs.h team_heavy)
    std::vector<int> dk(n, 0);
    std::vector<uint8_t> cc(n, 0);
    {
        float dps = 0, hp = 0;
        for (CT t : bodies) { dps += base_dps(t); hp += (float)bc::champion(t).base_stats().base_health; }
        for (int g = 0; g < n; ++g) {
            const float x = info[g].str * 0.7f * info[g].hp / (0.55f * 800.f * 60.f);
            dk[g] = (int)std::ceil(std::sqrt(std::max(0.f, x)));
            cc[g] = info[g].str * 0.5f * info[g].hp / std::max(1.f, dps) <= hp;   // the members die in turn
        }
    }
    // static per team and group: the denial term (mv_denial / mg_denial) and the far-side factor of job_score
    std::vector<float> den0(2 * n, 0.f), smul(2 * n, 1.f);
    for (int tm = 0; tm < 2; ++tm)
        for (int g = 0; g < n; ++g) {
            const GI& x = info[g];
            const int dh = dh_of(tm, g), da = da_of(tm, g);
            if (dh < 900 && da < 900) {
                const float ratio = (float)dh / std::max(1, dh + da);
                const float pp = 1.f / (1.f + std::exp(0.034f + 12.14f * (ratio - 0.5f)));
                float d = x.gems * pp * (1.f - pp);
                if (x.gems <= 0 && x.str <= 0 && x.gold > 0) d += GEMS_PER_GOLD * x.gold * pp * (1.f - pp);
                den0[tm * n + g] = d;
            }
            if (da * 10 < dh * 7) smul[tm * n + g] = 0.8f;
        }
    // static per group: the banking detour of job_score; per body slot and group: the farm multiplier on its dps
    std::vector<float> bank0(n, 0.f), emb(B * n, 1.f);
    for (int g = 0; g < n; ++g) {
        if (info[g].gems > 0) bank0[g] = 0.35f * info[g].dl * std::min(1.f, info[g].gems / 60.f);
        for (int k = 0; k < B; ++k) {
            float e = 1.f;
            const auto& c = champ(bodies[k]);
            if (c.splash && nm[g] >= 2) e *= 1.f + 0.6f * std::min(3, nm[g] - 1);
            if (c.aoe_farm && nm[g] >= 2) e *= 1.3f;
            if (c.kill_reset && nm[g] >= 2) e *= 1.15f;
            emb[k * n + g] = e;
        }
    }
    // groups within LONE_ALLY_RADIUS of each group (the lone-support test), and group centres in 8x8 cells (vision)
    std::vector<int> nl0(n + 1, 0), nl;
    for (int g = 0; g < n; ++g) {
        for (int h = 0; h < n; ++h) if (cheb(ctr[g], ctr[h]) <= LONE_ALLY_RADIUS) nl.push_back(h);
        nl0[g + 1] = (int)nl.size();
    }
    const int CW = (W + 7) / 8, CH = (H + 7) / 8;
    std::vector<int> c0(CW * CH + 1, 0), cg(n);
    for (int g = 0; g < n; ++g) ++c0[(ctr[g].y / 8) * CW + ctr[g].x / 8 + 1];
    for (int c = 0; c < CW * CH; ++c) c0[c + 1] += c0[c];
    {
        std::vector<int> fill(c0.begin(), c0.end() - 1);
        for (int g = 0; g < n; ++g) cg[fill[(ctr[g].y / 8) * CW + ctr[g].x / 8]++] = g;
    }
    bool th[2] = {false, false};
    {
        int altars = 0;
        for (auto a : bc::map::altars().positions()) { (void)a; ++altars; }
        for (int tm = 0; tm < 2; ++tm) {
            float all = 0, heavy = 0; int n3 = 0, nall = 0;
            for (int g = 0; g < n; ++g) {
                const GI& x = info[g];
                if (x.mono || x.gems <= 0 || dh_of(tm, g) >= 900) continue;
                if (dh_of(tm, g) * 10 >= da_of(tm, g) * 9) continue;
                const float rate = x.gems / (float)std::max(1, resp[g]);
                all += rate; ++nall;
                if (dk[g] >= 3) ++n3;
                if (dk[g] < TEAM_HEAVY_BODIES || !cc[g]) continue;
                heavy += rate;
            }
            const bool party_only = nall > 0 && n3 == nall;
            if (altars < TEAM_HEAVY_MIN_ALTARS && !party_only) continue;
            th[tm] = (all > 0 && heavy >= TEAM_HEAVY_SHARE * all) || (nall > 0 && n3 == nall && nall <= 3);
        }
    }
    int heavy_g[2] = {-1, -1};
    const bool mfteam = (b[2] & MF_TEAM) && b[18] >= TEAM_CAMP_MIN_PARTY && B >= TEAM_CAMP_MIN_BODIES;
    int team_g[2] = {-1, -1}, team_prev[2] = {-1, -1};
    auto dps_at = [&](CT t, int r) { return std::max(10.f, base_dps(t) * (1.f + std::min(1.5f, r / 600.f))); };
    auto jungle = [&](const PSB& s) { return s.spent >= 300.f; };
    auto team_needed = [&](const PSB& s, int g, float hp_left, float dps, int r) {
        if (info[g].str <= 0) return 1;
        const float hpme = std::max(s.mhp * 0.8f, s.hp);
        float inc = info[g].str;
        if (jungle(s)) inc *= 0.7f;
        inc = std::max(inc, info[g].str * 0.15f);
        float soak = CAMP_SOAK_SHARE;
        if (jungle(s) && r - view[s.team][g].enemy_at > CAMP_SOAK_QUIET_ROUNDS) soak = std::max(soak, CAMP_SOAK_SHARE_JUNGLE);
        const float x = inc * hp_left / (soak * hpme * std::max(10.f, dps));
        return std::clamp((int)std::ceil(std::sqrt(x)), 1, 6);
    };
    auto claims = [&](const PSB& o) { return o.mode == PS_GO || o.mode == PS_CAP; };
    // the choosing team's helpers per group (count, dps, capturers) and lone support, taken at each choose
    std::vector<int> hcnt(n), hcap(n), sup(n);
    std::vector<float> hdps(n);
    float tot = 0;
    auto agg = [&](int tm, int r) {
        std::fill(hcnt.begin(), hcnt.end(), 0); std::fill(hcap.begin(), hcap.end(), 0);
        std::fill(sup.begin(), sup.end(), 0); std::fill(hdps.begin(), hdps.end(), 0.f);
        tot = 0;
        for (int k = tm * B; k < tm * B + B; ++k) {
            const PSB& o = sb[k];
            tot += dps_at(o.t, r);
            if (o.job < 0 || !claims(o)) continue;
            ++hcnt[o.job]; hdps[o.job] += dps_at(o.t, r);
            if (o.mode == PS_CAP) ++hcap[o.job];
            for (int q = nl0[o.job]; q < nl0[o.job + 1]; ++q) ++sup[nl[q]];
        }
    };
    // job_score (brain_job_score.h) for body k on group g at round r. bound: an upper bound without the two parts that
    // only lower it (the recruits' real arrival delay, the lone penalty), all else exact
    auto score = [&](int k, int g, int r, bool bound, int* kmin_out) -> float {
        PSB& s = sb[k];
        const GI& x = info[g];
        const int tm = s.team;
        const PSV& v = view[tm][g];
        if (x.mono || fg[g].d.empty() || r < skip[(tm * B + s.slot) * n + g]) return -1;
        const float value = grole_value(s.t, r, (float)x.gems, (float)x.gold, GEMS_PER_GOLD);
        if (value <= 0) return -1;
        const int dist = fg[g].at(s.p);
        if (dist == UNREACH) return -1;
        const int t = std::max(0, dist - s.reach);
        float wait = 0;
        if (v.st == 2) {
            wait = (float)(v.ready - (r + t));
            if (wait > 40) return -1;
            wait = std::max(0.f, wait);
        }
        const float my = dps_at(s.t, r);
        const float hp_left = v.st == 0 ? v.hp : x.hp;
        const bool mine = claims(s) && s.job == g;
        const int helpers = hcnt[g] - (mine ? 1 : 0);
        const float hd = hdps[g] - (mine ? my : 0.f);
        const bool hc = hcap[g] - (mine && s.mode == PS_CAP ? 1 : 0) > 0;
        if (v.st == 1 && hc) return -1;
        if (v.st == 2 && helpers > 0 && wait > 3) return -1;
        const float eff = my * emb[s.slot * n + g];
        float clear = 0, gather = 0, share = 1.f, lone = 1.f;
        int k2 = 1, kmin = 1;
        const int dh = dh_of(tm, g), da = da_of(tm, g);
        if (v.st != 1) {
            kmin = team_needed(s, g, hp_left, eff, r);
            if (!bound && r < LONE_UNTIL_ROUND && dh * 10 >= da * LONE_SIDE_RATIO && x.str > 0 && B >= 2) {
                const bool self_sup = claims(s) && s.job >= 0 && cheb(ctr[s.job], ctr[g]) <= LONE_ALLY_RADIUS;
                if (sup[g] - (self_sup ? 1 : 0) <= 0) lone = LONE_PENALTY;
            }
            k2 = std::max(kmin, helpers + 1);
            if (k2 > B) return -1;
            const int recruits = std::max(0, kmin - helpers - 1);
            clear = hp_left / (eff + hd + recruits * my * 0.9f);
            gather = recruits > 0 ? 18.f + 12.f * recruits : 0.f;
            if (r < 200 && recruits > 0 && v.st == 0 && bound) gather = 2.f;
            else if (r < 200 && recruits > 0 && v.st == 0) {
                int dl[8], av = 0;
                for (int q = tm * B; q < tm * B + B; ++q) {
                    const PSB& o = sb[q];
                    if (&o == &s || cheb(o.p, s.p) > 8 || (o.job >= 0 && claims(o)) || o.mode == PS_RECALL) continue;
                    const int ar = std::max(0, (int)fg[g].at(o.p) - o.reach);
                    if (ar > t + 6 || av >= 8) continue;
                    dl[av++] = std::max(0, ar - t);
                }
                if (av >= recruits) {
                    std::sort(dl, dl + av);
                    gather = std::min(gather, (float)(2 + dl[recruits - 1]));
                }
            }
            share = kmin == 1 ? (helpers == 0 ? 1.f : std::max(0.35f, eff / (eff + hd))) : 1.f / k2;
            float inc = x.str;
            if (jungle(s)) inc *= 0.7f;
            inc = std::max(inc, x.str * 0.15f);
            if (inc * 0.75f * clear / std::max(1, k2) > s.hp * 0.85f) return -1;
        }
        const float cap = (helpers > 0 || k2 > 1) ? 2.f : (float)(capt + 1);
        const float bank = bank0[g];
        float sc = (value + den0[tm * n + g]) * share / (t * JOB_TRAVEL_WEIGHT + wait + gather + clear + cap + bank + 1.f);
        if (kmin > 1 && helpers >= 1 && helpers + 1 <= kmin) sc *= 1.3f;
        sc *= smul[tm * n + g];
        if (x.str > 0) sc *= 0.6f + 0.4f * s.hp / s.mhp;
        sc *= lone;
        if (g == s.job) sc *= 1.3f;
        if (kmin_out) *kmin_out = kmin;
        return sc;
    };
    auto want_bank = [&](const PSB& s) {
        if (s.gems <= 0) return false;
        const int da = fl.at(s.p);
        if (da == UNREACH) return false;
        if (da <= 2 && s.gems >= 10) return true;
        int need = std::min<int>(BANK_MAX_GEMS, BANK_BASE_GEMS + (int)(BANK_GEMS_PER_STEP * da));
        if ((b[2] & MF_CALT) && da <= CONTESTED_BANK_STEPS) need = std::min(need, CONTESTED_BANK_GEMS);
        return s.gems >= need;
    };
    auto want_shop = [&](const PSB& s) {
        if (champ(s.t).remote_shop || s.gold < 60) return false;
        const int hd = home_f[s.team]->at(s.p);
        if (hd == UNREACH) return false;
        const float cost = (hd <= 6 ? hd * 2.f : 6.f + hd) + 2.f;
        return s.gold >= PRESIM_SHOP_BASE + PRESIM_SHOP_TRIP_FACTOR * cost;
    };
    auto go_home = [&](PSB& s, int r) {
        s.job = -1;
        if (home_f[s.team]->at(s.p) <= 7) s.mode = PS_HOME; else { s.mode = PS_RECALL; s.until = r + 5; }
    };
    int dt = 1;   // rounds per tick
    auto step = [&](PSB& s, const Field& f, int stop) {
        for (int i = 0; i < dt; ++i) {
            const int d = f.at(s.p);
            if (d == UNREACH || d <= stop) return;
            bool moved = false;
            for (const auto& dd : DIRS) {
                const vec2 q = s.p + dd;
                if (f.at(q) < d) { s.p = q; moved = true; break; }
            }
            if (!moved) return;
        }
    };
    std::vector<float> cu(n);
    std::vector<int> ci(n);
    auto choose = [&](int k, int r) {
        PSB& s = sb[k];
        const int tm = s.team;
        s.last_plan = r;
        // team-heavy opening: the whole team on one heavy camp of our side
        if (r < TEAM_HEAVY_UNTIL_ROUND && th[tm]) {
            int hg = heavy_g[tm];
            auto ok = [&](int g) {
                const PSV& v = view[tm][g];
                if (info[g].mono || info[g].gems <= 0 || dh_of(tm, g) >= 900 || fg[g].d.empty()) return false;
                if (dh_of(tm, g) * 10 >= da_of(tm, g) * 9 || dk[g] < 3 || !cc[g]) return false;
                if (v.st == 2 && v.ready - r > 20) return false;
                return true;
            };
            if (!(hg >= 0 && view[tm][hg].st != 2 && ok(hg))) {
                hg = -1; float hs = 0;
                const float tdps = B * 55.f;
                for (int g = 0; g < n; ++g) {
                    if (!ok(g) || r - view[tm][g].enemy_at < 15) continue;
                    const PSV& v = view[tm][g];
                    const float wait = v.st == 2 ? (float)std::max(0, v.ready - r) : 0.f;
                    const float val = info[g].gems + 0.5f * info[g].gold;
                    const float sc = val / (std::min(dh_of(tm, g), 200) + (v.st == 0 ? v.hp : info[g].hp) / tdps + wait + 8.f);
                    if (sc > hs) { hs = sc; hg = g; }
                }
                heavy_g[tm] = hg;
            }
            if (hg >= 0) {
                const PSV& v = view[tm][hg];
                s.kmin = std::clamp(team_needed(s, hg, v.st == 0 ? v.hp : info[hg].hp, dps_at(s.t, r), r), 1, std::max(1, std::min(3, B - 1)));
                s.job = hg; s.mode = PS_GO; return;
            }
        }
        // the roaming team on team-camp maps
        if (mfteam && r < TEAM_CAMP_UNTIL_ROUND) {
            int tg = team_g[tm];
            if (!(tg >= 0 && view[tm][tg].st != 2)) {
                if (tg >= 0) team_prev[tm] = tg;
                tg = -1; float ts = 0;
                const float tdps = B * 55.f;
                for (int g = 0; g < n; ++g) {
                    const PSV& v = view[tm][g];
                    if (info[g].mono || v.st == 2 || fg[g].d.empty() || g == team_prev[tm]) continue;
                    if (da_of(tm, g) * 10 < dh_of(tm, g) * 8) continue;
                    const float val = info[g].gems + 0.5f * info[g].gold;
                    if (val <= 0) continue;
                    const float hp = v.st == 0 ? v.hp : 0.f;
                    if (hp / tdps > 60.f) continue;
                    const float hpn = v.st == 0 ? v.hp : info[g].hp, grow = 1.f + r / 600.f;
                    const float xx = info[g].str * hpn / (CAMP_SOAK_SHARE * 950.f * (1.f + r / 800.f) * 55.f * grow);
                    const int need = info[g].str > 0 ? std::clamp((int)std::ceil(std::sqrt(xx)), 1, 9) : 1;
                    if (need > B || need < TEAM_CAMP_MIN_NEED) continue;
                    int from = std::min(dh_of(tm, g), 200);
                    if (team_prev[tm] >= 0) { const int fd = fg[team_prev[tm]].at(ctr[g]); if (fd != UNREACH) from = fd; }
                    const float sc = val / (from + hp / tdps + 8.f);
                    if (sc > ts) { ts = sc; tg = g; }
                }
                team_g[tm] = tg;
            }
            if (tg >= 0) {
                const PSV& v = view[tm][tg];
                s.kmin = std::clamp(team_needed(s, tg, v.st == 0 ? v.hp : 0.f, dps_at(s.t, r), r), 1, std::max(1, std::min(TEAM_CAMP_BODIES, B)));
                s.job = tg; s.mode = PS_GO; return;
            }
        }
        agg(tm, r);
        if (s.open >= 0 && !s.open_done) {
            int km = 1;
            if (r < 80 && view[tm][s.open].st != 2 && score(k, s.open, r, false, &km) > 0) { s.job = s.open; s.kmin = km; s.mode = PS_GO; return; }
            s.open_done = true;
        }
        // a cheap bound per group (real helpers, the fastest clear the team could give, full share), then in falling
        // order: the exact bound (score's bound mode) while the cheap one beats the best, the full score while that does.
        // Same pick as scoring every group.
        const float my = dps_at(s.t, r);
        const float va = 1.f + GEMS_PER_GOLD * tribute_gold(s.t), vb = GEMS_PER_GOLD * champ(s.t).gold_bonus * grole_w(s.t, r);
        const float hpm = 0.6f + 0.4f * s.hp / s.mhp;
        const int sk = (tm * B + s.slot) * n;
        const float* em = &emb[s.slot * n];
        const bool mc = claims(s);
        int nt = 0;
        for (int g = 0; g < n; ++g) {
            const GI& x = info[g];
            if (x.mono || fg[g].d.empty() || r < skip[sk + g]) continue;
            const float value = x.gems * va + x.gold * vb;
            if (value <= 0) continue;
            const int dist = fg[g].at(s.p);
            if (dist == UNREACH) continue;
            const int t = std::max(0, dist - s.reach);
            const PSV& v = view[tm][g];
            float wait = 0;
            if (v.st == 2) { wait = (float)(v.ready - (r + t)); if (wait > 40) continue; wait = std::max(0.f, wait); }
            const bool mine = mc && s.job == g;
            const int helpers = hcnt[g] - mine;
            const float hd = hdps[g] - (mine ? my : 0.f);
            const float clear_lb = v.st == 1 ? 0.f : (v.st == 0 ? v.hp : x.hp) / (my * em[g] + hd + std::max(0, B - 1 - helpers) * my * 0.9f);
            float fac = smul[tm * n + g] * (x.str > 0 ? hpm : 1.f);
            if (helpers >= 1) fac *= 1.3f;
            if (g == s.job) fac *= 1.3f;
            const float u = (value + den0[tm * n + g]) * fac / (t * JOB_TRAVEL_WEIGHT + wait + clear_lb + bank0[g] + 3.f);
            cu[nt] = u; ci[nt++] = g;
        }
        int best = -1, bk = 1; float bs = 0;
        for (;;) {
            int i = -1; float u = bs;
            for (int j = 0; j < nt; ++j) if (cu[j] > u) { u = cu[j]; i = j; }
            if (i < 0) break;
            cu[i] = -1.f;
            if (score(k, ci[i], r, true, nullptr) <= bs) continue;
            int km = 1;
            const float sc = score(k, ci[i], r, false, &km);
            if (sc > bs) { bs = sc; best = ci[i]; bk = km; }
        }
        if (best < 0) { s.job = -1; s.mode = PS_FREE; return; }
        if (best != s.job) s.gather_since = -1;
        s.job = best; s.kmin = bk; s.mode = PS_GO;
    };
    // vision: a body within 7 of a group's centre sees its state; an enemy body within 7 of it marks enemy_at
    std::vector<int> seen[2] = {std::vector<int>(n, -100), std::vector<int>(n, -100)};
    auto look = [&](int r, int bucket) {
        for (const auto& s : sb) {
            const int cx = s.p.x / 8, cy = s.p.y / 8;
            for (int yy = std::max(0, cy - 1); yy <= std::min(CH - 1, cy + 1); ++yy)
                for (int xx = std::max(0, cx - 1); xx <= std::min(CW - 1, cx + 1); ++xx)
                    for (int q = c0[yy * CW + xx]; q < c0[yy * CW + xx + 1]; ++q) {
                        const int g = cg[q];
                        const int c = cheb(ctr[g], s.p);
                        if (c > 7) continue;
                        seen[s.team][g] = r;
                        PSV& v = view[s.team][g];
                        v.st = gs[g].st; v.hp = gs[g].hp; v.ready = gs[g].ready;
                        if (c <= 4) pres[s.team][g] |= 1u << bucket;
                    }
        }
        for (int g = 0; g < n; ++g)
            if (seen[0][g] == r && seen[1][g] == r) view[0][g].enemy_at = view[1][g].enemy_at = r;
    };

    static const int RP[4] = {6, 9, 12, 18}, VS[4] = {2, 2, 1, 1}, DT[4] = {1, 1, 2, 3};
    int lvl = 0;
    const long ff0 = (long)bc::fuel_remaining();
    long f30 = ff0, fq = 0;
    std::vector<int> work[2] = {std::vector<int>(n, 0), std::vector<int>(n, 0)};
    std::vector<float> dmg(n, 0.f);
    std::vector<int> cnt(n, 0);

    // bank engine: the enemy's (the mirror team's) body-rounds within 7 of each altar over the sim. Its fuel (fi) stays out of
    // the level and floor checks, so the sim plays as without it
    long fi = 0;
    vec2 be_alt[8]; int be_na = 0; long be_p[8] = {0};
    for (auto a : bc::map::altars().positions()) if (be_na < 8) be_alt[be_na++] = a;
    int next = 30, last = 0, tick = 0;
    for (int r = 0; r < PRESIM_ROUNDS; r += dt, ++tick) {
        fq = (long)bc::fuel_remaining() + fi;
        if (fq < PRESIM_FUEL_FLOOR) break;
        if (r >= next) {
            // the last rounds' cost, projected to PRESIM_ROUNDS: coarser until it fits (each level ~0.7 of the cost)
            float proj = (f30 - fq) / (float)(r - last) * (PRESIM_ROUNDS - r);
            while (lvl < 3 && proj > fq - PRESIM_FUEL_FLOOR) { ++lvl; proj *= 0.7f; }
            f30 = fq; last = r; next = r + 30; dt = DT[lvl];
        }
        const int bucket = r / 10;
        for (int g = 0; g < n; ++g) if (gs[g].st == 2 && r >= gs[g].ready) { gs[g].st = 0; gs[g].hp = info[g].hp; }
        if (tick % VS[lvl] == 0) look(r, bucket);
        fq = (long)bc::fuel_remaining();
        // decisions and moves
        for (int k = 0; k < (int)sb.size(); ++k) {
            PSB& s = sb[k];
            const int tb = s.team * B;
            if (s.mode == PS_RECALL) {
                if (r >= s.until) { const auto& hv = s.team == 0 ? home0 : home1; s.p = hv[s.slot % hv.size()]; s.mode = PS_HOME; }
                continue;
            }
            if (s.mode == PS_HOME) {
                if (home_f[s.team]->at(s.p) <= 0) { s.hp = s.mhp; s.spent += s.gold; s.gold = 0; s.mode = PS_FREE; }
                else { step(s, *home_f[s.team], 0); continue; }
            }
            if (s.mode == PS_TRIB) {
                if (r >= s.until) { s.gold += s.gems * tribute_gold(s.t); s.gems = 0; s.mode = PS_FREE; }
                continue;
            }
            if (s.mode == PS_CAP) continue;   // resolved below
            const bool fighting = s.mode == PS_GO && s.job >= 0 && gs[s.job].st == 0 && fg[s.job].at(s.p) <= s.reach &&
                                  gs[s.job].hp < info[s.job].hp;
            // banking, shopping, healing (brain_turn.h order)
            if (s.mode == PS_BANK || want_bank(s)) {
                const bool finish = fighting && gs[s.job].hp < 300 && s.gems < 150;
                if (!finish) {
                    s.job = -1; s.mode = PS_BANK;
                    if (fl.at(s.p) <= 0) { s.mode = PS_TRIB; s.until = r + std::max(1, (s.gems + 9) / 10); }
                    else step(s, fl, 0);
                    continue;
                }
            }
            if (s.hp < 0.35f * s.mhp || (want_shop(s) && !(fighting && gs[s.job].hp < info[s.job].hp * 0.5f))) { go_home(s, r); continue; }
            const bool bad = s.job < 0 || (view[s.team][s.job].st == 2 && view[s.team][s.job].ready - r > 30);
            if (s.mode == PS_FREE || bad || (r - s.last_plan >= RP[lvl] && !fighting)) choose(k, r);
            if (s.mode != PS_GO || s.job < 0) continue;
            const int g = s.job;
            const int d = fg[g].at(s.p);
            if (gs[g].st == 1 || (view[s.team][g].st == 1)) {
                // a reward: step on and capture it unless an ally of ours is on it
                bool ally_on = false;
                for (int q = tb; q < tb + B; ++q) if (q != k && sb[q].mode == PS_CAP && sb[q].job == g) ally_on = true;
                if (gs[g].st != 1 || ally_on) { if (gs[g].st != 1) { view[s.team][g].st = gs[g].st; view[s.team][g].ready = gs[g].ready; } s.mode = PS_FREE; s.job = -1; continue; }
                if (d > 0) { step(s, fg[g], 0); continue; }
                s.mode = PS_CAP; s.until = r + capt + 1;
                continue;
            }
            if (gs[g].st == 2) {
                if (d > s.reach + 1) step(s, fg[g], s.reach + 1);
                continue;
            }
            if (d > s.reach) {
                // team camp: wait out of reach until enough of us are here
                if (s.kmin > 1 && d <= s.reach + 3 && gs[g].hp >= info[g].hp) {
                    int here = 1;
                    for (int q = tb; q < tb + B; ++q) if (q != k && sb[q].mode != PS_RECALL && fg[g].at(sb[q].p) <= 6) ++here;
                    if (here < s.kmin) {
                        if (s.gather_since < 0) s.gather_since = r;
                        if (r - s.gather_since > 14) {
                            int coming = 0;
                            for (int q = tb; q < tb + B; ++q) if (q != k && sb[q].job == g && claims(sb[q])) ++coming;
                            if (!(coming + 1 >= s.kmin && coming + 1 > here && r - s.gather_since <= 40)) {
                                skip[(s.team * B + s.slot) * n + g] = r + 60; s.gather_since = -1; s.mode = PS_FREE; s.job = -1;
                            }
                        }
                        continue;
                    }
                }
                step(s, fg[g], s.reach);
            }
        }
        fq = (long)bc::fuel_remaining();
        // camp fights, captures and contests
        std::fill(dmg.begin(), dmg.end(), 0.f);
        std::fill(cnt.begin(), cnt.end(), 0);
        for (int tm = 0; tm < 2; ++tm) std::fill(work[tm].begin(), work[tm].end(), 0);
        for (const auto& s : sb) {
            if (s.job < 0 || (s.mode != PS_GO && s.mode != PS_CAP)) continue;
            const int d = fg[s.job].at(s.p);
            if (d <= 4) work[s.team][s.job] |= 1;
            if (s.mode == PS_GO && gs[s.job].st == 0 && d <= s.reach) { dmg[s.job] += dps_at(s.t, r) * dt; ++cnt[s.job]; }
        }
        for (int g = 0; g < n; ++g) {
            if (work[0][g] && work[1][g]) {
                // both teams work it: we meet. The side that came later (fewer bodies) gives it up for a while.
                mask[g] |= 1u << bucket;
                int c[2] = {0, 0};
                for (const auto& s : sb) if (s.job == g && (s.mode == PS_GO || s.mode == PS_CAP) && fg[g].at(s.p) <= 4) ++c[s.team];
                const int loser = c[0] < c[1] ? 0 : c[1] < c[0] ? 1 : (r % 2);
                for (auto& s : sb)
                    if (s.team == loser && s.job == g) { skip[(s.team * B + s.slot) * n + g] = r + 30; s.job = -1; s.mode = PS_FREE; s.gather_since = -1; }
            }
            if (cnt[g] > 0 && gs[g].st == 0) {
                const float inc0 = info[g].str * (0.5f + 0.5f * gs[g].hp / std::max(1.f, info[g].hp));
                gs[g].hp -= dmg[g];
                for (auto& s : sb) {
                    if (s.job != g || s.mode != PS_GO || fg[g].at(s.p) > s.reach) continue;
                    const float each = inc0 * dt * (jungle(s) ? 0.7f : 1.f) / cnt[g];
                    s.hp -= each;
                    if (s.hp < std::max(0.3f * s.mhp, 3.f * each)) go_home(s, r);   // LOWHP: out, home to heal
                }
                if (gs[g].hp <= 0) {
                    gs[g].hp = 0; gs[g].st = 1;
                    for (auto& s : sb) if (s.job == g && fg[g].at(s.p) <= 6) { view[s.team][g].st = 1; }
                }
            }
        }
        // captures: the reward goes to the body on the tiles when its count ends, unless an enemy stands within 4
        for (auto& s : sb) {
            if (s.mode != PS_CAP) continue;
            const int g = s.job;
            if (gs[g].st != 1) { s.mode = PS_FREE; s.job = -1; continue; }
            if (r < s.until) continue;
            if (work[1 - s.team][g]) { s.until = r + capt + 1; continue; }
            s.gems += info[g].gems; s.gold += info[g].gold * champ(s.t).gold_bonus;
            gs[g].st = 2; gs[g].ready = r + resp[g];
            for (int tm = 0; tm < 2; ++tm)
                for (int q = tm * B; q < tm * B + B; ++q) if (cheb(sb[q].p, ctr[g]) <= 7) { view[tm][g].st = 2; view[tm][g].ready = gs[g].ready; break; }
            s.mode = PS_FREE; s.job = -1;
        }

        {
            const long i0 = (long)bc::fuel_remaining();
            for (int q = B; q < 2 * B; ++q)
                for (int a = 0; a < be_na; ++a) if (cheb(be_alt[a], sb[q].p) <= 7) be_p[a] += dt;
            fi += i0 - (long)bc::fuel_remaining();
        }
    }
    for (int g = 0; g < n && g < PLAN_GROUPS; ++g) {
        for (int x = 0; x < 4; ++x) b[PRE_BASE + 4 * g + x] = (uint8_t)(mask[g] >> (8 * x));

    }
    b[19] = 1;
    for (int a = 0; a < be_na; ++a) {
        b[DRAFT_ALTAR_BYTES + 3 * a] = (uint8_t)be_alt[a].x; b[DRAFT_ALTAR_BYTES + 3 * a + 1] = (uint8_t)be_alt[a].y;
        b[DRAFT_ALTAR_BYTES + 3 * a + 2] = (uint8_t)std::min<long>(255, be_p[a] / 2);   // per round over 200 rounds, x100
    }
    b[DRAFT_ALTAR_BYTES + 24] = (uint8_t)be_na; b[DRAFT_ALTAR_BYTES + 25] = 1;
    // after the race, so its fuel levels and masks are as without it. The race ends at 1.3-1.6M on mid-size maps
    // and never under PRESIM_FUEL_FLOOR; the table costs <= ~300k at 50+ groups, so 900k keeps >= 600k for the pick
    if ((long)bc::fuel_remaining() > 900000L) geo_table(b, info, groups, n, fg);
}
