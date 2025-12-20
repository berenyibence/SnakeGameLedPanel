#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../../engine/GameBase.h"
#include "../../engine/ControllerManager.h"
#include "../../engine/config.h"
#include "../../component/SmallFont.h"
#include "../../engine/Settings.h"
#include "../../engine/UserProfiles.h"
#include "../../component/GameOverLeaderboardView.h"
#include "LabyrinthGameConfig.h"

/**
 * LabyrinthGame - Maze navigation game
 * Player navigates through a maze to reach the exit
 */
class LabyrinthGame : public GameBase {
private:
    // ---------------------------------------------------------
    // Layout / Difficulty
    // ---------------------------------------------------------
    static constexpr int HUD_H = LabyrinthGameConfig::HUD_H; // top HUD band (matches other games)

    // We support up to 1x1 tiles (63x55-ish cells after HUD), so allocate a safe max.
    static constexpr int MAX_MAZE_W = LabyrinthGameConfig::MAX_MAZE_W;   // 64
    static constexpr int MAX_MAZE_H = LabyrinthGameConfig::MAX_MAZE_H;   // 64 (we'll use less due to HUD)
    static constexpr int MAX_CELLS = MAX_MAZE_W * MAX_MAZE_H; // 4096

    // Player structure (INTEGER-ONLY fixed-point movement + pixel-accurate collision)
    struct Player {
        // Top-left position in screen pixels, in 8.8 fixed-point.
        // (x_fp >> 8) gives integer pixel coordinate.
        int32_t x_fp;
        int32_t y_fp;

        // Velocity in 8.8 fixed-point pixels PER TICK (not per second).
        int32_t vx_fp;
        int32_t vy_fp;

        // Max speed in px/s (integer). We derive per-tick velocity from this.
        uint16_t maxSpeedPxPerS;
        uint16_t color;
        uint8_t sizePx;      // draw size (>=1)
        
        Player()
            : x_fp(0), y_fp(0),
              vx_fp(0), vy_fp(0),
              maxSpeedPxPerS(28),
              color(COLOR_GREEN), sizePx(2) {}
    };
    
    Player player;
    bool gameOver;
    int level;
    unsigned long lastUpdate;
    unsigned long levelCompleteTime;
    static constexpr int UPDATE_INTERVAL_MS = LabyrinthGameConfig::UPDATE_INTERVAL_MS;  // ~60 FPS (render is capped by engine)

    // Score / timer
    uint32_t score = 0;
    uint32_t levelStartTimeMs = 0;
    uint16_t cachedSecondsLeft = 60;

    // Level transition (NOT game over)
    bool levelComplete = false;
    uint16_t secondsLeftAtComplete = 0;
    enum LevelCompletePhase : uint8_t {
        PHASE_CLEAR_ANIM = 0,
        PHASE_TEXT = 1
    };
    LevelCompletePhase levelPhase = PHASE_CLEAR_ANIM;

    // Brightness fade animation (used for outro fade-out and intro fade-in).
    // IMPORTANT: HUD must not be affected.
    // We fade ONLY the labyrinth drawing area by scaling colors toward black.
    enum AnimMode : uint8_t { ANIM_NONE = 0, ANIM_FADE_OUT = 1, ANIM_FADE_IN = 2 };
    AnimMode animMode = ANIM_NONE;
    uint32_t animStartMs = 0;
    uint16_t animDurationMs = 0;

    // Dynamic maze sizing based on level difficulty:
    // - Levels 1..10: 4x4 tiles (easy)
    // - Levels 11..20: 2x2 tiles (medium)
    // - Levels 21+: 1x1 tiles (hard)
    int cellSizePx = 4;
    int mazeW = 0;  // in cells
    int mazeH = 0;  // in cells
    int mazeOriginX = 0; // screen-space origin for drawing (centered)
    int mazeOriginY = HUD_H; // screen-space origin for drawing (centered within playfield)
    
    // Maze data: 0 = wall, 1 = path, 2 = start, 3 = exit
    uint8_t maze[MAX_MAZE_H][MAX_MAZE_W];
    int exitX, exitY;
    // IMPORTANT (ESP32):
    // Do NOT allocate large temporary buffers as class members (heap) or locals (stack).
    // We keep maze generation scratch buffers as static storage inside the generator functions
    // (BSS), and we pack (x,y) into a single uint16_t cell index to keep memory low.

