// jm_coef.h: generated from the job model fit on
// ladder games, from the bot's own view.
// Do not edit: rerun the script.
#pragma once
// P(caught) on a job: logistic on (x - MU) / SD, weights intercept first. Features, in order:
// 0 walk, 1 side, 2 rnd, 3 mem_camp, 4 mem_route, 5 fought_near, 6 fought, 7 in_view, 8 bal_camp, 9 bal_view, 10 ally_camp, 11 ally_me, 12 hp, 13 gems, 14 unseen, 15 alive_them, 16 alive_us, 17 size, 18 bal_narrow, 19 switch
static constexpr float JM_PC_MU[20] = {0.430477673f, 0.383136433f, 0.54212387f, 0.50050695f, 0.0448790682f, 0.344183443f, 0.408811852f, 0.255295332f, 2.2506894f, 2.49459405f, 0.627883791f, 0.661640984f, 0.874049678f, 0.986449857f, 3.19246847f, 4.28894758f, 3.98273661f, 4.28894758f, 2.36033166f, 0.06038143f};
static constexpr float JM_PC_SD[20] = {0.734104674f, 0.145430136f, 0.37331699f, 0.887130626f, 0.268445101f, 0.738334416f, 0.790842776f, 0.616781358f, 1.5350167f, 1.24431393f, 0.87593051f, 0.894085348f, 0.192335708f, 1.41715653f, 1.68151415f, 0.812558905f, 1.01729711f, 0.812558905f, 1.4187274f, 0.480510699f};
static constexpr float JM_PC_W[21] = {-3.49667833f, 0.073081704f, 0.44658959f, 0.133704679f, 0.218877833f, 0.160869795f, 0.227454839f, -0.265762263f, 0.00380641151f, -0.207045154f, -0.0601643461f, 0.214625474f, -0.0194036856f, -0.0439651922f, -0.141934373f, -0.0240082539f, 0.0914341272f, -0.296165105f, 0.0914341272f, -0.0588284601f, 0.12581553f};
// our team takes the job camp's next capture (JM_GET), they take it (JM_THEY) = sigmoid(b0 + b1 c
// + b2 c lbal + b3 S / 16 + b4 walk / 20), c = contested (an enemy seen within 7 of it in the last 15 rounds),
// lbal = log camp Balance, S = the team switch (-16..15)
static constexpr float JM_GET[5] = {1.2402457f, -1.40427946f, 0.268450609f, -0.0301521175f, -0.427437885f};
static constexpr float JM_THEY[5] = {-3.04404895f, 1.73989881f, -0.310644828f, 0.821757176f, 0.156810484f};
// enemy deaths near a contested job camp in its window, by camp Balance < 0.5 / < 1 / < 2 / 2 and up
static constexpr float JM_PK[4] = {0.15512765f, 0.168961864f, 0.177825388f, 0.184093437f};
// death cost = carried gems + JM_DC0 (61 rounds x 0.4 farm + 10 kill gold); kill value = JM_KV (+ their usual 8 gems)
static constexpr float JM_DC0 = 34.4f, JM_KV = 42.4f;
