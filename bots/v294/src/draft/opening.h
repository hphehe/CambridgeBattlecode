// The team opening, draft side. The draft is the one moment the team can plan together: the map and the line-up are
// known and every body reads the same bytes. For each way of dividing the bodies into squads, each squad gets the camp
// order that earns most by OP_HORIZON.
// A reward is paid only to allies on the camp's tiles when the capture finishes, so one member stays to collect while the
// others walk on to the next camp. The collector is the member whose passive gains most from that reward (the rule of
// cap_class) and the value is grole_value, the price the job engine uses.
// Draft bytes, one running index over two free blocks (OP_A, OP_B): [0] squads (0 = no plan), then per squad
// [+0, +1] member mask by champion type, [+2] stop count, [+3...] stops as group id + 1.
#pragma once

static constexpr int OP_A = 260, OP_A_LEN = 40, OP_B = 540, OP_B_LEN = 20;
static constexpr int OP_MAX_SQUADS = 6, OP_MAX_STOPS = 24;
static constexpr int OP_MOST_SQUADS = 3;                // more squads than this is the solo farming the job engine does anyway
static constexpr int OP_WIDTH = 4, OP_TRY = 4;          // states kept per stop; camps tried from each state
static constexpr int OP_NEAR = 12;                      // camps worth looking at from any one place
static constexpr int OP_MAX_MONSTERS = 16;              // of one camp, in killing order (those that do not hit are one last entry)
static constexpr int OP_HORIZON = 200;                  // the plan covers the rounds where the early gold is decided
static constexpr int OP_FIRST_ROUND = 2;                // the first round a squad is under way
static constexpr int OP_MAX_WAIT = 25;                  // never plan to stand this long at a camp that is not back yet
static constexpr float OP_HEALTH_USE = 1.f - RETREAT_HEALTH;   // a body leaves a fight at the retreat line: this share of its health is what it can spend
static constexpr float OP_RATE_LAG = 15.f;              // value / (round + lag) orders the search: early gold first
static constexpr long OP_FUEL_NEEDED = 3000000L;        // the search (2.35M at most on 26 ladder maps) plus what the pick needs
static constexpr int OP_GATHER_NEAR = 6;                // within this of a live monster counts as arrived
static constexpr int OP_GATHER_WAIT = 8;                // the squad walks together: whoever is not here by now is not close

struct OpMap {   // the camps a plan may use
    int m = 0;
    bool open_first = false;   // no camp pays before the enemy could walk there: a first stop on our own side is still ours, the enemy has its own side to take first
    int id[PLAN_GROUPS], gems[PLAN_GROUPS], gold[PLAN_GROUPS], resp[PLAN_GROUPS];
    int da[PLAN_GROUPS];   // the enemy's walk to the camp: from that round on it is not ours to plan
    float hp[PLAN_GROUPS];
    int nmon[PLAN_GROUPS];
    float mhp[PLAN_GROUPS][OP_MAX_MONSTERS], mstr[PLAN_GROUPS][OP_MAX_MONSTERS];   // killing order: most damage per health first
    uint8_t d[PLAN_GROUPS + 1][PLAN_GROUPS];      // steps between two camps' nearest tiles (255 = out of reach); row m = from our spawn
};
struct OpBody { CT t; float dps, hp; };
struct OpNode {   // one search state: a squad's route so far, chained through parent
    float value, rate;
    float t, tp;          // round the squad is free to walk on; round the last collector is free
    float dmg;            // damage taken since the last recall
    short ce, parent;     // round this capture pays; previous state
    int8_t camp, taker;   // this stop; which member collects it
};
struct OpRoute { int n = 0; float value = 0; uint8_t camp[OP_MAX_STOPS], taker[OP_MAX_STOPS]; short ce[OP_MAX_STOPS]; };
struct OpBest { float value = -1.f; int ns = 0; int sizes[OP_MAX_SQUADS]; OpRoute r[OP_MAX_SQUADS]; };

// the member whose passive gains most from this reward; with no passive for it, the one whose damage is missed least
static int op_taker(const OpBody* b, int k, int gems, int gold) {
    int best = 0;
    float top = -1.f;
    for (int i = 0; i < k; ++i) {
        const float v = tribute_gold(b[i].t) * gems + (champ(b[i].t).gold_bonus - 1.f) * gold;
        if (v > top || (v == top && b[i].dps < b[best].dps)) { top = v; best = i; }
    }
    return best;
}

