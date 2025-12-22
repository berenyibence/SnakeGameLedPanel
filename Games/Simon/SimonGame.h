#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "../../engine/GameBase.h"
#include "../../engine/ControllerManager.h"
#include "../../engine/config.h"
#include "../../engine/Settings.h"
#include "../../engine/UserProfiles.h"
#include "../../engine/AudioManager.h"
#include "../../component/SmallFont.h"
#include "../../component/GameOverLeaderboardView.h"

#include "SimonGameConfig.h"
#include "SimonGameAudio.h"

/**
 * SimonGame - "Simon Says" style memory game.
 *
 * Difficulty:
 * - Easy:   X, Y, A, B
 * - Medium: + LB, RB (shown at top corners, dedicated colors)
 * - Hard:   + D-pad Up/Down/Left/Right (shown as 4x4 edge indicators)
 *
 * Scoring:
 * - Score = maximum sequence length completed in this run.
 * - Submitted to leaderboard on game over.
 */
class SimonGame : public GameBase {
public:
    enum Difficulty : uint8_t { DIFF_EASY = 0, DIFF_MEDIUM = 1, DIFF_HARD = 2 };

    enum Symbol : uint8_t {
        SYM_X = 0,
        SYM_Y,
        SYM_A,
        SYM_B,
        SYM_LB,
        SYM_RB,
        SYM_UP,
        SYM_DOWN,
        SYM_LEFT,
        SYM_RIGHT,
        SYM_NONE = 255
    };

private:
    enum Phase : uint8_t {
        PHASE_READY,
        PHASE_SHOW,
        PHASE_INPUT,
        PHASE_BETWEEN,
        PHASE_ERROR,
        PHASE_GAME_OVER
    };

    // -----------------------------------------------------
    // Input helpers (Bluepad32 API can vary across versions/controllers)
    // -----------------------------------------------------
    struct InputDetail {
        template <typename T>
        static auto l1(T* c, int) -> decltype(c->l1(), bool()) { return (bool)c->l1(); }
        template <typename T>
        static bool l1(T*, ...) { return false; }

        template <typename T>
        static auto r1(T* c, int) -> decltype(c->r1(), bool()) { return (bool)c->r1(); }
        template <typename T>
        static bool r1(T*, ...) { return false; }

        // Some Bluepad32 versions/controllers expose LB/RB as lb()/rb()
        template <typename T>
        static auto lb(T* c, int) -> decltype(c->lb(), bool()) { return (bool)c->lb(); }
        template <typename T>
        static bool lb(T*, ...) { return false; }

        template <typename T>
        static auto rb(T* c, int) -> decltype(c->rb(), bool()) { return (bool)c->rb(); }
        template <typename T>
        static bool rb(T*, ...) { return false; }
    };

    // -----------------------------------------------------
    // State
    // -----------------------------------------------------
    Difficulty difficulty = DIFF_EASY;
    Phase phase = PHASE_READY;
    bool gameOver = false;

    // Lives
    uint8_t maxLives = 3;
    uint8_t lives = 3;

    // Sequence and progress
    uint8_t seq[SimonGameConfig::MAX_SEQUENCE] = {};
    uint16_t seqLen = 0;          // current round length
    uint16_t inputIndex = 0;      // how many inputs matched this round
    uint16_t bestScore = 0;
    bool roundHadMistake = false;

    // Timing
    uint32_t phaseStartMs = 0;
    uint32_t nextStepMs = 0;
    uint16_t showIndex = 0;
    bool showOn = false;

    // Currently highlighted symbol (for show/press flash)
    Symbol activeSym = SYM_NONE;
    uint32_t activeUntilMs = 0;

    // Show-phase brightness animation
    uint8_t showIntensity = 0; // 0..255

    // Success pulse (expanding outline 3-4 steps)
    struct Pulse {
        bool active = false;
        Symbol sym = SYM_NONE;
        uint32_t startMs = 0;
    } pulse;