    // Analog input smoothing / deadzone
    static constexpr int16_t AXIS_DIVISOR = LabyrinthGameConfig::AXIS_DIVISOR;   // Bluepad32 commonly ~[-512..512]
    static constexpr int16_t STICK_DEADZONE_RAW = LabyrinthGameConfig::STICK_DEADZONE_RAW;
    static constexpr uint8_t VEL_SMOOTH_NUM = LabyrinthGameConfig::VEL_SMOOTH_NUM;
    static constexpr uint8_t VEL_SMOOTH_DEN = LabyrinthGameConfig::VEL_SMOOTH_DEN;
    static constexpr uint8_t STOP_FRICTION_NUM = LabyrinthGameConfig::STOP_FRICTION_NUM;
    static constexpr uint8_t STOP_FRICTION_DEN = LabyrinthGameConfig::STOP_FRICTION_DEN;
    
    /**
     * Generate a simple maze using depth-first carve for smoother paths
     */
    static inline uint8_t tileSizeForLevel(int lvl) { return LabyrinthGameConfig::tileSizeForLevel(lvl); }

    // Bluepad32 analog helper (SFINAE) so we don't hard-depend on a single API surface.
    struct InputDetail {
        template <typename T>
        static auto axisX(T* c, int) -> decltype(c->axisX(), int16_t()) { return (int16_t)c->axisX(); }
        template <typename T>
        static int16_t axisX(T*, ...) { return 0; }

        template <typename T>
        static auto axisY(T* c, int) -> decltype(c->axisY(), int16_t()) { return (int16_t)c->axisY(); }
        template <typename T>
        static int16_t axisY(T*, ...) { return 0; }
    };

    // Fixed-point helpers (8.8)
    static constexpr int FP_SHIFT = 8;
    static constexpr int32_t FP_ONE = (1 << FP_SHIFT);
    static inline int32_t toFp(int32_t px) { return (px << FP_SHIFT); }
    static inline int32_t fpToIntFloor(int32_t vfp) { return (vfp >> FP_SHIFT); }
    static inline int32_t fpAbs(int32_t v) { return (v < 0) ? -v : v; }
    static inline int32_t fpSign(int32_t v) { return (v < 0) ? -1 : (v > 0) ? 1 : 0; }

    // ---------------------------------------------------------
    // Pixel-accurate collision mask (what you see is what you collide with)
    // ---------------------------------------------------------
    // solid[y][x] is 1 if that SCREEN pixel is solid (wall/outside maze/HUD),
    // and 0 if it is walkable.
    //
    // Why this approach:
    // - It guarantees physics matches visuals 1:1 on a 64x64 panel.
    // - It is simple and robust across different `cellSizePx` values and centering offsets.
    // - Memory is tiny: 64*64 = 4096 bytes.
    uint8_t solid[PANEL_RES_Y][PANEL_RES_X];

    void buildSolidMaskFromMaze() {
        // Default: everything solid. Then carve out walkable path pixels.
        for (int y = 0; y < PANEL_RES_Y; y++) {
            for (int x = 0; x < PANEL_RES_X; x++) {
                solid[y][x] = 1;
            }
        }

        // Maze is only drawn below HUD.
        for (int my = 0; my < mazeH; my++) {
            for (int mx = 0; mx < mazeW; mx++) {
                const bool walkable = (maze[my][mx] != 0);
                const int sx0 = mazeOriginX + mx * cellSizePx;
                const int sy0 = mazeOriginY + my * cellSizePx;
                for (int py = 0; py < cellSizePx; py++) {
                    const int sy = sy0 + py;
                    if (sy < 0 || sy >= PANEL_RES_Y) continue;
                    for (int px = 0; px < cellSizePx; px++) {
                        const int sx = sx0 + px;
                        if (sx < 0 || sx >= PANEL_RES_X) continue;
                        solid[sy][sx] = walkable ? 0 : 1;
                    }
                }
            }
        }
    }

    static inline int16_t applyDeadzoneRaw(int16_t v, int16_t dz) {
        if (v > -dz && v < dz) return 0;
        return v;
    }

    static inline int8_t signNonZero(int16_t v) {
        return (v < 0) ? -1 : (v > 0) ? 1 : 0;
    }

    static inline int32_t computeMaxStepPerTickFp(uint16_t speedPxPerS) {
        // Convert px/s -> px/tick in 8.8 fixed-point:
        // step_fp = speed * 256 * dt_ms / 1000
        // Keep it in 32-bit to avoid overflow.
        const uint32_t dt = (uint32_t)UPDATE_INTERVAL_MS;
        const uint32_t numer = (uint32_t)speedPxPerS * (uint32_t)FP_ONE * dt;
        return (int32_t)(numer / 1000UL);
    }

    static inline int32_t lerpInt32(int32_t cur, int32_t target, uint8_t num, uint8_t den) {
        // cur += (target-cur) * num / den
        const int32_t delta = target - cur;
        return cur + (int32_t)((int64_t)delta * (int64_t)num / (int64_t)den);
    }

