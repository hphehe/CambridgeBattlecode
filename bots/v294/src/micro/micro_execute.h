#pragma once

static bool attack_from_here(int exact = -1) {
    Champ* t = exact >= 0 && exact < EN_N ? EC_G[exact] : nullptr;
    const int reach = attack_reach(ME.melee);
    if (!t || !targetable(*t) || cheb(ME.p, t->p) > reach) t = basic_target(ME.p); // API/state mismatch: legacy fallback
    if (!t || !attack_champ(*t)) return false;
    t->hp -= my_str();   // HoR passive
    if (t->hp <= 0) kill_reset_reread();   // (our health estimate: a kill on the engine's side re-reads the same)
    return true;
}
static bool cast_spell_now(int s, int exact = -1, vec2 exact_tile = {-1, -1}) {
    Champ* who = nullptr;
    vec2 tile{-1, -1};
    const bool area = spell_area(s);
    if (exact >= 0 && exact < EN_N) who = EC_G[exact];
    if (exact_tile.x >= 0) tile = exact_tile;
    if (who && targetable(*who) && cheb(ME.p, area ? tile : who->p) <= 4) {
        const bool ok = area ? cast_slot_tile(s, tile) : cast_slot_champ(s, who->h);
        if (ok) {
            LAST_CHAMP_HIT = rnd();
            if (area) { for (auto& o : ENEMY) if (targetable(o) && cheb(o.p, tile) <= 1) o.hp -= SP_DMG[s]; }
            else who->hp -= SP_DMG[s];
            return true;
        }
        who = nullptr; tile = {-1, -1}; // exact action failed: retain the old best-target execution
    }
    if (area && AC_N[s] > 0) {
        float bv = 0;
        for (int c = 0; c < AC_N[s]; ++c) if (cheb(ME.p, AC_P[s][c]) <= 4 && AC_V[s][c] > bv) { bv = AC_V[s][c]; tile = AC_P[s][c]; }
        if (bv <= 0) return false;
        for (auto& o : ENEMY) if (targetable(o) && cheb(o.p, tile) <= 1) { who = &o; break; }
        if (!who || !cast_slot_tile(s, tile)) return false;
        LAST_CHAMP_HIT = rnd();
        for (auto& o : ENEMY) if (targetable(o) && cheb(o.p, tile) <= 1) o.hp -= SP_DMG[s];
        return true;
    }
    if (spell_best(s, ME.p, &who, &tile) <= 0 || !who) return false;
    const bool ok = area ? cast_slot_tile(s, tile) : cast_slot_champ(s, who->h);
    if (!ok) return false;
    LAST_CHAMP_HIT = rnd();
    if (area) { for (auto& o : ENEMY) if (targetable(o) && cheb(o.p, tile) <= 1) o.hp -= SP_DMG[s]; }
    else who->hp -= SP_DMG[s];
    return true;
}

static bool micro_exec(const Plan& P) {
    bool hit = false;
    const int rs = reset_slot();
    if ((P.cast == 0 || P.cast == 1) && P.cast_first) hit |= cast_spell_now(P.cast,
        P.cast_target, P.cast_tile
    );
    if (P.atk == 1) hit |= attack_from_here(
        P.atk_target
    );
    // melee: attack, reset, attack, then walk (rules Q13). The plan scored the reset as a second hit; cast from the end
    // tile only, it was lost whenever the walk left every enemy's reach
    if (ME.melee && P.atk == 1 && P.cast == 2 && P.dash.x < 0 && P.walk.x >= 0 && ME.can_cast && !ME.can_attack && rs >= 0 &&
        basic_target(ME.p)) {
        if (cast_self(rs)) { ME.can_attack = true; hit |= attack_from_here(-1); }
    }
    if (P.walk_first && P.walk.x >= 0 && cheb(P.walk, ME.p) == 1) try_move_to(P.walk);
    if (P.dash.x >= 0 && (!P.walk_first || ME.p == P.walk)) {
        const int ds = dash_slot();
        bool ok = false;
        if (ds >= 0) { try { ok = bc::self().abilities()[ds].use(P.dash); } catch (const std::exception&) { ok = false; } }
        if (ok) {
            prev_pos = ME.p; ME.p = P.dash; ME.cd[ds] = 8; ME.can_cast = false;
            if (P.dash_damage > 0.f) for (auto& o : ENEMY) if (targetable(o) && cheb(o.p, ME.p) <= 1) o.hp -= P.dash_damage;
        }
    }
    if (P.atk == 2) hit |= attack_from_here(P.atk_target);   // dash, hit, walk
    if (!P.walk_first && P.walk.x >= 0 && cheb(P.walk, ME.p) == 1) try_move_to(P.walk);
    if ((P.cast == 0 || P.cast == 1) && !P.cast_first) hit |= cast_spell_now(P.cast,
        P.cast_target, P.cast_tile
    );
    if (P.atk == 3) {
        // ranged after walking: walk, reset, attack
        if (!ME.can_attack && P.cast == 2 && rs >= 0 && ME.can_cast && cast_self(rs)) ME.can_attack = true;
        hit |= attack_from_here(
            P.atk_target
        );
    }
    if (P.cast == 2 && ME.can_cast && !ME.can_attack && rs >= 0 && basic_target(ME.p)) {
        if (cast_self(rs)) { ME.can_attack = true; hit |= attack_from_here(
            P.reset_target
        ); }
    }
    // a kill this turn reset the cooldowns; with the cast unused, reset and hit again
    // an unused cast: the self-shield (Heart of Ruin, Varg) when a threat reaches us (unless the model chose not to)
    if (ME.can_cast && !QM_NOSH) {
        const int sh = shield_slot();
        if (sh >= 0) for (int i = 0; i < TH_N; ++i) if (cheb(TH[i].p, ME.p) <= TH[i].power.reach) { cast_self(sh); break; }
    }
    return hit;
}
