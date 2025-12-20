// MVisualAppSprites.h
// -----------------------------------------------------------------------------
// Visual tables (palettes / counts) for the MVisualApp visualizer.
//
// Kept separate from gameplay/config so art/color tweaks do not touch logic.
// Included inside `namespace MVisualAppConfig` from `MVisualAppConfig.h`.
// -----------------------------------------------------------------------------
#pragma once

#include <Arduino.h>
#include "../../engine/config.h"

// NOTE: This file is included inside `namespace MVisualAppConfig { ... }`.

// Base colors for "mono + vertical gradient" mode (cycled via A button).
static constexpr uint16_t MONO_BASE_COLORS[] = {
    COLOR_RED,
    COLOR_GREEN,
    COLOR_BLUE,
    COLOR_CYAN,
    COLOR_MAGENTA,
    COLOR_YELLOW,
    COLOR_WHITE,
    COLOR_ORANGE,
    COLOR_PURPLE
};

static constexpr uint8_t MONO_COLOR_COUNT =
    (uint8_t)(sizeof(MONO_BASE_COLORS) / sizeof(MONO_BASE_COLORS[0]));

// How many distinct rainbow styles we support (cycled via B button).
// Effects:
// 0: Stationary per-bar rainbow (each bar keeps its color)
// 1: Blue->Red vertical gradient (only visible where the bar has pixels)
// 2: Moving horizontal rainbow (kept from earlier)
static constexpr uint8_t RAINBOW_EFFECT_COUNT = 3;


