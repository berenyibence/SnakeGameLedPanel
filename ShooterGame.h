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

    // Bullet visuals
    static constexpr int BULLET_SIZE_PX = 2; // 2x2 for visibility
    static inline float bulletHalf() { return 1.0f; }

    // Round phases (countdown on start + after losing a life)
    enum RoundPhase : uint8_t { PHASE_COUNTDOWN, PHASE_PLAYING, PHASE_DEATH_FLASH, PHASE_GAME_OVER };
    RoundPhase phase = PHASE_COUNTDOWN;
    uint32_t phaseStartMs = 0;
    static constexpr uint16_t DEATH_FLASH_MS = 450;
    static constexpr uint16_t COUNTDOWN_MS = 3000;

    // Player ship structure
    struct Ship {
        float x;
        int y;
        int width;
        int height;
        float speed;
        uint16_t color;
        float vx;
        
        // Speed values in this project are effectively "px per tick" (tick ~= 16ms).
        // Keep the base movement intentionally slower for a more relaxed feel.
        Ship() : x(32.0f), y(56), width(5), height(4), speed(1.6f), color(COLOR_GREEN), vx(0.0f) {}
    };
    
    // Bullet structure
    struct Bullet {
        float x;
        float y;
        float vy;
        bool active;
        uint16_t color;
        
        Bullet(float xPos, float yPos) 
            : x(xPos), y(yPos), vy(-2.0f), active(true), color(COLOR_CYAN) {}
    };
    
    // Enemy structure
    struct Enemy {
        float x;
        float y;
        float vx;
        bool alive;
        uint16_t color;
        int type;
        
        Enemy(float xPos, float yPos, int t) 
            : x(xPos), y(yPos), vx(0.25f), alive(true), color(COLOR_RED), type(t) {}
    };
    
    Ship player;
    std::vector<Bullet> bullets;
    std::vector<Enemy> enemies;
    bool gameOver;
    int score;
    int level;
    int lives;
    unsigned long lastUpdate;
    unsigned long lastShot;
    static const int UPDATE_INTERVAL_MS = 16;  // ~60 FPS
    static const int SHOT_COOLDOWN_MS = 200;
    static const int ENEMY_ROWS = 3;
    static const int ENEMY_COLS = 6;
    
    /**
     * Initialize enemies
     */
    void initEnemies() {
        enemies.clear();
        int startX = 4;
        int startY = HUD_H + 2;
        int spacingX = 10;
        int spacingY = 8;
        
        for (int row = 0; row < ENEMY_ROWS; row++) {
            for (int col = 0; col < ENEMY_COLS; col++) {
                float x = startX + col * spacingX;
                float y = startY + row * spacingY;
                enemies.emplace_back(x, y, row);
            }
        }
    }
    
    /**
     * Spawn a new enemy wave
     */
    void spawnEnemyWave(bool incrementLevel) {
        initEnemies();
        if (incrementLevel) level++;
        // Increase enemy speed with level
        for (auto& enemy : enemies) {
            // Smooth/slow progression:
            // - Very slow at the start
            // - Small per-wave increments
            // - Hard cap to avoid becoming unreadable on 64x64
            const float base = 0.22f;
            const float inc = 0.015f;
            enemy.vx = min(0.55f, base + (float)max(0, level - 1) * inc);
        }
    }

    void resetPlayerAndBullets() {
        player.x = PANEL_RES_X / 2.0f - player.width / 2.0f;
        player.vx = 0.0f;
        bullets.clear();
        lastShot = 0;
    }

    void loseLife(uint32_t now) {
        lives--;
        if (lives <= 0) {
            gameOver = true;
            phase = PHASE_GAME_OVER;
            return;
        }
        // Visual feedback + countdown, then restart the current wave (same level).
        phase = PHASE_DEATH_FLASH;
        phaseStartMs = now;
        resetPlayerAndBullets();
        spawnEnemyWave(false);
    }

