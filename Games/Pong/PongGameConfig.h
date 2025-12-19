// PongGameConfig.h
// -----------------------------------------------------------------------------
// Central place for ALL tweakable Pong gameplay variables (speeds, timings, AI).
//
// Pattern (per README):
// - `Games/Pong/PongGame.h` contains logic and references these values.
// - Visual tables live in `PongGameSprites.h`, included inside the namespace.
// -----------------------------------------------------------------------------
#pragma once

#include <Arduino.h>

namespace PongGameConfig {

// Main logic tick (~60fps).
static constexpr uint16_t UPDATE_INTERVAL_MS = 16;

// Ball
static constexpr int BALL_SIZE_PX = 2;     // drawn size (minimum 2x2 as requested)
static constexpr float BALL_HALF = 1.0f;   // half-size for collision checks (center-based)
static inline constexpr float ballStartSpeed() { return 0.95f; } // slower start speed
static inline constexpr float ballMaxSpeed() { return 1.35f; }   // cap to keep playable on 64x64

// Player input
static constexpr float PLAYER_SPEED = 2.4f;    // px per tick at full stick
static constexpr float STICK_DEADZONE = 0.18f; // 0..1
static constexpr int16_t AXIS_DIVISOR = 512;   // Bluepad32 commonly ~[-512..512]

// CPU difficulty (intentionally beatable)
static constexpr uint16_t AI_THINK_MS = 70; // reaction delay
static constexpr float AI_SPEED = 1.4f;     // slower than player
static constexpr int AI_ERROR_PX = 7;       // aim error range (+/-)

// Round flow
static constexpr uint16_t POINT_FLASH_MS = 450;
static constexpr uint16_t COUNTDOWN_MS = 3000;

// Visual tables (currently empty placeholder)
#include "PongGameSprites.h"

} // namespace PongGameConfig


