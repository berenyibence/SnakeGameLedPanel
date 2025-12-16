#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "GameBase.h"
#include "ControllerManager.h"
#include "config.h"
#include "SmallFont.h"

/**
 * LabyrinthGame - Maze navigation game
 * Player navigates through a maze to reach the exit
 */
class LabyrinthGame : public GameBase {
private:
    // Player structure
    struct Player {
        float x;
        float y;
        float speed;
        uint16_t color;
        int size;
        
        Player() : x(2.0f), y(2.0f), speed(1.0f), color(COLOR_GREEN), size(2) {}
    };
    
    Player player;
    bool gameOver;
    bool gameWon;
    int level;
    unsigned long lastUpdate;
    unsigned long winTime;
    static const int UPDATE_INTERVAL_MS = 16;  // ~60 FPS
    static const int CELL_SIZE = 4;  // Size of each maze cell in pixels
    static const int MAZE_WIDTH = PANEL_RES_X / CELL_SIZE;
    static const int MAZE_HEIGHT = PANEL_RES_Y / CELL_SIZE;
    
    // Maze data: 0 = wall, 1 = path, 2 = start, 3 = exit
    uint8_t maze[MAZE_HEIGHT][MAZE_WIDTH];
    int exitX, exitY;
    
    /**
     * Generate a simple maze using depth-first carve for smoother paths
     */
    void generateMaze() {
        // Initialize all cells as walls
        for (int y = 0; y < MAZE_HEIGHT; y++) {
            for (int x = 0; x < MAZE_WIDTH; x++) {
                maze[y][x] = 0;  // Wall
            }
        }

        // Start point
        const int startX = 1;
        const int startY = 1;
        maze[startY][startX] = 1;

        // DFS stack
        int stackX[MAZE_WIDTH * MAZE_HEIGHT];
        int stackY[MAZE_WIDTH * MAZE_HEIGHT];
        int top = 0;
        stackX[top] = startX;
        stackY[top] = startY;

        // Direction vectors (up, down, left, right)
        const int dx[4] = { 0, 0, -1, 1 };
        const int dy[4] = { -1, 1, 0, 0 };

        while (top >= 0) {
            int cx = stackX[top];
            int cy = stackY[top];

            // Build list of neighbors two steps away that are walls
            int neighbors[4];
            int nCount = 0;
            for (int dir = 0; dir < 4; dir++) {
                int nx = cx + dx[dir] * 2;
                int ny = cy + dy[dir] * 2;
                if (nx > 0 && nx < MAZE_WIDTH - 1 && ny > 0 && ny < MAZE_HEIGHT - 1) {
                    if (maze[ny][nx] == 0) {
                        neighbors[nCount++] = dir;
                    }
                }
            }

            if (nCount == 0) {
                // backtrack
                top--;
                continue;
            }

            // pick random neighbor
            int dir = neighbors[random(0, nCount)];
            int nx = cx + dx[dir] * 2;
            int ny = cy + dy[dir] * 2;
            int bx = cx + dx[dir];
            int by = cy + dy[dir];

            // carve path
            maze[by][bx] = 1;
            maze[ny][nx] = 1;

            // push new cell
            top++;
            stackX[top] = nx;
            stackY[top] = ny;
        }

        // Set start and exit
        exitX = MAZE_WIDTH - 2;
        exitY = MAZE_HEIGHT - 2;
        maze[startY][startX] = 2;  // Start
        maze[exitY][exitX] = 3;    // Exit

        // Reset player position
        player.x = startX * CELL_SIZE + CELL_SIZE / 2.0f;
        player.y = startY * CELL_SIZE + CELL_SIZE / 2.0f;
    }
    
    /**
     * Check if position is valid (not a wall)
     */
    bool isValidPosition(float px, float py) {
        int cellX = (int)(px / CELL_SIZE);
        int cellY = (int)(py / CELL_SIZE);
        
        if (cellX < 0 || cellX >= MAZE_WIDTH || cellY < 0 || cellY >= MAZE_HEIGHT) {
            return false;
        }
        
        // Check if it's a wall
        if (maze[cellY][cellX] == 0) {
            return false;
        }
        
        // Check collision with walls more precisely
        float cellLeft = cellX * CELL_SIZE;
        float cellRight = (cellX + 1) * CELL_SIZE;
        float cellTop = cellY * CELL_SIZE;
        float cellBottom = (cellY + 1) * CELL_SIZE;
        
        // Check if player overlaps with wall boundaries
        if (maze[cellY][cellX] == 0) {
            if (px - player.size < cellRight && px + player.size > cellLeft &&
                py - player.size < cellBottom && py + player.size > cellTop) {
                return false;
            }
        }
        
        return true;
    }
    