    void computeMazeDimensions() {
        cellSizePx = (int)tileSizeForLevel(level);

        // Maze is drawn below the HUD band.
        mazeW = PANEL_RES_X / cellSizePx;
        mazeH = (PANEL_RES_Y - HUD_H) / cellSizePx;

        // DFS carving uses 2-cell steps, which works best on odd dimensions.
        if ((mazeW & 1) == 0) mazeW--;
        if ((mazeH & 1) == 0) mazeH--;

        // Safety minimum for a playable maze.
        if (mazeW < 7) mazeW = 7;
        if (mazeH < 7) mazeH = 7;

        // Center the maze inside the available playfield area (below HUD).
        // This naturally shifts:
        // - 4x4 mode (15*4=60px) by +2px
        // - 2x2 mode (31*2=62px) by +1px
        const int playWpx = mazeW * cellSizePx;
        const int playHpx = mazeH * cellSizePx;
        const int availWpx = PANEL_RES_X;
        const int availHpx = PANEL_RES_Y - HUD_H;
        mazeOriginX = (availWpx - playWpx) / 2;
        mazeOriginY = HUD_H + (availHpx - playHpx) / 2;
    }

    void clearMazeToWalls() {
        for (int y = 0; y < mazeH; y++) {
            for (int x = 0; x < mazeW; x++) {
                maze[y][x] = 0; // wall
            }
        }
    }

    bool isInBounds(int x, int y) const {
        return (x >= 0 && x < mazeW && y >= 0 && y < mazeH);
    }

    int countWalkableNeighbors(int x, int y) const {
        int c = 0;
        if (isInBounds(x, y - 1) && maze[y - 1][x] != 0) c++;
        if (isInBounds(x, y + 1) && maze[y + 1][x] != 0) c++;
        if (isInBounds(x - 1, y) && maze[y][x - 1] != 0) c++;
        if (isInBounds(x + 1, y) && maze[y][x + 1] != 0) c++;
        return c;
    }

    /**
     * Add "false leads" by extending some dead-ends into longer corridors.
     * This increases the chance of getting lost without requiring a heavier algorithm.
     */
    void extendDeadEnds(uint16_t extensions, uint8_t maxSteps) {
        if (extensions == 0) return;

        // IMPORTANT (ESP32): Avoid large stack buffers here.
        // Instead of building a full list of dead ends, we sample randomly and
        // only extend when we hit a valid dead-end cell.
        const uint16_t attemptsPerExtension = 60;

        for (uint16_t i = 0; i < extensions; i++) {
            int x = -1, y = -1;

            // Find a dead-end candidate by random sampling.
            for (uint16_t a = 0; a < attemptsPerExtension; a++) {
                const int rx = random(1, mazeW - 1);
                const int ry = random(1, mazeH - 1);
                if (maze[ry][rx] == 0) continue;
                if (maze[ry][rx] == 2 || maze[ry][rx] == 3) continue; // avoid start/exit tiles
                if (countWalkableNeighbors(rx, ry) != 1) continue;     // must be a dead end
                x = rx; y = ry;
                break;
            }

            if (x < 0 || y < 0) {
                // Couldn't find more dead ends (or maze is already too loopy).
                break;
            }

            for (uint8_t step = 0; step < maxSteps; step++) {
                // Find carve directions that go into walls (prefer not immediately linking back).
                int dirs[4];
                int dCount = 0;
                // Up
                if (isInBounds(x, y - 1) && maze[y - 1][x] == 0) dirs[dCount++] = 0;
                // Down
                if (isInBounds(x, y + 1) && maze[y + 1][x] == 0) dirs[dCount++] = 1;
                // Left
                if (isInBounds(x - 1, y) && maze[y][x - 1] == 0) dirs[dCount++] = 2;
                // Right
                if (isInBounds(x + 1, y) && maze[y][x + 1] == 0) dirs[dCount++] = 3;

                if (dCount == 0) break;

                const int d = dirs[random(0, dCount)];
                int nx = x, ny = y;
                if (d == 0) ny--;
                else if (d == 1) ny++;
                else if (d == 2) nx--;
                else nx++;

                // Carve one cell forward.
                maze[ny][nx] = 1;
                x = nx; y = ny;

                // If we accidentally created a junction, stop; we want long-ish corridors.
                if (countWalkableNeighbors(x, y) >= 2) break;
            }
        }
    }

