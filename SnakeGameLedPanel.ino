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
#include "TronGame.h"
#include "PongGame.h"
#include "BreakoutGame.h"
#include "ShooterGame.h"
#include "LabyrinthGame.h"
#include "TetrisGame.h"
#include "EmojisGame.h"
#include "AsteroidsGame.h"
#include "Menu.h"
#include "EepromManager.h"
#include "Settings.h"
#include "SettingsMenu.h"
#include "LeaderboardMenu.h"
#include "Leaderboard.h"
#include "UserProfiles.h"
#include "UserSelectMenu.h"
#include "SmallFont.h"

// ---------------------------------------------------------
// Globals
// ---------------------------------------------------------
MatrixPanel_I2S_DMA* dma_display = nullptr;

Menu menu;
SettingsMenu settingsMenu;
LeaderboardMenu leaderboardMenu;
UserSelectMenu userSelectMenu;
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
  STATE_USER_SELECT,
  STATE_LEADERBOARD,
  STATE_GAME_RUNNING
};

AppState currentState = STATE_NO_CONTROLLER;
// When controllers disconnect, we show the NO_CONTROLLER screen, but we keep the
// previous state so we can resume (especially important for in-progress games).
AppState resumeStateAfterController = STATE_MENU;
// When a controller connects and no user is bound yet, we go through
// STATE_USER_SELECT first, then continue here.
AppState nextStateAfterUserSelect = STATE_MENU;

