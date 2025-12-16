#pragma once
#include <Arduino.h>
#include <EEPROM.h>
#include "config.h"

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
        uint8_t reserved[5];      // Reserved for future settings
        uint8_t checksum;         // Simple checksum for validation
    };
    
    static const int EEPROM_ADDRESS = 0;
    static const uint8_t DEFAULT_BRIGHTNESS = 200;  // Higher default brightness
    static const uint8_t DEFAULT_GAME_SPEED = 1;
    static const uint8_t DEFAULT_SOUND_ENABLED = 0;
    
    SettingsData data;
    
    Settings() {
        // Initialize EEPROM storage.
        // NOTE: We intentionally DO NOT call load() here, so that any debug
        // Serial output only happens after Serial.begin() in setup().
        EEPROM.begin(512);
    }
    
    /**
     * Load settings from EEPROM
     */
    void load() {
        Serial.println(F("[Settings] load() called"));
        EEPROM.get(EEPROM_ADDRESS, data);

        Serial.print(F("[Settings]  raw brightness from EEPROM: "));
        Serial.println(data.brightness);
        Serial.print(F("[Settings]  raw checksum from EEPROM: "));
        Serial.println(data.checksum);

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
        EEPROM.put(EEPROM_ADDRESS, data);
        EEPROM.commit();
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
};

// Global settings instance
extern Settings globalSettings;

