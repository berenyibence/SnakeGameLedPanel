#pragma once
#include <Arduino.h>
#include <math.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../engine/ControllerManager.h"
#include "../component/SmallFont.h"
#include "../engine/Settings.h"
#include "../component/ScrollableList.h"
#include "../engine/EepromManager.h"
#include "../engine/Leaderboard.h"
#include "../engine/UserProfiles.h"

// Forward declaration
extern MatrixPanel_I2S_DMA* dma_display;

/**
 * SettingsMenu - Menu for adjusting system settings
 * Allows changing brightness and other persistent settings
 */
class SettingsMenu {
public:
    enum SettingType {
        SETTING_BRIGHTNESS,
        SETTING_GAME_SPEED,
        SETTING_SOUND,
        SETTING_SOUND_VOLUME,
        SETTING_SIMON_DIFFICULTY,
        SETTING_SIMON_LIVES,
        SETTING_RESET,
        SETTING_REBOOT,
        SETTING_ERASE_EEPROM,
        SETTING_BACK,
        NUM_SETTINGS
    };
    
    // Reusable list widget (selection + input). We keep `selected` as an alias
    // so existing engine code can keep setting `settingsMenu.selected = 0`.
    int selected = 0;
    ScrollableList list;
    bool isActive = false;

    // HUD layout
    static constexpr int HUD_H = 8;

    // Analog tuning
    static constexpr float STICK_DEADZONE = 0.22f;
    static constexpr int16_t AXIS_DIVISOR = 512;

    // Repeat behavior (prevents double steps when not "fast enough")
    static constexpr uint16_t DPAD_REPEAT_DELAY_MS = 450;
    static constexpr uint16_t DPAD_REPEAT_INTERVAL_MS = 180;
    
    // Bluepad32 analog helper (SFINAE) so we don't hard-depend on a single API surface.
    struct InputDetail {
        template <typename T>
        static auto axisX(T* c, int) -> decltype(c->axisX(), int16_t()) { return (int16_t)c->axisX(); }
        template <typename T>
        static int16_t axisX(T*, ...) { return 0; }

        template <typename T>
        static auto axisY(T* c, int) -> decltype(c->axisY(), int16_t()) { return (int16_t)c->axisY(); }
        template <typename T>
        static int16_t axisY(T*, ...) { return 0; }
    };

    static inline float clampf(float v, float lo, float hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }
    static inline float deadzone01(float v, float dz) {
        const float a = fabsf(v);
        if (a <= dz) return 0.0f;
        const float s = (a - dz) / (1.0f - dz);
        return (v < 0) ? -s : s;
    }

    void draw(MatrixPanel_I2S_DMA* display, ControllerManager* input) {
        (void)input; // Settings screen doesn't need to show player icons.
        display->fillScreen(0);
        
        // ----------------------
        // HUD
        // ----------------------
        SmallFont::drawString(display, 2, 6, "SETTINGS", COLOR_CYAN);
        for (int x = 0; x < PANEL_RES_X; x += 2) display->drawPixel(x, HUD_H - 1, COLOR_BLUE);
        
        // Keep widget selection in sync with our legacy `selected` field.
        list.selectedActual = constrain(selected, 0, NUM_SETTINGS - 1);

        // Draw settings list using the reusable widget + a right-side value callback.
        ScrollableList::Layout lay;
        lay.hudH = HUD_H;
        // 64px screen: with an 8px HUD band we can comfortably show 7 rows (7 * 8px + 8px HUD = 64px).
        // If we add more settings, the list will scroll automatically.
        lay.visibleRows = 7;
        list.draw(display, model, lay, ScrollableList::Colors(), &SettingsMenu::drawRightValueThunk, this);

        // Sync back so engine logic keeps working.
        selected = list.selectedActual;
    }
    
