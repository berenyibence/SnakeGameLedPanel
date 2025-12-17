#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "GameBase.h"
#include "ControllerManager.h"
#include "config.h"
#include "SmallFont.h"
#include "Settings.h"
#include "UserProfiles.h"
#include "GameOverLeaderboardView.h"

/**
 * TronGame - Classic Tron / Light-Cycles
 *
 * Rules (classic):
 * - Each player moves continuously on a grid, leaving a solid trail behind.
 * - Turning is 90 degrees only; reversing direction is not allowed.
 * - Colliding with the wall or ANY trail (including another player's head cell) eliminates you.
 * - When <= 1 player remains, the surviving player (if any) earns 1 point and a new round starts.
 * - First to WIN_SCORE wins the match.
 *
 * HUD:
 * - Same style / reserved top area approach as Snake: scores across the top.
 *
 * Notes:
 * - All constants/types are kept inside the class to avoid header name collisions
 *   with other games (e.g., Snake defines Point/Direction/HUD_HEIGHT globally).
 */
class TronGame : public GameBase {
private:
    // ---------------------------------------------------------
    // Layout (match Snake's "inset border + HUD" concept)
    // ---------------------------------------------------------
    static constexpr int HUD_H = 8;                 // reserve top for HUD
    static constexpr int CELL_PX = 1;               // 1px wide trails (classic Tron look)
    static constexpr int BORDER_INSET_X = 1;        // avoid edge pixels
    static constexpr int BORDER_INSET_Y = 1;        // 1px gap below HUD
    static constexpr int BORDER_INSET_BOTTOM = 1;   // avoid last row

    // Border rectangle (in pixels)
    static constexpr int BORDER_X = BORDER_INSET_X;
    static constexpr int BORDER_Y = HUD_H + BORDER_INSET_Y;
    static constexpr int BORDER_W = PANEL_RES_X - (BORDER_INSET_X * 2);
    static constexpr int BORDER_H = (PANEL_RES_Y - BORDER_Y) - BORDER_INSET_BOTTOM;

    // Content area inside border (1px thickness)
    static constexpr int CONTENT_X = BORDER_X + 1;
    static constexpr int CONTENT_Y = BORDER_Y + 1;
    static constexpr int CONTENT_W = BORDER_W - 2;
    static constexpr int CONTENT_H = BORDER_H - 2;

    // Logical grid (cells)
    static constexpr int GRID_W = (CONTENT_W / CELL_PX);
    static constexpr int GRID_H = (CONTENT_H / CELL_PX);

    // ---------------------------------------------------------
    // Game rules / pacing
    // ---------------------------------------------------------
    static constexpr uint8_t WIN_SCORE = 5;
    static constexpr uint32_t ROUND_RESET_DELAY_MS = 1200;

    enum class Dir : uint8_t { Up, Down, Left, Right };

    struct Player {
        bool active = false;     // participating in this match (connected at start)
        bool alive = false;      // alive in the current round
        bool isAi = false;       // true when this player is controlled by the built-in AI
        uint8_t padIndex = 0;    // 0..3 (controller slot)
        uint8_t score = 0;       // round wins
        uint16_t color = COLOR_WHITE;
        Dir dir = Dir::Right;
        Dir nextDir = Dir::Right;
        int x = 0;               // logical cell
        int y = 0;               // logical cell
    };

    // 0 = empty, else (padIndex+1) owner
    uint8_t trail[GRID_W * GRID_H];
    Player players[MAX_GAMEPADS];
    bool gameOver = false;
    int winnerPad = -1; // 0..3
    uint8_t roundNo = 1;

    uint32_t lastTickMs = 0;
    uint32_t roundEndMs = 0;
    bool roundActive = false;

    uint16_t playerColors[4] = { COLOR_GREEN, COLOR_CYAN, COLOR_ORANGE, COLOR_PURPLE };

    static inline int idx(int x, int y) { return y * GRID_W + x; }

    static inline bool isOpposite(Dir a, Dir b) {
        return (a == Dir::Up && b == Dir::Down) ||
               (a == Dir::Down && b == Dir::Up) ||
               (a == Dir::Left && b == Dir::Right) ||
               (a == Dir::Right && b == Dir::Left);
    }

    static inline Dir turnLeft(Dir d) {
        switch (d) {
            case Dir::Up: return Dir::Left;
            case Dir::Down: return Dir::Right;
            case Dir::Left: return Dir::Down;
            case Dir::Right: return Dir::Up;
        }
        return Dir::Up;
    }

