#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "../../engine/GameBase.h"
#include "../../engine/ControllerManager.h"
#include "../../engine/config.h"
#include "../../engine/UserProfiles.h"
#include "../../component/SmallFont.h"
#include "../../component/GameOverLeaderboardView.h"

#include "BomberManGameConfig.h"
#include "BomberManGameSprites.h"

/**
 * BomberManGame
 * -------------
 * Multiplayer Bomberman-style game with enemies, destructible bricks, bombs, powerups and a hidden exit gate.
 *
 * Controls (each player):
 * - D-pad: move
 * - A: place bomb
 *
 * Powerups (hidden under bricks):
 * - Boot: faster movement
 * - Extra Bomb: +1 bomb capacity
 * - Flame: +1 explosion range
 * - Shield: blocks one hit then breaks
 *
 * Exit gate:
 * - Exactly one per level, hidden under a brick
 * - Opens when all enemies are defeated
 * - Any alive player reaching the open gate clears the level
 */
class BomberManGame : public GameBase {
private:
    using Cfg = BomberManGameConfig;

    enum Tile : uint8_t {
        TILE_EMPTY = 0,
        TILE_SOLID,
        TILE_BRICK
    };

    enum PickupType : uint8_t {
        PU_NONE = 0,
        PU_BOOT,
        PU_BOMB,
        PU_FLAME,
        PU_SHIELD,
        PU_GATE
    };

    struct Bomb {
        bool active = false;
        uint8_t owner = 0;
        uint8_t gx = 0, gy = 0;
        uint32_t plantedMs = 0;
        uint8_t range = 2;
    };

    struct Explosion {
        bool active = false;
        uint8_t gx = 0, gy = 0;
        uint32_t startMs = 0;
    };

    struct BreakAnim {
        bool active = false;
        uint8_t gx = 0, gy = 0;
        uint32_t startMs = 0;
    };

    struct Pickup {
        bool active = false;
        uint8_t gx = 0, gy = 0;
        PickupType type = PU_NONE;
        bool revealed = false;
    };

    struct Player {
        bool active = false;
        bool everConnected = false;
        bool alive = false;
        uint8_t pad = 0;
        uint8_t gx = 1, gy = 1;     // grid position (derived from px/py)
        int16_t px = 0, py = 0;     // pixel position (top-left of cell, playfield-relative)
        uint8_t bombsCap = Cfg::START_BOMBS;
        uint8_t bombsActive = 0;
        uint8_t range = Cfg::START_RANGE;
        uint8_t speed = Cfg::START_SPEED;
        bool shield = false;
        uint16_t color = COLOR_GREEN;
        uint32_t respawnUntilMs = 0;

        // input edges
        bool lastA = false;

        // movement
        int8_t dirX = 0;
        int8_t dirY = 0;
        int8_t wishX = 0;
        int8_t wishY = 0;
    };

    struct Enemy {
        bool alive = false;
        uint8_t type = 0; // 0 random, 1 chaser
        uint8_t gx = 0, gy = 0;
        uint8_t dir = 0; // 0 up,1 down,2 left,3 right
        uint32_t nextTurnMs = 0;
        uint32_t moveIntervalMs = 320;
    };

    // Level state
    Tile tiles[Cfg::GRID_H][Cfg::GRID_W];
    uint8_t breakT[Cfg::GRID_H][Cfg::GRID_W];     // 0 none, else intensity for break animation
    uint8_t explT[Cfg::GRID_H][Cfg::GRID_W];      // 0 none, else intensity for explosion

    bool gateHidden = false;
    bool gateRevealed = false;
    bool gateOpen = false;
    uint8_t gateX = 0, gateY = 0;

    Player players[Cfg::MAX_PLAYERS];
    Enemy enemies[Cfg::MAX_ENEMIES];
    Bomb bombs[Cfg::MAX_BOMBS];
    Pickup pickups[Cfg::MAX_PICKUPS];

    bool gameOver = false;
    uint32_t score = 0;
    uint16_t level = 1;

    uint32_t lastTickMs = 0;

    // -----------------------------------------------------
    // Helpers
    // -----------------------------------------------------
    static inline bool dUp(uint8_t d) { return (d & 0x01) != 0; }
    static inline bool dDown(uint8_t d) { return (d & 0x02) != 0; }
    static inline bool dRight(uint8_t d) { return (d & 0x04) != 0; }
    static inline bool dLeft(uint8_t d) { return (d & 0x08) != 0; }

    static inline uint16_t dim565(uint16_t c, uint8_t amount /*0..255*/) {
        // Linear dimming in RGB565 space (cheap + good enough for LED panel UI).
        // amount=255 -> unchanged; amount=0 -> black.
        const uint16_t r = (uint16_t)((c >> 11) & 0x1F);
        const uint16_t g = (uint16_t)((c >> 5) & 0x3F);
        const uint16_t b = (uint16_t)(c & 0x1F);
        const uint16_t rr = (uint16_t)((r * amount) / 255);
        const uint16_t gg = (uint16_t)((g * amount) / 255);
        const uint16_t bb = (uint16_t)((b * amount) / 255);
        return (uint16_t)((rr << 11) | (gg << 5) | bb);
    }