// who stands nearest the monsters: melee first, then the most health
static void op_front(const OpBody* b, int k, int* front) {
    for (int i = 0; i < k; ++i) {
        int q = i;
        const auto ahead = [&](int x, int y) { return is_melee(b[x].t) != is_melee(b[y].t) ? is_melee(b[x].t) : b[x].hp > b[y].hp; };
        while (q > 0 && ahead(i, front[q - 1])) { front[q] = front[q - 1]; --q; }
        front[q] = i;
    }
}

// a monster joins a camp's killing order: most damage per health first; those that do not hit are one last entry (op_seal)
static void op_add_monster(float* mh, float* ms, int& nh, float& harmless, float h, float s) {
    if (s <= 0) { harmless += h; return; }
    if (nh == OP_MAX_MONSTERS - 1) { mh[nh - 1] += h; ms[nh - 1] += s; return; }
    int q = nh++;
    while (q > 0 && s * mh[q - 1] > ms[q - 1] * h) { mh[q] = mh[q - 1]; ms[q] = ms[q - 1]; --q; }
    mh[q] = h; ms[q] = s;
}
static void op_seal(float* mh, float* ms, int& nh, float harmless) { if (harmless > 0) { mh[nh] = harmless; ms[nh] = 0; ++nh; } }

// One camp against one squad, by the rules: each monster hits the nearest champion, so the damage lands on the front body until
// it has to leave, and its damage leaves with it. spend, dps_of: per body, front body first. extra: damage a round from outside
// the camp (enemy champions at it). False: the squad runs out before the camp does.
static bool op_fight(int nmon, const float* mhp, const float* mstr, const float* spend, const float* dps_of, int k, float extra, float& clear, float& hit) {
    float cap[OP_MAX_SQUADS], dps = 0, s = extra;
    for (int i = 0; i < k; ++i) { cap[i] = spend[i]; dps += dps_of[i]; }
    for (int i = 0; i < nmon; ++i) s += mstr[i];
    int f = 0, h = 0;
    float rem = mhp[0];
    clear = hit = 0;
    for (int t = 1; t <= OP_HORIZON; ++t) {
        hit += s;
        cap[f] -= s;
        float d = dps;
        const bool leaves = cap[f] <= 0;
        if (leaves) { dps -= dps_of[f]; ++f; }
        while (d >= rem) {
            d -= rem; s -= mstr[h];
            if (++h == nmon) { clear = (float)t; return true; }
            rem = mhp[h];
        }
        rem -= d;
        if (f == k) return false;
    }
    return false;
}

// the round a camp is clear: the squad works from start, the last collector (late_dps of all_dps) joins at late
static float op_clear(float hp, float start, float late, float all_dps, float late_dps) {
    if (late > start) {
        const float rest = std::max(all_dps / OP_MAX_SQUADS, all_dps - late_dps);
        const float done = (late - start) * rest;
        if (done >= hp) return start + std::max(1.f, hp / rest);
        hp -= done;
        start = late;
    }
    return start + std::max(1.f, hp / all_dps);
}

