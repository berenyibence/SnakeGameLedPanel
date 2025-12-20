#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../../engine/GameBase.h"
#include "../../engine/ControllerManager.h"
#include "../../engine/config.h"
#include "../../engine/AudioManager.h"
#include "../../component/SmallFont.h"
#include "../../engine/UserProfiles.h"
#include "../../component/GameOverLeaderboardView.h"
#include "TetrisGameConfig.h"
#include "TetrisGameAudio.h"

/**
 * TetrisGame - Classic Tetris game
 * Only visible when one player is connected
 */
class TetrisGame : public GameBase {
private:
    // Tetris board dimensions
    static constexpr int BOARD_WIDTH = TetrisGameConfig::BOARD_WIDTH;
    static constexpr int BOARD_HEIGHT = TetrisGameConfig::BOARD_HEIGHT;
    // Classic look: 3×3 pixel cells for the board.
    static constexpr int CELL_SIZE = TetrisGameConfig::CELL_SIZE;  // Size of each cell in pixels
    
    // Board: 0 = empty, 1-7 = different colored blocks
    uint8_t board[BOARD_HEIGHT][BOARD_WIDTH];
    
    // Current falling piece
    struct Piece {
        int x, y;           // Position on board
        int type;            // Piece type (0-6)
        int rotation;        // Rotation state (0-3)
        uint8_t shape[4][4]; // Piece shape data
        uint16_t color;      // Piece color
    };
    
    Piece currentPiece;
    // Next queue (3 pieces shown in the UI)
    Piece nextPieces[3];
    bool hasHold;
    int holdType;                 // 0..6 (valid only if hasHold)
    bool holdUsedThisTurn;        // standard tetris: only 1 hold per piece drop
    bool gameOver;
    int score;
    int linesCleared;
    int level;
    unsigned long lastFall;
    unsigned long lastMove;
    unsigned long lastRotate;
    unsigned long lastDrop;
    unsigned long lastHold;
    unsigned long inputIgnoreUntil;  // Ignore held buttons briefly after start (prevents "stale" menu input)
    static constexpr unsigned long INITIAL_FALL_DELAY = TetrisGameConfig::INITIAL_FALL_DELAY_MS;  // ms

    // Line clear visual feedback (flash rows before removing)
    bool lineFlashing;
    bool flashOn;
    uint8_t flashTogglesRemaining; // 6 toggles => 3 visible flashes
    unsigned long lastFlashToggleMs;
    uint8_t flashingRows[4];
    uint8_t flashingRowCount;
    uint8_t pendingCleared; // number of lines being cleared (for scoring/level)
    static constexpr unsigned long FLASH_TOGGLE_MS = TetrisGameConfig::FLASH_TOGGLE_MS;
    
    // Tetris pieces (Tetrominoes) - 7 types (from TetrisGameConfig).
    static inline constexpr auto& PIECES = TetrisGameConfig::PIECES;             // [type][rotation][y][x]
    static inline constexpr auto& PIECE_COLORS = TetrisGameConfig::PIECE_COLORS; // RGB565 palette

    // ---------------------------------------------------------
    // Small color helper (RGB565 dimming) - used for ghost piece
    // ---------------------------------------------------------
    static inline uint16_t dimColor(uint16_t c, uint8_t mul /*0..255*/) {
        uint8_t r = (uint8_t)((c >> 11) & 0x1F);
        uint8_t g = (uint8_t)((c >> 5) & 0x3F);
        uint8_t b = (uint8_t)(c & 0x1F);
        r = (uint8_t)((r * mul) / 255);
        g = (uint8_t)((g * mul) / 255);
        b = (uint8_t)((b * mul) / 255);
        return (uint16_t)((r << 11) | (g << 5) | b);
    }
    
    // ---------------------------------------------------------
    // Tetris-only "Tetris!" explosion particles (spawned ONLY when clearing 4 lines)
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
    static constexpr int MAX_PARTICLES = 70;
    Particle particles[MAX_PARTICLES] = {};

