#pragma once
#include <Arduino.h>
#include <math.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "GameBase.h"
#include "ControllerManager.h"
#include "config.h"
#include "SmallFont.h"
#include "Settings.h"

/**
 * PongGame - Classic Pong game implementation
 * Supports 1-2 players with paddle controls
 */
class PongGame : public GameBase {
private:
    // ---------------------------------------------------------
    // Analog helpers (Bluepad32 API varies across versions/controllers)
    // ---------------------------------------------------------
    struct InputDetail {
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

    // Paddle structure
    struct Paddle {
        int x;
        float y;
        int width;
        int height;
        int score;
        uint16_t color;
        
        Paddle(int xPos, int yPos, int w, int h, uint16_t c) 
            : x(xPos), y((float)yPos), width(w), height(h), score(0), color(c) {}
    };
    
    // Ball structure
    struct Ball {
        float x;
        float y;
        float vx;
        float vy;
        uint16_t color;
        
        Ball() : x(32.0f), y(32.0f), vx(1.5f), vy(1.0f), color(COLOR_WHITE) {}
    };
    
    Paddle leftPaddle;
    Paddle rightPaddle;
    Ball ball;
    bool gameOver;
    bool twoPlayer;
    unsigned long lastUpdate;
    static const int UPDATE_INTERVAL_MS = 16;  // ~60 FPS

    // Gameplay tuning
    static constexpr int BALL_SIZE_PX = 2;            // drawn size (minimum 2x2 as requested)
    static constexpr float BALL_HALF = 1.0f;          // half-size for collision checks (center-based)
    // NOTE (Arduino/ESP32 toolchain): avoid `static constexpr float` class members that can
    // require out-of-class definitions and cause linker errors. Use inline getters instead.
    static inline float ballStartSpeed() { return 0.95f; } // slower start speed
    static inline float ballMaxSpeed() { return 1.35f; }   // cap to keep it playable on 64x64
    static constexpr float PLAYER_SPEED = 2.4f;       // px per tick at full stick
    static constexpr float STICK_DEADZONE = 0.18f;    // 0..1
    static constexpr int16_t AXIS_DIVISOR = 512;      // Bluepad32 commonly ~[-512..512]

    // CPU difficulty (intentionally beatable)
    static constexpr uint16_t AI_THINK_MS = 70;       // reaction delay
    static constexpr float AI_SPEED = 1.4f;           // slower than player
    static constexpr int AI_ERROR_PX = 7;             // aim error range (+/-)
    uint32_t lastAiThinkMs = 0;
    float aiAimY = 32.0f;

    // Round flow: visual feedback + countdown between points (and on start).
    enum RoundPhase : uint8_t { PHASE_COUNTDOWN, PHASE_PLAYING, PHASE_POINT_FLASH };
    RoundPhase phase = PHASE_COUNTDOWN;
    uint32_t phaseStartMs = 0;
    uint8_t lastPointWinner = 0; // 0=left, 1=right
    static constexpr uint16_t POINT_FLASH_MS = 450;
    static constexpr uint16_t COUNTDOWN_MS = 3000;
    
    /**
     * Reset ball to center with random direction
     */
    void resetBall(int serveDir /* -1 left, +1 right */) {
        ball.x = PANEL_RES_X / 2.0f;
        ball.y = PANEL_RES_Y / 2.0f;
        ball.vx = (serveDir >= 0) ? ballStartSpeed() : -ballStartSpeed();
        ball.vy = (random(-100, 100) / 100.0f) * 0.55f;
    }
    
    /**
     * Check collision between ball and paddle
     */
    bool checkPaddleCollision(const Paddle& paddle) {
        if (ball.x + BALL_HALF >= paddle.x &&
            ball.x - BALL_HALF <= paddle.x + paddle.width &&
            ball.y + BALL_HALF >= paddle.y &&
            ball.y - BALL_HALF <= paddle.y + paddle.height) {
            return true;
        }
        return false;
    }
    
