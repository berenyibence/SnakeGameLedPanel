// TronGameConfig.h
// -----------------------------------------------------------------------------
// Central place for ALL tweakable Tron gameplay variables (layout, timing, rules).
//
// Pattern (per README):
// - `Games/Tron/TronGame.h` contains logic and references these values.
// - Visual tables live in `TronGameSprites.h`, included inside the namespace.
// -----------------------------------------------------------------------------
#pragma once

#include <Arduino.h>
#include "../../engine/config.h"

namespace TronGameConfig {

// -----------------------------------------------------------------------------
// Layout (match Snake's "inset border + HUD" concept)
// -----------------------------------------------------------------------------
static constexpr int HUD_H = 8;                 // reserve top for HUD
static constexpr int CELL_PX = 1;               // 1px wide trails (classic Tron look)
static constexpr int BORDER_INSET_X = 1;        // avoid edge pixels
static constexpr int BORDER_INSET_Y = 1;        // 1px gap below HUD
static constexpr int BORDER_INSET_BOTTOM = 1;   // avoid last row

// Border rectangle (in pixels)
static constexpr int BORDER_X = BORDER_INSET_X;
static constexpr int BORDER_Y = HUD_H + BORDER_INSET_Y;
static constexpr int BORDER_W = PANEL_RES_X - (BORDER_INSET_X * 2);
static constexpr int BORDER_H = (PANEL_RES_Y - BORDER_Y) - BORDER_INSET_BOTTOM;

// Content area inside border (1px thickness)
static constexpr int CONTENT_X = BORDER_X + 1;
static constexpr int CONTENT_Y = BORDER_Y + 1;
static constexpr int CONTENT_W = BORDER_W - 2;
static constexpr int CONTENT_H = BORDER_H - 2;

// Logical grid (cells)
static constexpr int GRID_W = (CONTENT_W / CELL_PX);
static constexpr int GRID_H = (CONTENT_H / CELL_PX);

// -----------------------------------------------------------------------------
// Game rules / pacing
// -----------------------------------------------------------------------------
static constexpr uint8_t WIN_SCORE = 5;
static constexpr uint32_t ROUND_RESET_DELAY_MS = 1200;

// -----------------------------------------------------------------------------
// Visual tables / sprites
// -----------------------------------------------------------------------------
#include "TronGameSprites.h"

} // namespace TronGameConfig


