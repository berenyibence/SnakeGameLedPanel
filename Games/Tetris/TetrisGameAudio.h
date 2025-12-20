#pragma once
#include <Arduino.h>

/**
 * TetrisGameAudio
 * ---------------
 * Minimal RTTTL song assets for Tetris.
 *
 * Notes:
 * - RTTTL playback is handled by `engine/AudioManager` (non-blocking).
 * - On ESP32, `const char[]` literals are stored in flash and are readable via normal pointers.
 */
namespace TetrisGameAudio {
  // "Starting song" (user-provided RTTTL).
  static const char MUSIC_START_RTTTL[] =
      "korobyeyniki:d=4,o=5,b=160:e6,8b,8c6,8d6,16e6,16d6,8c6,8b,a,8a,8c6,e6,8d6,8c6,b,8b,8c6,d6,e6,c6,a,2a,8p,d6,8f6,a6,8g6,8f6,e6,8e6,8c6,e6,8d6,8c6,b,8b,8c6,d6,e6,c6,a,a";
}


