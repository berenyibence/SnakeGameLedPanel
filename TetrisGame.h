#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "GameBase.h"
#include "ControllerManager.h"
#include "config.h"
#include "SmallFont.h"

/**
 * TetrisGame - Classic Tetris game
 * Only visible when one player is connected
 */
class TetrisGame : public GameBase {
private:
    // Tetris board dimensions
    static const int BOARD_WIDTH = 10;
    static const int BOARD_HEIGHT = 20;
    // Classic look: 3×3 pixel cells for the board.
    static const int CELL_SIZE = 3;  // Size of each cell in pixels
    
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
    static const unsigned long INITIAL_FALL_DELAY = 500;  // ms

    // Line clear visual feedback (flash rows before removing)
    bool lineFlashing;
    bool flashOn;
    uint8_t flashTogglesRemaining; // 6 toggles => 3 visible flashes
    unsigned long lastFlashToggleMs;
    uint8_t flashingRows[4];
    uint8_t flashingRowCount;
    uint8_t pendingCleared; // number of lines being cleared (for scoring/level)
    static const unsigned long FLASH_TOGGLE_MS = 90;
    
    // Tetris pieces (Tetrominoes) - 7 types
    static const uint8_t PIECES[7][4][4][4];  // [type][rotation][y][x]
    static const uint16_t PIECE_COLORS[7];
    
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
    }

    void reset() override {
        start();
    }

    void update(ControllerManager* input) override {
        if (gameOver) return;
        
        ControllerPtr p1 = input->getController(0);
        if (!p1 || !p1->isConnected()) return;
        
        unsigned long now = millis();
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
            SmallFont::drawString(display, 8, 28, "GAME OVER", COLOR_RED);
            char scoreStr[16];
            snprintf(scoreStr, sizeof(scoreStr), "SCORE:%d", score);
            SmallFont::drawString(display, 4, 38, scoreStr, COLOR_WHITE);
            return;
        }

        // Layout (centered, 1px margins): [HOLD box] [board] [NEXT box]
        // - Board uses 3×3 blocks
        // - Hold/Next previews use 2×2 blocks to save space
        const int previewCell = 2;
        const int previewSize = 4 * previewCell;     // 4x4 tetromino grid
        const int boxOuter = previewSize + 2;        // border included
        const int boardOuterW = BOARD_WIDTH * CELL_SIZE + 2;
        const int boardOuterH = BOARD_HEIGHT * CELL_SIZE + 2;
        const int gap = 1; // 1px gap between panels to maximize space
        const int totalW = boxOuter + gap + boardOuterW + gap + boxOuter;
        const int originX = (PANEL_RES_X - totalW) / 2;
        const int outerY = 1; // align boxes with top of game area (no top HUD)

        const int holdOuterX = originX;
        const int boardOuterX = holdOuterX + boxOuter + gap;
        const int nextOuterX = boardOuterX + boardOuterW + gap;

        const int boardStartX = boardOuterX + 1;
        const int boardStartY = outerY + 1;

        // Hold box (aligned with top of playfield)
        display->drawRect(holdOuterX, outerY, boxOuter, boxOuter, COLOR_WHITE);

        // Next box is taller to show 3 upcoming pieces (stacked).
        const int vGap = 1; // 1px gap between preview rows
        const int nextInnerH = (previewSize * 3) + (vGap * 2);
        const int nextOuterH = nextInnerH + 2; // border
        display->drawRect(nextOuterX, outerY, boxOuter, nextOuterH, COLOR_WHITE);

        // Info under the boxes (to preserve vertical board space):
        // - Score under HOLD (left), written vertically (one digit per line)
        // - Level under NEXT (right), just the number (no "L:")
        const int scoreBaseY = outerY + boxOuter + 6; // TomThumb baseline
        const int levelY = outerY + nextOuterH + 6;

        // Score: vertical digits
        {
            char sbuf[16];
            snprintf(sbuf, sizeof(sbuf), "%d", score);
            const int digitX = holdOuterX + (boxOuter / 2) - 1; // roughly centered under the box
            int yy = scoreBaseY;
            for (int i = 0; sbuf[i] != '\0'; i++) {
                SmallFont::drawChar(display, digitX, yy, sbuf[i], COLOR_YELLOW);
                yy += 8;
                if (yy > PANEL_RES_Y - 2) break;
            }
        }

        // Level: compact (just digits)
        {
            char lbuf[6];
            snprintf(lbuf, sizeof(lbuf), "%d", level);
            SmallFont::drawString(display, nextOuterX + 3, levelY, lbuf, COLOR_GREEN);
        }

        // Draw board background
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

        // Hold preview (left)
        drawPreview(holdOuterX, outerY, holdType, hasHold);

        // Next previews (right): show 3 pieces stacked
        for (int i = 0; i < 3; i++) {
            const int previewOuterY = outerY + i * (previewSize + vGap);
            drawPreview(nextOuterX, previewOuterY, nextPieces[i].type, true);
        }
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

