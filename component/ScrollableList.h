#pragma once
#include <Arduino.h>
#include <math.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../engine/ControllerManager.h"
#include "../engine/AudioManager.h"
#include "SmallFont.h"

/**
 * ScrollableList
 * --------------
 * Reusable, smooth-scrolling list widget for 64x64 UI screens.
 *
 * Goals:
 * - One implementation of: selection, scrolling, smooth scroll animation,
 *   analog + D-pad navigation with sensible repeat behavior.
 * - Usable by: main menu, settings, leaderboard, and any future game/applet menu.
 *
 * How to use:
 * - Implement `ListModel` (or create a small adapter class) for your items.
 * - Keep one `ScrollableList` instance per screen so it owns its own input state.
 * - Call `update()` each loop, and `draw()` whenever you render.
 */

class ListModel {
public:
    virtual ~ListModel() {}

    /** Total number of *actual* items (before visibility filtering). */
    virtual int itemCount() const = 0;

    /** Per-item visibility (default = visible). */
    virtual bool isItemVisible(int actualIndex) const { (void)actualIndex; return true; }

    /** Label string for the actual item index. */
    virtual const char* label(int actualIndex) const = 0;
};

class ScrollableList {
public:
    // Selection is expressed in *actual* indices (not visible indices).
    int selectedActual = 0;

    // Target scroll offset, in visible rows.
    int scrollOffset = 0;

    // Animated scroll position (float rows).
    float scrollPos = 0.0f;

    // Layout defaults tuned for 64x64 with an 8px HUD band.
    struct Layout {
        int hudH;
        int visibleRows;      // 7 * 8px + 8px HUD = 64px
        int rowStepPx;
        int markerX;
        int labelX;
        int baseY;           // baseline Y of first row; -1 => auto (hudH + 6)
        int arrowX;
        int upArrowY;        // -1 => auto (hudH + 1)
        int downArrowY;

        Layout()
            : hudH(8),
              visibleRows(7),
              rowStepPx(8),
              markerX(2),
              labelX(6),
              baseY(-1),
              arrowX(60),
              upArrowY(-1),
              downArrowY(62) {}
    };

    struct Colors {
        uint16_t selected;
        uint16_t normal;
        uint16_t marker;
        uint16_t arrows;

        Colors()
            : selected(COLOR_GREEN),
              normal(COLOR_WHITE),
              marker(COLOR_GREEN),
              arrows(COLOR_WHITE) {}
    };

    struct InputConfig {
        // Smooth scrolling
        float scrollSmooth; // 0..1 (higher = snappier)

        // Analog behavior
        float stickDeadzone;
        int16_t axisDivisor;

        // D-pad repeat (press once + repeat after hold)
        uint16_t dpadRepeatDelayMs;
        uint16_t dpadRepeatIntervalMs;

        // A button debounce
        uint16_t selectDebounceMs;

        InputConfig()
            : scrollSmooth(0.18f),
              stickDeadzone(0.22f),
              axisDivisor(512),
              dpadRepeatDelayMs(450),
              dpadRepeatIntervalMs(180),
              selectDebounceMs(200) {}
    };

    // Optional hook: draw a value on the right side of a row (e.g. brightness value).
    typedef void (*DrawRightFn)(MatrixPanel_I2S_DMA* d, int actualIndex, int yBaseline, bool isSelected, void* user);

    ScrollableList() = default;

