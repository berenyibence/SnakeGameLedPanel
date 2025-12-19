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
#include "ShooterGameConfig.h"

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
        static auto axisY(T* c, int) -> decltype(c->axisY(), int16_t()) { return (int16_t)c->axisY(); }
        template <typename T>
        static int16_t axisY(T*, ...) { return 0; }

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
    static constexpr int16_t AXIS_DIVISOR = ShooterGameConfig::AXIS_DIVISOR;      // ~[-512..512]
    static constexpr float STICK_DEADZONE = ShooterGameConfig::STICK_DEADZONE;    // 0..1
    static constexpr float MOVE_SMOOTH = ShooterGameConfig::MOVE_SMOOTH;          // 0..1
    static constexpr uint16_t TRIGGER_THRESHOLD = ShooterGameConfig::TRIGGER_THRESHOLD;

    // ---------------------------------------------------------
    // Visuals (sprites)
    // ---------------------------------------------------------
    // Player ship sprite: 5x5 (drawn pixel-art style, not a filled box)
    static constexpr int SHIP_W = ShooterGameConfig::SHIP_W;
    static constexpr int SHIP_H = ShooterGameConfig::SHIP_H;

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
    static constexpr uint16_t COUNTDOWN_MS = ShooterGameConfig::COUNTDOWN_MS;
    static constexpr uint16_t GAME_OVER_FREEZE_MS = ShooterGameConfig::GAME_OVER_FREEZE_MS;

    // ---------------------------------------------------------
    // Player
    // ---------------------------------------------------------
    struct Ship {
        float x;         // left position (float for smooth movement)
        float y;         // top position (float for smooth movement)
        float speed;     // px per tick at full stick
        uint16_t color;  // primary sprite color
        float vx;        // smoothed velocity
        float vy;        // smoothed velocity
        
        // Speed values in this project are effectively "px per tick" (tick ~= 16ms).
        // Keep the base movement intentionally slower for a more relaxed feel.
        Ship() : x(32.0f), y((float)(PANEL_RES_Y - 1 - SHIP_H)), speed(ShooterGameConfig::PLAYER_SPEED), color(COLOR_GREEN), vx(0.0f), vy(0.0f) {}
    };
    
    struct Bullet {
        float x;
        float y;
        float vx;      // px per tick
        float vy;      // px per tick
        bool active;
        uint16_t color; // base color for head
        uint8_t dmg;    // damage dealt on hit
    };

    struct PowerUp {
        float x;
        float y;
        uint8_t type;   // 0=shield(blue), 1=weapon(red), 2=life(green), 3=rockets(purple)
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

    // ---------------------------------------------------------
    // Bosses (spawned every 500 score, scale over time)
    // ---------------------------------------------------------
    struct Boss {
        bool active = false;
        float x = 0.0f;
        float y = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        float stopY = 0.0f;       // entrance target Y (boss stops here; does not approach further)
        uint8_t type = 0;         // 0..4
        uint8_t hp = 0;           // 0..10
        uint8_t maxHp = 0;
        uint8_t shieldTier = 0;   // 0..10 (absorbs hits before HP)
        uint32_t shieldFlashUntilMs = 0;
        uint32_t nextStarBurstMs = 0;
        uint32_t nextRocketMs = 0;
    };
    Boss boss = {};
    bool bossPending = false;         // threshold reached: stop spawning, wait for clear, then spawn boss
    uint8_t bossesDefeated = 0;       // difficulty scaling

    // Boss spawn pacing (requested):
    // hits_until_boss starts at 10 and increases by 1 every level after each boss kill.
    // Level 1 => 10, Level 2 => 11, Level 3 => 12, ...
    uint16_t hitsThisLevel = 0;
    uint16_t hitsUntilBoss = ShooterGameConfig::BOSS_HITS_BASE;

    // Boss death sequence (huge explosion + guaranteed loot)
    bool bossDeathActive = false;
    uint32_t bossDeathStartMs = 0;
    int bossDeathCx = 0;
    int bossDeathCy = 0;
    bool bossLootSpawned = false;
    bool bossDeathDamagedPlayer = false;

    // Player final-death sequence (big explosion before GAME OVER)
    bool playerDeathActive = false;
    uint32_t playerDeathStartMs = 0;
    int playerDeathCx = 0;
    int playerDeathCy = 0;

    // Boss projectiles
    struct StarShot {
        bool active = false;
        float x = 0.0f;
        float y = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        uint32_t startMs = 0;
    };
    struct Rocket {
        bool active = false;
        float x = 0.0f;
        float y = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        uint32_t startMs = 0;
        uint32_t nextTrailMs = 0;
    };

    static constexpr int MAX_STAR_SHOTS = ShooterGameConfig::MAX_STAR_SHOTS;
    static constexpr int MAX_ROCKETS = ShooterGameConfig::MAX_ROCKETS;
    StarShot starShots[MAX_STAR_SHOTS] = {};
    Rocket rockets[MAX_ROCKETS] = {};

    // Player guided rockets (from purple powerup): max 2.
    static constexpr int MAX_PLAYER_ROCKETS = ShooterGameConfig::MAX_PLAYER_ROCKETS;
    Rocket playerRockets[MAX_PLAYER_ROCKETS] = {};

    // ---------------------------------------------------------
    // Background clouds (2-layer parallax)
    // ---------------------------------------------------------
    struct Cloud {
        bool active = false;
        float x = 0.0f;
        float y = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        uint8_t sprite = 0; // 0..CLOUD_SPRITE_COUNT-1
    };
    static constexpr int CLOUD_LAYER0_COUNT = ShooterGameConfig::CLOUD_LAYER0_COUNT;
    static constexpr int CLOUD_LAYER1_COUNT = ShooterGameConfig::CLOUD_LAYER1_COUNT;
    Cloud cloudsFar[CLOUD_LAYER0_COUNT] = {};
    Cloud cloudsNear[CLOUD_LAYER1_COUNT] = {};
    
    Ship player;

    // Avoid heap churn: keep fixed-size pools for bullets and powerups.
    static constexpr int MAX_PLAYER_BULLETS = ShooterGameConfig::MAX_PLAYER_BULLETS;
    static constexpr int MAX_ENEMY_BULLETS  = ShooterGameConfig::MAX_ENEMY_BULLETS;
    static constexpr int MAX_ENEMIES        = ShooterGameConfig::MAX_ENEMIES;
    static constexpr int MAX_POWERUPS       = ShooterGameConfig::MAX_POWERUPS;

    Bullet playerBullets[MAX_PLAYER_BULLETS] = {};
    Bullet enemyBullets[MAX_ENEMY_BULLETS] = {};
    Enemy enemies[MAX_ENEMIES] = {};
    PowerUp powerups[MAX_POWERUPS] = {};

    bool gameOver;
    int score;
    int level;
    int lives;
    uint8_t rocketAmmo = 0;          // 0..2
    uint32_t lastRocketFireMs = 0;
    static constexpr uint16_t ROCKET_COOLDOWN_MS = ShooterGameConfig::PLAYER_ROCKET_COOLDOWN_MS;
    int kills; // for level progression
    unsigned long lastUpdate;
    unsigned long lastShot;
    static const int UPDATE_INTERVAL_MS = ShooterGameConfig::UPDATE_INTERVAL_MS;  // ~60 FPS
    static const int SHOT_COOLDOWN_MS = ShooterGameConfig::PLAYER_SHOT_COOLDOWN_MS;

    // Continuous spawning
    uint32_t lastSpawnMs = 0;
    static constexpr uint32_t SPAWN_INTERVAL_MS = ShooterGameConfig::ENEMY_SPAWN_INTERVAL_MS;
    uint32_t spawnPauseUntilMs = 0; // post-boss grace time to pick up loot

    // Powerup timers (ms)
    uint32_t shieldUntilMs = 0;
    uint32_t weaponUntilMs = 0;
    uint8_t shieldTier = 0;  // 0=none, 1..10
    uint8_t weaponTier = 0;  // 0=none, 1..5

    // Tier durations (seconds): 10,20,30,40,50
    static inline uint32_t tierDurationMs(uint8_t tier) {
        // Keep the original 10s-per-tier behavior, but allow up to 10 shield tiers.
        // (Weapon tiers are still clamped separately where applied.)
        const uint8_t t = (uint8_t)constrain(tier, 1, 10);
        return (uint32_t)t * 10000UL;
    }

    // Shield radius by tier: base 4, +0,+2,+4,+6,+10
    static inline uint8_t shieldRadiusForTier(uint8_t tier) {
        // Expanded to 10 tiers (requested). Grows slowly after tier 5.
        if (tier <= 1) return 4;
        if (tier == 2) return 6;
        if (tier == 3) return 8;
        if (tier == 4) return 10;
        if (tier == 5) return 12;
        if (tier == 6) return 13;
        if (tier == 7) return 14;
        if (tier == 8) return 15;
        if (tier == 9) return 16;
        return 17;
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
    static constexpr int MAX_EXPLOSIONS = ShooterGameConfig::MAX_EXPLOSIONS;
    Explosion explosions[MAX_EXPLOSIONS] = {};

    // ---------------------------------------------------------
    // Particle FX (Breakout-style sparkle bursts; cheap and fun)
    // ---------------------------------------------------------
    struct Particle {
        bool active;
        float x;
        float y;
        float vx;
        float vy;
        uint16_t color;
        uint32_t endMs;
    };
    static constexpr int MAX_PARTICLES = ShooterGameConfig::MAX_PARTICLES;
    Particle particles[MAX_PARTICLES] = {};

    void clearParticles() {
        for (int i = 0; i < MAX_PARTICLES; i++) particles[i].active = false;
    }

    void spawnParticles(float x, float y, uint16_t color, uint8_t count, uint32_t now) {
        // Keep it modest: looks good on 64×64 without being too heavy.
        const uint8_t tuned = max<uint8_t>(1, (uint8_t)(count / 2));
        for (uint8_t n = 0; n < tuned; n++) {
            int slot = -1;
            for (int i = 0; i < MAX_PARTICLES; i++) {
                if (!particles[i].active) { slot = i; break; }
            }
            if (slot < 0) return;
            Particle& p = particles[slot];
            p.active = true;
            p.x = x;
            p.y = y;
            // Strong sideways variety, mild upward kick (looks like debris).
            p.vx = ((float)random(-100, 101) / 100.0f) * 0.75f;
            p.vy = ((float)random(-90, 41) / 100.0f) * 0.65f;
            p.color = color;
            p.endMs = now + (uint32_t)random(240, 560);
        }
    }

    void updateParticles(uint32_t now) {
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (!particles[i].active) continue;
            if ((int32_t)(particles[i].endMs - now) <= 0) {
                particles[i].active = false;
                continue;
            }
            particles[i].x += particles[i].vx;
            particles[i].y += particles[i].vy;
            particles[i].vx *= 0.97f;
            particles[i].vy *= 0.97f;
            particles[i].vy += 0.018f; // mild gravity
        }
    }

    void drawParticles(MatrixPanel_I2S_DMA* display, uint32_t now) {
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (!particles[i].active) continue;
            const uint32_t age = (uint32_t)(now - (particles[i].endMs - 560)); // rough age baseline
            (void)age;
            const int x = (int)particles[i].x;
            const int y = (int)particles[i].y;
            if (x < 0 || x >= PANEL_RES_X || y < 0 || y >= PANEL_RES_Y) continue;
            display->drawPixel(x, y, particles[i].color);
        }
    }

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
    // Sprite data lives in ShooterGameConfig.h so it can be tweaked easily.
    // Keep these references (instead of duplicating arrays in this class) to
    // avoid accidental divergence between visuals and tuning.

    // 4 normal enemy types, each 5x5 (1 = draw).
    // Ship-like sprites (facing down) for better readability on 64×64.
    static constexpr uint8_t ENEMY_W = ShooterGameConfig::ENEMY_W;
    static constexpr uint8_t ENEMY_H = ShooterGameConfig::ENEMY_H;
    // See ShooterGameConfig::ENEMY_SPRITES / ENEMY_COLORS

    // Bosses: 5 types, each 10x10 (pixel-art).
    static constexpr uint8_t BOSS_W = ShooterGameConfig::BOSS_W;
    static constexpr uint8_t BOSS_H = ShooterGameConfig::BOSS_H;
    // See ShooterGameConfig::BOSS_SPRITES / BOSS_COLORS

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

    void clearBossProjectiles() {
        for (int i = 0; i < MAX_STAR_SHOTS; i++) starShots[i].active = false;
        for (int i = 0; i < MAX_ROCKETS; i++) rockets[i].active = false;
        for (int i = 0; i < MAX_PLAYER_ROCKETS; i++) playerRockets[i].active = false;
    }

    void startBossDeath(uint32_t now) {
        bossDeathActive = true;
        bossDeathStartMs = now;
        bossLootSpawned = false;
        bossDeathDamagedPlayer = false;
        bossDeathCx = (int)boss.x + (int)(BOSS_W / 2);
        bossDeathCy = (int)boss.y + (int)(BOSS_H / 2);
        clearBossProjectiles(); // fairness: clear boss bullets/rockets

        // Pause enemy spawning for the whole death sequence + extra loot grace.
        // (2s explosion + 1s pickup time)
        const uint32_t pauseUntil = now + (uint32_t)ShooterGameConfig::BOSS_DEATH_EXPLOSION_MS + (uint32_t)ShooterGameConfig::BOSS_LOOT_GRACE_MS;
        if ((int32_t)(pauseUntil - spawnPauseUntilMs) > 0) spawnPauseUntilMs = pauseUntil;
        lastSpawnMs = now; // avoid "instant spawn" the frame the pause ends
    }

    void drawBossDeathExplosion(MatrixPanel_I2S_DMA* display, uint32_t now) {
        if (!bossDeathActive) return;
        const uint32_t age = (uint32_t)(now - bossDeathStartMs);
        static constexpr uint32_t LIFE_MS = ShooterGameConfig::BOSS_DEATH_EXPLOSION_MS;
        if (age >= LIFE_MS) return;

        // Big expanding rings + flickering core.
        const float t = (float)age / (float)LIFE_MS; // 0..1
        const int cx = bossDeathCx;
        const int cy = bossDeathCy;

        const int r1 = 2 + (int)(t * 18.0f);
        const int r2 = 1 + (int)(t * 12.0f);
        const int r3 = 1 + (int)(t * 7.0f);

        const bool flicker = ((now / 80) % 2) == 0;
        const uint16_t core = flicker ? COLOR_WHITE : COLOR_YELLOW;
        display->drawCircle(cx, cy, r1, COLOR_ORANGE);
        display->drawCircle(cx, cy, r2, COLOR_RED);
        display->drawCircle(cx, cy, r3, COLOR_YELLOW);
        display->drawPixel(cx, cy, core);
        display->drawPixel(cx + 1, cy, core);
        display->drawPixel(cx - 1, cy, core);
        display->drawPixel(cx, cy + 1, core);
        display->drawPixel(cx, cy - 1, core);

        // Periodic spark bursts
        if ((age % 180) < 16) {
            spawnParticles((float)cx, (float)cy, COLOR_ORANGE, 24, now);
        }
    }

    void spawnPowerupForced(uint8_t type, float x, float y, float kickVx, float kickVy) {
        // Direct spawn of a powerup pack (used for boss loot).
        for (int i = 0; i < MAX_POWERUPS; i++) {
            if (powerups[i].active) continue;
            powerups[i].active = true;
            powerups[i].type = type;
            powerups[i].x = x;
            powerups[i].y = y;
            powerups[i].vx = kickVx;
            powerups[i].vy = kickVy;
            powerups[i].tier = 0;
            return;
        }
    }

    void updateBossDeath(uint32_t now) {
        if (!bossDeathActive) return;
        static constexpr uint32_t LIFE_MS = ShooterGameConfig::BOSS_DEATH_EXPLOSION_MS;
        const uint32_t age = (uint32_t)(now - bossDeathStartMs);
        if (age < LIFE_MS) return;

        if (!bossLootSpawned) {
            bossLootSpawned = true;
            // Spawn one of each loot (blue/red/green) shooting out in different directions.
            // type: 0=shield(blue), 1=weapon(red), 2=life(green), 3=rockets(purple)
            const float x = (float)bossDeathCx;
            const float y = (float)bossDeathCy;
            spawnPowerupForced(0, x, y, -0.75f, -0.10f);
            spawnPowerupForced(1, x, y,  0.00f, -0.14f);
            spawnPowerupForced(2, x, y,  0.75f, -0.10f);
            spawnPowerupForced(3, x, y,  0.00f, -0.06f);
        }

        // End the death sequence once loot is spawned.
        bossDeathActive = false;
    }

    void startPlayerDeath(uint32_t now) {
        playerDeathActive = true;
        playerDeathStartMs = now;
        playerDeathCx = (int)player.x + (int)(SHIP_W / 2);
        playerDeathCy = (int)player.y + (int)(SHIP_H / 2);
        // Clear bullets for readability during the final explosion.
        clearBullets();
        clearBossProjectiles();
        spawnExplosion(playerDeathCx, playerDeathCy, COLOR_ORANGE, now);
        spawnParticles((float)playerDeathCx, (float)playerDeathCy, COLOR_YELLOW, 26, now);

        // AoE blast (requested): enemies inside the explosion area take 5 damage.
        // If they die, add their points to the score before GAME OVER.
        const int r = (int)ShooterGameConfig::PLAYER_DEATH_AOE_RADIUS_PX;
        const int r2 = r * r;
        for (int ei = 0; ei < MAX_ENEMIES; ei++) {
            Enemy& e = enemies[ei];
            if (!e.alive) continue;

            const int ex = (int)e.x + (int)(ENEMY_W / 2);
            const int ey = (int)e.y + (int)(ENEMY_H / 2);
            const int dx = ex - playerDeathCx;
            const int dy = ey - playerDeathCy;
            if ((dx * dx + dy * dy) > r2) continue;

            const uint8_t dmg = ShooterGameConfig::PLAYER_DEATH_AOE_DAMAGE;
            if (e.hp > dmg) {
                e.hp = (uint8_t)(e.hp - dmg);
            } else {
                e.hp = 0;
                e.alive = false;
                kills++;
                score += 10 + (e.type * 5);
                spawnExplosion(ex, ey, ShooterGameConfig::ENEMY_COLORS[e.type & 3], now);
                spawnParticles((float)ex, (float)ey, ShooterGameConfig::ENEMY_COLORS[e.type & 3], 14, now);
            }
        }
    }

    void drawPlayerDeathExplosion(MatrixPanel_I2S_DMA* display, uint32_t now) {
        if (!playerDeathActive) return;
        const uint32_t age = (uint32_t)(now - playerDeathStartMs);
        static constexpr uint32_t LIFE_MS = ShooterGameConfig::PLAYER_DEATH_EXPLOSION_MS;
        if (age >= LIFE_MS) return;

        // Similar vibe to boss explosion, just longer (3 seconds).
        const float t = (float)age / (float)LIFE_MS; // 0..1
        const int cx = playerDeathCx;
        const int cy = playerDeathCy;

        const int r1 = 2 + (int)(t * 20.0f);
        const int r2 = 1 + (int)(t * 13.0f);
        const int r3 = 1 + (int)(t * 8.0f);

        const bool flicker = ((now / 70) % 2) == 0;
        const uint16_t core = flicker ? COLOR_WHITE : COLOR_YELLOW;
        display->drawCircle(cx, cy, r1, COLOR_ORANGE);
        display->drawCircle(cx, cy, r2, COLOR_RED);
        display->drawCircle(cx, cy, r3, COLOR_YELLOW);
        display->drawPixel(cx, cy, core);
        display->drawPixel(cx + 1, cy, core);
        display->drawPixel(cx - 1, cy, core);
        display->drawPixel(cx, cy + 1, core);
        display->drawPixel(cx, cy - 1, core);

        // Periodic spark bursts
        if ((age % 160) < 16) {
            spawnParticles((float)cx, (float)cy, COLOR_ORANGE, 18, now);
        }
    }

    void maybeApplyBossDeathExplosionDamage(uint32_t now) {
        if (!bossDeathActive) return;
        if (bossDeathDamagedPlayer) return;
        const uint32_t age = (uint32_t)(now - bossDeathStartMs);
        if (age >= (uint32_t)ShooterGameConfig::BOSS_DEATH_EXPLOSION_MS) return;

        // Use the same expanding radius as drawBossDeathExplosion().
        const float t = (float)age / (float)ShooterGameConfig::BOSS_DEATH_EXPLOSION_MS;
        const int r1 = 2 + (int)(t * 18.0f);
        const int pcx = (int)player.x + (int)(SHIP_W / 2);
        const int pcy = (int)player.y + (int)(SHIP_H / 2);
        const int dx = pcx - bossDeathCx;
        const int dy = pcy - bossDeathCy;
        if ((dx * dx + dy * dy) <= (r1 * r1)) {
            bossDeathDamagedPlayer = true;
            // Requested: entering the boss death explosion radius costs 2 lives.
            lives -= 2;
            if (lives <= 0) {
                lives = 0;
                startPlayerDeath(now);
                phase = PHASE_GAME_OVER_DELAY;
                phaseStartMs = now;
            } else {
                invulnUntilMs = now + 900;
                spawnExplosion(pcx, pcy, COLOR_ORANGE, now);
            }
        }
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

        // Spawn behavior (requested):
        // Enemies should not "pop" into existence in the playfield. They enter from the top
        // (like bosses), drifting down into view.
        const float x = (float)random(2, PANEL_RES_X - (int)ENEMY_W - 2);
        const float y = -(float)ENEMY_H - (float)random(0, 12);

        const int type = random(0, 4);
        // Movement tuning:
        // - Mostly linear downward movement
        // - Slight side-to-side drift (2x vs previous)
        const float drift = ((float)random(-10, 11) / 100.0f) * 0.4f; // ~-0.16..0.16
        const float vx = drift;
        // Advance faster (3x vs previous)
        const float vy = 4.0f * (0.05f + 0.004f * (float)min(12, max(0, level - 1)));

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

    void spawnBoss(uint32_t now) {
        boss.active = true;
        boss.type = (uint8_t)random(0, 5);
        boss.x = (float)(PANEL_RES_X / 2 - (int)BOSS_W / 2);
        // Enter from above the screen.
        boss.y = -(float)BOSS_H;
        boss.vx = (random(0, 2) == 0) ? -0.22f : 0.22f;
        // Comes downward towards the player, but stops in the top half to combat.
        boss.vy = 0.32f + 0.01f * (float)min<uint8_t>(12, bossesDefeated);
        boss.stopY = (float)(HUD_H + ShooterGameConfig::BOSS_STOP_Y_OFFSET); // top half stop point

        // HP: 5..10 base, scales a bit with bossesDefeated.
        const uint8_t baseHp = (uint8_t)random(5, 11);
        const uint8_t bonusHp = (uint8_t)min(3, (int)(bossesDefeated / 2));
        boss.maxHp = (uint8_t)min(10, (int)baseHp + (int)bonusHp);
        boss.hp = boss.maxHp;

        // Shield tiers: 1..10 (scales with bossesDefeated).
        const uint8_t baseShield = (uint8_t)min(10, 3 + (int)bossesDefeated);
        boss.shieldTier = (uint8_t)constrain(baseShield, 1, 10);
        boss.shieldFlashUntilMs = 0;

        // Attack cadence (gets faster over time).
        const uint32_t starBase = ShooterGameConfig::BOSS_STAR_BASE_MS;
        const uint32_t rocketBase = ShooterGameConfig::BOSS_ROCKET_BASE_MS;
        const uint32_t dec = (uint32_t)min((int)ShooterGameConfig::BOSS_ATTACK_DEC_MAX, (int)bossesDefeated * (int)ShooterGameConfig::BOSS_ATTACK_DEC_PER_BOSS);
        boss.nextStarBurstMs = now + (starBase - dec) + (uint32_t)random(0, 500);
        boss.nextRocketMs = now + (rocketBase - dec) + (uint32_t)random(0, 800);
    }

    void updateBossFlow(uint32_t now) {
        // hits_until_boss model:
        // - once we've accumulated enough total damage hits against normal enemies,
        //   stop spawning and wait for the screen to clear, then spawn a boss.
        if (!bossPending && !boss.active && !bossDeathActive && hitsThisLevel >= hitsUntilBoss) {
            bossPending = true;
        }
        if (bossPending && !boss.active && !bossDeathActive) {
            if (aliveEnemyCount() == 0) {
                spawnBoss(now);
            }
        }
    }

    void balanceBossDifficulty() {
        // Quick readability/fairness clamps for 64×64:
        // - Keep boss vertical band limited
        // - Keep projectile pools from saturating too hard
        // (Most tuning is already inside spawnBoss/updateBoss.)
        if (bossesDefeated > 20) bossesDefeated = 20;
    }

    void resetPlayerAndBullets() {
        player.x = PANEL_RES_X / 2.0f - (float)SHIP_W / 2.0f;
        player.vx = 0.0f;
        player.y = (float)(PANEL_RES_Y - 1 - SHIP_H);
        player.vy = 0.0f;
        clearBullets();
        lastShot = 0;
    }

    void loseLife(uint32_t now) {
        lives--;
        if (lives <= 0) {
            lives = 0;
            // Final death: play a big explosion animation, then show game-over.
            startPlayerDeath(now);
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
    // Powerup pack physics (ported from Breakout tuning):
    // - More sideways launch variety, slower gravity
    // - Bounce off walls so packs don't exit the screen
    static constexpr float POWERUP_GRAVITY = ShooterGameConfig::POWERUP_GRAVITY;
    static constexpr float POWERUP_DRAG = ShooterGameConfig::POWERUP_DRAG;
    static constexpr float POWERUP_BOUNCE = ShooterGameConfig::POWERUP_BOUNCE;
    static constexpr int POWERUP_SIZE_PX = ShooterGameConfig::POWERUP_SIZE_PX; // drawn as 2x2 box

    void spawnPlayerBullet(int x, int y) {
        for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
            if (playerBullets[i].active) continue;
            playerBullets[i].active = true;
            playerBullets[i].x = (float)x;
            playerBullets[i].y = (float)y;
            playerBullets[i].vx = 0.0f;
            playerBullets[i].vy = -ShooterGameConfig::PLAYER_BULLET_SPEED;
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
            enemyBullets[i].x = (float)x;
            enemyBullets[i].y = (float)y;
            // Aim at the player *at fire time* (straight shot, no trajectory changes after spawn).
            const float tx = player.x + (float)SHIP_W * 0.5f;
            const float ty = player.y + (float)SHIP_H * 0.5f;
            const float dx = tx - enemyBullets[i].x;
            const float dy = ty - enemyBullets[i].y;
            const float len = sqrtf(dx * dx + dy * dy);
            const float inv = (len > 0.001f) ? (1.0f / len) : 0.0f;
            const float s = ShooterGameConfig::ENEMY_BULLET_SPEED; // requested: 2x slower
            enemyBullets[i].vx = dx * inv * s;
            enemyBullets[i].vy = dy * inv * s;
            enemyBullets[i].color = ShooterGameConfig::ENEMY_COLORS[type % 4];
            enemyBullets[i].dmg = 1;
            return;
        }
    }

    // ---------------------------------------------------------
    // Boss projectiles
    // ---------------------------------------------------------
    void spawnStarShot(float x, float y, float vx, float vy, uint32_t now) {
        for (int i = 0; i < MAX_STAR_SHOTS; i++) {
            if (starShots[i].active) continue;
            starShots[i].active = true;
            starShots[i].x = x;
            starShots[i].y = y;
            starShots[i].vx = vx;
            starShots[i].vy = vy;
            starShots[i].startMs = now;
            return;
        }
    }

    void spawnRocket(float x, float y, float vx, float vy, uint32_t now) {
        for (int i = 0; i < MAX_ROCKETS; i++) {
            if (rockets[i].active) continue;
            rockets[i].active = true;
            rockets[i].x = x;
            rockets[i].y = y;
            rockets[i].vx = vx;
            rockets[i].vy = vy;
            rockets[i].startMs = now;
            rockets[i].nextTrailMs = now;
            return;
        }
    }

    void spawnPlayerRocket(float x, float y, uint32_t now) {
        for (int i = 0; i < MAX_PLAYER_ROCKETS; i++) {
            if (playerRockets[i].active) continue;
            playerRockets[i].active = true;
            playerRockets[i].x = x;
            playerRockets[i].y = y;
            playerRockets[i].vx = 0.0f;
            playerRockets[i].vy = -0.40f; // starts going up, then homes
            playerRockets[i].startMs = now;
            playerRockets[i].nextTrailMs = now;
            return;
        }
    }

    void bossFireStarBurst(uint32_t now) {
        // Very slow red spinning stars in (approx) all directions.
        // Use 12 directions (no trig).
        static const int8_t DIRS[12][2] = {
            { 2, 0}, { 2, 1}, { 1, 2}, { 0, 2}, {-1, 2}, {-2, 1},
            {-2, 0}, {-2,-1}, {-1,-2}, { 0,-2}, { 1,-2}, { 2,-1},
        };
        const float s = 0.22f; // slow
        const float cx = boss.x + (float)(BOSS_W / 2);
        const float cy = boss.y + (float)(BOSS_H - 1);
        for (int i = 0; i < 12; i++) {
            spawnStarShot(cx, cy, (float)DIRS[i][0] * s, (float)DIRS[i][1] * s, now);
        }
        // Little pop at the boss muzzle.
        spawnParticles(cx, cy, COLOR_RED, 10, now);
    }

    void bossFireRocket(uint32_t now) {
        const float cx = boss.x + (float)(BOSS_W / 2);
        const float cy = boss.y + (float)(BOSS_H - 1);
        // Slow initial rocket, then it will steer toward the player.
        spawnRocket(cx, cy, 0.0f, 0.25f, now);
        spawnParticles(cx, cy, COLOR_ORANGE, 8, now);
    }

    void updateBoss(uint32_t now) {
        if (!boss.active) return;

        // Movement: bounce within horizontal bounds and a vertical band.
        boss.x += boss.vx;
        // Entrance: move down until stopY, then stop vertical motion (does not approach).
        if (boss.y < boss.stopY) boss.y += boss.vy;
        else { boss.y = boss.stopY; boss.vy = 0.0f; }
        if (boss.x < 1) { boss.x = 1; boss.vx = fabsf(boss.vx); }
        if (boss.x > (float)(PANEL_RES_X - BOSS_W - 1)) { boss.x = (float)(PANEL_RES_X - BOSS_W - 1); boss.vx = -fabsf(boss.vx); }

        // Attacks
        if (now >= boss.nextStarBurstMs) {
            bossFireStarBurst(now);
            const uint32_t base = 2500;
            const uint32_t dec = (uint32_t)min(1500, (int)bossesDefeated * 120);
            boss.nextStarBurstMs = now + (base - dec) + (uint32_t)random(0, 650);
        }
        if (now >= boss.nextRocketMs) {
            // First 5 bosses (bossesDefeated 0..4) do NOT fire rockets.
        if (bossesDefeated >= ShooterGameConfig::BOSSES_WITHOUT_ROCKETS) bossFireRocket(now);
            const uint32_t base = 3600;
            const uint32_t dec = (uint32_t)min(2200, (int)bossesDefeated * 160);
            boss.nextRocketMs = now + (base - dec) + (uint32_t)random(0, 900);
        }
    }

    void updateBossProjectiles(uint32_t now) {
        // Stars
        for (int i = 0; i < MAX_STAR_SHOTS; i++) {
            if (!starShots[i].active) continue;
            starShots[i].x += starShots[i].vx;
            starShots[i].y += starShots[i].vy;
            if (starShots[i].x < -2 || starShots[i].x > PANEL_RES_X + 2 ||
                starShots[i].y < HUD_H - 2 || starShots[i].y > PANEL_RES_Y + 2) {
                starShots[i].active = false;
            }
        }

        // Rockets (homing)
        const float tx = player.x + 2.0f;
        const float ty = (float)((int)player.y + 2);
        for (int i = 0; i < MAX_ROCKETS; i++) {
            if (!rockets[i].active) continue;

            // Lifespan (requested): prevent unavoidable forever-rockets.
            if ((uint32_t)(now - rockets[i].startMs) >= (uint32_t)ShooterGameConfig::ENEMY_ROCKET_LIFE_MS) {
                rockets[i].active = false;
                continue;
            }

            // Desired direction toward player
            float dx = tx - rockets[i].x;
            float dy = ty - rockets[i].y;
            const float d = sqrtf(dx * dx + dy * dy);
            if (d > 0.001f) { dx /= d; dy /= d; }
            const float desiredVx = dx * 0.55f;
            const float desiredVy = dy * 0.55f;

            // Steer (slow but persistent)
            const float steer = 0.08f;
            rockets[i].vx = rockets[i].vx * (1.0f - steer) + desiredVx * steer;
            rockets[i].vy = rockets[i].vy * (1.0f - steer) + desiredVy * steer;

            rockets[i].x += rockets[i].vx;
            rockets[i].y += rockets[i].vy;

            // Trail/fire particles
            if (now >= rockets[i].nextTrailMs) {
                rockets[i].nextTrailMs = now + 70;
                spawnParticles(rockets[i].x, rockets[i].y, COLOR_ORANGE, 6, now);
            }

            if (rockets[i].x < -3 || rockets[i].x > PANEL_RES_X + 3 ||
                rockets[i].y < HUD_H - 3 || rockets[i].y > PANEL_RES_Y + 3) {
                rockets[i].active = false;
            }
        }
    }

    void updatePlayerRockets(uint32_t now) {
        // Target: boss if active, else nearest enemy, else keep going up.
        for (int i = 0; i < MAX_PLAYER_ROCKETS; i++) {
            if (!playerRockets[i].active) continue;

            float tx = playerRockets[i].x;
            float ty = playerRockets[i].y - 10.0f;
            bool hasTarget = false;

            if (boss.active) {
                tx = boss.x + (float)(BOSS_W / 2);
                ty = boss.y + (float)(BOSS_H / 2);
                hasTarget = true;
            } else {
                float bestD2 = 1e9f;
                for (int ei = 0; ei < MAX_ENEMIES; ei++) {
                    if (!enemies[ei].alive) continue;
                    const float ex = enemies[ei].x + (float)(ENEMY_W / 2);
                    const float ey = enemies[ei].y + (float)(ENEMY_H / 2);
                    const float dx = ex - playerRockets[i].x;
                    const float dy = ey - playerRockets[i].y;
                    const float d2 = dx * dx + dy * dy;
                    if (d2 < bestD2) {
                        bestD2 = d2;
                        tx = ex;
                        ty = ey;
                        hasTarget = true;
                    }
                }
            }

            if (hasTarget) {
                float dx = tx - playerRockets[i].x;
                float dy = ty - playerRockets[i].y;
                const float d = sqrtf(dx * dx + dy * dy);
                if (d > 0.001f) { dx /= d; dy /= d; }

                const float desiredVx = dx * 0.70f;
                const float desiredVy = dy * 0.70f;
                const float steer = 0.12f;
                playerRockets[i].vx = playerRockets[i].vx * (1.0f - steer) + desiredVx * steer;
                playerRockets[i].vy = playerRockets[i].vy * (1.0f - steer) + desiredVy * steer;
            } else {
                // No target: keep going upward slowly.
                playerRockets[i].vx *= 0.98f;
                playerRockets[i].vy = min(playerRockets[i].vy, -0.35f);
            }

            playerRockets[i].x += playerRockets[i].vx;
            playerRockets[i].y += playerRockets[i].vy;

            // Trail
            if (now >= playerRockets[i].nextTrailMs) {
                playerRockets[i].nextTrailMs = now + 70;
                spawnParticles(playerRockets[i].x, playerRockets[i].y, COLOR_PURPLE, 8, now);
            }

            if (playerRockets[i].x < -4 || playerRockets[i].x > PANEL_RES_X + 4 ||
                playerRockets[i].y < -6 || playerRockets[i].y > PANEL_RES_Y + 6) {
                playerRockets[i].active = false;
            }
        }
    }

    void maybeDropPowerup(float x, float y, float kickVx, float kickVy) {
        // Keep it occasional.
        const int dropChance = ShooterGameConfig::POWERUP_DROP_CHANCE_PERCENT; // % (tunable)
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
        if (lives < 5 && r < 28) t = 2;          // green life
        else if (r < 52) t = 0;                 // blue shield
        else if (r < 82) t = 1;                 // red weapon
        else t = 3;                              // purple rockets

        powerups[slot].active = true;
        powerups[slot].x = x;
        powerups[slot].y = y;
        powerups[slot].type = t;
        // Launch away from explosion so it's harder to catch (strong sideways variety),
        // but keep overall fall speed floaty/slower.
        powerups[slot].vx = kickVx + ((float)random(-80, 81) / 100.0f) * 0.28f;
        powerups[slot].vy = kickVy + ((float)random(-20, 41) / 100.0f) * 0.08f;
        powerups[slot].tier = 0;
    }

    void applyPowerup(uint8_t type, uint32_t now) {
        if (type == 0) { // Blue Pack (shield tier)
            if ((int32_t)(shieldUntilMs - now) > 0) shieldTier = min<uint8_t>(10, (uint8_t)(shieldTier + 1));
            else shieldTier = 1;
            shieldUntilMs = now + tierDurationMs(shieldTier);
        } else if (type == 1) { // Red Pack (weapon tier)
            if ((int32_t)(weaponUntilMs - now) > 0) weaponTier = min<uint8_t>(5, weaponTier + 1);
            else weaponTier = 1;
            // Weapon tiers remain 1..5 even though tierDurationMs() supports shield tiers up to 10.
            weaponUntilMs = now + tierDurationMs((uint8_t)min<uint8_t>(5, weaponTier));
        } else if (type == 2) { // Green pack: +life (no tiers)
            lives = min((int)ShooterGameConfig::PLAYER_MAX_LIVES, lives + 1);
        } else { // Purple pack: +rocket ammo (max 2)
            rocketAmmo = min<uint8_t>(ShooterGameConfig::PLAYER_MAX_ROCKET_AMMO, (uint8_t)(rocketAmmo + 1));
        }
    }

    void updateBulletsAndPowerups(uint32_t now) {
        // Player bullets
        for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
            if (!playerBullets[i].active) continue;
            playerBullets[i].x += playerBullets[i].vx;
            playerBullets[i].y += playerBullets[i].vy;
            if (playerBullets[i].y < (float)HUD_H || playerBullets[i].y > (float)(PANEL_RES_Y + 2) ||
                playerBullets[i].x < -2.0f || playerBullets[i].x > (float)(PANEL_RES_X + 2)) {
                playerBullets[i].active = false;
            }
        }

        // Enemy bullets
        for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
            if (!enemyBullets[i].active) continue;
            enemyBullets[i].x += enemyBullets[i].vx;
            enemyBullets[i].y += enemyBullets[i].vy;
            if (enemyBullets[i].y < -2.0f || enemyBullets[i].y > (float)(PANEL_RES_Y + 2) ||
                enemyBullets[i].x < -2.0f || enemyBullets[i].x > (float)(PANEL_RES_X + 2)) {
                enemyBullets[i].active = false;
            }
        }

        // Powerups
        for (int i = 0; i < MAX_POWERUPS; i++) {
            if (!powerups[i].active) continue;
            // Ballistic motion (kick + gravity + drag) + wall bounces.
            powerups[i].x += powerups[i].vx;
            powerups[i].y += powerups[i].vy;
            powerups[i].vy += POWERUP_GRAVITY;
            powerups[i].vx *= POWERUP_DRAG;
            powerups[i].vy *= POWERUP_DRAG;

            // Bounce off left/right bounds so packs stay on-screen.
            const float minX = 0.0f;
            const float maxX = (float)(PANEL_RES_X - POWERUP_SIZE_PX);
            if (powerups[i].x < minX) {
                powerups[i].x = minX;
                powerups[i].vx = fabsf(powerups[i].vx) * POWERUP_BOUNCE;
            } else if (powerups[i].x > maxX) {
                powerups[i].x = maxX;
                powerups[i].vx = -fabsf(powerups[i].vx) * POWERUP_BOUNCE;
            }

            // Bounce out of the HUD band if launched upward.
            const float minY = (float)(HUD_H + 1);
            if (powerups[i].y < minY) {
                powerups[i].y = minY;
                powerups[i].vy = fabsf(powerups[i].vy) * POWERUP_BOUNCE;
            }

            // Safety clamp: keep extremes in check.
            powerups[i].vx = clampf(powerups[i].vx, -0.85f, 0.85f);
            powerups[i].vy = clampf(powerups[i].vy, -0.85f, 0.85f);

            // Catch by player ship bounds
            const int px = (int)player.x;
            const int py = (int)player.y;
            if (powerups[i].y >= (float)py - 1 &&
                powerups[i].x >= (float)(px - 1) &&
                powerups[i].x <= (float)(px + SHIP_W)) {
                // Pickup sparkle (colored by pack).
                const uint16_t c =
                    (powerups[i].type == 0) ? COLOR_BLUE :
                    (powerups[i].type == 1) ? COLOR_RED :
                    (powerups[i].type == 2) ? COLOR_GREEN :
                    COLOR_PURPLE;
                spawnParticles(powerups[i].x + 1.0f, powerups[i].y + 1.0f, c, 10, now);
                applyPowerup(powerups[i].type, now);
                powerups[i].active = false;
                continue;
            }

            if (powerups[i].y > PANEL_RES_Y) powerups[i].active = false;
        }
    }

    void updateEnemiesAndEnemyFire(uint32_t now) {
        // Allow enemies to enter from above the screen (no snap to HUD line).
        const float minY = -(float)ENEMY_H - 2.0f;
        for (int i = 0; i < MAX_ENEMIES; i++) {
            Enemy& e = enemies[i];
            if (!e.alive) continue;

            // Organic drift
            e.x += e.vx;
            e.y += e.vy;

            // Soft bounce on edges (rare now since vx is small, but keeps them on-screen).
            if (e.x < 1) { e.x = 1; e.vx = fabsf(e.vx); }
            if (e.x > (float)(PANEL_RES_X - ENEMY_W - 1)) { e.x = (float)(PANEL_RES_X - ENEMY_W - 1); e.vx = -fabsf(e.vx); }

            // Keep within entrance band.
            if (e.y < minY) e.y = minY;
            // Enemies can fly past the player and exit the screen downward.
            if (e.y > (float)(PANEL_RES_Y + ENEMY_H + 2)) {
                e.alive = false;
                continue;
            }

            // Enemy firing: tuned to be ~2x more frequent.
            if (now >= e.nextShotMs) {
                const uint32_t baseInterval = ShooterGameConfig::ENEMY_FIRE_BASE_MS;
                const uint32_t dec = (uint32_t)min((int)(baseInterval - 1), (int)(max(0, level - 1) * (int)ShooterGameConfig::ENEMY_FIRE_DEC_PER_LEVEL));
                const uint32_t interval = max((uint32_t)ShooterGameConfig::ENEMY_FIRE_MIN_MS, (baseInterval - dec) / (uint32_t)ShooterGameConfig::ENEMY_FIRE_RATE_DIVIDER);
                e.nextShotMs = now + interval + (uint32_t)random(0, ShooterGameConfig::ENEMY_FIRE_JITTER_MS);

                const float p = min(ShooterGameConfig::ENEMY_FIRE_P_MAX, ShooterGameConfig::ENEMY_FIRE_P_BASE + ShooterGameConfig::ENEMY_FIRE_P_PER_LEVEL * (float)max(0, level - 1));
                const int roll = random(0, 1000);
                if ((float)roll < p * 1000.0f) {
                    const int bx = (int)e.x + (int)(ENEMY_W / 2);
                    const int by = (int)e.y + (int)ENEMY_H;
                    spawnEnemyBullet(bx, by, (uint8_t)e.type);
                }
            }
        }
    }

    void handleCollisions(uint32_t now) {
        // ---------------------------------------------------------
        // Player guided rockets vs enemies/boss
        // ---------------------------------------------------------
        for (int ri = 0; ri < MAX_PLAYER_ROCKETS; ri++) {
            if (!playerRockets[ri].active) continue;

            const int rx = (int)playerRockets[ri].x;
            const int ry = (int)playerRockets[ri].y;

            // Boss hit
            if (boss.active && rectContains(rx, ry, (int)boss.x, (int)boss.y, BOSS_W, BOSS_H)) {
                playerRockets[ri].active = false;
                spawnExplosion(rx, ry, COLOR_WHITE, now);
                spawnParticles((float)rx, (float)ry, COLOR_PURPLE, 18, now);

                if (boss.shieldTier > 0) {
                    const uint8_t d = min<uint8_t>(2, boss.shieldTier);
                    boss.shieldTier = (uint8_t)(boss.shieldTier - d);
                    boss.shieldFlashUntilMs = now + 220;
                } else {
                    const uint8_t dmg = 3;
                    if (boss.hp > dmg) boss.hp = (uint8_t)(boss.hp - dmg);
                    else boss.hp = 0;
                    boss.shieldFlashUntilMs = now + 220;
                }

                if (boss.hp == 0) {
                    score += 250 + (int)bossesDefeated * 25;
                    bossesDefeated++;
                    level++;                 // boss kill => next level (requested)
                    hitsThisLevel = 0;       // reset hits for the new level
                    hitsUntilBoss = (uint16_t)(9 + max(1, level)); // level1=10, level2=11, ...
                    boss.active = false;
                    bossPending = false;
                    startBossDeath(now);
                }
                continue;
            }

            // Enemy hit
            for (int ei = 0; ei < MAX_ENEMIES; ei++) {
                Enemy& e = enemies[ei];
                if (!e.alive) continue;
                if (!rectContains(rx, ry, (int)e.x, (int)e.y, ENEMY_W, ENEMY_H)) continue;

                playerRockets[ri].active = false;
                // Count full HP as "hits" (damage units) for hits_until_boss.
                const uint8_t hpBefore = e.hp;
                e.alive = false;
                hitsThisLevel = (uint16_t)min<uint32_t>(65535u, (uint32_t)hitsThisLevel + (uint32_t)hpBefore);
                kills++;
                score += 10 + (e.type * 5);

                const int ex = (int)e.x + (int)(ENEMY_W / 2);
                const int ey = (int)e.y + (int)(ENEMY_H / 2);
                spawnExplosion(ex, ey, COLOR_WHITE, now);
                spawnParticles((float)ex, (float)ey, COLOR_PURPLE, 16, now);
                // Keep existing drop behavior.
                const float kickVx = ((float)random(-100, 101) / 100.0f) * 0.70f;
                const float kickVy = -(((float)random(20, 80) / 100.0f) * 0.10f);
                maybeDropPowerup(e.x + 1.0f, e.y + 2.0f, kickVx, kickVy);
                break;
            }
        }

        // ---------------------------------------------------------
        // Player bullets vs boss
        // ---------------------------------------------------------
        if (boss.active) {
            const int bx0 = (int)boss.x;
            const int by0 = (int)boss.y;
            for (int bi = 0; bi < MAX_PLAYER_BULLETS; bi++) {
                Bullet& b = playerBullets[bi];
                if (!b.active) continue;
                const int bix = (int)b.x;
                const int biy = (int)b.y;
                if (rectContains(bix, biy, bx0, by0, BOSS_W, BOSS_H)) {
                    b.active = false;
                    // Shield absorbs first.
                    if (boss.shieldTier > 0) {
                        boss.shieldTier--;
                        boss.shieldFlashUntilMs = now + 180;
                    } else {
                        const uint8_t dmg = max<uint8_t>(1, b.dmg);
                        if (boss.hp > dmg) boss.hp = (uint8_t)(boss.hp - dmg);
                        else boss.hp = 0;
                        boss.shieldFlashUntilMs = now + 180;
                    }

                    spawnParticles((float)bix, (float)biy, COLOR_WHITE, 10, now);

                    if (boss.hp == 0) {
                        // Boss defeated: start huge explosion sequence (2 seconds),
                        // then guaranteed loot spawns at the end.
                        score += 250 + (int)bossesDefeated * 25;
                        bossesDefeated++;
                        level++;                 // boss kill => next level (requested)
                        hitsThisLevel = 0;       // reset hits for the new level
                        hitsUntilBoss = (uint16_t)(9 + max(1, level)); // level1=10, level2=11, ...
                        boss.active = false;
                        bossPending = false;
                        startBossDeath(now);
                    }
                    break;
                }
            }
        }

        // Player bullets vs enemies
        for (int bi = 0; bi < MAX_PLAYER_BULLETS; bi++) {
            Bullet& b = playerBullets[bi];
            if (!b.active) continue;
            const int bx = (int)b.x;
            const int by = (int)b.y;

            for (int ei = 0; ei < MAX_ENEMIES; ei++) {
                Enemy& e = enemies[ei];
                if (!e.alive) continue;
                if (rectContains(bx, by, (int)e.x, (int)e.y, ENEMY_W, ENEMY_H)) {
                    // Apply damage
                    const uint8_t dmg = max<uint8_t>(1, b.dmg);
                    const uint8_t hpBefore = e.hp;
                    if (e.hp > dmg) e.hp = (uint8_t)(e.hp - dmg);
                    else e.hp = 0;
                    const uint8_t applied = (hpBefore > e.hp) ? (uint8_t)(hpBefore - e.hp) : 0;
                    hitsThisLevel = (uint16_t)min<uint32_t>(65535u, (uint32_t)hitsThisLevel + (uint32_t)applied);

                    if (e.hp == 0) {
                        e.alive = false;
                        kills++;
                        score += 10 + (e.type * 5);
                        // Explosion + powerup kick
                        const int ex = (int)e.x + (int)(ENEMY_W / 2);
                        const int ey = (int)e.y + (int)(ENEMY_H / 2);
                        spawnExplosion(ex, ey, ShooterGameConfig::ENEMY_COLORS[e.type & 3], now);
                        // Extra sparkle burst (Breakout-style debris).
                        spawnParticles((float)ex, (float)ey, ShooterGameConfig::ENEMY_COLORS[e.type & 3], 12, now);
                        // Powerup kick: stronger sideways randomness, slight upward.
                        const float kickVx = ((float)random(-100, 101) / 100.0f) * 0.70f; // ~-0.70..0.70
                        const float kickVy = -(((float)random(20, 80) / 100.0f) * 0.10f);  // ~-0.02..-0.08
                        maybeDropPowerup(e.x + 1.0f, e.y + 2.0f, kickVx, kickVy);
                    }
                    b.active = false;
                    break;
                }
            }
        }

        // Enemy body vs player ship:
        // - Enemies do NOT explode at the bottom anymore (they can fly off-screen).
        // - They only cause damage when touching the player ship.
        // Shield mechanics change: player shield does NOT collide with enemies/bosses,
        // but a shield tier can still be consumed on contact as a "buffer" (requested).
        const bool shieldActiveForContact = ((int32_t)(shieldUntilMs - now) > 0) && shieldTier > 0;
        const bool invuln = ((int32_t)(invulnUntilMs - now) > 0);
        const int px = (int)player.x;
        const int py = (int)player.y;
        for (int ei = 0; ei < MAX_ENEMIES; ei++) {
            Enemy& e = enemies[ei];
            if (!e.alive) continue;
            // Enemy body overlap with player ship rect
            const int ex = (int)e.x;
            const int ey = (int)e.y;
            // Quick reject
            if (ex + ENEMY_W <= px || ex >= px + SHIP_W || ey + ENEMY_H <= py || ey >= py + SHIP_H) continue;

            // Collision: destroy enemy and apply damage unless invulnerable.
            e.alive = false;
            spawnExplosion(ex + (int)(ENEMY_W / 2), ey + (int)(ENEMY_H / 2), ShooterGameConfig::ENEMY_COLORS[e.type & 3], now);

            if (!invuln) {
                if (shieldActiveForContact) {
                    shieldHitFlashUntilMs = now + 180;
                    shieldTier--;
                    if (shieldTier == 0) shieldUntilMs = 0;
                } else {
                    loseLife(now);
                }
            }
        }

        // Enemy bullets vs player (shield neutralizes projectiles only)
        const bool shieldActive = ((int32_t)(shieldUntilMs - now) > 0) && shieldTier > 0;
        const uint8_t shieldR = shieldActive ? shieldRadiusForTier(shieldTier) : 0;
        const int cx = (int)player.x + 2;
        const int cy = (int)player.y + 2;

        // Invulnerability window after taking damage.
        // (invuln / px / py already declared above and reused below)
        for (int bi = 0; bi < MAX_ENEMY_BULLETS; bi++) {
            Bullet& b = enemyBullets[bi];
            if (!b.active) continue;

            // Shield neutralization (circle)
            const int bix = (int)b.x;
            const int biy = (int)b.y;

            if (shieldActive) {
                const int dx = bix - cx;
                const int dy = biy - cy;
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

            if (rectContains(bix, biy, px, py, SHIP_W, SHIP_H)) {
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

        // ---------------------------------------------------------
        // Boss projectiles vs player (stars + rockets)
        // ---------------------------------------------------------
        auto tryHitPlayer = [&](float fx, float fy, bool& activeFlag) {
            const int ix = (int)fx;
            const int iy = (int)fy;

            // Shield neutralization (circle)
            if (shieldActive) {
                const int dx = ix - cx;
                const int dy = iy - cy;
                if ((dx * dx + dy * dy) <= (int)shieldR * (int)shieldR) {
                    activeFlag = false;
                    shieldHitFlashUntilMs = now + 120;
                    if (shieldTier > 0) {
                        shieldTier--;
                        if (shieldTier == 0) shieldUntilMs = 0;
                    }
                    spawnParticles(fx, fy, COLOR_CYAN, 8, now);
                    return;
                }
            }

            if (rectContains(ix, iy, px, py, SHIP_W, SHIP_H)) {
                activeFlag = false;
                if (!invuln) {
                    spawnExplosion(cx, cy, COLOR_ORANGE, now);
                    loseLife(now);
                }
            }
        };

        for (int i = 0; i < MAX_STAR_SHOTS; i++) {
            if (!starShots[i].active) continue;
            bool& a = starShots[i].active;
            tryHitPlayer(starShots[i].x, starShots[i].y, a);
        }
        for (int i = 0; i < MAX_ROCKETS; i++) {
            if (!rockets[i].active) continue;
            bool& a = rockets[i].active;
            tryHitPlayer(rockets[i].x, rockets[i].y, a);
        }
    }

    // ---------------------------------------------------------
    // Rendering helpers
    // ---------------------------------------------------------
    void drawThrusterBack(MatrixPanel_I2S_DMA* display,
                          int shipX, int shipY, int shipW, int shipH,
                          bool exhaustDown, bool enabled, uint32_t now) {
        // Simplified thruster:
        // - Always comes out the "back" of the sprite (no velocity-vector math).
        // - Draw it BEFORE the ship sprite so it sits underneath.
        // - Skip pixels inside the ship bounding box so it never shows through holes.
        if (!enabled) return;

        (void)now;

        // Middle pixel of ship (requested)
        const int cx = shipX + shipW / 2;
        const int cy = shipY + shipH / 2;

        // Exhaust direction: down (+y) or up (-y)
        const int sy = exhaustDown ? 1 : -1;

        const int len = (int)ShooterGameConfig::THRUSTER_LEN_PX;
        const float denom = (float)max(1, len - 1);
        for (int i = 1; i <= len; i++) {
            const int y = cy + sy * i;

            // Fade from bright cyan to transparent (by skipping very dim pixels).
            // d: 0 at first exhaust pixel, 1 at last exhaust pixel
            const float d = (float)(i - 1) / denom;
            float f = 1.0f - d;      // 1..0
            f = f * f;               // faster fade than linear, but smoother than cubic
            const uint8_t mul = (uint8_t)constrain((int)(255.0f * f), 0, 255);
            if (mul < ShooterGameConfig::THRUSTER_MIN_MUL) continue; // transparent

            // 1px wide (requested)
            const int x = cx;
            if (x < 0 || x >= PANEL_RES_X || y < 0 || y >= PANEL_RES_Y) continue;
            if (x >= shipX && x < shipX + shipW && y >= shipY && y < shipY + shipH) continue;

            display->drawPixel(x, y, dimColor(display, COLOR_CYAN, mul));
        }
    }

    static inline uint8_t randomCloudSprite(uint8_t minIncl, uint8_t maxIncl) {
        // random(a,b) on Arduino is [a, b), so we +1 for inclusive max.
        const int lo = (int)minIncl;
        const int hi = (int)maxIncl + 1;
        return (uint8_t)random(lo, hi);
    }

    void initCloudLayer(Cloud* arr, int count, float baseVy, float vxJitter, uint8_t spriteMinIncl, uint8_t spriteMaxIncl, uint32_t now) {
        (void)now;
        for (int i = 0; i < count; i++) {
            arr[i].active = true;
            arr[i].sprite = randomCloudSprite(spriteMinIncl, spriteMaxIncl);
            const int w = (int)ShooterGameConfig::CLOUD_W[arr[i].sprite];
            const int h = (int)ShooterGameConfig::CLOUD_H[arr[i].sprite];
            arr[i].x = (float)random(-w, PANEL_RES_X);
            arr[i].y = (float)random(HUD_H + 1, PANEL_RES_Y);
            arr[i].vy = baseVy * (0.8f + ((float)random(0, 41) / 100.0f)); // 0.8..1.2
            arr[i].vx = ((float)random(-100, 101) / 100.0f) * vxJitter;
        }
    }

    void resetOneCloud(Cloud& c, float baseVy, float vxJitter, uint8_t spriteMinIncl, uint8_t spriteMaxIncl) {
        c.active = true;
        c.sprite = randomCloudSprite(spriteMinIncl, spriteMaxIncl);
        const int w = (int)ShooterGameConfig::CLOUD_W[c.sprite];
        const int h = (int)ShooterGameConfig::CLOUD_H[c.sprite];
        c.x = (float)random(-w, PANEL_RES_X);
        c.y = (float)(HUD_H - h - (int)random(0, 12)); // spawn above the HUD band
        c.vy = baseVy * (0.8f + ((float)random(0, 41) / 100.0f));
        c.vx = ((float)random(-100, 101) / 100.0f) * vxJitter;
    }

    void updateCloudLayer(Cloud* arr, int count, float baseVy, float vxJitter, uint8_t spriteMinIncl, uint8_t spriteMaxIncl) {
        for (int i = 0; i < count; i++) {
            Cloud& c = arr[i];
            if (!c.active) continue;
            c.x += c.vx;
            c.y += c.vy;

            // Wrap horizontally with a little slack.
            if (c.x < -12.0f) c.x = (float)(PANEL_RES_X + 11);
            if (c.x > (float)(PANEL_RES_X + 12)) c.x = -11.0f;

            const int h = (int)ShooterGameConfig::CLOUD_H[c.sprite];
            if (c.y > (float)(PANEL_RES_Y + h + 2)) {
                resetOneCloud(c, baseVy, vxJitter, spriteMinIncl, spriteMaxIncl);
            }
        }
    }

    void updateClouds(uint32_t now) {
        (void)now;
        updateCloudLayer(
            cloudsFar,
            CLOUD_LAYER0_COUNT,
            ShooterGameConfig::CLOUD_LAYER0_VY,
            ShooterGameConfig::CLOUD_LAYER0_VX_JITTER,
            ShooterGameConfig::CLOUD_LAYER0_SPRITE_MIN,
            ShooterGameConfig::CLOUD_LAYER0_SPRITE_MAX
        );
        updateCloudLayer(
            cloudsNear,
            CLOUD_LAYER1_COUNT,
            ShooterGameConfig::CLOUD_LAYER1_VY,
            ShooterGameConfig::CLOUD_LAYER1_VX_JITTER,
            ShooterGameConfig::CLOUD_LAYER1_SPRITE_MIN,
            ShooterGameConfig::CLOUD_LAYER1_SPRITE_MAX
        );
    }

    void drawCloudLayer(MatrixPanel_I2S_DMA* display, const Cloud* arr, int count, uint8_t mul) {
        for (int i = 0; i < count; i++) {
            const Cloud& c = arr[i];
            if (!c.active) continue;
            const int sx = (int)c.sprite;
            const int w = (int)ShooterGameConfig::CLOUD_W[sx];
            const int h = (int)ShooterGameConfig::CLOUD_H[sx];
            const int x0 = (int)c.x;
            const int y0 = (int)c.y;
            for (int y = 0; y < h; y++) {
                const int py = y0 + y;
                if (py < 0 || py >= PANEL_RES_Y) continue;
                for (int x = 0; x < w; x++) {
                    const uint8_t v = ShooterGameConfig::CLOUD_SPRITES[sx][y][x] & 3;
                    if (v == 0) continue;
                    const int px = x0 + x;
                    if (px < 0 || px >= PANEL_RES_X) continue;
                    // Layer mul is the brightness for "3". Scale 1..3 accordingly.
                    const uint8_t pmul = (uint8_t)((uint16_t)mul * (uint16_t)v / 3u);
                    display->drawPixel(px, py, dimColor(display, COLOR_WHITE, pmul));
                }
            }
        }
    }

    void drawClouds(MatrixPanel_I2S_DMA* display) {
        // Two-layer parallax: draw far first, then near.
        drawCloudLayer(display, cloudsFar, CLOUD_LAYER0_COUNT, ShooterGameConfig::CLOUD_LAYER0_MUL);
        drawCloudLayer(display, cloudsNear, CLOUD_LAYER1_COUNT, ShooterGameConfig::CLOUD_LAYER1_MUL);
    }

    void drawShip(MatrixPanel_I2S_DMA* display, int x, int y, uint16_t color, bool shield) {
        static constexpr uint8_t MUL_LUT[4] = { 0, 80, 160, 255 };
        for (int yy = 0; yy < SHIP_H; yy++) {
            for (int xx = 0; xx < SHIP_W; xx++) {
                const uint8_t v = ShooterGameConfig::SHIP_SPRITE[yy][xx] & 3;
                if (v == 0) continue;
                const uint16_t c = (v == 3) ? color : dimColor(display, color, MUL_LUT[v]);
                display->drawPixel(x + xx, y + yy, c);
            }
        }

        // Rocket ammo indicator (purple powerup):
        // row 2, col 0 and col 4 (0-based in the 5x5 ship)
        // - 1 rocket: left purple pixel
        // - 2 rockets: both purple pixels
        const int ry = y + 2;
        if (ry >= 0 && ry < PANEL_RES_Y) {
            display->drawPixel(x + 0, ry, (rocketAmmo >= 1) ? COLOR_PURPLE : COLOR_BLACK);
            display->drawPixel(x + 4, ry, (rocketAmmo >= 2) ? COLOR_PURPLE : COLOR_BLACK);
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
        static constexpr uint8_t MUL_LUT[4] = { 0, 80, 160, 255 };
        const uint16_t c = ShooterGameConfig::ENEMY_COLORS[type & 3];
        for (int yy = 0; yy < ENEMY_H; yy++) {
            for (int xx = 0; xx < ENEMY_W; xx++) {
                const uint8_t v = ShooterGameConfig::ENEMY_SPRITES[type & 3][yy][xx] & 3;
                if (v == 0) continue;
                const uint16_t col = (v == 3) ? c : dimColor(display, c, MUL_LUT[v]);
                display->drawPixel(x + xx, y + yy, col);
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
        const uint32_t now = (uint32_t)millis();
        // Enemies face DOWN, so exhaust goes UP. Always on for enemies.
        drawThrusterBack(display, (int)e.x, (int)e.y, ENEMY_W, ENEMY_H, false, true, now);
        drawEnemy(display, (int)e.x, (int)e.y, e.type);
        // Lives behind the enemy (same style concept as the player):
        // centered dots that can overlap the top sprite row (like the player lives overlap the bottom row).
        const int x = (int)e.x;
        const int y = (int)e.y;
        const uint8_t maxHp = (uint8_t)constrain(e.maxHp, 1, 4);
        const uint8_t hp = (uint8_t)constrain(e.hp, 0, 4);
        const int dotY = max(HUD_H, y); // overlap top row when possible
        if (dotY >= 0 && dotY < PANEL_RES_Y) {
            const int cx = x + (int)(ENEMY_W / 2);
            const int startX = cx - (int)((maxHp - 1) / 2);
            for (uint8_t i = 0; i < maxHp; i++) {
                const uint16_t col = (i < hp) ? COLOR_GREEN : dimColor(display, COLOR_GREEN, 60);
                display->drawPixel(startX + (int)i, dotY, col);
            }
        }
    }

    void drawBoss(MatrixPanel_I2S_DMA* display, uint32_t now) {
        if (!boss.active) return;
        static constexpr uint8_t MUL_LUT[4] = { 0, 80, 160, 255 };
        const int x0 = (int)boss.x;
        const int y0 = (int)boss.y;
        // Boss faces DOWN, so exhaust goes UP. Always on while boss is active.
        drawThrusterBack(display, x0, y0, BOSS_W, BOSS_H, false, true, now);
        const uint16_t baseCol = ShooterGameConfig::BOSS_COLORS[boss.type % 5];

        // Shield flash when hit.
        const bool flash = ((int32_t)(boss.shieldFlashUntilMs - now) > 0);
        const uint16_t col = flash ? COLOR_WHITE : baseCol;

        for (int y = 0; y < BOSS_H; y++) {
            for (int x = 0; x < BOSS_W; x++) {
                const uint8_t v = ShooterGameConfig::BOSS_SPRITES[boss.type % 5][y][x] & 3;
                if (v == 0) continue;
                const uint16_t col2 = (v == 3) ? col : dimColor(display, col, MUL_LUT[v]);
                display->drawPixel(x0 + x, y0 + y, col2);
            }
        }

        // Boss shield ring (10 tiers max).
        if (boss.shieldTier > 0) {
            const int cx = x0 + (int)(BOSS_W / 2);
            const int cy = y0 + (int)(BOSS_H / 2);
            const int r = 6 + (int)min<uint8_t>(4, (uint8_t)(boss.shieldTier / 2)); // 6..10
            const bool flicker = ((now / 220) % 2) == 0;
            uint16_t sc = COLOR_CYAN;
            if (!flash && flicker) sc = dimColor(display, COLOR_CYAN, 180);
            if (flash) sc = COLOR_RED;
            display->drawCircle(cx, cy, r, sc);
        }

        // Lives behind the boss (same style concept as the player):
        // centered dots that can overlap the top sprite row.
        const int py = max(HUD_H, y0); // overlap top row when possible
        if (py >= 0 && py < PANEL_RES_Y) {
            const int cxp = x0 + (int)(BOSS_W / 2);
            const int maxHp = (int)boss.maxHp;
            const int startX = cxp - (maxHp - 1) / 2;
            for (int i = 0; i < maxHp; i++) {
                const uint16_t c = (i < (int)boss.hp) ? COLOR_GREEN : dimColor(display, COLOR_GREEN, 60);
                display->drawPixel(startX + i, py, c);
            }
        }
    }

    void drawBossProjectiles(MatrixPanel_I2S_DMA* display, uint32_t now) {
        // Spinning stars (red)
        for (int i = 0; i < MAX_STAR_SHOTS; i++) {
            if (!starShots[i].active) continue;
            const int x = (int)starShots[i].x;
            const int y = (int)starShots[i].y;
            if (x < 1 || x >= PANEL_RES_X - 1 || y < 1 || y >= PANEL_RES_Y - 1) continue;

            // Spin: alternate plus / x shape.
            const uint32_t age = (uint32_t)(now - starShots[i].startMs);
            const bool phase = ((age / 140) % 2) == 0;
            display->drawPixel(x, y, COLOR_RED);
            if (phase) {
                display->drawPixel(x + 1, y, COLOR_RED);
                display->drawPixel(x - 1, y, COLOR_RED);
                display->drawPixel(x, y + 1, COLOR_RED);
                display->drawPixel(x, y - 1, COLOR_RED);
            } else {
                display->drawPixel(x + 1, y + 1, COLOR_RED);
                display->drawPixel(x - 1, y + 1, COLOR_RED);
                display->drawPixel(x + 1, y - 1, COLOR_RED);
                display->drawPixel(x - 1, y - 1, COLOR_RED);
            }
        }

        // Guided rockets: 2px tall WHITE body (high visibility) + flickering flame.
        for (int i = 0; i < MAX_ROCKETS; i++) {
            if (!rockets[i].active) continue;
            const int x = (int)rockets[i].x;
            const int y = (int)rockets[i].y;
            if (x < 0 || x >= PANEL_RES_X || y < 0 || y >= PANEL_RES_Y) continue;

            // 2px tall white body (vertical) to read well on 64×64.
            display->drawPixel(x, y, COLOR_WHITE);
            if (y - 1 >= 0) display->drawPixel(x, y - 1, COLOR_WHITE);

            // Flame flicker behind velocity.
            const bool flicker = ((now / 90) % 2) == 0;
            const int fx = x - (rockets[i].vx >= 0 ? 1 : -1);
            const int fy = y - (rockets[i].vy >= 0 ? 1 : -1);
            if (fx >= 0 && fx < PANEL_RES_X && fy >= 0 && fy < PANEL_RES_Y) {
                display->drawPixel(fx, fy, flicker ? COLOR_RED : COLOR_YELLOW);
            }
        }

        // Player guided rockets (also 2px tall white body, with purple flame).
        for (int i = 0; i < MAX_PLAYER_ROCKETS; i++) {
            if (!playerRockets[i].active) continue;
            const int x = (int)playerRockets[i].x;
            const int y = (int)playerRockets[i].y;
            if (x < 0 || x >= PANEL_RES_X || y < 0 || y >= PANEL_RES_Y) continue;

            display->drawPixel(x, y, COLOR_WHITE);
            if (y - 1 >= 0) display->drawPixel(x, y - 1, COLOR_WHITE);

            const bool flicker = ((now / 90) % 2) == 0;
            const int fx = x - (playerRockets[i].vx >= 0 ? 1 : -1);
            const int fy = y - (playerRockets[i].vy >= 0 ? 1 : -1);
            if (fx >= 0 && fx < PANEL_RES_X && fy >= 0 && fy < PANEL_RES_Y) {
                display->drawPixel(fx, fy, flicker ? COLOR_PURPLE : COLOR_MAGENTA);
            }
        }
    }

    void drawBullet(MatrixPanel_I2S_DMA* display, const Bullet& b, bool playerUp) {
        // Fading tail along velocity (straight trajectory; enemy shots can be angled now).
        const uint16_t head = b.color;
        const uint16_t mid = dimColor(display, b.color, 160);
        const uint16_t tail = dimColor(display, b.color, 90);
        const uint16_t tail2 = dimColor(display, b.color, 40);

        const int hx = (int)b.x;
        const int hy = (int)b.y;
        if (hx < 0 || hx >= PANEL_RES_X || hy < 0 || hy >= PANEL_RES_Y) return;

        float vx = b.vx;
        float vy = b.vy;
        // If velocity is degenerate (shouldn't happen), fall back to vertical orientation.
        if (fabsf(vx) < 0.001f && fabsf(vy) < 0.001f) vy = playerUp ? -1.0f : 1.0f;
        const float len = sqrtf(vx * vx + vy * vy);
        const float inv = (len > 0.001f) ? (1.0f / len) : 1.0f;
        const float ux = vx * inv;
        const float uy = vy * inv;

        // Head
        display->drawPixel(hx, hy, head);

        // Tail samples behind the head.
        const int n = playerUp ? BULLET_LEN : ENEMY_BULLET_LEN;
        for (int i = 1; i < n; i++) {
            const int tx = (int)roundf((float)hx - ux * (float)i);
            const int ty = (int)roundf((float)hy - uy * (float)i);
            if (tx < 0 || tx >= PANEL_RES_X || ty < 0 || ty >= PANEL_RES_Y) continue;
            const uint16_t c = (i == 1) ? mid : (i == 2) ? tail : tail2;
            display->drawPixel(tx, ty, c);
        }
    }

    void drawPowerup(MatrixPanel_I2S_DMA* display, int x, int y, uint8_t type) {
        const uint16_t c =
            (type == 0) ? COLOR_BLUE :
            (type == 1) ? COLOR_RED :
            (type == 2) ? COLOR_GREEN :
            COLOR_PURPLE;
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
        : gameOver(false), score(0), level(1), lives(1), lastUpdate(0), 
          lastShot(0) {
        kills = 0;
    }

    void start() override {
        gameOver = false;
        score = 0;
        level = 1;
        lives = ShooterGameConfig::PLAYER_START_LIVES; // start with configured lives
        kills = 0;
        hitsThisLevel = 0;
        hitsUntilBoss = ShooterGameConfig::BOSS_HITS_BASE;
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
        clearParticles();
        clearBossProjectiles();
        boss.active = false;
        bossesDefeated = 0;
        bossPending = false;
        level = 1;
        hitsThisLevel = 0;
        hitsUntilBoss = ShooterGameConfig::BOSS_HITS_BASE;
        bossDeathActive = false;
        bossLootSpawned = false;
        bossDeathDamagedPlayer = false;
        playerDeathActive = false;
        spawnPauseUntilMs = 0;
        for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].alive = false;
        
        // Apply current global player color (chosen in the main menu).
        player.color = globalSettings.getPlayerColor();

        resetPlayerAndBullets();
        initCloudLayer(
            cloudsFar,
            CLOUD_LAYER0_COUNT,
            ShooterGameConfig::CLOUD_LAYER0_VY,
            ShooterGameConfig::CLOUD_LAYER0_VX_JITTER,
            ShooterGameConfig::CLOUD_LAYER0_SPRITE_MIN,
            ShooterGameConfig::CLOUD_LAYER0_SPRITE_MAX,
            (uint32_t)lastUpdate
        );
        initCloudLayer(
            cloudsNear,
            CLOUD_LAYER1_COUNT,
            ShooterGameConfig::CLOUD_LAYER1_VY,
            ShooterGameConfig::CLOUD_LAYER1_VX_JITTER,
            ShooterGameConfig::CLOUD_LAYER1_SPRITE_MIN,
            ShooterGameConfig::CLOUD_LAYER1_SPRITE_MAX,
            (uint32_t)lastUpdate
        );
        // Spawn initial enemies: adhere to the difficulty curve (starts at 1 enemy).
        spawnEnemy((uint32_t)lastUpdate);
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

        // Background always moves (nice parallax even during countdown/freeze).
        updateClouds((uint32_t)now);

        // Particles keep simulating even during countdown / freezes (looks nicer).
        updateParticles((uint32_t)now);

        // Final freeze before game over overlay/leaderboard.
        if (phase == PHASE_GAME_OVER_DELAY) {
            // Keep the player death explosion animating during the delay.
            // (Particles are updated at the top of update().)
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
            const float rawY = clampf((float)InputDetail::axisY(p1, 0) / (float)AXIS_DIVISOR, -1.0f, 1.0f);
            float sx = deadzone01(rawX, STICK_DEADZONE);
            float sy = deadzone01(rawY, STICK_DEADZONE);
            if (sx == 0.0f) {
                const uint8_t dpad = p1->dpad();
                if (dpad & 0x08) sx = -1.0f;
                else if (dpad & 0x04) sx = 1.0f;
            }
            if (sy == 0.0f) {
                const uint8_t dpad = p1->dpad();
                // D-pad up/down fallback for vertical motion
                if (dpad & 0x01) sy = -1.0f;
                else if (dpad & 0x02) sy = 1.0f;
            }

            // Thruster-style movement:
            // - When stick/dpad is engaged, we smoothly steer velocity toward target.
            // - When disengaged, ship drifts and slowly decays (space feel).
            const float targetVx = sx * player.speed;
            const float targetVy = sy * player.speed;

            const bool thrustX = (fabsf(sx) > 0.001f);
            const bool thrustY = (fabsf(sy) > 0.001f);
            if (thrustX) player.vx = player.vx * (1.0f - MOVE_SMOOTH) + targetVx * MOVE_SMOOTH;
            else player.vx *= ShooterGameConfig::PLAYER_DRIFT_DRAG;
            if (fabsf(player.vx) < ShooterGameConfig::PLAYER_DRIFT_STOP_EPS) player.vx = 0.0f;

            if (thrustY) player.vy = player.vy * (1.0f - MOVE_SMOOTH) + targetVy * MOVE_SMOOTH;
            else player.vy *= ShooterGameConfig::PLAYER_DRIFT_DRAG;
            if (fabsf(player.vy) < ShooterGameConfig::PLAYER_DRIFT_STOP_EPS) player.vy = 0.0f;

            player.x += player.vx;
            player.y += player.vy;

            // Keep ship within playfield (stay below HUD band).
            const float minY = (float)(HUD_H + 1);
            const float maxY = (float)(PANEL_RES_Y - SHIP_H);

            // Clamp and zero velocity on impact so we don't "buzz" against the walls.
            if (player.x < 0.0f) { player.x = 0.0f; player.vx = 0.0f; }
            if (player.x > (float)(PANEL_RES_X - SHIP_W)) { player.x = (float)(PANEL_RES_X - SHIP_W); player.vx = 0.0f; }
            if (player.y < minY) { player.y = minY; player.vy = 0.0f; }
            if (player.y > maxY) { player.y = maxY; player.vy = 0.0f; }
            
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
                    const int py = (int)player.y;
                    spawnPlayerBullet(cx, py - 1);
                    spawnPlayerBullet(cx - 2, py - 1);
                    spawnPlayerBullet(cx + 2, py - 1);

                    // tier4: make center bullet "red 2 dmg"
                    if (t == 4) {
                        for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
                            if (!playerBullets[i].active) continue;
                            if (playerBullets[i].x == cx && playerBullets[i].y == (int)player.y - 1) {
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
                            if (playerBullets[i].y == (int)player.y - 1 &&
                                (playerBullets[i].x == cx || playerBullets[i].x == cx - 2 || playerBullets[i].x == cx + 2)) {
                                playerBullets[i].color = COLOR_RED;
                                playerBullets[i].dmg = 2;
                            }
                        }
                    }
                } else {
                    // Single bullet
                    spawnPlayerBullet(cx, (int)player.y - 1);
                    // tier2: red 2 dmg
                    if (t == 2) {
                        for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
                            if (!playerBullets[i].active) continue;
                            if (playerBullets[i].x == cx && playerBullets[i].y == (int)player.y - 1) {
                                playerBullets[i].color = COLOR_RED;
                                playerBullets[i].dmg = 2;
                                break;
                            }
                        }
                    }
                }
                lastShot = now;
            }

            // Fire guided missile (B) if we have rocket ammo (purple powerup).
            if (p1->b() && rocketAmmo > 0 && phase == PHASE_PLAYING && (uint32_t)(now - lastRocketFireMs) > ROCKET_COOLDOWN_MS) {
                const float rx = player.x + 2.0f;         // center-ish
                const float ry = (float)((int)player.y) - 1.0f;  // launch just above ship
                spawnPlayerRocket(rx, ry, (uint32_t)now);
                rocketAmmo--;
                lastRocketFireMs = (uint32_t)now;
            }
        }

        if (phase != PHASE_PLAYING) return;

        // Boss flow: hits_until_boss -> stop spawning, wait for clear, then spawn boss.
        balanceBossDifficulty();
        updateBossFlow((uint32_t)now);
        updateBoss((uint32_t)now);
        updateBossProjectiles((uint32_t)now);
        updatePlayerRockets((uint32_t)now);
        maybeApplyBossDeathExplosionDamage((uint32_t)now);
        updateBossDeath((uint32_t)now);

        // Continuous spawning (requested): start with 1 enemy, increase slowly with level.
        const bool spawningPaused = bossPending || boss.active || bossDeathActive || ((int32_t)(spawnPauseUntilMs - (uint32_t)now) > 0);
        const int target = min(MAX_ENEMIES, 1 + (max(1, level) - 1) / 2); // 1,1,2,2,3,3...
        if (!spawningPaused &&
            (uint32_t)(now - lastSpawnMs) >= SPAWN_INTERVAL_MS && aliveEnemyCount() < target) {
            lastSpawnMs = (uint32_t)now;
            spawnEnemy((uint32_t)now);
        }

        updateBulletsAndPowerups((uint32_t)now);
        updateEnemiesAndEnemyFire((uint32_t)now);
        handleCollisions((uint32_t)now);

        // Level progression is driven by boss kills (see boss defeat logic).
    }

    void draw(MatrixPanel_I2S_DMA* display) override {
        display->fillScreen(COLOR_BLACK);
        // Background layer (below everything)
        drawClouds(display);

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
            // Player faces UP, so exhaust goes DOWN. Only on while "thrusters" are engaged.
            const bool thrOn = (fabsf(player.vx) > ShooterGameConfig::PLAYER_DRIFT_STOP_EPS) || (fabsf(player.vy) > ShooterGameConfig::PLAYER_DRIFT_STOP_EPS);
            drawThrusterBack(display, (int)player.x, (int)player.y, SHIP_W, SHIP_H, true, thrOn, now);
            drawShip(display, (int)player.x, (int)player.y, player.color, ((int32_t)(shieldUntilMs - now) > 0));
            drawExplosions(display, now);
            drawParticles(display, now);
            drawPlayerDeathExplosion(display, now);

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
            // Player faces UP, so exhaust goes DOWN. Only on while "thrusters" are engaged.
            const bool thrOn = (fabsf(player.vx) > ShooterGameConfig::PLAYER_DRIFT_STOP_EPS) || (fabsf(player.vy) > ShooterGameConfig::PLAYER_DRIFT_STOP_EPS);
            drawThrusterBack(display, (int)player.x, (int)player.y, SHIP_W, SHIP_H, true, thrOn, now);
            drawShip(display, (int)player.x, (int)player.y, player.color, ((int32_t)(shieldUntilMs - now) > 0));
            return;
        }

        // Enemies
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (!enemies[i].alive) continue;
            drawEnemy(display, enemies[i]);
        }

        // Boss + boss projectiles
        drawBoss(display, now);
        drawBossProjectiles(display, now);
        drawBossDeathExplosion(display, now);
        drawPlayerDeathExplosion(display, now);

        // Powerups
        for (int i = 0; i < MAX_POWERUPS; i++) {
            if (!powerups[i].active) continue;
            drawPowerup(display, (int)powerups[i].x, (int)powerups[i].y, powerups[i].type);
        }

        // Bullets
        for (int i = 0; i < MAX_PLAYER_BULLETS; i++) if (playerBullets[i].active) drawBullet(display, playerBullets[i], true);
        for (int i = 0; i < MAX_ENEMY_BULLETS; i++) if (enemyBullets[i].active) drawBullet(display, enemyBullets[i], false);

        // Player ship (shield shows blue outline)
        // Player faces UP, so exhaust goes DOWN. Only on while "thrusters" are engaged.
        const bool thrOn = (fabsf(player.vx) > ShooterGameConfig::PLAYER_DRIFT_STOP_EPS) || (fabsf(player.vy) > ShooterGameConfig::PLAYER_DRIFT_STOP_EPS);
        drawThrusterBack(display, (int)player.x, (int)player.y, SHIP_W, SHIP_H, true, thrOn, now);
        drawShip(display, (int)player.x, (int)player.y, player.color, ((int32_t)(shieldUntilMs - (uint32_t)millis()) > 0));

        // Explosions overlay
        drawExplosions(display, now);
        drawParticles(display, now);
        drawPlayerDeathExplosion(display, now);
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

