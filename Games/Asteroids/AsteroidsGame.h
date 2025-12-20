#pragma once
#include <Arduino.h>
#include <math.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../../engine/GameBase.h"
#include "../../engine/ControllerManager.h"
#include "../../engine/config.h"
#include "../../component/SmallFont.h"
#include "../../engine/Settings.h"
#include "../../engine/UserProfiles.h"
#include "../../component/GameOverLeaderboardView.h"
#include "AsteroidsGameConfig.h"

/**
 * AsteroidsGame - Classic Asteroids-inspired gameplay adapted for a 64x64 HUB75 panel.
 *
 * Controls (Player 1):
 * - Left Stick: Move (twin-stick)
 * - Right Stick: Aim (twin-stick)
 * - Right Trigger: Shoot (analog trigger preferred; falls back to R2 button if needed)
 * - A: Hyperspace (teleport) with cooldown
 *
 * Notes:
 * - We reserve a small HUD band at the top for score/lives.
 * - World wraps horizontally and vertically within the playfield area.
 * - Rendering uses simple vector primitives (pixels/lines/circles) for speed.
 */
class AsteroidsGame : public GameBase {
private:
    // ---------------------------------------------------------
    // Layout
    // ---------------------------------------------------------
    static constexpr int HUD_H = AsteroidsGameConfig::HUD_H;  // reserved for text HUD

    // ---------------------------------------------------------
    // Tuning
    // ---------------------------------------------------------
    static constexpr uint32_t UPDATE_INTERVAL_MS = AsteroidsGameConfig::UPDATE_INTERVAL_MS;  // ~60Hz logic
    static constexpr float MAX_SPEED = AsteroidsGameConfig::MAX_SPEED;
    static constexpr float MOVE_SMOOTH = AsteroidsGameConfig::MOVE_SMOOTH;   // 0..1 (higher = snappier)
    static constexpr float STICK_DEADZONE = AsteroidsGameConfig::STICK_DEADZONE; // 0..1

    static constexpr uint32_t SHOT_COOLDOWN_MS = AsteroidsGameConfig::SHOT_COOLDOWN_MS;
    static constexpr uint32_t BULLET_LIFE_MS = AsteroidsGameConfig::BULLET_LIFE_MS;
    static constexpr int MAX_BULLETS = AsteroidsGameConfig::MAX_BULLETS;
    static constexpr float BULLET_SPEED = AsteroidsGameConfig::BULLET_SPEED;

    static constexpr uint32_t RESPAWN_INVULN_MS = AsteroidsGameConfig::RESPAWN_INVULN_MS;
    static constexpr uint32_t HYPERSPACE_COOLDOWN_MS = AsteroidsGameConfig::HYPERSPACE_COOLDOWN_MS;
    static constexpr int16_t AXIS_DIVISOR = AsteroidsGameConfig::AXIS_DIVISOR;   // Bluepad32 commonly uses ~[-512..512]
    static constexpr uint16_t TRIGGER_THRESHOLD = AsteroidsGameConfig::TRIGGER_THRESHOLD; // analog trigger threshold

    static constexpr uint8_t MAX_ASTEROIDS = AsteroidsGameConfig::MAX_ASTEROIDS;

    // ---------------------------------------------------------
    // Entities
    // ---------------------------------------------------------
    struct Ship {
        float x = PANEL_RES_X / 2.0f;
        float y = (HUD_H + (PANEL_RES_Y - 1)) / 2.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        float ang = -1.5708f; // facing up (approx -pi/2)
        uint16_t color = COLOR_GREEN;
    };

    struct Bullet {
        float x;
        float y;
        float vx;
        float vy;
        uint32_t bornMs;
        bool active;
        uint16_t color;
    };

    struct Asteroid {
        float x;
        float y;
        float vx;
        float vy;
        uint8_t size;   // 2=large, 1=medium, 0=small
        uint8_t radius; // pixels
        bool alive;
        uint16_t color;
    };

    // ---------------------------------------------------------
    // State
    // ---------------------------------------------------------
    Ship ship;
    Bullet bullets[MAX_BULLETS];
    Asteroid asteroids[MAX_ASTEROIDS];

    bool gameOver = false;
    int score = 0;
    int lives = 3;
    int level = 1;