    // Error animation
    Symbol errorSym = SYM_NONE; // last wrong press
    uint32_t errorStartMs = 0;

    // Input edge tracking (P1 only)
    bool lastA = false, lastB = false, lastX = false, lastY = false;
    bool lastLB = false, lastRB = false;
    uint8_t lastDpad = 0;

    // -----------------------------------------------------
    // Helpers
    // -----------------------------------------------------
    static inline uint16_t dim565(uint16_t c, uint8_t amount /*0..255*/) {
        // Linear dimming in RGB565 space (cheap + good enough for LED panel UI).
        // amount=255 -> unchanged; amount=0 -> black.
        const uint16_t r = (uint16_t)((c >> 11) & 0x1F);
        const uint16_t g = (uint16_t)((c >> 5) & 0x3F);
        const uint16_t b = (uint16_t)(c & 0x1F);
        const uint16_t rr = (uint16_t)((r * amount) / 255);
        const uint16_t gg = (uint16_t)((g * amount) / 255);
        const uint16_t bb = (uint16_t)((b * amount) / 255);
        return (uint16_t)((rr << 11) | (gg << 5) | bb);
    }

    static inline uint32_t speedScaleMs(uint32_t ms) {
        // globalSettings.getGameSpeed() is 1..5; higher should be faster.
        const uint8_t sp = globalSettings.getGameSpeed();
        if (sp <= 1) return ms;
        const uint32_t scaled = ms / (uint32_t)sp;
        return (scaled < 30) ? 30 : scaled; // avoid too-fast flicker on a buzzer/panel
    }

    uint8_t symbolCountForDifficulty() const {
        if (difficulty == DIFF_EASY) return 4;
        if (difficulty == DIFF_MEDIUM) return 6;
        return 10;
    }

    static inline uint16_t toneFor(Symbol s) {
        using namespace SimonGameAudio;
        switch (s) {
            case SYM_A: return TONE_A;
            case SYM_B: return TONE_B;
            case SYM_X: return TONE_X;
            case SYM_Y: return TONE_Y;
            case SYM_LB: return TONE_LB;
            case SYM_RB: return TONE_RB;
            case SYM_UP: return TONE_UP;
            case SYM_DOWN: return TONE_DOWN;
            case SYM_LEFT: return TONE_LEFT;
            case SYM_RIGHT: return TONE_RIGHT;
            default: return 0;
        }
    }

    static inline uint16_t colorFor(Symbol s) {
        using namespace SimonGameConfig;
        switch (s) {
            case SYM_A: return COL_A;
            case SYM_B: return COL_B;
            case SYM_X: return COL_X;
            case SYM_Y: return COL_Y;
            case SYM_LB: return COL_LB;
            case SYM_RB: return COL_RB;
            case SYM_UP:
            case SYM_DOWN:
            case SYM_LEFT:
            case SYM_RIGHT:
                return COL_DPAD;
            default: return COLOR_WHITE;
        }
    }

    static inline bool dpadUp(uint8_t d) { return (d & 0x01) != 0; }
    static inline bool dpadDown(uint8_t d) { return (d & 0x02) != 0; }
    static inline bool dpadRight(uint8_t d) { return (d & 0x04) != 0; }
    static inline bool dpadLeft(uint8_t d) { return (d & 0x08) != 0; }

    void clearInputEdges() {
        lastA = lastB = lastX = lastY = false;
        lastLB = lastRB = false;
        lastDpad = 0;
    }

    void addRandomSymbol() {
        const uint8_t n = symbolCountForDifficulty();
        if (seqLen >= SimonGameConfig::MAX_SEQUENCE) return;
        const uint8_t r = (uint8_t)random(0, (int)n);
        seq[seqLen++] = r; // map index directly to Symbol enum order (X,Y,A,B,LB,RB,UP,DOWN,LEFT,RIGHT)
    }

