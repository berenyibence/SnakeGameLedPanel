#pragma once
#include <Arduino.h>
#include <math.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../engine/ControllerManager.h"
#include "../component/SmallFont.h"
#include "../engine/Settings.h"
#include "../component/ScrollableList.h"

class Menu : public ListModel {
public:
    // Main menu options (actual indices). Keep Settings LAST (engine treats it specially).
    const char* options[18] = { "Snake", "Tron", "Pong", "Breakout", "Shooter", "Labyrinth", "Tetris", "Asteroids", "Music", "MVisual", "Bomber", "Simon", "Dino", "Mines", "Matrix", "Lava", "Leaderboard", "Settings" };
    static const int NUM_OPTIONS = 18;

    // Reusable list widget state (selection + scrolling + input).
    ScrollableList list;

    // HUD layout
    static constexpr int HUD_H = 8;

    // Context for visibility filtering (set at draw/update time).
    int playersContext = 0;
    
    // -----------------------------------------------------
    // ListModel (for ScrollableList)
    // -----------------------------------------------------
    int itemCount() const override { return NUM_OPTIONS; }
    const char* label(int actualIndex) const override { return options[actualIndex]; }
    bool isItemVisible(int index) const override { return isOptionVisible(index, playersContext); }
    
    // Check if option should be visible
    bool isOptionVisible(int index, int players) const {
        if (index == 6) {  // Tetris
            return players == 1;
        }
        if (index == 7) {  // Asteroids
            return players == 1;
        }
        // All others always visible
        return true;  // All other options always visible
    }

    void draw(MatrixPanel_I2S_DMA* d, ControllerManager* input) {
        const int players = (input != nullptr) ? input->getConnectedCount() : 0;
        playersContext = players;
        d->fillScreen(0);
        
        // ----------------------
        // HUD: "MENU" + player icons (P1..P4)
        // ----------------------
        SmallFont::drawString(d, 2, 6, "MENU", COLOR_CYAN);
        for (int x = 0; x < PANEL_RES_X; x += 2) d->drawPixel(x, HUD_H - 1, COLOR_BLUE);

        const uint16_t pColors[MAX_GAMEPADS] = {
            globalSettings.getPlayerColor(),
            COLOR_CYAN,
            COLOR_ORANGE,
            COLOR_PURPLE
        };
        const uint16_t offC = d->color565(90, 90, 90);

        // P1..P4 tokens: flush-right, no extra gaps.
        // We intentionally draw tokens with a tiny overlap to avoid perceived "spaces" between them.
        d->setFont(&TomThumb);
        int16_t x1 = 0, y1 = 0;
        uint16_t w = 0, h = 0;
        d->getTextBounds("P4", 0, 0, &x1, &y1, &w, &h);
        const int tokenW = (int)w;
        static constexpr int TOKEN_OVERLAP = 1; // overlap by 1px between tokens
        const int totalW = MAX_GAMEPADS * tokenW - (MAX_GAMEPADS - 1) * TOKEN_OVERLAP;
        // Fully flush-right: right edge at PANEL_RES_X.
        int px = PANEL_RES_X - totalW;
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            const bool connected = (input && input->getController(i) != nullptr);
            SmallFont::drawStringF(d, px, 6, connected ? pColors[i] : offC, "P%d", i + 1);
            px += tokenW - TOKEN_OVERLAP;
        }

        // Draw list below HUD using the reusable widget.
        ScrollableList::Layout lay;
        lay.hudH = HUD_H;
        lay.visibleRows = 7;
        list.selectedActual = constrain(list.selectedActual, 0, NUM_OPTIONS - 1);
        list.draw(d, *this, lay);
    }

    int update(ControllerManager* input) {
        if (!input) return -1;
        playersContext = input->getConnectedCount();
        const int sel = list.update(input, *this);

        // Cycle player color with Y button (debounced)
        ControllerPtr ctl = input->getController(0);
        const unsigned long now = millis();
        static unsigned long lastColorChange = 0;
        // Bluepad32 exposes ABXY on most pads; if a controller doesn't have Y, this stays false.
        if (ctl && ctl->y() && (now - lastColorChange > 200)) {
            lastColorChange = now;
            globalSettings.cyclePlayerColor(1);
            globalSettings.save();
        }

        if (sel != -1) return sel;
        return -1;
    }
};
