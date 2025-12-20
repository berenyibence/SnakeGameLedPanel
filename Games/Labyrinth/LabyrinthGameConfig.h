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

// Timing
// - Player has 1 minute per level.
// - When the exit is reached, we briefly show a "LEVEL COMPLETE" transition,
//   then reset the clock back to 1 minute and generate the next level.
static constexpr uint32_t LEVEL_TIME_MS = 60UL * 1000UL;
// Clear animation duration (random pixel fade-out) for the LABYRINTH ONLY (HUD is excluded).
// Tweak this freely; 1000ms feels snappy.
static constexpr uint16_t LEVEL_CLEAR_ANIM_MS = 1000;
// Fade-in animation duration (random pixel reveal) for the next level (LABYRINTH ONLY).
static constexpr uint16_t LEVEL_FADEIN_ANIM_MS = 1000;
static constexpr uint16_t LEVEL_COMPLETE_TEXT_MS = 750;

// Update tick
static constexpr uint16_t UPDATE_INTERVAL_MS = 16;

// Maze memory limits
static constexpr int MAX_MAZE_W = PANEL_RES_X; // 64
static constexpr int MAX_MAZE_H = PANEL_RES_Y; // 64 (actual use is below HUD)

// Input / movement feel
static constexpr int16_t AXIS_DIVISOR = 512;   // Bluepad32 commonly ~[-512..512]
// Integer-only input smoothing / friction:
// - Deadzone in raw axis units (avoid floats entirely).
// - Smoothing and friction are rational factors.
static constexpr int16_t STICK_DEADZONE_RAW = 92;   // ~= 0.18 * 512
static constexpr uint8_t VEL_SMOOTH_NUM = 22;       // ~= 0.22
static constexpr uint8_t VEL_SMOOTH_DEN = 100;
static constexpr uint8_t STOP_FRICTION_NUM = 85;   // ~= 0.85
static constexpr uint8_t STOP_FRICTION_DEN = 100;

// Level -> cell size scaling (tile size in pixels)
static inline constexpr uint8_t tileSizeForLevel(int lvl) {
    // NOTE: Keep this as a single return expression so it compiles cleanly even
    // under older constexpr rules (C++11-style constexpr restrictions).
    return (lvl > 20) ? (uint8_t)1 : (lvl > 10) ? (uint8_t)2 : (uint8_t)4;
}

#include "LabyrinthGameSprites.h"

} // namespace LabyrinthGameConfig