    /**
     * Update settings menu and handle input
     * Returns true if user wants to go back
     */
    bool update(ControllerManager* input) {
        ControllerPtr ctl = input->getController(0);
        if (!ctl) return false;
        
        const uint8_t dpad = ctl->dpad();
        const unsigned long now = millis();

        // ----------------------
        // Navigation (analog + D-pad) - shared via ScrollableList
        // ----------------------
        list.selectedActual = constrain(selected, 0, NUM_SETTINGS - 1);
        const int selectIdx = list.update(input, model);
        selected = list.selectedActual;

        // ----------------------
        // Adjust (analog X + D-pad Left/Right)
        // ----------------------
        // (Keep the old adjustment logic, but make the "prevDpad" state per-instance.)
        const bool up = (dpad & 0x01) != 0;
        const bool down = (dpad & 0x02) != 0;
        (void)up; (void)down; // kept for readability symmetry with the old code.

        const bool left = (dpad & 0x08) != 0;
        const bool right = (dpad & 0x04) != 0;
        const bool prevLeft = (prevDpadAdj & 0x08) != 0;
        const bool prevRight = (prevDpadAdj & 0x04) != 0;

        int adjDir = 0;
        if (left || right) {
            if ((left && !prevLeft) || (right && !prevRight)) {
                adjDir = left ? -1 : 1;
                dpadAdjHoldStartMs = (uint32_t)now;
                dpadAdjLastRepeatMs = (uint32_t)now;
            } else if (dpadAdjHoldStartMs != 0 &&
                       (uint32_t)(now - dpadAdjHoldStartMs) >= DPAD_REPEAT_DELAY_MS &&
                       (uint32_t)(now - dpadAdjLastRepeatMs) >= DPAD_REPEAT_INTERVAL_MS) {
                adjDir = left ? -1 : 1;
                dpadAdjLastRepeatMs = (uint32_t)now;
            }
        } else {
            dpadAdjHoldStartMs = 0;
        }

        if (adjDir == 0 && !(left || right)) {
            const float rawX = clampf((float)InputDetail::axisX(ctl, 0) / (float)AXIS_DIVISOR, -1.0f, 1.0f);
            const float sx = deadzone01(rawX, STICK_DEADZONE);
            if (sx < 0) adjDir = -1;
            else if (sx > 0) adjDir = 1;

            if (adjDir != 0) {
                const float mag = fabsf(sx);
                const uint32_t interval = (uint32_t)(320.0f - 160.0f * mag);
                if ((uint32_t)(now - lastAnalogAdjMs) > interval) lastAnalogAdjMs = (uint32_t)now;
                else adjDir = 0;
            } else {
                lastAnalogAdjMs = 0;
            }
        }

        if (adjDir != 0) adjustSetting(adjDir);
        if (adjDir != 0) {
            // Distinct left/right adjustment sounds.
            // IMPORTANT: play AFTER the value change so volume changes are hearable instantly.
            if (adjDir < 0) globalAudio.uiLeft();
            else globalAudio.uiRight();
        }
        prevDpadAdj = dpad;
        
        // Select/Activate with A button (debounced by ScrollableList)
        if (selectIdx != -1) {
            if (selected == SETTING_RESET) {
                // Reset to defaults
                globalSettings.resetToDefaults();
                globalSettings.save();
                delay(300);
                return false;  // Stay in menu
            } else if (selected == SETTING_REBOOT) {
                globalSettings.save();
                delay(150);
                Serial.println(F("[SettingsMenu] Reboot requested"));
                ESP.restart();
                return true; // unreachable, but keeps control flow obvious
            } else if (selected == SETTING_ERASE_EEPROM) {
                Serial.println(F("[SettingsMenu] Erase EEPROM requested"));
                eraseEepromAndReboot();
                return true; // unreachable
            } else if (selected == SETTING_BACK) {
                // Save all settings before going back
                globalSettings.save();
                delay(200);
                return true;
            }
        }
        
        // Also allow B button to go back
        static unsigned long lastB = 0;
        if (ctl->b() && (now - lastB > 200)) {
            lastB = now;
            globalSettings.save();
            delay(200);
            return true;
        }
        
        return false;
    }
    
private:
    // Settings list model (static strings + fixed ordering).
    class SettingsListModel : public ListModel {
    public:
        const char* settingNames[NUM_SETTINGS] = { "Brightness", "Game Speed", "Sound", "Volume", "Simon Diff", "Simon Lives", "Reset", "Reboot", "EraseEEP", "Back" };
        int itemCount() const override { return NUM_SETTINGS; }
        const char* label(int actualIndex) const override { return settingNames[actualIndex]; }
    };

    SettingsListModel model;

    // Per-instance adjustment repeat state (left/right).
    uint8_t prevDpadAdj = 0;
    uint32_t dpadAdjHoldStartMs = 0;
    uint32_t dpadAdjLastRepeatMs = 0;
    uint32_t lastAnalogAdjMs = 0;

    static void drawRightValueThunk(MatrixPanel_I2S_DMA* d, int actualIndex, int yBaseline, bool /*isSelected*/, void* user) {
        ((SettingsMenu*)user)->drawRightValue(d, actualIndex, yBaseline);
    }

