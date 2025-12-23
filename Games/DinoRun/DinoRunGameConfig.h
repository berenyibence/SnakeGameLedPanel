#pragma once
#include <Arduino.h>
#include "../../engine/config.h"

// NOTE: struct (not namespace) so it can be used via `using Cfg = DinoRunConfig;`
struct DinoRunConfig {

static constexpr int HUD_H = 8;

static constexpr int GROUND_Y = 54; // baseline in pixels
static constexpr int DINO_X = 10;
static constexpr int DINO_W = 10;
static constexpr int DINO_H = 10;

static constexpr float GRAVITY = 0.85f;
static constexpr float JUMP_VY = -10.5f;
static constexpr float MAX_FALL_VY = 12.0f;

// Gameplay pacing (tuned for playability on the 64x64 panel).
// The game logic is normalized to a ~60fps timestep (see DinoRunGame::update()).
static constexpr float BASE_SPEED_PX = 0.35f;          // pixels per "60fps frame"
static constexpr float MAX_SPEED_BONUS_PX = 0.75f;     // extra speed at long distances
static constexpr float SPEEDUP_PER_PX = 0.00020f;      // +speed per traveled pixel (very gradual)

// Obstacle spacing (pixels). Larger gaps = easier.
static constexpr uint16_t OBSTACLE_MIN_GAP = 42;
static constexpr uint16_t OBSTACLE_MAX_GAP = 84;

// Parallax factors
static constexpr uint8_t LAYER_COUNT = 3;
// NOTE: Use a constexpr function instead of a static constexpr array to avoid
// undefined-reference/linker issues on some Arduino toolchains (C++11 ODR rules).
static constexpr float layerSpeed(uint8_t i) {
    return (i == 0) ? 0.35f : (i == 1) ? 0.60f : 1.0f;
}

};


