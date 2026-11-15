// One turn with an enemy in reach. play() walks the options in order and each try_ step says whether it used the turn:
// a kill, a finish, a flight, a battle, a shot from neutral ground. When none does, think() carries on with the plain turn.
#pragma once

// An enemy is capturing a cleared camp and I can get there in time. One body on any of its tiles sends their timer back to
// the start (rules: Capture), and the timer of a camp in vision is in the SDK: t rounds left on my turn in round r pays in
// round r + t, so t turns of walking arrive in time. It is a job like any other: the reward over the rounds it costs me,
// against what I hold now. The reward at face value: it is a double swing, theirs gone and maybe ours, but they stand on
// it and may keep it. The fight turn runs first, so nobody it told to leave comes here, and none with better to lose goes:
// a squad on its list, a carrier, a hurt body, one that ran from here a moment ago.
static bool try_break_capture() {
    const int r = rnd();
    static int tried_at = -1;
    if (tried_at == r) return false;
    tried_at = r;
    if (SQ_ON || banking || ME.gems >= CARRIER_GEMS || ME.hp < ME.mhp * 0.5f || flees_since(r - FIGHT_MEMORY_ROUNDS) > 0) return false;
    Group* best = nullptr;
    float top = -1.f;
    for (auto& e : ENEMY) {
        if (!e.vis || !e.alive || !inb(e.p)) continue;
        const int gid = group_at[idx(e.p)];
        if (gid < 0 || G[gid].st != ST_REWARD || group_at[idx(ME.p)] == gid) continue;
        Group& g = G[gid];
        int d = 999;   // to the nearest tile nobody stands on: the only way in
        for (int k = 0; k < g.n; ++k) if (!occupied(g.m[k].p)) d = std::min(d, cheb(g.m[k].p, ME.p));
        d = std::max(d, gdist(g, ME.p));
        if (d > (int)bc::constants::capture_time) continue;   // no timer is longer than this
        bc::map::camp_group_capture_state cs{};
        try { cs = (*GROUPS)[gid].capture_state(); } catch (const std::exception&) { continue; }
        if (cs.state != bc::map::camp_group_capture_type::enemy_capturing || d > cs.timer) continue;
        const float gw = ANY_GEM_GROUP ? groi::gw_job(r) : 1.0f;
        const float rate = grole_value(ME.type, r, (float)g.gems, (float)g.gold, gw) / (float)(d + capture_rounds());
        if (rate > top) { top = rate; best = &g; }
    }
    if (!best) return false;
    if (JOB >= 0 && JOB != best->id && (long)bc::fuel_remaining() >= REPLAN_GUARD_FUEL && job_score(G[JOB]) > top) return false;
    ensure_field(*best, 40000);
    set_claim(best->id, false);
    if (!do_capture(*best)) return false;
    if (JOB != best->id) { JOB = best->id; job_since = r; }
    return true;
}

struct FightTurn {
    const Balance& bal;
    Champ* target;
    int campers, dmin;
    bool capturing_now;
    int reach, r;

    float adv = 0.f, fight_at = 0.f, flee_at = 0.f;
    bool in_battle = false, outnumbered = false, very_low = false, force_flee = false, heavy_bag = false;
    Champ* focus = nullptr;
    FCtx fc{};
    // the learned fight/flee decision, computed only when it is asked
    float flx[FL_NF];
    bool flx_ok = false, ml_ok = false, ml_done = false;
    double ml_mf = 0.0, ml_mr = 0.0, ml_d = 0.0;
    // my best and second ready damage spell (one cast per turn), and my landing dash
    float my_sp = 0.f, my_sp2 = 0.f, my_land_now = 0.f, my_land = 0.f;
    // how far this fight reaches
    bool may_chase = false, mc_caster = false, mc_safe = false, joining = false;
    int fight_r = 0;

    // first sight of an enemy with no ally of ours near: call the team now, the duel is decided in ~2 rounds
    void call_team_on_first_sight() {
        bool mate = false, seen = false;
        for (int i = 0; i < (int)ALLY.size(); ++i) {
            const Champ& al = ALLY[i];
            if (i != MY_SLOT && al.vis && al.alive && al.hp >= al.mhp * 0.4f && cheb(al.p, ME.p) <= CALL_NEAR_RADIUS) mate = true;
        }
        for (auto& e : ENEMY) if (e.vis && e.alive && e.hp > 0 && !on_enemy_spawn(e.p) && cheb(e.p, ME.p) <= 8) seen = true;
        // the CALL raises the beacon (b9 had dropped it: ult -65 vs zc3, a free ally near a lone fight kept farming)
        // cb1: only a fight I would stay in (the flee test's bar for the bodies counted); a call of mine whose fight
        // fell under it is withdrawn
        const float call_bar = bal.us_n >= 4 ? FLEE_ADV_FOUR : bal.us_n >= 3 ? FLEE_ADV_THREE : FLEE_ADV_SMALL;
        if (seen && !mate && adv >= call_bar && (beacon_mine_at < 0 || call_beacon)) { raise_beacon(); if (beacon_mine_at >= 0) call_beacon = true; }
        else if (call_beacon && beacon_mine_at >= 0 && adv < call_bar && beacon_slot() == MY_SLOT) { drop_beacon(); }
    }

