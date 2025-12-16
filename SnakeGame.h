#pragma once
#include <Arduino.h>
#include <vector>
#include "GameBase.h"
#include "config.h"
#include "SmallFont.h"

// Game canvas configuration: reserve top space for HUD
static const int HUD_HEIGHT = 8;  // Space reserved at top for score/player info
static const int PIXEL_SIZE = 2;  // Render size for snake/body/food (2x2 pixels per logical cell)

// =========================================================
// Playfield layout (Snake)
// =========================================================
// To avoid edge-pixel artifacts on some HUB75 panels, we keep Snake's entire
// playfield (border + all sprites) *inset by 1 pixel* from the physical panel
// edges (left/right/bottom). We also keep a 1px gap below the HUD.
//
// Visually:
// - y: [0..HUD_HEIGHT-1]  -> HUD area
// - y: HUD_HEIGHT         -> 1px spacer line
// - y: HUD_HEIGHT+1..62   -> playfield border + content
// - y: 63                 -> unused (edge pixel)
static const int PLAYFIELD_BORDER_INSET_X = 1;   // leaves x=0 unused, border starts at x=1
static const int PLAYFIELD_BORDER_INSET_Y = 1;   // leaves a 1px gap below HUD
static const int PLAYFIELD_BORDER_INSET_BOTTOM = 1; // leaves y=63 unused

// Border rectangle (in pixels)
static const int PLAYFIELD_BORDER_X = PLAYFIELD_BORDER_INSET_X;
static const int PLAYFIELD_BORDER_Y = HUD_HEIGHT + PLAYFIELD_BORDER_INSET_Y;
static const int PLAYFIELD_BORDER_W = PANEL_RES_X - (PLAYFIELD_BORDER_INSET_X * 2);
static const int PLAYFIELD_BORDER_H = (PANEL_RES_Y - PLAYFIELD_BORDER_Y) - PLAYFIELD_BORDER_INSET_BOTTOM;

// Content area is inside the border (1px thickness)
static const int PLAYFIELD_CONTENT_X = PLAYFIELD_BORDER_X + 1;
static const int PLAYFIELD_CONTENT_Y = PLAYFIELD_BORDER_Y + 1;
static const int PLAYFIELD_CONTENT_W = PLAYFIELD_BORDER_W - 2;
static const int PLAYFIELD_CONTENT_H = PLAYFIELD_BORDER_H - 2;

// Logical game grid dimensions (in game cells, not pixels)
// NOTE: Must evenly divide by PIXEL_SIZE.
#define LOGICAL_WIDTH  (PLAYFIELD_CONTENT_W / PIXEL_SIZE)
#define LOGICAL_HEIGHT (PLAYFIELD_CONTENT_H / PIXEL_SIZE)

enum Direction { UP, DOWN, LEFT, RIGHT, NONE };

struct Point {
    int x;
    int y;
};

class Snake {
public:
    std::vector<Point> body;
    Direction dir;
    Direction nextDir;
    uint16_t color;
    bool alive;
    int score;
    int playerIndex;

    Snake(int idx, int x, int y, uint16_t c) {
        playerIndex = idx;
        color = c;
        reset(x, y);
    }

    void reset(int x, int y) {
        body.clear();
        body.push_back({x, y});
        body.push_back({x, y + 1});
        dir = UP;
        nextDir = UP;
        alive = true;
        score = 0;
    }

    void handleInput(ControllerPtr ctl) {
        if (!ctl || !ctl->isConnected()) return;

        uint8_t d = ctl->dpad();
        if ((d & 0x01) && dir != DOWN) nextDir = UP;
        if ((d & 0x02) && dir != UP) nextDir = DOWN;
        if ((d & 0x04) && dir != LEFT) nextDir = RIGHT;
        if ((d & 0x08) && dir != RIGHT) nextDir = LEFT;
    }