public:
    ShooterGame() 
        : gameOver(false), score(0), level(1), lives(3), lastUpdate(0), 
          lastShot(0) {
        initEnemies();
    }

    void start() override {
        gameOver = false;
        score = 0;
        level = 1;
        lives = 3;
        lastUpdate = millis();
        lastShot = 0;
        phase = PHASE_COUNTDOWN;
        phaseStartMs = lastUpdate;
        
        // Apply current global player color (chosen in the main menu).
        player.color = globalSettings.getPlayerColor();

        resetPlayerAndBullets();
        spawnEnemyWave(false);
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

        // Phase handling
        if (phase == PHASE_DEATH_FLASH) {
            if ((uint32_t)(now - phaseStartMs) >= DEATH_FLASH_MS) {
                phase = PHASE_COUNTDOWN;
                phaseStartMs = now;
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
            player.x = constrain(player.x, 0.0f, (float)(PANEL_RES_X - player.width));
            
            // Shoot with right trigger (fallback to A)
            const uint16_t rt = InputDetail::throttle(p1, 0);
            const bool shoot = (rt >= TRIGGER_THRESHOLD) || InputDetail::r2(p1, 0) || p1->a();
            if (shoot && (now - lastShot > SHOT_COOLDOWN_MS) && phase == PHASE_PLAYING) {
                bullets.emplace_back(player.x + player.width / 2.0f, (float)player.y);
                lastShot = now;
            }
        }

        if (phase != PHASE_PLAYING) return;
        
        // Update bullets
        for (auto it = bullets.begin(); it != bullets.end();) {
            it->y += it->vy;
            
            // Remove bullets that are off screen
            if (it->y < 0 || it->y > PANEL_RES_Y) {
                it = bullets.erase(it);
            } else {
                it++;
            }
        }
        
        // Update enemies
        bool shouldReverse = false;
        for (auto& enemy : enemies) {
            if (!enemy.alive) continue;
            
            enemy.x += enemy.vx;
            
            // Check if enemies hit the sides
            if (enemy.x <= 0 || enemy.x >= PANEL_RES_X - 4) {
                shouldReverse = true;
            }
            
            // Check if enemy reached player
            if (enemy.y >= player.y - 2) {
                loseLife((uint32_t)now);
                return;
            }
        }
        
        // Reverse enemy direction if needed
        if (shouldReverse) {
            for (auto& enemy : enemies) {
                enemy.vx = -enemy.vx;
                enemy.y += 1;  // Move down (slower pressure)
            }
        }
        
        // Check bullet-enemy collisions
        for (auto bulletIt = bullets.begin(); bulletIt != bullets.end();) {
            bool hit = false;
            
            for (auto& enemy : enemies) {
                if (!enemy.alive) continue;
                
                // Bullet is 2x2, enemy is 4x4.
                if (bulletIt->x + bulletHalf() >= enemy.x && bulletIt->x - bulletHalf() <= enemy.x + 4 &&
                    bulletIt->y + bulletHalf() >= enemy.y && bulletIt->y - bulletHalf() <= enemy.y + 4) {
                    enemy.alive = false;
                    score += 10 + (enemy.type * 5);
                    hit = true;
                    break;
                }
            }
            
            if (hit) {
                bulletIt = bullets.erase(bulletIt);
            } else {
                bulletIt++;
            }
        }
        
        // Check if all enemies destroyed
        bool allDestroyed = true;
        for (const auto& enemy : enemies) {
            if (enemy.alive) {
                allDestroyed = false;
                break;
            }
        }
        
        if (allDestroyed) {
            spawnEnemyWave(true);
        }
    }

    void draw(MatrixPanel_I2S_DMA* display) override {
        display->fillScreen(COLOR_BLACK);

        // HUD
        SmallFont::drawStringF(display, 2, 6, COLOR_YELLOW, "S:%d", score);
        SmallFont::drawStringF(display, 32, 6, COLOR_CYAN, "L:%d", lives);
        SmallFont::drawStringF(display, 52, 6, COLOR_WHITE, "W:%d", level);
        for (int x = 0; x < PANEL_RES_X; x += 2) display->drawPixel(x, HUD_H - 1, COLOR_BLUE);
        
        if (phase == PHASE_GAME_OVER || gameOver) {
            char tag[4];
            UserProfiles::getPadTag(0, tag);
            GameOverLeaderboardView::draw(display, "GAME OVER", leaderboardId(), leaderboardScore(), tag);
            return;
        }

        if (phase == PHASE_DEATH_FLASH) {
            display->fillRect(0, HUD_H, PANEL_RES_X, PANEL_RES_Y - HUD_H, COLOR_RED);
            SmallFont::drawString(display, 18, 30, "HIT!", COLOR_WHITE);
            return;
        }

        if (phase == PHASE_COUNTDOWN) {
            const uint32_t now = millis();
            const uint32_t elapsed = (uint32_t)(now - phaseStartMs);
            int secsLeft = 3 - (int)(elapsed / 1000UL);
            if (secsLeft < 1) secsLeft = 1;
            char c[2] = { (char)('0' + secsLeft), '\0' };
            SmallFont::drawString(display, 30, 30, c, COLOR_YELLOW);
            // Draw player ship during countdown.
            display->fillRect((int)player.x, player.y, player.width, player.height, player.color);
            return;
        }
        
        // Draw enemies
        for (const auto& enemy : enemies) {
            if (enemy.alive) {
                display->fillRect((int)enemy.x, (int)enemy.y, 4, 4, enemy.color);
            }
        }
        
        // Draw bullets
        for (const auto& bullet : bullets) {
            display->fillRect((int)bullet.x - 1, (int)bullet.y - 1, BULLET_SIZE_PX, BULLET_SIZE_PX, bullet.color);
        }
        
        // Draw player ship
        display->fillRect(
            (int)player.x, 
            player.y, 
            player.width, 
            player.height, 
            player.color
        );
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

