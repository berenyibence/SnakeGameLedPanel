#pragma once
#include <Arduino.h>
#include <EEPROM.h>
#include "config.h"
#include "EepromManager.h"

/**
 * Settings - Persistent settings storage using EEPROM
 * Settings are saved to EEPROM and persist between power cycles
 */
class Settings {
public:
    struct SettingsData {
        uint8_t brightness;      // Display brightness (0-255)
        uint8_t gameSpeed;        // Game speed multiplier (1-5)
        uint8_t soundEnabled;     // Sound enabled (0 or 1)
        uint8_t reserved[5];      // Reserved for future settings (reserved[0] = playerColorIndex, reserved[1] = soundVolumeLevel)
        uint8_t checksum;         // Simple checksum for validation
    };
    
    static const int EEPROM_ADDRESS = 0;
    static const uint8_t DEFAULT_BRIGHTNESS = 200;  // Higher default brightness
    static const uint8_t DEFAULT_GAME_SPEED = 1;
    static const uint8_t DEFAULT_SOUND_ENABLED = 0;
    static const uint8_t DEFAULT_SOUND_VOLUME_LEVEL = 6; // 0..10
    static const uint8_t SOUND_VOLUME_MIN = 0;
    static const uint8_t SOUND_VOLUME_MAX = 10;

    // -----------------------------------------------------
    // Player Color (persisted)
    // -----------------------------------------------------
    // NOTE: We intentionally store this in reserved[0] so older EEPROM layouts
    // remain compatible and checksums remain valid (reserved bytes were already
    // part of the checksum).
    static const uint8_t DEFAULT_PLAYER_COLOR_INDEX = 0;

    // Palette of "player" colors that look good on a HUB75 RGB565 panel.
    // Keep this list small and high-contrast to avoid muddy colors at lower brightness.
    static const uint8_t PLAYER_COLOR_COUNT = 8;

    // NOTE: These arrays are defined in `Settings.cpp` to satisfy the linker
    // on Arduino/ESP32 (where headers are compiled as separate translation units).
    static const uint16_t PLAYER_COLORS[PLAYER_COLOR_COUNT];
    static const char* const PLAYER_COLOR_NAMES[PLAYER_COLOR_COUNT];
    
    SettingsData data;
    
    Settings() {
        // NOTE: EEPROM initialization is now handled centrally by EepromManager
        // in setup(). We intentionally DO NOT call load() here, so that any debug
        // Serial output only happens after Serial.begin() in setup().
    }
    
    /**
     * Load settings from EEPROM
     */
    void load() {
        Serial.println(F("[Settings] load() called"));
        Serial.print(F("[Settings] Reading from EEPROM address "));
        Serial.println(EEPROM_ADDRESS);
        
        // Check if EEPROM is initialized
        if (!EepromManager::isInitialized()) {
            Serial.println(F("[Settings] ERROR: EEPROM not initialized! Call EepromManager::begin() first."));
            resetToDefaults();
            return;
        }
        
        EEPROM.get(EEPROM_ADDRESS, data);

        Serial.print(F("[Settings]  raw brightness from EEPROM: "));
        Serial.println(data.brightness);
        Serial.print(F("[Settings]  raw checksum from EEPROM: "));
        Serial.println(data.checksum);
        
        // Debug: dump first few bytes
        Serial.print(F("[Settings]  First 8 bytes: "));
        for (int i = 0; i < 8 && i < (int)sizeof(SettingsData); i++) {
            Serial.print(EepromManager::readByte(EEPROM_ADDRESS + i), HEX);
            Serial.print(F(" "));
        }
        Serial.println();

        // Basic sanity check: if brightness is clearly invalid / uninitialized,
        // treat EEPROM as empty and use defaults.
        if (data.brightness < 30 || data.brightness > 255) {
            Serial.println(F("[Settings]  brightness looks invalid -> treat as first boot"));
            resetToDefaults();
            save();
            return;
        }
        
        // Validate checksum
        uint8_t calculatedChecksum = calculateChecksum();
        Serial.print(F("[Settings]  calculated checksum: "));
        Serial.println(calculatedChecksum);

        if (calculatedChecksum != data.checksum) {
            // Invalid or first boot - use defaults
            Serial.println(F("[Settings]  checksum mismatch -> resetToDefaults()"));
            resetToDefaults();
            save();
        } else {
            Serial.println(F("[Settings]  checksum OK -> using stored values"));
        }
    }
    