    void startNewRun(uint32_t now) {
        gameOver = false;
        phase = PHASE_READY;
        phaseStartMs = now;
        nextStepMs = 0;
        showIndex = 0;
        showOn = false;
        activeSym = SYM_NONE;
        activeUntilMs = 0;
        showIntensity = 0;
        pulse.active = false;
        errorSym = SYM_NONE;
        errorStartMs = 0;
        seqLen = 0;
        inputIndex = 0;
        bestScore = 0;
        roundHadMistake = false;
        clearInputEdges();
        addRandomSymbol(); // start at length 1
    }

    void beginShowPhase(uint32_t now) {
        phase = PHASE_SHOW;
        phaseStartMs = now;
        showIndex = 0;
        showOn = false;
        nextStepMs = now; // start immediately
        activeSym = SYM_NONE;
        activeUntilMs = 0;
        showIntensity = 0;
    }

    void beginInputPhase(uint32_t now) {
        phase = PHASE_INPUT;
        phaseStartMs = now;
        inputIndex = 0;
        activeSym = SYM_NONE;
        activeUntilMs = 0;
        showIntensity = 0;
        clearInputEdges(); // avoid "carry-in" from SHOW phase
    }

    void setActive(Symbol s, uint16_t toneMs, uint32_t now, bool startPulse) {
        activeSym = s;
        activeUntilMs = now + speedScaleMs(SimonGameConfig::PRESS_FLASH_MS);
        const uint16_t f = toneFor(s);
        if (f != 0) globalAudio.playTone(f, speedScaleMs(toneMs));
        if (startPulse) {
            pulse.active = true;
            pulse.sym = s;
            pulse.startMs = now;
        }
    }

    Symbol readEdgeSymbol(ControllerPtr ctl) {
        if (!ctl) return SYM_NONE;

        const bool aNow = (bool)ctl->a();
        const bool bNow = (bool)ctl->b();
        const bool xNow = (bool)ctl->x();
        const bool yNow = (bool)ctl->y();

        const bool aEdge = aNow && !lastA;
        const bool bEdge = bNow && !lastB;
        const bool xEdge = xNow && !lastX;
        const bool yEdge = yNow && !lastY;

        lastA = aNow; lastB = bNow; lastX = xNow; lastY = yNow;

        if (aEdge) return SYM_A;
        if (bEdge) return SYM_B;
        if (xEdge) return SYM_X;
        if (yEdge) return SYM_Y;

        if (difficulty >= DIFF_MEDIUM) {
            const bool lbNow = InputDetail::l1(ctl, 0) || InputDetail::lb(ctl, 0);
            const bool rbNow = InputDetail::r1(ctl, 0) || InputDetail::rb(ctl, 0);
            const bool lbEdge = lbNow && !lastLB;
            const bool rbEdge = rbNow && !lastRB;
            lastLB = lbNow; lastRB = rbNow;
            if (lbEdge) return SYM_LB;
            if (rbEdge) return SYM_RB;
        }

        if (difficulty >= DIFF_HARD) {
            const uint8_t d = ctl->dpad();
            const bool upEdge = dpadUp(d) && !dpadUp(lastDpad);
            const bool downEdge = dpadDown(d) && !dpadDown(lastDpad);
            const bool leftEdge = dpadLeft(d) && !dpadLeft(lastDpad);
            const bool rightEdge = dpadRight(d) && !dpadRight(lastDpad);
            lastDpad = d;

            if (upEdge) return SYM_UP;
            if (downEdge) return SYM_DOWN;
            if (leftEdge) return SYM_LEFT;
            if (rightEdge) return SYM_RIGHT;
        }

        return SYM_NONE;
    }

    // -----------------------------------------------------
    // Drawing helpers (Xbox-ish layout)
    // -----------------------------------------------------
    struct Rect { int x; int y; int w; int h; };

    static inline void faceCenter(Symbol s, int& cx, int& cy) {
        using namespace SimonGameConfig;
        cx = FACE_CX;
        cy = FACE_CY;
        const int g = FACE_GAP + FACE_R;
        // Y(top), A(bottom), X(left), B(right)
        if (s == SYM_Y) { cy -= g; return; }
        if (s == SYM_A) { cy += g; return; }
        if (s == SYM_X) { cx -= g; return; }
        if (s == SYM_B) { cx += g; return; }
    }