    /**
     * Add loops/junctions by opening some walls that connect two existing paths.
     * This removes the "tree" property (single-solution), making navigation harder.
     */
    void addLoops(uint16_t openings) {
        if (openings == 0) return;

        for (uint16_t i = 0; i < openings; i++) {
            // Try a few random samples to find a good wall to open.
            for (uint8_t tries = 0; tries < 18; tries++) {
                const int x = random(1, mazeW - 1);
                const int y = random(1, mazeH - 1);
                if (maze[y][x] != 0) continue; // already open

                const bool up = (maze[y - 1][x] != 0);
                const bool down = (maze[y + 1][x] != 0);
                const bool left = (maze[y][x - 1] != 0);
                const bool right = (maze[y][x + 1] != 0);

                // Open a wall that connects two opposite corridors, forming a loop.
                const bool vertical = up && down && !left && !right;
                const bool horizontal = left && right && !up && !down;
                if (vertical || horizontal) {
                    maze[y][x] = 1;
                    break;
                }
            }
        }
    }

    void carvePerfectMaze(int startX, int startY) {
        // DFS stack (static storage to avoid stack overflow and heap allocation).
        static uint16_t stack[MAX_CELLS];
        int top = 0;
        auto pack = [&](int x, int y) -> uint16_t { return (uint16_t)(y * mazeW + x); };
        auto unpackX = [&](uint16_t v) -> int { return (int)(v % (uint16_t)mazeW); };
        auto unpackY = [&](uint16_t v) -> int { return (int)(v / (uint16_t)mazeW); };

        stack[top] = pack(startX, startY);
        maze[startY][startX] = 1; // path

        // Direction vectors (up, down, left, right)
        const int dx[4] = { 0, 0, -1, 1 };
        const int dy[4] = { -1, 1, 0, 0 };

        while (top >= 0) {
            const int cx = unpackX(stack[top]);
            const int cy = unpackY(stack[top]);

            int neighbors[4];
            int nCount = 0;
            for (int dir = 0; dir < 4; dir++) {
                const int nx = cx + dx[dir] * 2;
                const int ny = cy + dy[dir] * 2;
                if (nx > 0 && nx < mazeW - 1 && ny > 0 && ny < mazeH - 1) {
                    if (maze[ny][nx] == 0) neighbors[nCount++] = dir;
                }
            }

            if (nCount == 0) {
                top--;
                continue;
            }

            const int dir = neighbors[random(0, nCount)];
            const int nx = cx + dx[dir] * 2;
            const int ny = cy + dy[dir] * 2;
            const int bx = cx + dx[dir];
            const int by = cy + dy[dir];

            // Carve bridge + destination.
            maze[by][bx] = 1;
            maze[ny][nx] = 1;

            top++;
            stack[top] = pack(nx, ny);
        }
    }

    void pickFarthestExitFrom(int startX, int startY) {
        // BFS to guarantee the exit is reachable (fixes the "inaccessible exit" issue).
        static uint16_t q[MAX_CELLS];
        static int16_t dist[MAX_CELLS];
        const int total = mazeW * mazeH;
        for (int i = 0; i < total; i++) dist[i] = (int16_t)-1;

        auto idx = [&](int x, int y) { return y * mazeW + x; };
        auto unpackX = [&](uint16_t v) -> int { return (int)(v % (uint16_t)mazeW); };
        auto unpackY = [&](uint16_t v) -> int { return (int)(v / (uint16_t)mazeW); };

        int head = 0;
        int tail = 0;
        q[tail] = (uint16_t)idx(startX, startY);
        dist[idx(startX, startY)] = 0;
        tail++;

        const int dx[4] = { 0, 0, -1, 1 };
        const int dy[4] = { -1, 1, 0, 0 };

        int bestX = startX;
        int bestY = startY;
        int bestD = 0;

        while (head < tail) {
            const uint16_t cur = q[head];
            const int cx = unpackX(cur);
            const int cy = unpackY(cur);
            const int cd = (int)dist[idx(cx, cy)];
            head++;

            if (cd > bestD) {
                bestD = cd;
                bestX = cx;
                bestY = cy;
            }

            for (int dir = 0; dir < 4; dir++) {
                const int nx = cx + dx[dir];
                const int ny = cy + dy[dir];
                if (!isInBounds(nx, ny)) continue;
                if (maze[ny][nx] == 0) continue; // wall
                const int ni = idx(nx, ny);
                if (dist[ni] != (int16_t)-1) continue;
                dist[ni] = (int16_t)(cd + 1);
                q[tail] = (uint16_t)ni;
                tail++;
            }
        }

        exitX = bestX;
        exitY = bestY;
    }

