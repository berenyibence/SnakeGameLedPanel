// ShooterGameConfig.h
// -----------------------------------------------------------------------------
// Central place for ALL tweakable ShooterGame gameplay variables.
//
// How to use:
// - Include this header in `ShooterGame.h`
// - Replace "magic numbers" / `static constexpr` tuning knobs with values from
//   `ShooterGameConfig`.
//
// Notes for ESP32 / Arduino:
// - Use `static constexpr` for compile-time constants (no RAM cost).
// - Keep pools small: these values impact memory usage and performance.
// - Speeds in this project are generally "pixels per tick" where tick ~= 16ms
//   (see `UPDATE_INTERVAL_MS`), unless explicitly stated otherwise.
// -----------------------------------------------------------------------------
#pragma once

#include <Arduino.h>
#include "../../config.h"

namespace ShooterGameConfig {

// -----------------------------------------------------------------------------
// Global timing / phases
// -----------------------------------------------------------------------------
// Main update tick interval. Lower = smoother but more CPU. Keep ~16ms for ~60fps.
static constexpr uint16_t UPDATE_INTERVAL_MS = 16;

// Round start countdown duration.
static constexpr uint16_t COUNTDOWN_MS = 3000;

// On final death: freeze action briefly before leaderboard/game-over screen.
static constexpr uint16_t GAME_OVER_FREEZE_MS = 3000;

// -----------------------------------------------------------------------------
// Input feel (analog smoothing)
// -----------------------------------------------------------------------------
static constexpr int16_t AXIS_DIVISOR = 512;    // Bluepad32 axis normalization target
static constexpr float STICK_DEADZONE = 0.18f;  // 0..1
static constexpr float MOVE_SMOOTH = 0.22f;     // 0..1, higher = more smoothing
static constexpr uint16_t TRIGGER_THRESHOLD = 360;

// -----------------------------------------------------------------------------
// Entity sizes (gameplay/collisions) + fixed pools
// -----------------------------------------------------------------------------
static constexpr uint8_t SHIP_W = 5;
static constexpr uint8_t SHIP_H = 5;

static constexpr uint8_t ENEMY_W = 5;
static constexpr uint8_t ENEMY_H = 5;

static constexpr uint8_t BOSS_W = 10;
static constexpr uint8_t BOSS_H = 10;

// Pools (affect RAM). Increase carefully.
static constexpr uint8_t MAX_PLAYER_BULLETS = 18;
static constexpr uint8_t MAX_ENEMY_BULLETS  = 18;
static constexpr uint8_t MAX_ENEMIES        = 20;
static constexpr uint8_t MAX_POWERUPS       = 6;
static constexpr uint8_t MAX_STAR_SHOTS     = 48;
static constexpr uint8_t MAX_ROCKETS        = 8;
static constexpr uint8_t MAX_PLAYER_ROCKETS = 2;
static constexpr uint8_t MAX_EXPLOSIONS     = 10;
static constexpr uint16_t MAX_PARTICLES     = 80;

// -----------------------------------------------------------------------------
// Player tuning
// -----------------------------------------------------------------------------
// Starting and maximum player lives. (Green powerup clamps to this max.)
static constexpr uint8_t PLAYER_START_LIVES = 1;
static constexpr uint8_t PLAYER_MAX_LIVES = 5;

// Player movement speed at full stick (px per tick).
static constexpr float PLAYER_SPEED = 1.6f;

// Player drift physics:
// When there is no movement input (thrusters disengaged), the ship keeps drifting and
// slowly loses speed by this multiplier each tick.
// - Closer to 1.0 => more drift (more "spacey")
// - Smaller => stops faster
static constexpr float PLAYER_DRIFT_DRAG = 0.9f;

// Below this absolute velocity, snap to 0 to avoid endless micro-drift / jitter.
static constexpr float PLAYER_DRIFT_STOP_EPS = 0.02f;

// Fire rate control
static constexpr uint16_t PLAYER_SHOT_COOLDOWN_MS = 300;

// Bullet speeds (pixels per tick).
// - Player bullets remain fast and readable (vertical shots).
// - Enemy bullets are slower (requested 2x slower) and will be aimed at the player at fire time.
static constexpr float PLAYER_BULLET_SPEED = 2.0f;
static constexpr float ENEMY_BULLET_SPEED = 0.5f; // 2x slower vs old 1px/tick

// Thruster FX (ships) - simplified:
// A fixed 1px-wide tail that always comes out the "back" of the sprite.
// - Player ship faces UP => tail goes DOWN.
// - Enemies/boss face DOWN => tail goes UP.
// Color is CYAN and fades from bright (near the ship) to transparent (far away).
static constexpr uint8_t THRUSTER_LEN_PX = 10;
// Minimum brightness to draw. Below this, pixels are skipped (transparent).
static constexpr uint8_t THRUSTER_MIN_MUL = 6;

// Enemy rocket lifetime (boss guided rockets). Prevents unavoidable "forever" rockets.
static constexpr uint16_t ENEMY_ROCKET_LIFE_MS = 2000;

// Player guided rockets (purple powerup)
static constexpr uint8_t PLAYER_MAX_ROCKET_AMMO = 2;
static constexpr uint16_t PLAYER_ROCKET_COOLDOWN_MS = 350;

// -----------------------------------------------------------------------------
// Progression: boss pacing (hits_until_boss model)
// -----------------------------------------------------------------------------
// "Hit" means 1 HP of damage applied to normal enemies (not bosses).
// Level 1 requires 10 hits; after each boss kill level increments and requirement
// increases by 1:
//   hitsUntilBoss(level) = 9 + level
static constexpr uint16_t BOSS_HITS_BASE = 10;  // level 1 => 10

// -----------------------------------------------------------------------------
// Spawning / difficulty pacing
// -----------------------------------------------------------------------------
// How often we attempt to spawn a new enemy while spawning is allowed.
// Lower = faster spawns.
static constexpr uint16_t ENEMY_SPAWN_INTERVAL_MS = 450;

// -----------------------------------------------------------------------------
// Enemy behavior tuning
// -----------------------------------------------------------------------------
// Enemies fly in from above and can exit the screen downward.
// When overlapping player ship, they deal damage (shield tier if available, else life).
//
// Enemy fire timing:
// interval = max(ENEMY_FIRE_MIN_MS, (ENEMY_FIRE_BASE_MS - level*dec) / divider)
static constexpr uint16_t ENEMY_FIRE_BASE_MS = 12000;
static constexpr uint16_t ENEMY_FIRE_MIN_MS  = 1400;
static constexpr uint16_t ENEMY_FIRE_JITTER_MS = 800;
static constexpr uint16_t ENEMY_FIRE_DEC_PER_LEVEL = 520;
static constexpr uint8_t  ENEMY_FIRE_RATE_DIVIDER = 2; // 2 => ~2x more often

// Chance to actually fire when timer triggers (0..1).
// p = min(P_MAX, P_BASE + P_PER_LEVEL*(level-1))
static constexpr float ENEMY_FIRE_P_BASE = 0.5f;
static constexpr float ENEMY_FIRE_P_PER_LEVEL = 0.025f;
static constexpr float ENEMY_FIRE_P_MAX = 0.45f;

// -----------------------------------------------------------------------------
// Powerups: drop rates + physics (Breakout-style floaty packs)
// -----------------------------------------------------------------------------
// Powerup drop chance per kill, in percent.
// (The current implementation uses an integer roll against this.)
static constexpr uint8_t POWERUP_DROP_CHANCE_PERCENT = 40;

// Powerup physics (floaty / bouncy packs)
static constexpr float POWERUP_GRAVITY = 0.012f;
static constexpr float POWERUP_DRAG = 0.984f;
static constexpr float POWERUP_BOUNCE = 0.78f;
static constexpr uint8_t POWERUP_SIZE_PX = 2; // drawn as 2x2 box

// -----------------------------------------------------------------------------
// Boss tuning
// -----------------------------------------------------------------------------
// Boss stops in the top half at HUD_H + BOSS_STOP_Y_OFFSET.
static constexpr uint8_t BOSS_STOP_Y_OFFSET = 14;

// Boss attacks
static constexpr uint16_t BOSS_STAR_BASE_MS = 2600;
static constexpr uint16_t BOSS_ROCKET_BASE_MS = 3800;
static constexpr uint16_t BOSS_ATTACK_DEC_PER_BOSS = 140;
static constexpr uint16_t BOSS_ATTACK_DEC_MAX = 1600;

// Disable boss rockets for the first N bosses.
static constexpr uint8_t BOSSES_WITHOUT_ROCKETS = 5;

// Boss death sequence timing
static constexpr uint16_t BOSS_DEATH_EXPLOSION_MS = 2000; // huge explosion duration
static constexpr uint16_t BOSS_LOOT_GRACE_MS = 1000;      // pause after loot to pick up

// Player death explosion (final death): show a big explosion animation, then game over screen.
static constexpr uint16_t PLAYER_DEATH_EXPLOSION_MS = 3000;

// Player death AoE blast:
// When the player dies (final life), enemies within this radius are hit once.
// This is applied immediately at death start (so it's deterministic and fair).
static constexpr uint8_t PLAYER_DEATH_AOE_DAMAGE = 5;
static constexpr uint8_t PLAYER_DEATH_AOE_RADIUS_PX = 22; // matches the max visual ring radius

// -----------------------------------------------------------------------------
// Background clouds (parallax)
// -----------------------------------------------------------------------------
// Two layers:
// - Layer 0 (far): slower and dimmer
// - Layer 1 (near): faster and brighter
//
// Movement is downward to simulate forward motion.
static constexpr uint8_t CLOUD_SPRITE_MAX_W = 10;
static constexpr uint8_t CLOUD_SPRITE_MAX_H = 10;
static constexpr uint8_t CLOUD_SPRITE_COUNT = 6;

static constexpr uint8_t CLOUD_LAYER0_COUNT = 6;
static constexpr uint8_t CLOUD_LAYER1_COUNT = 6;

// Exaggerated parallax (requested):
// - Layer 0 (bottom/far): uses smaller clouds (0..2), very dim, noticeably slower.
// - Layer 1 (top/near): uses bigger clouds (3..5), brighter, noticeably faster.
static constexpr uint8_t CLOUD_LAYER0_SPRITE_MIN = 0;
static constexpr uint8_t CLOUD_LAYER0_SPRITE_MAX = 2; // inclusive
static constexpr uint8_t CLOUD_LAYER1_SPRITE_MIN = 3;
static constexpr uint8_t CLOUD_LAYER1_SPRITE_MAX = 5; // inclusive

static constexpr float CLOUD_LAYER0_VY = 0.03f;
static constexpr float CLOUD_LAYER1_VY = 0.18f;
static constexpr float CLOUD_LAYER0_VX_JITTER = 0.010f;
static constexpr float CLOUD_LAYER1_VX_JITTER = 0.055f;

static constexpr uint8_t CLOUD_LAYER0_MUL = 14;  // very dim
// Foreground clouds should still read as "near" but must not distract from gameplay.
// Tune this down if clouds compete with enemies/bullets.
static constexpr uint8_t CLOUD_LAYER1_MUL = 50; // even dimmer near layer (requested)

// Sprite tables (clouds + ships + bosses) live in a separate header for easier editing.
// NOTE: Included here so users can tune visuals in one place without touching game logic.
#include "ShooterGameSprites.h"

} // namespace ShooterGameConfig


