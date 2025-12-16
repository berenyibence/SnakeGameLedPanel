#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "GameBase.h"
#include "ControllerManager.h"
#include "config.h"
#include "SmallFont.h"

/**
 * ShooterGame - Space shooter game
 * Player controls a ship at the bottom, shoots enemies from above
 */
class ShooterGame : public GameBase {
private:
    // Player ship structure
    struct Ship {
        float x;
        int y;
        int width;
        int height;
        float speed;
        uint16_t color;
        
        Ship() : x(32.0f), y(56), width(5), height(4), speed(2.0f), color(COLOR_GREEN) {}
    };
    
    // Bullet structure
    struct Bullet {
        float x;
        float y;
        float vy;
        bool active;
        uint16_t color;
        
        Bullet(float xPos, float yPos) 
            : x(xPos), y(yPos), vy(-3.0f), active(true), color(COLOR_CYAN) {}
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
            : x(xPos), y(yPos), vx(0.5f), alive(true), color(COLOR_RED), type(t) {}
    };
    
    Ship player;
    std::vector<Bullet> bullets;
    std::vector<Enemy> enemies;
    bool gameOver;
    int score;
    int level;
    unsigned long lastUpdate;
    unsigned long lastShot;
    unsigned long lastEnemySpawn;
    static const int UPDATE_INTERVAL_MS = 16;  // ~60 FPS
    static const int SHOT_COOLDOWN_MS = 200;
    static const int ENEMY_SPAWN_INTERVAL_MS = 1500;
    static const int ENEMY_ROWS = 3;
    static const int ENEMY_COLS = 6;
    
    /**
     * Initialize enemies
     */
    void initEnemies() {
        enemies.clear();
        int startX = 4;
        int startY = 8;
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
    void spawnEnemyWave() {
        initEnemies();
        level++;
        // Increase enemy speed with level
        for (auto& enemy : enemies) {
            enemy.vx = 0.5f + (level * 0.1f);
        }
    }

public:
    ShooterGame() 
        : gameOver(false), score(0), level(1), lastUpdate(0), 
          lastShot(0), lastEnemySpawn(0) {
        initEnemies();
    }

    void start() override {
        gameOver = false;
        score = 0;
        level = 1;
        lastUpdate = millis();
        lastShot = 0;
        lastEnemySpawn = millis();
        
        player.x = PANEL_RES_X / 2.0f - player.width / 2.0f;
        bullets.clear();
        spawnEnemyWave();
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
        
        // Update player position
        ControllerPtr p1 = input->getController(0);
        if (p1 && p1->isConnected()) {
            uint8_t dpad = p1->dpad();
            if ((dpad & 0x08) && player.x > 0) {  // LEFT
                player.x -= player.speed;
            }
            if ((dpad & 0x04) && player.x < PANEL_RES_X - player.width) {  // RIGHT
                player.x += player.speed;
            }
            
            // Shoot with A button
            if (p1->a() && (now - lastShot > SHOT_COOLDOWN_MS)) {
                bullets.emplace_back(player.x + player.width / 2.0f, player.y);
                lastShot = now;
            }
        }
        
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
                gameOver = true;
                return;
            }
        }
        
        // Reverse enemy direction if needed
        if (shouldReverse) {
            for (auto& enemy : enemies) {
                enemy.vx = -enemy.vx;
                enemy.y += 2;  // Move down
            }
        }
        
        // Check bullet-enemy collisions
        for (auto bulletIt = bullets.begin(); bulletIt != bullets.end();) {
            bool hit = false;
            
            for (auto& enemy : enemies) {
                if (!enemy.alive) continue;
                
                if (bulletIt->x >= enemy.x && bulletIt->x <= enemy.x + 4 &&
                    bulletIt->y >= enemy.y && bulletIt->y <= enemy.y + 4) {
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
            spawnEnemyWave();
        }
    }

    void draw(MatrixPanel_I2S_DMA* display) override {
        display->fillScreen(COLOR_BLACK);
        
        if (gameOver) {
            SmallFont::drawString(display, 8, 28, "GAME OVER", COLOR_RED);
            char scoreStr[16];
            snprintf(scoreStr, sizeof(scoreStr), "SCORE:%d", score);
            SmallFont::drawString(display, 4, 38, scoreStr, COLOR_WHITE);
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
            display->fillRect((int)bullet.x - 1, (int)bullet.y - 2, 2, 3, bullet.color);
        }
        
        // Draw player ship
        display->fillRect(
            (int)player.x, 
            player.y, 
            player.width, 
            player.height, 
            player.color
        );
        
        // Draw score and level with small font
        char infoStr[16];
        snprintf(infoStr, sizeof(infoStr), "S:%d L:%d", score, level);
        SmallFont::drawString(display, 2, 2, infoStr, COLOR_YELLOW);
    }

    bool isGameOver() override {
        return gameOver;
    }
};

