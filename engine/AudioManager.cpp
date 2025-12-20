#include "AudioManager.h"
#include "Settings.h"
#include <math.h>

// ESP32 LEDC API (Arduino-ESP32)
// We intentionally do not include esp-idf headers directly; Arduino provides LEDC helpers.

AudioManager globalAudio;

// -----------------------------
// Internal helpers
// -----------------------------
bool AudioManager::soundAllowed() const {
#if ENABLE_AUDIO
    // `globalSettings` lives in Settings.cpp and is already used widely.
    return globalSettings.isSoundEnabled();
#else
    return false;
#endif
}

void AudioManager::ensureInit() {
#if ENABLE_AUDIO
    if (initialized) return;

    #if DEBUG_AUDIO
    Serial.print(F("[Audio] init: attach pin="));
    Serial.print((int)AUDIO_BUZZER_PIN);
    Serial.print(F(" ch="));
    Serial.println((int)AUDIO_PWM_CHANNEL);
    #endif

    // Configure a base PWM setup for compatibility across Arduino-ESP32 versions.
    // `ledcWriteTone()` will override frequency as needed.
    ledcSetup(AUDIO_PWM_CHANNEL, 2000 /*Hz*/, 8 /*bits*/);

    // Attach the pin to the configured LEDC channel.
    // We attach once and keep it attached; tones are started/stopped via ledcWriteTone().
    ledcAttachPin(AUDIO_BUZZER_PIN, AUDIO_PWM_CHANNEL);
    setToneHz(0);
    initialized = true;
#endif
}

void AudioManager::setToneHz(uint16_t freqHz) {
#if ENABLE_AUDIO
    // Arduino-ESP32 convention: 0 stops the tone on that channel.
    ledcWriteTone(AUDIO_PWM_CHANNEL, (double)freqHz);
#else
    (void)freqHz;
#endif
}

void AudioManager::applyVolumeDuty() {
#if ENABLE_AUDIO
    // Volume is implemented via PWM duty.
    // Level 0 acts as "volume mute" (Sound may still be ON).
    const uint8_t vol = globalSettings.getSoundVolumeLevel(); // 0..10
    if (vol == 0) {
        ledcWrite(AUDIO_PWM_CHANNEL, 0);
        return;
    }

    // We keep the max duty conservative to avoid overdriving small buzzers directly from GPIO.
    // 8-bit resolution => duty in [0..255].
    const uint8_t dutyMin = 8;   // ~3%
    const uint8_t dutyMax = 128; // 50%
    const uint8_t duty = (uint8_t)map((int)vol, 1, 10, (int)dutyMin, (int)dutyMax);
    ledcWrite(AUDIO_PWM_CHANNEL, duty);
#endif
}

const char* AudioManager::skipToChar(const char* s, char ch) {
    if (!s) return nullptr;
    while (*s && *s != ch) s++;
    return (*s == ch) ? s : nullptr;
}

uint16_t AudioManager::parseNumber(const char*& s) {
    uint16_t v = 0;
    while (s && isDigit(*s)) {
        v = (uint16_t)(v * 10 + (uint16_t)(*s - '0'));
        s++;
    }
    return v;
}