    static Rect shoulderRect(Symbol s) {
        using namespace SimonGameConfig;
        if (s == SYM_LB) return { SHOULDER_X_PAD, SHOULDER_Y, SHOULDER_W, SHOULDER_H };
        if (s == SYM_RB) return { PANEL_RES_X - SHOULDER_X_PAD - SHOULDER_W, SHOULDER_Y, SHOULDER_W, SHOULDER_H };
        return { 0, 0, 0, 0 };
    }

    static Rect dpadBandRect(Symbol s) {
        using namespace SimonGameConfig;
        const int t = DPAD_BAND;
        if (s == SYM_UP) return { 0, 0, PANEL_RES_X, t };
        if (s == SYM_DOWN) return { 0, PANEL_RES_Y - t, PANEL_RES_X, t };
        if (s == SYM_LEFT) return { 0, 0, t, PANEL_RES_Y };
        if (s == SYM_RIGHT) return { PANEL_RES_X - t, 0, t, PANEL_RES_Y };
        return { 0, 0, 0, 0 };
    }

    static uint16_t labelColorFor(uint16_t baseColor) {
        // High-contrast label color. Yellow/white/orange need black; most other fills can use white.
        if (baseColor == COLOR_YELLOW || baseColor == COLOR_WHITE || baseColor == COLOR_ORANGE) return COLOR_BLACK;
        return COLOR_WHITE;
    }

    static void drawButton(MatrixPanel_I2S_DMA* d, const Rect& r, uint16_t baseColor, const char* label, bool active) {
        if (!d || r.w <= 0 || r.h <= 0) return;
        const uint16_t fill = active ? baseColor : dim565(baseColor, 90);
        const uint16_t border = active ? COLOR_WHITE : dim565(baseColor, 180);
        d->fillRect(r.x, r.y, r.w, r.h, fill);
        d->drawRect(r.x, r.y, r.w, r.h, border);
        if (label && label[0]) {
            // Center label approximately (TomThumb is tiny; ~4px advance per char in this project).
            int len = 0;
            while (label[len] != '\0' && len < 8) len++;
            const int textW = len * 4;
            const int lx = r.x + max(0, (r.w - textW) / 2);
            const int ly = r.y + (r.h / 2) + 2;
            SmallFont::drawString(d, lx, ly, label, labelColorFor(baseColor));
        }
    }

    static void drawPill(MatrixPanel_I2S_DMA* d,
                         const Rect& r,
                         uint16_t baseColor,
                         const char* label,
                         bool active,
                         uint8_t intensity /*0..255*/,
                         bool errorTint) {
        if (!d || r.w <= 0 || r.h <= 0) return;
        const uint16_t col = errorTint ? COLOR_RED : baseColor;
        const uint8_t amt = active ? intensity : 90;
        const uint16_t fill = dim565(col, amt);
        const uint16_t border = active ? COLOR_WHITE : dim565(col, 180);
        const int rad = max(1, r.h / 2);
        d->fillRoundRect(r.x, r.y, r.w, r.h, rad, fill);
        d->drawRoundRect(r.x, r.y, r.w, r.h, rad, border);
        if (label && label[0]) {
            int len = 0;
            while (label[len] != '\0' && len < 8) len++;
            const int textW = len * 4;
            const int lx = r.x + max(0, (r.w - textW) / 2);
            const int ly = r.y + (r.h / 2) + 2;
            SmallFont::drawString(d, lx, ly, label, labelColorFor(col));
        }
    }

