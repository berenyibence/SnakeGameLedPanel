#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "GameBase.h"
#include "ControllerManager.h"
#include "config.h"
#include "SmallFont.h"
#include "Settings.h"

/**
 * LabyrinthGame - Maze navigation game
 * Player navigates through a maze to reach the exit
 */
class LabyrinthGame : public GameBase {
private:
    // ---------------------------------------------------------
    // Layout / Difficulty
    // ---------------------------------------------------------
    static constexpr int HUD_H = 8; // top HUD band (matches other games)

    // We support up to 1x1 tiles (63x55-ish cells after HUD), so allocate a safe max.
    static constexpr int MAX_MAZE_W = PANEL_RES_X;   // 64
    static constexpr int MAX_MAZE_H = PANEL_RES_Y;   // 64 (we'll use less due to HUD)
    static constexpr int MAX_CELLS = MAX_MAZE_W * MAX_MAZE_H; // 4096

    // Player structure (analog movement with robust grid collision)
    struct Player {
        float x;     // position in playfield pixels (0..mazeW*cellSizePx)
        float y;     // position in playfield pixels (0..mazeH*cellSizePx)
        float vx;    // velocity px/s
        float vy;    // velocity px/s
        float radiusPx;
        float maxSpeedPxPerS;
        uint16_t color;
        uint8_t sizePx;      // draw size (>=1)
        
        Player()
            : x(0.0f), y(0.0f),
              vx(0.0f), vy(0.0f),
              radiusPx(1.0f), maxSpeedPxPerS(28.0f),
              color(COLOR_GREEN), sizePx(2) {}
    };
    
    Player player;
    bool gameOver;
    bool gameWon;
    int level;
    unsigned long lastUpdate;
    unsigned long winTime;
    static const int UPDATE_INTERVAL_MS = 16;  // ~60 FPS (render is capped by engine)

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
    static constexpr float STICK_DEADZONE = 0.18f; // 0..1
    static constexpr float VEL_SMOOTH = 0.22f;     // 0..1 (higher = snappier)
    static constexpr float STOP_FRICTION = 0.85f;  // per tick when no input
    static constexpr int16_t AXIS_DIVISOR = 512;   // Bluepad32 commonly ~[-512..512]
    
    /**
     * Generate a simple maze using depth-first carve for smoother paths
     */
    static inline uint8_t tileSizeForLevel(int lvl) {
        if (lvl > 20) return 1;
        if (lvl > 10) return 2;
        return 4;
    }

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

