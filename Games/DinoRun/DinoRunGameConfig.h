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

static constexpr uint16_t BASE_SPEED_PX = 2;  // scrolling speed

static constexpr uint16_t OBSTACLE_MIN_GAP = 22;
static constexpr uint16_t OBSTACLE_MAX_GAP = 44;

// Parallax factors
static constexpr uint8_t LAYER_COUNT = 3;
static constexpr float LAYER_SPEED[LAYER_COUNT] = { 0.35f, 0.60f, 1.0f };

};


