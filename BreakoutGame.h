#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "GameBase.h"
#include "ControllerManager.h"
#include "config.h"
#include "SmallFont.h"

/**
 * BreakoutGame - Classic Breakout/Arkanoid style game
 * Player controls a paddle at the bottom to bounce a ball and break bricks
 */
class BreakoutGame : public GameBase {
private:
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
        
        Paddle() : x(32.0f), y(58), width(12), height(3), speed(2.0f), color(COLOR_CYAN) {}
    };
    
    // Ball structure
    struct Ball {
        float x;
        float y;
        float vx;
        float vy;
        int size;
        uint16_t color;
        
        Ball() : x(32.0f), y(50.0f), vx(1.2f), vy(-1.2f), size(2), color(COLOR_WHITE) {}
    };
    
    std::vector<Brick> bricks;
    Paddle paddle;
    Ball ball;
    bool gameOver;
    bool gameWon;
    int score;
    int lives;
    unsigned long lastUpdate;
    static const int UPDATE_INTERVAL_MS = 16;  // ~60 FPS
    static const int BRICK_ROWS = 4;
    static const int BRICK_COLS = 8;
    static const int BRICK_WIDTH = 7;
    static const int BRICK_HEIGHT = 3;
    static const int BRICK_SPACING = 1;
    
    /**
     * Initialize bricks in rows
     */
    void initBricks() {
        bricks.clear();
        uint16_t colors[] = {COLOR_RED, COLOR_ORANGE, COLOR_YELLOW, COLOR_GREEN};
        
        int startX = 2;
        int startY = 8;
        
        for (int row = 0; row < BRICK_ROWS; row++) {
            for (int col = 0; col < BRICK_COLS; col++) {
                int x = startX + col * (BRICK_WIDTH + BRICK_SPACING);
                int y = startY + row * (BRICK_HEIGHT + BRICK_SPACING);
                bricks.emplace_back(x, y, colors[row % 4]);
            }
        }
    }
    
    /**
     * Reset ball to starting position
     */
    void resetBall() {
        ball.x = paddle.x + paddle.width / 2.0f;
        ball.y = paddle.y - 5.0f;
        ball.vx = (random(0, 2) == 0) ? -1.2f : 1.2f;
        ball.vy = -1.2f;
    }
    
    /**
     * Check collision between ball and rectangle
     */
    bool checkRectCollision(float bx, float by, int rx, int ry, int rw, int rh) {
        return (bx + ball.size >= rx && bx - ball.size <= rx + rw &&
                by + ball.size >= ry && by - ball.size <= ry + rh);
    }

public:
    BreakoutGame() 
        : gameOver(false), gameWon(false), score(0), lives(3), lastUpdate(0) {
        initBricks();
        resetBall();
    }

    void start() override {
        gameOver = false;
        gameWon = false;
        score = 0;
        lives = 3;
        lastUpdate = millis();
        
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
        
        // Update paddle position
        ControllerPtr p1 = input->getController(0);
        if (p1 && p1->isConnected()) {
            uint8_t dpad = p1->dpad();
            if ((dpad & 0x08) && paddle.x > 0) {  // LEFT
                paddle.x -= paddle.speed;
            }
            if ((dpad & 0x04) && paddle.x < PANEL_RES_X - paddle.width) {  // RIGHT
                paddle.x += paddle.speed;
            }
            
            // Launch ball with A button
            if (p1->a() && ball.vy > 0) {
                ball.vy = -abs(ball.vy);
            }
        }
        
        // Update ball position
        ball.x += ball.vx;
        ball.y += ball.vy;
        
        // Ball collision with walls
        if (ball.x - ball.size <= 0 || ball.x + ball.size >= PANEL_RES_X) {
            ball.vx = -ball.vx;
            ball.x = constrain(ball.x, ball.size, PANEL_RES_X - ball.size);
        }
        
        if (ball.y - ball.size <= 0) {
            ball.vy = -ball.vy;
            ball.y = ball.size;
        }
        
        // Ball collision with paddle
        if (checkRectCollision(ball.x, ball.y, (int)paddle.x, paddle.y, paddle.width, paddle.height)) {
            // Calculate hit position on paddle (-1 to 1)
            float hitPos = ((ball.x - paddle.x) / paddle.width) * 2.0f - 1.0f;
            ball.vx = hitPos * 1.5f;
            ball.vy = -abs(ball.vy);
            ball.y = paddle.y - ball.size;
            
            // Normalize velocity
            float speed = sqrt(ball.vx * ball.vx + ball.vy * ball.vy);
            if (speed > 0) {
                ball.vx = (ball.vx / speed) * 1.8f;
                ball.vy = (ball.vy / speed) * 1.8f;
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
        if (ball.y > PANEL_RES_Y) {
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
        
        if (gameOver) {
            SmallFont::drawString(display, 8, 28, "GAME OVER", COLOR_RED);
            char scoreStr[16];
            snprintf(scoreStr, sizeof(scoreStr), "SCORE:%d", score);
            SmallFont::drawString(display, 4, 38, scoreStr, COLOR_WHITE);
            return;
        }
        
        if (gameWon) {
            SmallFont::drawString(display, 12, 28, "YOU WIN!", COLOR_GREEN);
            char scoreStr[16];
            snprintf(scoreStr, sizeof(scoreStr), "SCORE:%d", score);
            SmallFont::drawString(display, 4, 38, scoreStr, COLOR_WHITE);
            return;
        }
        
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
        display->fillRect(
            (int)ball.x - ball.size, 
            (int)ball.y - ball.size, 
            ball.size * 2, 
            ball.size * 2, 
            ball.color
        );
        
        // Draw score and lives with small font
        char scoreStr[12];
        snprintf(scoreStr, sizeof(scoreStr), "S:%d L:%d", score, lives);
        SmallFont::drawString(display, 2, 2, scoreStr, COLOR_YELLOW);
    }

    bool isGameOver() override {
        return gameOver || gameWon;
    }
};

