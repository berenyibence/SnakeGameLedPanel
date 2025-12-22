#pragma once
#include <Arduino.h>

/**
 * SimonGameAudio
 * --------------
 * Frequency mapping (Hz) for Simon symbols.
 *
 * Notes:
 * - Keep tones distinct and buzzer-friendly.
 * - Durations are driven by game logic; this header only provides frequencies.
 */
namespace SimonGameAudio {

// Face buttons (Xbox-ish colors in UI: A=green, B=red, X=blue, Y=yellow)
static constexpr uint16_t TONE_A  = 440;  // A4
static constexpr uint16_t TONE_B  = 523;  // C5
static constexpr uint16_t TONE_X  = 659;  // E5
static constexpr uint16_t TONE_Y  = 784;  // G5

// Shoulders
static constexpr uint16_t TONE_LB = 988;  // B5
static constexpr uint16_t TONE_RB = 1175; // D6

// D-pad
static constexpr uint16_t TONE_UP    = 1319; // E6
static constexpr uint16_t TONE_DOWN  = 392;  // G4
static constexpr uint16_t TONE_LEFT  = 330;  // E4
static constexpr uint16_t TONE_RIGHT = 880;  // A5

// Feedback
static constexpr uint16_t TONE_FAIL  = 220;  // A3

} // namespace SimonGameAudio


