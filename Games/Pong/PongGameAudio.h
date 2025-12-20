#pragma once
#include <Arduino.h>
#include "../../engine/AudioManager.h"

/**
 * PongGameAudio
 * -------------
 * Very small buzzer SFX palette for Pong.
 *
 * Design goals:
 * - Non-blocking: driven by `globalAudio.update()` in the main loop
 * - Minimal / recognizable: distinct pitch + short durations
 * - Safe: AudioManager already respects Settings.soundEnabled + volume
 *
 * Notes:
 * - Arrays are `static` to keep internal linkage (safe in headers on Arduino).
 * - Rests are represented as freqHz=0 steps.
 */
namespace PongGameAudio {
  // Paddle hit: short, bright blip.
  static const AudioManager::Step SFX_PADDLE_HIT[] = {
    { 1850, 10 }
  };

  // Wall hit: slightly lower blip.
  static const AudioManager::Step SFX_WALL_HIT[] = {
    { 980, 10 }
  };

  // Point scored: two-step "goal" chirp.
  static const AudioManager::Step SFX_SCORE[] = {
    { 660, 22 }, { 0, 8 },
    { 1320, 26 }
  };

  // Game over: descending "boo" (still short, so it doesn't get annoying).
  static const AudioManager::Step SFX_GAME_OVER[] = {
    { 880, 70 }, { 0, 20 },
    { 660, 70 }, { 0, 20 },
    { 440, 140 }
  };
}