    void need_flx() {
        if (flx_ok) return;
        flx_ok = true;
        fight_features(flx, fc);
    }
    // the learned model's delta = E[win - lose | fight] - E[win - lose | flee] (fightmodel.h) vetoes the
    // hand rule's fight/flee choice when it is clear: bit 1 turns a flee into a battle (delta >= margin, more while
    // carrying gems), bit 2 turns a battle or a team kill into a flee (delta <= -margin).
    // evaluated lazily: only where the hand rule has picked a fight or a flee (fuel)
    double ml_delta() {
        if (!ml_done) {
            ml_done = true;
            static_assert(FM_NIN == FL_NF, "fightmodel.h was built for another feature vector");
            need_flx();
            ml_d = fm_delta(flx);
        }
        return ml_d;
    }
    bool ml_fight() { return ml_ok && ml_delta() >= ml_mf; }
    bool ml_flee() { return ml_ok && ml_delta() <= -ml_mr; }
    void ml_run() {
        set_danger(true);
        battle_until = -1;
        micro_fight(MM_FLEE, focus);
        if (!ME.walked && ME.hp < ME.mhp * 0.6f && safe_to_channel()) start_recall();
    }

    void read_my_burst() {
        for (int s = 0; s < 2; ++s) {
            const Ab& a = ME.ab[s];
            if (ready(s) && (a.dmg || a.aoe) && (a.enemies || a.ground) && !a.dash) {
                float v = 0.f;
                my_dmg(s, v);
                if (v > my_sp) { my_sp2 = my_sp; my_sp = v; } else my_sp2 = std::max(my_sp2, v);
            }
        }
        // a landing dash (Heart of Ruin) is its biggest hit, and the allies' sums already count it
        // (kit SK_AREA), so mine left it out while theirs had it. Reach 3 (dash 2, radius 1), one cast: this turn it
        // needs the cast left (shield_ahead holds the shield while a dash is ready), over two turns only the cooldown.
        for (int s = 0; s < 2; ++s)
            if (ME.ab[s].dash && ME.ab[s].land > 0 && ME.cd[s] == 0) {
                float v = 0.f;
                my_dmg(s, v);
                my_land = v;
                if (ME.can_cast) my_land_now = v;
            }
    }

    // a kill I land alone from where I stand: the weakest enemy my ready damage finishes this turn
    Champ* kill_here_target() const {
        Champ* best = nullptr;
        for (auto& e : ENEMY) {
            if (!targetable(e)) continue;
            const int d = cheb(e.p, ME.p);
            float mine = (d <= reach ? my_str() : 0.f) + (d <= 4 ? my_sp : 0.f);
            if (d <= 3 && my_land_now > 0.f) mine = std::max(mine, my_land_now + (ME.melee ? my_str() : 0.f));
            if (mine >= e.hp && (!best || e.hp < best->hp)) best = &e;
        }
        return best;
    }

    // the whole team's ready burst on one enemy; if it kills, go (every ally runs the same sum on the same picture)
    Champ* team_kill_target() {
        Champ* tk = nullptr;
        float tk_m = 1e9f;
        for (auto& e : ENEMY) {
            if (!targetable(e)) continue;
            const int d = cheb(e.p, ME.p);
            if (d > 5) continue;
            // me: one step first
            float team = (d - 1 <= reach ? my_str() * 2.f : 0.f) + (d - 1 <= 4 ? my_sp + my_sp2 : 0.f);
            if (d - 1 <= 3) team += my_land;
            int helpers = 0;
            for (int i = 0; i < (int)ALLY.size(); ++i) {
                Champ& al = ALLY[i];
                if (i == MY_SLOT || !al.vis || !al.alive || al.hp <= 0 || al.channel) continue;
                const int da = cheb(al.p, e.p);
                if (da > TEAM_KILL_RADIUS) continue;
                const int dm = std::max(0, da - 1);
                read_ecd(al);
                if (al.stunned) continue;
                const Kit kt = kit_of(al.type);
                const int ar = attack_reach(is_melee(al.type));
                float sp = 0.f, sp2 = 0.f, land = 0.f; bool rst = false;
                for (int s = 0; s < 2; ++s) {
                    if (al.ecd[s] != 0) continue;   // unknown (-1) counts as not ready
                    const SlotK& sk = kt.s[s];
                    if (sk.kind == SK_AREA && is_hor(al)) { land = sk.mult * al.focus; continue; }   // its landing
                    if (sk.kind == SK_DMG || sk.kind == SK_AREA) {
                        const float v = sk.mult * al.focus;
                        if (v > sp) { sp2 = sp; sp = v; } else sp2 = std::max(sp2, v);
                    }
                    if (sk.kind == SK_RESET) rst = true;
                }
                float b = 0.f;
                if (dm <= ar) b += al.str * ((rst ? 2.f : 1.f) + 1.f);
                if (dm <= 4) b += sp + sp2;
                if (dm <= 3) b += land;
                if (is_hor(al)) b *= hor_mult(al);
                if (b > 0.f) { team += b; ++helpers; }
            }
            const float need = e.hp * TEAM_KILL_HEALTH_FACTOR;
            const bool i_help = (d - 1 <= reach) || (d - 1 <= 4 && my_sp > 0.f) || (d - 1 <= 3 && my_land > 0.f);
            if (helpers == 0 || !i_help || team < need || very_low) continue;
            // not into a crowd: the other enemies close to it must not outnumber us
            int crowd = 0;
            for (auto& o : ENEMY) if (&o != &e && targetable(o) && cheb(o.p, e.p) <= 4) ++crowd;
            if (crowd > helpers) continue;
            if (in_es_zone(e.p) || in_es_zone(ME.p)) continue;   // no team chase by their spawn
            if (heavy_bag) continue;   // a carrier joins no team kill
            if (!tk || e.hp < tk_m) { tk = &e; tk_m = e.hp; }
        }
        return tk;
    }

