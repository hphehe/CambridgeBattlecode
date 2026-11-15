// qmodel.h - the learned fight choice (option-set model).
// Built from 3,302 games (top-team ladder, our ladder, local): every legal (end tile, action, target) option of a fight
// turn gets the option features of the training set. A state net, run once a turn, turns the
// state into per-option-feature weights:
//   Q heads  P(die in 6), P(an enemy within 8 dies in 6) = sigmoid(c_k(s) + o . V_k(s))
//   pi       how a top team chooses among the options = softmax(o . u(s))
// Choice: argmax  Pkill - Pdie x (1 + carried gems / 100)  among the options pi gives >= QMODEL_SUPPORT x its best (the
// model only picks among moves strong players actually make: no extrapolation to moves the data never shows).
// Weights: qweights.h (state z-scoring folded into the first layers, option z-scoring folded per turn).
#pragma once
#include <wasm_simd128.h>
#include "qweights.h"
static_assert(QM_NS == 46 && QM_DO == 46 && QM_NH == 3, "qweights.h was built for another feature set");

namespace qm {
static Kit kit_t(CT t) { return champ(t).qkit; }
static int type_ix(CT t) { return (int)t >= 0 && (int)t < CT_N ? (int)t : -1; }
static bool mel(CT t) { return is_melee(t); }
static float dps_of(CT t, float st, float fo) {
    const Kit k = kit_t(t);
    float sp = 0; bool r = false;
    for (int s = 0; s < 2; ++s) { if (k.s[s].k == KD || k.s[s].k == KA || k.s[s].k == KHD) sp += k.s[s].m; if (k.s[s].k == KR) r = true; }
    return st + sp * fo / 8.f + (r ? st / 8.f : 0.f);
}
static float horm(CT t, float h, float mh) { return champ(t).missing_hp ? 1.f + 0.5f * std::max(0.f, mh - h) / std::max(1.f, mh) : 1.f; }
static inline float th(float x) {   // tanh, Pade [7/6]
    if (x > 4.97f) return 1.f;
    if (x < -4.97f) return -1.f;
    const float x2 = x * x;
    return x * (135135.f + x2 * (17325.f + x2 * (378.f + x2))) / (135135.f + x2 * (62370.f + x2 * (3150.f + x2 * 28.f)));
}
static inline float sg(float z) { return 0.5f + 0.5f * th(0.5f * z); }   // sigmoid via the Pade tanh (no exp)

static bool INIT = false;
static Bits BUSHQ, WALLH;
static std::vector<uint32_t> STC;   // per map tile, filled lazily: bit 31 set | open<<20 | own spawn<<17 | bush<<16 | dh (16 bits)
static int SPAN = 1;
static void init() {
    if (INIT) return;
    INIT = true;
    BUSHQ = unpack(bc::map::bushes());
    WALLH = unpack(bc::map::walls() | bc::map::holes());
    STC.assign((size_t)(W * H), 0u);
    int best = UNREACH;
    for (auto s : en_spawn)
        for (auto d : DIRS) {
            const vec2 q = s + d;
            if (!inb(q)) continue;
            const int h = F_HOME.at(q);
            if (h != UNREACH) best = std::min(best, h + 1);
        }
    SPAN = best == UNREACH ? std::max(W, H) : std::max(1, best);
}
static inline int dh_of(vec2 p) { const int h = F_HOME.at(p); return std::min(300, h == UNREACH ? 999 : (int)h); }
static inline int da_of(vec2 p) {
    if (inb(p) && EN_SPAWN[idx(p)]) return 0;
    const int a = F_AWAY.at(p);
    return std::min(300, a == UNREACH ? 999 : a + 1);
}

struct KE { int ei; vec2 p; float hp, mhp, str, fo, dps; int gems; CT t; bool vis; bool rdy[2]; int d; };
struct KAl { vec2 p; float hp, mhp, dps; CT t; };
struct Opt { int8_t tw, kind, j, s; float pz, pd, pk; };
enum { ON = 0, OB, OR_, OS, OA, OT, OSH };
// the last decision's options, for scoring the legacy plan with the model (trust mode)
static const Opt* L_OP = nullptr; static int L_NO = 0; static vec2 L_P0{-1, -1}; static float L_VD = 1.f, L_THR = 0.f, L_BV = 0.f; static int L_ROUND = -1;

// the per-turn nets: state x (raw) -> Q head coefficients (option z folded) and pi weights.
// Fuel: every layer is written as an element-wise update over its outputs (vectorizes; a float reduction does not),
// and the option weights are interleaved W4[d] = {pi, die, kill, mykill} so one SIMD pass scores all heads.
static float QC[QM_NH];
// W4[d] = {pi, die, kill, mykill} weight of option feature d (option z-scoring folded in at export);
// row QM_DO holds {0, c_die, c_kill, c_mykill}
alignas(16) static float W4[QM_DO + 1][4];
// out = b + in . W (W row-major [NI][NO]). Fuel: fixed sizes, fully unrolled, all outputs held in registers per input.
template <int NI, int NO>
static inline void lay(const float* in, const float* W, const float* b, float* out) {
    constexpr int G = NO / 4;
    v128_t acc[G];
#pragma clang loop unroll(full)
    for (int g = 0; g < G; ++g) acc[g] = wasm_v128_load(b + 4 * g);
    float tl[NO % 4 + 1];
    for (int t = 0; t < NO % 4; ++t) tl[t] = b[4 * G + t];
#pragma clang loop unroll(full)
    for (int i = 0; i < NI; ++i) {
        const float xi = in[i];
        const v128_t s = wasm_f32x4_splat(xi);
        const float* w = W + i * NO;
#pragma clang loop unroll(full)
        for (int g = 0; g < G; ++g) acc[g] = wasm_f32x4_add(acc[g], wasm_f32x4_mul(s, wasm_v128_load(w + 4 * g)));
        for (int t = 0; t < NO % 4; ++t) tl[t] += xi * w[4 * G + t];
    }
#pragma clang loop unroll(full)
    for (int g = 0; g < G; ++g) wasm_v128_store(out + 4 * g, acc[g]);
    for (int t = 0; t < NO % 4; ++t) out[4 * G + t] = tl[t];
}
// tanh on 4 lanes (the Pade [7/6] of th, clamped)
static inline v128_t vth(v128_t x) {
    x = wasm_f32x4_pmin(wasm_f32x4_splat(4.97f), wasm_f32x4_pmax(wasm_f32x4_splat(-4.97f), x));
    const v128_t x2 = wasm_f32x4_mul(x, x);
    const v128_t n = wasm_f32x4_mul(x, wasm_f32x4_add(wasm_f32x4_splat(135135.f), wasm_f32x4_mul(x2, wasm_f32x4_add(wasm_f32x4_splat(17325.f), wasm_f32x4_mul(x2, wasm_f32x4_add(wasm_f32x4_splat(378.f), x2))))));
    const v128_t d = wasm_f32x4_add(wasm_f32x4_splat(135135.f), wasm_f32x4_mul(x2, wasm_f32x4_add(wasm_f32x4_splat(62370.f), wasm_f32x4_mul(x2, wasm_f32x4_add(wasm_f32x4_splat(3150.f), wasm_f32x4_mul(x2, wasm_f32x4_splat(28.f)))))));
    return wasm_f32x4_div(n, d);
}
template <int N>
static inline void vtanh(float* h, const float* add = nullptr) {
#pragma clang loop unroll(full)
    for (int g = 0; g < N / 4; ++g) {
        v128_t v = wasm_v128_load(h + 4 * g);
        if (add) v = wasm_f32x4_add(v, wasm_v128_load(add + 4 * g));
        wasm_v128_store(h + 4 * g, vth(v));
    }
}
static_assert(QM_H1 % 4 == 0 && QM_H2 % 4 == 0 && QM_QIO == 4 * (QM_DO + 1), "layer sizes");
static void nets(const float* x) {
    alignas(16) float h1[QM_H1], h2[QM_H2], pz[QM_DO];
    // Q: the flag row (our team / a top team) is added before the first tanh
    lay<QM_NS, QM_H1>(x, QM_QW1, QM_QB1, h1);
    vtanh<QM_H1>(h1, QM_QW1 + (QM_NS + (1)) * QM_H1);
    lay<QM_H1, QM_H2>(h1, QM_QW2, QM_QB2, h2);
    vtanh<QM_H2>(h2);
    lay<QM_H2, QM_QIO>(h2, QM_QIW, QM_QIB, &W4[0][0]);
    for (int k = 0; k < QM_NH; ++k) QC[k] = W4[QM_DO][1 + k];
    // pi (its constant term cancels in the softmax)
    lay<QM_NS, QM_H1>(x, QM_PW1, QM_PB1, h1);
    vtanh<QM_H1>(h1);
    lay<QM_H1, QM_H2>(h1, QM_PW2, QM_PB2, h2);
    vtanh<QM_H2>(h2);
    lay<QM_H2, QM_DO>(h2, QM_PFW, QM_PFB, pz);
    for (int d = 0; d < QM_DO; ++d) W4[d][0] = pz[d];
}
// 4-lane accumulators over the interleaved option weights
typedef v128_t V4;
static inline V4 v4_zero() { return wasm_f32x4_splat(0.f); }
static inline V4 v4_add(V4 a, V4 b) { return wasm_f32x4_add(a, b); }
static inline V4 v4_mac(V4 a, int d, float v) { return wasm_f32x4_add(a, wasm_f32x4_mul(wasm_f32x4_splat(v), wasm_v128_load(W4[d]))); }
static inline void v4_out(V4 a, float* r) { wasm_v128_store(r, a); }

// One fight turn. Returns true with the plan to execute; false = not a model turn (no known enemy within 6, fuel).
static bool decide(MicroMode mode, Plan& out, int* nopt_out = nullptr) {
    const long f0 = (long)bc::fuel_remaining();
    if (f0 < QMODEL_START_FUEL) return false;
    init();
    const vec2 p0 = MP0;
    const int r = ROUND;
    // ---- known enemies: in view, or seen within 2 rounds and 8 tiles (not on their spawn)
    KE ke[8]; int nk = 0, alive_e = 0, known_pre = 0;
    for (auto& e : ENEMY) {
        bool al = e.alive;
        if (!e.vis) { try { al = e.h.is_alive(); } catch (const std::exception&) {} }
        if (!al || (e.vis && e.hp <= 0)) continue;
        ++alive_e;
        vec2 p; bool v;
        if (e.vis) { p = e.p; v = true; }
        else if (e.last_p.x >= 0 && r - e.seen_at <= 2 && cheb(e.last_p, p0) <= 8) { p = e.last_p; v = false; }
        else continue;
        ++known_pre;
        if (inb(p) && EN_SPAWN[idx(p)]) continue;
        if (nk >= 8) continue;
        KE& k = ke[nk++];
        k.ei = -1;
        if (v) for (int i = 0; i < EN_N; ++i) if (EC_G[i] == &e) k.ei = i;
        k.p = p; k.hp = e.hp; k.mhp = std::max(1.f, e.mhp); k.str = e.str; k.fo = e.focus; k.t = e.type; k.vis = v; k.gems = e.gems;
        k.dps = dps_of(e.type, e.str, e.focus);
        if (v) read_ecd(e);
        for (int s = 0; s < 2; ++s) {
            int cd = e.ecd[s];
            if (cd > 0) cd -= r - e.ecd_at;
            k.rdy[s] = cd <= 0;
        }
        k.d = cheb(p0, p);
    }
    if (nk == 0) return false;
    for (int a = 1; a < nk; ++a) for (int b = a; b > 0 && ke[b].d < ke[b - 1].d; --b) std::swap(ke[b], ke[b - 1]);
    if (ke[0].d > 6) return false;
    KAl ka[6]; int na = 0;
    for (int s = 0; s < (int)ALLY.size(); ++s) {
        const Champ& a = ALLY[s];
        if (s == MY_SLOT || !a.vis || !a.alive || na >= 6) continue;
        ka[na++] = KAl{a.p, a.hp, std::max(1.f, a.mhp), dps_of(a.type, a.str, a.focus), a.type};
    }
    // ---- state
    float x[QM_NS] = {};
    const int ti = type_ix(ME.type);
    if (ti >= 0) x[ti] = 1.f;
    const float hp_i = ME.hp, mhp = std::max(1.f, ME.mhp), st_ = ME.str, fo = ME.focus;
    const int dh = dh_of(p0), da = da_of(p0);
    const float myd = dps_of(ME.type, st_, fo);
    float eHp4 = 0, eD4 = 0, eHp8 = 0, eD8 = 0, aHp4 = 0, aD4 = 0, minEhpf = 9, sumEg4 = 0;
    int nE2 = 0, nE4 = 0, nE6 = 0, nA2 = 0, nA4 = 0;
    for (int j = 0; j < nk; ++j) {
        const KE& k = ke[j];
        eHp8 += k.hp; eD8 += k.dps;
        if (k.d <= 4) { eHp4 += k.hp; eD4 += k.dps; sumEg4 += k.gems; }
        nE2 += k.d <= 2; nE4 += k.d <= 4; nE6 += k.d <= 6;
        minEhpf = std::min(minEhpf, k.hp / k.mhp);
    }
    for (int a = 0; a < na; ++a) {
        const int d = cheb(p0, ka[a].p);
        if (d <= 4) { aHp4 += ka[a].hp; aD4 += ka[a].dps; }
        nA2 += d <= 2; nA4 += d <= 4;
    }
    int items = 0;
    try { const auto ic = bc::self().item_count(); for (auto v : ic) items += (int)v; } catch (const std::exception&) {}
    {
        float* v = x + 13;
        v[0] = hp_i; v[1] = mhp; v[2] = hp_i / mhp; v[3] = st_; v[4] = fo; v[5] = ME.cd[0] == 0; v[6] = ME.cd[1] == 0;
        v[7] = (float)ME.gold; v[8] = (float)ME.gems; v[9] = (float)items; v[10] = r / 1000.f;
        v[11] = dh / (float)SPAN; v[12] = da / (float)SPAN; v[13] = dh / (float)std::max(1, dh + da); v[14] = BUSHQ[idx(p0)] ? 1.f : 0.f;
        v[15] = (float)ALIVE_ALLIES; v[16] = (float)alive_e; v[17] = (float)(alive_e - known_pre);
        v[18] = std::log(((hp_i + aHp4) * (myd + aD4) + 1.f) / (eHp4 * eD4 + 1.f));
        v[19] = std::log(((hp_i + aHp4) * (myd + aD4) + 1.f) / (eHp8 * eD8 + 1.f));
        v[20] = eHp4 / 1000.f; v[21] = eD4 / 100.f; v[22] = aHp4 / 1000.f; v[23] = aD4 / 100.f;
        v[24] = (float)nE2; v[25] = (float)nE4; v[26] = (float)nE6; v[27] = (float)nA2; v[28] = (float)nA4;
        v[29] = minEhpf; v[30] = (float)ke[0].gems; v[31] = sumEg4; v[32] = QM_WARD_ON ? 1.f : 0.f;
    }
    nets(x);
    // ---- my tiles: 0 stay, 1 walk, 2 dash, 3 dash + walk (DT: the dash tile)
    const Kit mk = kit_t(ME.type);
    const bool rdy[2] = {ME.cd[0] == 0 && ME.can_cast, ME.cd[1] == 0 && ME.can_cast};
    const int ds = dash_slot();
    const int rsl = reset_slot();
    int8_t MV[49]; vec2 DT[49];
    for (int w = 0; w < 49; ++w) MV[w] = -1;
    MV[wi(p0)] = 0;
    if (ME.can_move) for (auto d : DIRS) { const vec2 q = p0 + d; if (C_WALK[wi(q)]) MV[wi(q)] = 1; }
    // window offset tables (position independent): ring-2 tiles with the steps next to them, ring-3 tiles next to them
    static bool WT_OK = false;
    static uint8_t VIAM[49]; static int8_t M3L[49][8], M3N[49], WXI[49], WYI[49];
    if (!WT_OK) {
        WT_OK = true;
        for (int w = 0; w < 49; ++w) {
            WXI[w] = (int8_t)(w / 7); WYI[w] = (int8_t)(w % 7);
            const vec2 b{w / 7 - 3, w % 7 - 3};
            VIAM[w] = 0; M3N[w] = 0;
            if (cheb(b, vec2{0, 0}) != 2) continue;
            for (int i = 0; i < 8; ++i) {
                if (cheb(DIRS[i], b) == 1) VIAM[w] |= 1u << i;
                const vec2 c = b + DIRS[i];
                if (cheb(c, vec2{0, 0}) == 3) M3L[w][M3N[w]++] = (int8_t)((c.x + 3) * 7 + (c.y + 3));
            }
        }
    }
    if (ds >= 0) {
        uint8_t nbm = 0;   // the steps next to me that are not blocked (by a unit either)
        for (int i = 0; i < 8; ++i) { const vec2 a = p0 + DIRS[i]; if (inb(a) && !BLOCK[idx(a)] && (C_WALK[wi(a)])) nbm |= 1u << i; }
        for (int w = 0; w < 49; ++w)
            if ((VIAM[w] & nbm) && C_WALK[w]) { MV[w] = 2; DT[w] = vec2{p0.x + WXI[w] - 3, p0.y + WYI[w] - 3}; }
        if (ME.can_move)
            for (int w = 0; w < 49; ++w) {
                if (MV[w] != 2) continue;
                for (int i = 0; i < M3N[w]; ++i) {
                    const int c = M3L[w][i];
                    if (!C_WALK[c] || MV[c] >= 0) continue;
                    MV[c] = 3; DT[c] = DT[w];
                }
            }
    }
    const bool hd_dash = ds >= 0 && mk.s[ds].k == KHD;
    const float mult_me = horm(ME.type, hp_i, mhp);
    const int brange = attack_reach(mel(ME.type));
    int TW[49], ntw = 0;   // the window tiles I can end on: stay and walk first, then dash
    for (int w = 0; w < 49; ++w) if (MV[w] == 0 || MV[w] == 1) TW[ntw++] = w;
    const int ntw_walk = ntw;
    // dash tiles: dash only, then dash + walk, each in a spread order (w x 17 mod 49) so a fuel cut keeps every direction
    // no landing that wakes a sleeping camp (the model has no camp terms: micro_turn_state.h wake_tiles)
    for (int c = 2; c <= 3; ++c) for (int i = 0; i < 49; ++i) {
        const int w = i * 17 % 49;
        if (MV[w] == c && !(hd_dash && WAKE_M && (WAKE_M >> wi(DT[w]) & 1))) TW[ntw++] = w;
    }
    auto tq = [&](int w) { return vec2{p0.x + w / 7 - 3, p0.y + w % 7 - 3}; };
    {   // what the fuel left pays for: every walk tile (threat pass + tile features), then dash tiles with their options
        const long F = (long)bc::fuel_remaining();
        const long ct = 1100 + 100L * nk;
        if (F - (long)ntw_walk * ct < QMODEL_FUEL_MIN + 4000) return false;
        const long room = F - QMODEL_DASH_FUEL_MIN - (long)ntw_walk * ct;
        const int nd = room > 0 ? (int)(room / (ct + 2500)) : 0;
        if (ntw > ntw_walk + nd) { ntw = ntw_walk + nd; }
    }
    // ---- per enemy: its next-turn threat to those tiles (dmg, counts spells); per tile the enemy stats of the options
    const int my_nr = (mel(ME.type) ? 2 : 3) + 1;
    float TH_D[8][49]; int8_t TH_S[8][49];
    float XD[49], MN[49]; uint8_t XN[49], XS[49], DH1[49], N3[49], NR[49], NN[49], DE[49];
    for (int t = 0; t < ntw; ++t) { const int w = TW[t]; XD[w] = 0; MN[w] = 1.f; XN[w] = XS[w] = DH1[w] = N3[w] = NR[w] = NN[w] = 0; DE[w] = 99; }
    for (int j = 0; j < nk; ++j) {
        const KE& k = ke[j];
        const Kit kt = kit_t(k.t);
        bool dash = false, reset = false;
        for (int s = 0; s < 2; ++s) if (k.rdy[s]) { if (kt.s[s].k == KH || kt.s[s].k == KHD) dash = true; if (kt.s[s].k == KR) reset = true; }
        const float mult = horm(k.t, k.hp, k.mhp);
        const bool m = mel(k.t);
        const int reach = (m ? 2 : 3) + (dash ? 2 : 0);
        const float basic = k.str * (reset ? 2.f : 1.f);
        int sk[2] = {K0, K0}; float sm[2] = {0, 0};
        for (int s = 0; s < 2; ++s) if (k.rdy[s]) { sk[s] = kt.s[s].k; sm[s] = kt.s[s].m * k.fo; }
        const uint8_t bj = (uint8_t)(1u << j);
        const float hf = std::max(0.f, k.hp) / k.mhp;
        float HT[7]; int8_t HS[7];   // threat by distance (none from 6 on)
        for (int dj = 0; dj < 7; ++dj) {
            float hit = dj <= reach ? basic : 0.f;
            int ns = 0;
            for (int s = 0; s < 2; ++s) {
                if (sk[s] == KD && dj <= 4) { hit = std::max(hit, dj <= 3 + (m ? 1 : 0) ? k.str : 0.f) + sm[s]; ++ns; }
                else if (sk[s] == KA && dj <= 5) { hit += sm[s]; ++ns; }
                else if (sk[s] == KHD && dj <= 3) { hit += sm[s]; ++ns; }
            }
            HT[dj] = hit * mult; HS[dj] = (int8_t)ns;
        }
        int AX[7], AY[7];
        for (int i = 0; i < 7; ++i) { AX[i] = std::abs(k.p.x - (p0.x + i - 3)); AY[i] = std::abs(k.p.y - (p0.y + i - 3)); }
        for (int t = 0; t < ntw; ++t) {
            const int w = TW[t];
            const int dj = std::max(AX[WXI[w]], AY[WYI[w]]);
            if (dj < DE[w]) DE[w] = (uint8_t)dj;
            if (dj <= my_nr) {
                if (dj <= 1) DH1[w] |= bj;
                if (dj <= 3) N3[w] |= bj;
                NR[w] |= bj; ++NN[w]; MN[w] = std::min(MN[w], hf);
            }
            const int di = dj < 6 ? dj : 6;
            const float th = HT[di];
            TH_D[j][w] = th; TH_S[j][w] = HS[di];
            XD[w] += th; XN[w] += th > 0; XS[w] += HS[di];
        }
    }
    // ---- options
    static Opt OP[400];
    int no = 0;
    bool stop = false;
    const int e0d = ke[0].d;
    float zmax = -1e30f;
    // ---- fast scoring of the same features (the same values as feats): every tile's no-action option is
    // scored once from its tile features; an action adds its own features and corrects the tile ones it changes.
    uint8_t AM[6];   // per ally: the enemies it covers (within its reach + 1)
    for (int a = 0; a < na; ++a) {
        AM[a] = 0;
        const int rj = mel(ka[a].t) ? 2 : 3;
        for (int jj = 0; jj < nk; ++jj) if (cheb(ka[a].p, ke[jj].p) <= rj + 1) AM[a] |= 1u << jj;
    }
    struct TC { V4 b; float x19, x20, x21, x22, x23, x24, cov, nn, mn, bush, m37, x40, x43; uint8_t nr, dhit; };
    TC tc[49];
    V4 BM[4];   // the move-class part: {stay, walk, dash, dash + walk} x (o_none, tgt_hp 1, dash cast)
    BM[0] = v4_mac(v4_mac(v4_mac(v4_zero(), 0, 1.f), 10, 1.f), 14, 1.f);
    BM[1] = v4_mac(v4_mac(v4_mac(v4_zero(), 1, 1.f), 10, 1.f), 14, 1.f);
    BM[2] = v4_mac(v4_mac(v4_mac(v4_mac(v4_zero(), 2, 1.f), 10, 1.f), 14, 1.f), 36, 1.f);
    BM[3] = v4_mac(BM[2], 1, 1.f);
    const float ihp = 1.f / std::max(1.f, hp_i);
    for (int t = 0; t < ntw; ++t) {
        const int w = TW[t];
        const vec2 q = tq(w);
        TC& c = tc[w];
        if (t >= ntw_walk && (long)bc::fuel_remaining() < QMODEL_DASH_FUEL_MIN) { ntw = t; break; }
        const float xd = XD[w], mn = MN[w]; const int xn = XN[w], xs = XS[w], dE = DE[w], nn = NN[w];
        const uint8_t n3 = N3[w], nr = NR[w], dh1 = DH1[w];
        const int xa = __builtin_popcount(dh1);
        int a2 = 0, a4 = 0, cov = 0;
        for (int a = 0; a < na; ++a) { const int d = cheb(q, ka[a].p); a2 += d <= 2; a4 += d <= 4; cov += (AM[a] & n3) != 0; }
        const int qi = idx(q);
        uint32_t sf = STC[qi];
        if (!(sf >> 31)) {
            int op = 0;
            for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) { const vec2 e{q.x + dx, q.y + dy}; if (inb(e) && !WALLH[idx(e)]) ++op; }
            sf = 1u << 31 | (uint32_t)op << 20 | (OWN_SPAWN[qi] ? 1u : 0u) << 17 | (BUSHQ[qi] ? 1u : 0u) << 16 | (uint32_t)dh_of(q);
            STC[qi] = sf;
        }
        const int op = (int)(sf >> 20 & 15);
        const float x20 = xd * ihp, x21 = xd >= hp_i ? 1.f : 0.f, m3 = std::min(x20, 3.f), bush = (sf >> 16 & 1) ? 1.f : 0.f;
        c.x19 = xd / 100.f; c.x20 = x20; c.x21 = x21; c.x22 = (float)xn; c.x23 = (float)xs; c.x24 = (float)xa;
        c.cov = (float)cov; c.nn = (float)nn; c.mn = mn; c.bush = bush; c.m37 = m3 * m3; c.x40 = x21 * cov; c.x43 = bush * x20;
        c.nr = nr; c.dhit = dh1;
        V4 acc = BM[MV[w]];
        if (xd > 0) { acc = v4_mac(acc, 19, c.x19); acc = v4_mac(acc, 20, x20); acc = v4_mac(acc, 22, c.x22); acc = v4_mac(acc, 37, c.m37); }
        if (x21 != 0.f) { acc = v4_mac(acc, 21, x21); if (cov) acc = v4_mac(acc, 40, c.x40); }
        if (xs) acc = v4_mac(acc, 23, c.x23);
        if (xa) { acc = v4_mac(acc, 24, c.x24); acc = v4_mac(acc, 44, 1.f); }
        if (a4) { acc = v4_mac(acc, 25, (float)a2); acc = v4_mac(acc, 26, (float)a4); }
        if (cov) acc = v4_mac(acc, 27, c.cov);
        if (nn) acc = v4_mac(acc, 28, c.nn);
        acc = v4_mac(acc, 29, mn);
        if (bush != 0.f) { acc = v4_mac(acc, 30, bush); acc = v4_mac(acc, 43, c.x43); }
        const int dhq = std::max(-3, std::min(3, (int)(sf & 0xffff) - dh));
        if (dhq) acc = v4_mac(acc, 31, (float)dhq);
        acc = v4_mac(acc, 32, (float)dE);
        if (dE != e0d) acc = v4_mac(acc, 33, (float)(dE - e0d));
        if (sf >> 17 & 1) acc = v4_mac(acc, 34, 1.f);
        acc = v4_mac(acc, 35, op / 9.f);
        if (dE >= 5) acc = v4_mac(acc, 45, 1.f);
        c.b = acc;
    }
    // an action (kind, target, slot) and what it does, independent of the end tile; xon: also the HoR dash hit (xh)
    struct AC { V4 a; float dmg, f7, f11, shield; uint8_t hit, killed, gone; uint32_t ok; };
    auto build = [&](AC& A, int kind, int j, int s, bool xon, uint8_t xh) {
        float dmg = 0; uint8_t hit = 0; int hit0 = -1;
        if (kind == OB) { dmg = st_; hit = 1u << j; hit0 = j; }
        else if (kind == OR_) { dmg = 2 * st_; hit = 1u << j; hit0 = j; }
        else if (kind == OS) { dmg = mk.s[s].m * fo + (cheb(p0, ke[j].p) <= brange ? st_ : 0.f); hit = 1u << j; hit0 = j; }
        else if (kind == OT) { dmg = cheb(p0, ke[j].p) <= brange ? st_ : 0.f; hit = 1u << j; hit0 = j; }
        else if (kind == OA) { dmg = mk.s[s].m * fo; for (int jj = 0; jj < nk; ++jj) if (cheb(ke[jj].p, ke[j].p) <= 1) { hit |= 1u << jj; if (hit0 < 0) hit0 = jj; } }
        if (xon) {
            hit |= xh;
            dmg = std::max(dmg, mk.s[ds].m * fo);
            if (hit0 < 0) for (int jj = 0; jj < nk; ++jj) if (hit >> jj & 1) { hit0 = jj; break; }
        }
        dmg *= mult_me;
        const bool stun = kind == OT || ((kind == OS || kind == OA) && mk.s[s].stun) || xon;
        float tot = 0; int kills = 0, nh = 0; uint8_t gone = 0;
        for (int jj = 0; jj < nk; ++jj) if (hit >> jj & 1) {
            ++nh; tot += std::min(dmg, ke[jj].hp);
            if (dmg >= ke[jj].hp) { ++kills; gone |= 1u << jj; }
        }
        A.killed = gone;
        if (stun) gone |= hit;
        A.gone = gone; A.hit = hit; A.dmg = dmg;
        A.shield = kind == OSH ? mk.s[s].m * fo : 0.f;
        A.f7 = stun ? 1.f : 0.f; A.f11 = tot / 100.f;
        float f13 = (float)kills, f14 = 1.f, f15 = 0, f16 = 0, f17 = 0;
        if (hit0 >= 0) { const KE& k = ke[hit0]; f14 = std::max(0.f, k.hp - dmg) / k.mhp; f15 = k.dps / 100.f; f16 = k.gems / 10.f; f17 = k.hp / k.mhp < 0.4f; }
        V4 acc = v4_zero();
        if (kind != ON) {
            acc = v4_mac(acc, 10, -1.f);
            if (kind != OT) acc = v4_mac(acc, kind == OB ? 3 : kind == OR_ ? 4 : kind == OS ? 5 : kind == OA ? 6 : 8, 1.f);
        }
        if (A.f7 != 0.f) acc = v4_mac(acc, 7, A.f7);
        if (tot != 0.f) { acc = v4_mac(acc, 11, A.f11); acc = v4_mac(acc, 12, tot / std::max(1.f, eHp8)); }
        if (f13 != 0.f) acc = v4_mac(acc, 13, f13);
        if (f14 != 1.f) acc = v4_mac(acc, 14, f14 - 1.f);
        if (f15 != 0.f) acc = v4_mac(acc, 15, f15);
        if (f16 != 0.f) acc = v4_mac(acc, 16, f16);
        if (f17 != 0.f) acc = v4_mac(acc, 17, f17);
        if (nh) acc = v4_mac(acc, 18, (float)nh);
        if (f13 * f16 != 0.f) acc = v4_mac(acc, 38, f13 * f16);
        if (f17 * A.f11 != 0.f) acc = v4_mac(acc, 39, f17 * A.f11);
        A.a = acc;
    };
    static AC ac[7][9][3];
    static uint32_t QTURN = 0;
    ++QTURN;
    auto action = [&](int kind, int j, int s) -> const AC& {
        AC& A = ac[kind][j + 1][s + 1];
        if (A.ok != QTURN) { build(A, kind, j, s, false, 0); A.ok = QTURN; }
        return A;
    };
    auto score = [&](int w, vec2 q, const AC& A, int kind, int s, float* r4) {
        const TC& t = tc[w];
        V4 acc = v4_add(t.b, A.a);
        if (s >= 0 && MV[w] < 2) acc = v4_mac(acc, 36, 1.f);
        float f20 = t.x20;
        if (A.gone || kind == OSH) {
            float xd = 0; int xn = 0, xs = 0, xa = 0;
            if (A.gone) {   // few removed: subtract them; else sum the rest (same order as feats)
                for (int jj = 0; jj < nk; ++jj) {
                    if (A.gone >> jj & 1) continue;
                    const float hj = TH_D[jj][w];
                    xn += hj > 0; xd += hj;
                    xs += TH_S[jj][w];
                }
                xa = __builtin_popcount(t.dhit & ~A.gone);
            } else { xd = XD[w]; xn = XN[w]; xs = XS[w]; xa = __builtin_popcount(t.dhit); }
            xd = std::max(0.f, xd - A.shield);
            f20 = xd * ihp;
            const float f21 = xd >= hp_i ? 1.f : 0.f, m3 = std::min(f20, 3.f);
            acc = v4_mac(acc, 19, xd / 100.f - t.x19); acc = v4_mac(acc, 20, f20 - t.x20); acc = v4_mac(acc, 21, f21 - t.x21);
            acc = v4_mac(acc, 22, xn - t.x22); acc = v4_mac(acc, 23, xs - t.x23); acc = v4_mac(acc, 24, xa - t.x24);
            acc = v4_mac(acc, 37, m3 * m3 - t.m37); acc = v4_mac(acc, 40, f21 * t.cov - t.x40); acc = v4_mac(acc, 43, t.bush * f20 - t.x43);
        }
        if (A.f11 != 0.f) acc = v4_mac(acc, 41, A.f11 * f20);
        if (A.f7 != 0.f) acc = v4_mac(acc, 42, A.f7 * f20);
        if (A.hit & t.nr) {
            int nn = 0; float mn = 1.f;
            for (uint32_t b = t.nr & ~A.killed & 0xffu; b; b &= b - 1) {
                const int jj = __builtin_ctz(b);
                ++nn;
                mn = std::min(mn, std::max(0.f, ke[jj].hp - ((A.hit >> jj & 1) ? A.dmg : 0.f)) / ke[jj].mhp);
            }
            acc = v4_mac(acc, 28, nn - t.nn); acc = v4_mac(acc, 29, mn - t.mn);
        }
        v4_out(acc, r4);
    };
    auto add = [&](int w, vec2 q, int kind, int j, int s) {
        if (no >= 400 || stop) return;
        if ((no & 3) == 3 && (long)bc::fuel_remaining() < QMODEL_FUEL_MIN) { stop = true; return; }
        alignas(16) float r4[4];
        if (hd_dash && MV[w] >= 2) {
            static AC XC[32]; static uint32_t XK[32], XT[32];
            const uint32_t key = (uint32_t)kind << 16 | (uint32_t)(j + 1) << 8 | tc[w].dhit;
            const int h = (int)((key * 2654435761u) >> 27);
            if (XT[h] != QTURN || XK[h] != key) { build(XC[h], kind, j, s, true, tc[w].dhit); XK[h] = key; XT[h] = QTURN; }
            score(w, q, XC[h], kind, s, r4);
        }
        else score(w, q, action(kind, j, s), kind, s, r4);
        Opt& p = OP[no++];
        p.tw = (int8_t)w; p.kind = (int8_t)kind; p.j = (int8_t)j; p.s = (int8_t)s;
        p.pz = r4[0];
        p.pd = QC[0] + r4[1];
        p.pk = QC[1] + r4[2];
        zmax = std::max(zmax, p.pz);
    };
    for (int t = 0; t < ntw; ++t) {
            const int w = TW[t];
            const vec2 q = tq(w);
            const int mv = MV[w];
            const bool walked = mv == 1 || mv == 3, dashed = mv >= 2;
            add(w, q, ON, -1, -1);
            if (ME.can_attack)
                for (int j = 0; j < nk; ++j) {
                    if (ke[j].ei < 0) continue;
                    bool inr = cheb(p0, ke[j].p) <= brange || cheb(q, ke[j].p) <= brange;
                    if (!mel(ME.type) && walked) inr = false;
                    if (!inr) continue;
                    add(w, q, OB, j, -1);
                    if (rsl >= 0 && !dashed) add(w, q, OR_, j, rsl);
                }
            if (dashed) continue;
            for (int s = 0; s < 2; ++s) {
                if (!rdy[s]) continue;
                const Slot& sl = mk.s[s];
                if (sl.k == KD && !walked && SP_OK[s]) { for (int j = 0; j < nk; ++j) if (ke[j].ei >= 0 && cheb(p0, ke[j].p) <= 4) add(w, q, OS, j, s); }
                else if (sl.k == KA && !walked && SP_OK[s]) { for (int j = 0; j < nk; ++j) if (ke[j].ei >= 0 && cheb(p0, ke[j].p) <= 4) add(w, q, OA, j, s); }
                else if (sl.k == KT && SP_OK[s] && !(ME.ab[s].excl && walked)) { for (int j = 0; j < nk; ++j) if (ke[j].ei >= 0 && cheb(p0, ke[j].p) <= 4) add(w, q, OT, j, s); }
                else if (sl.k == KS && ME.ab[s].shield) add(w, q, OSH, -1, s);
            }
        }
    if (no == 0) return false;
    // ---- support, then the value
    const float thr = zmax + std::log(QMODEL_SUPPORT);
    const float vd = 1.f + ME.gems / 100.f;
    int bi = -1; float bv = -1e30f;
    uint64_t hasb = 0;   // window tiles with a legal basic attack option
    uint64_t onsup = 0;  // tiles whose "none" is in the support: their attacks inherit it
    for (int i = 0; i < no; ++i) { if (OP[i].kind == OB) hasb |= 1ull << OP[i].tw; if (OP[i].kind == ON && OP[i].pz >= thr) onsup |= 1ull << OP[i].tw; }
    for (int i = 0; i < no; ++i) {
        if (OP[i].pz < thr && !(OP[i].kind == OB && (onsup >> OP[i].tw & 1))) continue;
        if (OP[i].kind == ON && (hasb >> OP[i].tw & 1)) continue;
        const float ev = sg(OP[i].pk) - sg(OP[i].pd) * vd;
        if (ev > bv) { bv = ev; bi = i; }
    }
    if (bi < 0) return false;
    const Opt& c = OP[bi];
    const vec2 q{p0.x + c.tw / 7 - 3, p0.y + c.tw % 7 - 3};
    const int mv = MV[c.tw];
    Plan P;
    P.end = q;
    if (mv == 1) P.walk = q;
    else if (mv == 2) P.dash = q;
    else if (mv == 3) { P.dash = DT[c.tw]; P.walk = q; }
    const bool walked = mv == 1 || mv == 3, dashed = mv >= 2;
    if (dashed) { P.cast = 3; if (hd_dash) { float v; if (my_dmg(ds, v)) P.dash_damage = v; } }
    QM_NOSH = c.kind != OSH;
    auto basic_code = [&](int j) { return (cheb(p0, ke[j].p) <= brange && (mel(ME.type) || !walked)) ? 1 : 3; };
    const bool any_basic_here = ME.can_attack && basic_target(p0) != nullptr && (mel(ME.type) || !walked);
    if (c.kind == OB) { P.atk = basic_code(c.j); P.atk_target = ke[c.j].ei; }
    else if (c.kind == OSH && (hasb >> c.tw & 1)) {   // shield plus the model's best attack from the same tile
        int bj = -1; float be = -1e30f;
        for (int i = 0; i < no; ++i) if (OP[i].kind == OB && OP[i].tw == c.tw) { const float ev = sg(OP[i].pk) - sg(OP[i].pd) * vd; if (ev > be) { be = ev; bj = OP[i].j; } }
        if (bj >= 0) { P.atk = basic_code(bj); P.atk_target = ke[bj].ei; }
    }
    else if (c.kind == OR_) { P.atk = basic_code(c.j); P.atk_target = ke[c.j].ei; P.cast = 2; P.reset_target = ke[c.j].ei; }
    else if (c.kind == OS || c.kind == OA || c.kind == OT) {
        P.cast = c.s; P.cast_first = true; P.cast_target = ke[c.j].ei;
        if (c.kind == OA) P.cast_tile = ke[c.j].p;
        if (any_basic_here) { P.atk = 1; P.atk_target = cheb(p0, ke[c.j].p) <= brange ? ke[c.j].ei : -1; }
    }
    P.score = bv; P.val = sg(OP[bi].pk);
    out = P;
    if (nopt_out) *nopt_out = no;
    L_OP = OP; L_NO = no; L_P0 = p0; L_VD = vd; L_THR = thr; L_BV = bv; L_ROUND = ROUND;
    return true;
}
// the model's value of a plan (its best option at the plan's end tile of the plan's kind class), or -1e30 when not scored
static float plan_value(const Plan& L) {
    if (L_ROUND != ROUND || !L_OP || L.end.x < 0) return -1e30f;
    const int dx = L.end.x - L_P0.x, dy = L.end.y - L_P0.y;
    if (std::abs(dx) > 3 || std::abs(dy) > 3) return -1e30f;
    const int tw = (dx + 3) * 7 + (dy + 3);
    const int cls = L.atk > 0 ? (L.cast == 2 ? OR_ : OB) : (L.cast == 0 || L.cast == 1) ? OS : ON;
    float lv = -1e30f;
    for (int i = 0; i < L_NO; ++i) {
        const Opt& o = L_OP[i];
        if (o.tw != tw) continue;
        const bool m = cls == OS ? (o.kind == OS || o.kind == OA || o.kind == OT) : cls == ON ? (o.kind == ON || o.kind == OSH) : o.kind == cls;
        if (m) lv = std::max(lv, sg(o.pk) - sg(o.pd) * L_VD);
    }
    return lv;
}
// the model's value gain of its own plan over the legacy plan (after decide on this turn); -1 when the legacy plan was not scored
static float trust_gain(const Plan& L) {
    const float lv = plan_value(L);
    if (lv < -1e29f) { return -1.f; }
    return L_BV - lv;
}
}  // namespace qm
