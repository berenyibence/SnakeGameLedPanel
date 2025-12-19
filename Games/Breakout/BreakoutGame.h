#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../../GameBase.h"
#include "../../ControllerManager.h"
#include "../../config.h"
#include "../../SmallFont.h"
#include "../../Settings.h"
#include "../../UserProfiles.h"
#include "../../GameOverLeaderboardView.h"
#include "BreakoutGameConfig.h"

/**
 * BreakoutGame - Breakout/Arkanoid style game (modernized).
 *
 * Key features (aligned with README conventions + ShooterGame inspiration):
 * - **Multiplayer**: up to `MAX_GAMEPADS` stacked paddles, independent lives.
 * - **No pause on life loss**: if a player loses their last active ball, they lose a life and immediately respawn.
 * - **Continuous brick stream**: bricks spawn at the top and slowly scroll down; difficulty ramps with level.
 * - **Bricks with HP**: stronger bricks appear as level increases.
 * - **Powerups (falling boxes)**:
 *   - **Red**: paddle length tier (stacks 1..5)
 *   - **Blue**: one-hit shield below paddle (catches a missed ball once)
 *   - **Green**: spawns a new “shot” ball on the paddle (fast/straight on launch)
 *   - **Purple**: explode 5 random bricks (animated)
 *   - **Cyan**: duplicate a random active ball
 * - **FX**: small particle bursts when bricks break / explode.
 */
class BreakoutGame : public GameBase {
private:
    // ---------------------------------------------------------
    // Analog helpers (Bluepad32 API varies across versions/controllers)
    // ---------------------------------------------------------
    struct InputDetail {
        template <typename T>
        static auto axisX(T* c, int) -> decltype(c->axisX(), int16_t()) { return (int16_t)c->axisX(); }
        template <typename T>
        static int16_t axisX(T*, ...) { return 0; }
    };

    static inline float clampf(float v, float lo, float hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }
    static inline float deadzone01(float v, float dz) {
        const float a = fabsf(v);
        if (a <= dz) return 0.0f;
        const float s = (a - dz) / (1.0f - dz);
        return (v < 0) ? -s : s;
    }

    // ---------------------------------------------------------
    // Tuning (BreakoutGameConfig.h)
    // ---------------------------------------------------------
    static constexpr float STICK_DEADZONE = BreakoutGameConfig::STICK_DEADZONE;
    static constexpr int16_t AXIS_DIVISOR = BreakoutGameConfig::AXIS_DIVISOR;
    static constexpr uint16_t UPDATE_INTERVAL_MS = BreakoutGameConfig::UPDATE_INTERVAL_MS;

    static constexpr int HUD_H = BreakoutGameConfig::HUD_H;

    static constexpr int PADDLE_H = BreakoutGameConfig::PADDLE_H;
    static constexpr int PADDLE_STACK_SPACING_Y = BreakoutGameConfig::PADDLE_STACK_SPACING_Y;
    static constexpr int PADDLE_BASE_Y = BreakoutGameConfig::PADDLE_BASE_Y;

    static constexpr int BRICK_COLS = BreakoutGameConfig::BRICK_COLS;
    static constexpr int BRICK_WIDTH = BreakoutGameConfig::BRICK_WIDTH;
    static constexpr int BRICK_HEIGHT = BreakoutGameConfig::BRICK_HEIGHT;
    static constexpr int BRICK_SPACING = BreakoutGameConfig::BRICK_SPACING;

    static constexpr int BALL_SIZE_PX = BreakoutGameConfig::BALL_SIZE_PX;
    static inline float ballHalf() { return BreakoutGameConfig::BALL_HALF; }

    static constexpr int MAX_BALLS = BreakoutGameConfig::MAX_BALLS;
    static constexpr int MAX_BRICKS = BreakoutGameConfig::MAX_BRICKS;
    static constexpr int MAX_POWERUPS = BreakoutGameConfig::MAX_POWERUPS;
    static constexpr int MAX_PARTICLES = BreakoutGameConfig::MAX_PARTICLES;

    // Powerups (color-coded)
    enum PowerUpType : uint8_t { PU_RED = 0, PU_BLUE = 1, PU_GREEN = 2, PU_PURPLE = 3, PU_CYAN = 4 };

    // Round phases
    enum RoundPhase : uint8_t { PHASE_COUNTDOWN, PHASE_PLAYING, PHASE_GAME_OVER };
    RoundPhase phase = PHASE_COUNTDOWN;
    uint32_t phaseStartMs = 0;
    static constexpr uint16_t COUNTDOWN_MS = BreakoutGameConfig::COUNTDOWN_MS;

    struct Player {
        bool enabled = false;
        float x = 0.0f;
        int y = 0;
        int width = 10;      // default shorter; upgraded by red powerup
        float speed = 2.4f;
        uint16_t color = COLOR_CYAN;
        int lives = 3;
        uint8_t paddleTier = 0;       // red powerup tier 0..5
    };

    struct Ball {
        bool active = false;
        bool attached = false;  // follows paddle until launched
        bool shotStyle = false; // green powerup style
        uint8_t owner = 0;      // last paddle touched
        float x = 0.0f;
        float y = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        uint16_t color = COLOR_WHITE;
    };

    struct Brick {
        bool active = false;
        int x = 0;
        float y = 0.0f;
        uint8_t hp = 1;
        uint8_t maxHp = 1;
        uint16_t baseColor = COLOR_RED;
        bool exploding = false;      // purple animation
        uint32_t explodeStartMs = 0; // purple animation start
    };

