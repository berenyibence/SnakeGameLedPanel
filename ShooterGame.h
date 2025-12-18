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
 * ShooterGame - Space shooter game
 * Player controls a ship at the bottom, shoots enemies from above
 */
class ShooterGame : public GameBase {
private:
    // ---------------------------------------------------------
    // Analog / Trigger helpers (Bluepad32 API varies across versions/controllers)
    // ---------------------------------------------------------
    struct InputDetail {
        template <typename T>
        static auto axisX(T* c, int) -> decltype(c->axisX(), int16_t()) { return (int16_t)c->axisX(); }
        template <typename T>
        static int16_t axisX(T*, ...) { return 0; }

        template <typename T>
        static auto throttle(T* c, int) -> decltype(c->throttle(), uint16_t()) { return (uint16_t)c->throttle(); }
        template <typename T>
        static uint16_t throttle(T*, ...) { return 0; }

        template <typename T>
        static auto r2(T* c, int) -> decltype(c->r2(), bool()) { return (bool)c->r2(); }
        template <typename T>
        static bool r2(T*, ...) { return false; }
    };

    static inline float clampf(float v, float lo, float hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }
    static inline float deadzone01(float v, float dz) {
        const float a = fabsf(v);
        if (a <= dz) return 0.0f;
        const float s = (a - dz) / (1.0f - dz);
        return (v < 0) ? -s : s;
    }

    // HUD band (keep gameplay below it)
    static constexpr int HUD_H = 8;

    // Tuning (use inline getters for floats to avoid Arduino linker ODR issues)
    static constexpr int16_t AXIS_DIVISOR = 512;      // ~[-512..512]
    static constexpr float STICK_DEADZONE = 0.18f;    // 0..1
    static constexpr float MOVE_SMOOTH = 0.22f;       // 0..1
    static constexpr uint16_t TRIGGER_THRESHOLD = 360;

    // ---------------------------------------------------------
    // Visuals (sprites)
    // ---------------------------------------------------------
    // Player ship sprite: 5x5 (drawn pixel-art style, not a filled box)
    static constexpr int SHIP_W = 5;
    static constexpr int SHIP_H = 5;

    // Bullets: 1px wide with fading tail
    static constexpr int BULLET_LEN = 3;       // player bullets
    static constexpr int ENEMY_BULLET_LEN = 4; // enemy bullets (a bit longer)

    // Round phases
    // - Countdown on start only
    // - Game continues immediately on hit (no full-screen pauses)
    // - On final death: freeze the field for a short time, then show game-over leaderboard
    enum RoundPhase : uint8_t { PHASE_COUNTDOWN, PHASE_PLAYING, PHASE_GAME_OVER_DELAY, PHASE_GAME_OVER };
    RoundPhase phase = PHASE_COUNTDOWN;
    uint32_t phaseStartMs = 0;
    static constexpr uint16_t COUNTDOWN_MS = 3000;
    static constexpr uint16_t GAME_OVER_FREEZE_MS = 3000;

    // ---------------------------------------------------------
    // Player
    // ---------------------------------------------------------
    struct Ship {
        float x;         // left position (float for smooth movement)
        int y;           // top position (int, anchored near bottom)
        float speed;     // px per tick at full stick
        uint16_t color;  // primary sprite color
        float vx;        // smoothed velocity
        
        // Speed values in this project are effectively "px per tick" (tick ~= 16ms).
        // Keep the base movement intentionally slower for a more relaxed feel.
        Ship() : x(32.0f), y(PANEL_RES_Y - 1 - SHIP_H), speed(1.6f), color(COLOR_GREEN), vx(0.0f) {}
    };
    
    struct Bullet {
        int x;
        int y;
        int8_t vy;     // -2 for player bullets (up), +1 for enemy bullets (down)
        bool active;
        uint16_t color; // base color for head
        uint8_t dmg;    // damage dealt on hit
    };

    struct PowerUp {
        float x;
        float y;
        uint8_t type;   // 0=shield(blue), 1=triple(red), 2=life(green)
        bool active;
        float vx;
        float vy;
        uint8_t tier;   // 1..5 for red/blue, unused for green
    };
    
    // Enemy structure
    struct Enemy {
        float x;
        float y;
        float vx;
        float vy;
        bool alive;
        int type;            // 0..3
        uint32_t nextShotMs; // per-enemy firing timer
        uint8_t hp;          // current health 1..4
        uint8_t maxHp;       // max health for pips
        
        Enemy() : x(0), y(0), vx(0), vy(0), alive(false), type(0), nextShotMs(0), hp(1), maxHp(1) {}
        Enemy(float xPos, float yPos, int t, float vxIn, float vyIn, uint32_t now) 
            : x(xPos), y(yPos), vx(vxIn), vy(vyIn), alive(true), type(t), nextShotMs(now), hp(1), maxHp(1) {}
    };
    
    Ship player;

    // Avoid heap churn: keep fixed-size pools for bullets and powerups.
    static constexpr int MAX_PLAYER_BULLETS = 18;
    static constexpr int MAX_ENEMY_BULLETS  = 18;
    static constexpr int MAX_ENEMIES        = 20;
    static constexpr int MAX_POWERUPS       = 6;

