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
 * BreakoutGame - Classic Breakout/Arkanoid style game
 * Player controls a paddle at the bottom to bounce a ball and break bricks
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

    // HUD band at the top (avoid drawing bricks under the HUD).
    static constexpr int HUD_H = 8;

    // Ball: 2x2 minimum.
    static constexpr int BALL_SIZE_PX = 2;
    static inline float ballHalf() { return 1.0f; }
    static inline float ballSpeed() { return 1.15f; } // slower, more controllable
    static constexpr float STICK_DEADZONE = 0.18f;
    static constexpr int16_t AXIS_DIVISOR = 512;

    // Brick structure
    struct Brick {
        int x;
        int y;
        bool alive;
        uint16_t color;
        
        Brick(int xPos, int yPos, uint16_t c) 
            : x(xPos), y(yPos), alive(true), color(c) {}
    };
    
    // Paddle structure
    struct Paddle {
        float x;
        int y;
        int width;
        int height;
        float speed;
        uint16_t color;
        
        // Modern style: 1px tall paddle (as requested).
        Paddle() : x(32.0f), y(PANEL_RES_Y - 2), width(14), height(1), speed(2.4f), color(COLOR_CYAN) {}
    };
    
    // Ball structure
    struct Ball {
        float x;
        float y;
        float vx;
        float vy;
        uint16_t color;
        
        Ball() : x(32.0f), y(50.0f), vx(0.0f), vy(0.0f), color(COLOR_WHITE) {}
    };
    
    std::vector<Brick> bricks;
    Paddle paddle;
    Ball ball;
    bool gameOver;
    bool gameWon;
    bool ballHeld; // ball is held on paddle until A is pressed
    int score;
    int lives;
    unsigned long lastUpdate;
    static const int UPDATE_INTERVAL_MS = 16;  // ~60 FPS
    // Smaller bricks + more of them (as requested).
    static const int BRICK_ROWS = 6;
    static const int BRICK_COLS = 12;
    static const int BRICK_WIDTH = 4;
    static const int BRICK_HEIGHT = 2;
    static const int BRICK_SPACING = 1;
    
    /**
     * Initialize bricks in rows
     */
    void initBricks() {
        bricks.clear();
        uint16_t colors[] = {COLOR_RED, COLOR_ORANGE, COLOR_YELLOW, COLOR_GREEN, COLOR_CYAN, COLOR_PURPLE};

        // Center horizontally.
        const int totalW = BRICK_COLS * BRICK_WIDTH + (BRICK_COLS - 1) * BRICK_SPACING;
        const int startX = (PANEL_RES_X - totalW) / 2;
        const int startY = HUD_H + 2;
        
        for (int row = 0; row < BRICK_ROWS; row++) {
            for (int col = 0; col < BRICK_COLS; col++) {
                int x = startX + col * (BRICK_WIDTH + BRICK_SPACING);
                int y = startY + row * (BRICK_HEIGHT + BRICK_SPACING);
                bricks.emplace_back(x, y, colors[row % 6]);
            }
        }
    }
    
    /**
     * Reset ball to starting position
     */
    void resetBall() {
        ballHeld = true;
        ball.x = paddle.x + paddle.width / 2.0f;
        ball.y = (float)paddle.y - 2.0f; // 2x2 ball sits just above paddle
        ball.vx = 0.0f;
        ball.vy = 0.0f;
    }
    
    /**
     * Check collision between ball and rectangle
     */
    bool checkRectCollision(float bx, float by, int rx, int ry, int rw, int rh) {
        const float h = ballHalf();
        return (bx + h >= rx && bx - h <= rx + rw &&
                by + h >= ry && by - h <= ry + rh);
    }