    struct PowerUp {
        bool active = false;
        float x = 0.0f;
        float y = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        uint8_t type = 0;
        uint8_t tier = 0; // reserved for future tiers
    };

    struct Particle {
        bool active = false;
        float x = 0.0f;
        float y = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        uint16_t color = COLOR_WHITE;
        uint32_t endMs = 0;
    };

    Player players[MAX_GAMEPADS] = {};
    Ball balls[MAX_BALLS] = {};
    Brick bricks[MAX_BRICKS] = {};
    PowerUp powerups[MAX_POWERUPS] = {};
    Particle particles[MAX_PARTICLES] = {};

    bool gameOver = false;
    int score = 0;
    int level = 1;
    uint32_t bricksDestroyed = 0;

    // Blue powerup: global one-hit "floor shield"
    // - spans the full playfield width
    // - saves exactly one ball from falling (bounces it back), then disappears
    bool floorShieldArmed = false;

    // All-clear bonus lock:
    // When the player clears ALL currently active bricks, we:
    // - double the score
    // - spawn 6 new rows instantly
    // This lock prevents repeated triggering while the field is empty.
    bool allClearLock = false;

    uint32_t lastUpdateMs = 0;
    uint32_t lastScrollMs = 0;
    uint32_t lastRowSpawnMs = 0;

    // ---------------------------------------------------------
    // Small helpers
    // ---------------------------------------------------------
    static inline uint16_t dimColor(uint16_t c, uint8_t mul /*0..255*/) {
        uint8_t r = (uint8_t)((c >> 11) & 0x1F);
        uint8_t g = (uint8_t)((c >> 5) & 0x3F);
        uint8_t b = (uint8_t)(c & 0x1F);
        r = (uint8_t)((r * mul) / 255);
        g = (uint8_t)((g * mul) / 255);
        b = (uint8_t)((b * mul) / 255);
        return (uint16_t)((r << 11) | (g << 5) | b);
    }

    static inline uint16_t brightenColor(uint16_t c, uint8_t add /*0..63-ish*/) {
        int r = (int)((c >> 11) & 0x1F);
        int g = (int)((c >> 5) & 0x3F);
        int b = (int)(c & 0x1F);
        r = min(31, r + (int)min<uint8_t>(31, add / 2));
        g = min(63, g + (int)min<uint8_t>(63, add));
        b = min(31, b + (int)min<uint8_t>(31, add / 2));
        return (uint16_t)((r << 11) | (g << 5) | b);
    }

    static inline int bricksStartX() {
        const int totalW = BRICK_COLS * BRICK_WIDTH + (BRICK_COLS - 1) * BRICK_SPACING;
        return (PANEL_RES_X - totalW) / 2;
    }

    static inline int brickXForCol(int col) {
        return bricksStartX() + col * (BRICK_WIDTH + BRICK_SPACING);
    }

    static inline bool checkRectCollision(float bx, float by, int rx, int ry, int rw, int rh) {
        const float h = ballHalf();
        return (bx + h >= (float)rx && bx - h <= (float)(rx + rw) &&
                by + h >= (float)ry && by - h <= (float)(ry + rh));
    }

    // ---------------------------------------------------------
    // Difficulty
    // ---------------------------------------------------------
    void recomputeLevel() {
        level = 1 + (int)(bricksDestroyed / 24u);
        level = (int)constrain(level, 1, 30);
    }

    uint16_t brickScrollIntervalMs() const {
        // Early-game pacing: start ~10x slower at level 1, then ramp back to
        // normal speed by around level 10.
        const int base = max(55, 190 - level * 5);
        const int slowFactor = max(1, 11 - min(10, max(1, level))); // lvl1=10x ... lvl10+=1x
        // Additional global tuning: make descent 4x slower (requested).
        const uint32_t ms = (uint32_t)base * (uint32_t)slowFactor * 4u;
        return (uint16_t)min<uint32_t>(60000u, ms);
    }

    uint16_t brickRowSpawnIntervalMs() const {
        // Match scroll pacing: new rows should also start much slower, otherwise
        // the field still floods even if scroll is slower.
        const int base = max(260, 900 - level * 18);
        const int slowFactor = max(1, 11 - min(10, max(1, level))); // lvl1=10x ... lvl10+=1x
        // Keep row spawn in sync with descent so difficulty feels consistent.
        const uint32_t ms = (uint32_t)base * (uint32_t)slowFactor * 4u;
        return (uint16_t)min<uint32_t>(60000u, ms);
    }

    float baseBallSpeed() const {
        // Base ball speed scales with level, but the very start should be calmer.
        // NOTE: We also clamp the actual ball velocity after collisions to prevent
        // rare runaway spikes.
        float s = 1.00f + 0.018f * (float)min(18, max(0, level - 1));
        if (level <= 1) s *= 0.42f; // a bit slower at start (requested)
        return s;
    }

    float maxBallSpeed() const {
        // Safety clamp: prevents the ball from feeling "too fast" after paddle hits.
        // Keep some progression, but cap hard for 64×64 readability.
        const float s = 1.32f + 0.02f * (float)min(12, max(0, level - 1));
        return min(1.65f, s);
    }