    Bullet playerBullets[MAX_PLAYER_BULLETS] = {};
    Bullet enemyBullets[MAX_ENEMY_BULLETS] = {};
    Enemy enemies[MAX_ENEMIES] = {};
    PowerUp powerups[MAX_POWERUPS] = {};

    bool gameOver;
    int score;
    int level;
    int lives;
    int kills; // for level progression
    unsigned long lastUpdate;
    unsigned long lastShot;
    static const int UPDATE_INTERVAL_MS = 16;  // ~60 FPS
    static const int SHOT_COOLDOWN_MS = 200;

    // Continuous spawning
    uint32_t lastSpawnMs = 0;
    static constexpr uint32_t SPAWN_INTERVAL_MS = 900;

    // Powerup timers (ms)
    uint32_t shieldUntilMs = 0;
    uint32_t weaponUntilMs = 0;
    uint8_t shieldTier = 0;  // 0=none, 1..5
    uint8_t weaponTier = 0;  // 0=none, 1..5

    // Tier durations (seconds): 10,20,30,40,50
    static inline uint32_t tierDurationMs(uint8_t tier) {
        const uint8_t t = (uint8_t)constrain(tier, 1, 5);
        return (uint32_t)t * 10000UL;
    }

    // Shield radius by tier: base 4, +0,+2,+4,+6,+10
    static inline uint8_t shieldRadiusForTier(uint8_t tier) {
        if (tier <= 1) return 4;
        if (tier == 2) return 6;
        if (tier == 3) return 8;
        if (tier == 4) return 10;
        return 14;
    }

    // Hit feedback (no full-screen pauses)
    uint32_t invulnUntilMs = 0;
    uint32_t shieldHitFlashUntilMs = 0;

    // Rumble feedback helpers (SFINAE for Bluepad32 API variations)
    struct RumbleDetail {
        template <typename T>
        static auto playDualRumble(T* c, int, uint16_t, uint16_t, uint16_t) -> decltype(c->playDualRumble(0, 0, 0), bool()) {
            return true;
        }
        template <typename T>
        static bool playDualRumble(T*, ...) { return false; }

        template <typename T>
        static auto setRumble(T* c, int, uint8_t, uint8_t) -> decltype(c->setRumble(0, 0), bool()) {
            return true;
        }
        template <typename T>
        static bool setRumble(T*, ...) { return false; }
    };

    // ---------------------------------------------------------
    // Explosion FX (small and cheap)
    // ---------------------------------------------------------
    struct Explosion {
        int x;
        int y;
        uint16_t color;
        uint32_t startMs;
        bool active;
    };
    static constexpr int MAX_EXPLOSIONS = 10;
    Explosion explosions[MAX_EXPLOSIONS] = {};

    void spawnExplosion(int x, int y, uint16_t color, uint32_t now) {
        for (int i = 0; i < MAX_EXPLOSIONS; i++) {
            if (explosions[i].active) continue;
            explosions[i].active = true;
            explosions[i].x = x;
            explosions[i].y = y;
            explosions[i].color = color;
            explosions[i].startMs = now;
            return;
        }
        // If full, overwrite the oldest.
        int oldest = 0;
        for (int i = 1; i < MAX_EXPLOSIONS; i++) {
            if (explosions[i].startMs < explosions[oldest].startMs) oldest = i;
        }
        explosions[oldest].active = true;
        explosions[oldest].x = x;
        explosions[oldest].y = y;
        explosions[oldest].color = color;
        explosions[oldest].startMs = now;
    }

    void drawExplosions(MatrixPanel_I2S_DMA* display, uint32_t now) {
        // Tiny expanding ring/spark burst (slower / softer).
        static constexpr uint32_t LIFE_MS = 420;
        for (int i = 0; i < MAX_EXPLOSIONS; i++) {
            if (!explosions[i].active) continue;
            const uint32_t age = (uint32_t)(now - explosions[i].startMs);
            if (age >= LIFE_MS) {
                explosions[i].active = false;
                continue;
            }
            const uint16_t c = explosions[i].color;
            const int x = explosions[i].x;
            const int y = explosions[i].y;
            const int r = (int)(age / 110); // 0..3 slower
            // Cross + diagonals (looks like a small explosion)
            display->drawPixel(x, y, c);
            if (r >= 1) {
                display->drawPixel(x + 1, y, c);
                display->drawPixel(x - 1, y, c);
                display->drawPixel(x, y + 1, c);
                display->drawPixel(x, y - 1, c);
            }
            if (r >= 2) {
                display->drawPixel(x + 1, y + 1, c);
                display->drawPixel(x - 1, y + 1, c);
                display->drawPixel(x + 1, y - 1, c);
                display->drawPixel(x - 1, y - 1, c);
            }
            if (r >= 3) {
                display->drawPixel(x + 2, y, c);
                display->drawPixel(x - 2, y, c);
                display->drawPixel(x, y + 2, c);
                display->drawPixel(x, y - 2, c);
            }
        }
    }
    
    // ---------------------------------------------------------
    // Sprites (bitmaps)
    // ---------------------------------------------------------
    // 5x5 ship (1 = draw). Colors are applied at draw-time.
    // Shape inspired by classic arcade ships: narrow nose, wings, engine.
    // NOTE (Arduino/ESP32): These arrays are ODR-used and require out-of-class
    // definitions (C++11). See definitions at the bottom of this header.
    static const uint8_t SHIP_SPRITE[SHIP_H][SHIP_W];

