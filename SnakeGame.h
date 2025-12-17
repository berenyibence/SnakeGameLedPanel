#pragma once
#include <Arduino.h>
#include <math.h>
#include <vector>
#include "GameBase.h"
#include "config.h"
#include "SmallFont.h"
#include "Settings.h"

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
    bool dying;
    uint32_t deathStartMs;
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
        dying = false;
        deathStartMs = 0;
        score = 0;
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

    static inline bool isOpposite(Direction a, Direction b) {
        return (a == UP && b == DOWN) || (a == DOWN && b == UP) ||
               (a == LEFT && b == RIGHT) || (a == RIGHT && b == LEFT);
    }

    void handleInput(ControllerPtr ctl) {
        if (!ctl || !ctl->isConnected()) return;

        // Prefer analog stick (dominant axis), fallback to D-pad.
        static constexpr float STICK_DEADZONE = 0.22f;
        static constexpr int16_t AXIS_DIVISOR = 512;

        const float ax = clampf((float)InputDetail::axisX(ctl, 0) / (float)AXIS_DIVISOR, -1.0f, 1.0f);
        const float ay = clampf((float)InputDetail::axisY(ctl, 0) / (float)AXIS_DIVISOR, -1.0f, 1.0f);
        const float sx = deadzone01(ax, STICK_DEADZONE);
        const float sy = deadzone01(ay, STICK_DEADZONE);

        Direction desired = NONE;
        if (sx != 0.0f || sy != 0.0f) {
            // Dominant axis to prevent diagonal jitter.
            if (fabsf(sx) >= fabsf(sy)) desired = (sx < 0) ? LEFT : RIGHT;
            else desired = (sy < 0) ? UP : DOWN;
        } else {
            const uint8_t d = ctl->dpad();
            if (d & 0x01) desired = UP;
            else if (d & 0x02) desired = DOWN;
            else if (d & 0x04) desired = RIGHT;
            else if (d & 0x08) desired = LEFT;
        }

        if (desired != NONE && !isOpposite(dir, desired)) {
            nextDir = desired;
        }
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

    // Round flow: countdown on start, per-snake death blink, game over when all are gone.
    enum Phase : uint8_t { PHASE_COUNTDOWN, PHASE_PLAYING, PHASE_GAME_OVER };
    Phase phase;
    uint32_t phaseStartMs;
    static constexpr uint16_t COUNTDOWN_MS = 3000;

    static constexpr uint16_t DEATH_BLINK_TOTAL_MS = 900;
    static constexpr uint16_t DEATH_BLINK_PERIOD_MS = 120;

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
        phase = PHASE_COUNTDOWN;
        phaseStartMs = 0;
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
        phase = PHASE_COUNTDOWN;
        phaseStartMs = millis();
        lastMove = phaseStartMs;
        foods.clear();

        // Apply current global player color for Player 1 (pad index 0).
        // This allows changing the color in the main menu and having it reflect here.
        playerColors[0] = globalSettings.getPlayerColor();

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
        const uint32_t now = millis();

        // Cleanup finished death animations (remove corpse)
        for (auto& s : snakes) {
            if (s.dying && (uint32_t)(now - s.deathStartMs) >= DEATH_BLINK_TOTAL_MS) {
                s.dying = false;
                s.body.clear();
            }
        }

        // Countdown before starting movement (still accept input so players can buffer a direction)
        if (phase == PHASE_COUNTDOWN) {
            for (auto& s : snakes) {
                if (!s.alive) continue;
                ControllerPtr ctl = input->getController(s.playerIndex);
                if (ctl) s.handleInput(ctl);
            }
            if ((uint32_t)(now - phaseStartMs) >= COUNTDOWN_MS) {
                phase = PHASE_PLAYING;
                lastMove = now;
            }
            return;
        }

        if (phase == PHASE_GAME_OVER) return;

        if ((uint32_t)(now - lastMove) < (uint32_t)SNAKE_SPEED_MS) return;
        lastMove = now;

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
                s.dying = true;
                s.deathStartMs = now;
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
                s.dying = true;
                s.deathStartMs = now;
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
        bool anyDying = false;
        for (auto& s : snakes) {
            if (s.alive) anyAlive = true;
            if (s.dying) anyDying = true;
        }
        if (!anyAlive && !anyDying && !snakes.empty()) {
            phase = PHASE_GAME_OVER;
            gameOver = true;
        }
    }

    void draw(MatrixPanel_I2S_DMA* display) override {
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

        // HUD divider
        for (int x = 0; x < PANEL_RES_X; x += 2) display->drawPixel(x, HUD_HEIGHT - 1, COLOR_BLUE);

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
            // Alive snakes draw always. Dead snakes blink for a short time, then disappear.
            bool drawSnake = s.alive;
            if (!drawSnake && s.dying) {
                const uint32_t now = millis();
                if ((uint32_t)(now - s.deathStartMs) < DEATH_BLINK_TOTAL_MS) {
                    drawSnake = (((now / DEATH_BLINK_PERIOD_MS) % 2) == 0);
                }
            }
            if (!drawSnake) continue;
            for (auto& p : s.body) {
                int px = PLAYFIELD_CONTENT_X + p.x * PIXEL_SIZE;
                int py = PLAYFIELD_CONTENT_Y + p.y * PIXEL_SIZE;
                fillRectClipped(px, py, PIXEL_SIZE, PIXEL_SIZE, s.color);
            }
        }

        // Countdown overlay (during round start)
        if (phase == PHASE_COUNTDOWN) {
            const uint32_t now = millis();
            const uint32_t elapsed = (uint32_t)(now - phaseStartMs);
            int secsLeft = 3 - (int)(elapsed / 1000UL);
            if (secsLeft < 1) secsLeft = 1;
            char c[2] = { (char)('0' + secsLeft), '\0' };
            SmallFont::drawString(display, 30, 30, c, COLOR_YELLOW);
        }
    }

    bool isGameOver() override {
        return gameOver;
    }

    // ------------------------------
    // Leaderboard integration
    // ------------------------------
    bool leaderboardEnabled() const override { return true; }
    const char* leaderboardId() const override { return "snake"; }
    const char* leaderboardName() const override { return "Snake"; }
    uint32_t leaderboardScore() const override {
        // Multiplayer: submit the best individual score of the round.
        uint32_t best = 0;
        for (const auto& s : snakes) {
            if ((uint32_t)s.score > best) best = (uint32_t)s.score;
        }
        return best;
    }
};