    void move(bool grow) {
        if (!alive || dir == NONE) return;

        dir = nextDir;
        Point head = body.front();

        if (dir == UP) head.y--;
        else if (dir == DOWN) head.y++;
        else if (dir == LEFT) head.x--;
        else if (dir == RIGHT) head.x++;

        // Edge detection with wrap-around (updated for new game area)
        if (head.x < 0) head.x = LOGICAL_WIDTH - 1;
        else if (head.x >= LOGICAL_WIDTH) head.x = 0;
        if (head.y < 0) head.y = LOGICAL_HEIGHT - 1;
        else if (head.y >= LOGICAL_HEIGHT) head.y = 0;

        body.insert(body.begin(), head);
        if (!grow) body.pop_back();
    }
};

class SnakeGame : public GameBase {
private:
    std::vector<Snake> snakes;
    std::vector<Point> foods;
    unsigned long lastMove;
    bool gameOver;

    // When the last snake dies, we flash the screen a few times before showing GAME OVER.
    bool flashing;
    bool flashOn;
    uint8_t flashTogglesRemaining;          // 6 toggles => 3 visible flashes
    unsigned long lastFlashToggleMs;
    static const unsigned long FLASH_TOGGLE_MS = 800;

    uint16_t playerColors[4] = {
        COLOR_GREEN, COLOR_CYAN, COLOR_ORANGE, COLOR_PURPLE
    };

    void spawnFood() {
        bool ok;
        Point f;
        do {
            ok = true;
            f.x = random(0, LOGICAL_WIDTH);
            f.y = random(0, LOGICAL_HEIGHT);

            for (auto& s : snakes) {
                for (auto& p : s.body) {
                    if (p.x == f.x && p.y == f.y) {
                        ok = false;
                        break;
                    }
                }
            }
            for (auto& existing : foods) {
                if (existing.x == f.x && existing.y == f.y) {
                    ok = false;
                    break;
                }
            }
        } while (!ok);
        foods.push_back(f);
    }

public:
    SnakeGame() {
        lastMove = 0;
        gameOver = false;
        flashing = false;
        flashOn = false;
        flashTogglesRemaining = 0;
        lastFlashToggleMs = 0;
    }

    /**
     * Snake updates at a fixed tick rate (SNAKE_SPEED_MS).
     * Rendering faster than that doesn't improve gameplay, but it *does*
     * increase display bandwidth and can surface HUB75 ghosting artifacts on
     * some panels (especially with lots of black background).
     */
    uint16_t preferredRenderFps() const override {
        if (SNAKE_SPEED_MS == 0) return GAME_RENDER_FPS;
        uint16_t fps = (uint16_t)(1000UL / (uint32_t)SNAKE_SPEED_MS);
        if (fps < 10) fps = 10; // keep UI responsive / flashing visible
        if (fps > GAME_RENDER_FPS) fps = GAME_RENDER_FPS;
        return fps;
    }

    void start() override {
        snakes.clear();
        gameOver = false;
        flashing = false;
        flashOn = false;
        flashTogglesRemaining = 0;
        lastMove = millis();
        foods.clear();

        // Create snakes first so food never spawns on top of a snake on round start.
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            if (globalControllerManager->getController(i)) {
                snakes.emplace_back(
                    i,
                    LOGICAL_WIDTH / 2 + i * 2,
                    LOGICAL_HEIGHT / 2,
                    playerColors[i]
                );
            }
        }