    // 4 enemy types, each 4x4 (1 = draw).
    static constexpr uint8_t ENEMY_W = 4;
    static constexpr uint8_t ENEMY_H = 4;
    static const uint8_t ENEMY_SPRITES[4][ENEMY_H][ENEMY_W];
    static const uint16_t ENEMY_COLORS[4];

    // ---------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------
    static inline bool rectContains(int x, int y, int rx, int ry, int rw, int rh) {
        return (x >= rx && x < rx + rw && y >= ry && y < ry + rh);
    }

    static inline uint16_t dimColor(MatrixPanel_I2S_DMA* d, uint16_t c, uint8_t mul /*0..255*/) {
        // Approximate RGB565 dimming by scaling 5/6/5 channels.
        uint8_t r = (uint8_t)((c >> 11) & 0x1F);
        uint8_t g = (uint8_t)((c >> 5) & 0x3F);
        uint8_t b = (uint8_t)(c & 0x1F);
        r = (uint8_t)((r * mul) / 255);
        g = (uint8_t)((g * mul) / 255);
        b = (uint8_t)((b * mul) / 255);
        return (uint16_t)((r << 11) | (g << 5) | b);
    }

    void clearBullets() {
        for (int i = 0; i < MAX_PLAYER_BULLETS; i++) playerBullets[i].active = false;
        for (int i = 0; i < MAX_ENEMY_BULLETS; i++) enemyBullets[i].active = false;
    }

    void clearPowerups() {
        for (int i = 0; i < MAX_POWERUPS; i++) powerups[i].active = false;
    }

    int aliveEnemyCount() const {
        int c = 0;
        for (int i = 0; i < MAX_ENEMIES; i++) if (enemies[i].alive) c++;
        return c;
    }
    
    void spawnEnemy(uint32_t now) {
        // Find a free slot.
        int slot = -1;
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (!enemies[i].alive) { slot = i; break; }
        }
        if (slot < 0) return;

        // Organic spawn: random x, shallow y near top band, random drift.
        const float x = (float)random(2, PANEL_RES_X - (int)ENEMY_W - 2);
        const float y = (float)random(HUD_H + 2, HUD_H + 16);

        const int type = random(0, 4);
        const float baseVx = 0.20f + 0.02f * (float)min(10, max(0, level - 1));
        const float vx = (random(0, 2) == 0) ? -baseVx : baseVx;
        const float vy = 0.02f + 0.005f * (float)min(12, max(0, level - 1));

        enemies[slot] = Enemy(x, y, type, vx, vy, now);

        // Health progression: start with 1HP, increase odds/strength with level.
        // maxHP is 1..4 and is shown as 4 pips above the enemy.
        uint8_t hp = 1;
        const int lvl = max(1, level);
        // At higher levels, allow stronger enemies to appear.
        const int r = random(0, 100);
        if (lvl >= 3 && r < 25) hp = 2;
        if (lvl >= 6 && r < 18) hp = 3;
        if (lvl >= 10 && r < 12) hp = 4;
        enemies[slot].hp = hp;
        enemies[slot].maxHp = hp;