    uint8_t brickHpForSpawn() const {
        uint8_t hp = 1;
        if (level >= 3) hp = 2;
        if (level >= 7) hp = 3;
        if (level >= 12) hp = 4;
        if (level >= 18) hp = 5;
        const int r = random(0, 100);
        if (level >= 6 && r < 20) hp = min<uint8_t>(6, (uint8_t)(hp + 1));
        return hp;
    }

    // ---------------------------------------------------------
    // Pools / state
    // ---------------------------------------------------------
    void clearPools() {
        for (int i = 0; i < MAX_BALLS; i++) balls[i].active = false;
        for (int i = 0; i < MAX_BRICKS; i++) bricks[i].active = false;
        for (int i = 0; i < MAX_POWERUPS; i++) powerups[i].active = false;
        for (int i = 0; i < MAX_PARTICLES; i++) particles[i].active = false;
    }

    int alivePlayerCount() const {
        int c = 0;
        for (int i = 0; i < MAX_GAMEPADS; i++) if (players[i].enabled && players[i].lives > 0) c++;
        return c;
    }

    int highestPaddleY() const {
        int y = PADDLE_BASE_Y;
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            if (!players[i].enabled || players[i].lives <= 0) continue;
            y = min(y, players[i].y);
        }
        return y;
    }

    uint16_t playerColorForIndex(int idx) const {
        if (idx == 0) return globalSettings.getPlayerColor();
        static const uint16_t fallback[4] = { COLOR_GREEN, COLOR_CYAN, COLOR_YELLOW, COLOR_MAGENTA };
        return fallback[idx & 3];
    }

    void setupPlayersFromControllers(int connectedCount) {
        const int n = (int)constrain(connectedCount, 1, MAX_GAMEPADS);
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            players[i] = Player();
            players[i].enabled = (i < n);
            players[i].color = playerColorForIndex(i);
            players[i].y = PADDLE_BASE_Y - i * PADDLE_STACK_SPACING_Y;
            players[i].x = PANEL_RES_X / 2.0f - (float)players[i].width / 2.0f;
        }
    }

    // ---------------------------------------------------------
    // Balls
    // ---------------------------------------------------------
    int spawnHeldBall(uint8_t owner, bool shotStyle) {
        for (int i = 0; i < MAX_BALLS; i++) {
            if (balls[i].active) continue;
            balls[i].active = true;
            balls[i].attached = true;
            balls[i].shotStyle = shotStyle;
            balls[i].owner = owner;
            balls[i].vx = 0.0f;
            balls[i].vy = 0.0f;
            // Balls are always white; colors are reserved for powerups.
            balls[i].color = COLOR_WHITE;
            // IMPORTANT: initialize position immediately so the first draw frame
            // doesn't render the ball at (0,0) before the first update tick.
            if (owner < MAX_GAMEPADS) {
                const Player& p = players[owner];
                balls[i].x = p.x + (float)p.width * 0.5f;
                balls[i].y = (float)p.y - 2.0f;
            } else {
                balls[i].x = (float)(PANEL_RES_X / 2);
                balls[i].y = (float)(PADDLE_BASE_Y - 2);
            }
            return i;
        }
        return -1;
    }

    void updateAttachedBalls() {
        for (int i = 0; i < MAX_BALLS; i++) {
            Ball& b = balls[i];
            if (!b.active || !b.attached) continue;
            if (b.owner >= MAX_GAMEPADS) b.owner = 0;
            const Player& p = players[b.owner];
            b.x = p.x + (float)p.width * 0.5f;
            b.y = (float)p.y - 2.0f;
        }
    }

    void launchOneAttachedBall(uint8_t owner, bool preferStraightShot) {
        const float sp = baseBallSpeed();
        for (int i = 0; i < MAX_BALLS; i++) {
            Ball& b = balls[i];
            if (!b.active || !b.attached || b.owner != owner) continue;
            b.attached = false;
            const bool shot = preferStraightShot || b.shotStyle;
            const float s = shot ? (sp * 1.35f) : sp;
            b.vy = -s;
            b.vx = shot ? 0.0f : ((random(0, 2) == 0) ? -s : s);
            b.shotStyle = shot;
            b.color = COLOR_WHITE;
            return;
        }
    }

    void clampBallSpeed(Ball& b) const {
        const float v = sqrtf(b.vx * b.vx + b.vy * b.vy);
        if (v <= 0.0001f) return;
        const float vmax = maxBallSpeed();
        if (v > vmax) {
            const float k = vmax / v;
            b.vx *= k;
            b.vy *= k;
        }
    }

    void bounceBallOffPaddle(Ball& ball, const Player& p) {
        // Keep speed stable and avoid "too horizontal" rebounds which feel like
        // the paddle injected excessive momentum.
        const float hitPos = constrain(((ball.x - p.x) / (float)p.width) * 2.0f - 1.0f, -1.0f, 1.0f);
        const float sp = baseBallSpeed();

        // Direction vector, then normalized to speed:
        // - vx scales with hit position
        // - vy stays strongly negative so the ball doesn't become a fast wall-hugger
        const float vxN = hitPos;
        const float vyN = -(1.15f - 0.35f * fabsf(hitPos));
        const float vn = sqrtf(vxN * vxN + vyN * vyN);
        if (vn > 0.0001f) {
            ball.vx = (vxN / vn) * sp;
            ball.vy = (vyN / vn) * sp;
        } else {
            ball.vx = 0.0f;
            ball.vy = -sp;
        }
        clampBallSpeed(ball);
    }

    // ---------------------------------------------------------
    // Bricks
    // ---------------------------------------------------------
    int allocBrickSlot() {
        for (int i = 0; i < MAX_BRICKS; i++) if (!bricks[i].active) return i;
        return -1;
    }

    uint16_t baseBrickColorForColumn(int col) const {
        static const uint16_t palette[] = { COLOR_RED, COLOR_ORANGE, COLOR_YELLOW, COLOR_GREEN, COLOR_CYAN, COLOR_BLUE, COLOR_PURPLE, COLOR_MAGENTA };
        return palette[col % (sizeof(palette) / sizeof(palette[0]))];
    }

    void spawnBrickRow(float y) {
        for (int col = 0; col < BRICK_COLS; col++) {
            const int slot = allocBrickSlot();
            if (slot < 0) break;
            Brick& br = bricks[slot];
            br.active = true;
            br.exploding = false;
            br.explodeStartMs = 0;
            br.x = brickXForCol(col);
            br.y = y;
            br.baseColor = baseBrickColorForColumn(col);
            br.maxHp = brickHpForSpawn();
            br.hp = br.maxHp;
        }
    }

    void spawnInitialBricks() {
        const float startY = (float)(HUD_H + 2);
        for (int r = 0; r < 6; r++) spawnBrickRow(startY + (float)r * (float)(BRICK_HEIGHT + BRICK_SPACING));
    }

    int activeBrickCount() const {
        int c = 0;
        for (int i = 0; i < MAX_BRICKS; i++) if (bricks[i].active) c++;
        return c;
    }

    bool handleAllClearBonus(uint32_t now) {
        const int alive = activeBrickCount();
        if (alive > 0) {
            allClearLock = false;
            return false;
        }
        if (allClearLock) return false;
        allClearLock = true;

        // Reward: double score.
        if (score > 0) score *= 2;

        // Instant refill: 6 new rows.
        const float startY = (float)(HUD_H + 2);
        for (int r = 0; r < 6; r++) {
            spawnBrickRow(startY + (float)r * (float)(BRICK_HEIGHT + BRICK_SPACING));
        }

        // Give the player a moment before the stream immediately advances/spawns.
        lastScrollMs = now;
        lastRowSpawnMs = now;

        // Small celebration burst (cheap, visible).
        spawnParticles((float)(PANEL_RES_X / 2), (float)(HUD_H + 6), COLOR_YELLOW, 10, now);

        return true;
    }

    void moveBricksDownOnePixel() {
        for (int i = 0; i < MAX_BRICKS; i++) if (bricks[i].active) bricks[i].y += 1.0f;
    }

    // ---------------------------------------------------------
    // Particles / FX
    // ---------------------------------------------------------
    void spawnParticles(float x, float y, uint16_t color, uint8_t count, uint32_t now) {
        // Density tuning: keep FX quality but reduce total particle count (cheaper on ESP32).
        const uint8_t tunedCount = max<uint8_t>(1, (uint8_t)(count / 2));
        for (uint8_t n = 0; n < tunedCount; n++) {
            int slot = -1;
            for (int i = 0; i < MAX_PARTICLES; i++) if (!particles[i].active) { slot = i; break; }
            if (slot < 0) return;
            Particle& p = particles[slot];
            p.active = true;
            p.x = x;
            p.y = y;
            p.vx = ((float)random(-70, 71) / 100.0f) * 0.9f;
            p.vy = ((float)random(-70, 71) / 100.0f) * 0.9f;
            p.color = color;
            p.endMs = now + (uint32_t)random(220, 520);
        }
    }

    void updateParticles(uint32_t now) {
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (!particles[i].active) continue;
            if ((int32_t)(particles[i].endMs - now) <= 0) { particles[i].active = false; continue; }
            particles[i].x += particles[i].vx;
            particles[i].y += particles[i].vy;
            particles[i].vx *= 0.97f;
            particles[i].vy *= 0.97f;
            particles[i].vy += 0.015f;
        }
    }

    // ---------------------------------------------------------
    // Powerups
    // ---------------------------------------------------------
    uint16_t powerupColor(uint8_t type) const {
        switch ((PowerUpType)type) {
            case PU_RED: return COLOR_RED;
            case PU_BLUE: return COLOR_BLUE;
            case PU_GREEN: return COLOR_GREEN;
            case PU_PURPLE: return COLOR_PURPLE;
            case PU_CYAN: return COLOR_CYAN;
            default: return COLOR_WHITE;
        }
    }

    // Powerup motion tuning (floatier / slower overall)
    static constexpr float POWERUP_GRAVITY = 0.010f;
    static constexpr float POWERUP_DRAG = 0.984f;
    static constexpr float POWERUP_BOUNCE = 0.78f; // energy kept on wall bounce (lower => slower after bounces)
    static constexpr int POWERUP_SIZE_PX = 2;       // powerups render as 2x2

    void maybeDropPowerup(float x, float y, float kickVx, float kickVy) {
        const int baseChance = 18;
        const int chance = min(28, baseChance + level / 3);
        if (random(0, 100) >= chance) return;

        int slot = -1;
        for (int i = 0; i < MAX_POWERUPS; i++) if (!powerups[i].active) { slot = i; break; }
        if (slot < 0) return;

        const int r = random(0, 100);
        uint8_t t = PU_RED;
        if (r < 32) t = PU_RED;
        else if (r < 62) t = PU_BLUE;
        else if (r < 78) t = PU_GREEN;
        else if (r < 92) t = PU_CYAN;
        else t = PU_PURPLE;

        powerups[slot].active = true;
        powerups[slot].type = t;
        powerups[slot].x = x;
        powerups[slot].y = y;
        // Shoot out from the brick explosion with strong sideways kick (harder to catch),
        // but slower gravity so the player has time to chase.
        powerups[slot].vx = kickVx + ((float)random(-80, 81) / 100.0f) * 0.28f;
        powerups[slot].vy = kickVy + ((float)random(-20, 41) / 100.0f) * 0.08f;
        powerups[slot].tier = 0;
    }

    void applyPowerupToPlayer(uint8_t playerIdx, uint8_t type, uint32_t now) {
        (void)now;
        if (playerIdx >= MAX_GAMEPADS) return;
        Player& p = players[playerIdx];
        if (!p.enabled || p.lives <= 0) return;

        if (type == PU_RED) {
            p.paddleTier = min<uint8_t>(5, (uint8_t)(p.paddleTier + 1));
            p.width = 10 + (int)p.paddleTier * 2;
            p.x = constrain(p.x, 0.0f, (float)(PANEL_RES_X - p.width));
        } else if (type == PU_BLUE) {
            floorShieldArmed = true;
        } else if (type == PU_GREEN) {
            // Green: immediately spawn a "shot" ball attached to the paddle.
            // It will launch on A like normal, but prefers a straight/fast shot style.
            (void)spawnHeldBall(playerIdx, true);
        }
    }

    void triggerPurpleExplosion(uint32_t now) {
        int marked = 0;
        for (int tries = 0; tries < 60 && marked < 5; tries++) {
            const int idx = random(0, MAX_BRICKS);
            Brick& b = bricks[idx];
            if (!b.active || b.exploding) continue;
            b.exploding = true;
            b.explodeStartMs = now;
            marked++;
        }
    }

    void duplicateRandomBall(uint32_t now) {
        (void)now;
        int src = -1;
        for (int tries = 0; tries < 20; tries++) {
            const int i = random(0, MAX_BALLS);
            if (!balls[i].active || balls[i].attached) continue;
            src = i;
            break;
        }
        if (src < 0) return;

        int dst = -1;
        for (int i = 0; i < MAX_BALLS; i++) if (!balls[i].active) { dst = i; break; }
        if (dst < 0) return;

        balls[dst] = balls[src];
        balls[dst].active = true;
        balls[dst].attached = false;
        balls[dst].vx += ((float)random(-30, 31) / 100.0f) * 0.6f;
        balls[dst].vy *= 0.98f;
        balls[dst].color = COLOR_WHITE;
    }

    void updatePowerups(uint32_t now) {
        for (int i = 0; i < MAX_POWERUPS; i++) {
            PowerUp& pu = powerups[i];
            if (!pu.active) continue;

            pu.x += pu.vx;
            pu.y += pu.vy;
            pu.vy += POWERUP_GRAVITY;
            pu.vx *= POWERUP_DRAG;
            pu.vy *= POWERUP_DRAG;

            // Keep powerups inside screen bounds by bouncing off walls.
            // (Powerups are 2x2 pixels.)
            const float maxX = (float)(PANEL_RES_X - POWERUP_SIZE_PX);
            if (pu.x < 0.0f) {
                pu.x = 0.0f;
                pu.vx = fabsf(pu.vx) * POWERUP_BOUNCE;
            } else if (pu.x > maxX) {
                pu.x = maxX;
                pu.vx = -fabsf(pu.vx) * POWERUP_BOUNCE;
            }

            // If launched upward into the top band, bounce down.
            const float minY = (float)(HUD_H + 1);
            if (pu.y < minY) {
                pu.y = minY;
                pu.vy = fabsf(pu.vy) * POWERUP_BOUNCE;
            }

            // Extra safety clamp: avoid ultra-fast outliers from random kicks.
            pu.vx = clampf(pu.vx, -0.85f, 0.85f);
            pu.vy = clampf(pu.vy, -0.85f, 0.85f);

            // Catch by any paddle
            for (int pi = 0; pi < MAX_GAMEPADS; pi++) {
                if (!players[pi].enabled || players[pi].lives <= 0) continue;
                const int px = (int)players[pi].x;
                const int py = players[pi].y;
                const int pw = players[pi].width;

                if ((int)pu.y >= py - 1 && (int)pu.y <= py + 1 &&
                    (int)pu.x >= px - 1 && (int)pu.x <= px + pw) {
                    if (pu.type == PU_PURPLE) triggerPurpleExplosion(now);
                    else if (pu.type == PU_CYAN) duplicateRandomBall(now);
                    else applyPowerupToPlayer((uint8_t)pi, pu.type, now);
                    pu.active = false;
                    break;
                }
            }

            if (!pu.active) continue;
            if (pu.y > (float)(PANEL_RES_Y + 6)) pu.active = false;
        }
    }

    // ---------------------------------------------------------
    // Brick hit / destroy
    // ---------------------------------------------------------
    void destroyBrick(Brick& b, uint32_t now, uint8_t owner) {
        const float cx = (float)b.x + (float)BRICK_WIDTH * 0.5f;
        const float cy = b.y + (float)BRICK_HEIGHT * 0.5f;
        score += 8 + (int)b.maxHp * 4;
        bricksDestroyed++;
        recomputeLevel();
        spawnParticles(cx, cy, b.baseColor, (uint8_t)random(4, 8), now);
        // Strong sideways kick to make powerups harder to catch.
        const float kickVx = ((float)random(-100, 101) / 100.0f) * 0.70f;  // -0.70..0.70 (a bit lighter/slower)
        const float kickVy = -(((float)random(20, 80) / 100.0f) * 0.10f);   // -0.020..-0.080
        maybeDropPowerup(cx - 1.0f, cy - 1.0f, kickVx, kickVy);
        (void)owner;
        b.active = false;
        b.exploding = false;
        b.explodeStartMs = 0;
    }

    // ---------------------------------------------------------
    // Life handling
    // ---------------------------------------------------------
    void loseLife(uint8_t playerIdx, uint32_t now) {
        (void)now;
        if (playerIdx >= MAX_GAMEPADS) return;
        Player& p = players[playerIdx];
        if (!p.enabled || p.lives <= 0) return;
        p.lives--;
        if (p.lives < 0) p.lives = 0;
        if (p.lives > 0) (void)spawnHeldBall(playerIdx, false);
    }

    // ---------------------------------------------------------
    // Updates
    // ---------------------------------------------------------
    void updatePlayers(ControllerManager* input) {
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            Player& p = players[i];
            if (!p.enabled || p.lives <= 0) continue;
            ControllerPtr ctl = input ? input->getController(i) : nullptr;
            if (!(ctl && ctl->isConnected())) continue;

            const float raw = clampf((float)InputDetail::axisX(ctl, 0) / (float)AXIS_DIVISOR, -1.0f, 1.0f);
            float sx = deadzone01(raw, STICK_DEADZONE);
            if (sx == 0.0f) {
                const uint8_t dpad = ctl->dpad();
                if (dpad & 0x08) sx = -1.0f;
                else if (dpad & 0x04) sx = 1.0f;
            }
            p.x += sx * p.speed;
            p.x = constrain(p.x, 0.0f, (float)(PANEL_RES_X - p.width));
        }
    }

    void handleLaunchInputs(ControllerManager* input) {
        // During countdown: players can move but cannot launch.
        if (phase != PHASE_PLAYING) return;

        for (int i = 0; i < MAX_GAMEPADS; i++) {
            const Player& p = players[i];
            if (!p.enabled || p.lives <= 0) continue;
            ControllerPtr ctl = input ? input->getController(i) : nullptr;
            if (!(ctl && ctl->isConnected())) continue;
            if (ctl->a()) {
                // Release exactly one attached ball for this player.
                launchOneAttachedBall((uint8_t)i, false);
            }
        }
    }

    void updateBallsAndCollisions(uint32_t now) {
        const float h = ballHalf();
        for (int bi = 0; bi < MAX_BALLS; bi++) {
            Ball& ball = balls[bi];
            if (!ball.active || ball.attached) continue;

            ball.x += ball.vx;
            ball.y += ball.vy;

            // Walls
            if (ball.x - h <= 0.0f || ball.x + h >= (float)PANEL_RES_X) {
                ball.vx = -ball.vx;
                ball.x = constrain(ball.x, h, (float)PANEL_RES_X - h);
                clampBallSpeed(ball);
            }
            if (ball.y - h <= (float)(HUD_H + 1)) {
                ball.vy = -ball.vy;
                ball.y = (float)(HUD_H + 1) + h;
                clampBallSpeed(ball);
            }

            // Global floor shield (one-hit, full width).
            // Only interacts with descending balls near the bottom.
            if (floorShieldArmed && ball.vy > 0.0f) {
                const int sy = PANEL_RES_Y - 1;
                if (checkRectCollision(ball.x, ball.y, 0, sy, PANEL_RES_X, 1)) {
                    ball.vy = -fabsf(ball.vy);
                    ball.y = (float)sy - h;
                    floorShieldArmed = false;
                    spawnParticles(ball.x, ball.y, COLOR_BLUE, 10, now);
                    clampBallSpeed(ball);
                }
            }

            // Paddles
            for (int pi = MAX_GAMEPADS - 1; pi >= 0; pi--) {
                Player& p = players[pi];
                if (!p.enabled || p.lives <= 0) continue;

                const int px = (int)p.x;
                const int py = p.y;
                const int pw = p.width;

                if (checkRectCollision(ball.x, ball.y, px, py, pw, PADDLE_H)) {
                    ball.owner = (uint8_t)pi;
                    bounceBallOffPaddle(ball, p);
                    ball.y = (float)py - h;
                    break;
                }
            }

            // Bricks (one hit per tick per ball)
            for (int ri = 0; ri < MAX_BRICKS; ri++) {
                Brick& br = bricks[ri];
                if (!br.active || br.exploding) continue;
                const int bx = br.x;
                const int by = (int)br.y;
                if (!checkRectCollision(ball.x, ball.y, bx, by, BRICK_WIDTH, BRICK_HEIGHT)) continue;

                if (br.hp > 0) br.hp--;

                const float brickCenterX = (float)bx + (float)BRICK_WIDTH * 0.5f;
                const float brickCenterY = (float)by + (float)BRICK_HEIGHT * 0.5f;
                const float dx = ball.x - brickCenterX;
                const float dy = ball.y - brickCenterY;
                if (fabsf(dx) > fabsf(dy)) ball.vx = (dx > 0) ? fabsf(ball.vx) : -fabsf(ball.vx);
                else ball.vy = (dy > 0) ? fabsf(ball.vy) : -fabsf(ball.vy);
                clampBallSpeed(ball);

                spawnParticles(brickCenterX, brickCenterY, br.baseColor, 4, now);
                if (br.hp == 0) destroyBrick(br, now, ball.owner);
                break;
            }

            // Lost
            if (ball.y > (float)PANEL_RES_Y + h + 2.0f) ball.active = false;
        }
    }

    void ensurePlayerHasBallOrLoseLife(uint32_t now) {
        for (int pi = 0; pi < MAX_GAMEPADS; pi++) {
            if (!players[pi].enabled || players[pi].lives <= 0) continue;
            bool has = false;
            for (int bi = 0; bi < MAX_BALLS; bi++) {
                if (!balls[bi].active) continue;
                if (balls[bi].owner != (uint8_t)pi) continue;
                has = true;
                break;
            }
            if (!has) loseLife((uint8_t)pi, now);
        }
    }

    void updateBrickStream(uint32_t now) {
        if (phase != PHASE_PLAYING) return;

        if ((uint32_t)(now - lastScrollMs) >= brickScrollIntervalMs()) {
            lastScrollMs = now;
            moveBricksDownOnePixel();
        }
        if ((uint32_t)(now - lastRowSpawnMs) >= brickRowSpawnIntervalMs()) {
            lastRowSpawnMs = now;
            spawnBrickRow((float)(HUD_H + 2));
        }

        // Breach handling: lose one life (each active player) and clear the lower band.
        const int topPaddleY = highestPaddleY();
        const int breachY = topPaddleY - 2;
        bool breached = false;
        for (int i = 0; i < MAX_BRICKS; i++) if (bricks[i].active && (int)bricks[i].y >= breachY) { breached = true; break; }
        if (breached) {
            for (int pi = 0; pi < MAX_GAMEPADS; pi++) if (players[pi].enabled && players[pi].lives > 0) loseLife((uint8_t)pi, now);
            const int clearY = topPaddleY - 10;
            for (int i = 0; i < MAX_BRICKS; i++) {
                if (!bricks[i].active) continue;
                if ((int)bricks[i].y >= clearY) {
                    spawnParticles((float)bricks[i].x + 2.0f, bricks[i].y + 1.0f, bricks[i].baseColor, 6, now);
                    bricks[i].active = false;
                }
            }
        }
    }

    void updatePurpleExplosions(uint32_t now) {
        static constexpr uint32_t LIFE_MS = 360;
        for (int i = 0; i < MAX_BRICKS; i++) {
            Brick& b = bricks[i];
            if (!b.active || !b.exploding) continue;
            const uint32_t age = (uint32_t)(now - b.explodeStartMs);
            if ((age % 90) < 16) spawnParticles((float)b.x + 2.0f, b.y + 1.0f, COLOR_PURPLE, 5, now);
            if (age >= LIFE_MS) { b.hp = 0; destroyBrick(b, now, 0); }
        }
    }

    // ---------------------------------------------------------
    // Rendering helpers
    // ---------------------------------------------------------
    void drawBrickShaded(MatrixPanel_I2S_DMA* display, const Brick& b, uint32_t now) const {
        const int x = b.x;
        const int y = (int)b.y;
        if (y < HUD_H || y >= PANEL_RES_Y) return;

        const float hp01 = (b.maxHp > 0) ? (float)b.hp / (float)b.maxHp : 1.0f;
        const uint8_t mul = (uint8_t)(120 + (int)(hp01 * 135.0f));
        uint16_t base = dimColor(b.baseColor, mul);
        if (b.exploding) {
            const bool flicker = ((now / 80) % 2) == 0;
            if (flicker) base = COLOR_WHITE;
        }

        const uint16_t hi = brightenColor(base, 36);
        const uint16_t lo = dimColor(base, 160);
        display->fillRect(x, y, BRICK_WIDTH, BRICK_HEIGHT, base);
        display->fillRect(x, y, BRICK_WIDTH, 1, hi);
        display->fillRect(x, y + BRICK_HEIGHT - 1, BRICK_WIDTH, 1, lo);
    }

    void drawPlayers(MatrixPanel_I2S_DMA* display) const {
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            const Player& p = players[i];
            if (!p.enabled || p.lives <= 0) continue;
            const int x = (int)p.x;
            const int y = p.y;

            display->fillRect(x, y, p.width, PADDLE_H, p.color);

            // Lives below paddle (compact dots)
            const int ly = y + 1;
            if (ly >= 0 && ly < PANEL_RES_Y) {
                for (int dx = 0; dx < min(12, p.width); dx++) display->drawPixel(x + dx, ly, COLOR_BLACK);
                const int cx = x + p.width / 2;
                for (int l = 0; l < min(5, p.lives); l++) display->drawPixel(cx - 2 + l * 2, ly, p.color);
            }
        }
    }

    void drawFloorShield(MatrixPanel_I2S_DMA* display) const {
        if (!floorShieldArmed) return;
        const int sy = PANEL_RES_Y - 1;
        const bool flicker = ((millis() / 240) % 2) == 0;
        const uint16_t c = flicker ? COLOR_BLUE : dimColor(COLOR_BLUE, 160);
        display->drawFastHLine(0, sy, PANEL_RES_X, c);
    }

    void drawBall(MatrixPanel_I2S_DMA* display, const Ball& b) const {
        const int x = (int)b.x - 1;
        const int y = (int)b.y - 1;
        display->fillRect(x, y, BALL_SIZE_PX, BALL_SIZE_PX, b.color);
    }

    void drawPowerup(MatrixPanel_I2S_DMA* display, const PowerUp& pu) const {
        const int x = (int)pu.x;
        const int y = (int)pu.y;
        const uint16_t c = powerupColor(pu.type);
        display->fillRect(x, y, 2, 2, c);
        display->drawPixel(x, y, brightenColor(c, 28));
    }

    void drawParticles(MatrixPanel_I2S_DMA* display, uint32_t now) const {
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (!particles[i].active) continue;
            if ((int32_t)(particles[i].endMs - now) <= 0) continue;
            const int x = (int)particles[i].x;
            const int y = (int)particles[i].y;
            if (x < 0 || x >= PANEL_RES_X || y < HUD_H || y >= PANEL_RES_Y) continue;
            display->drawPixel(x, y, particles[i].color);
        }
    }

