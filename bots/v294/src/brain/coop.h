// coop.h: who is worth bringing to a camp, priced for the team.
// A champion's share of a camp is what its body adds to the allies that already claim it, never less than the average:
//   share = max(1/k, (1 - psi x T(all of us) / T(the claimers alone)) / (1 + recruits))
// T is the camp engine's finish time (camp_calc.h cc_finish, trips home included); the claimers alone may never finish
// (the whole camp is then mine). psi prices a contest: with enemies about the camp, P(take | the claimers) /
// P(take | all of us), P(take) = sigmoid(0.12 + 0.91 x (our bodies - theirs)), fitted to the 60+ gem contests of 605
// ladder games (taken 16/33/51/67/90% at -2..+2 bodies). Benched against a plain 1/k (paired, 3 opponents): the
// marginal share alone lost 5 points (it gives no credit to a body that soaks or contests: the Heart of Ruin stopped
// tanking camps), the floor alone lost, floor + contest gained (section 7.2).
#pragma once
// enemies about camp g: seen within 7 of it in the last 15 rounds (the job model's "contested"), or, if none, one where
// the draft's farming race expects an enemy body at round `at`
static int coop_enemies(const Group& g, int at) {
    int n = 0;
    const JmFT& T = jm_feat_turn(rnd());
    if (T.n15 > 0) {
        const JmBox bx = jm_box(g);
        for (const JmFE& q : T.e) if (q.r15 && jm_box_d(bx, q.lp) <= 7 && tc_gd(g, q.lp) <= 7) ++n;
    }
    if (n == 0 && g.id >= 0 && pre_enemy(g.id, at)) n = 1;
    return n;
}
static inline float coop_ptake(int d) { return 1.f / (1.f + std::exp(-(0.12f + 0.91f * (float)d))); }
