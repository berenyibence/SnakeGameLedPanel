// SnakeGameConfig.h
// -----------------------------------------------------------------------------
// Central place for ALL tweakable Snake gameplay variables (layout, timings, UI).
//
// Pattern (per README):
// - `Games/Snake/SnakeGame.h` contains logic and references these values.
// - Sprite tables live in `SnakeGameSprites.h`, included inside the namespace.
//
// ESP32/Arduino notes:
// - Prefer `static constexpr` for compile-time constants (no RAM).
// -----------------------------------------------------------------------------
#pragma once

#include <Arduino.h>
#include "../../engine/config.h"

namespace SnakeGameConfig {

// -----------------------------------------------------------------------------
// HUD / rendering
// -----------------------------------------------------------------------------
static constexpr int HUD_HEIGHT = 8;  // Space reserved at top for score/player info
static constexpr int PIXEL_SIZE = 2;  // 2x2 pixels per logical cell (snake/food grid)

// -----------------------------------------------------------------------------
// Playfield layout
// -----------------------------------------------------------------------------
// To avoid edge-pixel artifacts on some HUB75 panels, keep the whole playfield
// (border + sprites) inset from physical panel edges.
static constexpr int PLAYFIELD_BORDER_INSET_X = 1;      // leaves x=0 unused
static constexpr int PLAYFIELD_BORDER_INSET_Y = 1;      // 1px gap below HUD
static constexpr int PLAYFIELD_BORDER_INSET_BOTTOM = 1; // leaves y=63 unused

// Border rectangle (in pixels)
static constexpr int PLAYFIELD_BORDER_X = PLAYFIELD_BORDER_INSET_X;
static constexpr int PLAYFIELD_BORDER_Y = HUD_HEIGHT + PLAYFIELD_BORDER_INSET_Y;
static constexpr int PLAYFIELD_BORDER_W = PANEL_RES_X - (PLAYFIELD_BORDER_INSET_X * 2);
static constexpr int PLAYFIELD_BORDER_H = (PANEL_RES_Y - PLAYFIELD_BORDER_Y) - PLAYFIELD_BORDER_INSET_BOTTOM;

// Content area is inside the border (1px thickness)
static constexpr int PLAYFIELD_CONTENT_X = PLAYFIELD_BORDER_X + 1;
static constexpr int PLAYFIELD_CONTENT_Y = PLAYFIELD_BORDER_Y + 1;
static constexpr int PLAYFIELD_CONTENT_W = PLAYFIELD_BORDER_W - 2;
static constexpr int PLAYFIELD_CONTENT_H = PLAYFIELD_BORDER_H - 2;

// Logical game grid dimensions (in game cells, not pixels).
// NOTE: Must evenly divide by PIXEL_SIZE.
static constexpr int LOGICAL_WIDTH = (PLAYFIELD_CONTENT_W / PIXEL_SIZE);
static constexpr int LOGICAL_HEIGHT = (PLAYFIELD_CONTENT_H / PIXEL_SIZE);

// -----------------------------------------------------------------------------
// Input
// -----------------------------------------------------------------------------
static constexpr float STICK_DEADZONE = 0.22f;
static constexpr int16_t AXIS_DIVISOR = 512;

// -----------------------------------------------------------------------------
// Timings
// -----------------------------------------------------------------------------
static constexpr uint16_t COUNTDOWN_MS = 3000;
static constexpr uint16_t DEATH_BLINK_TOTAL_MS = 900;
static constexpr uint16_t DEATH_BLINK_PERIOD_MS = 120;

// Snake movement tick (ms). This is currently a global project knob in `config.h`.
static constexpr uint16_t MOVE_TICK_MS = (uint16_t)SNAKE_SPEED_MS;

// -----------------------------------------------------------------------------
// Gameplay limits (fixed pools; no heap allocations)
// -----------------------------------------------------------------------------
static constexpr uint8_t MAX_SNAKES = (uint8_t)MAX_GAMEPADS;
static constexpr uint8_t MAX_FOODS = 3;

// Max snake length in cells. Using full grid gives worst-case memory but ensures
// no overflow even if someone fills the entire board.
static constexpr uint16_t MAX_SNAKE_LEN = (uint16_t)(LOGICAL_WIDTH * LOGICAL_HEIGHT);

// -----------------------------------------------------------------------------
// Food tuning
// -----------------------------------------------------------------------------
// Food kind selection weights (0..99 roll):
// Apples are common; creatures are rarer and expire.
static constexpr uint8_t FOOD_WEIGHT_APPLE = 68;
static constexpr uint8_t FOOD_WEIGHT_MOUSE = 10;
static constexpr uint8_t FOOD_WEIGHT_FROG  = 8;
static constexpr uint8_t FOOD_WEIGHT_BIRD  = 6;
static constexpr uint8_t FOOD_WEIGHT_FISH  = 5;
static constexpr uint8_t FOOD_WEIGHT_BUG   = 3;

static constexpr uint32_t CREATURE_TTL_MS = 9000UL;

// -----------------------------------------------------------------------------
// Sprites / tables
// -----------------------------------------------------------------------------
#include "SnakeGameSprites.h"

} // namespace SnakeGameConfig


