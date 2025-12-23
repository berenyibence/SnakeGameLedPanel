#pragma once
#include <Arduino.h>
#include "../../engine/config.h"

// NOTE: struct (not namespace) so it can be used via `using Cfg = MinesweeperConfig;`
struct MinesweeperConfig {

static constexpr int HUD_H = 8;
static constexpr int CELL = 4;
static constexpr int W = 16;
static constexpr int H = 16; // uses full 64px height; top HUD overlays

static constexpr uint8_t MINES = 40;

};