    void generateMaze() {
        computeMazeDimensions();
        clearMazeToWalls();

        const int startX = 1;
        const int startY = 1;

        carvePerfectMaze(startX, startY);

        // Difficulty shaping:
        // 1) Extend some dead ends to create longer false leads (more "getting lost").
        // 2) Add some loops/junctions so there isn't a single clean path to follow.
        //
        // The smaller the tile size, the more cells we have; keep counts bounded.
        const int cells = mazeW * mazeH;
        const uint16_t deadEndExtensions = (uint16_t)constrain((cells / 90) + (level / 3), 4, 60);
        const uint16_t loopOpenings = (uint16_t)constrain((cells / 140) + (level / 4), 2, 40);
        extendDeadEnds(deadEndExtensions, (uint8_t)constrain(4 + level / 4, 4, 10));
        addLoops(loopOpenings);

        pickFarthestExitFrom(startX, startY);

        // Mark start & exit
        maze[startY][startX] = 2;
        maze[exitY][exitX] = 3;

        // Reset player position (TOP-LEFT of the player rect, in SCREEN coords)
        // Center the player inside the start tile.
        const int startCellXpx = mazeOriginX + startX * cellSizePx;
        const int startCellYpx = mazeOriginY + startY * cellSizePx;
        const int px = startCellXpx + (cellSizePx - (int)player.sizePx) / 2;
        const int py = startCellYpx + (cellSizePx - (int)player.sizePx) / 2;
        player.x_fp = toFp(px);
        player.y_fp = toFp(py);
        player.vx_fp = 0;
        player.vy_fp = 0;

        // Movement tuning per tile size:
        // Keep 1x1 playable by being slower (precision), while 4x4 can be faster.
        const uint16_t baseSpeed = (cellSizePx == 4) ? 34 : (cellSizePx == 2) ? 28 : 22;
        // +1 px/s per 4 levels (integer-only)
        const uint16_t lvlBonus = (uint16_t)(level / 4);
        player.maxSpeedPxPerS = (uint16_t)min<uint16_t>(42, (uint16_t)(baseSpeed + lvlBonus));

        // Draw size: match tile size in small modes.
        player.sizePx = (uint8_t)((cellSizePx <= 2) ? cellSizePx : 2);

        // Rebuild pixel-accurate collision after any maze changes.
        buildSolidMaskFromMaze();
    }
    
    bool collidesRectAtFp(int32_t x_fp, int32_t y_fp) const {
        // Collision model: AABB (top-left) with integer pixel coverage:
        // [x, x+size-1] and [y, y+size-1] are solid-tested.
        const int x = (int)fpToIntFloor(x_fp);
        const int y = (int)fpToIntFloor(y_fp);
        const int maxX = x + (int)player.sizePx - 1;
        const int maxY = y + (int)player.sizePx - 1;

        if (x < 0 || y < 0 || maxX >= PANEL_RES_X || maxY >= PANEL_RES_Y) return true;

        for (int py = y; py <= maxY; py++) {
            for (int px = x; px <= maxX; px++) {
                if (solid[py][px]) return true;
            }
        }
        return false;
    }

    /**
     * Check if player reached exit
     */
    bool checkExit() {
        // Use the player's center pixel for exit detection (integer-only).
        const int px = fpToIntFloor(player.x_fp) + (int)player.sizePx / 2;
        const int py = fpToIntFloor(player.y_fp) + (int)player.sizePx / 2;

        const int localX = px - mazeOriginX;
        const int localY = py - mazeOriginY;
        if (localX < 0 || localY < 0) return false;
        const int cx = localX / cellSizePx;
        const int cy = localY / cellSizePx;
        if (!isInBounds(cx, cy)) return false;
        return (maze[cy][cx] == 3);
    }

    uint16_t computeSecondsLeft(uint32_t nowMs) {
        if (levelStartTimeMs == 0) return 60;
        const uint32_t elapsed = (uint32_t)(nowMs - levelStartTimeMs);
        if (elapsed >= LabyrinthGameConfig::LEVEL_TIME_MS) return 0;
        const uint32_t leftMs = LabyrinthGameConfig::LEVEL_TIME_MS - elapsed;
        // Round up so the player sees "60" at the start and gets credit for partial seconds.
        const uint16_t s = (uint16_t)((leftMs + 999UL) / 1000UL);
        return (s > 60) ? 60 : s;
    }

    static inline uint16_t scaleColor565(uint16_t c, uint8_t alpha) {
        // Scale an RGB565 color toward black using alpha in [0..255].
        // 0 -> black, 255 -> original color.
        const uint16_t r5 = (uint16_t)((c >> 11) & 0x1Fu);
        const uint16_t g6 = (uint16_t)((c >> 5) & 0x3Fu);
        const uint16_t b5 = (uint16_t)(c & 0x1Fu);

        const uint16_t r5s = (uint16_t)((r5 * (uint16_t)alpha + 127u) / 255u);
        const uint16_t g6s = (uint16_t)((g6 * (uint16_t)alpha + 127u) / 255u);
        const uint16_t b5s = (uint16_t)((b5 * (uint16_t)alpha + 127u) / 255u);
        return (uint16_t)((r5s << 11) | (g6s << 5) | b5s);
    }

