// AsteroidsGameConfig.h
// -----------------------------------------------------------------------------
// Central place for ALL tweakable Asteroids gameplay variables (timings, speeds, pools).
//
// Pattern (per README):
// - `Games/Asteroids/AsteroidsGame.h` contains logic and references these values.
// - Visual tables live in `AsteroidsGameSprites.h`, included inside the namespace.
// -----------------------------------------------------------------------------
#pragma once

#include <Arduino.h>

namespace AsteroidsGameConfig {

// HUD
static constexpr int HUD_H = 8;

// Tick
static constexpr uint32_t UPDATE_INTERVAL_MS = 16; // ~60Hz logic

// Movement / input
static constexpr float MAX_SPEED = 2.6f;
static constexpr float MOVE_SMOOTH = 0.18f;    // 0..1 (higher = snappier)
static constexpr float STICK_DEADZONE = 0.18f; // 0..1
static constexpr int16_t AXIS_DIVISOR = 512;   // Bluepad32 commonly uses ~[-512..512]
static constexpr uint16_t TRIGGER_THRESHOLD = 360; // analog trigger threshold (0..1023-ish)

// Shooting
static constexpr uint32_t SHOT_COOLDOWN_MS = 180;
static constexpr uint32_t BULLET_LIFE_MS = 700;
static constexpr uint8_t MAX_BULLETS = 6;
static constexpr float BULLET_SPEED = 3.2f;

// Respawn / hyperspace
static constexpr uint32_t RESPAWN_INVULN_MS = 1500;
static constexpr uint32_t HYPERSPACE_COOLDOWN_MS = 1200;

// Asteroids pool: enough for 5 large at once + splits (each large -> 2 med -> 4 small).
// Worst-case simultaneously alive ~= 5*(1+2+4)=35. Cap to keep CPU stable.
static constexpr uint8_t MAX_ASTEROIDS = 36;

#include "AsteroidsGameSprites.h"

} // namespace AsteroidsGameConfig


