// MVisualApp.h
// -----------------------------------------------------------------------------
// "MVisual" music visualizer applet implemented as a GameBase so it plugs into the
// existing host loop with minimal changes (Pattern A from README).
//
// Current source for visualization data:
// - A lightweight pseudo-random "noise spectrum" generator (placeholder).
//
// Planned later:
// - Replace the noise generator with microphone FFT magnitudes.
// -----------------------------------------------------------------------------
#pragma once

#include <Arduino.h>
#include <math.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "../../engine/GameBase.h"
#include "../../engine/ControllerManager.h"
#include "../../engine/config.h"
#include "../../component/SmallFont.h"

#include "MVisualAppConfig.h"

class MVisualApp : public GameBase {
public:
    MVisualApp() = default;

    void start() override {
        gameOver = false;
        bars = MVisualAppConfig::DEFAULT_BAR_COUNT;
        hudHidden = false;
        shadingMode = SHADING_OFF;
        vizMode = VIZ_BARS;

        colorMode = MODE_MONO_GRADIENT;
        monoColorIndex = 0;
        rainbowEffectIndex = 0;

        // Seed PRNG with time; this keeps the effect "fresh" after reboot.
        rngState = (uint32_t)millis() ^ 0xA3C59AC3u;

        lastUpdateMs = (uint32_t)millis();

        // Initialize spectrum with small random values to avoid a "dead first frame".
        for (int i = 0; i < 64; i++) {
            spectrum64[i] = rand01() * 0.25f;
        }
        smoothSpectrum64();

        // Reset input repeat/edge states.
        prevDpad = 0;
        dpadHoldStartMs = 0;
        dpadLastRepeatMs = 0;
        lastA = false;
        lastB = false;
        lastSelect = false;
        lastY = false;
        lastX = false;
    }

    void reset() override { start(); }

    void update(ControllerManager* input) override {
        const uint32_t now = (uint32_t)millis();
        if ((uint32_t)(now - lastUpdateMs) < MVisualAppConfig::UPDATE_INTERVAL_MS) {
            // Still allow edge detection even if we skip simulation tick.
            handleInput(input, now);
            return;
        }
        lastUpdateMs = now;

        handleInput(input, now);

        // Update the 64-bin "spectrum" regardless of current bar count.
        // This makes bar-count changes feel stable (we just resample/aggregate).
        updateNoiseSpectrum(now);
    }

    void draw(MatrixPanel_I2S_DMA* display) override {
        if (!display) return;

        display->fillScreen(COLOR_BLACK);

        const int hudH = hudHidden ? 0 : MVisualAppConfig::HUD_H;
        if (!hudHidden) drawHud(display);

        // Reserve bar drawing region.
        const int yTop = hudH;
        const int yBottom = PANEL_RES_Y - 1;
        const int barAreaH = (yBottom - yTop + 1);
        if (barAreaH <= 0) return;

        // Build per-segment values by aggregating from the 64-bin spectrum.
        // `bars` is 1..64 and acts as a resolution knob across all viz modes.
        for (int i = 0; i < (int)bars; i++) {
            const int a = (i * 64) / bars;
            const int b = ((i + 1) * 64) / bars; // exclusive
            float acc = 0.0f;
            int cnt = 0;
            for (int k = a; k < b; k++) {
                acc += spectrum64[k];
                cnt++;
            }
            barValue[i] = (cnt > 0) ? (acc / (float)cnt) : 0.0f;
            if (barValue[i] < 0.0f) barValue[i] = 0.0f;
            if (barValue[i] > 1.0f) barValue[i] = 1.0f;
        }

        const uint8_t timeHue = (uint8_t)((millis() / 8) & 0xFF);
        switch (vizMode) {
            default:
            case VIZ_BARS:
                drawBars(display, yTop, yBottom, barAreaH, timeHue);
                break;
            case VIZ_LINES:
                drawLines(display, yTop, yBottom, barAreaH, timeHue);
                break;
            case VIZ_DOTS:
                drawDots(display, yTop, yBottom, barAreaH, timeHue);
                break;
        }
    }

    bool isGameOver() override { return gameOver; } // applet: never "game over"

    uint16_t preferredRenderFps() const override {
        // This is mostly a shader-like display; 30 FPS is plenty and reduces HUB75 artifacts.
        return 30;
    }

private:
    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------
    bool gameOver = false;
    uint32_t lastUpdateMs = 0;

