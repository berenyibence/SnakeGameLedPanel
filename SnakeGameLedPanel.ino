/**
 * SnakeGameLedPanel.ino
 *
 * Hardware: ESP32-WROOM + 64x64 HUB75 Matrix
 * Display library: ESP32-HUB75-MatrixPanel-I2S-DMA
 * Controller: Bluepad32
 */

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Bluepad32.h>

#include "config.h"
#include "DisplayPresent.h"
#include "ControllerManager.h"
#include "SnakeGame.h"
#include "PongGame.h"
#include "BreakoutGame.h"
#include "ShooterGame.h"
#include "LabyrinthGame.h"
#include "TetrisGame.h"
#include "EmojisGame.h"
#include "Menu.h"
#include "Settings.h"
#include "SettingsMenu.h"
#include "SmallFont.h"

// ---------------------------------------------------------
// Globals
// ---------------------------------------------------------
MatrixPanel_I2S_DMA* dma_display = nullptr;

Menu menu;
SettingsMenu settingsMenu;
GameBase* currentGame = nullptr;

// ---------------------------------------------------------
// Frame pacing / presentation helpers
// ---------------------------------------------------------
// We intentionally cap how often we redraw the framebuffer.
// Reason: On HUB75 DMA panels, continuously rewriting the single framebuffer
// while it is being scanned out can show as "scanlines"/row tearing—especially
// on content with lots of motion/contrast (like Snake).
static inline uint32_t fpsToIntervalMs(uint32_t fps) {
  if (fps == 0) return 0;
  return (uint32_t)(1000UL / fps);
}

static inline bool shouldRenderNow(uint32_t nowMs, uint32_t& lastRenderMs, uint32_t intervalMs, bool& force) {
  if (force) {
    force = false;
    lastRenderMs = nowMs;
    return true;
  }
  if (intervalMs == 0) return true; // uncapped (not recommended)
  if ((uint32_t)(nowMs - lastRenderMs) >= intervalMs) {
    lastRenderMs = nowMs;
    return true;
  }
  return false;
}

// ---------------------------------------------------------
// App State
// ---------------------------------------------------------
enum AppState {
  STATE_NO_CONTROLLER,
  STATE_MENU,
  STATE_SETTINGS,
  STATE_GAME_RUNNING
};

AppState currentState = STATE_NO_CONTROLLER;

// ---------------------------------------------------------
// Setup
// ---------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("BOOT: setup() reached");

  // -----------------------------------------------------
  // Bluetooth / Controllers
  // -----------------------------------------------------
  globalControllerManager = new ControllerManager();
  globalControllerManager->setup();
  Serial.println("[Init] Bluepad32 Service Started");

  // -----------------------------------------------------
  // DISPLAY CONFIG
  // IMPORTANT:
  // This MATCHES the WORKING example you posted
  // -----------------------------------------------------
  HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);

  // Explicit wiring (since your E is GPIO32 and that fixed it)
  mxconfig.gpio.r1 = R1_PIN;
  mxconfig.gpio.g1 = G1_PIN;
  mxconfig.gpio.b1 = B1_PIN;
  mxconfig.gpio.r2 = R2_PIN;
  mxconfig.gpio.g2 = G2_PIN;
  mxconfig.gpio.b2 = B2_PIN;

  mxconfig.gpio.a = A_PIN;
  mxconfig.gpio.b = B_PIN;
  mxconfig.gpio.c = C_PIN;
  mxconfig.gpio.d = D_PIN;
  mxconfig.gpio.e = E_PIN;

  mxconfig.gpio.lat = LAT_PIN;
  mxconfig.gpio.oe = OE_PIN;
  mxconfig.gpio.clk = CLK_PIN;

  // Stability first
  mxconfig.double_buff = ENABLE_DOUBLE_BUFFER;
  //mxconfig.driver = HUB75_I2S_CFG::FM6126A;
  mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_8M;

  // Try one of these (test both with panel power-cycle)
  mxconfig.clkphase = false; 



  dma_display = new MatrixPanel_I2S_DMA(mxconfig);


  //dma_display->setBrightness8(30);  // try 10–30
  //mxconfig.latch_blanking = 4;  // if available in your 3.0.13 build

  if (!dma_display->begin()) {
    Serial.println("ERROR: Display begin() failed");
    while (true) {}
  }
  
  // Load settings and apply brightness
  Serial.println(F("[Init] Loading settings..."));
  globalSettings.load();
  uint8_t startupBrightness = globalSettings.getBrightness();
  if (startupBrightness < 30) {
    Serial.print(F("[Init] Brightness from settings too low ("));
    Serial.print(startupBrightness);
    Serial.println(F(") -> forcing to 200"));
    startupBrightness = 200;
  }
  Serial.print(F("[Init] Applying brightness from settings: "));
  Serial.println(startupBrightness);
  dma_display->setBrightness8(startupBrightness);
  dma_display->clearScreen();


  // -----------------------------------------------------
  // Display sanity check
  // -----------------------------------------------------
  dma_display->setTextColor(dma_display->color565(255, 255, 255));
  dma_display->setCursor(0, 0);
  dma_display->println("DISPLAY OK");
  presentFrame(dma_display);

  Serial.println("[Init] Display Service Started");
}

