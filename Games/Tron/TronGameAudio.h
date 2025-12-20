#pragma once
#include <Arduino.h>
#include "../../engine/AudioManager.h"

/**
 * TronGameAudio
 * -------------
 * Minimal buzzer SFX palette for Tron / Light-Cycles.
 *
 * Goals:
 * - Distinct, short sounds (turn, crash, round win, game over)
 * - Non-blocking: driven by `globalAudio.update()` from the host loop
 * - Header-only safe: `static` arrays keep internal linkage
 */
namespace TronGameAudio {
  // Turn: crisp blip.
  static const AudioManager::Step SFX_TURN[] = {
    { 1560, 10 }
  };

  // Crash: short descending chirp.
  static const AudioManager::Step SFX_CRASH[] = {
    { 1200, 22 }, { 0, 6 },
    { 720, 30 }
  };

  // Round win: two rising tones.
  static const AudioManager::Step SFX_ROUND_WIN[] = {
    { 880, 24 }, { 0, 8 },
    { 1320, 26 }
  };

  // Match/game over: longer descending "final".
  static const AudioManager::Step SFX_GAME_OVER[] = {
    { 990, 70 }, { 0, 18 },
    { 660, 70 }, { 0, 18 },
    { 440, 140 }
  };
}