    void drawRightValue(MatrixPanel_I2S_DMA* display, int actualIndex, int yPos) {
        if (actualIndex == SETTING_BRIGHTNESS) {
            char val[8];
            snprintf(val, sizeof(val), "%d", globalSettings.getBrightness());
            SmallFont::drawString(display, 50, yPos, val, COLOR_YELLOW);
        } else if (actualIndex == SETTING_GAME_SPEED) {
            char val[4];
            snprintf(val, sizeof(val), "%d", globalSettings.getGameSpeed());
            SmallFont::drawString(display, 50, yPos, val, COLOR_YELLOW);
        } else if (actualIndex == SETTING_SOUND) {
            const char* val = globalSettings.isSoundEnabled() ? "ON" : "OFF";
            SmallFont::drawString(display, 50, yPos, val, COLOR_YELLOW);
        } else if (actualIndex == SETTING_SOUND_VOLUME) {
            // Volume level: 0..10
            char val[4];
            snprintf(val, sizeof(val), "%d", (int)globalSettings.getSoundVolumeLevel());
            SmallFont::drawString(display, 50, yPos, val, COLOR_YELLOW);
        } else if (actualIndex == SETTING_SIMON_DIFFICULTY) {
            const uint8_t d = globalSettings.getSimonDifficulty();
            const char* val = (d == 0) ? "EASY" : (d == 1) ? "MED" : "HARD";
            SmallFont::drawString(display, 50, yPos, val, COLOR_YELLOW);
        } else if (actualIndex == SETTING_SIMON_LIVES) {
            char val[4];
            snprintf(val, sizeof(val), "%d", (int)globalSettings.getSimonLives());
            SmallFont::drawString(display, 50, yPos, val, COLOR_YELLOW);
        }
    }

    void eraseEepromAndReboot() {
        // Best-effort: reset in-memory settings and wipe the EEPROM arena.
        globalSettings.resetToDefaults();
        globalSettings.save();

        // Clear logical storages too (will rewrite their headers), then wipe all bytes.
        (void)UserProfiles::userCount(); // force-load profiles header for debug consistency
        Leaderboard::clearAll();

        Serial.print(F("[SettingsMenu] Wiping EEPROM bytes: "));
        Serial.println((unsigned)EepromManager::TOTAL_SIZE);
        for (size_t i = 0; i < EepromManager::TOTAL_SIZE; i++) {
            EepromManager::writeByte(i, 0xFF);
        }
        const bool ok = EepromManager::commit();
        Serial.println(ok ? F("[SettingsMenu] EEPROM erase committed") : F("[SettingsMenu] EEPROM erase commit FAILED"));
        delay(150);
        ESP.restart();
    }

    void adjustSetting(int delta) {
        switch (selected) {
            case SETTING_BRIGHTNESS: {
                int newVal = globalSettings.getBrightness() + (delta * 5);
                globalSettings.setBrightness(newVal);
                globalSettings.save();
                // Apply brightness immediately
                if (dma_display != nullptr) {
                    dma_display->setBrightness8(globalSettings.getBrightness());
                }
                break;
            }
            case SETTING_GAME_SPEED: {
                int newVal = globalSettings.getGameSpeed() + delta;
                globalSettings.setGameSpeed(newVal);
                globalSettings.save();
                break;
            }
            case SETTING_SOUND: {
                globalSettings.setSoundEnabled(!globalSettings.isSoundEnabled());
                globalSettings.save();
                break;
            }
            case SETTING_SOUND_VOLUME: {
                globalSettings.adjustSoundVolumeLevel(delta);
                globalSettings.save();
                break;
            }
            case SETTING_SIMON_DIFFICULTY: {
                const int cur = (int)globalSettings.getSimonDifficulty();
                int next = cur + delta;
                if (next < (int)Settings::SIMON_DIFFICULTY_MIN) next = (int)Settings::SIMON_DIFFICULTY_MAX;
                if (next > (int)Settings::SIMON_DIFFICULTY_MAX) next = (int)Settings::SIMON_DIFFICULTY_MIN;
                globalSettings.setSimonDifficulty((uint8_t)next);
                globalSettings.save();
                break;
            }
            case SETTING_SIMON_LIVES: {
                int next = (int)globalSettings.getSimonLives() + delta;
                next = constrain(next, (int)Settings::SIMON_LIVES_MIN, (int)Settings::SIMON_LIVES_MAX);
                globalSettings.setSimonLives((uint8_t)next);
                globalSettings.save();
                break;
            }
        }
    }
};