// ---------------------------------------------------------
// Main Loop
// ---------------------------------------------------------
void loop() {
  // Frame pacing
  static uint32_t lastMenuRenderMs = 0;
  static uint32_t lastGameRenderMs = 0;
  static bool forceMenuRender = true;
  static bool forceGameRender = true;
  const uint32_t nowMs = millis();
  const uint32_t menuIntervalMs = fpsToIntervalMs(MENU_RENDER_FPS);
  // Game interval is selected per-game (see GameBase::preferredRenderFps()).
  uint32_t gameIntervalMs = fpsToIntervalMs(GAME_RENDER_FPS);
  if (currentGame) {
    gameIntervalMs = fpsToIntervalMs(currentGame->preferredRenderFps());
  }

  // 1. Hardware/Protocol Updates
  // Allow Bluepad32 to process incoming packets (Required)
  globalControllerManager->update();

  // 2. State Machine Logic
  switch (currentState) {

    // --- STATE: NO CONTROLLER ---
    // Display "Connect Controller" screen until a device connects.
    case STATE_NO_CONTROLLER:
      if (globalControllerManager->getConnectedCount() > 0) {
        // Transition to Menu
        currentState = STATE_MENU;
        dma_display->clearScreen();
        forceMenuRender = true;
      } else {
        // Render waiting screen with small font
        static unsigned long lastFrame = 0;
        if (millis() - lastFrame > 500) {  // Blink effect
          dma_display->fillScreen(0);
          SmallFont::drawString(dma_display, 10, 18, "NO GAMEPAD", COLOR_RED);
          SmallFont::drawString(dma_display, 10, 28, "Connect BT", COLOR_WHITE);
          SmallFont::drawString(dma_display, 11, 38, "Scanning...", COLOR_BLUE);
          presentFrame(dma_display);
          lastFrame = millis();
        }
      }
      break;

    // --- STATE: MAIN MENU ---
    // Select game.
    case STATE_MENU:
      // If all controllers disconnect, go back to waiting
      if (globalControllerManager->getConnectedCount() == 0) {
        currentState = STATE_NO_CONTROLLER;
      } else {
        // Draw Menu (capped FPS to reduce scanline/tearing artifacts)
        if (shouldRenderNow(nowMs, lastMenuRenderMs, menuIntervalMs, forceMenuRender)) {
          menu.draw(dma_display, globalControllerManager->getConnectedCount());
          presentFrame(dma_display);
        }

        // Handle Input
        int gameSelection = menu.update(globalControllerManager);
        if (gameSelection != -1) {
          // Valid selection made
          int players = globalControllerManager->getConnectedCount();
          
          if (gameSelection == 7) {  // Settings
            currentState = STATE_SETTINGS;
            settingsMenu.selected = 0;
            dma_display->clearScreen();
            forceMenuRender = true;
          } else {
            if (currentGame != nullptr) delete currentGame;
            
            switch (gameSelection) {
              case 0:  // Snake
                currentGame = new SnakeGame();
                break;
              case 1:  // Pong
                currentGame = new PongGame();
                break;
              case 2:  // Breakout
                currentGame = new BreakoutGame();
                break;
              case 3:  // Shooter
                currentGame = new ShooterGame();
                break;
              case 4:  // Labyrinth
                currentGame = new LabyrinthGame();
                break;
              case 5:  // Tetris (only visible with 1 player)
                if (players == 1) {
                  currentGame = new TetrisGame();
                }
                break;
              case 6:  // Emojis
                currentGame = new EmojisGame();
                break;
              default:
                currentGame = nullptr;
                break;
            }
            
            if (currentGame != nullptr) {
              currentGame->start();
              currentState = STATE_GAME_RUNNING;
              forceGameRender = true;
            }
          }
        }
      }
      break;

    // --- STATE: SETTINGS MENU ---
    case STATE_SETTINGS:
      // If all controllers disconnect, go back to waiting
      if (globalControllerManager->getConnectedCount() == 0) {
        currentState = STATE_NO_CONTROLLER;
      } else {
        // Draw Settings Menu (capped FPS)
        if (shouldRenderNow(nowMs, lastMenuRenderMs, menuIntervalMs, forceMenuRender)) {
          settingsMenu.draw(dma_display);
          presentFrame(dma_display);
        }
        
        // Handle Input
        if (settingsMenu.update(globalControllerManager)) {
          // User wants to go back
          currentState = STATE_MENU;
          dma_display->clearScreen();
          forceMenuRender = true;
          // Apply brightness if it was changed
          dma_display->setBrightness8(globalSettings.getBrightness());
        }
      }
      break;

    // --- STATE: GAME RUNNING ---
    // Execute active game logic.
    case STATE_GAME_RUNNING:
      // Safety check for disconnects
      if (globalControllerManager->getConnectedCount() == 0) {
        currentState = STATE_NO_CONTROLLER;
        if (currentGame) {
          delete currentGame;
          currentGame = nullptr;
        }
      } else {
        if (currentGame) {
          // Update per-game render pacing (some games prefer lower FPS).
          gameIntervalMs = fpsToIntervalMs(currentGame->preferredRenderFps());

          // 1. Update Physics/Logic
          currentGame->update(globalControllerManager);

          // 2. Render Frame (capped FPS to reduce tearing/scanline artifacts)
          if (shouldRenderNow(nowMs, lastGameRenderMs, gameIntervalMs, forceGameRender)) {
            currentGame->draw(dma_display);
            presentFrame(dma_display);
          }

          ControllerPtr p1 = globalControllerManager->getController(0);
          if (p1 && p1->b()) {
            delete currentGame;
            currentGame = nullptr;
            currentState = STATE_MENU;
            dma_display->clearScreen();
            forceMenuRender = true;
            delay(300);  // Debounce 'B' press
          }
        }
      }
      break;
  }

  // Small yield to feed Watchdog Timer (WDT)
  // Bluepad32 and DMA lib usually play nice, but this is safe practice
  delay(1);
}