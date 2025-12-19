// SnakeGameSprites.h
// -----------------------------------------------------------------------------
// All Snake sprite/visual tables live here.
//
// NOTE:
// - This header is intended to be included from inside `namespace SnakeGameConfig`
//   (see SnakeGameConfig.h). Do NOT declare `namespace SnakeGameConfig {}` again.
// -----------------------------------------------------------------------------
#pragma once

// Keep this header self-contained for IDE parsing/linting.
#include <Arduino.h>

// 4x4 pixel-art sprites for foods/creatures, indexed by FoodKind (0..5).
// 1 = draw pixel, 0 = transparent.
static inline constexpr uint8_t FOOD_SPRITE_4X4[6][4][4] = {
    // APPLE (hollow-ish)
    {{0,1,1,0},
     {1,0,0,1},
     {1,0,0,1},
     {0,1,1,0}},
    // MOUSE
    {{1,0,0,1},
     {0,1,1,0},
     {1,0,0,1},
     {0,1,1,0}},
    // FROG
    {{0,1,1,0},
     {1,0,0,1},
     {0,1,1,0},
     {1,0,0,1}},
    // BIRD
    {{0,1,1,0},
     {1,0,0,1},
     {0,1,1,0},
     {0,1,1,0}},
    // FISH
    {{1,0,1,0},
     {0,1,0,1},
     {1,1,1,0},
     {0,1,0,1}},
    // BUG
    {{0,1,1,0},
     {1,1,1,1},
     {1,0,0,1},
     {0,1,1,0}},
};


