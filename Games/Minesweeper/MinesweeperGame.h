#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "../../engine/GameBase.h"
#include "../../engine/ControllerManager.h"
#include "../../engine/config.h"
#include "../../engine/UserProfiles.h"
#include "../../component/SmallFont.h"
#include "../../component/GameOverLeaderboardView.h"

#include "MinesweeperGameConfig.h"

class MinesweeperGame : public GameBase {
private:
    using Cfg = MinesweeperConfig;

    struct Cell {
        uint8_t mine : 1;
        uint8_t rev  : 1;
        uint8_t flag : 1;
        uint8_t adj  : 4; // 0..8
    };

    Cell grid[Cfg::H][Cfg::W];
    uint8_t cursorX = 0, cursorY = 0;
    bool gameOver = false;
    bool win = false;
    uint32_t startMs = 0;
    uint32_t elapsedScore = 0;
    bool minesPlaced = false;

    bool lastA = false, lastB = false;
    uint8_t lastDpad = 0;

    static inline bool dUp(uint8_t d) { return (d & 0x01) != 0; }
    static inline bool dDown(uint8_t d) { return (d & 0x02) != 0; }
    static inline bool dRight(uint8_t d) { return (d & 0x04) != 0; }
    static inline bool dLeft(uint8_t d) { return (d & 0x08) != 0; }

    void clear() { memset(grid, 0, sizeof(grid)); }

    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < Cfg::W && y < Cfg::H; }

    void computeAdj() {
        for (int y = 0; y < Cfg::H; y++) {
            for (int x = 0; x < Cfg::W; x++) {
                if (grid[y][x].mine) { grid[y][x].adj = 0; continue; }
                int c = 0;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        const int nx = x + dx;
                        const int ny = y + dy;
                        if (inBounds(nx, ny) && grid[ny][nx].mine) c++;
                    }
                }
                grid[y][x].adj = (uint8_t)c;
            }
        }
    }

    void placeMines(uint8_t safeX, uint8_t safeY) {
        // Place mines avoiding the first-click cell and its neighbors.
        uint8_t placed = 0;
        while (placed < Cfg::MINES) {
            const int x = random(0, Cfg::W);
            const int y = random(0, Cfg::H);
            if (grid[y][x].mine) continue;
            if (abs(x - (int)safeX) <= 1 && abs(y - (int)safeY) <= 1) continue;
            grid[y][x].mine = 1;
            placed++;
        }
        computeAdj();
    }

    void floodReveal(int sx, int sy) {
        // BFS flood for zeros
        static uint8_t qx[Cfg::W * Cfg::H];
        static uint8_t qy[Cfg::W * Cfg::H];
        int qh = 0, qt = 0;
        qx[qt] = (uint8_t)sx; qy[qt] = (uint8_t)sy; qt++;

        while (qh < qt) {
            const int x = qx[qh];
            const int y = qy[qh];
            qh++;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (!inBounds(nx, ny)) continue;
                    Cell& c = grid[ny][nx];
                    if (c.rev || c.flag) continue;
                    if (c.mine) continue;
                    c.rev = 1;
                    if (c.adj == 0) { qx[qt] = (uint8_t)nx; qy[qt] = (uint8_t)ny; qt++; }
                }
            }
        }
    }

    bool checkWin() const {
        for (int y = 0; y < Cfg::H; y++) {
            for (int x = 0; x < Cfg::W; x++) {
                const Cell& c = grid[y][x];
                if (!c.mine && !c.rev) return false;
            }
        }
        return true;
    }

    void drawNumber4x4(MatrixPanel_I2S_DMA* d, int px, int py, uint8_t n) {
        if (n == 0) return;
        uint16_t col = COLOR_WHITE;
        // Classic-ish colors
        if (n == 1) col = COLOR_BLUE;
        else if (n == 2) col = COLOR_GREEN;
        else if (n == 3) col = COLOR_RED;
        else if (n == 4) col = COLOR_PURPLE;
        else if (n >= 5) col = COLOR_ORANGE;

        // Tiny patterns per digit (centered in 4x4)
        static const uint8_t pat[9][4][4] = {
            {{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}}, //0
            {{0,0,0,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}}, //1
            {{0,0,0,0},{0,1,1,0},{0,1,1,0},{0,0,0,0}}, //2
            {{0,0,0,0},{0,1,1,0},{0,0,1,0},{0,0,0,0}}, //3
            {{0,0,0,0},{0,1,0,0},{0,1,1,0},{0,0,0,0}}, //4
            {{0,0,0,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}}, //5
            {{0,0,0,0},{0,1,1,0},{0,1,1,0},{0,1,0,0}}, //6
            {{0,0,0,0},{0,1,1,0},{0,0,1,0},{0,0,1,0}}, //7
            {{0,0,0,0},{0,1,1,0},{0,1,1,0},{0,0,0,0}}, //8 (same as 2 but ok)
        };
        const uint8_t (*p)[4] = pat[min(8, (int)n)];
        for (int yy = 0; yy < 4; yy++) {
            for (int xx = 0; xx < 4; xx++) {
                if (!p[yy][xx]) continue;
                d->drawPixel(px + xx, py + yy, col);
            }
        }
    }