        // Avoid immediate "spawn shot" spikes; feels like difficulty didn't reset.
        enemies[slot].nextShotMs = now + (uint32_t)random(1200, 3200);
    }

    void resetPlayerAndBullets() {
        player.x = PANEL_RES_X / 2.0f - (float)SHIP_W / 2.0f;
        player.vx = 0.0f;
        clearBullets();
        lastShot = 0;
    }

    void loseLife(uint32_t now) {
        lives--;
        if (lives <= 0) {
            // Final death: freeze field for a short time before showing leaderboard.
            phase = PHASE_GAME_OVER_DELAY;
            phaseStartMs = now;
            return;
        }
        // Brief invulnerability so you don't get instantly chain-hit.
        invulnUntilMs = now + 900;
    }

    // ---------------------------------------------------------
    // Gameplay: bullets / enemies / powerups
    // ---------------------------------------------------------
    void spawnPlayerBullet(int x, int y) {
        for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
            if (playerBullets[i].active) continue;
            playerBullets[i].active = true;
            playerBullets[i].x = x;
            playerBullets[i].y = y;
            playerBullets[i].vy = -2;
            // Weapon tier determines bullet pattern, color and damage.
            const uint32_t now = millis();
            const bool weaponActive = ((int32_t)(weaponUntilMs - now) > 0);
            const uint8_t t = weaponActive ? weaponTier : 0;
            if (t == 2 || t == 5 || t == 4) playerBullets[i].color = COLOR_RED;
            else playerBullets[i].color = COLOR_CYAN;

            // Damage: tier1=1, tier2=2, tier3=1/shot, tier4 center=2 sides=1, tier5=2/shot
            // Default (tier0): same as tier1 (1 dmg).
            if (t == 2 || t == 5) playerBullets[i].dmg = 2;
            else if (t == 4) playerBullets[i].dmg = 1; // overridden by caller for center bullet
            else playerBullets[i].dmg = 1;
            return;
        }
    }

    void spawnEnemyBullet(int x, int y, uint8_t type) {
        for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
            if (enemyBullets[i].active) continue;
            enemyBullets[i].active = true;
            enemyBullets[i].x = x;
            enemyBullets[i].y = y;
            enemyBullets[i].vy = +1; // slower enemy bullets
            enemyBullets[i].color = ENEMY_COLORS[type % 4];
            enemyBullets[i].dmg = 1;
            return;
        }
    }

    void maybeDropPowerup(float x, float y, float kickVx, float kickVy) {
        // Keep it occasional.
        const int dropChance = 18; // %
        if (random(0, 100) >= dropChance) return;

        // Find slot.
        int slot = -1;
        for (int i = 0; i < MAX_POWERUPS; i++) {
            if (!powerups[i].active) { slot = i; break; }
        }
        if (slot < 0) return;

        // Choose type (bias towards life if low).
        uint8_t t = 0;
        const int r = random(0, 100);
        if (lives < 3 && r < 30) t = 2;          // green life
        else if (r < 55) t = 0;                 // blue shield
        else t = 1;                              // red triple shot

        powerups[slot].active = true;
        powerups[slot].x = x;
        powerups[slot].y = y;
        powerups[slot].type = t;
        // Launch away from explosion so it's harder to catch.
        powerups[slot].vx = kickVx;
        powerups[slot].vy = kickVy;
        powerups[slot].tier = 0;
    }

    void applyPowerup(uint8_t type, uint32_t now) {
        if (type == 0) { // Blue Pack (shield tier)
            if ((int32_t)(shieldUntilMs - now) > 0) shieldTier = min<uint8_t>(5, shieldTier + 1);
            else shieldTier = 1;
            shieldUntilMs = now + tierDurationMs(shieldTier);
        } else if (type == 1) { // Red Pack (weapon tier)
            if ((int32_t)(weaponUntilMs - now) > 0) weaponTier = min<uint8_t>(5, weaponTier + 1);
            else weaponTier = 1;
            weaponUntilMs = now + tierDurationMs(weaponTier);
        } else { // Green pack: +life (no tiers)
            lives = min(3, lives + 1);
        }
    }

    void updateBulletsAndPowerups(uint32_t now) {
        // Player bullets
        for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
            if (!playerBullets[i].active) continue;
            playerBullets[i].y += playerBullets[i].vy;
            if (playerBullets[i].y < HUD_H) playerBullets[i].active = false;
        }

        // Enemy bullets
        for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
            if (!enemyBullets[i].active) continue;
            enemyBullets[i].y += enemyBullets[i].vy;
            if (enemyBullets[i].y > PANEL_RES_Y - 1) enemyBullets[i].active = false;
        }

        // Powerups
        for (int i = 0; i < MAX_POWERUPS; i++) {
            if (!powerups[i].active) continue;
            // Simple ballistic motion (kick + gravity + drag)
            powerups[i].x += powerups[i].vx;
            powerups[i].y += powerups[i].vy;
            powerups[i].vy += 0.035f;    // gravity (slower)
            powerups[i].vx *= 0.990f;    // drag (more stable / slower)

            // Catch by player ship bounds
            const int px = (int)player.x;
            const int py = player.y;
            if (powerups[i].y >= (float)py - 1 &&
                powerups[i].x >= (float)(px - 1) &&
                powerups[i].x <= (float)(px + SHIP_W)) {
                applyPowerup(powerups[i].type, now);
                powerups[i].active = false;
                continue;
            }

            if (powerups[i].y > PANEL_RES_Y) powerups[i].active = false;
        }
    }

    void updateEnemiesAndEnemyFire(uint32_t now) {
        const float minY = (float)HUD_H;
        const float maxY = (float)(player.y - ENEMY_H - 1);
        for (int i = 0; i < MAX_ENEMIES; i++) {
            Enemy& e = enemies[i];
            if (!e.alive) continue;

            // Organic drift
            e.x += e.vx;
            e.y += e.vy;

            // Soft bounce on edges
            if (e.x < 1) { e.x = 1; e.vx = fabsf(e.vx); }
            if (e.x > (float)(PANEL_RES_X - ENEMY_W - 1)) { e.x = (float)(PANEL_RES_X - ENEMY_W - 1); e.vx = -fabsf(e.vx); }

            // Keep within vertical band; if it sinks too far, push up and speed up slightly.
            if (e.y < minY) e.y = minY;
            if (e.y > maxY) {
                // Reaching the player zone counts as a hit.
                const bool shield = ((int32_t)(shieldUntilMs - now) > 0);
                if (!shield) loseLife(now);
                // Reset this enemy so it doesn't chain-hit.
                e.alive = false;
                continue;
            }

            // Enemy firing: VERY rare early, more frequent as levels increase.
            if (now >= e.nextShotMs) {
                const uint32_t baseInterval = 12000;
                const uint32_t dec = (uint32_t)min(8500, (level - 1) * 520);
                const uint32_t interval = max(2400u, baseInterval - dec);
                e.nextShotMs = now + interval + (uint32_t)random(0, 1600);

                const float p = min(0.45f, 0.02f + 0.025f * (float)max(0, level - 1));
                const int roll = random(0, 1000);
                if ((float)roll < p * 1000.0f) {
                    const int bx = (int)e.x + 1;
                    const int by = (int)e.y + (int)ENEMY_H;
                    spawnEnemyBullet(bx, by, (uint8_t)e.type);
                }
            }
        }
    }

    void handleCollisions(uint32_t now) {
        // Player bullets vs enemies
        for (int bi = 0; bi < MAX_PLAYER_BULLETS; bi++) {
            Bullet& b = playerBullets[bi];
            if (!b.active) continue;
            const int bx = b.x;
            const int by = b.y;

            for (int ei = 0; ei < MAX_ENEMIES; ei++) {
                Enemy& e = enemies[ei];
                if (!e.alive) continue;
                if (rectContains(bx, by, (int)e.x, (int)e.y, ENEMY_W, ENEMY_H)) {
                    // Apply damage
                    const uint8_t dmg = max<uint8_t>(1, b.dmg);
                    if (e.hp > dmg) e.hp = (uint8_t)(e.hp - dmg);
                    else e.hp = 0;

                    if (e.hp == 0) {
                        e.alive = false;
                        kills++;
                        score += 10 + (e.type * 5);
                        // Explosion + powerup kick
                        const int ex = (int)e.x + 1;
                        const int ey = (int)e.y + 1;
                        spawnExplosion(ex, ey, ENEMY_COLORS[e.type & 3], now);
                        // Slower drop motion: always launched upward with small sideways drift.
                        const float kickVx = ((float)random(-35, 36) / 100.0f) * 0.6f; // ~-0.21..0.21
                        const float kickVy = -(((float)random(20, 46) / 100.0f) * 0.55f); // ~-0.11..-0.25
                        maybeDropPowerup(e.x + 1.0f, e.y + 2.0f, kickVx, kickVy);
                    }
                    b.active = false;
                    break;
                }
            }
        }

        // Enemy bullets vs player (shield neutralizes bullets, not just protects on hit)
        const bool shieldActive = ((int32_t)(shieldUntilMs - now) > 0) && shieldTier > 0;
        const uint8_t shieldR = shieldActive ? shieldRadiusForTier(shieldTier) : 0;
        const int cx = (int)player.x + 2;
        const int cy = player.y + 2;

        // Invulnerability window after taking damage.
        const bool invuln = ((int32_t)(invulnUntilMs - now) > 0);
        const int px = (int)player.x;
        const int py = player.y;
        for (int bi = 0; bi < MAX_ENEMY_BULLETS; bi++) {
            Bullet& b = enemyBullets[bi];
            if (!b.active) continue;

            // Shield neutralization (circle)
            if (shieldActive) {
                const int dx = b.x - cx;
                const int dy = b.y - cy;
                if ((dx * dx + dy * dy) <= (int)shieldR * (int)shieldR) {
                    b.active = false;
                    shieldHitFlashUntilMs = now + 120;
                    // Shield loses one tier per neutralized bullet.
                    if (shieldTier > 0) {
                        shieldTier--;
                        if (shieldTier == 0) shieldUntilMs = 0;
                    }
                    continue;
                }
            }

            if (rectContains(b.x, b.y, px, py, SHIP_W, SHIP_H)) {
                b.active = false;
                if (!invuln) {
                    // Hit feedback: flash shield red briefly and rumble.
                    shieldHitFlashUntilMs = now + 180;
                    // Player explosion
                    spawnExplosion(cx, cy, COLOR_ORANGE, now);
                    loseLife(now);
                    ControllerPtr p1 = globalControllerManager ? globalControllerManager->getController(0) : nullptr;
                    if (p1) {
                        // Try common Bluepad32 APIs (no-op if unavailable).
                        (void)RumbleDetail::playDualRumble(p1, 0, 0xFFFF, 0x4000, 180);
                        (void)RumbleDetail::setRumble(p1, 0, 180, 60);
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------
    // Rendering helpers
    // ---------------------------------------------------------
    void drawShip(MatrixPanel_I2S_DMA* display, int x, int y, uint16_t color, bool shield) {
        for (int yy = 0; yy < SHIP_H; yy++) {
            for (int xx = 0; xx < SHIP_W; xx++) {
                if (!SHIP_SPRITE[yy][xx]) continue;
                display->drawPixel(x + xx, y + yy, color);
            }
        }

        // Lives indicator is embedded into the ship sprite:
        // Bottom 3 pixels are always green when "present".
        // Mapping:
        // - 3 lives: left + middle + right
        // - 2 lives: left + right (middle off)
        // - 1 life: middle only
        const int ly = y + (SHIP_H - 1);
        // Clear indicator pixels first (so they are always controlled here).
        display->drawPixel(x + 1, ly, COLOR_BLACK);
        display->drawPixel(x + 2, ly, COLOR_BLACK);
        display->drawPixel(x + 3, ly, COLOR_BLACK);
        if (lives >= 3) {
            display->drawPixel(x + 1, ly, COLOR_GREEN);
            display->drawPixel(x + 2, ly, COLOR_GREEN);
            display->drawPixel(x + 3, ly, COLOR_GREEN);
        } else if (lives == 2) {
            display->drawPixel(x + 1, ly, COLOR_GREEN);
            display->drawPixel(x + 3, ly, COLOR_GREEN);
        } else if (lives == 1) {
            display->drawPixel(x + 2, ly, COLOR_GREEN);
        }

        if (shield) {
            const uint32_t now = millis();
            const uint8_t tier = max<uint8_t>(1, shieldTier);
            const uint8_t r = shieldRadiusForTier(tier);

            // Light cyan flicker (and red flash on hit).
            const bool flash = ((int32_t)(shieldHitFlashUntilMs - now) > 0);
            // Slower flicker
            const bool flicker = ((now / 320) % 2) == 0;
            uint16_t c = COLOR_CYAN;
            if (!flash && flicker) c = dimColor(display, COLOR_CYAN, 200);
            if (flash) c = COLOR_RED;

            const int cx = x + 2;
            const int cy = y + 2;
            display->drawCircle(cx, cy, r, c);
        }
    }

    void drawEnemy(MatrixPanel_I2S_DMA* display, int x, int y, int type) {
        const uint16_t c = ENEMY_COLORS[type & 3];
        for (int yy = 0; yy < ENEMY_H; yy++) {
            for (int xx = 0; xx < ENEMY_W; xx++) {
                if (!ENEMY_SPRITES[type & 3][yy][xx]) continue;
                display->drawPixel(x + xx, y + yy, c);
            }
        }

        // Enemy HP pips: 4 pixels at the top of the enemy (above sprite if possible).
        // Stronger enemies (2..4 hp) show more pips.
        const int pipY = (y > HUD_H) ? (y - 1) : y;
        // We don't have a pointer here; use the sprite position + type only.
        // The caller draws this immediately after using enemies[i], so it passes
        // the correct current hp/maxHp via the overload below.
    }

    void drawEnemy(MatrixPanel_I2S_DMA* display, const Enemy& e) {
        drawEnemy(display, (int)e.x, (int)e.y, e.type);
        const int x = (int)e.x;
        const int y = (int)e.y;
        const int pipY = (y > HUD_H) ? (y - 1) : y;
        const uint8_t maxHp = (uint8_t)constrain(e.maxHp, 1, 4);
        const uint8_t hp = (uint8_t)constrain(e.hp, 0, 4);

        for (uint8_t i = 0; i < maxHp; i++) {
            const uint16_t col = (i < hp) ? COLOR_GREEN : dimColor(display, COLOR_GREEN, 60);
            display->drawPixel(x + (int)i, pipY, col);
        }
    }

    void drawBullet(MatrixPanel_I2S_DMA* display, const Bullet& b, bool playerUp) {
        // Fading tail (enemy bullets are a bit longer)
        const uint16_t head = b.color;
        const uint16_t mid = dimColor(display, b.color, 160);
        const uint16_t tail = dimColor(display, b.color, 90);
        const uint16_t tail2 = dimColor(display, b.color, 40);
        if (playerUp) {
            // Head at (x,y), tail below
            display->drawPixel(b.x, b.y, head);
            display->drawPixel(b.x, b.y + 1, mid);
            display->drawPixel(b.x, b.y + 2, tail);
        } else {
            // Enemy bullet: head at (x,y), tail above
            display->drawPixel(b.x, b.y, head);
            display->drawPixel(b.x, b.y - 1, mid);
            display->drawPixel(b.x, b.y - 2, tail);
            display->drawPixel(b.x, b.y - 3, tail2);
        }
    }

    void drawPowerup(MatrixPanel_I2S_DMA* display, int x, int y, uint8_t type) {
        const uint16_t c = (type == 0) ? COLOR_BLUE : (type == 1) ? COLOR_RED : COLOR_GREEN;
        // 2x2 box
        display->fillRect(x, y, 2, 2, c);
    }

    void drawHudStatus(MatrixPanel_I2S_DMA* display) {
        // Optional: show shield/triple status dots (top-right, small and unobtrusive).
        const uint32_t now = millis();
        if ((int32_t)(shieldUntilMs - now) > 0) display->drawPixel(PANEL_RES_X - 1, 2, COLOR_CYAN);
        if ((int32_t)(weaponUntilMs - now) > 0) display->drawPixel(PANEL_RES_X - 3, 2, COLOR_RED);
    }

public:
    ShooterGame() 
        : gameOver(false), score(0), level(1), lives(3), lastUpdate(0), 
          lastShot(0) {
        kills = 0;
    }

    void start() override {
        gameOver = false;
        score = 0;
        level = 1;
        lives = 3;
        kills = 0;
        lastUpdate = millis();
        lastShot = 0;
        phase = PHASE_COUNTDOWN;
        phaseStartMs = lastUpdate;
        shieldUntilMs = 0;
        weaponUntilMs = 0;
        shieldTier = 0;
        weaponTier = 0;
        // Prevent immediate spawn bursts based on uptime.
        lastSpawnMs = (uint32_t)lastUpdate;
        invulnUntilMs = 0;
        shieldHitFlashUntilMs = 0;
        clearBullets();
        clearPowerups();
        for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].alive = false;
        
        // Apply current global player color (chosen in the main menu).
        player.color = globalSettings.getPlayerColor();

        resetPlayerAndBullets();
        // Spawn initial enemies (small, sparse)
        for (int i = 0; i < 10; i++) spawnEnemy((uint32_t)lastUpdate);
    }

    void reset() override {
        start();
    }

    void update(ControllerManager* input) override {
        if (gameOver) return;
        
        // Throttle updates
        unsigned long now = millis();
        if (now - lastUpdate < UPDATE_INTERVAL_MS) return;
        lastUpdate = now;

        // Final freeze before game over overlay/leaderboard.
        if (phase == PHASE_GAME_OVER_DELAY) {
            if ((uint32_t)(now - phaseStartMs) >= GAME_OVER_FREEZE_MS) {
                gameOver = true;
                phase = PHASE_GAME_OVER;
            }
            return;
        }

        if (phase == PHASE_COUNTDOWN) {
            if ((uint32_t)(now - phaseStartMs) >= COUNTDOWN_MS) {
                phase = PHASE_PLAYING;
            }
            // Allow the player to reposition during countdown.
        }
        
        // Update player position
        ControllerPtr p1 = input->getController(0);
        if (p1 && p1->isConnected()) {
            const float rawX = clampf((float)InputDetail::axisX(p1, 0) / (float)AXIS_DIVISOR, -1.0f, 1.0f);
            float sx = deadzone01(rawX, STICK_DEADZONE);
            if (sx == 0.0f) {
                const uint8_t dpad = p1->dpad();
                if (dpad & 0x08) sx = -1.0f;
                else if (dpad & 0x04) sx = 1.0f;
            }

            const float targetVx = sx * player.speed;
            player.vx = player.vx * (1.0f - MOVE_SMOOTH) + targetVx * MOVE_SMOOTH;
            player.x += player.vx;
            player.x = constrain(player.x, 0.0f, (float)(PANEL_RES_X - SHIP_W));
            
            // Shoot with right trigger (fallback to A)
            const uint16_t rt = InputDetail::throttle(p1, 0);
            const bool shoot = (rt >= TRIGGER_THRESHOLD) || InputDetail::r2(p1, 0) || p1->a();
            if (shoot && (now - lastShot > SHOT_COOLDOWN_MS) && phase == PHASE_PLAYING) {
                const int cx = (int)(player.x + (float)SHIP_W / 2.0f);

                // Weapon tier patterns:
                // tier1: standard cyan 1 dmg (duration 10s)
                // tier2: red 2 dmg (duration 20s)
                // tier3: 3 rows cyan, 1 dmg each (duration 30s)
                // tier4: middle red 2 dmg, 2 side cyan 1 dmg (duration 40s)
                // tier5: 3 rows red, 2 dmg each (duration 50s)
                const uint32_t nowMs = (uint32_t)now;
                const bool weaponActive = ((int32_t)(weaponUntilMs - nowMs) > 0) && weaponTier > 0;
                const uint8_t t = weaponActive ? weaponTier : 0;

                if (t == 3 || t == 4 || t == 5) {
                    // 3 rows
                    spawnPlayerBullet(cx, player.y - 1);
                    spawnPlayerBullet(cx - 2, player.y - 1);
                    spawnPlayerBullet(cx + 2, player.y - 1);

                    // tier4: make center bullet "red 2 dmg"
                    if (t == 4) {
                        for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
                            if (!playerBullets[i].active) continue;
                            if (playerBullets[i].x == cx && playerBullets[i].y == player.y - 1) {
                                playerBullets[i].color = COLOR_RED;
                                playerBullets[i].dmg = 2;
                                break;
                            }
                        }
                    }

                    // tier5: all bullets red 2 dmg
                    if (t == 5) {
                        for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
                            if (!playerBullets[i].active) continue;
                            if (playerBullets[i].y == player.y - 1 &&
                                (playerBullets[i].x == cx || playerBullets[i].x == cx - 2 || playerBullets[i].x == cx + 2)) {
                                playerBullets[i].color = COLOR_RED;
                                playerBullets[i].dmg = 2;
                            }
                        }
                    }
                } else {
                    // Single bullet
                    spawnPlayerBullet(cx, player.y - 1);
                    // tier2: red 2 dmg
                    if (t == 2) {
                        for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
                            if (!playerBullets[i].active) continue;
                            if (playerBullets[i].x == cx && playerBullets[i].y == player.y - 1) {
                                playerBullets[i].color = COLOR_RED;
                                playerBullets[i].dmg = 2;
                                break;
                            }
                        }
                    }
                }
                lastShot = now;
            }
        }

        if (phase != PHASE_PLAYING) return;

        // Continuous spawning (target scales with level)
        const int target = min(MAX_ENEMIES, 8 + level);
        if ((uint32_t)(now - lastSpawnMs) >= SPAWN_INTERVAL_MS && aliveEnemyCount() < target) {
            lastSpawnMs = (uint32_t)now;
            spawnEnemy((uint32_t)now);
        }

        updateBulletsAndPowerups((uint32_t)now);
        updateEnemiesAndEnemyFire((uint32_t)now);
        handleCollisions((uint32_t)now);

        // Level progression: every 15 kills -> next level.
        if (kills >= level * 15) level++;
    }

    void draw(MatrixPanel_I2S_DMA* display) override {
        display->fillScreen(COLOR_BLACK);

        const uint32_t now = (uint32_t)millis();

        // Final freeze: keep the last frame visible for a few seconds.
        if (phase == PHASE_GAME_OVER_DELAY) {
            // HUD stays visible while frozen.
            SmallFont::drawStringF(display, 2, 6, COLOR_YELLOW, "S:%d", score);
            SmallFont::drawStringF(display, 38, 6, COLOR_WHITE, "W:%d", level);
            drawHudStatus(display);
            for (int x = 0; x < PANEL_RES_X; x += 2) display->drawPixel(x, HUD_H - 1, COLOR_BLUE);

            // Render frozen entities
            for (int i = 0; i < MAX_ENEMIES; i++) if (enemies[i].alive) drawEnemy(display, enemies[i]);
            for (int i = 0; i < MAX_POWERUPS; i++) if (powerups[i].active) drawPowerup(display, (int)powerups[i].x, (int)powerups[i].y, powerups[i].type);
            for (int i = 0; i < MAX_PLAYER_BULLETS; i++) if (playerBullets[i].active) drawBullet(display, playerBullets[i], true);
            for (int i = 0; i < MAX_ENEMY_BULLETS; i++) if (enemyBullets[i].active) drawBullet(display, enemyBullets[i], false);
            drawShip(display, (int)player.x, player.y, player.color, ((int32_t)(shieldUntilMs - now) > 0));
            drawExplosions(display, now);

            // After delay, we will enter GAME_OVER (handled in update()).
            SmallFont::drawString(display, 12, 30, "GAME OVER", COLOR_RED);
            return;
        }

        // IMPORTANT: if we're in GAME OVER, don't draw the normal HUD underneath.
        if (phase == PHASE_GAME_OVER || gameOver) {
            char tag[4];
            UserProfiles::getPadTag(0, tag);
            GameOverLeaderboardView::draw(display, "GAME OVER", leaderboardId(), leaderboardScore(), tag);
            return;
        }

        // HUD (only for non-game-over screens)
        SmallFont::drawStringF(display, 2, 6, COLOR_YELLOW, "S:%d", score);
        SmallFont::drawStringF(display, 38, 6, COLOR_WHITE, "W:%d", level);
        drawHudStatus(display);
        for (int x = 0; x < PANEL_RES_X; x += 2) display->drawPixel(x, HUD_H - 1, COLOR_BLUE);

        if (phase == PHASE_COUNTDOWN) {
            const uint32_t now = millis();
            const uint32_t elapsed = (uint32_t)(now - phaseStartMs);
            int secsLeft = 3 - (int)(elapsed / 1000UL);
            if (secsLeft < 1) secsLeft = 1;
            char c[2] = { (char)('0' + secsLeft), '\0' };
            SmallFont::drawString(display, 30, 30, c, COLOR_YELLOW);
            // Draw player ship during countdown.
            drawShip(display, (int)player.x, player.y, player.color, ((int32_t)(shieldUntilMs - now) > 0));
            return;
        }

        // Enemies
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (!enemies[i].alive) continue;
            drawEnemy(display, enemies[i]);
        }

        // Powerups
        for (int i = 0; i < MAX_POWERUPS; i++) {
            if (!powerups[i].active) continue;
            drawPowerup(display, (int)powerups[i].x, (int)powerups[i].y, powerups[i].type);
        }

        // Bullets
        for (int i = 0; i < MAX_PLAYER_BULLETS; i++) if (playerBullets[i].active) drawBullet(display, playerBullets[i], true);
        for (int i = 0; i < MAX_ENEMY_BULLETS; i++) if (enemyBullets[i].active) drawBullet(display, enemyBullets[i], false);

        // Player ship (shield shows blue outline)
        drawShip(display, (int)player.x, player.y, player.color, ((int32_t)(shieldUntilMs - (uint32_t)millis()) > 0));

        // Explosions overlay
        drawExplosions(display, now);
    }

    bool isGameOver() override {
        return gameOver;
    }

    // ------------------------------
    // Leaderboard integration
    // ------------------------------
    bool leaderboardEnabled() const override { return true; }
    const char* leaderboardId() const override { return "shooter"; }
    const char* leaderboardName() const override { return "Shooter"; }
    uint32_t leaderboardScore() const override { return (score > 0) ? (uint32_t)score : 0u; }
};