    void spawnTetrisParticles(const uint8_t rows[4], uint8_t count, uint32_t now) {
        // Only for a true "tetris" (4 lines at once).
        if (count != 4) return;

        // Board pixel-space (matches draw() layout): board is flush-left.
        const int outerY = 1;
        const int boardStartX = 1;
        const int boardStartY = outerY + 1;
        const int innerW = BOARD_WIDTH * CELL_SIZE;

        // Emit a modest amount; visually punchy but cheap.
        const int bursts = 34;
        for (int n = 0; n < bursts; n++) {
            int slot = -1;
            for (int i = 0; i < MAX_PARTICLES; i++) {
                if (!particles[i].active) { slot = i; break; }
            }
            if (slot < 0) return;

            const uint8_t ry = rows[random(0, 4)];
            const int px = boardStartX + random(0, innerW);
            const int py = boardStartY + (int)ry * CELL_SIZE + (CELL_SIZE / 2);

            Particle& p = particles[slot];
            p.active = true;
            p.x = (float)px;
            p.y = (float)py;
            p.vx = ((float)random(-80, 81) / 100.0f) * 0.9f;
            p.vy = -(((float)random(20, 110) / 100.0f) * 0.9f);
            // Mix bright white with the current piece color so it feels themed.
            p.color = (random(0, 100) < 45) ? COLOR_WHITE : currentPiece.color;
            p.endMs = now + (uint32_t)random(260, 620);
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
            particles[i].vx *= 0.98f;
            particles[i].vy *= 0.98f;
            particles[i].vy += 0.028f; // gravity
        }
    }