// One squad's best camp order: a beam search over stops. busy[c] = the round camp c is next alive after earlier squads.
__attribute__((noinline))
static void op_route(const OpMap& M, const OpBody* b, int k, const short* busy, OpRoute& out) {
    const bool relay = k >= 2;
    const int pay = (int)bc::constants::capture_time + 1, recall = (int)bc::constants::recall_time + 1;
    float pool = 0;
    for (int i = 0; i < k; ++i) pool += b[i].hp;
    pool *= OP_HEALTH_USE;
    int front[OP_MAX_SQUADS];
    op_front(b, k, front);
    float spend[OP_MAX_SQUADS], dps_of[OP_MAX_SQUADS];
    for (int i = 0; i < k; ++i) { spend[i] = b[front[i]].hp * OP_HEALTH_USE; dps_of[i] = b[front[i]].dps; }
    float val[PLAN_GROUPS], clr[PLAN_GROUPS], dmg_of[PLAN_GROUPS];
    int8_t tk[PLAN_GROUPS];
    for (int c = 0; c < M.m; ++c) {
        if (!op_fight(M.nmon[c], M.mhp[c], M.mstr[c], spend, dps_of, k, 0.f, clr[c], dmg_of[c])) { clr[c] = 0; continue; }   // not this squad's camp
        tk[c] = (int8_t)op_taker(b, k, M.gems[c], M.gold[c]);
        val[c] = grole_value(b[tk[c]].t, 0, (float)M.gems[c], (float)M.gold[c], GEMS_PER_GOLD);
    }
    // this squad's camps worth a full look from one place: what each pays for the rounds it takes this squad. Built on first use.
    uint8_t near[PLAN_GROUPS + 1][OP_NEAR];
    int8_t nnear[PLAN_GROUPS + 1];
    for (int j = 0; j <= M.m; ++j) nnear[j] = -1;
    const auto near_of = [&](int j) {
        if (nnear[j] >= 0) return (int)nnear[j];
        float top[OP_NEAR];
        int cnt = 0;
        for (int c = 0; c < M.m; ++c) {
            if (clr[c] <= 0 || M.d[j][c] >= 255) continue;
            const float r = val[c] / (std::max(0, M.d[j][c] - 1) + clr[c] + pay);
            int q = cnt;
            while (q > 0 && r > top[q - 1]) --q;
            if (q >= OP_NEAR) continue;
            for (int z = std::min(cnt, OP_NEAR - 1); z > q; --z) { near[j][z] = near[j][z - 1]; top[z] = top[z - 1]; }
            near[j][q] = (uint8_t)c; top[q] = r;
            if (cnt < OP_NEAR) ++cnt;
        }
        return (int)(nnear[j] = (int8_t)cnt);
    };
    OpNode A[OP_MAX_STOPS * OP_WIDTH + 1];
    A[0] = OpNode{0.f, 0.f, (float)OP_FIRST_ROUND, (float)OP_FIRST_ROUND, 0.f, 0, -1, -1, -1};
    int lo = 0, hi = 1, best = 0;
    short own[PLAN_GROUPS];   // the round each camp is next alive: busy, plus the state's own captures while it is expanded
    for (int c = 0; c < M.m; ++c) own[c] = busy[c];
    for (int depth = 0; depth < OP_MAX_STOPS; ++depth) {
        OpNode next[OP_WIDTH];
        int nn = 0;
        for (int p = lo; p < hi; ++p) {
            const OpNode s = A[p];
            for (int q = p; q > 0; q = A[q].parent) own[A[q].camp] = std::max<short>(own[A[q].camp], A[q].ce + M.resp[A[q].camp]);
            const int from = s.camp < 0 ? M.m : s.camp;
            // the camps worth a full look, by a quick rate
            int pick[OP_TRY];
            float ps[OP_TRY];
            int np = 0;
            const int nl = near_of(from);
            for (int j = 0; j < nl; ++j) {
                const int c = near[from][j];
                const float r = val[c] / (std::max(0, M.d[from][c] - 1) + clr[c] + pay + std::max(0.f, own[c] - s.t));
                int q = np;
                while (q > 0 && r > ps[q - 1]) --q;
                if (q >= OP_TRY) continue;
                for (int z = std::min(np, OP_TRY - 1); z > q; --z) { pick[z] = pick[z - 1]; ps[z] = ps[z - 1]; }
                pick[q] = c; ps[q] = r;
                if (np < OP_TRY) ++np;
            }
            for (int i = 0; i < np; ++i) {
                const int c = pick[i];
                const float alive = own[c];
                const bool lag = relay && s.taker >= 0;
                int w = std::max(0, M.d[from][c] - 1);
                float t = s.t, tp = s.tp, dmg = s.dmg;
                float start = std::max(t + w, alive);
                const float eff = M.hp[c] / clr[c];   // the squad's damage over this fight, bodies leaving included
                float tc = op_clear(M.hp[c], start, lag ? std::max(tp + w, alive) : start, eff, lag ? b[s.taker].dps : 0.f);
                float hit = dmg_of[c] * (tc - start) / clr[c];
                if (hit > pool) continue;
                if (dmg + hit > pool) {   // too hurt for this camp: the squad recalls once the last reward is paid, and walks out
                    t = tp = std::max(t, tp) + recall;
                    dmg = 0;
                    w = std::max(0, M.d[M.m][c] - 1);
                    start = std::max(t + w, alive);
                    tc = start + clr[c];
                    hit = dmg_of[c];
                }
                if (alive - (t + w) > OP_MAX_WAIT) continue;
                const float ce = (relay && tk[c] == s.taker ? std::max(tc, tp + w) : tc) + pay;
                if (ce > OP_HORIZON) continue;
                if (ce > M.da[c] && !(M.open_first && s.camp < 0 && M.da[c] - M.d[M.m][c] >= pay)) continue;   // the plan is the part of the game nobody can contest: a stop the enemy can reach before it pays is the job engine's
                const float value = s.value + val[c];
                const OpNode x{value, value / (ce + OP_RATE_LAG), relay ? tc : ce, ce, dmg + hit, (short)ce, (short)p, (int8_t)c, tk[c]};
                int q = nn;
                while (q > 0 && x.rate > next[q - 1].rate) --q;
                if (q >= OP_WIDTH) continue;
                for (int z = std::min(nn, OP_WIDTH - 1); z > q; --z) next[z] = next[z - 1];
                next[q] = x;
                if (nn < OP_WIDTH) ++nn;
            }
            for (int q = p; q > 0; q = A[q].parent) own[A[q].camp] = busy[A[q].camp];
        }
        if (!nn) break;
        lo = hi;
        for (int i = 0; i < nn; ++i) { A[hi] = next[i]; if (next[i].value > A[best].value) best = hi; ++hi; }
    }
    out.value = A[best].value;
    out.n = 0;
    for (int q = best; q > 0; q = A[q].parent) ++out.n;
    for (int q = best, i = out.n - 1; q > 0; q = A[q].parent, --i) { out.camp[i] = (uint8_t)A[q].camp; out.taker[i] = (uint8_t)A[q].taker; out.ce[i] = A[q].ce; }
}

