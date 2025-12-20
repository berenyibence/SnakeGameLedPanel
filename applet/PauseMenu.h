#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../engine/ControllerManager.h"
#include "../component/SmallFont.h"
#include "../component/ScrollableList.h"

/**
 * PauseMenu
 * --------
 * Reusable pause screen intended to be used by ANY game.
 *
 * Input (per target pad):
 * - D-pad: navigate
 * - A: confirm
 * - B: quick resume
 *
 * Actions are returned to the caller so the engine (sketch) can decide how to
 * transition (resume game, open user select, or quit to main menu).
 */
class PauseMenu {
public:
    static constexpr int HUD_H = 8;
    static constexpr int MODAL_PAD_PX = 4;

    enum Action : uint8_t {
        ACTION_NONE = 0,
        ACTION_RESUME,
        ACTION_QUIT_TO_MENU
    };

    void beginForPad(uint8_t padIndexIn) {
        targetPad = padIndexIn;
        list.selectedActual = 0;
    }

    void draw(MatrixPanel_I2S_DMA* d) {
        // NOTE: caller typically draws the underlying game first, then calls this.
        // We draw a simple overlay on top.
        // 1) HUD title (keeps the "paused" state obvious and reuses our UI convention)
        d->fillRect(0, 0, PANEL_RES_X, HUD_H, COLOR_BLACK);
        SmallFont::drawString(d, 2, 6, "PAUSED", COLOR_YELLOW);
        for (int x = 0; x < PANEL_RES_X; x += 2) d->drawPixel(x, HUD_H - 1, COLOR_BLUE);

        // 2) Smallest centered modal containing only the 2 options.
        // TomThumb is tiny; approximate text width using a 4px per character stride.
        // (Good enough for fixed labels like "RESUME" / "QUIT".)
        static constexpr int CHAR_W = 4;
        const int maxLabelChars = 6; // max(strlen("RESUME"), strlen("QUIT"))
        const int contentW = (1 * CHAR_W) /* marker */ + (1 * CHAR_W) /* gap */ + (maxLabelChars * CHAR_W);
        const int modalW = MODAL_PAD_PX * 2 + contentW;
        const int modalH = MODAL_PAD_PX * 2 + (2 * 8); // 2 rows, 8px step

        const int usableH = PANEL_RES_Y - HUD_H;
        const int modalX = (PANEL_RES_X - modalW) / 2;
        const int modalY = HUD_H + (usableH - modalH) / 2;

        d->fillRect(modalX, modalY, modalW, modalH, COLOR_BLACK);
        d->drawRect(modalX, modalY, modalW, modalH, COLOR_BLUE);

        ScrollableList::Layout lay;
        lay.hudH = 0; // unused when baseY is set explicitly
        lay.visibleRows = 2;
        lay.markerX = modalX + MODAL_PAD_PX;
        lay.labelX = modalX + MODAL_PAD_PX + CHAR_W; // one char gap after marker
        lay.baseY = modalY + MODAL_PAD_PX + 6;       // baseline (TomThumb) + padding
        lay.arrowX = modalX + modalW - 4;            // arrows effectively off (no scrolling)
        lay.upArrowY = modalY + 2;
        lay.downArrowY = modalY + modalH - 2;
        list.draw(d, model, lay);
    }

    Action update(ControllerManager* input) {
        ControllerPtr ctl = input ? input->getController(targetPad) : nullptr;
        if (!ctl) return ACTION_NONE;

        // Quick resume on B (debounced in ScrollableList already, but we keep it explicit).
        if (ctl->b()) return ACTION_RESUME;

        const int sel = list.updateForPad(input, model, targetPad);
        if (sel == -1) return ACTION_NONE;

        switch (sel) {
            case 0: return ACTION_RESUME;
            case 1: return ACTION_QUIT_TO_MENU;
            default: return ACTION_NONE;
        }
    }

    uint8_t pad() const { return targetPad; }

private:
    uint8_t targetPad = 0;

    class PauseModel : public ListModel {
    public:
        int itemCount() const override { return 2; }
        const char* label(int actualIndex) const override {
            switch (actualIndex) {
                case 0: return "RESUME";
                case 1: return "QUIT";
                default: return "";
            }
        }
    } model;

    ScrollableList list;
};