    void beginFade(AnimMode mode, uint32_t nowMs, uint16_t durationMs) {
        animMode = mode;
        animStartMs = nowMs;
        animDurationMs = durationMs;
    }

    uint8_t currentFadeAlpha(uint32_t nowMs) const {
        if (animMode == ANIM_NONE) return 255;
        if (animDurationMs == 0) return 255;

        const uint32_t elapsed = (uint32_t)(nowMs - animStartMs);
        const uint32_t d = (uint32_t)animDurationMs;
        const uint32_t clamped = (elapsed >= d) ? d : elapsed;
        const uint8_t a = (uint8_t)((clamped * 255UL) / d); // 0..255
        return (animMode == ANIM_FADE_OUT) ? (uint8_t)(255u - a) : a;
    }

public:
    LabyrinthGame() 
        : gameOver(false), level(1), lastUpdate(0), levelCompleteTime(0) {
        generateMaze();
    }

    void start() override {
        gameOver = false;
        level = 1;
        score = 0;
        lastUpdate = millis();
        // Timer begins after the intro fade-in (so the player doesn't lose time during the reveal).
        levelStartTimeMs = 0;
        cachedSecondsLeft = 60;
        levelComplete = false;
        levelCompleteTime = 0;
        secondsLeftAtComplete = 0;
        levelPhase = PHASE_CLEAR_ANIM;
        animMode = ANIM_NONE;

        // Apply current global player color (chosen in the main menu).
        player.color = globalSettings.getPlayerColor();

        generateMaze();
        beginFade(ANIM_FADE_IN, (uint32_t)lastUpdate, LabyrinthGameConfig::LEVEL_FADEIN_ANIM_MS);
    }

    void reset() override {
        start();
    }