    /**
     * Update AI paddle (right paddle in single player mode)
     */
    void updateAI(uint32_t now) {
        if (!twoPlayer) {
            // Beatable CPU:
            // - Only "thinks" every AI_THINK_MS (reaction delay)
            // - Adds aiming error so it sometimes misses
            // - Tracks the ball mainly when it is moving towards the CPU
            if ((uint32_t)(now - lastAiThinkMs) >= AI_THINK_MS) {
                lastAiThinkMs = now;

                const float centerY = rightPaddle.y + rightPaddle.height / 2.0f;
                if (ball.vx > 0.0f) {
                    aiAimY = ball.y + (float)random(-AI_ERROR_PX, AI_ERROR_PX + 1);
                } else {
                    // When ball moves away, drift to center with slight wobble.
                    aiAimY = (PANEL_RES_Y / 2.0f) + (float)random(-2, 3);
                }

                const float dead = 1.2f;
                if (aiAimY < centerY - dead) rightPaddle.y -= AI_SPEED;
                else if (aiAimY > centerY + dead) rightPaddle.y += AI_SPEED;
            }

            rightPaddle.y = clampf(rightPaddle.y, 0.0f, (float)(PANEL_RES_Y - rightPaddle.height));
        }
    }

public:
    PongGame() 
        : leftPaddle(2, PANEL_RES_Y / 2 - 6, 1, 12, COLOR_GREEN),
          rightPaddle(PANEL_RES_X - 3, PANEL_RES_Y / 2 - 6, 1, 12, COLOR_CYAN),
          gameOver(false),
          twoPlayer(false),
          lastUpdate(0) {
        resetBall(1);
    }

