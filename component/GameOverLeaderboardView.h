#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "SmallFont.h"
#include "../engine/Leaderboard.h"

/**
 * GameOverLeaderboardView
 * -----------------------
 * Standardized "GAME OVER" overlay that shows the per-game Top-5 leaderboard.
 *
 * Conventions:
 * - 8px HUD at the top with title.
 * - Dotted divider line at y = HUD_H - 1.
 * - List rows below HUD in 8px steps.
 * - If the submitted score made it into the Top-5, highlight and mark it.
 */
namespace GameOverLeaderboardView {

static constexpr int HUD_H = 8;

static inline void draw(MatrixPanel_I2S_DMA* display,
                        const char* hudTitle,
                        const char* gameId,
                        uint32_t score,
                        const char* playerTag) {
    // HUD title
    SmallFont::drawString(display, 2, 6, hudTitle, COLOR_RED);
    for (int x = 0; x < PANEL_RES_X; x += 2) display->drawPixel(x, HUD_H - 1, COLOR_BLUE);

    const Leaderboard::Entry* e = Leaderboard::entryForGameId(gameId);
    const int rank = Leaderboard::rankFor(gameId, score, playerTag);

    const int baseY = HUD_H + 6;
    if (!e || e->scores[0] == 0) {
        SmallFont::drawString(display, 10, baseY + 12, "NO SCORES", COLOR_WHITE);
        return;
    }

    for (int i = 0; i < (int)Leaderboard::TOP_SCORES; i++) {
        const int y = baseY + i * 8;
        const bool sel = (rank == i);
        const uint16_t col = sel ? COLOR_GREEN : COLOR_WHITE;

        // Only show marker if the player actually made it into the leaderboard.
        SmallFont::drawChar(display, 2, y, (rank >= 0 && sel) ? '>' : ' ', col);

        const char* init = (e->initials[i][0]) ? e->initials[i] : "---";
        char line[20];
        snprintf(line, sizeof(line), "%d %s %lu", i + 1, init, (unsigned long)e->scores[i]);
        SmallFont::drawString(display, 6, y, line, col);
    }
}

} // namespace GameOverLeaderboardView


