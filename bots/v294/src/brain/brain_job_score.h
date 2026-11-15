// What one job is worth to me per round (job_score, at the end). The score also leaves its working on the group for the
// rest of the plan: p_wait, p_clear, p_value, kmin, and CC_HEALF for the camps that need a heal first.
#pragma once
static std::vector<float> SC;                 // this scan's job scores (-1: not scored)
// map vector prior: P(we take this group's gems) from its path ratio, fitted on 5,000 cross-family games (logit -0.034 - 12.14(ratio-0.5))
static inline float mv_prior_raw(const Group& g) {
    if (g.d_home >= 900 || g.d_away >= 900) return 1.f;
    const float ratio = (float)g.d_home / std::max(1, g.d_home + g.d_away);
    return 1.f / (1.f + std::exp(0.034f + 12.14f * (ratio - 0.5f)));
}
// p depends only on the group's d_home / d_away, which change rarely (its field built, the plan
// read), and the exp cost ~200 fuel on every job_score call: cached per group with the two distances it came from
struct MvCache { int h = -1, a = -1; float p = 1.f; };
static std::vector<MvCache> MVC;
static inline float mv_prior(const Group& g) {
    if (g.id < 0) return mv_prior_raw(g);
    if ((int)MVC.size() <= g.id) MVC.resize(std::max(G.size(), (size_t)g.id + 1));
    MvCache& c = MVC[(size_t)g.id];
    if (c.h != g.d_home || c.a != g.d_away) { c.h = g.d_home; c.a = g.d_away; c.p = mv_prior_raw(g); }
    return c.p;
}
// denial in gems - the enemy would take this camp with chance 1-P if we do not, and we take it with
// chance P, so taking it is worth gems x P(1-P) more to the gem difference. Added to the value (gems only, not
// gold), so the job model's P(caught) (jm.h) discounts it and the death cost competes with it directly.
static inline float mv_denial(const Group& g) {
    if (g.gems <= 0) return 0.f;
    const float p = mv_prior(g);
    return g.gems * p * (1.f - p);
}
// gold denial - the enemy is paid this gold with chance 1-P if we leave it, and gold buys the
// items that win fights, so a contested group is worth gw x gold x P(1-P) more (mv_denial counts the gems only)
static inline float mg_denial(const Group& g, float gw) {
    if (g.gold <= 0) return 0.f;
    if (g.gems > 0 || g.str_total > 0) return 0.f;   // gold-only Strength-0 groups only
    const float p = mv_prior(g);
    return gw * g.gold * p * (1.f - p);
}
// the scan keeps its best score so far here while it scores a camp (JM_NO_BAR everywhere else: a full score), and marks
// the camps whose model part that bar let it skip
static constexpr float JM_NO_BAR = -1e30f;
static float JM_BAR = JM_NO_BAR;
static bool jm_has_bar() { return JM_BAR > -1e29f; }
static std::vector<char> JM_PRUNED;
static std::vector<char> JM_EARLY;   // camps the early bound skipped this pass (no p_wait / kmin from it)
// The most jm_score can give a camp before its 20 features: P(caught) >= 0 and P(get) at its largest over the log Balance;
// when an enemy seen in the last 15 rounds is within 7 of the camp (it may be contested) also P(they take) at its largest
// and the largest kill rate. The walk and the team switch are known here.
// jm_score's caps that need no camp features: P(get) uncontested at the walk 0 (its walk weight is < 0) and the team
// switch's best, P(get) contested also at the Balance's best, P(they take) <= 1, the largest kill rate. Off (no bound)
// if a refit ever gives the walk a positive weight in P(get).
struct JmUbC { bool ok; float pgu, pgc, pk; };   // pk = the largest kill rate (x JM_KV_NOW at use)
static const JmUbC& jm_ubc() {
    static const JmUbC c = [] {
        float pk = 0.f;
        for (float v : JM_PK) pk = std::max(pk, v);
        return JmUbC{JM_GET[4] <= 0.f, jm_sig(JM_GET[0] + std::fabs(JM_GET[3])),
                     jm_sig(JM_GET[0] + JM_GET[1] + 3.f * std::fabs(JM_GET[2]) + std::fabs(JM_GET[3])), pk};
    }();
    return c;
}
// the most jm_score's numerator can be for this value and share (before the time): no enemy seen in the last 15 rounds =
// no camp contested
static float jm_ub_const(float value, float share) {
    const JmUbC& c = jm_ubc();
    if (!c.ok) return 1e30f;
    const float u = c.pgu * value * share;
    if (jm_feat_turn(rnd()).n15 == 0) return u;
    return std::max(u, c.pgc * value * share + value * c.pgc + c.pk * JM_KV_NOW);
}
static float jm_score_ub(const Group& g, float value, float share, float den, int walk) {
    const float w20 = walk / 20.f, s16 = tc_get() / 16.f;
    float ub = jm_sig(JM_GET[0] + JM_GET[3] * s16 + JM_GET[4] * w20) * value * share;
    const JmFT& T = jm_feat_turn(rnd());
    bool con = false;
    if (T.n15 > 0) {
        const JmBox bx = jm_box(g);
        for (const JmFE& q : T.e) if (q.r15 && jm_box_d(bx, q.lp) <= 7 && tc_gd(g, q.lp) <= 7) { con = true; break; }
    }
    if (con) {
        const float pg = jm_sig(JM_GET[0] + JM_GET[1] + 3.f * std::fabs(JM_GET[2]) + JM_GET[3] * s16 + JM_GET[4] * w20);
        const float pt = jm_sig(JM_THEY[0] + JM_THEY[1] + 3.f * std::fabs(JM_THEY[2]) + JM_THEY[3] * s16 + JM_THEY[4] * w20);
        float pk = 0.f;
        for (float v : JM_PK) pk = std::max(pk, v);
        ub = std::max(ub, pg * value * share + value * pg * pt + pk * JM_KV_NOW);
    }
    return ub / den;
}
#include "../camps/camp_calc.h"
// Each camp job_score priced in full this scan: its time (the score's denominator) and its walk, for the re-rank
// (brain_jobs.h geo_rerank). Sieges, spawn sieges and the gate are not noted and never re-ranked.
static std::vector<float> GEO_DEN, GEO_TW;
static std::vector<int> GEO_AT;   // the pass that noted it
static int GEO_PASS = 0;
static void geo_note(const Group& g, float den, int t) {
    if (g.id < 0) return;
    if ((int)GEO_AT.size() < (int)G.size()) { GEO_DEN.assign(G.size(), 0.f); GEO_TW.assign(G.size(), 0.f); GEO_AT.assign(G.size(), -1); }
    GEO_DEN[(size_t)g.id] = den; GEO_TW[(size_t)g.id] = (float)t; GEO_AT[(size_t)g.id] = GEO_PASS;
}
// ---- what one job is worth to me per round. A job is one of three things, each priced by its own step below:
// a siege from a distance, a camp fought from our own spawn, or an ordinary camp (the gate is an ordinary camp with its
// own team). -1: not a job for me now.