// ---------------------------------------------------------
// Setup
// ---------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("BOOT: setup() reached");

  // -----------------------------------------------------
  // EEPROM Initialization (MUST be first, before any EEPROM operations)
  // -----------------------------------------------------
  if (!EepromManager::begin()) {
    Serial.println(F("[Init] FATAL: EEPROM initialization failed!"));
    while (true) { delay(1000); } // Halt
  }

  // Quick EEPROM header dumps for debugging persistence across reboots.
  auto dumpRange = [&](int base, int len, const __FlashStringHelper* label) {
    Serial.print(F("[EEPROM] dump "));
    Serial.print(label);
    Serial.print(F(" @"));
    Serial.print(base);
    Serial.print(F(": "));
    for (int i = 0; i < len; i++) {
      uint8_t b = EepromManager::readByte((size_t)(base + i));
      if (b < 16) Serial.print('0');
      Serial.print(b, HEX);
      Serial.print(' ');
    }
    Serial.println();
  };
  dumpRange(0, 16, F("settings"));
  dumpRange(64, 24, F("users"));
  dumpRange(128, 32, F("leaderboard"));

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

  // Force-load user profiles and leaderboard once at boot for debug visibility.
  Serial.print(F("[Init] Users="));
  Serial.println(UserProfiles::userCount());
  Serial.print(F("[Init] Leaderboard games="));
  Serial.println(Leaderboard::gameCount());
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
        // When a controller connects, always go through user selection.
        // UserSelectMenu will show either:
        // - a list of existing users + NEW (if any users are stored), or
        // - the 3-letter editor directly (if no users exist yet).
        nextStateAfterUserSelect = resumeStateAfterController;
        userSelectMenu.beginForPad(0);
        currentState = STATE_USER_SELECT;
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
        resumeStateAfterController = STATE_MENU;
        currentState = STATE_NO_CONTROLLER;
      } else {
        // Draw Menu (capped FPS to reduce scanline/tearing artifacts)
        if (shouldRenderNow(nowMs, lastMenuRenderMs, menuIntervalMs, forceMenuRender)) {
          menu.draw(dma_display, globalControllerManager);
          presentFrame(dma_display);
        }

        // Handle Input
        int gameSelection = menu.update(globalControllerManager);
        if (gameSelection != -1) {
          // Valid selection made
          int players = globalControllerManager->getConnectedCount();
          
          if (gameSelection == (Menu::NUM_OPTIONS - 1)) {  // Settings (last option)
            currentState = STATE_SETTINGS;
            settingsMenu.selected = 0;
            dma_display->clearScreen();
            forceMenuRender = true;
          } else if (gameSelection == (Menu::NUM_OPTIONS - 2)) { // Leaderboard (just before Settings)
            currentState = STATE_LEADERBOARD;
            dma_display->clearScreen();
            forceMenuRender = true;
          } else {
            if (currentGame != nullptr) delete currentGame;
            
            switch (gameSelection) {
              case 0:  // Snake
                currentGame = new SnakeGame();
                break;
              case 1:  // Tron
                currentGame = new TronGame();
                break;
              case 2:  // Pong
                currentGame = new PongGame();
                break;
              case 3:  // Breakout
                currentGame = new BreakoutGame();
                break;
              case 4:  // Shooter
                currentGame = new ShooterGame();
                break;
              case 5:  // Labyrinth
                currentGame = new LabyrinthGame();
                break;
              case 6:  // Tetris (only visible with 1 player)
                if (players == 1) {
                  currentGame = new TetrisGame();
                }
                break;
              case 7:  // Emojis
                currentGame = new EmojisGame();
                break;
              case 8:  // Asteroids (only visible with 1 player)
                if (players == 1) {
                  currentGame = new AsteroidsGame();
                }
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
        resumeStateAfterController = STATE_SETTINGS;
        currentState = STATE_NO_CONTROLLER;
      } else {
        // Draw Settings Menu (capped FPS)
        if (shouldRenderNow(nowMs, lastMenuRenderMs, menuIntervalMs, forceMenuRender)) {
          settingsMenu.draw(dma_display, globalControllerManager);
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

    // --- STATE: USER SELECT ---
    case STATE_USER_SELECT:
      if (globalControllerManager->getConnectedCount() == 0) {
        // If controllers disconnect mid-selection, go back to waiting.
        currentState = STATE_NO_CONTROLLER;
      } else {
        if (shouldRenderNow(nowMs, lastMenuRenderMs, menuIntervalMs, forceMenuRender)) {
          userSelectMenu.draw(dma_display, globalControllerManager);
          presentFrame(dma_display);
        }
        if (userSelectMenu.update(globalControllerManager)) {
          currentState = nextStateAfterUserSelect;
          dma_display->clearScreen();
          forceMenuRender = true;
          // Debounce the confirming 'A' press so it doesn't immediately select "Snake" in the menu.
          delay(250);
        }
      }
      break;

    // --- STATE: LEADERBOARD ---
    case STATE_LEADERBOARD:
      if (globalControllerManager->getConnectedCount() == 0) {
        resumeStateAfterController = STATE_LEADERBOARD;
        currentState = STATE_NO_CONTROLLER;
      } else {
        if (shouldRenderNow(nowMs, lastMenuRenderMs, menuIntervalMs, forceMenuRender)) {
          leaderboardMenu.draw(dma_display, globalControllerManager);
          presentFrame(dma_display);
        }
        if (leaderboardMenu.update(globalControllerManager)) {
          currentState = STATE_MENU;
          dma_display->clearScreen();
          forceMenuRender = true;
        }
      }
      break;

    // --- STATE: GAME RUNNING ---
    // Execute active game logic.
    case STATE_GAME_RUNNING:
      // Safety check for disconnects
      if (globalControllerManager->getConnectedCount() == 0) {
        // IMPORTANT: Do NOT delete the current game. We want to resume when the
        // controller comes back.
        resumeStateAfterController = STATE_GAME_RUNNING;
        currentState = STATE_NO_CONTROLLER;
      } else {
        if (currentGame) {
          // -----------------------------------------------------
          // Auto-submit score to leaderboard once per game run
          // -----------------------------------------------------
          static GameBase* lastGamePtr = nullptr;
          static bool submitted = false;
          if (currentGame != lastGamePtr) {
            lastGamePtr = currentGame;
            submitted = false;
          }
          if (!submitted && currentGame->isGameOver()) {
            // Only submit for games that opt in (see GameBase leaderboard methods).
            // NOTE: leaderboard methods are optional with safe defaults.
            if (currentGame->leaderboardEnabled()) {
              char tag[4];
              UserProfiles::getPadTag(0, tag);
              Leaderboard::submitScore(currentGame->leaderboardId(),
                                       currentGame->leaderboardName(),
                                       currentGame->leaderboardScore(),
                                       tag);
            }
            submitted = true;
          }

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