// Tetris piece definitions
const uint8_t TetrisGame::PIECES[7][4][4][4] = {
    // I piece
    {{{1,1,1,1}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}},
     {{0,0,1,0}, {0,0,1,0}, {0,0,1,0}, {0,0,1,0}},
     {{1,1,1,1}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}},
     {{0,0,1,0}, {0,0,1,0}, {0,0,1,0}, {0,0,1,0}}},
    // O piece
    {{{1,1,0,0}, {1,1,0,0}, {0,0,0,0}, {0,0,0,0}},
     {{1,1,0,0}, {1,1,0,0}, {0,0,0,0}, {0,0,0,0}},
     {{1,1,0,0}, {1,1,0,0}, {0,0,0,0}, {0,0,0,0}},
     {{1,1,0,0}, {1,1,0,0}, {0,0,0,0}, {0,0,0,0}}},
    // T piece
    {{{0,1,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0}},
     {{0,1,0,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0}},
     {{0,0,0,0}, {1,1,1,0}, {0,1,0,0}, {0,0,0,0}},
     {{0,1,0,0}, {1,1,0,0}, {0,1,0,0}, {0,0,0,0}}},
    // S piece
    {{{0,1,1,0}, {1,1,0,0}, {0,0,0,0}, {0,0,0,0}},
     {{0,1,0,0}, {0,1,1,0}, {0,0,1,0}, {0,0,0,0}},
     {{0,1,1,0}, {1,1,0,0}, {0,0,0,0}, {0,0,0,0}},
     {{0,1,0,0}, {0,1,1,0}, {0,0,1,0}, {0,0,0,0}}},
    // Z piece
    {{{1,1,0,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}},
     {{0,0,1,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0}},
     {{1,1,0,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}},
     {{0,0,1,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0}}},
    // J piece
    {{{1,0,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0}},
     {{0,1,1,0}, {0,1,0,0}, {0,1,0,0}, {0,0,0,0}},
     {{0,0,0,0}, {1,1,1,0}, {0,0,1,0}, {0,0,0,0}},
     {{0,1,0,0}, {0,1,0,0}, {1,1,0,0}, {0,0,0,0}}},
    // L piece
    {{{0,0,1,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0}},
     {{0,1,0,0}, {0,1,0,0}, {0,1,1,0}, {0,0,0,0}},
     {{0,0,0,0}, {1,1,1,0}, {1,0,0,0}, {0,0,0,0}},
     {{1,1,0,0}, {0,1,0,0}, {0,1,0,0}, {0,0,0,0}}}
};

const uint16_t TetrisGame::PIECE_COLORS[7] = {
    COLOR_CYAN,    // I
    COLOR_YELLOW,  // O
    COLOR_PURPLE,  // T
    COLOR_GREEN,   // S
    COLOR_RED,     // Z
    COLOR_BLUE,    // J
    COLOR_ORANGE   // L
};