// One division of the bodies: squads planned in turn, each leaving the camps the earlier ones took busy until they are back.
__attribute__((noinline))
static void op_try_split(const OpMap& M, const OpBody* b, const int* sizes, int ns, OpBest& best) {
    short busy[PLAN_GROUPS];
    for (int c = 0; c < M.m; ++c) busy[c] = 0;
    OpRoute r[OP_MAX_SQUADS];
    float v = 0;
    for (int s = 0, off = 0; s < ns; off += sizes[s++]) {
        op_route(M, b + off, sizes[s], busy, r[s]);
        v += r[s].value;
        for (int i = 0; i < r[s].n; ++i) { const int c = r[s].camp[i]; busy[c] = std::max<short>(busy[c], r[s].ce[i] + M.resp[c]); }
    }
    if (v > best.value) {
        best.value = v; best.ns = ns;
        for (int s = 0; s < ns; ++s) { best.sizes[s] = sizes[s]; best.r[s] = r[s]; }
    }
}

// every way of writing the team as squad sizes, largest first
static void op_splits(const OpMap& M, const OpBody* b, int left, int most, int* sizes, int ns, OpBest& best) {
    if (left == 0) { op_try_split(M, b, sizes, ns, best); return; }
    if (ns == OP_MOST_SQUADS) return;
    for (int k = std::min(left, most); k >= 1; --k) { sizes[ns] = k; op_splits(M, b, left - k, k, sizes, ns + 1, best); }
}

// the plan into the free bytes; when it does not fit, the longest list loses its last stops
static void op_write(std::array<uint8_t, 1024>& b, const OpMap& M, const OpBody* order, const OpBest& best) {
    const auto put = [&](int i, int v) { b[i < OP_A_LEN ? OP_A + i : OP_B + i - OP_A_LEN] = (uint8_t)v; };
    int cnt[OP_MAX_SQUADS], total = 0;
    for (int s = 0; s < best.ns; ++s) { cnt[s] = best.r[s].n; total += cnt[s]; }
    for (int room = OP_A_LEN + OP_B_LEN - 1 - 3 * best.ns; total > room; --total) {
        int l = 0;
        for (int s = 1; s < best.ns; ++s) if (cnt[s] > cnt[l]) l = s;
        --cnt[l];
    }
    int i = 0;
    put(i++, best.ns);
    for (int s = 0, off = 0; s < best.ns; off += best.sizes[s++]) {
        int mask = 0;
        for (int k = 0; k < best.sizes[s]; ++k) mask |= 1 << (int)order[off + k].t;
        put(i++, mask & 255); put(i++, mask >> 8); put(i++, cnt[s]);
        for (int k = 0; k < cnt[s]; ++k) put(i++, M.id[best.r[s].camp[k]] + 1);
    }
}