    void start() override {
        gameOver = false;
        lastUpdate = millis();
        lastAiThinkMs = 0;
        aiAimY = PANEL_RES_Y / 2.0f;
        phase = PHASE_COUNTDOWN;
        phaseStartMs = lastUpdate;
        lastPointWinner = 0;
        
        // Determine if two players based on connected controllers
        twoPlayer = (globalControllerManager->getConnectedCount() >= 2);

        // Apply current global player color for Player 1 (left paddle).
        leftPaddle.color = globalSettings.getPlayerColor();
        
        // Reset scores and positions
        leftPaddle.score = 0;
        rightPaddle.score = 0;
        leftPaddle.y = (float)(PANEL_RES_Y / 2 - leftPaddle.height / 2);
        rightPaddle.y = (float)(PANEL_RES_Y / 2 - rightPaddle.height / 2);
        
        // Countdown on start, then serve to the right.
        resetBall(+1);
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

        // -----------------------------------------------------
        // Round phases (flash -> countdown -> play)
        // -----------------------------------------------------
        if (phase == PHASE_POINT_FLASH) {
            if ((uint32_t)(now - phaseStartMs) >= POINT_FLASH_MS) {
                phase = PHASE_COUNTDOWN;
                phaseStartMs = now;
                // Serve towards the player who conceded the point (so they receive).
                const int serveDir = (lastPointWinner == 0) ? -1 : +1;
                resetBall(serveDir);
            }
            return;
        }
        if (phase == PHASE_COUNTDOWN) {
            if ((uint32_t)(now - phaseStartMs) >= COUNTDOWN_MS) {
                phase = PHASE_PLAYING;
                phaseStartMs = now;
            }
            // Still allow paddle movement during countdown.
        }
        
        // Update left paddle (Player 1) - analog stick (fallback to dpad)
        ControllerPtr p1 = input->getController(0);
        if (p1 && p1->isConnected()) {
            const float raw = clampf((float)InputDetail::axisY(p1, 0) / (float)AXIS_DIVISOR, -1.0f, 1.0f);
            float sy = deadzone01(raw, STICK_DEADZONE);

            if (sy == 0.0f) {
                const uint8_t dpad = p1->dpad();
                if (dpad & 0x01) sy = -1.0f; // UP
                else if (dpad & 0x02) sy = 1.0f; // DOWN
            }

            leftPaddle.y += sy * PLAYER_SPEED;
            leftPaddle.y = clampf(leftPaddle.y, 0.0f, (float)(PANEL_RES_Y - leftPaddle.height));
        }
        
        // Update right paddle (Player 2 or AI) - analog stick for player 2
        if (twoPlayer) {
            ControllerPtr p2 = input->getController(1);
            if (p2 && p2->isConnected()) {
                const float raw = clampf((float)InputDetail::axisY(p2, 0) / (float)AXIS_DIVISOR, -1.0f, 1.0f);
                float sy = deadzone01(raw, STICK_DEADZONE);

                if (sy == 0.0f) {
                    const uint8_t dpad = p2->dpad();
                    if (dpad & 0x01) sy = -1.0f; // UP
                    else if (dpad & 0x02) sy = 1.0f; // DOWN
                }

                rightPaddle.y += sy * PLAYER_SPEED;
                rightPaddle.y = clampf(rightPaddle.y, 0.0f, (float)(PANEL_RES_Y - rightPaddle.height));
            }
        } else {
            updateAI((uint32_t)now);
        }

        // During countdown we don't move the ball.
        if (phase != PHASE_PLAYING) return;
        
        // Update ball position
        ball.x += ball.vx;
        ball.y += ball.vy;
        
        // Ball collision with top/bottom walls
        if (ball.y - BALL_HALF <= 0 || ball.y + BALL_HALF >= PANEL_RES_Y) {
            ball.vy = -ball.vy;
            ball.y = constrain(ball.y, BALL_HALF, PANEL_RES_Y - BALL_HALF);
        }
        
        // Ball collision with paddles
        if (checkPaddleCollision(leftPaddle)) {
            ball.vx = abs(ball.vx);  // Ensure ball goes right
            ball.vy += (ball.y - (leftPaddle.y + leftPaddle.height / 2.0f)) * 0.09f;
            ball.x = leftPaddle.x + leftPaddle.width + BALL_HALF;
            // Normalize velocity
            float speed = sqrt(ball.vx * ball.vx + ball.vy * ball.vy);
            if (speed > 0) {
                float target = clampf(speed, ballStartSpeed(), ballMaxSpeed());
                ball.vx = (ball.vx / speed) * target;
                ball.vy = (ball.vy / speed) * target;
            }
        }
        
        if (checkPaddleCollision(rightPaddle)) {
            ball.vx = -abs(ball.vx);  // Ensure ball goes left
            ball.vy += (ball.y - (rightPaddle.y + rightPaddle.height / 2.0f)) * 0.09f;
            ball.x = rightPaddle.x - BALL_HALF;
            // Normalize velocity
            float speed = sqrt(ball.vx * ball.vx + ball.vy * ball.vy);
            if (speed > 0) {
                float target = clampf(speed, ballStartSpeed(), ballMaxSpeed());
                ball.vx = (ball.vx / speed) * target;
                ball.vy = (ball.vy / speed) * target;
            }
        }
        
        // Score points
        if (ball.x < -BALL_HALF) {
            rightPaddle.score++;
            if (rightPaddle.score >= 5) {
                gameOver = true;
            } else {
                lastPointWinner = 1;
                phase = PHASE_POINT_FLASH;
                phaseStartMs = now;
            }
        } else if (ball.x > PANEL_RES_X + BALL_HALF) {
            leftPaddle.score++;
            if (leftPaddle.score >= 5) {
                gameOver = true;
            } else {
                lastPointWinner = 0;
                phase = PHASE_POINT_FLASH;
                phaseStartMs = now;
            }
        }
    }