    static inline float clampf(float v, float lo, float hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

    static inline float deadzone01(float v, float dz) {
        const float a = fabsf(v);
        if (a <= dz) return 0.0f;
        const float s = (a - dz) / (1.0f - dz);
        return (v < 0) ? -s : s;
    }

    static inline void normalizeStick(int16_t rawX, int16_t rawY, float& outX, float& outY) {
        const float x = clampf((float)rawX / (float)AXIS_DIVISOR, -1.0f, 1.0f);
        const float y = clampf((float)rawY / (float)AXIS_DIVISOR, -1.0f, 1.0f);
        outX = deadzone01(x, STICK_DEADZONE);
        outY = deadzone01(y, STICK_DEADZONE);
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

        // Reset player position (center of start cell, in playfield coords)
        player.x = (startX + 0.5f) * cellSizePx;
        player.y = (startY + 0.5f) * cellSizePx;
        player.vx = 0.0f;
        player.vy = 0.0f;

        // Movement tuning per tile size:
        // Keep 1x1 playable by being slower (precision), while 4x4 can be faster.
        const float baseSpeed = (cellSizePx == 4) ? 34.0f : (cellSizePx == 2) ? 28.0f : 22.0f;
        player.maxSpeedPxPerS = min(42.0f, baseSpeed + (float)level * 0.25f);

        // Collision radius: small enough for narrow corridors, large enough to feel solid.
        player.radiusPx = (cellSizePx == 1) ? 0.35f : (cellSizePx == 2) ? 0.60f : 0.90f;

        // Draw size: match tile size in small modes.
        player.sizePx = (uint8_t)((cellSizePx <= 2) ? cellSizePx : 2);
    }
    
    /**
     * Check if position is valid (not a wall)
     */
    bool isWalkableCell(int x, int y) const {
        if (!isInBounds(x, y)) return false;
        return maze[y][x] != 0;
    }
    
    bool collidesAt(float px, float py) const {
        // Collision model: axis-aligned square centered at (px, py) with size = player.sizePx.
        // This keeps visuals and physics perfectly aligned and avoids directional bias.
        const float half = (float)player.sizePx * 0.5f;

        // Treat bounds as [min, max) (exclusive max) to allow "touching" walls with no gap.
        static constexpr float EDGE_EPS = 0.0001f;
        const int minX = (int)floorf((px - half) / (float)cellSizePx);
        const int maxX = (int)floorf((px + half - EDGE_EPS) / (float)cellSizePx);
        const int minY = (int)floorf((py - half) / (float)cellSizePx);
        const int maxY = (int)floorf((py + half - EDGE_EPS) / (float)cellSizePx);

        for (int cy = minY; cy <= maxY; cy++) {
            for (int cx = minX; cx <= maxX; cx++) {
                if (!isInBounds(cx, cy)) return true; // treat out-of-bounds as wall
                if (maze[cy][cx] == 0) return true;
            }
        }
        return false;
    }

    /**
     * Resolve movement along X against wall cells (AABB vs grid).
     * This prevents clipping and avoids leaving a 1px gap on any side.
     */
    void resolveMoveX(float targetX) {
        if (targetX == player.x) return;

        const float half = (float)player.sizePx * 0.5f;
        static constexpr float EDGE_EPS = 0.0001f;

        const bool movingRight = (targetX > player.x);
        const float leadEdge = movingRight ? (targetX + half) : (targetX - half);
        const float leadEdgeAdj = leadEdge + (movingRight ? -EDGE_EPS : EDGE_EPS);
        const int leadCellX = (int)floorf(leadEdgeAdj / (float)cellSizePx);

        const int minY = (int)floorf((player.y - half) / (float)cellSizePx);
        const int maxY = (int)floorf((player.y + half - EDGE_EPS) / (float)cellSizePx);

        // If the leading column contains ANY wall tile in our vertical span, clamp to its boundary.
        for (int cy = minY; cy <= maxY; cy++) {
            if (!isInBounds(leadCellX, cy) || maze[cy][leadCellX] == 0) {
                if (movingRight) {
                    // Wall cell's left boundary is at leadCellX * cellSizePx.
                    player.x = (float)(leadCellX * cellSizePx) - half;
                } else {
                    // Wall cell's right boundary is at (leadCellX + 1) * cellSizePx.
                    player.x = (float)((leadCellX + 1) * cellSizePx) + half;
                }
                player.vx = 0.0f;
                return;
            }
        }

        // No collision: accept movement.
        player.x = targetX;
    }

    /**
     * Resolve movement along Y against wall cells (AABB vs grid).
     */
    void resolveMoveY(float targetY) {
        if (targetY == player.y) return;

        const float half = (float)player.sizePx * 0.5f;
        static constexpr float EDGE_EPS = 0.0001f;

        const bool movingDown = (targetY > player.y);
        const float leadEdge = movingDown ? (targetY + half) : (targetY - half);
        const float leadEdgeAdj = leadEdge + (movingDown ? -EDGE_EPS : EDGE_EPS);
        const int leadCellY = (int)floorf(leadEdgeAdj / (float)cellSizePx);

        const int minX = (int)floorf((player.x - half) / (float)cellSizePx);
        const int maxX = (int)floorf((player.x + half - EDGE_EPS) / (float)cellSizePx);

        for (int cx = minX; cx <= maxX; cx++) {
            if (!isInBounds(cx, leadCellY) || maze[leadCellY][cx] == 0) {
                if (movingDown) {
                    player.y = (float)(leadCellY * cellSizePx) - half;
                } else {
                    player.y = (float)((leadCellY + 1) * cellSizePx) + half;
                }
                player.vy = 0.0f;
                return;
            }
        }

        player.y = targetY;
    }

    /**
     * Check if player reached exit
     */
    bool checkExit() {
        const int cx = (int)(player.x / (float)cellSizePx);
        const int cy = (int)(player.y / (float)cellSizePx);
        return (cx == exitX && cy == exitY);
    }

public:
    LabyrinthGame() 
        : gameOver(false), gameWon(false), level(1), lastUpdate(0), winTime(0) {
        generateMaze();
    }

    void start() override {
        gameOver = false;
        gameWon = false;
        level = 1;
        lastUpdate = millis();
        winTime = 0;

        // Apply current global player color (chosen in the main menu).
        player.color = globalSettings.getPlayerColor();

        generateMaze();
    }

    void reset() override {
        start();
    }

    void update(ControllerManager* input) override {
        if (gameOver) return;
        
        // If won, wait 800ms then advance to next level with new maze
        if (gameWon) {
            if (millis() - winTime > 800) {
                gameWon = false;
                level++;
                generateMaze();
            }
            return;
        }
        
        // Throttle updates
        unsigned long now = millis();
        if (now - lastUpdate < UPDATE_INTERVAL_MS) return;
        lastUpdate = now;
        
        // Update player position (analog movement with collision against grid)
        ControllerPtr p1 = input->getController(0);
        if (p1 && p1->isConnected()) {
            float sx = 0.0f, sy = 0.0f;
            normalizeStick(InputDetail::axisX(p1, 0), InputDetail::axisY(p1, 0), sx, sy);

            // Fallback to dpad when stick is idle (nice for older controllers).
            if (sx == 0.0f && sy == 0.0f) {
                const uint8_t d = p1->dpad();
                if (d & 0x08) sx = -1.0f;
                else if (d & 0x04) sx = 1.0f;
                if (d & 0x01) sy = -1.0f;
                else if (d & 0x02) sy = 1.0f;
            }

            // Maze movement feels best as 4-directional.
            // Allow analog sticks, but if the player is pushing diagonally, pick the dominant axis.
            // This prevents tiny off-axis drift that can block turning into intersections (especially in 2x2 mode).
            if (sx != 0.0f && sy != 0.0f) {
                if (fabsf(sx) >= fabsf(sy)) sy = 0.0f;
                else sx = 0.0f;
            }

            // Target velocity (px/s) with smoothing.
            const float targetVx = sx * player.maxSpeedPxPerS;
            const float targetVy = sy * player.maxSpeedPxPerS;
            player.vx = player.vx * (1.0f - VEL_SMOOTH) + targetVx * VEL_SMOOTH;
            player.vy = player.vy * (1.0f - VEL_SMOOTH) + targetVy * VEL_SMOOTH;

            // If there's no input, apply a little friction so we settle quickly.
            if (targetVx == 0.0f) player.vx *= STOP_FRICTION;
            if (targetVy == 0.0f) player.vy *= STOP_FRICTION;
        }

        // Integrate with a fixed dt based on UPDATE_INTERVAL_MS.
        const float dt = (float)UPDATE_INTERVAL_MS / 1000.0f;
        float nx = player.x + player.vx * dt;
        float ny = player.y + player.vy * dt;

        // Axis-separated movement with deterministic collision resolution.
        // This prevents both clipping and "1px gap" issues in any direction.
        resolveMoveX(nx);
        resolveMoveY(ny);

        // Keep within playfield bounds (treat edges as walls), using the same AABB model.
        const float half = (float)player.sizePx * 0.5f;
        const float minPx = half;
        const float maxPx = (float)(mazeW * cellSizePx) - half;
        const float minPy = half;
        const float maxPy = (float)(mazeH * cellSizePx) - half;
        player.x = clampf(player.x, minPx, maxPx);
        player.y = clampf(player.y, minPy, maxPy);
        
        // Check if reached exit
        if (checkExit()) {
            gameWon = true;
            winTime = millis();
        }
    }

    void draw(MatrixPanel_I2S_DMA* display) override {
        display->fillScreen(COLOR_BLACK);
        
        if (gameWon) {
            SmallFont::drawString(display, 8, 28, "LEVEL COMPLETE!", COLOR_GREEN);
            char levelStr[16];
            snprintf(levelStr, sizeof(levelStr), "LEVEL: %d", level);
            SmallFont::drawString(display, 8, 38, levelStr, COLOR_WHITE);
            return;
        }
        
        if (gameOver) {
            SmallFont::drawString(display, 8, 28, "GAME OVER", COLOR_RED);
            return;
        }
        
        // HUD
        SmallFont::drawStringF(display, 2, 6, COLOR_YELLOW, "LVL:%d", level);
        SmallFont::drawStringF(display, 44, 6, COLOR_CYAN, "%dx%d", cellSizePx, cellSizePx);

        // Divider under HUD
        for (int x = 0; x < PANEL_RES_X; x += 2) display->drawPixel(x, HUD_H-1, COLOR_BLUE);

        // Softer colors for walls/exit (below HUD)
        uint16_t wallColor = display->color565(80, 120, 200);   // soft blue
        uint16_t pathColor = display->color565(10, 20, 40);    // near black
        uint16_t exitColor = display->color565(120, 220, 120); // soft green

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
        // Draw centered on the physics position (player.x/y is the center).
        const int px = mazeOriginX + (int)(player.x - (float)player.sizePx * 0.5f);
        const int py = mazeOriginY + (int)(player.y - (float)player.sizePx * 0.5f);
        if (cellSizePx == 1) {
            // 1x1 mode: draw single pixel at the center.
            display->drawPixel(mazeOriginX + (int)player.x, mazeOriginY + (int)player.y, player.color);
        } else {
            display->fillRect(px, py, player.sizePx, player.sizePx, player.color);
        }
    }

    bool isGameOver() override {
        return gameOver || gameWon;
    }

    // ------------------------------
    // Leaderboard integration
    // ------------------------------
    bool leaderboardEnabled() const override { return true; }
    const char* leaderboardId() const override { return "labyrinth"; }
    const char* leaderboardName() const override { return "Labyrinth"; }
    uint32_t leaderboardScore() const override {
        // This game is progression-based; we treat "level reached" as the score.
        // (Higher is better.)
        return (level > 0) ? (uint32_t)level : 0u;
    }
};