// Siege from distance 4: only casters take part; time = travel + total health / our spell damage.
static float siege_job_score(Group& g) {
    float mine = siege_dps();
    if (mine < 8.f) return -1;
    // every live caster is expected to join the siege
    float team = mine;
    for (int s2 = 0; s2 < (int)ALLY.size(); ++s2)
        if (s2 != MY_SLOT && ALLY[s2].alive) team += type_siege_dps(ALLY[s2].type) * (1.f + rnd() / 800.f);
    const float hp_left = g.st == ST_ALIVE ? g.hp_left : g.hp_total;
    const float clear = hp_left / team;
    if (clear > 450) return -1;
    const int t = std::max(0, gdist(g, ME.p) - 4);
    float wait = g.st == ST_RESPAWN ? std::max(0.f, (float)(g.ready_at - (rnd() + t))) : 0.f;
    if (wait > SIEGE_PRICE_WAIT) return -1;
    const float value = (g.gems + GEMS_PER_GOLD * g.gold) * (mine / team) * (ANY_GEM_GROUP ? 1.f : 4.f);
    // job model (jm.h): a siege is a job like the others, with its own time terms
    float jf[20];
    jm_job_feats(g, gdist(g, ME.p), jf);
    const float score = jm_score(value, 1.f, t + wait + clear + 8.f, jm_pcaught(jf), ME.gems, false, false, 3.f, tc_get(), jf[0]);
    g.p_wait = wait; g.p_clear = clear; g.p_value = value;
    return score;
}

