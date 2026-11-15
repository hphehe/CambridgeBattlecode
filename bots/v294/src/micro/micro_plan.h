#pragma once

// ---- plans
struct Plan {
    vec2 dash{-1, -1}, walk{-1, -1}, end{-1, -1};
    int atk = 0;              // 0 none, 1 from the start tile, 2 from the dash tile (dash, hit, walk), 3 from the end tile
    bool walk_first = false;  // walk, then dash from the walked-to tile (the landing goes off on the end tile)
    int cast = -1;            // -1 none, 0/1 spell slot, 2 attack reset, 3 the dash
    bool cast_first = false;  // the spell goes before walking (from the start tile)
    float score = -1e30f;
    float val = 0;            // what it hits (attack and cast values)
    int atk_target = -1, cast_target = -1, reset_target = -1;
    vec2 cast_tile{-1, -1};
    float dash_damage = 0.f;
};

// Small, fixed-size state for one candidate turn. EN_N is capped at six, so the rollout never allocates and its
// cost is bounded independently of map size. Linked Cassi/Issac deaths are resolved in the scratch state too.
struct SeqState { float hp[6]; bool stun[6]; };
struct SeqAction { float val = 0.f; int target = -1; vec2 tile{-1, -1}; };