    uint32_t lastTickMs = 0;
    uint32_t lastShotMs = 0;
    uint32_t respawnAtMs = 0;
    uint32_t invulnUntilMs = 0;
    uint32_t lastHyperMs = 0;

    // ---------------------------------------------------------
    // Helpers (math / wrapping)
    // ---------------------------------------------------------
    // We don't currently use analog sticks elsewhere in this repo, and Bluepad32
    // may expose slightly different names depending on version/controller.
    // These helpers use SFINAE to pick whichever API exists at compile time.
    struct InputDetail {
        template <typename T>
        static auto axisX(T* c, int) -> decltype(c->axisX(), int16_t()) { return (int16_t)c->axisX(); }
        template <typename T>
        static int16_t axisX(T*, ...) { return 0; }

        template <typename T>
        static auto axisY(T* c, int) -> decltype(c->axisY(), int16_t()) { return (int16_t)c->axisY(); }
        template <typename T>
        static int16_t axisY(T*, ...) { return 0; }

        template <typename T>
        static auto axisRX(T* c, int) -> decltype(c->axisRX(), int16_t()) { return (int16_t)c->axisRX(); }
        template <typename T>
        static int16_t axisRX(T*, ...) { return 0; }

        template <typename T>
        static auto axisRY(T* c, int) -> decltype(c->axisRY(), int16_t()) { return (int16_t)c->axisRY(); }
        template <typename T>
        static int16_t axisRY(T*, ...) { return 0; }

        // Right trigger (preferred analog): Bluepad32 commonly maps this to throttle().
        template <typename T>
        static auto throttle(T* c, int) -> decltype(c->throttle(), uint16_t()) { return (uint16_t)c->throttle(); }
        template <typename T>
        static uint16_t throttle(T*, ...) { return 0; }

        // Fallback digital R2.
        template <typename T>
        static auto r2(T* c, int) -> decltype(c->r2(), bool()) { return (bool)c->r2(); }
        template <typename T>
        static bool r2(T*, ...) { return false; }
    };

    static inline float clampf(float v, float lo, float hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }
    static inline float randf(float lo, float hi) {
        const float t = (float)random(0, 10000) / 10000.0f;
        return lo + (hi - lo) * t;
    }

    static inline float deadzone01(float v, float dz) {
        const float a = fabsf(v);
        if (a <= dz) return 0.0f;
        // Re-scale so output is continuous from 0..1 outside deadzone.
        const float s = (a - dz) / (1.0f - dz);
        return (v < 0) ? -s : s;
    }

    static inline void normalizeStick(int16_t rawX, int16_t rawY, float& outX, float& outY) {
        // Normalize to [-1..1] with a conservative divisor and clamp.
        const float x = clampf((float)rawX / (float)AXIS_DIVISOR, -1.0f, 1.0f);
        const float y = clampf((float)rawY / (float)AXIS_DIVISOR, -1.0f, 1.0f);
        outX = deadzone01(x, STICK_DEADZONE);
        outY = deadzone01(y, STICK_DEADZONE);
    }

    static inline float dist2(float ax, float ay, float bx, float by) {
        const float dx = ax - bx;
        const float dy = ay - by;
        return dx * dx + dy * dy;
    }

    static inline void wrapX(float& x) {
        if (x < 0) x += PANEL_RES_X;
        else if (x >= PANEL_RES_X) x -= PANEL_RES_X;
    }

    static inline void wrapY(float& y) {
        const float top = (float)HUD_H;
        const float bottom = (float)(PANEL_RES_Y - 1);
        if (y < top) y = bottom - (top - y);
        else if (y > bottom) y = top + (y - bottom);
    }

    void resetShipToCenter(uint32_t now) {
        ship.x = PANEL_RES_X / 2.0f;
        ship.y = (HUD_H + (PANEL_RES_Y - 1)) / 2.0f;
        ship.vx = 0.0f;
        ship.vy = 0.0f;
        ship.ang = -1.5708f;
        invulnUntilMs = now + RESPAWN_INVULN_MS;
    }