void AudioManager::rtttlParseHeaderAndReset(const char* rtttl) {
    // Defaults per RTTTL conventions.
    rtttlDefaultDur = 4;
    rtttlDefaultOct = 6;
    // RTTTL default BPM is 63 (per common RTTTL spec).
    rtttlBpm = 63;
    rtttlWholeNoteMs = 0;

    rtttlStr = rtttl;
    rtttlPos = nullptr;

    if (!rtttl) return;

    // Format: name:d=4,o=5,b=140:notes
    // We jump to the first ':' (end of name), then parse defaults until next ':'.
    const char* p = skipToChar(rtttl, ':');
    if (!p) return;
    p++; // after name ':'

    const char* end = skipToChar(p, ':');
    if (!end) return;

    // Parse defaults section: comma-separated tags like d=4,o=6,b=140 (whitespace allowed).
    while (p < end && *p) {
        while (p < end && (*p == ' ' || *p == ',')) p++;
        if (p >= end) break;

        const char tag = (char)tolower((unsigned char)*p);
        if ((tag == 'd' || tag == 'o' || tag == 'b') && p + 1 < end && p[1] == '=') {
            p += 2;
            const uint16_t v = parseNumber(p);
            if (tag == 'd' && v != 0) rtttlDefaultDur = v;
            if (tag == 'o' && v >= 4 && v <= 7) rtttlDefaultOct = (uint8_t)v;
            if (tag == 'b' && v != 0) rtttlBpm = v;
        }

        // Move to next comma or end.
        while (p < end && *p && *p != ',') p++;
        if (p < end && *p == ',') p++;
    }

    // Notes start right after the second ':'.
    rtttlPos = end + 1;

    // Whole note duration in ms.
    // wholenote = (60_000 / bpm) * 4
    if (rtttlBpm == 0) rtttlBpm = 63;
    rtttlWholeNoteMs = (uint32_t)((60000UL / (uint32_t)rtttlBpm) * 4UL);
}

uint16_t AudioManager::noteFreqHz(char note, bool sharp, uint8_t octave) const {
    // Match RTTTL expectations: A4 = 440Hz, 12-TET.
    int semi = -1; // semitone index within octave where C=0
    switch (note) {
        case 'c': semi = 0; break;
        case 'd': semi = 2; break;
        case 'e': semi = 4; break;
        case 'f': semi = 5; break;
        case 'g': semi = 7; break;
        case 'a': semi = 9; break;
        case 'b': semi = 11; break;
        default: return 0;
    }
    if (sharp) semi = (semi + 1) % 12;

    // Clamp to RTTTL common range (Nokia 61xx): {4..7}.
    if (octave < 4) octave = 4;
    if (octave > 7) octave = 7;

    // MIDI note number: (octave+1)*12 + semitone (C4 = 60).
    const int midi = ((int)octave + 1) * 12 + semi;
    const float freq = 440.0f * powf(2.0f, ((float)midi - 69.0f) / 12.0f);
    if (freq <= 0.0f) return 0;
    return (uint16_t)(freq + 0.5f);
}

bool AudioManager::rtttlStartNext() {
    if (!rtttlActive || !rtttlPos) return false;

    const char* p = rtttlPos;
    // Skip separators/spaces.
    while (*p == ' ' || *p == ',') p++;
    if (*p == '\0') {
        if (rtttlLoop) {
            // Loop: re-parse header and restart at notes.
            rtttlParseHeaderAndReset(rtttlStr);
            p = rtttlPos ? rtttlPos : p;
            while (*p == ' ' || *p == ',') p++;
            if (*p == '\0') return false;
        } else {
            return false;
        }
    }

    // Parse: [dur] note [#] [oct] [.] [,]
    uint16_t dur = 0;
    if (isDigit(*p)) dur = parseNumber(p);
    if (dur == 0) dur = rtttlDefaultDur;

    char n = *p;
    if (n >= 'A' && n <= 'Z') n = (char)(n - 'A' + 'a');
    if (!n) return false;
    p++;

    bool sharp = false;
    if (*p == '#') { sharp = true; p++; }

    // Some RTTTL sources use 'h' for 'b' (German notation).
    if (n == 'h') n = 'b';
    // Be forgiving: unknown note letters become a rest so we don't break playback
    // on "invalid note" characters from copy/paste sources.
    if (!((n >= 'a' && n <= 'g') || n == 'b' || n == 'p')) {
        n = 'p';
        sharp = false;
    }

    uint16_t oct = 0;
    if (isDigit(*p)) oct = parseNumber(p);
    if (oct == 0) oct = rtttlDefaultOct;

    bool dotted = false;
    if (*p == '.') { dotted = true; p++; }

    // Advance position to next token.
    while (*p && *p != ',') p++;
    if (*p == ',') p++;
    rtttlPos = p;

    // Duration in ms.
    if (rtttlWholeNoteMs == 0) rtttlWholeNoteMs = (uint32_t)((60000UL / (uint32_t)max(1, (int)rtttlBpm)) * 4UL);
    uint32_t noteMs = rtttlWholeNoteMs / (uint32_t)max(1, (int)dur);
    if (dotted) noteMs += noteMs / 2;
    if (noteMs < 10) noteMs = 10;

    uint16_t freq = 0;
    if (n == 'p') {
        freq = 0;
    } else {
        freq = noteFreqHz(n, sharp, (uint8_t)oct);
    }

    // Start tone.
    setToneHz(freq);
    if (freq == 0) ledcWrite(AUDIO_PWM_CHANNEL, 0);
    else applyVolumeDuty();

    playing = true;
    toneEndMs = (uint32_t)millis() + (uint32_t)noteMs;

    return true;
}