    static void drawFaceCircle(MatrixPanel_I2S_DMA* d, Symbol s, uint16_t baseColor, const char* label, bool active, uint8_t intensity /*0..255*/, bool errorTint) {
        if (!d) return;
        using namespace SimonGameConfig;
        int cx, cy;
        faceCenter(s, cx, cy);
        const uint16_t col = errorTint ? COLOR_RED : baseColor;
        const uint8_t dimAmt = active ? intensity : 90;
        const uint16_t fill = dim565(col, dimAmt);
        const uint16_t border = active ? COLOR_WHITE : dim565(col, 180);
        d->fillCircle(cx, cy, FACE_R, fill);
        d->drawCircle(cx, cy, FACE_R, border);
        if (label && label[0]) {
            const int lx = cx - 2;
            const int ly = cy + 2;
            const uint16_t lc = (col == COLOR_YELLOW) ? COLOR_BLACK : COLOR_WHITE;
            SmallFont::drawString(d, lx, ly, label, lc);
        }
    }

    static void drawHeart(MatrixPanel_I2S_DMA* d, int x, int y, bool filled) {
        if (!d) return;
        // 5x5 heart sprite
        static const uint8_t H[5][5] = {
            {0,1,0,1,0},
            {1,1,1,1,1},
            {1,1,1,1,1},
            {0,1,1,1,0},
            {0,0,1,0,0}
        };
        const uint16_t col = filled ? COLOR_RED : dim565(COLOR_RED, 90);
        for (int yy = 0; yy < 5; yy++) {
            for (int xx = 0; xx < 5; xx++) {
                if (!H[yy][xx]) continue;
                d->drawPixel(x + xx, y + yy, col);
            }
        }
    }

    void drawLivesHearts(MatrixPanel_I2S_DMA* d) const {
        using namespace SimonGameConfig;
        const int n = (int)maxLives;
        const int totalW = n * HEART_W + (n - 1) * HEART_GAP;
        const int startX = FACE_CX - totalW / 2;
        const int y = FACE_CY - 2;
        for (int i = 0; i < n; i++) {
            const bool on = (i < (int)lives);
            drawHeart(d, startX + i * (HEART_W + HEART_GAP), y, on);
        }
    }

    static uint8_t errorIntensity(uint32_t now, uint32_t startMs) {
        // 3 bright flashes with a smooth-ish triangle intensity.
        const uint32_t age = (uint32_t)(now - startMs);
        const uint32_t window = speedScaleMs(420);
        if (age >= window) return 0;
        const uint32_t period = max(1u, speedScaleMs(120));
        const uint32_t t = age % period;
        const uint32_t half = max(1u, period / 2u);
        const uint32_t dist = (t <= half) ? t : (period - t);
        const uint32_t v = (dist * 255u) / half;
        return (uint8_t)constrain((int)v, 0, 255);
    }

    void drawPulse(MatrixPanel_I2S_DMA* d, uint32_t now) const {
        if (!pulse.active) return;
        const uint32_t age = (uint32_t)(now - pulse.startMs);
        const uint32_t stepMs = speedScaleMs(55);
        const int step = (int)(age / max(1u, stepMs));
        if (step < 0 || step > 3) return;
        const uint8_t a = (uint8_t)max(0, 220 - step * 55);
        const uint16_t col = dim565(colorFor(pulse.sym), a);

        // Face buttons: expanding circle outline.
        if (pulse.sym == SYM_X || pulse.sym == SYM_Y || pulse.sym == SYM_A || pulse.sym == SYM_B) {
            int cx, cy;
            faceCenter(pulse.sym, cx, cy);
            const int r = SimonGameConfig::FACE_R + 2 + step * 2;
            d->drawCircle(cx, cy, r, col);
            return;
        }

        // Shoulder: expanding rect outline.
        if (pulse.sym == SYM_LB || pulse.sym == SYM_RB) {
            Rect r = shoulderRect(pulse.sym);
            const int pad = 1 + step;
            d->drawRect(r.x - pad, r.y - pad, r.w + pad * 2, r.h + pad * 2, col);
            return;
        }
    }