    void spawnWave(uint32_t now) {
        (void)now;
        for (int i = 0; i < MAX_ASTEROIDS; i++) asteroids[i].alive = false;
        for (int i = 0; i < MAX_BULLETS; i++) bullets[i].active = false;

        // Smooth difficulty curve:
        // - Level 1 starts with exactly 1 large asteroid.
        // - Add one extra large asteroid every 2 levels, capped to keep gameplay readable on 64x64.
        // - Increase drift speed slowly to avoid "instant chaos".
        const int count = (int)min(5, 1 + ((level - 1) / 2));
        const float baseSpeed = min(0.80f, 0.22f + (level * 0.03f));

        for (int i = 0; i < count; i++) {
            // Spawn away from the ship to reduce immediate unavoidable collisions.
            float ax = randf(0.0f, (float)(PANEL_RES_X - 1));
            float ay = randf((float)HUD_H, (float)(PANEL_RES_Y - 1));

            // Ensure some distance from ship center.
            if (dist2(ax, ay, ship.x, ship.y) < 18.0f * 18.0f) {
                ax = fmodf(ax + 24.0f, (float)PANEL_RES_X);
                ay = clampf(ay + 14.0f, (float)HUD_H, (float)(PANEL_RES_Y - 1));
            }

            const float ang = randf(0.0f, 6.28318f);
            const float sp = baseSpeed * randf(0.85f, 1.15f);
            const float vx = cosf(ang) * sp;
            const float vy = sinf(ang) * sp;
            // Allocate an asteroid slot.
            for (int ai = 0; ai < MAX_ASTEROIDS; ai++) {
                if (asteroids[ai].alive) continue;
                Asteroid& a = asteroids[ai];
                a.x = ax;
                a.y = ay;
                a.vx = vx;
                a.vy = vy;
                a.size = 2;
                a.radius = 6;
                a.alive = true;
                a.color = COLOR_WHITE;
                break;
            }
        }
    }

    void splitAsteroid(int idx, uint32_t now) {
        (void)now;
        Asteroid& a = asteroids[idx];
        a.alive = false;

        // Scoring: smaller = more points.
        if (a.size == 2) score += 20;
        else if (a.size == 1) score += 50;
        else score += 100;

        if (a.size == 0) return;

        // Spawn 2 children with slight random velocity variation.
        const uint8_t childSize = (uint8_t)(a.size - 1);
        for (int i = 0; i < 2; i++) {
            const float ang = randf(0.0f, 6.28318f);
            // Keep splits "fair": children are a bit faster than parent, but not wildly so.
            const float sp = randf(0.35f, 0.65f);
            const float nvx = a.vx * 0.65f + cosf(ang) * sp;
            const float nvy = a.vy * 0.65f + sinf(ang) * sp;
            for (int ai = 0; ai < MAX_ASTEROIDS; ai++) {
                if (asteroids[ai].alive) continue;
                Asteroid& c = asteroids[ai];
                c.x = a.x;
                c.y = a.y;
                c.vx = nvx;
                c.vy = nvy;
                c.size = childSize;
                c.radius = (childSize == 2) ? 6 : (childSize == 1) ? 4 : 2;
                c.alive = true;
                c.color = COLOR_WHITE;
                break;
            }
        }
    }

    bool allAsteroidsCleared() const {
        for (int i = 0; i < MAX_ASTEROIDS; i++) if (asteroids[i].alive) return false;
        return true;
    }

    void doHyperspace(uint32_t now) {
        if ((uint32_t)(now - lastHyperMs) < HYPERSPACE_COOLDOWN_MS) return;
        lastHyperMs = now;

        // Teleport to a random spot; reset velocity for fairness.
        ship.x = randf(0.0f, (float)(PANEL_RES_X - 1));
        ship.y = randf((float)HUD_H, (float)(PANEL_RES_Y - 1));
        ship.vx = 0.0f;
        ship.vy = 0.0f;

        // Give brief invulnerability to prevent instant death on spawn overlap.
        invulnUntilMs = now + 350;
    }

    void fire(uint32_t now) {
        if ((uint32_t)(now - lastShotMs) < SHOT_COOLDOWN_MS) return;
        int slot = -1;
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (!bullets[i].active) { slot = i; break; }
        }
        if (slot < 0) return;

        const float fx = cosf(ship.ang);
        const float fy = sinf(ship.ang);

