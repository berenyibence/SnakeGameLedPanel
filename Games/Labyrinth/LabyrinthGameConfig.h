// LabyrinthGameConfig.h
// -----------------------------------------------------------------------------
// Central place for ALL tweakable Labyrinth gameplay variables (layout, input, timing).
//
// Pattern (per README):
// - `Games/Labyrinth/LabyrinthGame.h` contains logic and references these values.
// - Visual tables live in `LabyrinthGameSprites.h`, included inside the namespace.
// -----------------------------------------------------------------------------
#pragma once

#include <Arduino.h>

namespace LabyrinthGameConfig {

// HUD
static constexpr int HUD_H = 8;

// Update tick
static constexpr uint16_t UPDATE_INTERVAL_MS = 16;

// Maze memory limits
static constexpr int MAX_MAZE_W = PANEL_RES_X; // 64
static constexpr int MAX_MAZE_H = PANEL_RES_Y; // 64 (actual use is below HUD)

// Input / movement feel
static constexpr float STICK_DEADZONE = 0.18f; // 0..1
static constexpr float VEL_SMOOTH = 0.22f;     // 0..1 (higher = snappier)
static constexpr float STOP_FRICTION = 0.85f;  // per tick when no input
static constexpr int16_t AXIS_DIVISOR = 512;   // Bluepad32 commonly ~[-512..512]

// Level -> cell size scaling (tile size in pixels)
static inline constexpr uint8_t tileSizeForLevel(int lvl) {
    // NOTE: Keep this as a single return expression so it compiles cleanly even
    // under older constexpr rules (C++11-style constexpr restrictions).
    return (lvl > 20) ? (uint8_t)1 : (lvl > 10) ? (uint8_t)2 : (uint8_t)4;
}

#include "LabyrinthGameSprites.h"

} // namespace LabyrinthGameConfig