    /**
     * Save settings to EEPROM
     */
    void save() {
        Serial.print(F("[Settings] save() brightness="));
        Serial.println(data.brightness);
        data.checksum = calculateChecksum();
        Serial.print(F("[Settings] Writing to EEPROM address "));
        Serial.println(EEPROM_ADDRESS);
        EEPROM.put(EEPROM_ADDRESS, data);
        const bool ok = EepromManager::commit();
        if (!ok) {
            Serial.println(F("[Settings] ERROR: EEPROM commit failed!"));
        } else {
            // Verify write by reading back
            SettingsData verify;
            EEPROM.get(EEPROM_ADDRESS, verify);
            Serial.print(F("[Settings] Verification read: brightness="));
            Serial.print(verify.brightness);
            Serial.print(F(", checksum="));
            Serial.println(verify.checksum);
            if (verify.brightness != data.brightness || verify.checksum != data.checksum) {
                Serial.println(F("[Settings] WARNING: Verification failed! Data mismatch."));
            } else {
                Serial.println(F("[Settings] Verification OK"));
            }
        }
    }
    
    /**
     * Reset settings to default values
     */
    void resetToDefaults() {
        Serial.println(F("[Settings] resetToDefaults()"));
        data.brightness = DEFAULT_BRIGHTNESS;
        data.gameSpeed = DEFAULT_GAME_SPEED;
        data.soundEnabled = DEFAULT_SOUND_ENABLED;
        for (int i = 0; i < 5; i++) {
            data.reserved[i] = 0;
        }
        data.reserved[0] = DEFAULT_PLAYER_COLOR_INDEX;
        data.reserved[1] = DEFAULT_SOUND_VOLUME_LEVEL;
    }
    
    /**
     * Calculate simple checksum for validation
     */
    uint8_t calculateChecksum() {
        uint8_t sum = 0;
        uint8_t* bytes = (uint8_t*)&data;
        for (int i = 0; i < sizeof(SettingsData) - 1; i++) {
            sum ^= bytes[i];
        }
        return sum;
    }
    
    /**
     * Get brightness setting
     */
    uint8_t getBrightness() const {
        return data.brightness;
    }
    
    /**
     * Set brightness (0-255)
     */
    void setBrightness(uint8_t brightness) {
        Serial.print(F("[Settings] setBrightness requested="));
        Serial.print(brightness);
        data.brightness = constrain(brightness, 30, 255);  // avoid too-dim startup
        Serial.print(F(" -> stored="));
        Serial.println(data.brightness);
    }
    
    /**
     * Get game speed multiplier
     */
    uint8_t getGameSpeed() const {
        return data.gameSpeed;
    }
    
    /**
     * Set game speed (1-5)
     */
    void setGameSpeed(uint8_t speed) {
        data.gameSpeed = constrain(speed, 1, 5);
    }
    
    /**
     * Check if sound is enabled
     */
    bool isSoundEnabled() const {
        return data.soundEnabled != 0;
    }
    
    /**
     * Enable or disable sound
     */
    void setSoundEnabled(bool enabled) {
        data.soundEnabled = enabled ? 1 : 0;
    }

    // -----------------------------------------------------
    // Sound volume (persisted via reserved[1])
    // -----------------------------------------------------
    /**
     * Sound volume level (0..10).
     * 0 = silent (acts like "volume mute" while still leaving Sound=ON).
     */
    uint8_t getSoundVolumeLevel() const {
        return (uint8_t)constrain((int)data.reserved[1], (int)SOUND_VOLUME_MIN, (int)SOUND_VOLUME_MAX);
    }

    void setSoundVolumeLevel(uint8_t level) {
        data.reserved[1] = (uint8_t)constrain((int)level, (int)SOUND_VOLUME_MIN, (int)SOUND_VOLUME_MAX);
    }

    void adjustSoundVolumeLevel(int delta) {
        const int lvl = (int)getSoundVolumeLevel() + delta;
        setSoundVolumeLevel((uint8_t)lvl);
    }

    // -----------------------------------------------------
    // Player color accessors (persisted via reserved[0])
    // -----------------------------------------------------
    uint8_t getPlayerColorIndex() const {
        // Always clamp to valid palette range.
        return (uint8_t)(data.reserved[0] % PLAYER_COLOR_COUNT);
    }

    void setPlayerColorIndex(uint8_t index) {
        data.reserved[0] = (uint8_t)(index % PLAYER_COLOR_COUNT);
    }

    void cyclePlayerColor(int delta = 1) {
        const int count = (int)PLAYER_COLOR_COUNT;
        int idx = (int)getPlayerColorIndex();
        idx = (idx + delta) % count;
        if (idx < 0) idx += count;
        setPlayerColorIndex((uint8_t)idx);
    }

    uint16_t getPlayerColor() const {
        return PLAYER_COLORS[getPlayerColorIndex()];
    }

    const char* getPlayerColorName() const {
        return PLAYER_COLOR_NAMES[getPlayerColorIndex()];
    }
};

// Global settings instance
extern Settings globalSettings;