        // Spawn bullet slightly in front of the ship with inherited velocity.
        const float bx = ship.x + fx * 4.0f;
        const float by = ship.y + fy * 4.0f;
        const float bv = BULLET_SPEED;
        Bullet& b = bullets[slot];
        b.x = bx;
        b.y = by;
        b.vx = ship.vx + fx * bv;
        b.vy = ship.vy + fy * bv;
        b.bornMs = now;
        b.active = true;
        b.color = COLOR_CYAN;
        lastShotMs = now;
    }

public:
    AsteroidsGame() {}

    void start() override {
        gameOver = false;
        score = 0;
        lives = 3;
        level = 1;

        // Apply current global player color (chosen in the main menu).
        ship.color = globalSettings.getPlayerColor();

        const uint32_t now = millis();
        lastTickMs = now;
        lastShotMs = 0;
        lastHyperMs = 0;
        respawnAtMs = 0;
        invulnUntilMs = 0;

        resetShipToCenter(now);
        spawnWave(now);
    }

    void reset() override {
        start();
    }

    void update(ControllerManager* input) override {
        if (gameOver) return;

        const uint32_t now = millis();
        if ((uint32_t)(now - lastTickMs) < UPDATE_INTERVAL_MS) return;
        lastTickMs = now;

        // Handle delayed respawn.
        // Use signed delta so this remains safe across millis() wraparound.
        if (respawnAtMs != 0 && (int32_t)(now - respawnAtMs) >= 0) {
            respawnAtMs = 0;
            resetShipToCenter(now);
        }

        // 1) Input
        ControllerPtr p1 = input->getController(0);
        if (p1 && p1->isConnected()) {
            // Left stick: movement (twin-stick).
            float lx = 0.0f, ly = 0.0f;
            normalizeStick(InputDetail::axisX(p1, 0), InputDetail::axisY(p1, 0), lx, ly);

            // Right stick: aim direction (twin-stick).
            float rx = 0.0f, ry = 0.0f;
            normalizeStick(InputDetail::axisRX(p1, 0), InputDetail::axisRY(p1, 0), rx, ry);

            // If the right stick is moved, update ship angle.
            const float aimMag2 = rx * rx + ry * ry;
            if (aimMag2 > 0.001f) {
                // Screen coordinates: +Y is down; atan2() matches our sin/cos usage.
                ship.ang = atan2f(ry, rx);
            }

            // Hyperspace on A (keeps B reserved for "back to menu" by the engine).
            if (p1->a()) {
                doHyperspace(now);
            }

            // Right trigger: shoot (prefer analog throttle, otherwise digital R2).
            const uint16_t rt = InputDetail::throttle(p1, 0);
            const bool rtPressed = (rt >= TRIGGER_THRESHOLD) || InputDetail::r2(p1, 0);
            if (rtPressed) {
                fire(now);
            }

            // Apply movement as a smoothed target velocity so it feels responsive
            // but not twitchy (and remains fair on a low-res panel).
            const float targetVx = lx * MAX_SPEED;
            const float targetVy = ly * MAX_SPEED;
            ship.vx = ship.vx * (1.0f - MOVE_SMOOTH) + targetVx * MOVE_SMOOTH;
            ship.vy = ship.vy * (1.0f - MOVE_SMOOTH) + targetVy * MOVE_SMOOTH;
        }

        // 2) Integrate ship
        ship.x += ship.vx;
        ship.y += ship.vy;
        wrapX(ship.x);
        wrapY(ship.y);

        // 3) Bullets
        for (int bi = 0; bi < MAX_BULLETS; bi++) {
            Bullet& b = bullets[bi];
            if (!b.active) continue;
            b.x += b.vx;
            b.y += b.vy;
            wrapX(b.x);
            wrapY(b.y);
            if ((uint32_t)(now - b.bornMs) > BULLET_LIFE_MS) b.active = false;
        }

        // 4) Asteroids
        for (int ai = 0; ai < MAX_ASTEROIDS; ai++) {
            Asteroid& a = asteroids[ai];
            if (!a.alive) continue;
            a.x += a.vx;
            a.y += a.vy;
            wrapX(a.x);
            wrapY(a.y);
        }

        // 5) Bullet vs asteroid collisions
        for (int bi = 0; bi < MAX_BULLETS; bi++) {
            Bullet& b = bullets[bi];
            if (!b.active) continue;
            for (int ai = 0; ai < MAX_ASTEROIDS; ai++) {
                Asteroid& a = asteroids[ai];
                if (!a.alive) continue;
                const float r = (float)a.radius;
                if (dist2(b.x, b.y, a.x, a.y) <= (r * r)) {
                    splitAsteroid(ai, now);
                    b.active = false;
                    break;
                }
            }
        }

        // 6) Ship vs asteroid collisions (unless invulnerable)
        if ((int32_t)(invulnUntilMs - now) <= 0) {
            for (int ai = 0; ai < MAX_ASTEROIDS; ai++) {
                const Asteroid& a = asteroids[ai];
                if (!a.alive) continue;
                const float r = (float)a.radius;
                // Ship approximated as a small circle.
                if (dist2(ship.x, ship.y, a.x, a.y) <= (r + 2.5f) * (r + 2.5f)) {
                    lives--;
                    for (int bi = 0; bi < MAX_BULLETS; bi++) bullets[bi].active = false;

                    if (lives <= 0) {
                        gameOver = true;
                    } else {
                        // Short delay before respawn so impact is visible.
                        respawnAtMs = now + 350;
                        invulnUntilMs = now + RESPAWN_INVULN_MS;
                        ship.vx = ship.vy = 0.0f;
                    }
                    break;
                }
            }
        }

        // 7) Next wave
        if (allAsteroidsCleared()) {
            level++;
            resetShipToCenter(now);
            spawnWave(now);
        }
    }

    void draw(MatrixPanel_I2S_DMA* display) override {
        display->fillScreen(COLOR_BLACK);

        // HUD
        SmallFont::drawStringF(display, 2, 6, COLOR_YELLOW, "S:%d", score);
        SmallFont::drawStringF(display, 34, 6, COLOR_CYAN, "L:%d", lives);

        // Divider line under HUD
        for (int x = 0; x < PANEL_RES_X; x += 2) display->drawPixel(x, HUD_H, COLOR_BLUE);

        if (gameOver) {
            char tag[4];
            UserProfiles::getPadTag(0, tag);
            GameOverLeaderboardView::draw(display, "GAME OVER", leaderboardId(), leaderboardScore(), tag);
            return;
        }

        // Asteroids
        for (int ai = 0; ai < MAX_ASTEROIDS; ai++) {
            const Asteroid& a = asteroids[ai];
            if (!a.alive) continue;
            display->drawCircle((int)a.x, (int)a.y, (int)a.radius, a.color);
        }

        // Bullets
        for (int bi = 0; bi < MAX_BULLETS; bi++) {
            const Bullet& b = bullets[bi];
            if (!b.active) continue;
            display->drawPixel((int)b.x, (int)b.y, b.color);
        }

        // Ship (blinks while invulnerable)
        const uint32_t now = millis();
        const bool invuln = ((int32_t)(invulnUntilMs - now) > 0);
        const bool showShip = !invuln || ((now / 120) % 2 == 0);
        if (showShip) {
            const float fx = cosf(ship.ang);
            const float fy = sinf(ship.ang);
            const float lx = cosf(ship.ang + 2.55f);
            const float ly = sinf(ship.ang + 2.55f);
            const float rx = cosf(ship.ang - 2.55f);
            const float ry = sinf(ship.ang - 2.55f);

            const int x0 = (int)(ship.x + fx * 4.0f);
            const int y0 = (int)(ship.y + fy * 4.0f);
            const int x1 = (int)(ship.x + lx * 3.0f);
            const int y1 = (int)(ship.y + ly * 3.0f);
            const int x2 = (int)(ship.x + rx * 3.0f);
            const int y2 = (int)(ship.y + ry * 3.0f);

            display->drawLine(x0, y0, x1, y1, ship.color);
            display->drawLine(x1, y1, x2, y2, ship.color);
            display->drawLine(x2, y2, x0, y0, ship.color);
            display->drawPixel((int)ship.x, (int)ship.y, COLOR_WHITE);
        }
    }

    bool isGameOver() override {
        return gameOver;
    }

    // ------------------------------
    // Leaderboard integration
    // ------------------------------
    bool leaderboardEnabled() const override { return true; }
    const char* leaderboardId() const override { return "asteroids"; }
    const char* leaderboardName() const override { return "Asteroid"; }
    uint32_t leaderboardScore() const override { return (score > 0) ? (uint32_t)score : 0u; }
};