    // an enemy a move could bring into reach, whose health my best damage finishes, away from their spawn
    ALWAYS_INLINE bool kill_after_move_possible(const Champ& e, int frs) const {
        if (!targetable(e)) return false;
        const int d = cheb(e.p, ME.p);
        if (d < 2 || d > reach + 3 || e.hp > my_str() + std::max(std::max(my_sp, my_land_now), frs >= 0 ? my_str() : 0.f)) return false;
        return !in_es_zone(e.p) && !in_es_zone(ME.p);
    }

    // alone, after a walk (spells that allow it, or a ranged reset), a dash, or a dash and a walk (melee);
    // the micro's own plan shapes: walk end tiles at 1, dash at 2, dash and walk at 3
    bool try_kill_after_move() {
        const int fds = dash_slot(), frs = reset_slot();
        bool any = false;
        for (auto& e : ENEMY) if (kill_after_move_possible(e, frs)) { any = true; break; }
        if (!any) return false;
        float wsp = 0.f;   // best ready damage spell that allows walking
        for (int s = 0; s < 2; ++s) {
            const Ab& a = ME.ab[s];
            if (ready(s) && (a.dmg || a.aoe) && (a.enemies || a.ground) && !a.dash && !a.excl) {
                float v = 0.f;
                my_dmg(s, v);
                wsp = std::max(wsp, v);
            }
        }
        Champ* fe = nullptr;
        vec2 fend{-1, -1}, fdash{-1, -1};
        float fth = 1e9f;
        const bool dash_fuel = fds >= 0 && (long)bc::fuel_remaining() > 30000;
        for (auto& e : ENEMY) {
            if (!kill_after_move_possible(e, frs)) continue;
            auto take = [&](vec2 q, float dmg, vec2 via) {
                if (dmg < e.hp) return;
                const float th = fin_threat(q, &e);
                if (th < ME.hp * 0.9f && th < fth) { fth = th; fe = &e; fend = q; fdash = via; }
            };
            if (ME.can_move)
                for (auto dd : DIRS) {
                    const vec2 q = ME.p + dd;
                    if (!walkable(q)) continue;
                    const float sp = (cheb(ME.p, e.p) <= 4 || cheb(q, e.p) <= 4) ? wsp : 0.f;
                    float dmg;
                    if (ME.melee) { const float a = cheb(q, e.p) <= 1 ? my_str() : 0.f; dmg = a + (a > 0.f ? std::max(sp, frs >= 0 ? my_str() : 0.f) : sp); }
                    else dmg = std::max((frs >= 0 && cheb(q, e.p) <= 3) ? my_str() : 0.f, sp);
                    take(q, dmg, vec2{-1, -1});
                }
            if (dash_fuel)
                for (auto d1 : DIRS) {
                    const vec2 a = ME.p + d1;
                    if (!dash_mid_ok(a)) continue;
                    for (auto d2 : DIRS) {
                        const vec2 b = a + d2;
                        if (cheb(b, ME.p) != 2 || !walkable(b)) continue;
                        if (cheb(b, e.p) <= reach) take(b, my_str() + (cheb(b, e.p) <= 1 ? my_land_now : 0.f), b);   // + the landing
                        else if (ME.melee && ME.can_move && cheb(b, e.p) == 2)
                            for (auto d3 : DIRS) {
                                const vec2 c = b + d3;
                                if (cheb(c, ME.p) == 3 && walkable(c) && cheb(c, e.p) <= 1) take(c, my_str(), b);
                            }
                    }
                }
        }
        if (!fe) return false;
        FIN_END = fend; FIN_DASH = fdash;
        M_PULL_NEXT = TEAM_KILL_PULL;
        micro_fight(MM_BATTLE, fe);
        return true;
    }