    /**
     * Draw the list using the provided model.
     * NOTE: Caller is expected to clear the screen and draw HUD separately.
     */
    void draw(MatrixPanel_I2S_DMA* d,
              const ListModel& model,
              const Layout& layout = Layout(),
              const Colors& colors = Colors(),
              DrawRightFn drawRight = nullptr,
              void* user = nullptr) {
        const int visibleCount = getVisibleCount(model);
        if (visibleCount <= 0) return;

        // Clamp selection.
        if (!isActualIndexVisible(model, selectedActual)) {
            // Snap to first visible item.
            selectedActual = getActualIndexFromVisible(model, 0);
        }

        // Update scroll target so selection stays in range.
        const int visibleSelected = getVisibleIndexFromActual(model, selectedActual);
        const int maxVisible = layout.visibleRows;
        if (visibleSelected < scrollOffset) scrollOffset = visibleSelected;
        if (visibleSelected >= scrollOffset + maxVisible) scrollOffset = visibleSelected - maxVisible + 1;

        // Smooth scroll towards target.
        scrollPos = scrollPos + ((float)scrollOffset - scrollPos) * cfg.scrollSmooth;

        const int baseY = (layout.baseY >= 0) ? layout.baseY : (layout.hudH + 6);
        const int listTop = baseY;
        const int listBottom = baseY + (layout.visibleRows - 1) * layout.rowStepPx;

        // Ensure selected row is always visible even if animation lags.
        const float selY = (float)baseY + ((float)visibleSelected - scrollPos) * (float)layout.rowStepPx;
        if ((int)selY < listTop || (int)selY > listBottom) {
            scrollPos = (float)scrollOffset;
        }

        // Draw visible items.
        int visibleIdx = 0;
        for (int actual = 0; actual < model.itemCount(); actual++) {
            if (!model.isItemVisible(actual)) continue;

            const float yF = (float)baseY + ((float)visibleIdx - scrollPos) * (float)layout.rowStepPx;
            const int y = (int)yF;
            if (y < listTop || y > listBottom) {
                visibleIdx++;
                continue;
            }

            const bool isSel = (actual == selectedActual);
            SmallFont::drawChar(d, layout.markerX, y, isSel ? '>' : ' ', colors.marker);
            // Prevent label overflow on a 64px wide screen by truncating long labels.
            // TomThumb is a tiny proportional font, but a safe approximation is ~4px per character.
            const int maxPx = max(0, layout.arrowX - layout.labelX - 1);
            const int maxChars = max(1, maxPx / 4);
            const char* raw = model.label(actual);
            char buf[32];
            buf[0] = '\0';
            if (raw) {
                const int rawLen = (int)strlen(raw);
                if (rawLen <= maxChars) {
                    strncpy(buf, raw, sizeof(buf) - 1);
                    buf[sizeof(buf) - 1] = '\0';
                } else {
                    // Leave room for ".."
                    const int keep = max(0, min(maxChars - 2, (int)sizeof(buf) - 3));
                    strncpy(buf, raw, (size_t)keep);
                    buf[keep] = '.';
                    buf[keep + 1] = '.';
                    buf[keep + 2] = '\0';
                }
            }
            SmallFont::drawString(d, layout.labelX, y, buf, isSel ? colors.selected : colors.normal);

            if (drawRight) drawRight(d, actual, y, isSel, user);

            visibleIdx++;
        }

        // Scroll indicators.
        const int upY = (layout.upArrowY >= 0) ? layout.upArrowY : (layout.hudH + 1);
        if (scrollOffset > 0) {
            d->drawPixel(layout.arrowX, upY, colors.arrows);
            d->drawPixel(layout.arrowX - 1, upY + 1, colors.arrows);
            d->drawPixel(layout.arrowX + 1, upY + 1, colors.arrows);
        }
        if (scrollOffset + maxVisible < visibleCount) {
            d->drawPixel(layout.arrowX, layout.downArrowY, colors.arrows);
            d->drawPixel(layout.arrowX - 1, layout.downArrowY - 1, colors.arrows);
            d->drawPixel(layout.arrowX + 1, layout.downArrowY - 1, colors.arrows);
        }
    }

    /**
     * Update navigation using controller 0 (analog + D-pad).
     *
     * Returns:
     * - The *actual* selected index when A is pressed (debounced)
     * - -1 otherwise
     */
    int update(ControllerManager* input, const ListModel& model) {
        return updateForPad(input, model, 0);
    }

