#pragma once
#include <Arduino.h>
#include <math.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "GameBase.h"
#include "ControllerManager.h"
#include "config.h"
#include "SmallFont.h"

/**
 * PongGame - Classic Pong game implementation
 * Supports 1-2 players with paddle controls
 */
class PongGame : public GameBase {
private:
    // Paddle structure
    struct Paddle {
        int x;
        int y;
        int width;
        int height;
        int speed;
        int score;
        uint16_t color;
        
        Paddle(int xPos, int yPos, int w, int h, uint16_t c) 
            : x(xPos), y(yPos), width(w), height(h), speed(2), score(0), color(c) {}
    };
    
    // Ball structure
    struct Ball {
        float x;
        float y;
        float vx;
        float vy;
        int size;
        uint16_t color;
        
        Ball() : x(32.0f), y(32.0f), vx(1.5f), vy(1.0f), size(2), color(COLOR_WHITE) {}
    };
    
    Paddle leftPaddle;
    Paddle rightPaddle;
    Ball ball;
    bool gameOver;
    bool twoPlayer;
    unsigned long lastUpdate;
    static const int PADDLE_SPEED = 2;
    static const int UPDATE_INTERVAL_MS = 16;  // ~60 FPS
    
    /**
     * Reset ball to center with random direction
     */
    void resetBall() {
        ball.x = PANEL_RES_X / 2.0f;
        ball.y = PANEL_RES_Y / 2.0f;
        ball.vx = (random(0, 2) == 0) ? -1.5f : 1.5f;
        ball.vy = (random(-100, 100) / 100.0f) * 1.0f;
    }
    
    /**
     * Check collision between ball and paddle
     */
    bool checkPaddleCollision(const Paddle& paddle) {
        if (ball.x + ball.size >= paddle.x &&
            ball.x - ball.size <= paddle.x + paddle.width &&
            ball.y + ball.size >= paddle.y &&
            ball.y - ball.size <= paddle.y + paddle.height) {
            return true;
        }
        return false;
    }
    
    /**
     * Update AI paddle (right paddle in single player mode)
     */
    void updateAI() {
        if (!twoPlayer) {
            // Simple AI: follow the ball
            int paddleCenter = rightPaddle.y + rightPaddle.height / 2;
            if (ball.y < paddleCenter - 2) {
                rightPaddle.y = max(0, rightPaddle.y - PADDLE_SPEED);
            } else if (ball.y > paddleCenter + 2) {
                rightPaddle.y = min(PANEL_RES_Y - rightPaddle.height, rightPaddle.y + PADDLE_SPEED);
            }
        }
    }

public:
    PongGame() 
        : leftPaddle(2, PANEL_RES_Y / 2 - 8, 3, 16, COLOR_GREEN),
          rightPaddle(PANEL_RES_X - 5, PANEL_RES_Y / 2 - 8, 3, 16, COLOR_CYAN),
          gameOver(false),
          twoPlayer(false),
          lastUpdate(0) {
        resetBall();
    }

    void start() override {
        gameOver = false;
        lastUpdate = millis();
        
        // Determine if two players based on connected controllers
        twoPlayer = (globalControllerManager->getConnectedCount() >= 2);
        
        // Reset scores and positions
        leftPaddle.score = 0;
        rightPaddle.score = 0;
        leftPaddle.y = PANEL_RES_Y / 2 - leftPaddle.height / 2;
        rightPaddle.y = PANEL_RES_Y / 2 - rightPaddle.height / 2;
        
        resetBall();
    }

    void reset() override {
        start();
    }

