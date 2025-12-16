#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "ControllerManager.h"
#include "SmallFont.h"
#include "Settings.h"

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
        SETTING_RESET,
        SETTING_BACK,
        NUM_SETTINGS
    };
    
    int selected = 0;
    bool isActive = false;
    
    void draw(MatrixPanel_I2S_DMA* display) {
        display->fillScreen(0);
        
        // Title
        SmallFont::drawString(display, 2, 2, "SETTINGS", COLOR_CYAN);
        
        // Draw settings options
        const char* settingNames[] = {
            "Brightness",
            "Game Speed",
            "Sound",
            "Reset",
            "Back"
        };
        
        for (int i = 0; i < NUM_SETTINGS; i++) {
            int yPos = 12 + (i * 8);
            
            // Selection indicator
            if (i == selected) {
                SmallFont::drawChar(display, 2, yPos, '>', COLOR_GREEN);
            } else {
                SmallFont::drawChar(display, 2, yPos, ' ', COLOR_WHITE);
            }
            
            // Setting name
            SmallFont::drawString(display, 8, yPos, settingNames[i], 
                i == selected ? COLOR_GREEN : COLOR_WHITE);
            
            // Draw value
            if (i == SETTING_BRIGHTNESS) {
                char val[8];
                snprintf(val, sizeof(val), "%d", globalSettings.getBrightness());
                SmallFont::drawString(display, 50, yPos, val, COLOR_YELLOW);
            } else if (i == SETTING_GAME_SPEED) {
                char val[4];
                snprintf(val, sizeof(val), "%d", globalSettings.getGameSpeed());
                SmallFont::drawString(display, 50, yPos, val, COLOR_YELLOW);
            } else if (i == SETTING_SOUND) {
                const char* val = globalSettings.isSoundEnabled() ? "ON" : "OFF";
                SmallFont::drawString(display, 50, yPos, val, COLOR_YELLOW);
            }
        }
        
        // Instructions
        SmallFont::drawString(display, 2, 58, "A:Select L/R:Change", COLOR_BLUE);
    }
    
    /**
     * Update settings menu and handle input
     * Returns true if user wants to go back
     */
    bool update(ControllerManager* input) {
        ControllerPtr ctl = input->getController(0);
        if (!ctl) return false;
        
        uint8_t dpad = ctl->dpad();
        static unsigned long lastMove = 0;
        unsigned long now = millis();
        
        // Navigate menu
        if (now - lastMove > 150) {
            if ((dpad & 0x01) && selected > 0) {  // UP
                selected--;
                lastMove = now;
            }
            if ((dpad & 0x02) && selected < NUM_SETTINGS - 1) {  // DOWN
                selected++;
                lastMove = now;
            }
        }
        
        // Adjust values with left/right
        if (now - lastMove > 100) {
            if (dpad & 0x08) {  // LEFT
                adjustSetting(-1);
                lastMove = now;
            }
            if (dpad & 0x04) {  // RIGHT
                adjustSetting(1);
                lastMove = now;
            }
        }
        
        // Select/Back with A button
        static unsigned long lastSelect = 0;
        if (ctl->a() && (now - lastSelect > 200)) {
            lastSelect = now;
            
            if (selected == SETTING_RESET) {
                // Reset to defaults
                globalSettings.resetToDefaults();
                globalSettings.save();
                delay(300);
                return false;  // Stay in menu
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
        }
    }
};

