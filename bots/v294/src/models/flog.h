// learned fight/flee decision.
//   fight_features(): one feature vector from what this champion knows at its fight/flee decision (think(), enemies
//                     branch, just before the team-kill check), quantized to 0.01 like the model's training rows.
//   fightmodel.h's delta (fight minus flee) vetoes the hand rule's fight/flee choice when clear (brain_fight_turn.h).
#pragma once

static constexpr int FL_NF = 68;

// What think() knows at the decision; filled before fight_features().
struct FCtx {
    const Balance* bal;
    float adv, adv_eng, fight_at, flee_at;
    int dmin, campers;
    bool capturing, in_battle;
    const Champ* focus;
};

static inline float fq(float v) { return std::nearbyint(v * 100.f) / 100.f; }   // the log's 0.01 grid

static void fight_features(float* x, const FCtx& c) {
    const int r = rnd();
    int k = 0;
    auto put = [&](float v) { x[k++] = v; };
    // ---- self
    float my_sp = 0.f;   // my biggest ready damage spell (no host call: kit table)
    for (int s = 0; s < 2; ++s) { const Ab& a = ME.ab[s]; if (ready(s) && (a.dmg || a.aoe) && (a.enemies || a.ground) && !a.dash) my_sp = spell_burst(ME.type, ME.focus) * my_mult(); }
    const int hd = F_HOME.at(ME.p);
    put(ME.hp); put(ME.mhp); put(ME.hp / std::max(1.f, ME.mhp)); put((float)ME.gems); put(ME.str); put(ME.focus);
    put(ME.melee ? 1.f : 0.f); put(dash_slot() >= 0 ? 1.f : 0.f); put((float)ME.cd[0]); put((float)ME.cd[1]);
    put(my_dps_c()); put(champ_heal(ME.type, ME.focus)); put(my_sp);   // own dps with the HoR passive
    put(hd == UNREACH ? 99.f : (float)std::min<int>(hd, 99)); put(ME.drop);
    put(0.f); put(is_hor_kit(ME.type) ? 1.f : 0.f);   // 0: was "I am Cassi or Issac" (the model's feature layout stays)
    // ---- context
    put((float)r); put((float)ALIVE_ALLIES); put(c.capturing ? 1.f : 0.f); put(c.in_battle ? 1.f : 0.f);
    put(0.f); put(ANY_GEM_GROUP ? 1.f : 0.f);   // the first: monolith-map mode (a model input)
    put(-1.f);   // linked partner's health fraction: -1 = none (we never draft Cassi & Issac)
    // ---- allies in view
    int a5n = 0, a8n = 0, admin = 99;
    float a5hp = 0, a5d = 0, a8hp = 0, a8d = 0, aminf = 1.f;
    for (int i = 0; i < (int)ALLY.size(); ++i) {
        const Champ& al = ALLY[i];
        if (i == MY_SLOT || !al.vis || !al.alive || al.hp <= 0) continue;
        const int d = cheb(al.p, ME.p);
        admin = std::min(admin, d);
        if (d > 8) continue;
        const float dp = al.channel ? 0.f : champ_dps_c(al);
        ++a8n; a8hp += al.hp; a8d += dp; aminf = std::min(aminf, al.hp / std::max(1.f, al.mhp));
        if (d <= 5) { ++a5n; a5hp += al.hp; a5d += dp; }
    }
    put((float)a5n); put(a5hp); put(a5d); put((float)a8n); put(a8hp); put(a8d); put((float)admin); put(aminf);
    // ---- enemies in view (not on their spawn), within 8
    int en = 0, ealive = 0, m2 = 0, r4 = 0, ech = 0, erdy = 0;
    const int est = 0;   // the model trained with no stun reads: this input stays 0
    float ehp = 0, emhp = 0, ed = 0, ehl = 0, emin = 0, eminf = 1.f, eg = 0, burst = 0;
    for (auto& e : ENEMY) {
        if (e.alive) ++ealive;
        if (!targetable(e)) continue;
        const int d = cheb(e.p, ME.p);
        if (d > 8) continue;
        ++en; ehp += e.hp; emhp += e.mhp; ed += champ_dps_c(e); ehl += champ_heal(e.type, e.focus); eg += (float)e.gems;
        if (en == 1 || e.hp < emin) emin = e.hp;
        eminf = std::min(eminf, e.hp / std::max(1.f, e.mhp));
        if (is_melee(e.type) && d <= 2) ++m2;
        if (d <= 4) ++r4;
        if (e.channel) ++ech;
        // the lethal-burst estimate (brain_fight_turn.h)
        if (e.channel || d > 6) continue;
        float b = 0.f;
        if (d <= 4) {
            read_ecd(e);
            const float sb = ready_spell_burst(e);
            if (sb > 0.f) { b += sb; ++erdy; }
        }
        else if (d == 5 && area_slot(e.type) >= 0) { read_ecd(e); if (e.ecd[area_slot(e.type)] <= 0) b += area_burst(e.type, e.focus); }
        const int br = (is_melee(e.type) ? 2 : 3) + (has_dash(e.type) ? 2 : 0);
        if (d <= br) b += e.str;
        if (is_hor(e)) b *= hor_mult(e);
        burst += b;
    }
    put((float)en); put(ehp); put(emhp); put(ed); put(ehl); put(emin); put(eminf); put((float)std::min(c.dmin, 99)); put(eg);
    put((float)m2); put((float)r4); put((float)ech); put((float)est); put(burst); put(burst / std::max(1.f, ME.hp)); put((float)erdy);
    // ---- enemies out of view (ghosts): the three slots stay 0, as on training turns with no ghost
    put(0.f); put(0.f); put(0.f); put((float)ealive);
    // ---- the hand rule's own numbers
    const Balance& b = *c.bal;
    put((float)b.us_n); put(b.us_hp); put(b.us_dps); put((float)b.th_n); put(b.th_hp); put(b.th_dps);
    put(std::log(std::max(1e-3f, std::min(99.f, c.adv)))); put(std::log(std::max(1e-3f, std::min(99.f, c.adv_eng))));
    put(c.fight_at); put(c.flee_at); put((float)c.campers);
    // ---- the focus target and the team's burst on it (strength in reach after a step, plus kit spells)
    if (c.focus) {
        const Champ& f = *c.focus;
        const int df = cheb(f.p, ME.p);
        float tb = (df - 1 <= attack_reach(ME.melee) ? my_str() : 0.f) + (df - 1 <= 4 ? my_sp : 0.f);
        for (int i = 0; i < (int)ALLY.size(); ++i) {
            const Champ& al = ALLY[i];
            if (i == MY_SLOT || !al.vis || !al.alive || al.hp <= 0 || al.channel) continue;
            const int da = std::max(0, cheb(al.p, f.p) - 1);
            if (da <= attack_reach(is_melee(al.type))) tb += al.str;
            if (da <= 4) tb += spell_burst(al.type, al.focus);
        }
        put(f.hp); put(f.hp / std::max(1.f, f.mhp)); put((float)df); put((float)f.gems); put(tb / std::max(1.f, f.hp));
    } else { put(0.f); put(1.f); put(99.f); put(0.f); put(0.f); }
    for (int i = 0; i < k; ++i) x[i] = fq(x[i]);
    for (int i = k; i < FL_NF; ++i) x[i] = 0.f;
}