public:
    void start() override {
        randomSeed((uint32_t)micros() ^ (uint32_t)millis());
        clear();
        cursorX = 0; cursorY = 0;
        gameOver = false;
        win = false;
        startMs = millis();
        elapsedScore = 0;
        lastA = lastB = false;
        lastDpad = 0;
        minesPlaced = false; // mines placed on first reveal to guarantee safe start
    }

    void reset() override { start(); }

    void update(ControllerManager* input) override {
        if (gameOver) return;
        const uint32_t now = millis();
        ControllerPtr ctl = input ? input->getController(0) : nullptr;
        if (!ctl) return;

        const uint8_t d = ctl->dpad();
        const bool upE = dUp(d) && !dUp(lastDpad);
        const bool downE = dDown(d) && !dDown(lastDpad);
        const bool leftE = dLeft(d) && !dLeft(lastDpad);
        const bool rightE = dRight(d) && !dRight(lastDpad);
        lastDpad = d;
        if (upE && cursorY > 0) cursorY--;
        if (downE && cursorY < Cfg::H - 1) cursorY++;
        if (leftE && cursorX > 0) cursorX--;
        if (rightE && cursorX < Cfg::W - 1) cursorX++;

        const bool aNow = (bool)ctl->a();
        const bool bNow = (bool)ctl->b();
        const bool aE = aNow && !lastA;
        const bool bE = bNow && !lastB;
        lastA = aNow;
        lastB = bNow;

        Cell& c = grid[cursorY][cursorX];
        if (bE && !c.rev) {
            c.flag = !c.flag;
        }

        if (aE && !c.rev && !c.flag) {
            // First click: if no mines placed yet, place them now.
            if (!minesPlaced) {
                placeMines(cursorX, cursorY);
                minesPlaced = true;
            }
            c.rev = 1;
            if (c.mine) {
                gameOver = true;
                win = false;
                elapsedScore = 0;
            } else {
                if (c.adj == 0) floodReveal(cursorX, cursorY);
                if (checkWin()) {
                    gameOver = true;
                    win = true;
                    elapsedScore = (uint32_t)((now - startMs) / 1000UL);
                }
            }
        }
    }

    void draw(MatrixPanel_I2S_DMA* d) override {
        d->fillScreen(COLOR_BLACK);
        if (gameOver) {
            char tag[4];
            UserProfiles::getPadTag(0, tag);
            GameOverLeaderboardView::draw(d, win ? "YOU WIN" : "BOOM!", leaderboardId(), leaderboardScore(), tag);
            return;
        }

        // HUD
        SmallFont::drawString(d, 2, 6, "MINES", COLOR_CYAN);
        for (int x = 0; x < PANEL_RES_X; x += 2) d->drawPixel(x, Cfg::HUD_H - 1, COLOR_BLUE);

        // Board: 16x16 * 4 = 64, we use full screen; HUD overlays top but OK.
        for (int y = 0; y < Cfg::H; y++) {
            for (int x = 0; x < Cfg::W; x++) {
                const int px = x * Cfg::CELL;
                const int py = y * Cfg::CELL;
                const Cell& c = grid[y][x];
                if (!c.rev) {
                    // closed
                    d->fillRect(px, py, 4, 4, d->color565(40,40,40));
                    d->drawRect(px, py, 4, 4, d->color565(80,80,80));
                    if (c.flag) {
                        d->drawPixel(px + 1, py + 1, COLOR_RED);
                        d->drawPixel(px + 2, py + 1, COLOR_RED);
                        d->drawPixel(px + 1, py + 2, COLOR_RED);
                    }
                } else {
                    // revealed
                    d->fillRect(px, py, 4, 4, d->color565(20,20,20));
                    if (c.mine) {
                        d->fillRect(px + 1, py + 1, 2, 2, COLOR_RED);
                    } else {
                        drawNumber4x4(d, px, py, c.adj);
                    }
                }
            }
        }

        // Cursor
        const int cx = cursorX * Cfg::CELL;
        const int cy = cursorY * Cfg::CELL;
        d->drawRect(cx, cy, 4, 4, COLOR_YELLOW);
    }

    bool isGameOver() override { return gameOver; }

    bool leaderboardEnabled() const override { return true; }
    const char* leaderboardId() const override { return "mines"; }
    const char* leaderboardName() const override { return "Mines"; }
    uint32_t leaderboardScore() const override {
        // For now: win score = max(1, 999 - seconds). Loss = 0.
        if (!win) return 0;
        const uint32_t s = elapsedScore;
        const uint32_t v = (s >= 999) ? 1 : (999 - s);
        return v;
    }
};


