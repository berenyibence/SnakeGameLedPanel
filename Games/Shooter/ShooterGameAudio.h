// ShooterGameAudio.h
// -----------------------------------------------------------------------------
// Tiny buzzer SFX set for ShooterGame using the engine AudioManager.
//
// Design goals:
// - Short, distinct, and non-annoying (buzzer-friendly).
// - No per-frame spam: ShooterGame throttles calls with cooldown timers.
// - Keep patterns <= 8 steps (AudioManager internal limit).
// -----------------------------------------------------------------------------
#pragma once

#include "../../engine/AudioManager.h"

namespace ShooterGameAudio {

using Step = AudioManager::Step;

// -----------------------------------------------------------------------------
// Music (RTTTL)
// -----------------------------------------------------------------------------
// Short intro sting to play when the game starts (requested):
// - Not looping
// - Only the first phrase (so it doesn't run long / distract)
static constexpr const char* MUSIC_THEME_RTTTL =
    "Star Wars:o=6,d=8,b=180,b=180:f5,f5,f5,2a#5.,2f.";

// Player shoot: short "pew" (two quick chirps).
static const Step SFX_SHOOT[] = {
    { 2600, 10 }, { 0, 4 },
    { 2100, 12 }
};
static constexpr uint8_t SFX_SHOOT_N = (uint8_t)(sizeof(SFX_SHOOT) / sizeof(SFX_SHOOT[0]));

// Rocket launch: slightly lower + longer so it reads as a heavier weapon.
static const Step SFX_ROCKET[] = {
    { 1600, 14 }, { 0, 6 },
    { 1200, 24 }
};
static constexpr uint8_t SFX_ROCKET_N = (uint8_t)(sizeof(SFX_ROCKET) / sizeof(SFX_ROCKET[0]));

// Enemy killed: short pop.
static const Step SFX_ENEMY_KILL[] = {
    { 900, 10 }, { 0, 4 },
    { 1200, 12 }
};
static constexpr uint8_t SFX_ENEMY_KILL_N = (uint8_t)(sizeof(SFX_ENEMY_KILL) / sizeof(SFX_ENEMY_KILL[0]));

// Boss death: longer descending alert.
static const Step SFX_BOSS_DEATH[] = {
    { 980, 60 }, { 0, 18 },
    { 740, 70 }, { 0, 18 },
    { 520, 120 }
};
static constexpr uint8_t SFX_BOSS_DEATH_N = (uint8_t)(sizeof(SFX_BOSS_DEATH) / sizeof(SFX_BOSS_DEATH[0]));

// Player hit (lose a life): harsh-ish, short descending.
static const Step SFX_PLAYER_HIT[] = {
    { 1400, 30 }, { 0, 10 },
    { 820, 50 }
};
static constexpr uint8_t SFX_PLAYER_HIT_N = (uint8_t)(sizeof(SFX_PLAYER_HIT) / sizeof(SFX_PLAYER_HIT[0]));

// Powerup pickup (distinct per type).
static const Step SFX_PICKUP_BLUE[]   = { { 1760, 14 }, { 0, 6 }, { 1960, 16 } };
static const Step SFX_PICKUP_RED[]    = { { 1320, 12 }, { 0, 4 }, { 1760, 18 } };
static const Step SFX_PICKUP_GREEN[]  = { { 1040, 12 }, { 0, 4 }, { 1560, 22 } };
static const Step SFX_PICKUP_PURPLE[] = { { 980, 12 },  { 0, 4 }, { 1240, 12 }, { 0, 4 }, { 1560, 14 } };
static const Step SFX_PICKUP_YELLOW[] = { { 1960, 10 }, { 0, 4 }, { 2340, 12 } };
static const Step SFX_PICKUP_CYAN[]   = { { 1560, 10 }, { 0, 4 }, { 2080, 12 } };
static const Step SFX_PICKUP_WHITE[]  = { { 1760, 12 }, { 0, 4 }, { 1960, 12 }, { 0, 4 }, { 2340, 18 } };

static constexpr uint8_t SFX_PICKUP_BLUE_N   = (uint8_t)(sizeof(SFX_PICKUP_BLUE) / sizeof(SFX_PICKUP_BLUE[0]));
static constexpr uint8_t SFX_PICKUP_RED_N    = (uint8_t)(sizeof(SFX_PICKUP_RED) / sizeof(SFX_PICKUP_RED[0]));
static constexpr uint8_t SFX_PICKUP_GREEN_N  = (uint8_t)(sizeof(SFX_PICKUP_GREEN) / sizeof(SFX_PICKUP_GREEN[0]));
static constexpr uint8_t SFX_PICKUP_PURPLE_N = (uint8_t)(sizeof(SFX_PICKUP_PURPLE) / sizeof(SFX_PICKUP_PURPLE[0]));
static constexpr uint8_t SFX_PICKUP_YELLOW_N = (uint8_t)(sizeof(SFX_PICKUP_YELLOW) / sizeof(SFX_PICKUP_YELLOW[0]));
static constexpr uint8_t SFX_PICKUP_CYAN_N   = (uint8_t)(sizeof(SFX_PICKUP_CYAN) / sizeof(SFX_PICKUP_CYAN[0]));
static constexpr uint8_t SFX_PICKUP_WHITE_N  = (uint8_t)(sizeof(SFX_PICKUP_WHITE) / sizeof(SFX_PICKUP_WHITE[0]));

} // namespace ShooterGameAudio