    void update(ControllerManager* input) override {
        if (gameOver) return;
        
        const uint32_t nowMs = (uint32_t)millis();

        // Intro fade-in blocks gameplay and freezes timer until complete.
        if (!levelComplete && animMode == ANIM_FADE_IN) {
            if ((uint32_t)(nowMs - animStartMs) >= (uint32_t)animDurationMs) {
                animMode = ANIM_NONE;
                levelStartTimeMs = nowMs; // start the 60s clock AFTER reveal
                cachedSecondsLeft = 60;
            }
            return;
        }

        // Level complete transition:
        // - brightness fade-out (labyrinth area only)
        // - then show "COMPLETED" briefly
        // - then next level + score award + brightness fade-in
        if (levelComplete) {
            const uint32_t elapsed = (uint32_t)(nowMs - (uint32_t)levelCompleteTime);
            const uint32_t clearMs = (uint32_t)LabyrinthGameConfig::LEVEL_CLEAR_ANIM_MS;
            const uint32_t textMs  = (uint32_t)LabyrinthGameConfig::LEVEL_COMPLETE_TEXT_MS;

            if (elapsed < clearMs) {
                levelPhase = PHASE_CLEAR_ANIM;
                if (animMode != ANIM_FADE_OUT) beginFade(ANIM_FADE_OUT, (uint32_t)levelCompleteTime, LabyrinthGameConfig::LEVEL_CLEAR_ANIM_MS);
                return;
            }
            if (elapsed < (clearMs + textMs)) {
                levelPhase = PHASE_TEXT;
                animMode = ANIM_NONE; // fully cleared at this point
                return;
            }

            // Award: seconds remaining + 10 points for completing the labyrinth.
            score += (uint32_t)secondsLeftAtComplete + 10UL;
            level++;
            levelStartTimeMs = 0; // will start after fade-in
            cachedSecondsLeft = 60;
            levelComplete = false;
            levelPhase = PHASE_CLEAR_ANIM;
            generateMaze();
            beginFade(ANIM_FADE_IN, nowMs, LabyrinthGameConfig::LEVEL_FADEIN_ANIM_MS);
            return;
        }
        
        // Throttle updates
        if (nowMs - (uint32_t)lastUpdate < (uint32_t)UPDATE_INTERVAL_MS) return;
        lastUpdate = nowMs;

        // Timer (1 minute per level)
        if (levelStartTimeMs != 0) {
            cachedSecondsLeft = computeSecondsLeft(nowMs);
            if (cachedSecondsLeft == 0) {
                gameOver = true;
                return;
            }
        }
        
        // Update player position (analog movement with collision against grid)
        ControllerPtr p1 = input->getController(0);
        if (p1 && p1->isConnected()) {
            int16_t rawX = InputDetail::axisX(p1, 0);
            int16_t rawY = InputDetail::axisY(p1, 0);
            rawX = applyDeadzoneRaw(rawX, STICK_DEADZONE_RAW);
            rawY = applyDeadzoneRaw(rawY, STICK_DEADZONE_RAW);

            // Fallback to dpad when stick is idle (nice for older controllers).
            if (rawX == 0 && rawY == 0) {
                const uint8_t d = p1->dpad();
                if (d & 0x08) rawX = -(AXIS_DIVISOR);
                else if (d & 0x04) rawX = (AXIS_DIVISOR);
                if (d & 0x01) rawY = -(AXIS_DIVISOR);
                else if (d & 0x02) rawY = (AXIS_DIVISOR);
            }

            // Maze movement feels best as 4-directional.
            // Allow analog sticks, but if the player is pushing diagonally, pick the dominant axis.
            // This prevents tiny off-axis drift that can block turning into intersections (especially in 2x2 mode).
            if (rawX != 0 && rawY != 0) {
                if (abs(rawX) >= abs(rawY)) rawY = 0;
                else rawX = 0;
            }

            const int8_t dirX = signNonZero(rawX);
            const int8_t dirY = signNonZero(rawY);
            const int32_t maxStep_fp = computeMaxStepPerTickFp(player.maxSpeedPxPerS);

            const int32_t targetVx_fp = (int32_t)dirX * maxStep_fp;
            const int32_t targetVy_fp = (int32_t)dirY * maxStep_fp;

            // Integer-only velocity smoothing.
            player.vx_fp = lerpInt32(player.vx_fp, targetVx_fp, VEL_SMOOTH_NUM, VEL_SMOOTH_DEN);
            player.vy_fp = lerpInt32(player.vy_fp, targetVy_fp, VEL_SMOOTH_NUM, VEL_SMOOTH_DEN);

            // If there's no input on an axis, apply a little friction so we settle quickly.
            if (targetVx_fp == 0) player.vx_fp = (int32_t)((int64_t)player.vx_fp * STOP_FRICTION_NUM / STOP_FRICTION_DEN);
            if (targetVy_fp == 0) player.vy_fp = (int32_t)((int64_t)player.vy_fp * STOP_FRICTION_NUM / STOP_FRICTION_DEN);
        }

        // Integrate fixed-point movement. We step in <= 1px chunks to avoid tunneling
        // and to keep collision response exact at pixel granularity.
        auto stepAxis = [&](bool xAxis) {
            int32_t& pos_fp = xAxis ? player.x_fp : player.y_fp;
            int32_t& vel_fp = xAxis ? player.vx_fp : player.vy_fp;
            const int32_t other_fp = xAxis ? player.y_fp : player.x_fp;

            int32_t remaining = vel_fp;
            while (remaining != 0) {
                // Move at most 1 pixel per sub-step in the direction of travel.
                const int32_t step = (fpAbs(remaining) > FP_ONE) ? (int32_t)(fpSign(remaining) * FP_ONE) : remaining;
                const int32_t next_fp = pos_fp + step;

                const bool hit = xAxis ? collidesRectAtFp(next_fp, other_fp) : collidesRectAtFp(other_fp, next_fp);
                if (hit) {
                    vel_fp = 0;
                    return;
                }
                pos_fp = next_fp;
                remaining -= step;
            }
        };

        stepAxis(true);  // X
        stepAxis(false); // Y
        
        // Check if reached exit
        if (checkExit()) {
            levelComplete = true;
            levelCompleteTime = nowMs;
            secondsLeftAtComplete = cachedSecondsLeft;
            levelPhase = PHASE_CLEAR_ANIM;
            beginFade(ANIM_FADE_OUT, nowMs, LabyrinthGameConfig::LEVEL_CLEAR_ANIM_MS);
        }
    }