    static inline int toPx(int g) { return g * Cfg::CELL; }

    bool inBounds(int x, int y) const {
        return x >= 0 && y >= 0 && x < Cfg::GRID_W && y < Cfg::GRID_H;
    }

    bool isBlocked(int gx, int gy) const {
        if (!inBounds(gx, gy)) return true;
        if (tiles[gy][gx] == TILE_SOLID || tiles[gy][gx] == TILE_BRICK) return true;
        // bombs block movement
        for (int i = 0; i < Cfg::MAX_BOMBS; i++) {
            if (bombs[i].active && bombs[i].gx == gx && bombs[i].gy == gy) return true;
        }
        return false;
    }

    bool isTileBlockedForPlayer(int gx, int gy) const {
        // Same as isBlocked, but lets you slide within a cell without treating
        // the current cell as blocked. For now identical; kept for readability.
        return isBlocked(gx, gy);
    }

    static inline int8_t signi(int v) { return (v < 0) ? -1 : (v > 0) ? 1 : 0; }

    void spawnPlayerAt(Player& p, uint8_t gx, uint8_t gy) {
        p.gx = gx;
        p.gy = gy;
        p.px = (int16_t)toPx(gx);
        p.py = (int16_t)toPx(gy);
        p.dirX = 0; p.dirY = 0;
        p.wishX = 0; p.wishY = 0;
        p.alive = true;
    }

    bool anyExplosionAt(int gx, int gy) const {
        if (!inBounds(gx, gy)) return false;
        return explT[gy][gx] != 0;
    }

    void clearArrays() {
        memset(tiles, 0, sizeof(tiles));
        memset(breakT, 0, sizeof(breakT));
        memset(explT, 0, sizeof(explT));
        for (auto &b : bombs) b.active = false;
        for (auto &p : pickups) p.active = false;
        for (auto &e : enemies) e.alive = false;
    }

    void spawnPickup(uint8_t gx, uint8_t gy, PickupType t, bool revealed) {
        for (int i = 0; i < Cfg::MAX_PICKUPS; i++) {
            if (pickups[i].active) continue;
            pickups[i].active = true;
            pickups[i].gx = gx;
            pickups[i].gy = gy;
            pickups[i].type = t;
            pickups[i].revealed = revealed;
            return;
        }
    }

