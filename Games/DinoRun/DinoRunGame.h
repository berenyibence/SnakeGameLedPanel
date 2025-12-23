#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "../../engine/GameBase.h"
#include "../../engine/ControllerManager.h"
#include "../../engine/config.h"
#include "../../engine/UserProfiles.h"
#include "../../component/SmallFont.h"
#include "../../component/GameOverLeaderboardView.h"

#include "DinoRunGameConfig.h"
#include "DinoRunGameSprites.h"

class DinoRunGame : public GameBase {
private:
    using Cfg = DinoRunConfig;

    struct Obstacle {
        bool active = false;
        float x = 0;
        uint8_t kind = 0;
    };

    float dinoY = (float)Cfg::GROUND_Y - (float)Cfg::DINO_H;
    float dinoVy = 0.0f;
    bool onGround = true;

    Obstacle obs[6];
    float spawnDistLeft = 60.0f;   // pixels to travel until next obstacle spawn
    float distancePx = 0.0f;       // total traveled distance in pixels (for speed ramp + score)

    uint32_t score = 0;
    bool gameOver = false;
    uint32_t lastMs = 0;

    float layerOff[Cfg::LAYER_COUNT] = {0,0,0};

    static inline void drawBitmap(MatrixPanel_I2S_DMA* d, int x, int y, const uint8_t* bits, int w, int h, uint16_t col) {
        for (int yy = 0; yy < h; yy++) {
            for (int xx = 0; xx < w; xx++) {
                const uint8_t v = bits[yy * w + xx];
                if (!v) continue;
                d->drawPixel(x + xx, y + yy, col);
            }
        }
    }

    static inline bool aEdge(ControllerPtr ctl) {
        static bool last = false;
        const bool now = ctl && (bool)ctl->a();
        const bool edge = now && !last;
        last = now;
        return edge;
    }

    void spawnObstacle(float x) {
        for (auto &o : obs) {
            if (o.active) continue;
            o.active = true;
            o.x = x;
            o.kind = 0;
            return;
        }
    }

    void resetSpawnDistance() {
        spawnDistLeft = (float)random((int)Cfg::OBSTACLE_MIN_GAP, (int)Cfg::OBSTACLE_MAX_GAP);
    }

    bool collideDinoObstacle(const Obstacle& o) const {
        if (!o.active) return false;
        const int ox = (int)o.x;
        const int oy = Cfg::GROUND_Y - 10;
        const int ow = 6;
        const int oh = 10;
        const int dx = Cfg::DINO_X;
        const int dy = (int)dinoY;
        const int dw = Cfg::DINO_W;
        const int dh = Cfg::DINO_H;
        return !(dx + dw <= ox || ox + ow <= dx || dy + dh <= oy || oy + oh <= dy);
    }

public:
    void start() override {
        randomSeed((uint32_t)micros() ^ (uint32_t)millis());
        dinoY = (float)Cfg::GROUND_Y - (float)Cfg::DINO_H;
        dinoVy = 0;
        onGround = true;
        for (auto &o : obs) o.active = false;
        distancePx = 0.0f;
        resetSpawnDistance();
        score = 0;
        gameOver = false;
        lastMs = millis();
        for (int i = 0; i < Cfg::LAYER_COUNT; i++) layerOff[i] = 0.0f;
    }

    void reset() override { start(); }