    uint8_t bars = MVisualAppConfig::DEFAULT_BAR_COUNT; // 1..64
    bool hudHidden = false;
    enum ShadingMode : uint8_t {
        SHADING_OFF = 0,
        SHADING_HORIZONTAL = 1, // left 100% -> right 50%
        SHADING_VERTICAL = 2    // bottom 100% -> top 50%
    };
    ShadingMode shadingMode = SHADING_OFF;

    enum ColorMode : uint8_t {
        MODE_MONO_GRADIENT = 0,
        MODE_RAINBOW = 1
    };
    ColorMode colorMode = MODE_MONO_GRADIENT;
    uint8_t monoColorIndex = 0;
    uint8_t rainbowEffectIndex = 0;

    enum VizMode : uint8_t {
        VIZ_BARS = 0,
        VIZ_LINES = 1,
        VIZ_DOTS = 2
    };
    VizMode vizMode = VIZ_BARS;

    // Input repeat state (Up/Down to change bar count)
    uint8_t prevDpad = 0;
    uint32_t dpadHoldStartMs = 0;
    uint32_t dpadLastRepeatMs = 0;
    bool lastA = false;
    bool lastB = false;
    bool lastSelect = false;
    bool lastY = false;
    bool lastX = false;

    // Noise spectrum (always 64 bins; bars are an aggregation view)
    uint32_t rngState = 0x12345678u;
    float spectrum64[64] = {};
    float spectrumTmp64[64] = {};
    float barValue[64] = {};

    // -------------------------------------------------------------------------
    // Bluepad32 button helpers (SFINAE + miscButtons fallback)
    // -------------------------------------------------------------------------
    struct InputDetail {
        template <typename T>
        static auto back(T* c, int) -> decltype(c->back(), bool()) { return (bool)c->back(); }
        template <typename T>
        static bool back(T*, ...) { return false; }

        template <typename T>
        static auto select(T* c, int) -> decltype(c->select(), bool()) { return (bool)c->select(); }
        template <typename T>
        static bool select(T*, ...) { return false; }

        template <typename T>
        static auto miscButtons(T* c, int) -> decltype(c->miscButtons(), uint16_t()) { return (uint16_t)c->miscButtons(); }
        template <typename T>
        static uint16_t miscButtons(T*, ...) { return 0; }
    };

    static inline float clamp01(float v) {
        return (v < 0.0f) ? 0.0f : (v > 1.0f) ? 1.0f : v;
    }

    // Xorshift32 PRNG: tiny, fast, deterministic.
    inline uint32_t nextU32() {
        uint32_t x = rngState;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        rngState = x;
        return x;
    }

    inline float rand01() {
        // 24-bit mantissa-ish fraction in [0,1).
        return (float)((nextU32() >> 8) & 0x00FFFFFFu) / (float)0x01000000u;
    }

    inline bool selectPressed(ControllerPtr ctl) const {
        if (!ctl) return false;
        // Prefer dedicated accessors when present.
        if (InputDetail::back(ctl, 0)) return true;
        if (InputDetail::select(ctl, 0)) return true;
        // Fallback to miscButtons() bitmask.
        const uint16_t misc = InputDetail::miscButtons(ctl, 0);
        return (misc & MVisualAppConfig::MISC_SELECT_MASK) != 0;
    }