    void generateLevel(uint32_t now) {
        (void)now;
        clearArrays();
        gateHidden = true;
        gateRevealed = false;
        gateOpen = false;

        // Base layout: border solids + internal checker solids (classic bomberman)
        for (int y = 0; y < Cfg::GRID_H; y++) {
            for (int x = 0; x < Cfg::GRID_W; x++) {
                bool solid = (x == 0 || y == 0 || x == Cfg::GRID_W - 1 || y == Cfg::GRID_H - 1);
                if ((x % 2 == 0) && (y % 2 == 0)) solid = true;
                tiles[y][x] = solid ? TILE_SOLID : TILE_EMPTY;
            }
        }

        // Reserve spawn zones around corners (keep empty)
        auto reserve = [&](int sx, int sy) {
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    const int x = sx + dx;
                    const int y = sy + dy;
                    if (inBounds(x, y) && tiles[y][x] != TILE_SOLID) tiles[y][x] = TILE_EMPTY;
                }
            }
        };
        reserve(1, 1);
        reserve(Cfg::GRID_W - 2, 1);
        reserve(1, Cfg::GRID_H - 2);
        reserve(Cfg::GRID_W - 2, Cfg::GRID_H - 2);

        // Place bricks randomly on empty tiles
        for (int y = 1; y < Cfg::GRID_H - 1; y++) {
            for (int x = 1; x < Cfg::GRID_W - 1; x++) {
                if (tiles[y][x] != TILE_EMPTY) continue;
                // not too dense: ~55%
                if (random(0, 100) < 55) tiles[y][x] = TILE_BRICK;
            }
        }

        // Place gate under one brick (guaranteed)
        // Find a random brick not in spawn zones.
        for (int tries = 0; tries < 400; tries++) {
            const int x = random(1, Cfg::GRID_W - 1);
            const int y = random(1, Cfg::GRID_H - 1);
            if (tiles[y][x] != TILE_BRICK) continue;
            gateX = (uint8_t)x;
            gateY = (uint8_t)y;
            break;
        }

        // Hide some powerups under bricks.
        for (int y = 1; y < Cfg::GRID_H - 1; y++) {
            for (int x = 1; x < Cfg::GRID_W - 1; x++) {
                if (tiles[y][x] != TILE_BRICK) continue;
                if ((uint8_t)x == gateX && (uint8_t)y == gateY) continue;
                if (random(0, 100) >= Cfg::CHANCE_POWERUP) continue;

                const int r = random(0, 100);
                PickupType t = PU_BOOT;
                if (r < 25) t = PU_BOOT;
                else if (r < 55) t = PU_BOMB;
                else if (r < 80) t = PU_FLAME;
                else t = PU_SHIELD;
                spawnPickup((uint8_t)x, (uint8_t)y, t, false /*hidden*/);
            }
        }

        // Spawn enemies on empty tiles (avoid spawn zones).
        const int enemyCount = min((int)Cfg::MAX_ENEMIES, 2 + (int)level);
        int placed = 0;
        for (int tries = 0; tries < 2000 && placed < enemyCount; tries++) {
            const int x = random(1, Cfg::GRID_W - 1);
            const int y = random(1, Cfg::GRID_H - 1);
            if (tiles[y][x] != TILE_EMPTY) continue;
            // avoid near player spawns
            if ((abs(x - 1) <= 2 && abs(y - 1) <= 2) ||
                (abs(x - (Cfg::GRID_W - 2)) <= 2 && abs(y - 1) <= 2) ||
                (abs(x - 1) <= 2 && abs(y - (Cfg::GRID_H - 2)) <= 2) ||
                (abs(x - (Cfg::GRID_W - 2)) <= 2 && abs(y - (Cfg::GRID_H - 2)) <= 2)) continue;

            for (int i = 0; i < Cfg::MAX_ENEMIES; i++) {
                if (enemies[i].alive) continue;
                enemies[i].alive = true;
                // Mix enemy types: some chasers at higher levels.
                enemies[i].type = (uint8_t)((level >= 3 && random(0, 100) < (int)min(55, 10 + (int)level * 6)) ? 1 : 0);
                enemies[i].gx = (uint8_t)x;
                enemies[i].gy = (uint8_t)y;
                enemies[i].dir = (uint8_t)random(0, 4);
                // Speed up slightly with level; chasers are a bit faster.
                const uint32_t base = (uint32_t)max(160, 360 - (int)level * 18);
                enemies[i].moveIntervalMs = base - (enemies[i].type == 1 ? 60 : 0);
                enemies[i].nextTurnMs = millis() + (uint32_t)random(200, 520);
                placed++;
                break;
            }
        }

        // Setup players
        const uint16_t pCols[Cfg::MAX_PLAYERS] = {
            globalSettings.getPlayerColor(),
            COLOR_CYAN,
            COLOR_ORANGE,
            COLOR_PURPLE
        };
        const uint8_t spawnX[Cfg::MAX_PLAYERS] = { 1, (uint8_t)(Cfg::GRID_W - 2), 1, (uint8_t)(Cfg::GRID_W - 2) };
        const uint8_t spawnY[Cfg::MAX_PLAYERS] = { 1, 1, (uint8_t)(Cfg::GRID_H - 2), (uint8_t)(Cfg::GRID_H - 2) };

        for (int i = 0; i < Cfg::MAX_PLAYERS; i++) {
            Player& p = players[i];
            p.pad = (uint8_t)i;
            p.active = false;
            p.alive = false;
            p.color = pCols[i];
            p.bombsCap = Cfg::START_BOMBS;
            p.bombsActive = 0;
            p.range = Cfg::START_RANGE;
            p.speed = Cfg::START_SPEED;
            p.shield = false;
            p.respawnUntilMs = 0;
            p.lastA = false;

            p.gx = spawnX[i];
            p.gy = spawnY[i];
            p.px = (int16_t)toPx(p.gx);
            p.py = (int16_t)toPx(p.gy);
            p.dirX = 0; p.dirY = 0;
            p.wishX = 0; p.wishY = 0;
        }

        // Ensure spawn tiles are empty.
        for (int i = 0; i < Cfg::MAX_PLAYERS; i++) {
            tiles[spawnY[i]][spawnX[i]] = TILE_EMPTY;
        }
    }

    void plantBomb(Player& p, uint32_t now) {
        if (!p.alive) return;
        if (p.bombsActive >= p.bombsCap) return;
        // One bomb per tile
        for (int i = 0; i < Cfg::MAX_BOMBS; i++) {
            if (!bombs[i].active) continue;
            if (bombs[i].gx == p.gx && bombs[i].gy == p.gy) return;
        }
        for (int i = 0; i < Cfg::MAX_BOMBS; i++) {
            if (bombs[i].active) continue;
            bombs[i].active = true;
            bombs[i].owner = p.pad;
            bombs[i].gx = p.gx;
            bombs[i].gy = p.gy;
            bombs[i].plantedMs = now;
            bombs[i].range = p.range;
            p.bombsActive++;
            return;
        }
    }

    void explodeAt(uint8_t gx, uint8_t gy, uint32_t now) {
        if (!inBounds(gx, gy)) return;
        explT[gy][gx] = 255;
        // Damage players/enemies immediately.
        for (int i = 0; i < Cfg::MAX_PLAYERS; i++) {
            Player& p = players[i];
            if (!p.alive) continue;
            if (p.gx == gx && p.gy == gy) hitPlayer(p, now);
        }
        for (int i = 0; i < Cfg::MAX_ENEMIES; i++) {
            if (!enemies[i].alive) continue;
            if (enemies[i].gx == gx && enemies[i].gy == gy) {
                enemies[i].alive = false;
                score += Cfg::SCORE_KILL_ENEMY;
            }
        }
    }

    void breakBrick(uint8_t gx, uint8_t gy, uint32_t now) {
        if (!inBounds(gx, gy)) return;
        if (tiles[gy][gx] != TILE_BRICK) return;
        tiles[gy][gx] = TILE_EMPTY;
        breakT[gy][gx] = 255;
        (void)now;
        score += Cfg::SCORE_BREAK_BRICK;

        // Reveal gate if it was here.
        if (gateHidden && gx == gateX && gy == gateY) {
            gateHidden = false;
            gateRevealed = true;
            spawnPickup(gx, gy, PU_GATE, true);
        }

        // Reveal pickup hidden at this tile.
        for (int i = 0; i < Cfg::MAX_PICKUPS; i++) {
            if (!pickups[i].active) continue;
            if (pickups[i].revealed) continue;
            if (pickups[i].gx == gx && pickups[i].gy == gy) {
                pickups[i].revealed = true;
            }
        }
    }

    void detonateBomb(int bi, uint32_t now) {
        Bomb& b = bombs[bi];
        if (!b.active) return;
        b.active = false;

        // decrement owner's active bomb count
        if (b.owner < Cfg::MAX_PLAYERS) {
            Player& p = players[b.owner];
            if (p.bombsActive > 0) p.bombsActive--;
        }

        // Center
        explodeAt(b.gx, b.gy, now);

        // Propagate 4 dirs
        const int dx[4] = { 0, 0, -1, 1 };
        const int dy[4] = { -1, 1, 0, 0 };
        for (int dir = 0; dir < 4; dir++) {
            int x = (int)b.gx;
            int y = (int)b.gy;
            for (int step = 0; step < (int)b.range; step++) {
                x += dx[dir];
                y += dy[dir];
                if (!inBounds(x, y)) break;
                if (tiles[y][x] == TILE_SOLID) break;
                explodeAt((uint8_t)x, (uint8_t)y, now);
                // Chain bombs
                for (int j = 0; j < Cfg::MAX_BOMBS; j++) {
                    if (bombs[j].active && bombs[j].gx == (uint8_t)x && bombs[j].gy == (uint8_t)y) {
                        bombs[j].plantedMs = 0; // explode ASAP in updateBombs
                    }
                }
                if (tiles[y][x] == TILE_BRICK) {
                    breakBrick((uint8_t)x, (uint8_t)y, now);
                    break;
                }
            }
        }
    }

    void updateBombs(uint32_t now) {
        for (int i = 0; i < Cfg::MAX_BOMBS; i++) {
            if (!bombs[i].active) continue;
            if ((uint32_t)(now - bombs[i].plantedMs) >= Cfg::BOMB_FUSE_MS || bombs[i].plantedMs == 0) {
                detonateBomb(i, now);
            }
        }
    }

    void updateExplosions(uint32_t now) {
        // Decay explT + breakT
        for (int y = 0; y < Cfg::GRID_H; y++) {
            for (int x = 0; x < Cfg::GRID_W; x++) {
                if (explT[y][x] != 0) {
                    // simple decay
                    explT[y][x] = (explT[y][x] > 20) ? (uint8_t)(explT[y][x] - 20) : 0;
                }
                if (breakT[y][x] != 0) {
                    breakT[y][x] = (breakT[y][x] > 35) ? (uint8_t)(breakT[y][x] - 35) : 0;
                }
            }
        }
        (void)now;
    }

    void applyPickup(Player& p, PickupType t) {
        if (t == PU_BOOT) {
            if (p.speed < Cfg::MAX_SPEED) p.speed++;
        } else if (t == PU_BOMB) {
            if (p.bombsCap < 9) p.bombsCap++;
        } else if (t == PU_FLAME) {
            if (p.range < Cfg::MAX_RANGE) p.range++;
        } else if (t == PU_SHIELD) {
            p.shield = true;
        }
    }

    void updatePickups() {
        for (int i = 0; i < Cfg::MAX_PICKUPS; i++) {
            if (!pickups[i].active || !pickups[i].revealed) continue;
            // collected by any player
            for (int pi = 0; pi < Cfg::MAX_PLAYERS; pi++) {
                Player& p = players[pi];
                if (!p.alive) continue;
                if (p.gx == pickups[i].gx && p.gy == pickups[i].gy) {
                    if (pickups[i].type == PU_GATE) {
                        // Gate pickup is just a marker (drawn via gate state)
                    } else {
                        applyPickup(p, pickups[i].type);
                    }
                    // Remove pickup (gate persists via gate state)
                    if (pickups[i].type != PU_GATE) pickups[i].active = false;
                }
            }
        }
    }

    void hitPlayer(Player& p, uint32_t now) {
        if (p.shield) {
            p.shield = false;
            p.respawnUntilMs = now + 700;
            return;
        }
        p.alive = false;
    }

    void updateEnemies(uint32_t now) {
        // helper: find nearest alive player tile
        auto nearestPlayer = [&](uint8_t ex, uint8_t ey, uint8_t& outX, uint8_t& outY) -> bool {
            int bestD = 9999;
            bool found = false;
            for (int i = 0; i < Cfg::MAX_PLAYERS; i++) {
                const Player& p = players[i];
                if (!p.alive) continue;
                const int d = abs((int)p.gx - (int)ex) + abs((int)p.gy - (int)ey);
                if (d < bestD) { bestD = d; outX = p.gx; outY = p.gy; found = true; }
            }
            return found;
        };

        // helper: pick next dir via BFS one-step towards target
        auto bfsNextStep = [&](uint8_t sx, uint8_t sy, uint8_t tx, uint8_t ty, int8_t& stepDx, int8_t& stepDy) -> bool {
            stepDx = 0; stepDy = 0;
            if (sx == tx && sy == ty) return false;
            static int8_t prevDir[Cfg::GRID_H][Cfg::GRID_W];
            static uint8_t qx[Cfg::GRID_W * Cfg::GRID_H];
            static uint8_t qy[Cfg::GRID_W * Cfg::GRID_H];
            for (int y = 0; y < Cfg::GRID_H; y++) for (int x = 0; x < Cfg::GRID_W; x++) prevDir[y][x] = -1;
            int qh = 0, qt = 0;
            qx[qt] = sx; qy[qt] = sy; qt++;
            prevDir[sy][sx] = 4; // start marker
            const int dx[4] = { 0, 0, -1, 1 };
            const int dy[4] = { -1, 1, 0, 0 };
            while (qh < qt) {
                const uint8_t x = qx[qh];
                const uint8_t y = qy[qh];
                qh++;
                for (int dir = 0; dir < 4; dir++) {
                    const int nx = (int)x + dx[dir];
                    const int ny = (int)y + dy[dir];
                    if (!inBounds(nx, ny)) continue;
                    if (prevDir[ny][nx] != -1) continue;
                    if (isBlocked(nx, ny)) continue;
                    prevDir[ny][nx] = (int8_t)dir;
                    qx[qt] = (uint8_t)nx; qy[qt] = (uint8_t)ny; qt++;
                    if ((uint8_t)nx == tx && (uint8_t)ny == ty) {
                        // found target; backtrack one step
                        uint8_t bx = (uint8_t)nx;
                        uint8_t by = (uint8_t)ny;
                        int8_t last = prevDir[by][bx];
                        // Walk back until we reach start marker
                        while (!(bx == sx && by == sy)) {
                            const int8_t d0 = prevDir[by][bx];
                            // previous tile is inverse direction
                            int px = (int)bx - dx[d0];
                            int py = (int)by - dy[d0];
                            if ((uint8_t)px == sx && (uint8_t)py == sy) { last = d0; break; }
                            bx = (uint8_t)px; by = (uint8_t)py;
                        }
                        stepDx = (int8_t)dx[last];
                        stepDy = (int8_t)dy[last];
                        return true;
                    }
                }
            }
            return false;
        };

        for (int i = 0; i < Cfg::MAX_ENEMIES; i++) {
            Enemy& e = enemies[i];
            if (!e.alive) continue;
            // If in explosion -> die
            if (anyExplosionAt(e.gx, e.gy)) {
                e.alive = false;
                score += Cfg::SCORE_KILL_ENEMY;
                continue;
            }

            // Move occasionally
            if (now >= e.nextTurnMs) {
                int8_t mdx = 0, mdy = 0;
                if (e.type == 1) {
                    uint8_t tx = 0, ty = 0;
                    if (nearestPlayer(e.gx, e.gy, tx, ty)) {
                        (void)bfsNextStep(e.gx, e.gy, tx, ty, mdx, mdy);
                    }
                }
                // fallback random or if BFS didn't find
                if (mdx == 0 && mdy == 0) {
                    const int dx[4] = { 0, 0, -1, 1 };
                    const int dy[4] = { -1, 1, 0, 0 };
                    int tries = 0;
                    while (tries < 8) {
                        const int dir = (tries == 0) ? (int)e.dir : (int)random(0, 4);
                        const int nx = (int)e.gx + dx[dir];
                        const int ny = (int)e.gy + dy[dir];
                        if (!isBlocked(nx, ny)) { mdx = (int8_t)dx[dir]; mdy = (int8_t)dy[dir]; e.dir = (uint8_t)dir; break; }
                        tries++;
                    }
                }
                if (mdx != 0 || mdy != 0) {
                    const int nx = (int)e.gx + (int)mdx;
                    const int ny = (int)e.gy + (int)mdy;
                    if (!isBlocked(nx, ny)) { e.gx = (uint8_t)nx; e.gy = (uint8_t)ny; }
                }
                e.nextTurnMs = now + e.moveIntervalMs;
            }

            // Contact damage players
            for (int pi = 0; pi < Cfg::MAX_PLAYERS; pi++) {
                Player& p = players[pi];
                if (!p.alive) continue;
                if (p.gx == e.gx && p.gy == e.gy) hitPlayer(p, now);
            }
        }
    }

    void updatePlayers(ControllerManager* input, uint32_t now) {
        for (int i = 0; i < Cfg::MAX_PLAYERS; i++) {
            Player& p = players[i];
            ControllerPtr ctl = input ? input->getController(i) : nullptr;
            const bool connected = (ctl && ctl->isConnected());
            p.active = connected;
            if (connected && !p.everConnected) {
                p.everConnected = true;
                // spawn at corner
                const uint8_t spawnX[Cfg::MAX_PLAYERS] = { 1, (uint8_t)(Cfg::GRID_W - 2), 1, (uint8_t)(Cfg::GRID_W - 2) };
                const uint8_t spawnY[Cfg::MAX_PLAYERS] = { 1, 1, (uint8_t)(Cfg::GRID_H - 2), (uint8_t)(Cfg::GRID_H - 2) };
                spawnPlayerAt(p, spawnX[i], spawnY[i]);
            }
            if (!connected) continue;
            if (!p.alive) continue;

            // Explosion damage
            if (anyExplosionAt(p.gx, p.gy)) hitPlayer(p, now);

            // Movement (pixel-based inside 4x4 cells)
            const uint8_t d = ctl->dpad();
            p.wishX = 0; p.wishY = 0;
            if (dUp(d)) p.wishY = -1;
            else if (dDown(d)) p.wishY = 1;
            else if (dLeft(d)) p.wishX = -1;
            else if (dRight(d)) p.wishX = 1;

            // speed 1..7 -> pxStep 1..3
            const int pxStep = 1 + ((int)p.speed - 1) / 3;

            // Current grid based on px
            p.gx = (uint8_t)constrain((int)(p.px / Cfg::CELL), 0, Cfg::GRID_W - 1);
            p.gy = (uint8_t)constrain((int)(p.py / Cfg::CELL), 0, Cfg::GRID_H - 1);
            const int offX = p.px % Cfg::CELL;
            const int offY = p.py % Cfg::CELL;
            const bool alignedX = (offX == 0);
            const bool alignedY = (offY == 0);

            // Soft "auto-centering" towards grid lines for smoother cornering.
            // When moving horizontally, try to nudge Y to nearest aligned if possible (and vice versa).
            auto tryNudgeToAlign = [&](bool movingHoriz) {
                if (movingHoriz) {
                    if (alignedY) return;
                    const int downDist = (Cfg::CELL - offY) % Cfg::CELL;
                    const int upDist = offY;
                    const int dir = (upDist <= downDist) ? -1 : 1; // nudge to nearest
                    const int ny = p.py + dir;
                    const int ngy = (ny + (dir > 0 ? (Cfg::CELL - 1) : 0)) / Cfg::CELL;
                    // Only nudge if still in same column and target tile isn't blocked.
                    if (!isTileBlockedForPlayer((int)p.gx, ngy)) {
                        p.py = (int16_t)constrain(ny, 0, (Cfg::GRID_H - 1) * Cfg::CELL);
                    }
                } else {
                    if (alignedX) return;
                    const int rightDist = (Cfg::CELL - offX) % Cfg::CELL;
                    const int leftDist = offX;
                    const int dir = (leftDist <= rightDist) ? -1 : 1;
                    const int nx = p.px + dir;
                    const int ngx = (nx + (dir > 0 ? (Cfg::CELL - 1) : 0)) / Cfg::CELL;
                    if (!isTileBlockedForPlayer(ngx, (int)p.gy)) {
                        p.px = (int16_t)constrain(nx, 0, (Cfg::GRID_W - 1) * Cfg::CELL);
                    }
                }
            };

            // Allow turning when aligned to grid.
            if ((p.wishX != 0 || p.wishY != 0) && ((p.wishX != 0 && alignedY) || (p.wishY != 0 && alignedX))) {
                const int ngx = (int)p.gx + (int)p.wishX;
                const int ngy = (int)p.gy + (int)p.wishY;
                if (!isBlocked(ngx, ngy)) {
                    p.dirX = p.wishX;
                    p.dirY = p.wishY;
                }
            }

            // Move in current direction (if not blocked when crossing cell boundary)
            if (p.dirX != 0 || p.dirY != 0) {
                // Nudge perpendicular axis towards alignment for smoother cornering.
                tryNudgeToAlign(p.dirX != 0);

                int nxPx = p.px + p.dirX * pxStep;
                int nyPx = p.py + p.dirY * pxStep;

                // Determine the next cell we are trying to enter ONLY when crossing a boundary.
                int nextGx = (int)p.gx;
                int nextGy = (int)p.gy;
                bool crossing = false;
                if (p.dirX > 0 && (offX + pxStep) >= Cfg::CELL) { nextGx = (int)p.gx + 1; crossing = true; }
                if (p.dirX < 0 && (offX - pxStep) < 0)           { nextGx = (int)p.gx - 1; crossing = true; }
                if (p.dirY > 0 && (offY + pxStep) >= Cfg::CELL) { nextGy = (int)p.gy + 1; crossing = true; }
                if (p.dirY < 0 && (offY - pxStep) < 0)           { nextGy = (int)p.gy - 1; crossing = true; }

                if (!crossing || !isBlocked(nextGx, nextGy)) {
                    // Apply movement; clamp within playfield.
                    p.px = (int16_t)constrain(nxPx, 0, (Cfg::GRID_W - 1) * Cfg::CELL);
                    p.py = (int16_t)constrain(nyPx, 0, (Cfg::GRID_H - 1) * Cfg::CELL);
                    p.gx = (uint8_t)constrain((int)(p.px / Cfg::CELL), 0, Cfg::GRID_W - 1);
                    p.gy = (uint8_t)constrain((int)(p.py / Cfg::CELL), 0, Cfg::GRID_H - 1);
                } else {
                    // Stop at cell boundary.
                    if (p.dirX != 0) p.px = (int16_t)toPx(p.gx);
                    if (p.dirY != 0) p.py = (int16_t)toPx(p.gy);
                    p.dirX = 0; p.dirY = 0;
                }
            }

            // Place bomb (A edge)
            const bool aNow = (bool)ctl->a();
            const bool aEdge = aNow && !p.lastA;
            p.lastA = aNow;
            if (aEdge) plantBomb(p, now);
        }
    }

    int alivePlayers() const {
        int n = 0;
        for (int i = 0; i < Cfg::MAX_PLAYERS; i++) if (players[i].alive) n++;
        return n;
    }

    int aliveEnemies() const {
        int n = 0;
        for (int i = 0; i < Cfg::MAX_ENEMIES; i++) if (enemies[i].alive) n++;
        return n;
    }

    void updateGate(uint32_t now) {
        (void)now;
        if (gateRevealed && !gateOpen) {
            gateOpen = (aliveEnemies() == 0);
        }
        if (gateOpen) {
            // Level clear if any alive player stands on gate tile
            for (int i = 0; i < Cfg::MAX_PLAYERS; i++) {
                Player& p = players[i];
                if (!p.alive) continue;
                if (p.gx == gateX && p.gy == gateY) {
                    score += Cfg::SCORE_LEVEL_CLEAR;
                    level++;
                    generateLevel(now);
                    // reset players positions but keep powerups
                    const uint8_t spawnX[Cfg::MAX_PLAYERS] = { 1, (uint8_t)(Cfg::GRID_W - 2), 1, (uint8_t)(Cfg::GRID_W - 2) };
                    const uint8_t spawnY[Cfg::MAX_PLAYERS] = { 1, 1, (uint8_t)(Cfg::GRID_H - 2), (uint8_t)(Cfg::GRID_H - 2) };
                    for (int pi = 0; pi < Cfg::MAX_PLAYERS; pi++) {
                        if (!players[pi].everConnected) continue;
                        if (!players[pi].alive) continue;
                        spawnPlayerAt(players[pi], spawnX[pi], spawnY[pi]);
                    }
                    return;
                }
            }
        }
    }