    static inline Dir turnRight(Dir d) {
        switch (d) {
            case Dir::Up: return Dir::Right;
            case Dir::Down: return Dir::Left;
            case Dir::Left: return Dir::Up;
            case Dir::Right: return Dir::Down;
        }
        return Dir::Up;
    }

    void clearTrail() {
        memset(trail, 0, sizeof(trail));
    }

    void markCell(int x, int y, uint8_t ownerPadIndex) {
        if (x < 0 || x >= GRID_W || y < 0 || y >= GRID_H) return;
        trail[idx(x, y)] = (uint8_t)(ownerPadIndex + 1);
    }

    uint8_t getCell(int x, int y) const {
        if (x < 0 || x >= GRID_W || y < 0 || y >= GRID_H) return 0xFF; // treat OOB as wall
        return trail[idx(x, y)];
    }

    void setupPlayersFromConnectedControllers() {
        // Apply current global player color for Player 1 (pad index 0).
        playerColors[0] = globalSettings.getPlayerColor();

        // Initialize all players as inactive; only controllers present at match start participate.
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            players[i].active = false;
            players[i].alive = false;
            players[i].isAi = false;
            players[i].padIndex = (uint8_t)i;
            players[i].score = 0;
            players[i].color = playerColors[i];
            players[i].dir = Dir::Right;
            players[i].nextDir = Dir::Right;
            players[i].x = 0;
            players[i].y = 0;
        }

        for (int i = 0; i < MAX_GAMEPADS; i++) {
            if (globalControllerManager->getController(i)) {
                players[i].active = true;
            }
        }

        // Single-player support: if only one controller is connected at match start,
        // spawn one basic AI opponent in the first free slot.
        if (globalControllerManager->getConnectedCount() == 1) {
            int humanPad = -1;
            for (int i = 0; i < MAX_GAMEPADS; i++) {
                if (players[i].active) { humanPad = i; break; }
            }

            int aiPad = -1;
            for (int i = 0; i < MAX_GAMEPADS; i++) {
                if (i == humanPad) continue;
                if (!players[i].active) { aiPad = i; break; }
            }

            if (aiPad >= 0) {
                players[aiPad].active = true;
                players[aiPad].isAi = true;
            }
        }
    }

    int activeCount() const {
        int c = 0;
        for (int i = 0; i < MAX_GAMEPADS; i++) if (players[i].active) c++;
        return c;
    }

    int aliveCount() const {
        int c = 0;
        for (int i = 0; i < MAX_GAMEPADS; i++) if (players[i].active && players[i].alive) c++;
        return c;
    }

    int lastAlivePad() const {
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            if (players[i].active && players[i].alive) return i;
        }
        return -1;
    }

    void startRound(uint32_t nowMs) {
        clearTrail();
        roundActive = true;
        roundEndMs = 0;
        lastTickMs = nowMs;

        // Default spawn points (works for 1..4 players)
        // P1: left-mid -> right
        // P2: right-mid -> left
        // P3: mid-top -> down
        // P4: mid-bottom -> up
        const int leftX = 2;
        const int rightX = GRID_W - 3;
        const int midX = GRID_W / 2;
        const int topY = 2;
        const int bottomY = GRID_H - 3;
        const int midY = GRID_H / 2;

        for (int i = 0; i < MAX_GAMEPADS; i++) {
            if (!players[i].active) continue;
            players[i].alive = true;
            players[i].nextDir = players[i].dir; // will be set below
        }

        if (players[0].active) { players[0].x = leftX;  players[0].y = midY;   players[0].dir = Dir::Right; players[0].nextDir = Dir::Right; }
        if (players[1].active) { players[1].x = rightX; players[1].y = midY;   players[1].dir = Dir::Left;  players[1].nextDir = Dir::Left;  }
        if (players[2].active) { players[2].x = midX;   players[2].y = topY;   players[2].dir = Dir::Down;  players[2].nextDir = Dir::Down;  }
        if (players[3].active) { players[3].x = midX;   players[3].y = bottomY;players[3].dir = Dir::Up;    players[3].nextDir = Dir::Up;    }

        // Mark initial head cells as occupied (heads are solid in Tron)
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            if (players[i].active && players[i].alive) {
                markCell(players[i].x, players[i].y, (uint8_t)i);
            }
        }
    }

    void handlePlayerInput(Player& p, ControllerPtr ctl) {
        if (!ctl || !ctl->isConnected()) return;

        // D-pad mapping in this codebase:
        // 0x01 UP, 0x02 DOWN, 0x04 RIGHT, 0x08 LEFT
        const uint8_t d = ctl->dpad();
        Dir desired = p.nextDir;

        if (d & 0x01) desired = Dir::Up;
        else if (d & 0x02) desired = Dir::Down;
        else if (d & 0x04) desired = Dir::Right;
        else if (d & 0x08) desired = Dir::Left;

        // Tron: do not allow 180-degree reversal
        if (!isOpposite(p.dir, desired)) {
            p.nextDir = desired;
        }
    }

    /**
     * Basic AI:
     * - Prefer going straight if safe.
     * - Otherwise pick left/right with the most open space (short lookahead).
     * - This is intentionally "simple" (not perfect) but avoids obvious crashes.
     */
    int lookaheadFreeCells(int x, int y, Dir d, int maxSteps) const {
        int nx = x;
        int ny = y;
        int freeSteps = 0;
        for (int i = 0; i < maxSteps; i++) {
            if (d == Dir::Up) ny--;
            else if (d == Dir::Down) ny++;
            else if (d == Dir::Left) nx--;
            else if (d == Dir::Right) nx++;

            if (getCell(nx, ny) != 0) break;
            freeSteps++;
        }
        return freeSteps;
    }

    void handleAiInput(Player& p) {
        // Candidate directions: straight, left, right (never reverse)
        const Dir straight = p.dir;
        const Dir left = turnLeft(p.dir);
        const Dir right = turnRight(p.dir);

        // Small lookahead is enough for a "basic" AI and keeps CPU low.
        const int MAX_LOOK = 10;
        const int sStraight = lookaheadFreeCells(p.x, p.y, straight, MAX_LOOK);
        const int sLeft = lookaheadFreeCells(p.x, p.y, left, MAX_LOOK);
        const int sRight = lookaheadFreeCells(p.x, p.y, right, MAX_LOOK);

        // Prefer straight if it isn't immediately fatal and is competitive.
        int best = sStraight;
        Dir bestDir = straight;

        if (sLeft > best) { best = sLeft; bestDir = left; }
        else if (sLeft == best && random(0, 2) == 0) { bestDir = left; }

        if (sRight > best) { best = sRight; bestDir = right; }
        else if (sRight == best && random(0, 2) == 0) { bestDir = right; }

        // If all are zero, we still must choose something; pick a turn randomly.
        if (best == 0) {
            bestDir = (random(0, 2) == 0) ? left : right;
        }

        p.nextDir = bestDir;
    }