    void handleInput(ControllerManager* input, uint32_t now) {
        ControllerPtr p1 = input ? input->getController(0) : nullptr;
        if (!p1) return;

        // ----------------------
        // Up/Down => bar count (repeat)
        // ----------------------
        const uint8_t d = p1->dpad();
        const bool up = (d & 0x01) != 0;
        const bool down = (d & 0x02) != 0;
        const bool prevUp = (prevDpad & 0x01) != 0;
        const bool prevDown = (prevDpad & 0x02) != 0;

        int deltaBars = 0;
        if (up || down) {
            if ((up && !prevUp) || (down && !prevDown)) {
                // Edge press.
                deltaBars = up ? 1 : -1;
                dpadHoldStartMs = now;
                dpadLastRepeatMs = now;
            } else if (dpadHoldStartMs != 0 &&
                       (uint32_t)(now - dpadHoldStartMs) >= MVisualAppConfig::DPAD_REPEAT_DELAY_MS &&
                       (uint32_t)(now - dpadLastRepeatMs) >= MVisualAppConfig::DPAD_REPEAT_INTERVAL_MS) {
                // Held repeat.
                deltaBars = up ? 1 : -1;
                dpadLastRepeatMs = now;
            }
        } else {
            dpadHoldStartMs = 0;
        }

        if (deltaBars != 0) {
            const int nb = (int)bars + deltaBars;
            bars = (uint8_t)constrain(nb, (int)MVisualAppConfig::BAR_COUNT_MIN, (int)MVisualAppConfig::BAR_COUNT_MAX);
        }
        prevDpad = d;

        // ----------------------
        // A => mono+gradient mode + cycle base color (edge-triggered)
        // ----------------------
        const bool aNow = (bool)p1->a();
        if (aNow && !lastA) {
            colorMode = MODE_MONO_GRADIENT;
            monoColorIndex = (uint8_t)((monoColorIndex + 1) % MVisualAppConfig::MONO_COLOR_COUNT);
        }
        lastA = aNow;

        // ----------------------
        // B => cycle rainbow effects (edge-triggered)
        // ----------------------
        const bool bNow = (bool)p1->b();
        if (bNow && !lastB) {
            if (colorMode != MODE_RAINBOW) {
                colorMode = MODE_RAINBOW;
                rainbowEffectIndex = 0;
            } else {
                rainbowEffectIndex = (uint8_t)((rainbowEffectIndex + 1) % MVisualAppConfig::RAINBOW_EFFECT_COUNT);
            }
        }
        lastB = bNow;

        // ----------------------
        // X => cycle visualization type (edge-triggered)
        // Bars -> Lines -> Dots -> Bars
        // ----------------------
        const bool xNow = (bool)p1->x();
        if (xNow && !lastX) {
            vizMode = (VizMode)(((uint8_t)vizMode + 1) % 3);
        }
        lastX = xNow;

        // ----------------------
        // Select/Back => toggle HUD (edge-triggered)
        // ----------------------
        const bool selNow = selectPressed(p1);
        if (selNow && !lastSelect) {
            hudHidden = !hudHidden;
        }
        lastSelect = selNow;

        // ----------------------
        // Y => cycle shading modes (edge-triggered)
        // Off -> Horizontal -> Vertical -> Off
        // ----------------------
        const bool yNow = (bool)p1->y();
        if (yNow && !lastY) {
            shadingMode = (ShadingMode)(((uint8_t)shadingMode + 1) % 3);
        }
        lastY = yNow;
    }

    // -------------------------------------------------------------------------
    // Noise spectrum generation (placeholder for mic FFT)
    // -------------------------------------------------------------------------
    void updateNoiseSpectrum(uint32_t now) {
        // 1) Per-bin random impulses + decay
        // Bias toward lower values, with occasional spikes.
        for (int i = 0; i < 64; i++) {
            const float r = rand01();
            const float spike = r * r; // quadratic bias (more lows, some highs)
            const float impulse = clamp01(spike * MVisualAppConfig::NOISE_IMPULSE_GAIN);

            // Mild "moving comb" to feel like bands shifting.
            const float t = (float)now * 0.001f;
            const float band = 0.65f + 0.35f * sinf((float)i * 0.28f + t * 2.0f);

            const float target = clamp01(impulse * band);
            const float decayed = spectrum64[i] * MVisualAppConfig::NOISE_DECAY;
            spectrum64[i] = (target > decayed) ? target : decayed;
        }

        // 2) Neighbor smoothing (makes it look more like a spectrum)
        smoothSpectrum64();
    }

    void smoothSpectrum64() {
        const float a = clamp01(MVisualAppConfig::NOISE_SMOOTH);
        if (a <= 0.0f) return;

        spectrumTmp64[0] = spectrum64[0];
        spectrumTmp64[63] = spectrum64[63];

        for (int i = 1; i < 63; i++) {
            // Simple 1D blur.
            const float blur = (spectrum64[i - 1] + 2.0f * spectrum64[i] + spectrum64[i + 1]) * 0.25f;
            spectrumTmp64[i] = (1.0f - a) * spectrum64[i] + a * blur;
        }

        for (int i = 0; i < 64; i++) {
            spectrum64[i] = clamp01(spectrumTmp64[i]);
        }
    }