// my_dps_est's model at an ally's last-read stats
static float camp_dps_live(const Champ& c) {
    if (c.str <= 0.f) return ally_dps_est(c.type);
    const ChampionInfo& k = champ(c.type);
    const float d = k.dps_scales_focus ? base_dps(c.type) * (0.4f + 0.6f * c.focus / champion_design(c.type).f)
                                       : c.str * k.bd_s1 * k.bd_s2 + k.bd_f * c.focus / 8.f;
    return std::max(10.f, d) * (is_hor(c) ? hor_mult(c) : 1.f);
}

// A camp every hitting monster of which we can hit from one of our own spawn tiles (ladder 03homonoliths: a 60,000-health,
// 600-gem Monolith one step from our spawn, nobody started it before round 790 and the enemy took it). Standing on our spawn
// at turn end heals to full (rules 06), so a monster that cannot kill us in one round never sends us home: no trips, no camp
// risk. The time is the walk to the spawn, the wait and the clear by me and every ally that can also hit it from the spawn;
// my part of the value is my share of that damage.
// Only once more gear would no longer finish it sooner (gearing_is_over), and only for
// a camp whose hits over the whole clear exceed a body's health, i.e. one the ordinary plan would need trips home for: the
// spawn's heal is what this pricing adds. A small camp in a ranged champion's reach of the spawn (12 of 86 local maps) cleared
// in a few rounds stays with the ordinary job score: priced as risk-free from round 1 it held Vesper at home (12 maps x 2
// opponents: 13/46, -225 gems a game).
// False: this camp is not priced this way. True: `score` holds its price.
static bool spawn_camp_job_score(Group& g, int r, float& score) {
    // camp_combat.h: fought from the spawn too
    if (!(spawn_siege_near(g) && g.st != ST_REWARD && F_HOME.at(ME.p) != UNREACH && spawn_siege_ok(g, attack_reach(ME.melee), ME.mhp))) return false;
    float team = MYDPS;
    for (int s2 = 0; s2 < (int)ALLY.size(); ++s2)
        if (s2 != MY_SLOT && ALLY[s2].alive && spawn_siege_ok(g, attack_reach(is_melee(ALLY[s2].type)), ALLY[s2].mhp))
            team += camp_dps_live(ALLY[s2]);
    const float hp_left = g.st == ST_ALIVE ? g.hp_left : g.hp_total;
    float hits = 0.f;
    for (int k = 0; k < g.n; ++k) if (g.st != ST_ALIVE || g.m[k].hp > 0) hits += std::max(0.f, g.m[k].str);
    const float clear = hp_left / std::max(10.f, team);
    // a started job stays on: the camp never heals, so the damage already dealt is never lost
    const bool started = g.st == ST_ALIVE && g.hp_left < g.hp_total - 1.f;
    if (!(hits * clear > ME.mhp && (started || gearing_is_over(clear)))) return false;
    const int hd = F_HOME.at(ME.p);
    float wait = 0.f;
    score = -1;
    if (g.st == ST_RESPAWN) { wait = std::max(0.f, (float)(g.ready_at - (r + hd))); if (wait > SIEGE_PRICE_WAIT) return true; }
    const float gw = ANY_GEM_GROUP ? groi::gw_job(r) : 1.0f;
    const float value = grole_value(ME.type, r, (float)g.gems, (float)g.gold, gw);
    if (value <= 0) return true;
    const float share = MYDPS / std::max(1.f, team);
    const float den = hd * JOB_TRAVEL_WEIGHT + wait + clear + capture_rounds() + 1.f;
    score = jm_score(value, share, den, 0.f, ME.gems, false, false, 3.f, tc_get(), 0.f) +
            (mv_denial(g) + mg_denial(g, gw)) * share / den;
    g.p_wait = wait; g.p_clear = clear; g.p_value = value * share; g.kmin = 1;
    return true;
}