    // go for this enemy, and call the team
    void commit_kill(Champ* e) {
        battle_until = r + 3;
        raise_beacon();
        M_PULL_NEXT = TEAM_KILL_PULL;   // actually walk in (the micro's own pull is 18 a tile)
        micro_fight(MM_BATTLE, e);
    }

    // A kill from here is certain, so nothing weighs against it. A team kill depends on allies, and the model may veto it.
    bool try_kill() {
        read_my_burst();
        if (Champ* e = kill_here_target()) { commit_kill(e); return true; }
        Champ* tk = team_kill_target();
        if (!tk) return !heavy_bag && ME.can_attack && (ME.can_move || dash_slot() >= 0) && try_kill_after_move();
        if (ml_flee()) ml_run();
        else commit_kill(tk);
        return true;
    }

    // the finish model. Hunters that stay next to an enemy below 40% kill it about twice as often as hunters
    // that step away, for about the same own-death rate (top teams and us alike). Two logistic models
    // fitted on recorded moments: P(the enemy dies within 8 rounds) from the team kill time, my distance, my
    // time to die, the crowd and the share of hunters staying; P(I die) from the same plus whether I stay. Others are
    // assumed to keep doing what they do (within 3 = staying). Gain = dP(kill) x kill value - dP(death) x my value.
    bool try_finish() {
        static const float FK_W[] = {-1.0952f, -1.5088f, -1.0173f, 0.3546f, -0.0478f, -0.3131f, 0.0441f, 0.4877f, 0.0793f, 0.0002f, 2.0436f};
        static const float FD_W[] = {1.3672f, 0.6648f, -1.7200f, -1.7572f, -1.1832f, -0.0486f, 0.2227f, -0.0273f, -0.4435f, 0.4430f, -0.4733f, 0.6024f};
        auto rate_of = [](CT t, float str, float fo) { return str + spell_avg(t, fo); };
        auto sg = [](float z) { return 1.f / (1.f + std::exp(-std::max(-30.f, std::min(30.f, z)))); };
        const float my_rate = rate_of(ME.type, ME.str, ME.focus) * my_mult();   // HoR passive, as the enemy rates below
        Champ* fbest = nullptr;
        float fev = FINISH_MIN_GAIN;
        for (auto& e : ENEMY) {
            if (!targetable(e) || on_enemy_spawn(e.p) || e.hp >= 0.4f * e.mhp) continue;
            if (in_es_zone(e.p) || in_es_zone(ME.p)) continue;   // it heals there
            const int d = cheb(e.p, ME.p);
            if (d > 6) continue;
            int nh = 1, nh3 = d <= 3 ? 1 : 0, others = 0;
            float rsum = my_rate;
            for (int i = 0; i < (int)ALLY.size(); ++i) {
                const Champ& al = ALLY[i];
                if (i == MY_SLOT || !al.vis || !al.alive || al.hp <= 0) continue;
                const int da = cheb(al.p, e.p);
                if (da > 6) continue;
                ++nh; rsum += rate_of(al.type, al.str, al.focus);
                if (da <= 3) { ++nh3; ++others; }
            }
            float thr = rate_of(e.type, e.str, e.focus) * (is_hor(e) ? hor_mult(e) : 1.f);
            int vm4 = 0, hm4 = 0;
            for (auto& o : ENEMY) {
                if (&o == &e || !targetable(o)) continue;
                if (cheb(o.p, e.p) <= 4) ++vm4;
                if (cheb(o.p, ME.p) <= 4) { ++hm4; thr += rate_of(o.type, o.str, o.focus) * (is_hor(o) ? hor_mult(o) : 1.f); }
            }
            const float x[10] = {1.f, std::log1p(e.hp / std::max(1.f, rsum)), d / 6.f, std::log1p(ME.hp / std::max(1.f, thr)),
                                 ME.hp / std::max(1.f, ME.mhp), (float)vm4, (float)hm4, (float)nh3, ME.melee ? 1.f : 0.f, e.channel ? 1.f : 0.f};
            float zk = 0.f, zd = 0.f;
            for (int k = 0; k < 10; ++k) { zk += FK_W[k] * x[k]; zd += FD_W[k] * x[k]; }
            const float sw = (others + 1) / (float)nh, so = others / (float)nh;
            const float dk = sg(zk + FK_W[10] * sw) - sg(zk + FK_W[10] * so);
            const float dd = sg(zd + FD_W[10] * sw + FD_W[11]) - sg(zd + FD_W[10] * so);
            const float ev = dk * (1.f + e.gems / FINISH_BAG_DOUBLES) - dd * (1.f + ME.gems / FINISH_BAG_DOUBLES);
            if (ev > fev) { fev = ev; fbest = &e; }
        }
        if (!fbest) return false;
        commit_kill(fbest);
        return true;
    }