public:
    BreakoutGame() 
        : gameOver(false), gameWon(false), ballHeld(true), score(0), lives(3), lastUpdate(0) {
        initBricks();
        resetBall();
    }

    void start() override {
        gameOver = false;
        gameWon = false;
        ballHeld = true;
        score = 0;
        lives = 3;
        lastUpdate = millis();

        // Apply current global player color for the player paddle.
        paddle.color = globalSettings.getPlayerColor();
        
        paddle.x = PANEL_RES_X / 2.0f - paddle.width / 2.0f;
        initBricks();
        resetBall();
    }

    void reset() override {
        start();
    }

    void update(ControllerManager* input) override {
        if (gameOver || gameWon) return;
        
        // Throttle updates
        unsigned long now = millis();
        if (now - lastUpdate < UPDATE_INTERVAL_MS) return;
        lastUpdate = now;
        
        // Update paddle position (analog left stick + D-pad fallback)
        ControllerPtr p1 = input->getController(0);
        if (p1 && p1->isConnected()) {
            const float raw = clampf((float)InputDetail::axisX(p1, 0) / (float)AXIS_DIVISOR, -1.0f, 1.0f);
            float sx = deadzone01(raw, STICK_DEADZONE);

            if (sx == 0.0f) {
                const uint8_t dpad = p1->dpad();
                if (dpad & 0x08) sx = -1.0f;      // LEFT
                else if (dpad & 0x04) sx = 1.0f;  // RIGHT
            }

            paddle.x += sx * paddle.speed;
            paddle.x = constrain(paddle.x, 0.0f, (float)(PANEL_RES_X - paddle.width));
            
            // Launch ball with A button
            if (p1->a() && ballHeld) {
                ballHeld = false;
                ball.vx = (random(0, 2) == 0) ? -ballSpeed() : ballSpeed();
                ball.vy = -ballSpeed();
            }
        }

        // Ball follow paddle during "held" phase
        if (ballHeld) {
            ball.x = paddle.x + paddle.width / 2.0f;
            ball.y = (float)paddle.y - 2.0f;
            return;
        }
        
        // Update ball position
        ball.x += ball.vx;
        ball.y += ball.vy;
        
        // Ball collision with walls
        const float h = ballHalf();
        if (ball.x - h <= 0 || ball.x + h >= PANEL_RES_X) {
            ball.vx = -ball.vx;
            ball.x = constrain(ball.x, h, PANEL_RES_X - h);
        }
        
        // Top bound is below HUD
        if (ball.y - h <= (HUD_H + 1)) {
            ball.vy = -ball.vy;
            ball.y = (float)(HUD_H + 1) + h;
        }
        
        // Ball collision with paddle
        if (checkRectCollision(ball.x, ball.y, (int)paddle.x, paddle.y, paddle.width, paddle.height)) {
            // Calculate hit position on paddle (-1 to 1)
            float hitPos = ((ball.x - paddle.x) / paddle.width) * 2.0f - 1.0f;
            ball.vx = hitPos * ballSpeed();
            ball.vy = -abs(ball.vy);
            ball.y = (float)paddle.y - h;
            
            // Normalize velocity
            float speed = sqrt(ball.vx * ball.vx + ball.vy * ball.vy);
            if (speed > 0) {
                ball.vx = (ball.vx / speed) * ballSpeed();
                ball.vy = (ball.vy / speed) * ballSpeed();
            }
        }
        
        // Ball collision with bricks
        for (auto& brick : bricks) {
            if (!brick.alive) continue;
            
            if (checkRectCollision(ball.x, ball.y, brick.x, brick.y, BRICK_WIDTH, BRICK_HEIGHT)) {
                brick.alive = false;
                score += 10;
                
                // Determine bounce direction
                float brickCenterX = brick.x + BRICK_WIDTH / 2.0f;
                float brickCenterY = brick.y + BRICK_HEIGHT / 2.0f;
                
                float dx = ball.x - brickCenterX;
                float dy = ball.y - brickCenterY;
                
                if (abs(dx) > abs(dy)) {
                    ball.vx = (dx > 0) ? abs(ball.vx) : -abs(ball.vx);
                } else {
                    ball.vy = (dy > 0) ? abs(ball.vy) : -abs(ball.vy);
                }
                break;
            }
        }
        
        // Check if ball fell off bottom
        if (ball.y > PANEL_RES_Y + h) {
            lives--;
            if (lives <= 0) {
                gameOver = true;
            } else {
                resetBall();
            }
        }
        
        // Check if all bricks destroyed
        bool allDestroyed = true;
        for (const auto& brick : bricks) {
            if (brick.alive) {
                allDestroyed = false;
                break;
            }
        }
        if (allDestroyed) {
            gameWon = true;
        }
    }

    void draw(MatrixPanel_I2S_DMA* display) override {
        display->fillScreen(COLOR_BLACK);
        
        if (gameOver || gameWon) {
            // Standard game-over screen: show Top-5 leaderboard for this game.
            const uint32_t s = leaderboardScore();
            char tag[4];
            UserProfiles::getPadTag(0, tag);
            GameOverLeaderboardView::draw(display, gameWon ? "YOU WIN" : "GAME OVER", leaderboardId(), s, tag);
            return;
        }

        // HUD
        SmallFont::drawStringF(display, 2, 6, COLOR_YELLOW, "S:%d", score);
        SmallFont::drawStringF(display, 36, 6, COLOR_CYAN, "L:%d", lives);
        for (int x = 0; x < PANEL_RES_X; x += 2) display->drawPixel(x, HUD_H - 1, COLOR_BLUE);
        
        // Draw bricks
        for (const auto& brick : bricks) {
            if (brick.alive) {
                display->fillRect(brick.x, brick.y, BRICK_WIDTH, BRICK_HEIGHT, brick.color);
            }
        }
        
        // Draw paddle
        display->fillRect(
            (int)paddle.x, 
            paddle.y, 
            paddle.width, 
            paddle.height, 
            paddle.color
        );
        
        // Draw ball
        display->fillRect((int)ball.x - 1, (int)ball.y - 1, BALL_SIZE_PX, BALL_SIZE_PX, ball.color);
    }

    bool isGameOver() override {
        return gameOver || gameWon;
    }

    // ------------------------------
    // Leaderboard integration
    // ------------------------------
    bool leaderboardEnabled() const override { return true; }
    const char* leaderboardId() const override { return "breakout"; }
    const char* leaderboardName() const override { return "Breakout"; }
    uint32_t leaderboardScore() const override { return (score > 0) ? (uint32_t)score : 0u; }
};