__attribute__((noinline))
static void plan_opening(std::array<uint8_t, 1024>& b, const std::vector<GI>& info, const std::vector<bc::map::camp_group>& groups,
                         int n, const std::vector<CT>& bodies, const std::vector<Field>& fg) {
    const int B = std::min<int>((int)bodies.size(), OP_MAX_SQUADS);
    OpMap M;
    for (int g = 0; g < n; ++g) {
        const GI& x = info[g];
        if (x.mono || !fg[g].ok() || x.dh >= 255 || x.dh > x.da) continue;   // they walk there first: it can never pay before they arrive
        const int c = M.m++;
        M.id[c] = g; M.gems[c] = x.gems; M.gold[c] = x.gold; M.hp[c] = x.hp; M.da[c] = x.da;
        int nh = 0;
        float harmless = 0;
        for (const auto& u : groups[g]) {
            float h, s;
            camp_stats_of(u, kind_of(u), h, s);
            op_add_monster(M.mhp[c], M.mstr[c], nh, harmless, h, s);
        }
        op_seal(M.mhp[c], M.mstr[c], nh, harmless);
        M.nmon[c] = nh;
        M.resp[c] = (int)groups[g].max_respawn_timer();
    }
    if (M.m == 0 || B == 0) return;
    std::vector<std::vector<vec2>> tiles(M.m);
    for (int c = 0; c < M.m; ++c) for (const auto& u : groups[M.id[c]]) tiles[c].push_back(u.spawn_position());
    for (int j = 0; j < M.m; ++j)
        for (int k = 0; k < M.m; ++k) {
            int d = UNREACH;
            for (auto p : tiles[j]) d = std::min<int>(d, fg[M.id[k]].at(p));
            M.d[j][k] = (uint8_t)std::min(d, 255);
        }
    for (int c = 0; c < M.m; ++c) M.d[M.m][c] = (uint8_t)info[M.id[c]].dh;
    // the gold collector leads, then by damage, so the first and largest squad has both
    OpBody order[OP_MAX_SQUADS];
    for (int i = 0; i < B; ++i) order[i] = OpBody{bodies[i], base_dps(bodies[i]), (float)bc::champion(bodies[i]).base_stats().base_health};
    for (int i = 0; i < B; ++i)
        for (int j = i + 1; j < B; ++j) {
            const float gi = champ(order[i].t).gold_bonus, gj = champ(order[j].t).gold_bonus;
            if (gj > gi || (gj == gi && order[j].dps > order[i].dps)) std::swap(order[i], order[j]);
        }
    OpBest best;
    int sizes[OP_MAX_SQUADS];
    op_splits(M, order, B, B, sizes, 0, best);
    int stops = 0;
    for (int s = 0; s < best.ns; ++s) stops += best.r[s].n;
    if (!stops) { M.open_first = true; best = OpBest{}; op_splits(M, order, B, B, sizes, 0, best); }
    op_write(b, M, order, best);
}

// Champion side: my squad's list.
static bool SQ_ON = false;            // walking the list
static int SQ_N = 0, SQ_AT = 0;       // stops on it; the one I am on
static int SQ_MASK = 0;               // my squad, by champion type
static uint8_t SQ_STOP[OP_MAX_STOPS];

static void read_opening() {
    const auto& dm = bc::draft_memory;
    if (dm.size() < (size_t)(OP_B + OP_B_LEN)) return;
    const auto byte = [&](int i) { return (int)std::to_integer<uint8_t>(dm[i < OP_A_LEN ? OP_A + i : OP_B + i - OP_A_LEN]); };
    int i = 0;
    const int ns = byte(i++);
    for (int s = 0; s < ns; ++s) {
        const int mask = byte(i) | (byte(i + 1) << 8), c = byte(i + 2);
        i += 3;
        if ((mask >> (int)ME.type) & 1) {
            SQ_MASK = mask; SQ_N = c; SQ_AT = 0; SQ_ON = c > 0;
            for (int k = 0; k < c; ++k) SQ_STOP[k] = (uint8_t)(byte(i + k) - 1);
        }
        i += c;
    }
}