void AudioManager::startStep(uint8_t index) {
#if ENABLE_AUDIO
    if (!patternActive || index >= patternCount) return;

    const Step& s = pattern[index];
    setToneHz(s.freqHz);
    // Apply duty after setting tone frequency (some cores reset duty on ledcWriteTone()).
    applyVolumeDuty();
    playing = true;
    toneEndMs = (uint32_t)millis() + (uint32_t)s.durationMs;

    #if DEBUG_AUDIO
    Serial.print(F("[Audio] step "));
    Serial.print((int)index);
    Serial.print(F("/"));
    Serial.print((int)patternCount);
    Serial.print(F(" freq="));
    Serial.print((int)s.freqHz);
    Serial.print(F("Hz dur="));
    Serial.print((int)s.durationMs);
    Serial.print(F("ms vol="));
    Serial.println((int)globalSettings.getSoundVolumeLevel());
    #endif
#endif
}

// -----------------------------
// Public API
// -----------------------------
void AudioManager::begin() {
#if ENABLE_AUDIO
    ensureInit();
#endif
}

void AudioManager::update() {
#if ENABLE_AUDIO
    // If sound got disabled, silence immediately.
    if (!soundAllowed()) {
        #if DEBUG_AUDIO
        static uint32_t lastMutedLogMs = 0;
        const uint32_t now = (uint32_t)millis();
        if (playing && (uint32_t)(now - lastMutedLogMs) > 500) {
            lastMutedLogMs = now;
            Serial.println(F("[Audio] muted while playing -> stopAll()"));
        }
        #endif
        stopAll();
        return;
    }

    if (playing) {
        const uint32_t now = (uint32_t)millis();
        // Wrap-safe "now >= toneEndMs"
        if ((int32_t)(now - toneEndMs) >= 0) {
            if (patternActive && patternCount > 0) {
                patternIndex++;
                if (patternIndex < patternCount) {
                    startStep(patternIndex);
                } else {
                    // Pattern finished. If a ringtone is active, resume it.
                    patternActive = false;
                    patternCount = 0;
                    patternIndex = 0;
                    playing = false;
                    toneEndMs = 0;
                    if (rtttlActive) {
                        (void)rtttlStartNext();
                    } else {
                        stopAll();
                    }
                }
            } else {
                // A single tone or an RTTTL note ended.
                playing = false;
                toneEndMs = 0;
                if (rtttlActive) {
                    if (!rtttlStartNext()) {
                        // Ringtone ended.
                        stopRtttl();
                    }
                } else {
                    stopAll();
                }
            }
        }
    } else {
        // Nothing currently playing: if ringtone is active, continue it.
        if (rtttlActive) {
            if (!rtttlStartNext()) stopRtttl();
        }
    }
#endif
}

void AudioManager::stopAll() {
#if ENABLE_AUDIO
    if (!initialized) return;
    setToneHz(0);
    ledcWrite(AUDIO_PWM_CHANNEL, 0);
    playing = false;
    patternActive = false;
    patternCount = 0;
    patternIndex = 0;
    rtttlActive = false;
    rtttlLoop = true;
    rtttlStr = nullptr;
    rtttlPos = nullptr;
    toneEndMs = 0;
#endif
}