    // lethal burst: what the visible enemies can land on us before our next turn (a ready spell within 4, a basic
    // attack within a walk or a dash). The #1 team's bodies survive 73% of dips below a quarter of their health,
    // ours 39%, and our deaths come from about 40% health within three rounds: leave before the burst lands.
    bool lethal_burst_ahead() {
        float burst = 0.f;
        for (auto& e : ENEMY) {
            // spawn campers too (their burst is real; they are only never a target)
            if (!e.vis || !e.alive || e.hp <= 0 || standoff_ignored(e) || chan_ignored(e)) continue;
            burst += danger_at(enemy_power(e), cheb(e.p, ME.p));
        }
        float k = LETHAL_BURST_SHARE;
        // CARRIER_GEMS: a heavy bag is the opponent's prize (collision: 465 gems a lost game taken from carriers)
        if (heavy_bag) k = std::min(k, LETHAL_BURST_SHARE_CARRIER);
        return burst >= ME.hp * k;
    }

    // trapped: 41% of our platform deaths came after the lethal-burst flee fired, mostly melee bodies
    // stepping away from an adjacent melee enemy that walked and hit again. A retreat that cannot leave every
    // enemy's reach before their next turn only gives up our damage: fight back instead.
    bool trapped_here() {
        // this asks where we could run to, and a runner asks for an earlier turn next round: read the enemies' turns so
        struct AsRunner { const bool was = DANGER; AsRunner() { DANGER = true; } ~AsRunner() { DANGER = was; } } as_runner;
        auto threat_at = [&](vec2 q) {
            float b = 0.f;
            for (auto& e : ENEMY) {
                if (!targetable(e) || chan_ignored(e)) continue;
                b += danger_at(enemy_power(e), cheb(e.p, q));
            }
            return b;
        };
        bool escape = false;
        if (ME.can_move)
            for (auto d : DIRS) { const vec2 q = ME.p + d; if (walkable(q) && threat_at(q) < ME.hp * 0.8f) { escape = true; break; } }
        if (!escape && ME.can_cast && dash_slot() >= 0) {
            bool seen[49] = {};
            for (auto d1 : DIRS) {
                const vec2 mid = ME.p + d1;
                if (!dash_mid_ok(mid)) continue;
                for (auto d2 : DIRS) {
                    const vec2 q = mid + d2;
                    if (cheb(q, ME.p) != 2 || !walkable(q)) continue;
                    const int qi = (q.x - ME.p.x + 3) * 7 + q.y - ME.p.y + 3;
                    if (seen[qi]) continue;
                    seen[qi] = true;
                    if (threat_at(q) < ME.hp * 0.8f) { escape = true; break; }
                    if (ME.can_move) for (auto d3 : DIRS) {
                        const vec2 end = q + d3;
                        if (cheb(end, ME.p) == 3 && walkable(end) && threat_at(end) < ME.hp * 0.8f) { escape = true; break; }
                    }
                    if (escape) break;
                }
                if (escape) break;
            }
        }
        return !escape && cheb(focus->p, ME.p) <= (ME.melee ? 2 : 4);
    }

    bool try_flee() {
        const bool lethal_flee = !capturing_now && !on_own_spawn() && lethal_burst_ahead();
        // by their spawn with an enemy about, leave (they heal and respawn there; the trapped check below
        // still fights back when no step gets out of their reach)
        const bool es_leave = in_es_zone(ME.p) && dmin <= 6 && !capturing_now;
        const bool want_out = adv < flee_at || outnumbered || very_low || lethal_flee || es_leave;
        if (!force_flee && !(want_out && dmin <= 6)) return false;
        const bool trapped = focus && !force_flee && !on_own_spawn() && trapped_here();
        // With endpoint checking enabled, adjacency alone cannot override
        // a legal escape. Mandatory channel-cancel retreats also stay fleeing.
        if (trapped) {
            set_danger(true);
            micro_fight(MM_BATTLE, focus);
            return true;
        }
        if (!very_low && !es_leave && ml_fight()) {
            battle_until = r + 3;
            raise_beacon();
            micro_fight(MM_BATTLE, focus);
            return true;
        }
        ml_run();
        return true;
    }

