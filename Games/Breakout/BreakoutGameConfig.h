// BreakoutGameConfig.h
// -----------------------------------------------------------------------------
// Central place for ALL tweakable BreakoutGame gameplay variables.
//
// Pattern (per README):
// - `Breakout/BreakoutGame.h` contains logic and references these values.
// - Visual tables live in `BreakoutGameSprites.h`, included inside the namespace.
//
// ESP32/Arduino notes:
// - Prefer `static constexpr` (no RAM usage).
// -----------------------------------------------------------------------------
#pragma once

#include <Arduino.h>
#include "../../config.h"

namespace BreakoutGameConfig {

// -----------------------------------------------------------------------------
// Input / tick
// -----------------------------------------------------------------------------
static constexpr float STICK_DEADZONE = 0.18f;
static constexpr int16_t AXIS_DIVISOR = 512;
static constexpr uint16_t UPDATE_INTERVAL_MS = 16; // ~60 FPS logic tick

// HUD band at the top (avoid drawing bricks under the HUD).
static constexpr int HUD_H = 8;

// Countdown
static constexpr uint16_t COUNTDOWN_MS = 1800;

// -----------------------------------------------------------------------------
// Paddles
// -----------------------------------------------------------------------------
static constexpr int PADDLE_H = 1;
static constexpr int PADDLE_STACK_SPACING_Y = 3;
static constexpr int PADDLE_BASE_Y = PANEL_RES_Y - 4; // leaves room for lives dots + shield

// -----------------------------------------------------------------------------
// Bricks
// -----------------------------------------------------------------------------
static constexpr int BRICK_COLS = 12;
static constexpr int BRICK_WIDTH = 4;
static constexpr int BRICK_HEIGHT = 2;
static constexpr int BRICK_SPACING = 1;

// -----------------------------------------------------------------------------
// Ball
// -----------------------------------------------------------------------------
static constexpr int BALL_SIZE_PX = 2;
static inline constexpr float BALL_HALF = 1.0f; // 2x2 render => half-size ~1px

// -----------------------------------------------------------------------------
// Pools (avoid heap churn on ESP32)
// -----------------------------------------------------------------------------
static constexpr int MAX_BALLS = 8;
static constexpr int MAX_BRICKS = 240;
static constexpr int MAX_POWERUPS = 10;
static constexpr int MAX_PARTICLES = 90;

// -----------------------------------------------------------------------------
// Powerups
// -----------------------------------------------------------------------------
// 0=red,1=blue,2=green,3=purple,4=cyan (logic uses this mapping)
static constexpr int POWERUP_SIZE_PX = 2;

// Include all sprite/visual tables (no namespace wrapper inside that header!)
#include "BreakoutGameSprites.h"

} // namespace BreakoutGameConfig