public:
    void start() override {
        randomSeed((uint32_t)micros() ^ (uint32_t)millis());
        score = 0;
        level = 1;
        gameOver = false;
        generateLevel((uint32_t)millis());
        // Reset player connection state for a new run.
        for (int i = 0; i < Cfg::MAX_PLAYERS; i++) {
            players[i].active = false;
            players[i].everConnected = false;
            players[i].alive = false;
        }
    }

    void reset() override {
        start();
    }

    void update(ControllerManager* input) override {
        if (gameOver) return;
        const uint32_t now = (uint32_t)millis();
        if ((uint32_t)(now - lastTickMs) < Cfg::TICK_MS) return;
        lastTickMs = now;

        updatePlayers(input, now);
        updateBombs(now);
        updateExplosions(now);
        updateEnemies(now);
        updatePickups();
        updateGate(now);

        // Game over when no players alive (but at least one controller was connected)
        if (alivePlayers() == 0) {
            bool anyConnected = false;
            for (int i = 0; i < Cfg::MAX_PLAYERS; i++) {
                if (input && input->getController(i) && input->getController(i)->isConnected()) { anyConnected = true; break; }
            }
            if (anyConnected) {
                gameOver = true;
            }
        }
    }

    void draw(MatrixPanel_I2S_DMA* display) override {
        if (!display) return;
        display->fillScreen(COLOR_BLACK);

        if (gameOver) {
            char tag[4];
            UserProfiles::getPadTag(0, tag);
            GameOverLeaderboardView::draw(display, "GAME OVER", leaderboardId(), leaderboardScore(), tag);
            return;
        }

        // HUD
        SmallFont::drawString(display, 2, 6, "BOMBER", COLOR_CYAN);
        SmallFont::drawStringF(display, 36, 6, COLOR_YELLOW, "L%u", (unsigned)level);
        SmallFont::drawStringF(display, 52, 6, COLOR_YELLOW, "%lu", (unsigned long)score);
        for (int x = 0; x < PANEL_RES_X; x += 2) display->drawPixel(x, Cfg::HUD_H - 1, COLOR_BLUE);

        // Tiles
        for (int gy = 0; gy < Cfg::GRID_H; gy++) {
            for (int gx = 0; gx < Cfg::GRID_W; gx++) {
                const int px = Cfg::ORIGIN_X + gx * Cfg::CELL;
                const int py = Cfg::ORIGIN_Y + gy * Cfg::CELL;

                uint16_t c = Cfg::COL_FLOOR;
                if (tiles[gy][gx] == TILE_SOLID) c = Cfg::COL_SOLID;
                else if (tiles[gy][gx] == TILE_BRICK) c = Cfg::COL_BRICK;
                display->fillRect(px, py, Cfg::CELL, Cfg::CELL, c);

                // Break animation overlay (debris)
                if (breakT[gy][gx] != 0) {
                    // Simple debris: 3 frames based on intensity
                    const uint8_t bt = breakT[gy][gx];
                    const uint16_t dc1 = (bt > 170) ? COLOR_YELLOW : (bt > 90) ? COLOR_ORANGE : dim565(COLOR_ORANGE, 140);
                    display->drawPixel(px + 1, py + 1, dc1);
                    if (bt > 120) display->drawPixel(px + 2, py + 1, dc1);
                    if (bt > 70)  display->drawPixel(px + 1, py + 2, dc1);
                    if (bt > 150) display->drawPixel(px + 2, py + 2, dc1);
                }

                // Gate
                if (gateRevealed && gx == gateX && gy == gateY) {
                    const uint16_t gc = gateOpen ? Cfg::COL_GATE_OPEN : Cfg::COL_GATE_LOCKED;
                    display->drawRect(px, py, Cfg::CELL, Cfg::CELL, gc);
                }

                // Pickup (revealed)
                for (int i = 0; i < Cfg::MAX_PICKUPS; i++) {
                    if (!pickups[i].active || !pickups[i].revealed) continue;
                    if (pickups[i].gx != gx || pickups[i].gy != gy) continue;
                    if (pickups[i].type == PU_GATE) break; // gate is drawn via gate state

                    const uint8_t (*spr)[4] = PU_BOOT_4;
                    uint16_t pc = COLOR_WHITE;
                    if (pickups[i].type == PU_BOOT) { spr = PU_BOOT_4; pc = COLOR_WHITE; }
                    if (pickups[i].type == PU_BOMB) { spr = PU_BOMB_4; pc = COLOR_YELLOW; }
                    if (pickups[i].type == PU_FLAME) { spr = PU_FLAME_4; pc = COLOR_ORANGE; }
                    if (pickups[i].type == PU_SHIELD) { spr = PU_SHIELD_4; pc = COLOR_CYAN; }

                    for (int yy = 0; yy < 4; yy++) {
                        for (int xx = 0; xx < 4; xx++) {
                            if (!spr[yy][xx]) continue;
                            display->drawPixel(px + xx, py + yy, pc);
                        }
                    }
                }

                // Explosion overlay (animated)
                if (explT[gy][gx] != 0) {
                    // Animate by intensity: bright -> full fill, mid -> plus, low -> sparks
                    const uint8_t et = explT[gy][gx];
                    if (et > 170) {
                        display->fillRect(px, py, Cfg::CELL, Cfg::CELL, Cfg::COL_EXPLO1);
                    } else if (et > 90) {
                        display->fillRect(px, py, Cfg::CELL, Cfg::CELL, Cfg::COL_EXPLO2);
                        // plus highlight
                        display->drawPixel(px + 1, py + 1, Cfg::COL_EXPLO1);
                        display->drawPixel(px + 2, py + 1, Cfg::COL_EXPLO1);
                        display->drawPixel(px + 1, py + 2, Cfg::COL_EXPLO1);
                        display->drawPixel(px + 2, py + 2, Cfg::COL_EXPLO1);
                    } else {
                        display->drawPixel(px + 1, py + 1, Cfg::COL_EXPLO2);
                        display->drawPixel(px + 2, py + 2, Cfg::COL_EXPLO1);
                    }
                }
            }
        }

        // Bombs
        for (int i = 0; i < Cfg::MAX_BOMBS; i++) {
            if (!bombs[i].active) continue;
            const int px = Cfg::ORIGIN_X + bombs[i].gx * Cfg::CELL;
            const int py = Cfg::ORIGIN_Y + bombs[i].gy * Cfg::CELL;
            const uint32_t age = (uint32_t)(millis() - bombs[i].plantedMs);
            const bool blink = ((age / 120) % 2) == 0;
            const uint16_t bc = blink ? COLOR_WHITE : COLOR_YELLOW;
            display->fillRect(px + 1, py + 1, Cfg::CELL - 2, Cfg::CELL - 2, bc);
        }

        // Enemies
        for (int i = 0; i < Cfg::MAX_ENEMIES; i++) {
            if (!enemies[i].alive) continue;
            const int px = Cfg::ORIGIN_X + enemies[i].gx * Cfg::CELL;
            const int py = Cfg::ORIGIN_Y + enemies[i].gy * Cfg::CELL;
            const uint8_t (*spr)[4] = (enemies[i].type == 1) ? ENEMY_CHASER_4 : ENEMY_RANDOM_4;
            const uint16_t ec = (enemies[i].type == 1) ? COLOR_RED : Cfg::COL_ENEMY;
            for (int yy = 0; yy < 4; yy++) {
                for (int xx = 0; xx < 4; xx++) {
                    if (!spr[yy][xx]) continue;
                    display->drawPixel(px + xx, py + yy, ec);
                }
            }
        }

        // Players (their own colors)
        for (int i = 0; i < Cfg::MAX_PLAYERS; i++) {
            const Player& p = players[i];
            if (!p.alive) continue;
            const int px = Cfg::ORIGIN_X + p.gx * Cfg::CELL;
            const int py = Cfg::ORIGIN_Y + p.gy * Cfg::CELL;
            display->fillRect(px + 1, py + 1, Cfg::CELL - 2, Cfg::CELL - 2, p.color);
            if (p.shield) display->drawRect(px, py, Cfg::CELL, Cfg::CELL, COLOR_CYAN);
        }
    }

    bool isGameOver() override { return gameOver; }

    // Leaderboard
    bool leaderboardEnabled() const override { return true; }
    const char* leaderboardId() const override { return "bomber"; }
    const char* leaderboardName() const override { return "Bomber"; }
    uint32_t leaderboardScore() const override { return score; }
};