    void update(ControllerManager* input) override {
        if (gameOver) return;
        
        // Throttle updates for smooth gameplay
        unsigned long now = millis();
        if (now - lastUpdate < UPDATE_INTERVAL_MS) return;
        lastUpdate = now;
        
        // Update left paddle (Player 1)
        ControllerPtr p1 = input->getController(0);
        if (p1 && p1->isConnected()) {
            uint8_t dpad = p1->dpad();
            if ((dpad & 0x01) && leftPaddle.y > 0) {  // UP
                leftPaddle.y -= PADDLE_SPEED;
            }
            if ((dpad & 0x02) && leftPaddle.y < PANEL_RES_Y - leftPaddle.height) {  // DOWN
                leftPaddle.y += PADDLE_SPEED;
            }
        }
        
        // Update right paddle (Player 2 or AI)
        if (twoPlayer) {
            ControllerPtr p2 = input->getController(1);
            if (p2 && p2->isConnected()) {
                uint8_t dpad = p2->dpad();
                if ((dpad & 0x01) && rightPaddle.y > 0) {  // UP
                    rightPaddle.y -= PADDLE_SPEED;
                }
                if ((dpad & 0x02) && rightPaddle.y < PANEL_RES_Y - rightPaddle.height) {  // DOWN
                    rightPaddle.y += PADDLE_SPEED;
                }
            }
        } else {
            updateAI();
        }
        
        // Update ball position
        ball.x += ball.vx;
        ball.y += ball.vy;
        
        // Ball collision with top/bottom walls
        if (ball.y - ball.size <= 0 || ball.y + ball.size >= PANEL_RES_Y) {
            ball.vy = -ball.vy;
            ball.y = constrain(ball.y, ball.size, PANEL_RES_Y - ball.size);
        }
        
        // Ball collision with paddles
        if (checkPaddleCollision(leftPaddle)) {
            ball.vx = abs(ball.vx);  // Ensure ball goes right
            ball.vy += (ball.y - (leftPaddle.y + leftPaddle.height / 2)) * 0.1f;
            ball.x = leftPaddle.x + leftPaddle.width + ball.size;
            // Normalize velocity
            float speed = sqrt(ball.vx * ball.vx + ball.vy * ball.vy);
            if (speed > 0) {
                ball.vx = (ball.vx / speed) * 1.8f;
                ball.vy = (ball.vy / speed) * 1.8f;
            }
        }
        
        if (checkPaddleCollision(rightPaddle)) {
            ball.vx = -abs(ball.vx);  // Ensure ball goes left
            ball.vy += (ball.y - (rightPaddle.y + rightPaddle.height / 2)) * 0.1f;
            ball.x = rightPaddle.x - ball.size;
            // Normalize velocity
            float speed = sqrt(ball.vx * ball.vx + ball.vy * ball.vy);
            if (speed > 0) {
                ball.vx = (ball.vx / speed) * 1.8f;
                ball.vy = (ball.vy / speed) * 1.8f;
            }
        }
        
        // Score points
        if (ball.x < 0) {
            rightPaddle.score++;
            resetBall();
            if (rightPaddle.score >= 5) {
                gameOver = true;
            }
        } else if (ball.x > PANEL_RES_X) {
            leftPaddle.score++;
            resetBall();
            if (leftPaddle.score >= 5) {
                gameOver = true;
            }
        }
    }

    void draw(MatrixPanel_I2S_DMA* display) override {
        display->fillScreen(COLOR_BLACK);
        
        if (gameOver) {
            // Show winner with small font
            if (leftPaddle.score >= 5) {
                SmallFont::drawString(display, 8, 25, "P1 WINS!", COLOR_RED);
            } else {
                SmallFont::drawString(display, 8, 25, "P2 WINS!", COLOR_RED);
            }
            
            char scoreStr[16];
            snprintf(scoreStr, sizeof(scoreStr), "P1:%d P2:%d", leftPaddle.score, rightPaddle.score);
            SmallFont::drawString(display, 4, 35, scoreStr, COLOR_WHITE);
            return;
        }
        
        // Draw center line
        for (int y = 0; y < PANEL_RES_Y; y += 4) {
            display->drawPixel(PANEL_RES_X / 2, y, COLOR_WHITE);
        }
        
        // Draw paddles
        display->fillRect(
            leftPaddle.x, 
            leftPaddle.y, 
            leftPaddle.width, 
            leftPaddle.height, 
            leftPaddle.color
        );
        
        display->fillRect(
            rightPaddle.x, 
            rightPaddle.y, 
            rightPaddle.width, 
            rightPaddle.height, 
            rightPaddle.color
        );
        
        // Draw ball
        display->fillRect(
            (int)ball.x - ball.size, 
            (int)ball.y - ball.size, 
            ball.size * 2, 
            ball.size * 2, 
            ball.color
        );
        
        // Draw scores with small font
        char scoreLeft[4], scoreRight[4];
        snprintf(scoreLeft, sizeof(scoreLeft), "%d", leftPaddle.score);
        snprintf(scoreRight, sizeof(scoreRight), "%d", rightPaddle.score);
        SmallFont::drawString(display, 4, 2, scoreLeft, leftPaddle.color);
        SmallFont::drawString(display, PANEL_RES_X - 12, 2, scoreRight, rightPaddle.color);
    }

    bool isGameOver() override {
        return gameOver;
    }
};

