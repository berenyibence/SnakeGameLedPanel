// MVisualAppConfig.h
// -----------------------------------------------------------------------------
// Central place for ALL tweakable MVisualApp (music visualizer) variables (layout,
// timing, noise behavior, palette behavior).
//
// Pattern (per README):
// - `Games/MVisual/MVisualApp.h` contains logic and references these values.
// - Visual tables live in `MVisualAppSprites.h`, included inside this namespace.
// -----------------------------------------------------------------------------
#pragma once

#include <Arduino.h>
#include "../../engine/config.h"

namespace MVisualAppConfig {

// -----------------------------------------------------------------------------
// Layout
// -----------------------------------------------------------------------------
static constexpr int HUD_H = 8; // common HUD height across "modern applets"

// -----------------------------------------------------------------------------
// Timing / input feel
// -----------------------------------------------------------------------------
static constexpr uint16_t UPDATE_INTERVAL_MS = 33; // ~30Hz simulation for smooth bars

// D-pad Up/Down repeat behavior for changing the number of bars.
static constexpr uint16_t DPAD_REPEAT_DELAY_MS = 320;
static constexpr uint16_t DPAD_REPEAT_INTERVAL_MS = 120;

// -----------------------------------------------------------------------------
// Bars
// -----------------------------------------------------------------------------
static constexpr uint8_t BAR_COUNT_MIN = 1;
static constexpr uint8_t BAR_COUNT_MAX = 64;
static constexpr uint8_t DEFAULT_BAR_COUNT = 16;

// -----------------------------------------------------------------------------
// Noise generator shaping (placeholder until mic FFT is wired in)
// -----------------------------------------------------------------------------
// How quickly previous levels decay (0..1). Closer to 1 => more persistence.
static constexpr float NOISE_DECAY = 0.88f;
// Post-processing smoothing strength across neighboring "frequency bins" (0..1).
static constexpr float NOISE_SMOOTH = 0.35f;
// Random impulse gain (0..1). Higher => more peaks.
static constexpr float NOISE_IMPULSE_GAIN = 1.0f;

// -----------------------------------------------------------------------------
// Select/Back button mapping (Bluepad32 fallback)
// -----------------------------------------------------------------------------
// Bluepad32 exposes controller "miscButtons()" on many builds/controllers.
// This repo already uses START as 0x04 (see `SnakeGameLedPanel.ino`).
//
// For SELECT/BACK, the exact bit can vary by controller mapping. 0x02 is a
// common convention. If your controller differs, tweak this constant.
static constexpr uint16_t MISC_SELECT_MASK = 0x02;

// -----------------------------------------------------------------------------
// Visual tables / palettes
// -----------------------------------------------------------------------------
#include "MVisualAppSprites.h"

} // namespace MVisualAppConfig