void AudioManager::playTone(uint16_t freqHz, uint16_t durationMs) {
#if ENABLE_AUDIO
    if (!soundAllowed()) {
        #if DEBUG_AUDIO
        static uint32_t lastNoSoundLogMs = 0;
        const uint32_t now = (uint32_t)millis();
        if ((uint32_t)(now - lastNoSoundLogMs) > 400) {
            lastNoSoundLogMs = now;
            Serial.println(F("[Audio] playTone() skipped (Sound=OFF). Enable Settings -> Sound -> ON"));
        }
        #endif
        return;
    }
    if (freqHz == 0 || durationMs == 0) return;

    // Cancel any in-progress pattern.
    patternActive = false;
    patternCount = 0;
    patternIndex = 0;

    // Volume 0 is a "volume mute".
    if (globalSettings.getSoundVolumeLevel() == 0) return;

    ensureInit();

    #if DEBUG_AUDIO
    Serial.print(F("[Audio] playTone freq="));
    Serial.print(freqHz);
    Serial.print(F("Hz dur="));
    Serial.print(durationMs);
    Serial.print(F("ms vol="));
    Serial.println((int)globalSettings.getSoundVolumeLevel());
    #endif

    setToneHz(freqHz);
    applyVolumeDuty();
    playing = true;
    toneEndMs = (uint32_t)millis() + (uint32_t)durationMs;
#else
    (void)freqHz; (void)durationMs;
#endif
}

void AudioManager::playPattern(const Step* steps, uint8_t stepCount) {
#if ENABLE_AUDIO
    if (!soundAllowed()) return;
    if (!steps || stepCount == 0) return;

    // Volume 0 is a "volume mute".
    if (globalSettings.getSoundVolumeLevel() == 0) return;

    ensureInit();

    const uint8_t n = (uint8_t)min((int)stepCount, (int)MAX_STEPS);
    for (uint8_t i = 0; i < n; i++) pattern[i] = steps[i];

    patternActive = true;
    patternCount = n;
    patternIndex = 0;
    startStep(0);
#else
    (void)steps; (void)stepCount;
#endif
}

void AudioManager::playRtttl(const char* rtttl, bool loop) {
#if ENABLE_AUDIO
    if (!soundAllowed()) return;
    if (!rtttl) return;

    rtttlLoop = loop;
    rtttlActive = true;
    rtttlParseHeaderAndReset(rtttl);

    // Start immediately if nothing else is currently playing.
    if (!playing && !patternActive) {
        if (!rtttlStartNext()) stopRtttl();
    }
#else
    (void)rtttl; (void)loop;
#endif
}

void AudioManager::stopRtttl() {
#if ENABLE_AUDIO
    rtttlActive = false;
    rtttlStr = nullptr;
    rtttlPos = nullptr;
    // Also silence output if ringtone was the current source.
    if (!patternActive) {
        setToneHz(0);
        ledcWrite(AUDIO_PWM_CHANNEL, 0);
        playing = false;
        toneEndMs = 0;
    }
#endif
}

void AudioManager::uiNavigateTick() {
    // A short, pleasant, clearly audible UI tick.
    // Frequency picked to be noticeable without being too harsh.
    playTone(1760 /*Hz*/, 18 /*ms*/);
}

void AudioManager::uiUp() {
    playTone(1960 /*Hz*/, 16 /*ms*/);
}

void AudioManager::uiDown() {
    playTone(1470 /*Hz*/, 16 /*ms*/);
}

void AudioManager::uiLeft() {
    playTone(1040 /*Hz*/, 14 /*ms*/);
}

void AudioManager::uiRight() {
    playTone(1240 /*Hz*/, 14 /*ms*/);
}

void AudioManager::uiConfirmShoot() {
    // A-button confirm: short "pew" (descending chirps).
    static const Step steps[] = {
        { 2800, 10 }, { 0, 4 },
        { 2200, 10 }, { 0, 4 },
        { 1700, 14 }
    };
    playPattern(steps, (uint8_t)(sizeof(steps) / sizeof(steps[0])));
}

void AudioManager::uiStartStop() {
    // START button: "STOP!" alert (descending, longer).
    static const Step steps[] = {
        { 880, 70 }, { 0, 30 },
        { 660, 70 }, { 0, 30 },
        { 440, 130 }
    };
    playPattern(steps, (uint8_t)(sizeof(steps) / sizeof(steps[0])));
}