    /**
     * Check if player reached exit
     */
    bool checkExit() {
        int cellX = (int)(player.x / CELL_SIZE);
        int cellY = (int)(player.y / CELL_SIZE);
        return (cellX == exitX && cellY == exitY);
    }

public:
    LabyrinthGame() 
        : gameOver(false), gameWon(false), level(1), lastUpdate(0), winTime(0) {
        generateMaze();
    }

    void start() override {
        gameOver = false;
        gameWon = false;
        level = 1;
        lastUpdate = millis();
        winTime = 0;
        generateMaze();
    }

    void reset() override {
        start();
    }

    void update(ControllerManager* input) override {
        if (gameOver) return;
        
        // If won, wait 800ms then advance to next level with new maze
        if (gameWon) {
            if (millis() - winTime > 800) {
                gameWon = false;
                level++;
                generateMaze();
            }
            return;
        }
        
        // Throttle updates
        unsigned long now = millis();
        if (now - lastUpdate < UPDATE_INTERVAL_MS) return;
        lastUpdate = now;
        
        // Update player position
        ControllerPtr p1 = input->getController(0);
        if (p1 && p1->isConnected()) {
            uint8_t dpad = p1->dpad();
            float newX = player.x;
            float newY = player.y;
            
            if (dpad & 0x01) {  // UP
                newY -= player.speed;
            }
            if (dpad & 0x02) {  // DOWN
                newY += player.speed;
            }
            if (dpad & 0x08) {  // LEFT
                newX -= player.speed;
            }
            if (dpad & 0x04) {  // RIGHT
                newX += player.speed;
            }
            
            // Try to move, only if valid position
            if (isValidPosition(newX, player.y)) {
                player.x = newX;
            }
            if (isValidPosition(player.x, newY)) {
                player.y = newY;
            }
            
            // Keep player within bounds
            player.x = constrain(player.x, player.size, PANEL_RES_X - player.size);
            player.y = constrain(player.y, player.size, PANEL_RES_Y - player.size);
        }
        
        // Check if reached exit
        if (checkExit()) {
            gameWon = true;
            winTime = millis();
        }
    }

    void draw(MatrixPanel_I2S_DMA* display) override {
        display->fillScreen(COLOR_BLACK);
        
        if (gameWon) {
            SmallFont::drawString(display, 8, 28, "LEVEL COMPLETE!", COLOR_GREEN);
            char levelStr[16];
            snprintf(levelStr, sizeof(levelStr), "LEVEL: %d", level);
            SmallFont::drawString(display, 8, 38, levelStr, COLOR_WHITE);
            return;
        }
        
        if (gameOver) {
            SmallFont::drawString(display, 8, 28, "GAME OVER", COLOR_RED);
            return;
        }
        
        // Softer colors for walls/exit
        uint16_t wallColor = display->color565(80, 120, 200);   // soft blue
        uint16_t pathColor = display->color565(10, 20, 40);    // near black
        uint16_t exitColor = display->color565(120, 220, 120); // soft green

        // Draw maze
        for (int y = 0; y < MAZE_HEIGHT; y++) {
            for (int x = 0; x < MAZE_WIDTH; x++) {
                int screenX = x * CELL_SIZE;
                int screenY = y * CELL_SIZE;
                
                if (maze[y][x] == 0) {
                    display->fillRect(screenX, screenY, CELL_SIZE, CELL_SIZE, wallColor);
                } else if (maze[y][x] == 3) {
                    display->fillRect(screenX, screenY, CELL_SIZE, CELL_SIZE, exitColor);
                } else {
                    display->fillRect(screenX, screenY, CELL_SIZE, CELL_SIZE, pathColor);
                }
            }
        }
        
        // Draw player
        display->fillRect(
            (int)player.x - player.size, 
            (int)player.y - player.size, 
            player.size * 2, 
            player.size * 2, 
            player.color
        );
        
        // Draw level info
        char levelStr[12];
        snprintf(levelStr, sizeof(levelStr), "L:%d", level);
        SmallFont::drawString(display, 2, 2, levelStr, COLOR_YELLOW);
    }

    bool isGameOver() override {
        return gameOver || gameWon;
    }
};

