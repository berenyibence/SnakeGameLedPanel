#pragma once
#include <Arduino.h>
#include "../../engine/config.h"

/**
 * SimonGameConfig
 * ---------------
 * Tweakable timing and UI constants for the Simon Says style game.
 */
namespace SimonGameConfig {

// Maximum sequence length we store.
static constexpr uint16_t MAX_SEQUENCE = 64;

// General timings (milliseconds). These are further scaled by Settings.gameSpeed.
static constexpr uint16_t READY_MS = 700;
static constexpr uint16_t BETWEEN_ROUNDS_MS = 350;

// Show-phase timings (per symbol), per difficulty (Easy/Medium/Hard).
static constexpr uint16_t SHOW_ON_MS[3]  = { 360, 300, 240 };
static constexpr uint16_t SHOW_OFF_MS[3] = { 140, 120, 100 };

// Input feedback flash
static constexpr uint16_t PRESS_FLASH_MS = 110;

// Tone durations
static constexpr uint16_t TONE_SHOW_MS  = 140;
static constexpr uint16_t TONE_PRESS_MS = 90;

// UI layout (pixel coordinates for 64x64)
// Face buttons cluster on the right: Y(top), X(left), B(right), A(bottom)
static constexpr int FACE_R = 7;       // circle radius
static constexpr int FACE_GAP = 4;     // gap between center and neighbor centers

// Diamond layout around this center.
static constexpr int FACE_CX = 40;
static constexpr int FACE_CY = 34;

// LB/RB bars at top corners
static constexpr int SHOULDER_W = 16;
static constexpr int SHOULDER_H = 7;
static constexpr int SHOULDER_Y = 10;
static constexpr int SHOULDER_X_PAD = 2;

// D-pad indicators as edge bands (4px thick)
static constexpr int DPAD_BAND = 4;

// Hearts (lives) in the middle
static constexpr int HEART_W = 5;
static constexpr int HEART_H = 5;
static constexpr int HEART_GAP = 2;

// Colors (RGB565) - we use existing palette + a couple distinct ones.
static constexpr uint16_t COL_A  = COLOR_GREEN;
static constexpr uint16_t COL_B  = COLOR_RED;
static constexpr uint16_t COL_X  = COLOR_BLUE;
static constexpr uint16_t COL_Y  = COLOR_YELLOW;
static constexpr uint16_t COL_LB = COLOR_PURPLE;
static constexpr uint16_t COL_RB = COLOR_ORANGE;
static constexpr uint16_t COL_DPAD = COLOR_WHITE;

} // namespace SimonGameConfig