// ---------------------------------------------------------
// ShooterGame static data definitions (required for Arduino/ESP32 C++11)
// ---------------------------------------------------------
const uint8_t ShooterGame::SHIP_SPRITE[ShooterGame::SHIP_H][ShooterGame::SHIP_W] = {
    {0, 0, 1, 0, 0},
    {0, 1, 1, 1, 0},
    {1, 1, 1, 1, 1},
    {1, 0, 1, 0, 1},
    {0, 0, 1, 0, 0},
};

const uint8_t ShooterGame::ENEMY_SPRITES[4][ShooterGame::ENEMY_H][ShooterGame::ENEMY_W] = {
    // Type 0: "bug"
    {{0,1,1,0},
     {1,1,1,1},
     {1,0,0,1},
     {0,1,1,0}},
    // Type 1: "invader"
    {{1,0,0,1},
     {0,1,1,0},
     {1,1,1,1},
     {0,1,1,0}},
    // Type 2: "skull"
    {{1,1,1,1},
     {1,0,0,1},
     {1,1,1,1},
     {0,1,1,0}},
    // Type 3: "diamond"
    {{0,1,0,0},
     {1,1,1,0},
     {0,1,1,1},
     {0,0,1,0}},
};

const uint16_t ShooterGame::ENEMY_COLORS[4] = {
    COLOR_RED,      // bug
    COLOR_ORANGE,   // invader
    COLOR_PURPLE,   // skull
    COLOR_YELLOW    // diamond
};

