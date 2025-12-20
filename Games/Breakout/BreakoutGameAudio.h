// BreakoutGameAudio.h
// -----------------------------------------------------------------------------
// Tiny buzzer SFX set for BreakoutGame using the engine AudioManager.
//
// Design goals:
// - Short, distinct, buzzer-friendly.
// - Non-blocking (AudioManager patterns).
// - Cooldown-throttled by BreakoutGame to avoid spam.
// - Keep patterns <= 8 steps (AudioManager internal limit).
// -----------------------------------------------------------------------------
#pragma once

#include "../../engine/AudioManager.h"

namespace BreakoutGameAudio {

using Step = AudioManager::Step;

// -----------------------------------------------------------------------------
// Music (RTTTL) - short intro sting (not looping)
// -----------------------------------------------------------------------------
static constexpr const char* MUSIC_INTRO_RTTTL =
    "Breakout:o=6,d=16,b=200:c,8g,8c7";

// -----------------------------------------------------------------------------
// SFX patterns
// -----------------------------------------------------------------------------
static const Step SFX_LAUNCH[] = {
    { 1800, 10 }, { 0, 4 },
    { 2400, 14 }
};
static constexpr uint8_t SFX_LAUNCH_N = (uint8_t)(sizeof(SFX_LAUNCH) / sizeof(SFX_LAUNCH[0]));

static const Step SFX_PADDLE_HIT[] = {
    { 1560, 10 }
};
static constexpr uint8_t SFX_PADDLE_HIT_N = (uint8_t)(sizeof(SFX_PADDLE_HIT) / sizeof(SFX_PADDLE_HIT[0]));

static const Step SFX_BRICK_HIT[] = {
    { 1240, 10 }
};
static constexpr uint8_t SFX_BRICK_HIT_N = (uint8_t)(sizeof(SFX_BRICK_HIT) / sizeof(SFX_BRICK_HIT[0]));

static const Step SFX_BRICK_BREAK[] = {
    { 900, 10 }, { 0, 4 },
    { 1200, 12 }
};
static constexpr uint8_t SFX_BRICK_BREAK_N = (uint8_t)(sizeof(SFX_BRICK_BREAK) / sizeof(SFX_BRICK_BREAK[0]));

static const Step SFX_LIFE_LOST[] = {
    { 1200, 24 }, { 0, 10 },
    { 720, 60 }
};
static constexpr uint8_t SFX_LIFE_LOST_N = (uint8_t)(sizeof(SFX_LIFE_LOST) / sizeof(SFX_LIFE_LOST[0]));

static const Step SFX_SHIELD_BOUNCE[] = {
    { 1760, 12 }, { 0, 6 },
    { 1960, 16 }
};
static constexpr uint8_t SFX_SHIELD_BOUNCE_N = (uint8_t)(sizeof(SFX_SHIELD_BOUNCE) / sizeof(SFX_SHIELD_BOUNCE[0]));

static const Step SFX_ALL_CLEAR[] = {
    { 1560, 14 }, { 0, 6 },
    { 1960, 14 }, { 0, 6 },
    { 2340, 26 }
};
static constexpr uint8_t SFX_ALL_CLEAR_N = (uint8_t)(sizeof(SFX_ALL_CLEAR) / sizeof(SFX_ALL_CLEAR[0]));

static const Step SFX_GAME_OVER[] = {
    { 980, 50 }, { 0, 20 },
    { 740, 70 }, { 0, 20 },
    { 520, 120 }
};
static constexpr uint8_t SFX_GAME_OVER_N = (uint8_t)(sizeof(SFX_GAME_OVER) / sizeof(SFX_GAME_OVER[0]));

// Powerup pickup (type-specific)
static const Step SFX_PICKUP_RED[]    = { { 1320, 12 }, { 0, 4 }, { 1760, 18 } };
static const Step SFX_PICKUP_BLUE[]   = { { 1760, 14 }, { 0, 6 }, { 1960, 16 } };
static const Step SFX_PICKUP_GREEN[]  = { { 1040, 12 }, { 0, 4 }, { 1560, 22 } };
static const Step SFX_PICKUP_PURPLE[] = { { 980, 12 },  { 0, 4 }, { 1240, 12 }, { 0, 4 }, { 1560, 14 } };
static const Step SFX_PICKUP_CYAN[]   = { { 1560, 10 }, { 0, 4 }, { 2080, 12 } };

static constexpr uint8_t SFX_PICKUP_RED_N    = (uint8_t)(sizeof(SFX_PICKUP_RED) / sizeof(SFX_PICKUP_RED[0]));
static constexpr uint8_t SFX_PICKUP_BLUE_N   = (uint8_t)(sizeof(SFX_PICKUP_BLUE) / sizeof(SFX_PICKUP_BLUE[0]));
static constexpr uint8_t SFX_PICKUP_GREEN_N  = (uint8_t)(sizeof(SFX_PICKUP_GREEN) / sizeof(SFX_PICKUP_GREEN[0]));
static constexpr uint8_t SFX_PICKUP_PURPLE_N = (uint8_t)(sizeof(SFX_PICKUP_PURPLE) / sizeof(SFX_PICKUP_PURPLE[0]));
static constexpr uint8_t SFX_PICKUP_CYAN_N   = (uint8_t)(sizeof(SFX_PICKUP_CYAN) / sizeof(SFX_PICKUP_CYAN[0]));

} // namespace BreakoutGameAudio