    // Ren, Ryth and Marcella have damage spells but are melee: they do not kite, not a caster here
    // the code counted every damage spell (Marcella, Ryth, HoR, Ren too) against this comment
    bool mc_is_caster(const Champ* f) const { return spell_burst(f->type, f->focus) > 0; }
    // may a melee champion commit to this caster from 3-4?
    bool mc_ok(const Champ* f) const {
        const int df = cheb(f->p, ME.p);
        bool ally = false; int us_n = 1, th_n = 0;
        for (int i = 0; i < (int)ALLY.size(); ++i) {
            const Champ& al = ALLY[i];
            if (i == MY_SLOT || !al.vis || !al.alive || al.hp <= 0 || al.channel) continue;
            if (cheb(al.p, f->p) <= 5) ally = true;
            if (cheb(al.p, ME.p) <= 5) ++us_n;
        }
        for (auto& e : ENEMY) if (targetable(e) && cheb(e.p, ME.p) <= 4) ++th_n;
        if (!ally || th_n > us_n) return false;
        if (dash_slot() < 0) {
            bool step = false;
            for (int dx = -1; dx <= 1 && !step; ++dx) for (int dy = -1; dy <= 1; ++dy) {
                const vec2 q{ME.p.x + dx, ME.p.y + dy};
                if ((dx || dy) && walkable(q) && cheb(q, f->p) < df) { step = true; break; }
            }
            if (!step) return false;
        }
        return true;
    }

    void set_fight_range() {
        // the no-chase pause (after four turns of chasing without a hit) is about walking after enemies;
        // it left Cassi standing next to a Vendetta for five turns without a spell (pit)
        // the battle beacon kept in_battle on after the four-turn no-hit stop (Varg stood 8 turns under a Stellar)
        may_chase = r >= no_chase_until || in_battle || (focus && cheb(focus->p, ME.p) <= reach);
        mc_caster = focus && ME.melee && cheb(focus->p, ME.p) >= 3 && mc_is_caster(focus);
        // the checks gate only the melee-caster commit (a start from 4 and the hard pull); a battle from 3 is as scX
        mc_safe = !mc_caster || mc_ok(focus);
        // a battle on keeps its fight one step wider than the range that starts one. The micro stepped a
        // Varg back to 4 (melee entry 3), the next turn read "no fight" and walked the job back to 3: BATTLE / JOB every
        // other round, 450 such steps in 80 suite games (eight: a Varg 10 rounds between (25,12) and (26,13))
        fight_r = !focus ? 0 : (ME.melee ? (mc_is_caster(focus) && mc_safe ? 4 : 3) : 5) + (in_battle ? 1 : 0)
                + (champ(ME.type).tank ? 1 : 0);   // a tank goes in first
    }

    // melee join: an ally already trading with an enemy up to JOIN_RADIUS from us. Our melee kept the job 4-8 away
    // while Vesper fought alone (20 ladder games: Vesper screened in group fights 35%, the #1 team 59%; melee at
    // 6-8 from a fight joined it 14%, the other teams 22-25%)
    void try_melee_join() {
        Champ* join = nullptr;
        float jk = 1e18f;
        for (auto& e : ENEMY) {
            if (!targetable(e)) continue;
            const int d = cheb(e.p, ME.p);
            if (d > JOIN_RADIUS || in_es_zone(e.p)) continue;
            bool engaged = false;
            for (int i = 0; i < (int)ALLY.size() && !engaged; ++i) {
                const Champ& al = ALLY[i];
                if (i == MY_SLOT || !al.vis || !al.alive || al.hp <= 0 || al.channel) continue;
                if (cheb(al.p, e.p) <= (is_melee(al.type) && is_melee(e.type) ? 2 : 4)) engaged = true;   // jn1: both sides' reach
            }
            if (!engaged) continue;
            const float key = e.hp + d * 60.f;
            if (key < jk) { jk = key; join = &e; }
        }
        // the join bar is the fight bar
        if (join && adv >= fight_at) {
            // the model judges the enemy we would join. Its veto cancels the join: we were not in this fight, so it is
            // no reason to run
            Champ* const old_focus = focus;
            const bool old_ml_ok = ml_ok;
            fc.focus = join; flx_ok = ml_done = false; ml_ok = dmin <= 6;
            if (ml_flee()) { fc.focus = old_focus; flx_ok = ml_done = false; ml_ok = old_ml_ok; }
            else { focus = join; fight_r = cheb(join->p, ME.p); joining = true; }
        }
    }

    // screen wait: a ranged champion with no melee ally between it and the focus, while a healthy melee ally
    // is coming, takes only the hits the micro finds safe from where it stands (no pull) instead of walking in
    // first. 20 ladder games: our Vesper screened in group fights 35% (the #1 team 59%) and the first hitter
    // 41% (29%). A low focus is a finisher: no wait (a channel is not: the fake recall keeps one up all game). At most SCREEN_WAIT_TURNS turns in a row.
    bool screen_wait() {
        const int dme = cheb(focus->p, ME.p);
        bool screened = false, coming = false;
        for (int i = 0; i < (int)ALLY.size(); ++i) {
            const Champ& al = ALLY[i];
            if (i == MY_SLOT || !al.vis || !al.alive || al.hp <= 0 || al.channel || !is_melee(al.type)) continue;
            const int da = cheb(al.p, focus->p);
            if (da <= 1 || da < dme) screened = true;
            else if (cheb(al.p, ME.p) <= 8 && al.hp >= al.mhp * 0.5f) coming = true;
        }
        // only a BATTLE reset the count, so waits used up in an old fight were gone in the next
        static int scr_last = -10;
        if (r - scr_last > 1) scr_wait = 0;
        if (!screened && coming && scr_wait < SCREEN_WAIT_TURNS) {
            ++scr_wait;
            scr_last = r;
            battle_until = r + 3;
            raise_beacon();
            M_PULL_NEXT = 0.01f;
            micro_fight(MM_POKE, focus);
            return true;
        }
        return false;
    }