        // Spawn multiple foods after snakes exist (so spawnFood() can avoid them).
        for (int i = 0; i < 3; i++) spawnFood();
    }

    void reset() override {
        start();
    }

    void update(ControllerManager* input) override {
        if (gameOver) return;

        // Flash sequence after the last snake dies: freeze the last frame and blink.
        if (flashing) {
            unsigned long now = millis();
            if (now - lastFlashToggleMs >= FLASH_TOGGLE_MS) {
                lastFlashToggleMs = now;
                flashOn = !flashOn;
                if (flashTogglesRemaining > 0) flashTogglesRemaining--;
                if (flashTogglesRemaining == 0) {
                    flashing = false;
                    gameOver = true;
                }
            }
            return;
        }

        if (millis() - lastMove < SNAKE_SPEED_MS) return;
        lastMove = millis();

        // -------------------------------------------------------------
        // Multiplayer-correct movement:
        // - Compute all next-head positions first (simultaneous tick)
        // - Resolve food growth per-snake
        // - Then resolve collisions (self, other bodies, head-on, head-swap)
        // -------------------------------------------------------------
        const int n = (int)snakes.size();
        if (n <= 0) return;

        std::vector<Point> nextHead(n);
        std::vector<bool> willMove(n, false);
        std::vector<bool> willGrow(n, false);
        std::vector<int> foodHitIndex(n, -1);
        std::vector<bool> collision(n, false);

        // 1) Inputs + next heads
        for (int i = 0; i < n; i++) {
            Snake& s = snakes[i];
            if (!s.alive) continue;

            ControllerPtr ctl = input->getController(s.playerIndex);
            if (!ctl) {
                s.alive = false;
                continue;
            }

            s.handleInput(ctl);
            s.dir = s.nextDir;

            Point head = s.body.front();
            Point nh = head;
            if (s.dir == UP) nh.y--;
            else if (s.dir == DOWN) nh.y++;
            else if (s.dir == LEFT) nh.x--;
            else if (s.dir == RIGHT) nh.x++;

            // Wrap around edges (playfield only)
            if (nh.x < 0) nh.x = LOGICAL_WIDTH - 1;
            else if (nh.x >= LOGICAL_WIDTH) nh.x = 0;
            if (nh.y < 0) nh.y = LOGICAL_HEIGHT - 1;
            else if (nh.y >= LOGICAL_HEIGHT) nh.y = 0;

            nextHead[i] = nh;
            willMove[i] = true;

            // Determine if this move would eat food (resolved later)
            for (size_t fi = 0; fi < foods.size(); fi++) {
                if (foods[fi].x == nh.x && foods[fi].y == nh.y) {
                    willGrow[i] = true;
                    foodHitIndex[i] = (int)fi;
                    break;
                }
            }
        }

        // 2) Head-on collisions (same destination cell)
        for (int i = 0; i < n; i++) {
            if (!willMove[i]) continue;
            for (int j = i + 1; j < n; j++) {
                if (!willMove[j]) continue;
                if (nextHead[i].x == nextHead[j].x && nextHead[i].y == nextHead[j].y) {
                    collision[i] = true;
                    collision[j] = true;
                }
            }
        }

        // 3) Head-swap collisions (A goes to B head, B goes to A head)
        for (int i = 0; i < n; i++) {
            if (!willMove[i]) continue;
            for (int j = i + 1; j < n; j++) {
                if (!willMove[j]) continue;
                const Point aHead = snakes[i].body.front();
                const Point bHead = snakes[j].body.front();
                if (nextHead[i].x == bHead.x && nextHead[i].y == bHead.y &&
                    nextHead[j].x == aHead.x && nextHead[j].y == aHead.y) {
                    collision[i] = true;
                    collision[j] = true;
                }
            }
        }

        // 4) Body collisions (including self)
        // Allow moving into a tail cell IF that tail is moving away this tick (i.e., !willGrow for that snake).
        for (int i = 0; i < n; i++) {
            if (!willMove[i]) continue;
            const Point nh = nextHead[i];

            for (int j = 0; j < n; j++) {
                Snake& other = snakes[j];
                if (!other.alive) continue;

                const bool otherTailVacates = willMove[j] && !willGrow[j];
                const size_t otherLen = other.body.size();

                for (size_t k = 0; k < otherLen; k++) {
                    // If other tail vacates, skip its last segment for collision checks.
                    if (otherTailVacates && k == otherLen - 1) continue;

                    // For self, ignore index 0 (current head); we only care about hitting the body.
                    if (i == j && k == 0) continue;

                    if (other.body[k].x == nh.x && other.body[k].y == nh.y) {
                        collision[i] = true;
                        break;
                    }
                }
                if (collision[i]) break;
            }
        }

        // 5) Apply moves + resolve food (single food can only be eaten once per tick)
        // If multiple snakes target the same food cell, head-on collision above will kill them; still, avoid double erase.
        for (int i = 0; i < n; i++) {
            if (!willMove[i]) continue;

            Snake& s = snakes[i];
            if (!s.alive) continue;

            // Move the snake (we still place the head even if it collided,
            // so the frozen frame shows the collision position clearly).
            const Point nh = nextHead[i];
            s.body.insert(s.body.begin(), nh);
            if (!willGrow[i]) s.body.pop_back();

            if (collision[i]) {
                s.alive = false;
                continue;
            }

            // Food + scoring for survivors
            if (willGrow[i] && foodHitIndex[i] >= 0) {
                // Re-check that the food still exists at this index (it may have been erased already)
                const int fi = foodHitIndex[i];
                if (fi >= 0 && fi < (int)foods.size() && foods[fi].x == nh.x && foods[fi].y == nh.y) {
                    s.score += 10;
                    foods.erase(foods.begin() + fi);
                    spawnFood();
                }
            }
        }

        bool anyAlive = false;
        for (auto& s : snakes) if (s.alive) anyAlive = true;
        if (!anyAlive && !snakes.empty()) {
            // Start flash sequence (3 flashes) before showing GAME OVER.
            flashing = true;
            flashOn = false;
            flashTogglesRemaining = 6;
            lastFlashToggleMs = millis();
        }
    }

    void draw(MatrixPanel_I2S_DMA* display) override {
        // During flashing, alternate between a full white frame and the frozen last frame.
        if (flashing && flashOn) {
            display->fillScreen(COLOR_BLACK);
            return;
        }

        display->fillScreen(COLOR_BLACK);

        if (gameOver) {
            // Use small font for game over text
            SmallFont::drawString(display, 8, 28, "GAME OVER", COLOR_RED);
            return;
        }

        // HUD: scores and players (at the top, fully visible)
        // Position text with 1px margin to prevent overflow at top edge
        int hudY = 6;  // Moved down by 2px (1px overflow fix + 1px margin)
        int hudX = 2;
        for (size_t i = 0; i < snakes.size(); i++) {
            char buf[10];
            snprintf(buf, sizeof(buf), "P%u:%d", (unsigned)i + 1, snakes[i].score);
            SmallFont::drawString(display, hudX, hudY, buf, snakes[i].color);
            hudX += 16;
        }
        char pbuf[8];
        snprintf(pbuf, sizeof(pbuf), "%dP", (int)snakes.size());
        SmallFont::drawString(display, PANEL_RES_X - 14, hudY, pbuf, COLOR_YELLOW);

        // Playfield border (inset to avoid using edge pixels)
        display->drawRect(PLAYFIELD_BORDER_X, PLAYFIELD_BORDER_Y, PLAYFIELD_BORDER_W, PLAYFIELD_BORDER_H, COLOR_WHITE);

        // Helper to draw a small rect but avoid spilling into the last row/col.
        // This preserves game logic while keeping everything aligned to edges.
        auto fillRectClipped = [&](int x, int y, int w, int h, uint16_t c) {
            // Hard clip to the playfield content area only.
            const int minX = PLAYFIELD_CONTENT_X;
            const int minY = PLAYFIELD_CONTENT_Y;
            const int maxX = PLAYFIELD_CONTENT_X + PLAYFIELD_CONTENT_W - 1;
            const int maxY = PLAYFIELD_CONTENT_Y + PLAYFIELD_CONTENT_H - 1;

            if (x < minX || y < minY) return;
            if (x > maxX || y > maxY) return;
            if (x + w - 1 > maxX) w = (maxX - x + 1);
            if (y + h - 1 > maxY) h = (maxY - y + 1);
            if (w <= 0 || h <= 0) return;
            display->fillRect(x, y, w, h, c);
        };

        // Draw foods (offset by HUD height)
        for (auto& f : foods) {
            int px = PLAYFIELD_CONTENT_X + f.x * PIXEL_SIZE;
            int py = PLAYFIELD_CONTENT_Y + f.y * PIXEL_SIZE;
            fillRectClipped(px, py, PIXEL_SIZE, PIXEL_SIZE, COLOR_RED);
        }

        // Draw snakes (offset by HUD height)
        for (auto& s : snakes) {
            if (!s.alive) continue;
            for (auto& p : s.body) {
                int px = PLAYFIELD_CONTENT_X + p.x * PIXEL_SIZE;
                int py = PLAYFIELD_CONTENT_Y + p.y * PIXEL_SIZE;
                fillRectClipped(px, py, PIXEL_SIZE, PIXEL_SIZE, s.color);
            }
        }
    }

    bool isGameOver() override {
        return gameOver;
    }
};