    void update(ControllerManager* input) override {
        if (gameOver) return;
        const uint32_t now = millis();
        const float dt = min(0.05f, (float)(now - lastMs) / 1000.0f);
        lastMs = now;
        // Normalize to a ~60fps timestep so the game plays consistently regardless of loop rate.
        const float step = dt * 60.0f;

        ControllerPtr ctl = input ? input->getController(0) : nullptr;
        if (ctl && (aEdge(ctl) || (ctl->dpad() & 0x01))) {
            if (onGround) {
                dinoVy = Cfg::JUMP_VY;
                onGround = false;
            }
        }

        // Physics
        dinoVy += Cfg::GRAVITY * step;
        if (dinoVy > Cfg::MAX_FALL_VY) dinoVy = Cfg::MAX_FALL_VY;
        dinoY += dinoVy * step;
        const float groundY = (float)Cfg::GROUND_Y - (float)Cfg::DINO_H;
        if (dinoY >= groundY) {
            dinoY = groundY;
            dinoVy = 0;
            onGround = true;
        }

        // Speed: start slow, then ramp up very gradually with distance.
        const float speed = (float)Cfg::BASE_SPEED_PX
            + min((float)Cfg::MAX_SPEED_BONUS_PX, distancePx * (float)Cfg::SPEEDUP_PER_PX);
        const float move = speed * step;

        // Obstacles move
        for (auto &o : obs) {
            if (!o.active) continue;
            o.x -= move;
            if (o.x < -10) o.active = false;
        }
        // Spawn pacing: distance-based spacing (prevents "instant wall of obstacles").
        spawnDistLeft -= move;
        if (spawnDistLeft <= 0.0f) {
            spawnObstacle((float)PANEL_RES_X + 10.0f);
            resetSpawnDistance();
        }

        for (auto &o : obs) {
            if (collideDinoObstacle(o)) {
                gameOver = true;
                break;
            }
        }

        // Parallax offsets
        for (int i = 0; i < Cfg::LAYER_COUNT; i++) {
            layerOff[i] += move * Cfg::layerSpeed((uint8_t)i);
            if (layerOff[i] > 64.0f) layerOff[i] -= 64.0f;
        }

        // Score by distance
        distancePx += move;
        const uint32_t newScore = (uint32_t)distancePx;
        if (newScore > score) score = newScore;
    }

    void draw(MatrixPanel_I2S_DMA* d) override {
        d->fillScreen(COLOR_BLACK);
        if (gameOver) {
            char tag[4];
            UserProfiles::getPadTag(0, tag);
            GameOverLeaderboardView::draw(d, "GAME OVER", leaderboardId(), leaderboardScore(), tag);
            return;
        }

        // HUD
        SmallFont::drawString(d, 2, 6, "DINO", COLOR_CYAN);
        SmallFont::drawStringF(d, 40, 6, COLOR_YELLOW, "%lu", (unsigned long)score);
        for (int x = 0; x < PANEL_RES_X; x += 2) d->drawPixel(x, Cfg::HUD_H - 1, COLOR_BLUE);

        // Parallax background (simple pixel art bands)
        for (int i = 0; i < Cfg::LAYER_COUNT; i++) {
            const int y = Cfg::HUD_H + 6 + i * 10;
            const uint16_t col = (i == 0) ? d->color565(30, 140, 80) : (i == 1) ? d->color565(30, 80, 150) : d->color565(140, 60, 160);
            for (int x = 0; x < PANEL_RES_X; x += (6 - i)) {
                const int xx = (int)(x - (int)layerOff[i]);
                const int px = (xx % 64 + 64) % 64;
                d->drawPixel(px, y, col);
                if (i == 2) d->drawPixel(px, y + 1, col);
            }
        }

        // Ground
        const uint16_t gcol = d->color565(90, 220, 90);
        d->drawFastHLine(0, Cfg::GROUND_Y, PANEL_RES_X, gcol);
        for (int x = 0; x < PANEL_RES_X; x += 4) d->drawPixel((x + (int)(score / 10)) % 64, Cfg::GROUND_Y + 1, d->color565(40, 150, 40));

        // Obstacles
        for (auto &o : obs) {
            if (!o.active) continue;
            const int ox = (int)o.x;
            const int oy = Cfg::GROUND_Y - 10;
            drawBitmap(d, ox, oy, (const uint8_t*)CACTUS_0, 6, 10, COLOR_GREEN);
        }

        // Dino (animated run)
        const uint8_t frame = (uint8_t)((millis() / 140) % 2);
        const uint16_t dcol = d->color565(240, 240, 240);
        drawBitmap(d, Cfg::DINO_X, (int)dinoY, (const uint8_t*)(frame ? DINO_RUN_1 : DINO_RUN_0), 10, 10, dcol);
    }

    bool isGameOver() override { return gameOver; }

    bool leaderboardEnabled() const override { return true; }
    const char* leaderboardId() const override { return "dino"; }
    const char* leaderboardName() const override { return "Dino"; }
    uint32_t leaderboardScore() const override { return score; }
};