    bool try_battle() {
        if (ml_flee()) { ml_run(); return true; }
        if (!ME.melee && focus->hp > focus->mhp * 0.35f && screen_wait()) return true;
        scr_wait = 0;
        battle_until = r + 3;
        raise_beacon();
        // a melee battle against a caster walks in hard (the micro's pull of 18 left Varg standing at 4)
        if ((mc_caster && mc_safe) || joining) M_PULL_NEXT = TEAM_KILL_PULL;
        const bool hit = micro_fight(MM_BATTLE, focus);
        chase_turns = hit ? 0 : chase_turns + 1;
        if (chase_turns >= 4) { no_chase_until = r + 15; chase_turns = 0; battle_until = -1; }
        return true;
    }

    void poke_or_leave() {
        if (ME.hp < ME.mhp * RETREAT_HEALTH) { ml_run(); return; }
        // a heavy bag gets no BATTLE, SOLOKILL or FINM; nor a walk after its focus here
        Champ* pf = adv >= fight_at && !heavy_bag ? focus : nullptr;
        if (pf) {
            // walking at an enemy we cannot reach this turn is how casters poke us to death
            const int dfoc = cheb(pf->p, ME.p);
            bool can_reach = !ME.melee || dfoc <= 2 || (dash_slot() >= 0 && dfoc <= 4);
            // a melee champion at 3-4 from a caster walks in when an ally is close to it
            bool mc_ally = false;
            if (ME.melee && !can_reach && dfoc <= 4 && mc_is_caster(pf)) {
                for (int i = 0; i < (int)ALLY.size(); ++i) {
                    const Champ& al = ALLY[i];
                    if (i != MY_SLOT && al.vis && al.alive && al.hp > 0 && !al.channel && cheb(al.p, pf->p) <= 5) mc_ally = true;
                }
                if (mc_ally && mc_ok(pf)) { can_reach = true; M_PULL_NEXT = TEAM_KILL_PULL; }
            }
            if (!may_chase || !can_reach) {
                // alone against it, do not stand in its spell range
                if (ME.melee && !mc_ally && dfoc <= 4 && mc_is_caster(pf)) { ml_run(); return; }
                pf = nullptr;
            }
        }
        const bool hit = micro_fight(MM_POKE, pf);
        if (pf) {
            chase_turns = hit ? 0 : chase_turns + 1;
            if (chase_turns >= 4) { no_chase_until = r + 15; chase_turns = 0; }
        }
    }

    // Neutral ground. On the ladder 2/3 of the damage we take lands while we do not hit back, mostly spells cast
    // from range 4 while we carry on with a job in their reach. If an enemy can hit us next turn without moving
    // (spells exclude walking: casters reach 4, ranged 3, melee 2, a dash adds 2; only casters count,
    // the ones with a damage or stun spell), the micro plays the turn: trade if it pays, else end out of
    // reach. Below the heal fraction it is a retreat.
    bool try_neutral_ground() {
        bool threat = false, can_hit = false;
        bool spell_up = false;
        for (int s = 0; s < 2; ++s) { const Ab& a = ME.ab[s]; if (ready(s) && (a.dmg || a.aoe || a.cc) && (a.enemies || a.ground) && !(a.excl && ME.walked)) spell_up = true; }
        // poke: our own ready damage spell reaches an enemy from here (an area spell centred next to it
        // reaches 5): take the shot. The #1 team casts on 36% of such Stellar turns, we did on 24%.
        bool area_up = false;
        for (int s = 0; s < 2; ++s) { const Ab& a = ME.ab[s]; if (ready(s) && (a.aoe || (a.ground && !a.enemies)) && (a.dmg || a.aoe) && !(a.excl && ME.walked)) area_up = true; }
        for (auto& e : ENEMY) {
            if (!targetable(e)) continue;
            const bool caster = spell_burst(e.type, e.focus) > 0;
            if (spell_up && !e.channel && cheb(e.p, ME.p) <= (area_up ? 5 : 4)) can_hit = true;
            if (!caster) continue;   // 2: only champions with a spell (the ladder's free damage)
            const int d = cheb(e.p, ME.p);
            if (!chan_ignored(e) && d <= 4) threat = true;   // every caster reaches 4
        }
        if (!(threat || can_hit)) return false;
        poke_or_leave();
        return true;
    }

