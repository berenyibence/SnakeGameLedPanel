// TetrisGameConfig.h
// -----------------------------------------------------------------------------
// Central place for ALL tweakable Tetris game variables (timings, layout, pools).
//
// Pattern (per README):
// - `Games/Tetris/TetrisGame.h` contains logic and references these values.
// - Table data (pieces + palette) lives in `TetrisGameSprites.h`.
// -----------------------------------------------------------------------------
#pragma once

#include <Arduino.h>

namespace TetrisGameConfig {

// -----------------------------------------------------------------------------
// Board / rendering
// -----------------------------------------------------------------------------
static constexpr int BOARD_WIDTH = 10;
static constexpr int BOARD_HEIGHT = 20;

// Classic look: 3×3 pixel cells for the board.
static constexpr int CELL_SIZE = 3;

// -----------------------------------------------------------------------------
// Timing (ms)
// -----------------------------------------------------------------------------
static constexpr unsigned long INITIAL_FALL_DELAY_MS = 500;
static constexpr unsigned long FLASH_TOGGLE_MS = 90; // 6 toggles => 3 visible flashes

// -----------------------------------------------------------------------------
// Sprites / palettes
// -----------------------------------------------------------------------------
// Included inside namespace (do NOT redeclare namespace inside the sprite header).
#include "TetrisGameSprites.h"

} // namespace TetrisGameConfig