    void drawHud(MatrixPanel_I2S_DMA* display) const {
        // Minimal HUD at top (avoid top edge overflow: y=6 like other games).
        SmallFont::drawString(display, 2, 6, "SIMON", COLOR_CYAN);
        char buf[16];
        snprintf(buf, sizeof(buf), "L:%u", (unsigned)bestScore);
        SmallFont::drawString(display, PANEL_RES_X - 20, 6, buf, COLOR_YELLOW);
        for (int x = 0; x < PANEL_RES_X; x += 2) display->drawPixel(x, 7, COLOR_BLUE);
    }

public:
    void start() override {
        // Load difficulty from persisted settings.
        difficulty = (Difficulty)globalSettings.getSimonDifficulty();
        maxLives = globalSettings.getSimonLives();
        lives = maxLives;
        randomSeed((uint32_t)micros() ^ (uint32_t)millis());
        startNewRun((uint32_t)millis());
    }

    void update(ControllerManager* input) override {
        const uint32_t now = (uint32_t)millis();
        if (gameOver) return;

        ControllerPtr ctl = (input != nullptr) ? input->getController(0) : nullptr;
        if (!(ctl && ctl->isConnected())) return;

        // Expire highlight
        if (activeSym != SYM_NONE && (int32_t)(now - activeUntilMs) >= 0) {
            activeSym = SYM_NONE;
        }

        // Expire pulse automatically (draw() uses time window).
        if (pulse.active) {
            // 4 steps * 55ms = 220ms
            if ((uint32_t)(now - pulse.startMs) > speedScaleMs(240)) pulse.active = false;
        }

        switch (phase) {
            case PHASE_READY: {
                if ((uint32_t)(now - phaseStartMs) >= speedScaleMs(SimonGameConfig::READY_MS)) {
                    roundHadMistake = false;
                    beginShowPhase(now);
                }
                break;
            }

            case PHASE_BETWEEN: {
                if ((uint32_t)(now - phaseStartMs) >= speedScaleMs(SimonGameConfig::BETWEEN_ROUNDS_MS)) {
                    roundHadMistake = false;
                    beginShowPhase(now);
                }
                break;
            }

            case PHASE_SHOW: {
                const uint16_t onMs = (uint16_t)speedScaleMs(SimonGameConfig::SHOW_ON_MS[(uint8_t)difficulty]);
                const uint16_t offMs = (uint16_t)speedScaleMs(SimonGameConfig::SHOW_OFF_MS[(uint8_t)difficulty]);

                if ((int32_t)(now - nextStepMs) < 0) break;

                if (showIndex >= seqLen) {
                    beginInputPhase(now);
                    break;
                }

                const Symbol s = (Symbol)seq[showIndex];

                if (!showOn) {
                    // Turn ON current symbol
                    showOn = true;
                    activeSym = s;
                    activeUntilMs = now + onMs;
                    // Brightness flash is computed in draw() as a triangle wave over [now..activeUntilMs]
                    showIntensity = 255;
                    const uint16_t f = toneFor(s);
                    if (f != 0) globalAudio.playTone(f, speedScaleMs(SimonGameConfig::TONE_SHOW_MS));
                    nextStepMs = now + onMs;
                } else {
                    // Turn OFF, move to next
                    showOn = false;
                    activeSym = SYM_NONE;
                    showIntensity = 0;
                    showIndex++;
                    nextStepMs = now + offMs;
                }
                break;
            }

            case PHASE_INPUT: {
                const Symbol pressed = readEdgeSymbol(ctl);
                if (pressed == SYM_NONE) break;

                const Symbol expected = (inputIndex < seqLen) ? (Symbol)seq[inputIndex] : SYM_NONE;
                if (pressed == expected) {
                    setActive(pressed, SimonGameConfig::TONE_PRESS_MS, now, true);
                    inputIndex++;
                    if (inputIndex >= seqLen) {
                        // Round cleared
                        bestScore = (uint16_t)max((int)bestScore, (int)seqLen);
                        if (!roundHadMistake && lives < maxLives) lives++;
                        phase = PHASE_BETWEEN;
                        phaseStartMs = now;
                        addRandomSymbol();
                    }
                } else {
                    // Fail -> lose life (and replay the same sequence), or game over if no lives remain.
                    roundHadMistake = true;
                    if (lives > 0) lives--;
                    errorSym = pressed;
                    errorStartMs = now;
                    // Error sound
                    globalAudio.playTone(SimonGameAudio::TONE_FAIL, 210);
                    // Red-tinted flash uses draw() while in PHASE_ERROR.
                    phase = PHASE_ERROR;
                    phaseStartMs = now;
                }
                break;
            }

            case PHASE_ERROR: {
                // Short error animation window. After it:
                // - if lives remain: replay same sequence (SHOW) and retry
                // - if lives are 0: transition to GAME OVER (leaderboard)
                const uint32_t dur = speedScaleMs(420);
                if ((uint32_t)(now - phaseStartMs) >= dur) {
                    if (lives == 0) {
                        // Score: best completed length so far.
                        bestScore = (uint16_t)max((int)bestScore, (int)(seqLen > 0 ? (seqLen - 1) : 0));
                        gameOver = true;
                        phase = PHASE_GAME_OVER;
                    } else {
                        // Retry same seqLen from start
                        inputIndex = 0;
                        beginShowPhase(now);
                    }
                }
                break;
            }

            case PHASE_GAME_OVER:
            default:
                break;
        }
    }