    bool play() {
        shield_ahead();
        adv = bal.adv();
        call_team_on_first_sight();
        if (on_own_spawn()) {
            // our spawn heals us fully every round and they cannot step on it: hold and hit
            if (adv < 1.0f || (target && cheb(target->p, ME.p) <= reach)) { micro_fight(MM_HOLD, pick_focus(reach)); return true; }
        }
        // Team-consistent battle decisions: with most of the team present the bar to fight is lower,
        // and once a battle is on we keep fighting (half the team running is the worst outcome).
        in_battle = r <= battle_until || (beacon_near(8) && !(beacon_mine_at >= 0 && call_beacon));
        fight_at = bal.us_n >= 3 ? FIGHT_ADV_TEAM : FIGHT_ADV_SMALL;
        flee_at = bal.us_n >= 4 ? FLEE_ADV_FOUR : bal.us_n >= 3 ? FLEE_ADV_THREE : FLEE_ADV_SMALL;
        if (!ANY_GEM_GROUP && !on_own_spawn()) {   // nearly dead: almost every map has a non-monolith gem group
            int ne = 0, na = 1;
            for (auto& e : ENEMY) if (e.vis && e.alive && e.hp > 0 && !on_enemy_spawn(e.p) && cheb(e.p, ME.p) <= 5) ++ne;
            for (auto& al : ALLY) if (al.vis && al.alive && al.hp > 0 && !al.channel && cheb(al.p, ME.p) <= 5) ++na;
            outnumbered = ne > na;
            if (dmin <= 3) outnumbered = false;   // already caught: running from reach rarely works
        }
        // the in-battle caps (fight bar <= 0.15, flee bar <= 0.50) were removed: with the bars above
        // (-0.10 / 0.0 and 0.50) they never bound. A battle on still widens fight_r by one and lets the chase go on.
        // Battle hysteresis must not erase the cost of losing our bag.
        if (ME.gems >= FIGHT_BAG_GEMS) { fight_at += 0.25f; flee_at += 0.3f; }
        // focus fire: the team's focus (pick_focus: every one of us scores it from the nearest of us)
        focus = pick_focus(reach);
        if (!focus) focus = target;
        // STOP_FIGHT_HEALTH: the #1 team's bodies below a quarter of their health stop fighting (12% of those still fighting
        // die, ours 52%); the threshold is higher, and an adjacent melee enemy no longer keeps us in (a step or a dash
        // away beats trading at that health), unless the micro's own kill check finds a kill this turn.
        very_low = ME.hp < ME.mhp * STOP_FIGHT_HEALTH;
        force_flee = r <= FORCE_FLEE_UNTIL;
        // no ghosts (enemies out of view) anywhere. The fight model's `log adv_eng` input is `log adv`, as on its
        // training turns that had no ghost (15% of ghosts arrived)
        fc = FCtx{&bal, adv, adv, fight_at, flee_at, dmin, campers, capturing_now, in_battle, focus};
        ml_ok = focus && dmin <= 6 && !capturing_now && !force_flee;
        ml_mf = FIGHT_MODEL_MARGIN + (ME.gems >= FIGHT_BAG_GEMS ? FIGHT_MODEL_MARGIN_CARRY : 0.f);
        ml_mr = FIGHT_MODEL_MARGIN;
        // a heavy bag suppresses every optional chase: a kill hunt must not pull a carrier away from the altar
        heavy_bag = ME.gems >= CARRIER_GEMS;
        // 16: not while my job camp next to me is cleared (reward waiting) or nearly so (stampede: the kill hunt left the boss reward to them)
        const bool tk_job = JOB >= 0 && JOB < (int)G.size() && gdist(G[JOB], ME.p) <= 3 &&
            (G[JOB].st == ST_REWARD || (G[JOB].st == ST_ALIVE && G[JOB].hp_left <= TEAM_KILL_CAMP_HEALTH));
        if (!tk_job && !force_flee && !capturing_now && !on_own_spawn() && try_kill()) return true;
        if (!force_flee && !heavy_bag && !capturing_now && !on_own_spawn() && try_finish()) return true;
        if (try_flee()) return true;
        // the fight does not say run and their capture can still be broken: the tile comes before spacing the fight
        if (!capturing_now && try_break_capture()) { if (focus && cheb(focus->p, ME.p) <= reach && ME.can_attack) attack_champ(*focus); return true; }
        set_fight_range();
        if (ME.melee && !very_low && !heavy_bag && !capturing_now && !force_flee &&
            ME.hp >= ME.mhp * JOIN_MIN_HEALTH && (!focus || cheb(focus->p, ME.p) > fight_r)) try_melee_join();
        if (focus && !heavy_bag && adv >= fight_at && (may_chase || joining) && !capturing_now && cheb(focus->p, ME.p) <= fight_r && try_battle()) return true;
        if ((!capturing_now || ME.hp < ME.mhp * 0.5f) && try_neutral_ground()) return true;
        // opportunistic shot without changing plans
        if (focus && cheb(focus->p, ME.p) <= reach && ME.can_attack) attack_champ(*focus);
        return false;
    }
};
