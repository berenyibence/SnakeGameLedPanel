#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "ControllerManager.h"
#include "SmallFont.h"

class Menu {
public:
    int selected = 0;
    int scrollOffset = 0;  // For scrolling when many items
    const char* options[8] = { "Snake", "Pong", "Breakout", "Shooter", "Labyrinth", "Tetris", "Emojis", "Settings" };
    static const int NUM_OPTIONS = 8;
    static const int VISIBLE_ITEMS = 6;  // 6 lines *8px + header fits 64px
    
    // Get actual number of visible options based on player count
    int getVisibleOptionsCount(int players) {
        int count = 0;
        for (int i = 0; i < NUM_OPTIONS; i++) {
            if (isOptionVisible(i, players)) count++;
        }
        return count;
    }
    
    // Check if option should be visible
    bool isOptionVisible(int index, int players) {
        if (index == 5) {  // Tetris
            return players == 1;
        }
        // All others always visible
        return true;  // All other options always visible
    }
    
    // Get actual option index from visible index
    int getActualIndex(int visibleIndex, int players) {
        int actual = 0;
        int visible = 0;
        
        for (int i = 0; i < NUM_OPTIONS; i++) {
            if (isOptionVisible(i, players)) {
                if (visible == visibleIndex) {
                    return i;
                }
                visible++;
            }
        }
        return visibleIndex;  // Fallback
    }
    
    // Get visible index from actual index
    int getVisibleIndex(int actualIndex, int players) {
        int visible = 0;
        for (int i = 0; i < actualIndex; i++) {
            if (isOptionVisible(i, players)) {
                visible++;
            }
        }
        return visible;
    }

    void draw(MatrixPanel_I2S_DMA* d, int players) {
        d->fillScreen(0);
        
        // Title with small font
        SmallFont::drawString(d, 2, 6, "MENU", COLOR_CYAN);

        // Count visible options
        int visibleCount = 0;
        for (int i = 0; i < NUM_OPTIONS; i++) {
            if (isOptionVisible(i, players)) {
                visibleCount++;
            }
        }
        
        // Ensure selected is valid
        int visibleSelected = getVisibleIndex(selected, players);
        if (visibleSelected >= visibleCount) {
            visibleSelected = visibleCount - 1;
            // Find actual index
            for (int i = 0; i < NUM_OPTIONS; i++) {
                if (isOptionVisible(i, players)) {
                    if (getVisibleIndex(i, players) == visibleSelected) {
                        selected = i;
                        break;
                    }
                }
            }
        }
        
        // Calculate visible range for scrolling
        int maxVisible = VISIBLE_ITEMS;
        if (visibleSelected < scrollOffset) {
            scrollOffset = visibleSelected;
        }
        if (visibleSelected >= scrollOffset + maxVisible) {
            scrollOffset = visibleSelected - maxVisible + 1;
        }
        
        // Draw visible options
        int visibleIdx = 0;
        for (int i = 0; i < NUM_OPTIONS; i++) {
            if (!isOptionVisible(i, players)) continue;
            
            if (visibleIdx >= scrollOffset && visibleIdx < scrollOffset + maxVisible) {
                int yPos = 14 + ((visibleIdx - scrollOffset) * 8);
                
                // Draw selection indicator
                if (i == selected) {
                    SmallFont::drawChar(d, 2, yPos, '>', COLOR_GREEN);
                } else {
                    SmallFont::drawChar(d, 2, yPos, ' ', COLOR_WHITE);
                }
                
                // Draw option name
                SmallFont::drawString(d, 6, yPos, options[i], 
                    i == selected ? COLOR_GREEN : COLOR_WHITE);
            }
            visibleIdx++;
        }

        // Player count with small font
        char playerStr[4];
        snprintf(playerStr, sizeof(playerStr), "%dP", players);
        SmallFont::drawString(d, 2, 60, playerStr, COLOR_YELLOW);
        
        // Scroll indicators (if needed)
        if (scrollOffset > 0) {
            // Up arrow indicator
            d->drawPixel(60, 8, COLOR_WHITE);
            d->drawPixel(59, 9, COLOR_WHITE);
            d->drawPixel(61, 9, COLOR_WHITE);
        }
        if (scrollOffset + maxVisible < visibleCount) {
            // Down arrow indicator
            d->drawPixel(60, 56, COLOR_WHITE);
            d->drawPixel(59, 55, COLOR_WHITE);
            d->drawPixel(61, 55, COLOR_WHITE);
        }
    }

    int update(ControllerManager* input) {
        ControllerPtr ctl = input->getController(0);
        if (!ctl) return -1;

        int players = input->getConnectedCount();
        uint8_t dpad = ctl->dpad();
        static unsigned long lastMove = 0;
        unsigned long now = millis();
        
        // Navigate menu with D-pad (with debouncing)
        if (now - lastMove > 150) {
            if (dpad & 0x01) {  // UP
                // Find previous visible option
                int currentVisible = getVisibleIndex(selected, players);
                if (currentVisible > 0) {
                    // Find previous visible option
                    for (int i = selected - 1; i >= 0; i--) {
                        if (isOptionVisible(i, players)) {
                            selected = i;
                            break;
                        }
                    }
                }
                lastMove = now;
            }
            if (dpad & 0x02) {  // DOWN
                // Find next visible option
                int currentVisible = getVisibleIndex(selected, players);
                int visibleCount = 0;
                for (int i = 0; i < NUM_OPTIONS; i++) {
                    if (isOptionVisible(i, players)) visibleCount++;
                }
                
                if (currentVisible < visibleCount - 1) {
                    // Find next visible option
                    for (int i = selected + 1; i < NUM_OPTIONS; i++) {
                        if (isOptionVisible(i, players)) {
                            selected = i;
                            break;
                        }
                    }
                }
                lastMove = now;
            }
        }
        
        // Select with A button
        static unsigned long lastSelect = 0;
        if (ctl->a() && (now - lastSelect > 200)) {
            lastSelect = now;
            return selected;
        }
        
        return -1;
    }
};
