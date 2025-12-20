#pragma once
#include <Arduino.h>
#include "config.h"

/**
 * AudioManager
 * ------------
 * Minimal, non-blocking buzzer audio service for ESP32.
 *
 * Purpose (phase 1):
 * - Provide a reliable "UI navigation tick" sound when moving in menus/lists.
 * - Be safe: never block with delay(), and respect Settings.soundEnabled.
 *
 * Notes:
 * - This class is intentionally tiny for the first integration.
 * - We use ESP32 LEDC (PWM) tone output. The PWM channel/pin are configured in `engine/config.h`.
 * - We keep the pin attached and stop tones by setting frequency to 0.
 */
class AudioManager {
public:
    AudioManager() = default;

    /**
     * Initialize the buzzer output.
     * Safe to call multiple times.
     */
    void begin();

    /**
     * Drive scheduled tone end (non-blocking).
     * Call once per loop from the host (SnakeGameLedPanel.ino).
     */
    void update();

    /**
     * Immediately silence the buzzer.
     */
    void stopAll();

    /**
     * Play a tone for a fixed duration (non-blocking).
     * If sound is disabled in settings, this is a no-op.
     */
    void playTone(uint16_t freqHz, uint16_t durationMs);

    /**
     * Standard UI sound: a short "tick" used for list/menu navigation.
     */
    void uiNavigateTick();

    // -----------------------------------------------------
    // UI sound effects (menus / lists)
    // -----------------------------------------------------
    // These are intentionally simple and distinct so they are recognizable on a buzzer.
    void uiUp();
    void uiDown();
    void uiLeft();
    void uiRight();
    void uiConfirmShoot(); // A button "pew" / shoot-like confirm
    void uiStartStop();    // START button "STOP!" alert pattern

    /**
     * Play a short multi-step pattern (non-blocking).
     * Use `freqHz=0` steps for silent rests.
     */
    struct Step {
        uint16_t freqHz;
        uint16_t durationMs;
    };
    void playPattern(const Step* steps, uint8_t stepCount);

    // -----------------------------------------------------
    // RTTTL (Nokia ringtone) playback
    // -----------------------------------------------------
    /**
     * Play an RTTTL ringtone string (monophonic).
     *
     * Format: "name:d=4,o=5,b=140:notes"
     * Notes: [duration][note][#][octave][.][,]
     * Example: "nokia:d=4,o=5,b=140:e6,d6,8f#6,8g#6,c#6,8d6,8e6,8b,8p, ..."
     *
     * - Non-blocking: driven by `update()`
     * - If UI SFX plays while a ringtone is active, the ringtone resumes after the SFX ends.
     */
    void playRtttl(const char* rtttl, bool loop = true);
    void stopRtttl();
    bool isRtttlActive() const { return rtttlActive; }

private:
    bool initialized = false;
    bool playing = false;
    uint32_t toneEndMs = 0;

    // Very small in-memory pattern buffer.
    static constexpr uint8_t MAX_STEPS = 8;
    bool patternActive = false;
    uint8_t patternCount = 0;
    uint8_t patternIndex = 0;
    Step pattern[MAX_STEPS] = {};

    // RTTTL player state (kept separate so UI SFX can interrupt and ringtone resumes).
    bool rtttlActive = false;
    bool rtttlLoop = true;
    const char* rtttlStr = nullptr;   // full RTTTL string
    const char* rtttlPos = nullptr;   // current parsing position in notes section
    uint16_t rtttlDefaultDur = 4;
    uint8_t rtttlDefaultOct = 6;
    uint16_t rtttlBpm = 63;           // RTTTL default per spec
    uint32_t rtttlWholeNoteMs = 0;

    void ensureInit();
    bool soundAllowed() const;
    void setToneHz(uint16_t freqHz);
    void applyVolumeDuty();
    void startStep(uint8_t index);

    // RTTTL parsing / playback helpers
    static inline bool isDigit(char c) { return c >= '0' && c <= '9'; }
    static const char* skipToChar(const char* s, char ch);
    static uint16_t parseNumber(const char*& s);
    void rtttlParseHeaderAndReset(const char* rtttl);
    bool rtttlStartNext(); // returns false if no more notes
    uint16_t noteFreqHz(char note, bool sharp, uint8_t octave) const;
};

// Global service instance (defined in engine/AudioManager.cpp)
extern AudioManager globalAudio;


