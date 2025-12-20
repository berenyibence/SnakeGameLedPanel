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
#include "../../engine/config.h"

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
// Audio (buzzer) - SFX throttling
// -----------------------------------------------------------------------------
// Cooldowns (ms) to prevent "sound spam" on frequent actions.
static constexpr uint16_t SFX_LAUNCH_COOLDOWN_MS      = 120;
static constexpr uint16_t SFX_PADDLE_HIT_COOLDOWN_MS  = 70;
static constexpr uint16_t SFX_BRICK_HIT_COOLDOWN_MS   = 55;
static constexpr uint16_t SFX_BRICK_BREAK_COOLDOWN_MS = 80;
static constexpr uint16_t SFX_PICKUP_COOLDOWN_MS      = 160;
static constexpr uint16_t SFX_SHIELD_COOLDOWN_MS      = 220;
static constexpr uint16_t SFX_LIFE_LOST_COOLDOWN_MS   = 300;
static constexpr uint16_t SFX_ALL_CLEAR_COOLDOWN_MS   = 1000;
static constexpr uint16_t SFX_GAME_OVER_COOLDOWN_MS   = 1400;

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

// IMPORTANT:
// - Ball speed should NOT scale with "level".
// - Difficulty is controlled via brick HP / scroll / spawn pacing instead.
// - Values are in "pixels per logic tick" (tick ~60 FPS by default).
static constexpr float BALL_SPEED = 0.95f;
static constexpr float BALL_SHOT_MULT = 1.35f;  // green powerup / straight-shot launch boost
static constexpr float BALL_MAX_SPEED = 1.65f;  // absolute clamp to prevent runaway spikes

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