    void draw(MatrixPanel_I2S_DMA* display) override {
        display->fillScreen(COLOR_BLACK);
        
        if (gameOver) {
            // Show winner with small font
            if (leftPaddle.score >= 5) {
                SmallFont::drawString(display, 10, 25, twoPlayer ? "P1 WINS!" : "YOU WIN!", COLOR_RED);
            } else {
                SmallFont::drawString(display, 10, 25, twoPlayer ? "P2 WINS!" : "CPU WINS!", COLOR_RED);
            }
            
            char scoreStr[16];
            snprintf(scoreStr, sizeof(scoreStr), twoPlayer ? "P1:%d P2:%d" : "P1:%d CPU:%d", leftPaddle.score, rightPaddle.score);
            SmallFont::drawString(display, 4, 35, scoreStr, COLOR_WHITE);
            return;
        }

        // HUD
        if (twoPlayer) {
            SmallFont::drawStringF(display, 2, 6, leftPaddle.color, "P1:%d", leftPaddle.score);
            SmallFont::drawStringF(display, 38, 6, rightPaddle.color, "P2:%d", rightPaddle.score);
        } else {
            SmallFont::drawStringF(display, 2, 6, leftPaddle.color, "P1:%d", leftPaddle.score);
            SmallFont::drawStringF(display, 38, 6, COLOR_CYAN, "CPU:%d", rightPaddle.score);
        }
        
        // Draw center line
        for (int y = 0; y < PANEL_RES_Y; y += 4) {
            display->drawPixel(PANEL_RES_X / 2, y, COLOR_WHITE);
        }
        
        // Draw paddles
        display->fillRect(
            leftPaddle.x, 
            (int)leftPaddle.y, 
            leftPaddle.width, 
            leftPaddle.height, 
            leftPaddle.color
        );
        
        display->fillRect(
            rightPaddle.x, 
            (int)rightPaddle.y, 
            rightPaddle.width, 
            rightPaddle.height, 
            rightPaddle.color
        );
        
        // Point feedback + countdown overlay
        if (phase == PHASE_POINT_FLASH) {
            // Flash the side that conceded the point.
            const uint16_t flashColor = COLOR_RED;
            if (lastPointWinner == 0) {
                display->fillRect(PANEL_RES_X / 2, 0, PANEL_RES_X / 2, PANEL_RES_Y, flashColor);
            } else {
                display->fillRect(0, 0, PANEL_RES_X / 2, PANEL_RES_Y, flashColor);
            }
            SmallFont::drawString(display, 22, 30, "MISS", COLOR_WHITE);
            return;
        }

        if (phase == PHASE_COUNTDOWN) {
            const uint32_t now = millis();
            const uint32_t elapsed = (uint32_t)(now - phaseStartMs);
            int secsLeft = 3 - (int)(elapsed / 1000UL);
            if (secsLeft < 1) secsLeft = 1;

            char c[2] = { (char)('0' + secsLeft), '\0' };
            SmallFont::drawString(display, 30, 30, c, COLOR_YELLOW);
            // Draw the ball in its serve position so the player sees where it'll start.
            display->fillRect((int)ball.x - 1, (int)ball.y - 1, BALL_SIZE_PX, BALL_SIZE_PX, ball.color);
            return;
        }

        // Draw ball (2x2)
        display->fillRect((int)ball.x - 1, (int)ball.y - 1, BALL_SIZE_PX, BALL_SIZE_PX, ball.color);
    }

    bool isGameOver() override {
        return gameOver;
    }

    // ------------------------------
    // Leaderboard integration
    // ------------------------------
    bool leaderboardEnabled() const override { return true; }
    const char* leaderboardId() const override { return "pong"; }
    const char* leaderboardName() const override { return "Pong"; }
    uint32_t leaderboardScore() const override {
        // Submit the higher score (covers 1P and 2P).
        const int best = (leftPaddle.score > rightPaddle.score) ? leftPaddle.score : rightPaddle.score;
        return (best > 0) ? (uint32_t)best : 0u;
    }
};

