#pragma once
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "ControllerManager.h"
#include "config.h"

class GameBase {
public:
    virtual void start() = 0;
    virtual void update(ControllerManager* input) = 0;
    virtual void draw(MatrixPanel_I2S_DMA* display) = 0;
    virtual bool isGameOver() = 0;
    virtual void reset() = 0;

    /**
     * Preferred render FPS for this game.
     *
     * Why: Some games (like Snake) update state at a slow tick rate but can be
     * expensive to redraw. Redrawing them at 60 FPS wastes CPU and increases
     * the chance of HUB75 visual artifacts (ghosting/tearing) from heavy writes.
     *
     * Default: use the global game render FPS.
     */
    virtual uint16_t preferredRenderFps() const { return GAME_RENDER_FPS; }
    virtual ~GameBase() {}
};