    // -------------------------------------------------------------------------
    // Rendering helpers
    // -------------------------------------------------------------------------
    void drawHud(MatrixPanel_I2S_DMA* d) {
        // Common HUD divider (dotted).
        for (int x = 0; x < PANEL_RES_X; x += 2) d->drawPixel(x, MVisualAppConfig::HUD_H - 1, COLOR_BLUE);

        SmallFont::drawString(d, 2, 6, "MVIS", COLOR_CYAN);
        SmallFont::drawStringF(d, 32, 6, COLOR_YELLOW, "B%02d", (int)bars);

        if (colorMode == MODE_MONO_GRADIENT) {
            SmallFont::drawStringF(d, 48, 6, COLOR_WHITE, "M%d", (int)monoColorIndex + 1);
        } else {
            SmallFont::drawStringF(d, 48, 6, COLOR_WHITE, "R%d", (int)rainbowEffectIndex + 1);
        }
        const char sc = (shadingMode == SHADING_HORIZONTAL) ? 'H' : (shadingMode == SHADING_VERTICAL) ? 'V' : ' ';
        char sbuf[2] = { sc, '\0' };
        SmallFont::drawString(d, 58, 6, sbuf, COLOR_WHITE);

        const char vc = (vizMode == VIZ_BARS) ? 'B' : (vizMode == VIZ_LINES) ? 'L' : 'D';
        char vbuf[2] = { vc, '\0' };
        SmallFont::drawString(d, 26, 6, vbuf, COLOR_WHITE);
    }

    static inline uint16_t scale565(uint16_t c, uint8_t mul /*0..255*/) {
        const uint8_t r5 = (uint8_t)((c >> 11) & 0x1F);
        const uint8_t g6 = (uint8_t)((c >> 5) & 0x3F);
        const uint8_t b5 = (uint8_t)(c & 0x1F);

        const uint8_t r = (uint8_t)((r5 * mul) / 255);
        const uint8_t g = (uint8_t)((g6 * mul) / 255);
        const uint8_t b = (uint8_t)((b5 * mul) / 255);

        return (uint16_t)((r << 11) | (g << 5) | b);
    }

    static inline uint16_t wheel565(uint8_t pos) {
        uint8_t r = 0, g = 0, b = 0;
        if (pos < 85) {
            r = (uint8_t)(255 - pos * 3);
            g = (uint8_t)(pos * 3);
            b = 0;
        } else if (pos < 170) {
            pos = (uint8_t)(pos - 85);
            r = 0;
            g = (uint8_t)(255 - pos * 3);
            b = (uint8_t)(pos * 3);
        } else {
            pos = (uint8_t)(pos - 170);
            r = (uint8_t)(pos * 3);
            g = 0;
            b = (uint8_t)(255 - pos * 3);
        }
        return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }

    static inline uint16_t lerp565(uint16_t c0, uint16_t c1, uint8_t t255) {
        const uint8_t r0 = (uint8_t)((c0 >> 11) & 0x1F);
        const uint8_t g0 = (uint8_t)((c0 >> 5) & 0x3F);
        const uint8_t b0 = (uint8_t)(c0 & 0x1F);

        const uint8_t r1 = (uint8_t)((c1 >> 11) & 0x1F);
        const uint8_t g1 = (uint8_t)((c1 >> 5) & 0x3F);
        const uint8_t b1 = (uint8_t)(c1 & 0x1F);

        const uint16_t r = (uint16_t)(r0 + (((int)r1 - (int)r0) * (int)t255) / 255);
        const uint16_t g = (uint16_t)(g0 + (((int)g1 - (int)g0) * (int)t255) / 255);
        const uint16_t b = (uint16_t)(b0 + (((int)b1 - (int)b0) * (int)t255) / 255);

        return (uint16_t)((r << 11) | (g << 5) | b);
    }

    uint16_t colorForPixel(int x, int /*y*/, int /*yTop*/, int /*yBottom*/, uint8_t timeHue, uint8_t barIndex, int yyFromBottom, int barH) const {
        if (colorMode == MODE_MONO_GRADIENT) {
            const uint16_t base = MVisualAppConfig::MONO_BASE_COLORS[monoColorIndex % MVisualAppConfig::MONO_COLOR_COUNT];
            uint16_t c = base;
            c = applyShading(c, x, barIndex, yyFromBottom, barH);
            return c;
        }

        switch (rainbowEffectIndex % MVisualAppConfig::RAINBOW_EFFECT_COUNT) {
            default:
            case 0: {
                const uint8_t hue = (uint8_t)(barIndex * (256 / max(1, (int)bars)));
                return applyShading(wheel565(hue), x, barIndex, yyFromBottom, barH);
            }
            case 1: {
                const uint8_t t = (barH <= 0) ? 0 : (uint8_t)((255 * yyFromBottom) / barH);
                uint16_t c = lerp565(COLOR_BLUE, COLOR_RED, t);
                return applyShading(c, x, barIndex, yyFromBottom, barH);
            }
            case 2: {
                const uint8_t hue = (uint8_t)(timeHue + (uint8_t)(barIndex * (256 / max(1, (int)bars))));
                return applyShading(wheel565(hue), x, barIndex, yyFromBottom, barH);
            }
        }
    }