    void drawParticles(MatrixPanel_I2S_DMA* display, uint32_t now) const {
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (!particles[i].active) continue;
            if ((int32_t)(particles[i].endMs - now) <= 0) continue;
            const int x = (int)particles[i].x;
            const int y = (int)particles[i].y;
            if (x < 0 || x >= PANEL_RES_X || y < 0 || y >= PANEL_RES_Y) continue;
            display->drawPixel(x, y, particles[i].color);
        }
    }

    /**
     * Initialize a piece
     */
    void initPiece(Piece& p, int type) {
        p.type = type;
        p.rotation = 0;
        p.color = PIECE_COLORS[type];
        p.x = BOARD_WIDTH / 2 - 2;
        p.y = 0;
        
        // Copy shape data
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                p.shape[y][x] = PIECES[type][0][y][x];
            }
        }
    }
    
    /**
     * Check if piece can be placed at position
     */
    bool canPlacePiece(const Piece& p, int dx, int dy, int rot) {
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                if (PIECES[p.type][rot][y][x]) {
                    int boardX = p.x + x + dx;
                    int boardY = p.y + y + dy;
                    
                    if (boardX < 0 || boardX >= BOARD_WIDTH ||
                        boardY >= BOARD_HEIGHT ||
                        (boardY >= 0 && board[boardY][boardX] != 0)) {
                        return false;
                    }
                }
            }
        }
        return true;
    }
    
    /**
     * Place piece on board
     */
    void placePiece(const Piece& p) {
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                if (PIECES[p.type][p.rotation][y][x]) {
                    int boardX = p.x + x;
                    int boardY = p.y + y;
                    if (boardY >= 0 && boardY < BOARD_HEIGHT &&
                        boardX >= 0 && boardX < BOARD_WIDTH) {
                        board[boardY][boardX] = p.type + 1;
                    }
                }
            }
        }
    }
    
    /**
     * Find completed lines (returns count and writes row indices)
     */
    int findFullLines(uint8_t outRows[4]) {
        int count = 0;
        for (int y = BOARD_HEIGHT - 1; y >= 0; y--) {
            bool lineFull = true;
            for (int x = 0; x < BOARD_WIDTH; x++) {
                if (board[y][x] == 0) { lineFull = false; break; }
            }
            if (lineFull) {
                if (count < 4) outRows[count] = (uint8_t)y;
                count++;
            }
        }
        if (count > 4) count = 4;
        return count;
    }

    /**
     * Remove specific rows and shift down.
     * Expects rows[] in any order; we remove from bottom to top safely.
     */
    void removeLines(const uint8_t rows[], int count) {
        if (count <= 0) return;
        // IMPORTANT:
        // Removing multiple rows by shifting one row at a time can skip rows because
        // indices change after each shift. Instead, do a single "compress" pass.
        bool removeRow[BOARD_HEIGHT] = { false };
        for (int i = 0; i < count; i++) {
            const int y = (int)rows[i];
            if (y >= 0 && y < BOARD_HEIGHT) removeRow[y] = true;
        }

        int dst = BOARD_HEIGHT - 1;
        for (int src = BOARD_HEIGHT - 1; src >= 0; src--) {
            if (removeRow[src]) continue;
            if (dst != src) {
                for (int x = 0; x < BOARD_WIDTH; x++) {
                    board[dst][x] = board[src][x];
                }
            }
            dst--;
        }

        // Clear remaining rows at the top.
        for (int y = dst; y >= 0; y--) {
            for (int x = 0; x < BOARD_WIDTH; x++) board[y][x] = 0;
        }
    }
    
    /**
     * Spawn new piece
     */
    void spawnNewPiece() {
        // Shift next queue and spawn a new 3rd preview.
        currentPiece = nextPieces[0];
        nextPieces[0] = nextPieces[1];
        nextPieces[1] = nextPieces[2];
        initPiece(nextPieces[2], random(0, 7));
        
        // Check game over
        if (!canPlacePiece(currentPiece, 0, 0, 0)) {
            gameOver = true;
        }
    }

    void doHoldSwap(unsigned long now) {
        if (!hasHold) {
            hasHold = true;
            holdType = currentPiece.type;
            spawnNewPiece(); // bring in the next piece
        } else {
            const int temp = holdType;
            holdType = currentPiece.type;
            initPiece(currentPiece, temp); // reset position/rotation
            if (!canPlacePiece(currentPiece, 0, 0, currentPiece.rotation)) {
                gameOver = true;
            }
        }
        holdUsedThisTurn = true;
        lastHold = now;
    }

    /**
     * Compute ghost landing Y for the current piece (hard-drop target).
     * This returns the Y position where the current piece would lock if dropped.
     */
    int computeGhostY() {
        int y = currentPiece.y;
        while (canPlacePiece(currentPiece, 0, (y - currentPiece.y) + 1, currentPiece.rotation)) {
            y++;
        }
        return y;
    }

