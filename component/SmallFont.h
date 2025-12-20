#pragma once
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Fonts/TomThumb.h>  // Very small 3x5 font from Adafruit GFX
#include "../engine/config.h"

/**
 * SmallFont - Helper functions for rendering smaller text
 * Uses Adafruit GFX TomThumb font (3x5 pixels)
 */
class SmallFont {
public:
    /**
     * Set the small font on the display
     */
    static void setFont(MatrixPanel_I2S_DMA* display) {
        display->setFont(&TomThumb);
    }
    
    /**
     * Draw a small string at position (x, y)
     * Uses TomThumb font (3x5 pixels per character)
     */
    static void drawString(MatrixPanel_I2S_DMA* display, int x, int y, const char* str, uint16_t color) {
        display->setFont(&TomThumb);
        display->setTextColor(color);
        display->setCursor(x, y);
        display->print(str);
    }
    
    /**
     * Draw a small formatted string (like printf)
     */
    static void drawStringF(MatrixPanel_I2S_DMA* display, int x, int y, uint16_t color, const char* format, ...) {
        char buffer[32];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        drawString(display, x, y, buffer, color);
    }
    
    /**
     * Draw a single character
     */
    static void drawChar(MatrixPanel_I2S_DMA* display, int x, int y, char c, uint16_t color) {
        char str[2] = {c, '\0'};
        drawString(display, x, y, str, color);
    }
};

