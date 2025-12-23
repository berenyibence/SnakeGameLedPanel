#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "../../engine/GameBase.h"
#include "../../engine/config.h"

/**
 * LavaLampApp - calming procedural color blobs using lightweight value noise.
 */
class LavaLampApp : public GameBase {
private:
    static inline uint32_t hash32(uint32_t x) {
        x ^= x >> 16;
        x *= 0x7feb352dU;
        x ^= x >> 15;
        x *= 0x846ca68bU;
        x ^= x >> 16;
        return x;
    }

    static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
    static inline float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

    static float noise2(int x, int y, int t) {
        const uint32_t h = hash32((uint32_t)(x * 73856093) ^ (uint32_t)(y * 19349663) ^ (uint32_t)(t * 83492791));
        return (float)(h & 0xFFFF) / 65535.0f;
    }

    static float valueNoise(float fx, float fy, int t) {
        const int x0 = (int)floorf(fx);
        const int y0 = (int)floorf(fy);
        const int x1 = x0 + 1;
        const int y1 = y0 + 1;
        const float tx = smooth(fx - (float)x0);
        const float ty = smooth(fy - (float)y0);
        const float v00 = noise2(x0, y0, t);
        const float v10 = noise2(x1, y0, t);
        const float v01 = noise2(x0, y1, t);
        const float v11 = noise2(x1, y1, t);
        const float a = lerp(v00, v10, tx);
        const float b = lerp(v01, v11, tx);
        return lerp(a, b, ty);
    }

    static uint16_t palette(float v) {
        // simple "lava lamp" palette: purple -> blue -> cyan -> pink
        v = constrain(v, 0.0f, 1.0f);
        const float r = (v < 0.5f) ? lerp(40, 20, v * 2.0f) : lerp(20, 240, (v - 0.5f) * 2.0f);
        const float g = (v < 0.5f) ? lerp(10, 160, v * 2.0f) : lerp(160, 30, (v - 0.5f) * 2.0f);
        const float b = (v < 0.5f) ? lerp(80, 220, v * 2.0f) : lerp(220, 140, (v - 0.5f) * 2.0f);
        return (uint16_t)(((uint16_t)(r / 8) << 11) | ((uint16_t)(g / 4) << 5) | ((uint16_t)(b / 8)));
    }

public:
    void start() override {}
    void reset() override {}
    bool isGameOver() override { return false; }
    void update(ControllerManager* /*input*/) override {}

    void draw(MatrixPanel_I2S_DMA* d) override {
        const uint32_t now = millis();
        const int t0 = (int)(now / 350);
        const float tf = (float)(now % 350) / 350.0f;

        // Render at low-res 16x16 then upscale to 64x64 (4x4 blocks) for speed.
        for (int gy = 0; gy < 16; gy++) {
            for (int gx = 0; gx < 16; gx++) {
                const float x = (float)gx / 5.0f;
                const float y = (float)gy / 5.0f;
                const float vA = valueNoise(x + 0.6f, y + 0.2f, t0);
                const float vB = valueNoise(x + 0.6f, y + 0.2f, t0 + 1);
                const float v = lerp(vA, vB, tf);

                // Soft threshold to form blobs
                float blob = (v - 0.35f) / 0.65f;
                blob = constrain(blob, 0.0f, 1.0f);
                const uint16_t col = palette(blob);

                const int px = gx * 4;
                const int py = gy * 4;
                d->fillRect(px, py, 4, 4, col);
            }
        }
    }
};