public:
    TetrisGame() 
        : gameOver(false), score(0), linesCleared(0), level(1), 
          lastFall(0), lastMove(0), lastRotate(0), lastDrop(0), lastHold(0),
          inputIgnoreUntil(0),
          hasHold(false), holdType(0), holdUsedThisTurn(false),
          lineFlashing(false), flashOn(false), flashTogglesRemaining(0), lastFlashToggleMs(0),
          flashingRowCount(0), pendingCleared(0) {
        // Initialize board
        for (int y = 0; y < BOARD_HEIGHT; y++) {
            for (int x = 0; x < BOARD_WIDTH; x++) {
                board[y][x] = 0;
            }
        }
        
        // Initialize first pieces
        initPiece(currentPiece, random(0, 7));
        initPiece(nextPieces[0], random(0, 7));
        initPiece(nextPieces[1], random(0, 7));
        initPiece(nextPieces[2], random(0, 7));
    }

    void start() override {
        gameOver = false;
        score = 0;
        linesCleared = 0;
        level = 1;
        hasHold = false;            // saved piece box starts empty
        holdUsedThisTurn = false;
        lineFlashing = false;
        flashOn = false;
        flashTogglesRemaining = 0;
        flashingRowCount = 0;
        pendingCleared = 0;
        const unsigned long now = millis();
        lastFall = now;
        lastMove = now;
        lastRotate = now;
        lastDrop = now;
        lastHold = now;
        // Prevent immediate move/rotate/drop caused by buttons still held from the menu.
        inputIgnoreUntil = now + 250;
        
        // Clear board
        for (int y = 0; y < BOARD_HEIGHT; y++) {
            for (int x = 0; x < BOARD_WIDTH; x++) {
                board[y][x] = 0;
            }
        }
        
        // Spawn first pieces
        initPiece(currentPiece, random(0, 7));
        initPiece(nextPieces[0], random(0, 7));
        initPiece(nextPieces[1], random(0, 7));
        initPiece(nextPieces[2], random(0, 7));

        // Clear particles
        for (int i = 0; i < MAX_PARTICLES; i++) particles[i].active = false;

        // -----------------------------------------------------
        // Audio: play the "starting song" once (RTTTL, non-blocking)
        // -----------------------------------------------------
        // This is intentionally minimal: we play on start/reset and stop any
        // leftover ringtone from other applets (e.g. MusicApp).
        globalAudio.stopRtttl();
        globalAudio.playRtttl(TetrisGameAudio::MUSIC_START_RTTTL, /*loop=*/false);
    }

    void reset() override {
        start();
    }

    void update(ControllerManager* input) override {
        if (gameOver) return;
        
        ControllerPtr p1 = input->getController(0);
        if (!p1 || !p1->isConnected()) return;
        
        unsigned long now = millis();
        // Particle simulation runs regardless of line flashing.
        updateParticles((uint32_t)now);
        if (lineFlashing) {
            // Flash the cleared rows before removing them.
            if (now - lastFlashToggleMs >= FLASH_TOGGLE_MS) {
                lastFlashToggleMs = now;
                flashOn = !flashOn;
                if (flashTogglesRemaining > 0) flashTogglesRemaining--;
                if (flashTogglesRemaining == 0) {
                    lineFlashing = false;
                    flashOn = false;

                    // Finalize clear, update score/level, then spawn new piece.
                    // Tetris "boom" particles ONLY when 4 lines were cleared together.
                    spawnTetrisParticles(flashingRows, pendingCleared, (uint32_t)now);
                    removeLines(flashingRows, (int)flashingRowCount);

                    linesCleared += pendingCleared;
                    score += (int)pendingCleared * 100 * level;
                    level = (linesCleared / 10) + 1;

                    pendingCleared = 0;
                    flashingRowCount = 0;
                    holdUsedThisTurn = false;
                    spawnNewPiece();
                }
            }
            return;
        }

        uint8_t dpad = p1->dpad();
        const bool acceptInput = (now >= inputIgnoreUntil);
        
        // Handle input with debouncing
        if (acceptInput && (now - lastMove > 100)) {
            if (dpad & 0x08) {  // LEFT
                if (canPlacePiece(currentPiece, -1, 0, currentPiece.rotation)) {
                    currentPiece.x--;
                    lastMove = now;
                }
            }
            if (dpad & 0x04) {  // RIGHT
                if (canPlacePiece(currentPiece, 1, 0, currentPiece.rotation)) {
                    currentPiece.x++;
                    lastMove = now;
                }
            }
            if (dpad & 0x02) {  // DOWN - soft drop
                if (canPlacePiece(currentPiece, 0, 1, currentPiece.rotation)) {
                    currentPiece.y++;
                    score += 1;  // Bonus for soft drop
                    lastMove = now;
                }
            }
        }
        
        // Controls: UP and A are intentionally flipped per your request:
        // - UP = hard drop
        // - A  = rotate
        if (acceptInput) {
            // Hold / swap (X)
            if (p1->x() && !holdUsedThisTurn && (now - lastHold > 200)) {
                doHoldSwap(now);
            }

            // Hard drop (UP)
            if ((dpad & 0x01) && (now - lastDrop > 200)) {
                while (canPlacePiece(currentPiece, 0, 1, currentPiece.rotation)) {
                    currentPiece.y++;
                    score += 2;
                }
                lastDrop = now;
            }

            // Rotate piece (A)
            if (p1->a() && (now - lastRotate > 150)) {
                int newRot = (currentPiece.rotation + 1) % 4;
                if (canPlacePiece(currentPiece, 0, 0, newRot)) {
                    currentPiece.rotation = newRot;
                    lastRotate = now;
                }
            }
        }
        
        // Auto fall
        unsigned long fallDelay = INITIAL_FALL_DELAY - (level * 50);
        if (fallDelay < 100) fallDelay = 100;
        
        if (now - lastFall > fallDelay) {
            if (canPlacePiece(currentPiece, 0, 1, currentPiece.rotation)) {
                currentPiece.y++;
            } else {
                // Piece landed - place it on board
                placePiece(currentPiece);

                // Find full lines; if any, flash them before removing.
                uint8_t rows[4] = {0, 0, 0, 0};
                const int cleared = findFullLines(rows);
                if (cleared > 0) {
                    flashingRowCount = (uint8_t)cleared;
                    for (int i = 0; i < cleared; i++) flashingRows[i] = rows[i];
                    pendingCleared = (uint8_t)cleared;
                    lineFlashing = true;
                    flashOn = true;
                    flashTogglesRemaining = 6;
                    lastFlashToggleMs = now;
                } else {
                    holdUsedThisTurn = false;
                    spawnNewPiece();
                }
            }
            lastFall = now;
        }
    }

    void draw(MatrixPanel_I2S_DMA* display) override {
        display->fillScreen(COLOR_BLACK);
        
        if (gameOver) {
            char tag[4];
            UserProfiles::getPadTag(0, tag);
            GameOverLeaderboardView::draw(display, "GAME OVER", leaderboardId(), leaderboardScore(), tag);
            return;
        }

        // Layout (as requested):
        // - Board is flush LEFT.
        // - Right HUD column is vertically centered and contains:
        //   score (6 digits), level (3 digits), then [NEXT (3 tall)] [HOLD] side-by-side.
        // Board uses 3×3 blocks, previews use 2×2.
        const int outerY = 1;
        const int boardOuterX = 0;
        const int boardOuterW = BOARD_WIDTH * CELL_SIZE + 2;
        const int boardOuterH = BOARD_HEIGHT * CELL_SIZE + 2;
        const int boardStartX = boardOuterX + 1;
        const int boardStartY = outerY + 1;

        const int hudX = boardOuterX + boardOuterW + 1; // 1px gap

        const int previewCell = 2;
        const int previewSize = 4 * previewCell; // 8
        const int boxOuter = previewSize + 2;    // 10
        const int boxGap = 1;

        // Next + Hold boxes side-by-side
        // - Next remains 3 pieces tall (stacked)
        // - Hold is 1 piece tall
        const int vGap = 1; // 1px gap between stacked previews
        const int nextInnerH = (previewSize * 3) + (vGap * 2);
        const int nextOuterH = nextInnerH + 2; // border
        const int boxesW = (boxOuter * 2) + boxGap;
        const int hudW = PANEL_RES_X - hudX;
        const int hudBlockX = hudX + max(0, (hudW - boxesW) / 2);

        // Vertically center the HUD block within the board height.
        // Text area is 2 lines; we use a fixed height for stability.
        const int textH = 18;     // includes both lines and a bit of breathing room
        const int textToBoxesGap = 4;
        const int hudBlockH = textH + textToBoxesGap + nextOuterH;
        const int hudBlockY = outerY + max(0, (boardOuterH - hudBlockH) / 2);

        const int scoreY = hudBlockY + 6;
        const int levelY = hudBlockY + 14;
        const int boxesY = hudBlockY + textH + textToBoxesGap;

        const int nextOuterX = hudBlockX;
        const int holdOuterX = hudBlockX + boxOuter + boxGap;

        // Score / Level (numbers only; fixed width with leading zeros)
        // Leading zeros are rendered dimmer (less bright).
        // - Score: 6 chars, e.g. 000250
        // - Level: 3 chars, e.g. 007
        {
            char sbuf[10];
            snprintf(sbuf, sizeof(sbuf), "%06d", max(0, score));
            // TomThumb is tiny; approximate centering with a 4px advance per char.
            const int textW = 6 * 4;
            const int sx = hudBlockX + max(0, (boxesW - textW) / 2);
            const uint16_t dim = dimColor(COLOR_YELLOW, 120);

            // Find first non-zero digit (keep at least the last digit bright).
            int firstNZ = 5;
            for (int i = 0; i < 6; i++) {
                if (sbuf[i] != '0') { firstNZ = i; break; }
            }
            if (firstNZ >= 6) firstNZ = 5;

            for (int i = 0; i < 6; i++) {
                const uint16_t c = (i < firstNZ) ? dim : COLOR_YELLOW;
                SmallFont::drawChar(display, sx + i * 4, scoreY, sbuf[i], c);
            }
        }
        {
            char lbuf[8];
            snprintf(lbuf, sizeof(lbuf), "%03d", max(0, level));
            const int textW = 3 * 4;
            const int lx = hudBlockX + max(0, (boxesW - textW) / 2);
            const uint16_t dim = dimColor(COLOR_GREEN, 120);

            int firstNZ = 2;
            for (int i = 0; i < 3; i++) {
                if (lbuf[i] != '0') { firstNZ = i; break; }
            }
            if (firstNZ >= 3) firstNZ = 2;

            for (int i = 0; i < 3; i++) {
                const uint16_t c = (i < firstNZ) ? dim : COLOR_GREEN;
                SmallFont::drawChar(display, lx + i * 4, levelY, lbuf[i], c);
            }
        }

        display->drawRect(nextOuterX, boxesY, boxOuter, nextOuterH, COLOR_WHITE);
        display->drawRect(holdOuterX, boxesY, boxOuter, boxOuter, COLOR_WHITE);

        // Board border
        display->drawRect(boardOuterX, outerY, boardOuterW, boardOuterH, COLOR_WHITE);
        
        // Draw placed blocks
        auto isFlashingRow = [&](int y) -> bool {
            if (!lineFlashing) return false;
            for (int i = 0; i < (int)flashingRowCount; i++) {
                if ((int)flashingRows[i] == y) return true;
            }
            return false;
        };

        for (int y = 0; y < BOARD_HEIGHT; y++) {
            for (int x = 0; x < BOARD_WIDTH; x++) {
                const int screenX = boardStartX + x * CELL_SIZE;
                const int screenY = boardStartY + y * CELL_SIZE;

                // Flash the entire row region (all cells) for strong feedback.
                if (isFlashingRow(y)) {
                    display->fillRect(screenX, screenY, CELL_SIZE, CELL_SIZE, flashOn ? COLOR_WHITE : COLOR_BLACK);
                    continue;
                }

                if (board[y][x] != 0) {
                    display->fillRect(screenX, screenY, CELL_SIZE, CELL_SIZE,
                                      PIECE_COLORS[board[y][x] - 1]);
                }
            }
        }

        // Ghost piece (hard-drop target) - drawn BEFORE the active piece.
        // This shows where UP would land the piece.
        if (!lineFlashing) {
            const int ghostY = computeGhostY();
            const uint16_t ghostCol = dimColor(currentPiece.color, 85); // ~33% intensity
            for (int y = 0; y < 4; y++) {
                for (int x = 0; x < 4; x++) {
                    if (!PIECES[currentPiece.type][currentPiece.rotation][y][x]) continue;
                    const int boardX = currentPiece.x + x;
                    const int boardY = ghostY + y;
                    if (boardY < 0) continue;
                    const int screenX = boardStartX + boardX * CELL_SIZE;
                    const int screenY = boardStartY + boardY * CELL_SIZE;
                    // Outline looks better than fill for a "ghost" on 3×3 cells.
                    display->drawRect(screenX, screenY, CELL_SIZE, CELL_SIZE, ghostCol);
                }
            }
        }
        
        // Draw current falling piece (skip while line flashing, because the piece was already placed).
        if (!lineFlashing) {
            for (int y = 0; y < 4; y++) {
                for (int x = 0; x < 4; x++) {
                    if (PIECES[currentPiece.type][currentPiece.rotation][y][x]) {
                        int boardX = currentPiece.x + x;
                        int boardY = currentPiece.y + y;
                        if (boardY >= 0) {
                            int screenX = boardStartX + boardX * CELL_SIZE;
                            int screenY = boardStartY + boardY * CELL_SIZE;
                            display->fillRect(screenX, screenY, CELL_SIZE, CELL_SIZE, 
                                             currentPiece.color);
                        }
                    }
                }
            }
        }

        // Helper: draw a piece centered inside a 4x4 preview box.
        // NOTE: boxOuterX/boxOuterY refer to the OUTER border rect coordinates.
        auto drawPreview = [&](int boxOuterX, int boxOuterY, int type, bool valid) {
            if (!valid) return;
            // Use rotation 0 for preview/hold.
            int minX = 4, minY = 4, maxX = -1, maxY = -1;
            for (int yy = 0; yy < 4; yy++) {
                for (int xx = 0; xx < 4; xx++) {
                    if (PIECES[type][0][yy][xx]) {
                        if (xx < minX) minX = xx;
                        if (yy < minY) minY = yy;
                        if (xx > maxX) maxX = xx;
                        if (yy > maxY) maxY = yy;
                    }
                }
            }
            if (maxX < 0 || maxY < 0) return;
            const int pieceW = (maxX - minX + 1) * previewCell;
            const int pieceH = (maxY - minY + 1) * previewCell;
            const int offX = (previewSize - pieceW) / 2 - (minX * previewCell);
            const int offY = (previewSize - pieceH) / 2 - (minY * previewCell);
            const int innerX = boxOuterX + 1;
            const int innerY = boxOuterY + 1;
            for (int yy = 0; yy < 4; yy++) {
                for (int xx = 0; xx < 4; xx++) {
                    if (PIECES[type][0][yy][xx]) {
                        const int px = innerX + offX + xx * previewCell;
                        const int py = innerY + offY + yy * previewCell;
                        display->fillRect(px, py, previewCell, previewCell, PIECE_COLORS[type]);
                    }
                }
            }
        };

        // Next previews (left, tall box): show 3 pieces stacked.
        for (int i = 0; i < 3; i++) {
            const int previewOuterY = boxesY + i * (previewSize + vGap);
            drawPreview(nextOuterX, previewOuterY, nextPieces[i].type, true);
        }

        // Hold preview (right, square box)
        drawPreview(holdOuterX, boxesY, holdType, hasHold);

        // Tetris particles (overlay)
        drawParticles(display, (uint32_t)millis());
    }

    bool isGameOver() override {
        return gameOver;
    }

    // ------------------------------
    // Leaderboard integration
    // ------------------------------
    bool leaderboardEnabled() const override { return true; }
    const char* leaderboardId() const override { return "tetris"; }
    const char* leaderboardName() const override { return "Tetris"; }
    uint32_t leaderboardScore() const override { return (score > 0) ? (uint32_t)score : 0u; }
};

// NOTE: Tetromino shapes + colors moved to `Games/Tetris/TetrisGameSprites.h`
// and are exposed via `TetrisGameConfig`.

