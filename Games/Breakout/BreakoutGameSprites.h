// BreakoutGameSprites.h
// -----------------------------------------------------------------------------
// All Breakout sprite/visual tables live here (colors, tiny patterns).
//
// NOTE:
// - This header is intended to be included from inside `namespace BreakoutGameConfig`
//   (see BreakoutGameConfig.h). Do NOT declare `namespace BreakoutGameConfig {}` again.
// -----------------------------------------------------------------------------
#pragma once

#include "../../engine/config.h" // COLOR_* constants

// Brick shading multipliers for a simple "3D" look.
// Index 0..2 is used as: top highlight, mid, bottom shade.
static inline constexpr uint8_t BRICK_SHADE_MUL[3] = { 255, 190, 120 };

// Palette used for brick base colors (game logic still picks colors by HP/level).
// Keep these bright and varied for readability.
static inline constexpr uint16_t BRICK_BASE_PALETTE[8] = {
    COLOR_RED, COLOR_ORANGE, COLOR_YELLOW, COLOR_GREEN,
    COLOR_CYAN, COLOR_BLUE, COLOR_PURPLE, COLOR_MAGENTA
};


