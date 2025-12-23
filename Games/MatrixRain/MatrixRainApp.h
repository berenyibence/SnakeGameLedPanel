#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "../../engine/GameBase.h"
#include "../../engine/ControllerManager.h"
#include "../../engine/config.h"
#include "../../component/SmallFont.h"

/**
 * MatrixRainApp - classic "Matrix" green code rain.
 * Non-interactive background app (press B in engine game-over? START pauses globally).
 */
class MatrixRainApp : public GameBase {
private:
    static constexpr int COLS = 16;     // 64px / 4px column width
    static constexpr int CELL_W = 4;
    static constexpr int CELL_H = 6;    // fits TomThumb-ish vertically

    struct Stream {
        int16_t y = 0;
        uint8_t speed = 1;
        uint8_t len = 10;
        uint8_t phase = 0;
    };

    Stream s[COLS];
    uint32_t lastMs = 0;

    static inline char randGlyph() {
        const uint8_t r = (uint8_t)random(0, 36);
        if (r < 10) return (char)('0' + r);
        return (char)('A' + (r - 10));
    }

public:
    void start() override {
        randomSeed((uint32_t)micros() ^ (uint32_t)millis());
        for (int i = 0; i < COLS; i++) {
            s[i].y = (int16_t)random(-64, 0);
            s[i].speed = (uint8_t)random(1, 4);
            s[i].len = (uint8_t)random(8, 18);
            s[i].phase = (uint8_t)random(0, 255);
        }
        lastMs = millis();
    }

    void reset() override { start(); }
    bool isGameOver() override { return false; }

    void update(ControllerManager* /*input*/) override {
        const uint32_t now = millis();
        if ((uint32_t)(now - lastMs) < 40) return;
        lastMs = now;
        for (int i = 0; i < COLS; i++) {
            s[i].y += s[i].speed;
            if (s[i].y > 64 + (int)s[i].len * CELL_H) {
                s[i].y = (int16_t)random(-90, -10);
                s[i].speed = (uint8_t)random(1, 4);
                s[i].len = (uint8_t)random(8, 18);
            }
        }
    }

    void draw(MatrixPanel_I2S_DMA* d) override {
        // Fade effect: draw a translucent-ish black overlay by sparsely clearing pixels
        // (cheap approximation for HUB75).
        for (int y = 0; y < PANEL_RES_Y; y++) {
            for (int x = 0; x < PANEL_RES_X; x++) {
                if (((x + y + (int)(millis() / 20)) & 0x07) == 0) d->drawPixel(x, y, COLOR_BLACK);
            }
        }

        for (int i = 0; i < COLS; i++) {
            const int x = i * CELL_W;
            // draw head + tail
            for (int k = 0; k < (int)s[i].len; k++) {
                const int yy = (int)s[i].y - k * CELL_H;
                if (yy < -CELL_H || yy >= PANEL_RES_Y) continue;

                const uint8_t fade = (uint8_t)constrain(255 - k * (220 / max(1, (int)s[i].len)), 20, 255);
                const uint16_t col = d->color565(0, (uint8_t)min(255, 40 + fade), 0);
                const char ch = randGlyph();
                SmallFont::drawChar(d, x, yy, ch, col);
            }
            // bright head
            const int hy = (int)s[i].y;
            if (hy >= 0 && hy < PANEL_RES_Y) SmallFont::drawChar(d, x, hy, randGlyph(), COLOR_WHITE);
        }
    }
};


