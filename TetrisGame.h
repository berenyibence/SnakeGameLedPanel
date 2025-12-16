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
    Piece nextPiece;
    bool gameOver;
    int score;
    int linesCleared;
    int level;
    unsigned long lastFall;
    unsigned long lastMove;
    unsigned long lastRotate;
    unsigned long lastDrop;
    unsigned long inputIgnoreUntil;  // Ignore held buttons briefly after start (prevents "stale" menu input)
    static const unsigned long INITIAL_FALL_DELAY = 500;  // ms
    
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
     * Clear completed lines
     */
    int clearLines() {
        int linesClearedCount = 0;
        
        for (int y = BOARD_HEIGHT - 1; y >= 0; y--) {
            bool lineFull = true;
            for (int x = 0; x < BOARD_WIDTH; x++) {
                if (board[y][x] == 0) {
                    lineFull = false;
                    break;
                }
            }
            
            if (lineFull) {
                // Remove line and shift down
                for (int y2 = y; y2 > 0; y2--) {
                    for (int x = 0; x < BOARD_WIDTH; x++) {
                        board[y2][x] = board[y2 - 1][x];
                    }
                }
                // Clear top line
                for (int x = 0; x < BOARD_WIDTH; x++) {
                    board[0][x] = 0;
                }
                linesClearedCount++;
                y++;  // Check same line again
            }
        }
        
        return linesClearedCount;
    }
    
    /**
     * Spawn new piece
     */
    void spawnNewPiece() {
        currentPiece = nextPiece;
        initPiece(nextPiece, random(0, 7));
        
        // Check game over
        if (!canPlacePiece(currentPiece, 0, 0, 0)) {
            gameOver = true;
        }
    }

public:
    TetrisGame() 
        : gameOver(false), score(0), linesCleared(0), level(1), 
          lastFall(0), lastMove(0), lastRotate(0), lastDrop(0), inputIgnoreUntil(0) {
        // Initialize board
        for (int y = 0; y < BOARD_HEIGHT; y++) {
            for (int x = 0; x < BOARD_WIDTH; x++) {
                board[y][x] = 0;
            }
        }
        
        // Initialize first pieces
        initPiece(currentPiece, random(0, 7));
        initPiece(nextPiece, random(0, 7));
    }

    void start() override {
        gameOver = false;
        score = 0;
        linesCleared = 0;
        level = 1;
        const unsigned long now = millis();
        lastFall = now;
        lastMove = now;
        lastRotate = now;
        lastDrop = now;
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
        initPiece(nextPiece, random(0, 7));
    }

    void reset() override {
        start();
    }

    void update(ControllerManager* input) override {
        if (gameOver) return;
        
        ControllerPtr p1 = input->getController(0);
        if (!p1 || !p1->isConnected()) return;
        
        unsigned long now = millis();
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
                
                // Clear lines
                int cleared = clearLines();
                if (cleared > 0) {
                    linesCleared += cleared;
                    score += cleared * 100 * level;
                    
                    // Level up every 10 lines
                    level = (linesCleared / 10) + 1;
                }
                
                // Spawn new piece
                spawnNewPiece();
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
        
        // Calculate board position (centered)
        // NOTE: Must not draw at negative X. Using 1px margin keeps the border inside the panel.
        int boardStartX = 1;   // left aligned with 1px margin (fixes left border overflow)
        int boardStartY = 2;
        
        // Draw board background
        display->drawRect(boardStartX - 1, boardStartY - 1, 
                         BOARD_WIDTH * CELL_SIZE + 2, 
                         BOARD_HEIGHT * CELL_SIZE + 2, 
                         COLOR_WHITE);
        
        // Draw placed blocks
        for (int y = 0; y < BOARD_HEIGHT; y++) {
            for (int x = 0; x < BOARD_WIDTH; x++) {
                if (board[y][x] != 0) {
                    int screenX = boardStartX + x * CELL_SIZE;
                    int screenY = boardStartY + y * CELL_SIZE;
                    display->fillRect(screenX, screenY, CELL_SIZE, CELL_SIZE, 
                                     PIECE_COLORS[board[y][x] - 1]);
                }
            }
        }
        
        // Draw current falling piece
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
        
        // Side panel (right side)
        int panelX = boardStartX + BOARD_WIDTH * CELL_SIZE + 4;
        int panelY = 4;

        // Next piece preview
        SmallFont::drawString(display, panelX, panelY, "NEXT", COLOR_CYAN);
        panelY += 8;
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                if (PIECES[nextPiece.type][nextPiece.rotation][y][x]) {
                    int px = panelX + x * 3;
                    int py = panelY + y * 3;
                    display->fillRect(px, py, 3, 3, PIECE_COLORS[nextPiece.type]);
                }
            }
        }
        panelY += 14;

        // Score and level
        char infoStr[16];
        snprintf(infoStr, sizeof(infoStr), "S:%d", score);
        SmallFont::drawString(display, panelX, panelY, infoStr, COLOR_YELLOW);
        panelY += 8;
        snprintf(infoStr, sizeof(infoStr), "L:%d", level);
        SmallFont::drawString(display, panelX, panelY, infoStr, COLOR_GREEN);
    }

    bool isGameOver() override {
        return gameOver;
    }
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