    /**
     * Update navigation using the specified controller index (analog + D-pad).
     *
     * Why: Some screens are controller-specific (e.g. per-controller user select,
     * pause menu invoked by a particular controller).
     *
     * Returns:
     * - The *actual* selected index when A is pressed (debounced)
     * - -1 otherwise
     */
    int updateForPad(ControllerManager* input, const ListModel& model, uint8_t padIndex) {
        ControllerPtr ctl = (input != nullptr) ? input->getController((int)padIndex) : nullptr;
        if (!ctl) return -1;

        const uint32_t now = (uint32_t)millis();
        const uint8_t dpad = ctl->dpad();

        // --- D-pad Up/Down (edge press + hold repeat) ---
        int navDir = 0;
        const bool dUp = (dpad & 0x01) != 0;
        const bool dDown = (dpad & 0x02) != 0;
        const bool prevUp = (prevDpad & 0x01) != 0;
        const bool prevDown = (prevDpad & 0x02) != 0;
        prevDpad = dpad;

        if (dUp || dDown) {
            if ((dUp && !prevUp) || (dDown && !prevDown)) {
                navDir = dUp ? -1 : 1;
                dpadHoldStartMs = now;
                lastDpadRepeatMs = now;
            } else {
                if (dpadHoldStartMs != 0 &&
                    (uint32_t)(now - dpadHoldStartMs) >= cfg.dpadRepeatDelayMs &&
                    (uint32_t)(now - lastDpadRepeatMs) >= cfg.dpadRepeatIntervalMs) {
                    navDir = dUp ? -1 : 1;
                    lastDpadRepeatMs = now;
                }
            }
        } else {
            dpadHoldStartMs = 0;
        }

        // --- Analog (only when D-pad isn't held) ---
        if (navDir == 0 && !(dUp || dDown)) {
            const float rawY = clampf((float)InputDetail::axisY(ctl, 0) / (float)cfg.axisDivisor, -1.0f, 1.0f);
            const float sy = deadzone01(rawY, cfg.stickDeadzone);
            if (sy < 0) navDir = -1;
            else if (sy > 0) navDir = 1;

            if (navDir != 0) {
                const float mag = fabsf(sy);
                const uint32_t interval = (uint32_t)(320.0f - 160.0f * mag); // ~320ms..160ms
                if ((uint32_t)(now - lastAnalogMoveMs) > interval) lastAnalogMoveMs = now;
                else navDir = 0;
            } else {
                lastAnalogMoveMs = 0;
            }
        }

        if (navDir != 0) {
            const int before = selectedActual;
            moveSelection(model, navDir);
            if (selectedActual != before) {
                // Distinct UI sounds (menus/lists):
                // - Up: higher tick
                // - Down: lower tick
                if (navDir < 0) globalAudio.uiUp();
                else globalAudio.uiDown();
                #if DEBUG_AUDIO
                Serial.print(F("[UI] list selection "));
                Serial.print(before);
                Serial.print(F(" -> "));
                Serial.println(selectedActual);
                #endif
            }
        }

        // Select with A button (debounced).
        if (ctl->a() && (uint32_t)(now - lastSelectMs) > cfg.selectDebounceMs) {
            lastSelectMs = now;
            globalAudio.uiConfirmShoot();
            return selectedActual;
        }

        return -1;
    }

    InputConfig cfg;

private:
    // Per-instance input state (do NOT make static; we want multiple independent lists).
    uint8_t prevDpad = 0;
    uint32_t dpadHoldStartMs = 0;
    uint32_t lastDpadRepeatMs = 0;
    uint32_t lastAnalogMoveMs = 0;
    uint32_t lastSelectMs = 0;

    // Bluepad32 analog helper (SFINAE) so we don't hard-depend on a single API surface.
    struct InputDetail {
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

    int getVisibleCount(const ListModel& model) const {
        int c = 0;
        for (int i = 0; i < model.itemCount(); i++) if (model.isItemVisible(i)) c++;
        return c;
    }

    bool isActualIndexVisible(const ListModel& model, int actual) const {
        if (actual < 0 || actual >= model.itemCount()) return false;
        return model.isItemVisible(actual);
    }

    int getActualIndexFromVisible(const ListModel& model, int visibleIndex) const {
        int v = 0;
        for (int i = 0; i < model.itemCount(); i++) {
            if (!model.isItemVisible(i)) continue;
            if (v == visibleIndex) return i;
            v++;
        }
        return 0;
    }

    int getVisibleIndexFromActual(const ListModel& model, int actualIndex) const {
        int v = 0;
        for (int i = 0; i < actualIndex; i++) {
            if (model.isItemVisible(i)) v++;
        }
        return v;
    }

    void moveSelection(const ListModel& model, int dir) {
        if (dir == 0) return;
        const int visibleCount = getVisibleCount(model);
        if (visibleCount <= 0) return;

        int curVisible = getVisibleIndexFromActual(model, selectedActual);

        if (dir < 0 && curVisible > 0) {
            for (int i = selectedActual - 1; i >= 0; i--) {
                if (model.isItemVisible(i)) { selectedActual = i; break; }
            }
        } else if (dir > 0 && curVisible < visibleCount - 1) {
            for (int i = selectedActual + 1; i < model.itemCount(); i++) {
                if (model.isItemVisible(i)) { selectedActual = i; break; }
            }
        }
    }
};