public:
    BreakoutGame() = default;

    void start() override {
        gameOver = false;
        score = 0;
        level = 1;
        bricksDestroyed = 0;
        floorShieldArmed = false;
        allClearLock = false;

        phase = PHASE_COUNTDOWN;
        phaseStartMs = (uint32_t)millis();

        lastUpdateMs = phaseStartMs;
        lastScrollMs = phaseStartMs;
        lastRowSpawnMs = phaseStartMs;

        clearPools();

        const int connected = globalControllerManager ? globalControllerManager->getConnectedCount() : 1;
        setupPlayersFromControllers(connected);

        spawnInitialBricks();

        // Spawn one held ball per enabled player.
        for (int i = 0; i < MAX_GAMEPADS; i++) if (players[i].enabled) (void)spawnHeldBall((uint8_t)i, false);
        // Ensure all attached balls are snapped to their paddles immediately.
        updateAttachedBalls();
    }

    void reset() override {
        start();
    }

    void update(ControllerManager* input) override {
        if (gameOver) return;
        const uint32_t now = (uint32_t)millis();
        if ((uint32_t)(now - lastUpdateMs) < UPDATE_INTERVAL_MS) return;
        lastUpdateMs = now;

        if (phase == PHASE_COUNTDOWN) {
            if ((uint32_t)(now - phaseStartMs) >= COUNTDOWN_MS) phase = PHASE_PLAYING;
        }

        updatePlayers(input);
        updateAttachedBalls();
        handleLaunchInputs(input);

        updateBallsAndCollisions(now);
        updatePurpleExplosions(now);
        updatePowerups(now);
        updateParticles(now);

        // All-clear bonus happens BEFORE the stream advances/spawns.
        const bool allClearTriggered = handleAllClearBonus(now);
        if (!allClearTriggered) {
            updateBrickStream(now);
        }

        ensurePlayerHasBallOrLoseLife(now);

        if (alivePlayerCount() <= 0) {
            gameOver = true;
            phase = PHASE_GAME_OVER;
        }
    }

    void draw(MatrixPanel_I2S_DMA* display) override {
        display->fillScreen(COLOR_BLACK);

        const uint32_t now = (uint32_t)millis();

        if (gameOver || phase == PHASE_GAME_OVER) {
            const uint32_t s = leaderboardScore();
            char tag[4];
            UserProfiles::getPadTag(0, tag);
            GameOverLeaderboardView::draw(display, "GAME OVER", leaderboardId(), s, tag);
            return;
        }

        // HUD
        SmallFont::drawStringF(display, 2, 6, COLOR_YELLOW, "S:%d", score);
        SmallFont::drawStringF(display, 34, 6, COLOR_WHITE, "W:%d", level);
        for (int x = 0; x < PANEL_RES_X; x += 2) display->drawPixel(x, HUD_H - 1, COLOR_BLUE);

        if (phase == PHASE_COUNTDOWN) {
            const uint32_t elapsed = (uint32_t)(now - phaseStartMs);
            int secsLeft = 2 - (int)(elapsed / 900UL);
            if (secsLeft < 1) secsLeft = 1;
            char c[2] = { (char)('0' + secsLeft), '\0' };
            SmallFont::drawString(display, 30, 30, c, COLOR_YELLOW);
        }

        // Bricks
        for (int i = 0; i < MAX_BRICKS; i++) if (bricks[i].active) drawBrickShaded(display, bricks[i], now);

        // Powerups
        for (int i = 0; i < MAX_POWERUPS; i++) if (powerups[i].active) drawPowerup(display, powerups[i]);

        // Players
        drawPlayers(display);
        drawFloorShield(display);

        // Balls
        for (int i = 0; i < MAX_BALLS; i++) if (balls[i].active) drawBall(display, balls[i]);

        // Particles
        drawParticles(display, now);
    }

    bool isGameOver() override {
        return gameOver;
    }

    // ------------------------------
    // Leaderboard integration
    // ------------------------------
    bool leaderboardEnabled() const override { return true; }
    const char* leaderboardId() const override { return "breakout"; }
    const char* leaderboardName() const override { return "Breakout"; }
    uint32_t leaderboardScore() const override { return (score > 0) ? (uint32_t)score : 0u; }
};

