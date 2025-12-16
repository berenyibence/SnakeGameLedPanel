#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "ControllerManager.h"

class Menu {
public:
    int selected = 0;
    const char* options[2] = { "Snake", "Pong" };
    static const int NUM_OPTIONS = 2;

    void draw(MatrixPanel_I2S_DMA* d, int players) {
        d->fillScreen(0);
        
        // Smaller font spacing - reduced cursor positions
        d->setCursor(2, 4);
        d->setTextColor(COLOR_CYAN);
        d->print("MENU");

        // Menu options with smaller spacing
        for (int i = 0; i < NUM_OPTIONS; i++) {
            int yPos = 18 + (i * 12);  // Reduced spacing from 20 to 12
            d->setCursor(4, yPos);
            
            if (i == selected) {
                d->setTextColor(COLOR_GREEN);
                d->print(">");
            } else {
                d->setTextColor(COLOR_WHITE);
                d->print(" ");
            }
            
            d->setCursor(10, yPos);
            d->setTextColor(i == selected ? COLOR_GREEN : COLOR_WHITE);
            d->print(options[i]);
        }

        // Player count with smaller text
        d->setCursor(2, 50);
        d->setTextColor(COLOR_YELLOW);
        d->printf("%dP", players);
    }

    int update(ControllerManager* input) {
        ControllerPtr ctl = input->getController(0);
        if (!ctl) return -1;

        uint8_t dpad = ctl->dpad();
        
        // Navigate menu with D-pad
        if ((dpad & 0x01) && selected > 0) {  // UP
            selected--;
            delay(150);  // Debounce
        }
        if ((dpad & 0x02) && selected < NUM_OPTIONS - 1) {  // DOWN
            selected++;
            delay(150);  // Debounce
        }
        
        // Select with A button
        if (ctl->a()) {
            delay(200);  // Debounce
            return selected;
        }
        
        return -1;
    }
};