    void draw(MatrixPanel_I2S_DMA* display) override {
        const uint32_t nowMs = (uint32_t)millis();

        if (gameOver) {
            display->fillScreen(COLOR_BLACK);
            char tag[4];
            UserProfiles::getPadTag(0, tag);
            GameOverLeaderboardView::draw(display, "GAME OVER", leaderboardId(), leaderboardScore(), tag);
            return;
        }

        // Level complete text phase (after the clear animation)
        if (levelComplete && levelPhase == PHASE_TEXT) {
            // Keep HUD visible; only clear the labyrinth area.
            // HUD
            display->fillScreen(COLOR_BLACK);
            SmallFont::drawStringF(display, 2, 6, COLOR_YELLOW, "S:%lu", (unsigned long)score);
            char tbuf[10];
            snprintf(tbuf, sizeof(tbuf), "T:%u", (unsigned int)cachedSecondsLeft);
            const int approxCharW = 4;
            const int tx = PANEL_RES_X - 2 - ((int)strlen(tbuf) * approxCharW);
            SmallFont::drawString(display, tx, 6, tbuf, COLOR_CYAN);
            for (int x = 0; x < PANEL_RES_X; x += 2) display->drawPixel(x, HUD_H-1, COLOR_BLUE);

            // Labyrinth area (no HUD fade)
            display->fillRect(mazeOriginX, mazeOriginY, mazeW * cellSizePx, mazeH * cellSizePx, COLOR_BLACK);
            SmallFont::drawString(display, mazeOriginX + 10, mazeOriginY + 20, "COMPLETED", COLOR_GREEN);
            SmallFont::drawStringF(display, mazeOriginX + 12, mazeOriginY + 30, COLOR_YELLOW, "+%u", (unsigned int)(secondsLeftAtComplete + 10));
            return;
        }
        
        display->fillScreen(COLOR_BLACK);
        
        // HUD
        SmallFont::drawStringF(display, 2, 6, COLOR_YELLOW, "S:%lu", (unsigned long)score);

        // Right-aligned timer (T:60 .. T:0)
        char tbuf[10];
        snprintf(tbuf, sizeof(tbuf), "T:%u", (unsigned int)cachedSecondsLeft);
        const int approxCharW = 4; // TomThumb is ~3px wide with spacing; 4 is a good estimate.
        const int tx = PANEL_RES_X - 2 - ((int)strlen(tbuf) * approxCharW);
        SmallFont::drawString(display, tx, 6, tbuf, COLOR_CYAN);

        // Divider under HUD
        for (int x = 0; x < PANEL_RES_X; x += 2) display->drawPixel(x, HUD_H-1, COLOR_BLUE);

        // Softer colors for walls/exit (below HUD)
        uint16_t wallColor = display->color565(80, 120, 200);   // soft blue
        uint16_t pathColor = display->color565(10, 20, 40);     // near black
        uint16_t exitColor = display->color565(120, 220, 120);  // soft green

        // Apply fade brightness ONLY to labyrinth content (not HUD).
        const uint8_t a = currentFadeAlpha(nowMs);
        if (a != 255) {
            wallColor = scaleColor565(wallColor, a);
            pathColor = scaleColor565(pathColor, a);
            exitColor = scaleColor565(exitColor, a);
        }

        // Draw maze
        for (int y = 0; y < mazeH; y++) {
            for (int x = 0; x < mazeW; x++) {
                const int screenX = mazeOriginX + x * cellSizePx;
                const int screenY = mazeOriginY + y * cellSizePx;
                
                if (maze[y][x] == 0) {
                    if (cellSizePx == 1) display->drawPixel(screenX, screenY, wallColor);
                    else display->fillRect(screenX, screenY, cellSizePx, cellSizePx, wallColor);
                } else if (maze[y][x] == 3) {
                    if (cellSizePx == 1) display->drawPixel(screenX, screenY, exitColor);
                    else display->fillRect(screenX, screenY, cellSizePx, cellSizePx, exitColor);
                } else {
                    if (cellSizePx == 1) display->drawPixel(screenX, screenY, pathColor);
                    else display->fillRect(screenX, screenY, cellSizePx, cellSizePx, pathColor);
                }
            }
        }
        
        // Draw player
        const int px = fpToIntFloor(player.x_fp);
        const int py = fpToIntFloor(player.y_fp);
        const uint16_t playerColor = (a == 255) ? player.color : scaleColor565(player.color, a);
        if (player.sizePx <= 1) display->drawPixel(px, py, playerColor);
        else display->fillRect(px, py, player.sizePx, player.sizePx, playerColor);
    }

    bool isGameOver() override {
        // IMPORTANT:
        // - We only signal "game over" to the engine when the timer runs out.
        // - Level completion is a transition and should NOT submit scores or end the run.
        return gameOver;
    }

    // ------------------------------
    // Leaderboard integration
    // ------------------------------
    bool leaderboardEnabled() const override { return true; }
    const char* leaderboardId() const override { return "labyrinth"; }
    const char* leaderboardName() const override { return "Labyrinth"; }
    uint32_t leaderboardScore() const override {
        // Score-based run:
        // - +10 for each completed labyrinth
        // - +remaining seconds on the clock
        return score;
    }
};