// a camp's gems must be walked to an altar: part of that walk counts against the job, in full from a 60-gem bag
ALWAYS_INLINE float bank_detour(const Group& g) { return g.gems > 0 ? 0.35f * g.d_altar * std::min(1.f, g.gems / 60.f) : 0.f; }

// An ordinary camp, or the gate: each step either fills in part of the price or ends the pricing.
struct CampJob {
    Group& g;
    const int r;
    bool gate = false;
    float gw = 1.f, value = 0.f;
    int helpers = 0;
    float helper_dps = 0.f;
    bool helper_captures = false;
    int t = 0;              // my walk
    float wait = 0.f;       // for its respawn after the walk
    float hp_left = 0.f;
    float bank = 0.f;       // banking detour per gem
    float clear = 0.f, gather = 0.f, share = 1.f, cap = 0.f;
    int k = 1, trips = 0;   // the bodies on it, my trips home
    float out = -1.f;       // the price when a bound ended the pricing early

    ALWAYS_INLINE float denial() const { return mv_denial(g) + mg_denial(g, gw); }
    ALWAYS_INLINE float time() const { return t * JOB_TRAVEL_WEIGHT + wait + gather + clear + cap + bank + 1.f; }

    // what it pays, and whether it is open to me at all
    ALWAYS_INLINE bool read_value() {
        gw = ANY_GEM_GROUP ? groi::gw_job(r) : 1.0f;   // the learned gold exchange rate
        value = grole_value(ME.type, r, (float)g.gems, (float)g.gold, gw);
        // opening the gate makes every gem we carry bankable; a carrier stays out (a camp kill destroys its bag)
        gate = gate_on(g);
        // the whole team goes, carriers too: only the nearest is hit and it steps out when low (fight_mono5)
        if (gate) value += gate_team_gems();
        return value > 0;
    }
    ALWAYS_INLINE bool open_to_me() {
        const Help& help = HELP[g.id];
        helper_dps = help.dps; helper_captures = help.cap;
        helpers = help.n;
        // an ally is already capturing it (a taker close to it still goes, unless another taker has it)
        if (g.st == ST_REWARD && helper_captures && !(cap_class(ME.type, g) > cap_top_class(g) && gdist(g, ME.p) <= TAKER_MAX_STEPS)) return false;
        return !taker_coming(g);   // left to a taker ally
    }
    ALWAYS_INLINE bool read_walk_and_wait() {
        t = std::max(0, gdist(g, ME.p) - attack_reach(ME.melee));
        if (t >= 900) return false;
        if (g.st != ST_RESPAWN) return true;
        wait = (float)(g.ready_at - (r + t));
        if (wait > JOB_PRICE_WAIT) return false;
        wait = std::max(0.f, wait);
        if (helpers > 0 && g.kmin <= 1 && wait > 3) return false;   // someone is already waiting for it
        // a harmless camp's respawn is one body's: a body that does not hold it never
        // joins (the test above let a far one come whose walk ends at the respawn), and of two holders the lower slot keeps
        // it (each dropped it for the other, idled, and took it back at the next plan: five bodies spent 24-66% of their
        // turns waiting at two 120-health spawn camps while the rich groups went untaken)
        if (helpers > 0 && g.str_total <= 0) {
            bool lower = false;
            for (int s = 0; s < MY_SLOT && s < (int)ALLY.size(); ++s) if (ALLY[s].alive && claim_group(s) == g.id) lower = true;
            // a joiner keeps the test above (it may still come when its walk ends within 3 rounds of the respawn: a race,
            // not a queue; never joining lost both sides of 03homonoliths and adgato-gemcombat1); only the two-holder loop is cut
            if (JOB == g.id && lower) return false;
        }
        return true;
    }
    // Early bound. The time so far lacks only gather (>= 0), clear (>= the camp's HP over every body we could bring),
    // capture (>= 2) and the bank detour; the share is <= 1. A camp that cannot beat the scan's best skips the rest.
    ALWAYS_INLINE bool early_bound_skips() {
        if (!jm_has_bar()) return false;
        // the clear with every body of ours from now (trips only add): clear = T - t >= that - t
        const float clb = std::max(0.f, cc_clear_lb(hp_left) - t);
        const float den_e = t * JOB_TRAVEL_WEIGHT + wait + clb + 3.f + bank_detour(g);
        const float ub = (jm_ub_const(value, 1.f) + denial()) / den_e;
        if (ub * 1.02f > JM_BAR) return false;
        if (g.id >= 0 && g.id < (int)JM_EARLY.size()) JM_EARLY[g.id] = 1;
        out = ub;
        return true;
    }
    // Gate pricing (a tank in front and two behind it; the rest join as they come)
    ALWAYS_INLINE bool price_gate() {
        float eff = MYDPS;
        if (champ(ME.type).splash && g.n >= 2) eff *= 1.f + 0.6f * std::min(3, g.n - 1);
        if (champ(ME.type).aoe_farm && g.n >= 2) eff *= 1.3f;
        if (champ(ME.type).kill_reset && g.n >= 2) eff *= 1.15f;   // kills refresh the cooldowns
        const int kmin = std::min(team_needed(g, hp_left, eff), 3);
        g.kmin = kmin;
        k = std::max(kmin, helpers + 1);
        if (k > ALIVE_ALLIES) return false;
        const int recruits = std::max(0, kmin - helpers - 1);
        clear = hp_left / (eff + helper_dps + recruits * MYDPS * 0.9f);
        gather = recruits > 0 ? (18.f + 12.f * recruits) : 0.f;
        share = kmin == 1 ? (helpers == 0 ? 1.f : std::max(0.35f, eff / (eff + helper_dps))) : 1.f / k;
        return true;
    }
    ALWAYS_INLINE bool price_clear() {
        // the draft's farming race puts an enemy body here about when we would arrive: two bodies
        int kneed = 1;
        if (g.str_total > 0 && ALIVE_ALLIES >= 2 && pre_enemy(g.id, r + t + (int)wait)) kneed = 2;
        // rounds from now until it is done, trips home included (a camp never heals), for me + the
        // allies that claim it + the bodies worth bringing (cc_job); my share 1/k
        const float fx = t * JOB_TRAVEL_WEIGHT + wait + bank + 1.f;
        bool rec = true;
        if (jm_has_bar()) {
            const float den_r = t * JOB_TRAVEL_WEIGHT + wait + std::max(1.f, cc_clear_lb(hp_left) - t) + 2.f + bank + 1.f;
            const float ubr = (jm_ub_const(value, 0.5f) + denial() * 0.5f) / den_r;
            if (ubr * 1.02f <= JM_BAR) rec = false;
        }
        const CJob cj = cc_job(g, t, fx + (helpers > 0 ? 2.f : (float)capture_rounds()), fx + 2.f, kneed, rec);
        if (cj.T >= 9999 || r + cj.T > (int)bc::constants::max_rounds) return false;   // done after the game ends
        k = std::max(1, cj.k); trips = cj.trips;
        if (g.id >= 0 && g.id < (int)CC_HEALF.size()) CC_HEALF[(size_t)g.id] = cj.heal;
        // the bodies it counts on when the ones there now do not finish it in one go (SIDE skips a camp above 1)
        g.kmin = (cj.recruits > 0 || cj.trips > 0) ? k : 1;
        clear = std::max(1.f, (float)(cj.T - t));
        share = coop_share(g, cj, k, r + t + (int)wait);   // coop.h: what I add (contest priced), at least the average 1/k
        return true;
    }
    // A camp that cannot beat the scan's best so far skips the model part (x 1.02: a seed copy's jitter). The constant caps
    // first, the cheaper bound (one exp, the contest check) only when they do not decide.
    ALWAYS_INLINE bool late_bound_skips() {
        if (!jm_has_bar()) return false;
        const float den = time();
        const float dn = denial() * share / den;
        const float ubc = jm_ub_const(value, share) / den + dn;
        if (ubc * 1.02f <= JM_BAR) { if (g.id >= 0 && g.id < (int)JM_PRUNED.size()) JM_PRUNED[g.id] = 1; out = ubc; return true; }
        const float ub = jm_score_ub(g, value, share, den, gdist(g, ME.p)) + dn;
        if (ub * 1.02f <= JM_BAR) { if (g.id >= 0 && g.id < (int)JM_PRUNED.size()) JM_PRUNED[g.id] = 1; out = ub; return true; }
        return false;
    }
    // job model (jm.h): [P(get) x value x share + the take we deny + the kills we expect - P(caught) x death cost] / time.
    // Contested: an enemy seen within 7 of the camp in the last 15 rounds.
    ALWAYS_INLINE float model_score() {
        float jf[20];
        const int walk_g = gdist(g, ME.p);
        jm_job_feats(g, walk_g, jf);
        // the risks were fitted per job of ~30 rounds; a longer job is that many fitted
        // jobs back to back, and each trip home is a walk out under fire
        const float t_job = (float)t + wait + gather + clear + cap;
        const float xg = gate ? 1.f : std::max(1.f, t_job / JOB_MODEL_LENGTH);
        const float win = (float)std::min(walk_g + 25, 60);
        const float pc = gate ? jm_pcaught(jf) : 1.f - std::pow(1.f - std::min(jm_pcaught(jf), 0.999f), std::max(1.f, t_job / win));
        const float exit_cost = gate ? 0.f : (float)trips * CAMP_EXIT_DEATH_RATE * (ME.gems * JM_CARRY_K + JM_DC0_NOW);
        // JOB_TRAVEL_WEIGHT (in time()): a far job also leaves us far from the next one (the winners' expanse: many near wolves)
        const float den = time();
        const float score = jm_score(value, share, den, pc, ME.gems, true, jf[18] < 3.f, jf[8], tc_get(), jf[0], xg)
                            - exit_cost / den
                            + denial() * share / den;
        if (!gate) geo_note(g, den, t);
        return score;
    }

