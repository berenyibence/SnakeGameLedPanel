#pragma once
#include <Arduino.h>
#include "../../engine/config.h"

// NOTE:
// This is a struct (not a namespace) so it can be used via a type alias (e.g. `using Cfg = BomberManGameConfig;`)
// inside game classes. Arduino's build system can be picky with namespace-alias patterns across translation units.
struct BomberManGameConfig {

// Display / grid
static constexpr int HUD_H = 8;
static constexpr int CELL = 4;                // 4x4 pixels per tile
static constexpr int GRID_W = PANEL_RES_X / CELL;                 // 16
static constexpr int GRID_H = (PANEL_RES_Y - HUD_H) / CELL;       // 14
static constexpr int ORIGIN_X = 0;
static constexpr int ORIGIN_Y = HUD_H;

// Timing
static constexpr uint32_t TICK_MS = 40;        // ~25Hz logic
static constexpr uint32_t BOMB_FUSE_MS = 1800;
static constexpr uint32_t EXPLOSION_MS = 450;
static constexpr uint32_t BREAK_MS = 260;

// Entities
static constexpr uint8_t MAX_PLAYERS = MAX_GAMEPADS;
static constexpr uint8_t MAX_BOMBS = 12;
static constexpr uint8_t MAX_ENEMIES = 10;
static constexpr uint8_t MAX_PICKUPS = 16;

// Gameplay defaults
static constexpr uint8_t START_BOMBS = 1;      // capacity
static constexpr uint8_t START_RANGE = 2;
static constexpr uint8_t START_SPEED = 3;      // px per tick step scale
static constexpr uint8_t MAX_RANGE = 8;
static constexpr uint8_t MAX_SPEED = 7;

// Level generation
static constexpr uint8_t WALL_SOLID = 1;
static constexpr uint8_t WALL_BRICK = 2;

// Powerup chances under bricks (percent)
static constexpr uint8_t CHANCE_POWERUP = 24;
static constexpr uint8_t CHANCE_GATE = 6;      // picked once; gate forced to exist

// Scoring
static constexpr uint16_t SCORE_KILL_ENEMY = 50;
static constexpr uint16_t SCORE_BREAK_BRICK = 5;
static constexpr uint16_t SCORE_LEVEL_CLEAR = 200;

// Colors
static constexpr uint16_t COL_SOLID = COLOR_WHITE;
static constexpr uint16_t COL_BRICK = COLOR_ORANGE;
static constexpr uint16_t COL_FLOOR = COLOR_BLACK;
static constexpr uint16_t COL_EXPLO1 = COLOR_YELLOW;
static constexpr uint16_t COL_EXPLO2 = COLOR_ORANGE;
static constexpr uint16_t COL_GATE_LOCKED = COLOR_BLUE;
static constexpr uint16_t COL_GATE_OPEN = COLOR_GREEN;
static constexpr uint16_t COL_ENEMY = COLOR_MAGENTA;

};