    uint16_t applyShading(uint16_t c, int x, uint8_t barIndex, int yyFromBottom, int barH) const {
        if (shadingMode == SHADING_OFF) return c;

        if (shadingMode == SHADING_VERTICAL) {
            const uint8_t mul = (barH <= 0) ? 255 : (uint8_t)(255 - (127 * yyFromBottom) / barH);
            return scale565(c, mul);
        }

        const int startX = ((int)barIndex * PANEL_RES_X) / (int)bars;
        const int endX = (((int)barIndex + 1) * PANEL_RES_X) / (int)bars - 1;
        const int w = max(1, endX - startX + 1);
        const int lx = constrain(x - startX, 0, w - 1);
        const uint8_t mul = (w <= 1) ? 255 : (uint8_t)(255 - (127 * lx) / (w - 1));
        return scale565(c, mul);
    }

    // -------------------------------------------------------------------------
    // Visualization renderers
    // -------------------------------------------------------------------------
    void drawBars(MatrixPanel_I2S_DMA* display, int yTop, int yBottom, int barAreaH, uint8_t timeHue) const {
        for (int x = 0; x < PANEL_RES_X; x++) {
            const int bi = (x * (int)bars) / PANEL_RES_X;
            const float v = barValue[bi];
            const int h = (int)lroundf(v * (float)(barAreaH - 1));
            if (h <= 0) continue;

            for (int yy = 0; yy <= h; yy++) {
                const int y = yBottom - yy;
                if (y < yTop) break;
                const uint16_t c = colorForPixel(x, y, yTop, yBottom, timeHue, (uint8_t)bi, yy, h);
                display->drawPixel(x, y, c);
            }
        }
    }

    void drawLines(MatrixPanel_I2S_DMA* display, int yTop, int yBottom, int barAreaH, uint8_t timeHue) const {
        if (bars <= 0) return;

        int prevX = -1;
        int prevY = -1;
        for (int i = 0; i < (int)bars; i++) {
            const int startX = (i * PANEL_RES_X) / (int)bars;
            const int endX = (((i + 1) * PANEL_RES_X) / (int)bars) - 1;
            const int cx = (startX + endX) / 2;

            const float v = barValue[i];
            const int h = (int)lroundf(v * (float)(barAreaH - 1));
            const int y = yBottom - h;
            if (y < yTop) continue;

            if (prevX >= 0) {
                const int midX = (prevX + cx) / 2;
                const int midY = (prevY + y) / 2;
                const int yyFromBottom = yBottom - midY;
                const uint16_t c = colorForPixel(midX, midY, yTop, yBottom, timeHue, (uint8_t)i, yyFromBottom, max(1, barAreaH - 1));
                display->drawLine(prevX, prevY, cx, y, c);
            }
            prevX = cx;
            prevY = y;
        }
    }

    void drawDots(MatrixPanel_I2S_DMA* display, int yTop, int yBottom, int /*barAreaH*/, uint8_t timeHue) const {
        if (bars <= 0) return;
        for (int i = 0; i < (int)bars; i++) {
            const int startX = (i * PANEL_RES_X) / (int)bars;
            const int endX = (((i + 1) * PANEL_RES_X) / (int)bars) - 1;
            const int cx = (startX + endX) / 2;

            const float v = barValue[i];
            const int h = (int)lroundf(v * (float)(PANEL_RES_Y - 1));
            const int y = (PANEL_RES_Y - 1) - h;
            if (y < yTop) continue;

            const int yyFromBottom = yBottom - y;
            const uint16_t c = colorForPixel(cx, y, yTop, yBottom, timeHue, (uint8_t)i, yyFromBottom, max(1, PANEL_RES_Y - 1));
            display->drawPixel(cx, y, c);
        }
    }
};