    void draw(MatrixPanel_I2S_DMA* display) override {
        if (!display) return;
        display->fillScreen(COLOR_BLACK);

        // If gameOver was set, show standard leaderboard screen (engine will also submit score).
        if (gameOver) {
            char tag[4];
            UserProfiles::getPadTag(0, tag);
            GameOverLeaderboardView::draw(display, "GAME OVER", leaderboardId(), leaderboardScore(), tag);
            return;
        }

        drawHud(display);

        const uint32_t now = (uint32_t)millis();
        const bool errorPhase = (phase == PHASE_ERROR);
        const Symbol err = errorPhase ? errorSym : SYM_NONE;
        const uint8_t errI = errorPhase ? errorIntensity(now, errorStartMs) : 0;

        // Hearts between buttons
        drawLivesHearts(display);

        // Face buttons as circles in diamond layout.
        // Active symbol uses showIntensity during SHOW, otherwise full brightness.
        uint8_t inten = 255;
        if (phase == PHASE_SHOW && activeSym != SYM_NONE) {
            // Triangle wave: low->high->low over the ON window.
            const uint32_t onMs = (uint32_t)speedScaleMs(SimonGameConfig::SHOW_ON_MS[(uint8_t)difficulty]);
            const uint32_t start = (uint32_t)(activeUntilMs - onMs);
            const uint32_t t = (uint32_t)(now - start);
            if (t >= onMs) inten = 0;
            else {
                const uint32_t half = max(1u, onMs / 2u);
                const uint32_t dist = (t <= half) ? t : (onMs - t);
                inten = (uint8_t)constrain((int)((dist * 255u) / half), 0, 255);
            }
        }
        // If we're in error phase, the wrong symbol becomes the "active" one with red tint + animated intensity.
        drawFaceCircle(display, SYM_Y, SimonGameConfig::COL_Y, "Y",
                       (activeSym == SYM_Y) || (err == SYM_Y),
                       (err == SYM_Y) ? errI : inten,
                       err == SYM_Y);
        drawFaceCircle(display, SYM_X, SimonGameConfig::COL_X, "X",
                       (activeSym == SYM_X) || (err == SYM_X),
                       (err == SYM_X) ? errI : inten,
                       err == SYM_X);
        drawFaceCircle(display, SYM_B, SimonGameConfig::COL_B, "B",
                       (activeSym == SYM_B) || (err == SYM_B),
                       (err == SYM_B) ? errI : inten,
                       err == SYM_B);
        drawFaceCircle(display, SYM_A, SimonGameConfig::COL_A, "A",
                       (activeSym == SYM_A) || (err == SYM_A),
                       (err == SYM_A) ? errI : inten,
                       err == SYM_A);

        // Medium+: shoulders
        if (difficulty >= DIFF_MEDIUM) {
            const bool lbAct = (activeSym == SYM_LB) || (err == SYM_LB);
            const bool rbAct = (activeSym == SYM_RB) || (err == SYM_RB);
            drawPill(display, shoulderRect(SYM_LB), SimonGameConfig::COL_LB, "LB", lbAct, (err == SYM_LB) ? errI : inten, err == SYM_LB);
            drawPill(display, shoulderRect(SYM_RB), SimonGameConfig::COL_RB, "RB", rbAct, (err == SYM_RB) ? errI : inten, err == SYM_RB);
        }

        // Hard: D-pad edge bands (4 columns / 4 rows flash)
        if (difficulty >= DIFF_HARD) {
            auto drawBand = [&](Symbol s) {
                const Rect r = dpadBandRect(s);
                const bool act = (activeSym == s) || (err == s);
                const uint16_t base = SimonGameConfig::COL_DPAD;
                const bool isErr = (err == s);
                const uint16_t col = isErr ? COLOR_RED : base;
                const uint8_t amt = act ? (isErr ? errI : inten) : 70;
                const uint16_t fill = dim565(col, amt);
                display->fillRect(r.x, r.y, r.w, r.h, fill);
            };
            drawBand(SYM_UP);
            drawBand(SYM_DOWN);
            drawBand(SYM_LEFT);
            drawBand(SYM_RIGHT);
        }

        // Pulse overlay (success)
        drawPulse(display, now);

        // Error flash overlay (short, red-ish global tint)
        if (phase == PHASE_ERROR) {
            const uint32_t age = (uint32_t)(now - phaseStartMs);
            // 3 quick pulses
            const uint32_t period = speedScaleMs(90);
            const bool on = ((age / max(1u, period)) % 2u) == 0u;
            if (on) {
                // Light red frame overlay (cheap): corners + center cross
                const uint16_t c = dim565(COLOR_RED, 140);
                display->drawRect(1, 9, PANEL_RES_X - 2, PANEL_RES_Y - 10, c);
                display->drawLine(0, PANEL_RES_Y / 2, PANEL_RES_X - 1, PANEL_RES_Y / 2, dim565(COLOR_RED, 90));
                display->drawLine(PANEL_RES_X / 2, 8, PANEL_RES_X / 2, PANEL_RES_Y - 1, dim565(COLOR_RED, 90));
            }
        }

        // Status text (center-left area)
        char st[24];
        if (phase == PHASE_READY) {
            SmallFont::drawString(display, 8, 20, "READY", COLOR_WHITE);
        } else if (phase == PHASE_SHOW) {
            snprintf(st, sizeof(st), "SHOW %u", (unsigned)seqLen);
            SmallFont::drawString(display, 8, 20, st, COLOR_WHITE);
        } else if (phase == PHASE_INPUT) {
            snprintf(st, sizeof(st), "IN %u/%u", (unsigned)inputIndex, (unsigned)seqLen);
            SmallFont::drawString(display, 8, 20, st, COLOR_WHITE);
        } else if (phase == PHASE_BETWEEN) {
            SmallFont::drawString(display, 8, 20, "GOOD!", COLOR_GREEN);
        } else if (phase == PHASE_ERROR) {
            // Error hint
            SmallFont::drawString(display, 8, 20, "MISS!", COLOR_RED);
        }
    }

    bool isGameOver() override { return gameOver; }

    void reset() override {
        difficulty = (Difficulty)globalSettings.getSimonDifficulty();
        maxLives = globalSettings.getSimonLives();
        lives = maxLives;
        startNewRun((uint32_t)millis());
    }

    // ------------------------------
    // Leaderboard integration
    // ------------------------------
    bool leaderboardEnabled() const override { return true; }
    const char* leaderboardId() const override { return "simon"; }
    const char* leaderboardName() const override { return "Simon"; }
    uint32_t leaderboardScore() const override { return (bestScore > 0) ? (uint32_t)bestScore : 0u; }
};