public:
    TronGame() {
        memset(trail, 0, sizeof(trail));
    }

    /**
     * Tron is a fixed-tick game; rendering much faster than the tick doesn't help.
     */
    uint16_t preferredRenderFps() const override {
        if (TRON_SPEED_MS == 0) return GAME_RENDER_FPS;
        uint16_t fps = (uint16_t)(1000UL / (uint32_t)TRON_SPEED_MS);
        if (fps < 10) fps = 10;
        if (fps > GAME_RENDER_FPS) fps = GAME_RENDER_FPS;
        return fps;
    }

    void start() override {
        gameOver = false;
        winnerPad = -1;
        roundNo = 1;

        setupPlayersFromConnectedControllers();
        startRound(millis());
    }

    void reset() override {
        start();
    }

    void update(ControllerManager* input) override {
        const uint32_t now = millis();
        if (gameOver) return;

        // Inter-round pause: after delay, start next round
        if (!roundActive) {
            if (roundEndMs != 0 && (uint32_t)(now - roundEndMs) >= ROUND_RESET_DELAY_MS) {
                roundNo++;
                startRound(now);
            }
            return;
        }

        // Tick pacing
        if (TRON_SPEED_MS > 0 && (uint32_t)(now - lastTickMs) < (uint32_t)TRON_SPEED_MS) return;
        lastTickMs = now;

        // 1) Input
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            Player& p = players[i];
            if (!p.active || !p.alive) continue;
            if (p.isAi) {
                handleAiInput(p);
            } else {
                ControllerPtr ctl = input->getController(p.padIndex);
                if (!ctl) {
                    // Controller disappeared -> eliminated
                    p.alive = false;
                    continue;
                }
                handlePlayerInput(p, ctl);
            }
        }

        // 2) Compute next heads (simultaneous)
        struct NextPos { int x; int y; bool willMove; bool crash; };
        NextPos next[MAX_GAMEPADS];
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            next[i] = {0, 0, false, false};
            Player& p = players[i];
            if (!p.active || !p.alive) continue;

            p.dir = p.nextDir;
            int nx = p.x;
            int ny = p.y;
            if (p.dir == Dir::Up) ny--;
            else if (p.dir == Dir::Down) ny++;
            else if (p.dir == Dir::Left) nx--;
            else if (p.dir == Dir::Right) nx++;

            next[i] = {nx, ny, true, false};
        }

        // 3) Collisions (walls + trails + head-on)
        // Wall/trail collision
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            if (!next[i].willMove) continue;
            const uint8_t cell = getCell(next[i].x, next[i].y);
            if (cell != 0) {
                next[i].crash = true;
            }
        }

        // Head-on: same destination cell => crash all involved
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            if (!next[i].willMove) continue;
            for (int j = i + 1; j < MAX_GAMEPADS; j++) {
                if (!next[j].willMove) continue;
                if (next[i].x == next[j].x && next[i].y == next[j].y) {
                    next[i].crash = true;
                    next[j].crash = true;
                }
            }
        }

        // 4) Apply moves + mark trails
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            Player& p = players[i];
            if (!p.active || !p.alive) continue;

            if (next[i].willMove) {
                p.x = next[i].x;
                p.y = next[i].y;
            }

            if (next[i].crash) {
                p.alive = false;
            } else {
                // Survived: occupy new head cell (trail is permanent)
                markCell(p.x, p.y, (uint8_t)i);
            }
        }

        // 5) Round end
        const int aliveNow = aliveCount();
        if (aliveNow <= 1) {
            roundActive = false;
            roundEndMs = now;

            const int last = lastAlivePad();
            if (last >= 0) {
                players[last].score++;
                if (players[last].score >= WIN_SCORE) {
                    gameOver = true;
                    winnerPad = last;
                }
            }
        }
    }

    void draw(MatrixPanel_I2S_DMA* display) override {
        display->fillScreen(COLOR_BLACK);

        // GAME OVER screen
        if (gameOver) {
            char title[12];
            if (winnerPad >= 0) snprintf(title, sizeof(title), "P%d WINS", winnerPad + 1);
            else snprintf(title, sizeof(title), "GAME OVER");

            char tag[4];
            UserProfiles::getPadTag(0, tag);
            GameOverLeaderboardView::draw(display, title, leaderboardId(), leaderboardScore(), tag);
            return;
        }

        // HUD (same spirit as Snake)
        const int hudY = 6; // 1px margin + avoid top overflow
        int hudX = 2;
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            if (!players[i].active) continue;
            SmallFont::drawStringF(display, hudX, hudY, players[i].color, "P%d:%d", i + 1, players[i].score);
            hudX += 16;
        }

        // Alive count indicator on the right
        SmallFont::drawStringF(display, PANEL_RES_X - 12, hudY, COLOR_YELLOW, "A%d", aliveCount());

        // Border
        display->drawRect(BORDER_X, BORDER_Y, BORDER_W, BORDER_H, COLOR_WHITE);

        // Trails (grid)
        for (int y = 0; y < GRID_H; y++) {
            for (int x = 0; x < GRID_W; x++) {
                const uint8_t v = trail[idx(x, y)];
                if (v == 0) continue;
                const uint8_t owner = (uint8_t)(v - 1);
                const uint16_t c = (owner < 4) ? playerColors[owner] : COLOR_WHITE;
                const int px = CONTENT_X + x * CELL_PX;
                const int py = CONTENT_Y + y * CELL_PX;
                // 1px-wide trails (CELL_PX == 1), but keep math generic.
                display->drawPixel(px, py, c);
            }
        }

        // Heads: small highlight so you can see direction more easily
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            if (!players[i].active || !players[i].alive) continue;
            const int px = CONTENT_X + players[i].x * CELL_PX;
            const int py = CONTENT_Y + players[i].y * CELL_PX;
            // White highlight on the head (still 1px)
            display->drawPixel(px, py, COLOR_WHITE);
        }

        // Round transition hint
        if (!roundActive) {
            const int last = lastAlivePad();
            if (last >= 0) {
                SmallFont::drawStringF(display, 10, BORDER_Y + 10, COLOR_WHITE, "P%d +1", last + 1);
            } else {
                SmallFont::drawString(display, 10, BORDER_Y + 10, "DRAW", COLOR_WHITE);
            }
        }
    }

    bool isGameOver() override {
        return gameOver;
    }

    // ------------------------------
    // Leaderboard integration
    // ------------------------------
    bool leaderboardEnabled() const override { return true; }
    const char* leaderboardId() const override { return "tron"; }
    const char* leaderboardName() const override { return "Tron"; }
    uint32_t leaderboardScore() const override {
        // Submit the highest match score achieved by any player.
        uint32_t best = 0;
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            if (!players[i].active) continue;
            if ((uint32_t)players[i].score > best) best = (uint32_t)players[i].score;
        }
        return best;
    }
};