    ALWAYS_INLINE float price() {
        if (!read_value() || !open_to_me() || !read_walk_and_wait()) return -1;
        hp_left = g.st == ST_ALIVE ? g.hp_left : g.st == ST_REWARD ? 0.f : g.hp_total;
        if (early_bound_skips()) return out;
        bank = bank_detour(g);
        if (g.st != ST_REWARD && !(gate ? price_gate() : price_clear())) return -1;
        cap = (helpers > 0 || k > 1) ? 2.f : (float)capture_rounds();   // no Capture emblem discount in job scores
        g.p_wait = wait; g.p_clear = clear + gather; g.p_value = value * share;
        if (late_bound_skips()) return out;
        return model_score();
    }
};

static float job_score(Group& g) {
    const int r = rnd();
    wm::update(r);   // this round's state prices (gold, carried bag, death, kill)
    if ((int)CC_HEALF.size() < (int)G.size()) CC_HEALF.assign(G.size(), 0);
    if (g.id >= 0 && g.id < (int)CC_HEALF.size()) CC_HEALF[(size_t)g.id] = 0;
    if (reward_reserved(g)) return -1;
    // Cooldowns apply before the early siege returns below. Without this
    // guard a stalled jackpot siege is immediately scored again.
    if (r < g.skip_until) return -1;
    if (siege_camp(g) && g.st != ST_REWARD) return siege_job_score(g);
    float spawn_score = -1;
    if (spawn_camp_job_score(g, r, spawn_score)) return spawn_score;
    CampJob job{g, r};
    return job.price();
}
