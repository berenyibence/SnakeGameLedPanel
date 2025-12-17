#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "ControllerManager.h"
#include "SmallFont.h"
#include "ScrollableList.h"
#include "Leaderboard.h"

/**
 * LeaderboardMenu
 * ---------------
 * Applet-style screen (host-managed state) for browsing per-game high scores.
 *
 * Controls:
 * - Up/Down: navigate
 * - A: select / enter
 * - B: back (from scores -> game list), or exit leaderboard (from game list)
 */
class LeaderboardMenu {
public:
    // HUD layout
    static constexpr int HUD_H = 8;

    LeaderboardMenu() = default;

    void draw(MatrixPanel_I2S_DMA* display, ControllerManager* input) {
        (void)input;
        display->fillScreen(0);

        // Common HUD divider
        for (int x = 0; x < PANEL_RES_X; x += 2) display->drawPixel(x, HUD_H - 1, COLOR_BLUE);

        if (screen == SCREEN_GAMES) {
            SmallFont::drawString(display, 2, 6, "LEADERBD", COLOR_CYAN);
            drawGames(display);
        } else {
            drawScores(display);
        }
    }

    /**
     * Returns true if the user wants to exit back to the main menu.
     */
    bool update(ControllerManager* input) {
        ControllerPtr ctl = input ? input->getController(0) : nullptr;
        if (!ctl) return false;

        const unsigned long now = millis();

        // Global back behavior within this applet.
        static unsigned long lastB = 0;
        if (ctl->b() && (now - lastB > 200)) {
            lastB = now;
            if (screen == SCREEN_SCORES) {
                screen = SCREEN_GAMES;
                return false;
            }
            return true; // exit leaderboard
        }

        if (screen == SCREEN_GAMES) {
            const int selActual = gamesList.update(input, gamesModel);
            selectedGame = gamesList.selectedActual;
            if (selActual != -1) {
                // Enter score view (selectedGame is an actual index into [0..gameCount-1])
                screen = SCREEN_SCORES;
                scoresList.selectedActual = 0;
            }
        } else {
            // Score list: just allow scrolling if needed (TOP_SCORES is small), and A is ignored.
            (void)scoresList.update(input, scoresModel);
        }

        return false;
    }

private:
    enum Screen : uint8_t { SCREEN_GAMES, SCREEN_SCORES };
    Screen screen = SCREEN_GAMES;

    // -----------------------
    // Game selection list
    // -----------------------
    ScrollableList gamesList;
    int selectedGame = 0;

    class GamesModel : public ListModel {
    public:
        int itemCount() const override { return (int)Leaderboard::gameCount(); }
        const char* label(int actualIndex) const override {
            const Leaderboard::Entry* e = Leaderboard::entryAt((uint8_t)actualIndex);
            return (e && e->name[0]) ? e->name : "UNKNOWN";
        }
    } gamesModel;

    // -----------------------
    // Scores list (for selected game)
    // -----------------------
    ScrollableList scoresList;
    // Needs to fit: "5 ABC 4294967295" + NUL = 1+1+3+1+10 +1 = 17 (plus safety).
    char scoreLabels[Leaderboard::TOP_SCORES][24] = {};

    class ScoresModel : public ListModel {
    public:
        const LeaderboardMenu* owner = nullptr;
        explicit ScoresModel(const LeaderboardMenu* o = nullptr) : owner(o) {}

        int itemCount() const override { return (int)Leaderboard::TOP_SCORES; }
        const char* label(int actualIndex) const override {
            if (!owner) return "";
            if (actualIndex < 0 || actualIndex >= (int)Leaderboard::TOP_SCORES) return "";
            return owner->scoreLabels[actualIndex];
        }
    } scoresModel{this};

    void drawGames(MatrixPanel_I2S_DMA* display) {
        const int count = (int)Leaderboard::gameCount();
        if (count <= 0) {
            SmallFont::drawString(display, 8, HUD_H + 18, "NO SCORES", COLOR_WHITE);
            SmallFont::drawString(display, 8, HUD_H + 28, "PLAY GAME", COLOR_WHITE);
            return;
        }

        gamesList.selectedActual = constrain(selectedGame, 0, count - 1);

        ScrollableList::Layout lay;
        lay.hudH = HUD_H;
        lay.visibleRows = 7;
        gamesList.draw(display, gamesModel, lay);

        selectedGame = gamesList.selectedActual;
    }

    void drawScores(MatrixPanel_I2S_DMA* display) {
        const int count = (int)Leaderboard::gameCount();
        if (count <= 0) {
            screen = SCREEN_GAMES;
            return;
        }

        const int gameIdx = constrain(selectedGame, 0, count - 1);
        const Leaderboard::Entry* e = Leaderboard::entryAt((uint8_t)gameIdx);
        if (!e) {
            screen = SCREEN_GAMES;
            return;
        }

        // Show selected game name in HUD area as "L: name".
        char hud[24];
        snprintf(hud, sizeof(hud), "L:%s", e->name);
        SmallFont::drawString(display, 2, 6, hud, COLOR_YELLOW);

        // If all scores are 0, show a hint.
        if (e->scores[0] == 0) {
            SmallFont::drawString(display, 8, HUD_H + 18, "NO SCORES", COLOR_WHITE);
            SmallFont::drawString(display, 8, HUD_H + 28, "YET", COLOR_WHITE);
            return;
        }

        // Build score labels (stable storage in member array): "1 ABC 12345"
        for (int i = 0; i < (int)Leaderboard::TOP_SCORES; i++) {
            const char* init = e->initials[i][0] ? e->initials[i] : "---";
            snprintf(scoreLabels[i], sizeof(scoreLabels[i]), "%d %s %lu",
                     i + 1, init, (unsigned long)e->scores[i]);
        }

        scoresList.selectedActual = constrain(scoresList.selectedActual, 0, (int)Leaderboard::TOP_SCORES - 1);
        ScrollableList::Layout lay;
        lay.hudH = HUD_H;
        lay.visibleRows = (int)Leaderboard::TOP_SCORES; // 5 rows fits, no scroll needed but ok
        lay.labelX = 8;
        scoresList.draw(display, scoresModel, lay);
    }
};


