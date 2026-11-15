// turn_order.h - our order inside the team for the next round.
// The engine alternates the teams champion by champion and swaps the starting team every round. set_priority only
// orders our own champions (higher first, ties keep the draft order) and takes effect from the next round. Acting
// before the enemies is worth something only when an enemy who would otherwise act first can change what happens to
// us. Each champion asks for its priority at the end of every turn (channelling turns included; the bait recall
// started at the end of the turn is not a channel, it is cancelled next turn), and the engine's sort gives the early
// slots to the champions that ask most, with no message between them. play_turn asks after its last action, before
// the spare-fuel users drain the budget.
// Levels: 3 a tribute that ends at our next turn start (it banks before
// the enemies can stun, lock or kill us), 2 a recall that does (home before they act), 1 danger, or under half health
// with an enemy within 5, 0 the rest.
#pragma once
namespace torder {


static int channel_rem() {
    try { return (int)bc::self().remaining_channel_time(); } catch (const std::exception&) { return -1; }
}

static int level() {
    CTR_UP = false;
    const auto st = bc::self().state();
    const bool trib = st.contains(bc::champion_state::submitting_gems), rec = st.contains(bc::champion_state::recalling);
    const int rem = trib || rec ? channel_rem() : -1;
    if (trib && rem >= 0 && rem <= 1) return 3;
    if (rec && rem >= 0 && rem <= 1) return 2;
    bool near = false;
    for (const auto& e : ENEMY) if (e.vis && e.alive && cheb(e.p, ME.p) <= 5) { near = true; break; }
    // a healthy champion in a standoff goes after the enemies of the round
    int b = near ? -1 : 0;
    if (DANGER || (near && ME.hp < ME.mhp * 0.5f)) b = 1;
    const int c = ctr_level(b);
    if (c > b) { CTR_UP = true; }
    return c;
}

static int prio_set = 0;
static int DONE_R = -1;    // the round of our last update (main.cpp asks once a turn)
static void update() {
    DONE_R = rnd();
    auto& me = bc::self();
    if (!me.is_alive()) return;
    const int want = level();
    if (want != prio_set) {
        try { me.set_priority(want); prio_set = OWN_SET = want; } catch (const std::exception&) {}
    }
}

}  // namespace torder
