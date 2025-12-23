/**
 * SnakeGameLedPanel.ino
 *
 * Hardware: ESP32-WROOM + 64x64 HUB75 Matrix
 * Display library: ESP32-HUB75-MatrixPanel-I2S-DMA
 * Controller: Bluepad32
 */

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Bluepad32.h>

#include "engine/config.h"
#include "engine/DisplayPresent.h"
#include "engine/ControllerManager.h"
#include "engine/AudioManager.h"
#include "Games/Snake/SnakeGame.h"
#include "Games/Tron/TronGame.h"
#include "Games/Pong/PongGame.h"
#include "Games/Breakout/BreakoutGame.h"
#include "Games/Shooter/ShooterGame.h"
#include "Games/Labyrinth/LabyrinthGame.h"
#include "Games/Tetris/TetrisGame.h"
#include "Games/Asteroids/AsteroidsGame.h"
#include "Games/Music/MusicApp.h"
#include "Games/MVisual/MVisualApp.h"
#include "Games/BomberMan/BomberManGame.h"
#include "Games/Simon/SimonGame.h"
#include "Games/DinoRun/DinoRunGame.h"
#include "Games/Minesweeper/MinesweeperGame.h"
#include "Games/MatrixRain/MatrixRainApp.h"
#include "Games/LavaLamp/LavaLampApp.h"
#include "applet/Menu.h"
#include "engine/EepromManager.h"
#include "engine/Settings.h"
#include "applet/SettingsMenu.h"
#include "applet/LeaderboardMenu.h"
#include "engine/Leaderboard.h"
#include "engine/UserProfiles.h"
#include "applet/UserSelectMenu.h"
#include "applet/PauseMenu.h"
#include "component/SmallFont.h"

// ---------------------------------------------------------
// Globals
// ---------------------------------------------------------
MatrixPanel_I2S_DMA* dma_display = nullptr;

Menu menu;
SettingsMenu settingsMenu;
LeaderboardMenu leaderboardMenu;
UserSelectMenu userSelectMenu;
PauseMenu pauseMenu;
GameBase* currentGame = nullptr;
// Monotonic game-run token to avoid relying on pointer addresses (which can be reused).
// Incremented each time we start a NEW game instance from the menu.
uint32_t currentGameRunId = 0;

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
// Controller helpers (Bluepad32 API surface varies by version/controller)
// ---------------------------------------------------------
// We intentionally use SFINAE to check for different APIs without hard-binding.
struct ExitButton {
  template <typename T>
  static auto miscButtons(T* c, int) -> decltype(c->miscButtons(), uint16_t()) { return (uint16_t)c->miscButtons(); }
  template <typename T>
  static uint16_t miscButtons(T*, ...) { return 0; }

  template <typename T>
  static auto start(T* c, int) -> decltype(c->start(), bool()) { return (bool)c->start(); }
  template <typename T>
  static bool start(T*, ...) { return false; }
};

static inline bool isStartPressed(ControllerPtr ctl) {
  if (!ctl) return false;
  // Prefer a dedicated start() accessor if present.
  if (ExitButton::start(ctl, 0)) return true;

  // Otherwise fall back to miscButtons() bitmask (common in Bluepad32).
  // Convention in Bluepad32: START is typically bit 0x04 in miscButtons().
  const uint16_t misc = ExitButton::miscButtons(ctl, 0);
  return (misc & 0x04) != 0;
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
  STATE_PAUSE,
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
// Input edge helpers (START)
// ---------------------------------------------------------
static inline bool startPressedEdge(uint8_t padIndex, ControllerManager* input) {
  static bool lastPressed[MAX_GAMEPADS] = { false, false, false, false };
  if (!input || padIndex >= MAX_GAMEPADS) return false;

  ControllerPtr ctl = input->getController((int)padIndex);
  const bool pressed = (ctl != nullptr) && isStartPressed(ctl);
  const bool edge = pressed && !lastPressed[padIndex];
  lastPressed[padIndex] = pressed;
  return edge;
}

static inline bool aPressedEdge(uint8_t padIndex, ControllerManager* input) {
  static bool lastPressed[MAX_GAMEPADS] = { false, false, false, false };
  if (!input || padIndex >= MAX_GAMEPADS) return false;

  ControllerPtr ctl = input->getController((int)padIndex);
  const bool pressed = (ctl != nullptr) && (bool)ctl->a();
  const bool edge = pressed && !lastPressed[padIndex];
  lastPressed[padIndex] = pressed;
  return edge;
}

static inline bool bPressedEdge(uint8_t padIndex, ControllerManager* input) {
  static bool lastPressed[MAX_GAMEPADS] = { false, false, false, false };
  if (!input || padIndex >= MAX_GAMEPADS) return false;

  ControllerPtr ctl = input->getController((int)padIndex);
  const bool pressed = (ctl != nullptr) && (bool)ctl->b();
  const bool edge = pressed && !lastPressed[padIndex];
  lastPressed[padIndex] = pressed;
  return edge;
}

static inline int8_t firstPadWithStartEdge(ControllerManager* input) {
  if (!input) return -1;
  for (int i = 0; i < MAX_GAMEPADS; i++) {
    if (startPressedEdge((uint8_t)i, input)) return (int8_t)i;
  }
  return -1;
}

static inline int8_t firstPadWithAEdge(ControllerManager* input) {
  if (!input) return -1;
  for (int i = 0; i < MAX_GAMEPADS; i++) {
    if (aPressedEdge((uint8_t)i, input)) return (int8_t)i;
  }
  return -1;
}

static inline int8_t firstPadWithBEdge(ControllerManager* input) {
  if (!input) return -1;
  for (int i = 0; i < MAX_GAMEPADS; i++) {
    if (bPressedEdge((uint8_t)i, input)) return (int8_t)i;
  }
  return -1;
}

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

  // -----------------------------------------------------
  // Audio (buzzer) - basic service init
  // -----------------------------------------------------
  globalAudio.begin();
  #if DEBUG_AUDIO
  Serial.print(F("[Audio] begin() done. ENABLE_AUDIO="));
  Serial.print((int)ENABLE_AUDIO);
  Serial.print(F(" pin="));
  Serial.print((int)AUDIO_BUZZER_PIN);
  Serial.print(F(" ch="));
  Serial.print((int)AUDIO_PWM_CHANNEL);
  Serial.print(F(" soundEnabled="));
  Serial.println(globalSettings.isSoundEnabled() ? F("ON") : F("OFF"));
  #endif

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

  // Audio service tick (non-blocking)
  globalAudio.update();

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
              case 7:  // Asteroids (only visible with 1 player)
                if (players == 1) {
                  currentGame = new AsteroidsGame();
                }
                break;
              case 8:  // Music
                currentGame = new MusicApp();
                break;
              case 9:  // MVisual
                currentGame = new MVisualApp();
                break;
              case 10: // Bomber
                currentGame = new BomberManGame();
                break;
              case 11: // Simon
                currentGame = new SimonGame();
                break;
              case 12: // Dino
                currentGame = new DinoRunGame();
                break;
              case 13: // Mines
                currentGame = new MinesweeperGame();
                break;
              case 14: // Matrix
                currentGame = new MatrixRainApp();
                break;
              case 15: // Lava
                currentGame = new LavaLampApp();
                break;
              default:
                currentGame = nullptr;
                break;
            }
            
            if (currentGame != nullptr) {
              currentGame->start();
              // New game run started. Increment token (never rely on pointer equality).
              currentGameRunId++;
              currentState = STATE_GAME_RUNNING;
              forceGameRender = true;
            }
          }
        }

        // START in menu: open user select for the controller that pressed START.
        const int8_t sp = firstPadWithStartEdge(globalControllerManager);
        if (sp >= 0) {
          globalAudio.uiStartStop();
          nextStateAfterUserSelect = STATE_MENU;
          userSelectMenu.beginForPad((uint8_t)sp);
          currentState = STATE_USER_SELECT;
          dma_display->clearScreen();
          forceMenuRender = true;
          delay(300); // debounce START
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
          forceGameRender = true; // if we return into PAUSE/GAME, render immediately
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

    // --- STATE: PAUSE ---
    // Freeze game updates, draw the game as a background, overlay pause UI.
    case STATE_PAUSE:
      if (globalControllerManager->getConnectedCount() == 0) {
        resumeStateAfterController = STATE_PAUSE;
        currentState = STATE_NO_CONTROLLER;
      } else if (currentGame) {
        // Render underlying game + overlay (capped FPS using game pacing).
        gameIntervalMs = fpsToIntervalMs(currentGame->preferredRenderFps());
        if (shouldRenderNow(nowMs, lastGameRenderMs, gameIntervalMs, forceGameRender)) {
          currentGame->draw(dma_display);
          pauseMenu.draw(dma_display);
          presentFrame(dma_display);
        }

        // START toggles resume (edge-triggered to avoid instant re-pause)
        if (startPressedEdge(pauseMenu.pad(), globalControllerManager)) {
          globalAudio.uiStartStop();
          currentState = STATE_GAME_RUNNING;
          forceGameRender = true;
          delay(250);
          break;
        }

        const PauseMenu::Action a = pauseMenu.update(globalControllerManager);
        if (a == PauseMenu::ACTION_RESUME) {
          currentState = STATE_GAME_RUNNING;
          forceGameRender = true;
          delay(250);
        } else if (a == PauseMenu::ACTION_QUIT_TO_MENU) {
          delete currentGame;
          currentGame = nullptr;
          currentState = STATE_MENU;
          dma_display->clearScreen();
          forceMenuRender = true;
          delay(300);
        }
      } else {
        // No game to pause -> fallback to menu.
        currentState = STATE_MENU;
        forceMenuRender = true;
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
          // Update per-game render pacing (some games prefer lower FPS).
          gameIntervalMs = fpsToIntervalMs(currentGame->preferredRenderFps());

          // 1. Update Physics/Logic
          currentGame->update(globalControllerManager);

          // -----------------------------------------------------
          // Auto-submit score to leaderboard once per game run
          // (IMPORTANT: do this AFTER update(), so a game-over set during update()
          // is recorded before the first GAME OVER render)
          // -----------------------------------------------------
          static uint32_t submittedRunId = 0;
          static bool submitted = false;
          if (submittedRunId != currentGameRunId) {
            submittedRunId = currentGameRunId;
            submitted = false;
          }
          if (!submitted && currentGame->isGameOver()) {
            // Only submit for games that opt in (see GameBase leaderboard methods).
            // NOTE: leaderboard methods are optional with safe defaults.
            if (currentGame->leaderboardEnabled()) {
              #if DEBUG_LEADERBOARD
              Serial.print(F("[Engine] Game over detected, submitting score: gameId="));
              Serial.print(currentGame->leaderboardId());
              Serial.print(F(" gameName="));
              Serial.print(currentGame->leaderboardName());
              Serial.print(F(" score="));
              Serial.println(currentGame->leaderboardScore());
              #endif
              
              char tag[4];
              UserProfiles::getPadTag(0, tag);
              #if DEBUG_LEADERBOARD
              Serial.print(F("[Engine] Player tag: "));
              Serial.println(tag);
              #endif
              
              Leaderboard::submitScore(currentGame->leaderboardId(),
                                       currentGame->leaderboardName(),
                                       currentGame->leaderboardScore(),
                                       tag);
              #if DEBUG_LEADERBOARD
              Serial.println(F("[Engine] submitScore() call completed"));
              #endif
            } else {
              #if DEBUG_LEADERBOARD
              Serial.println(F("[Engine] Game over but leaderboard not enabled for this game"));
              #endif
            }
            submitted = true;
          }

          // 2. Render Frame (capped FPS to reduce tearing/scanline artifacts)
          if (shouldRenderNow(nowMs, lastGameRenderMs, gameIntervalMs, forceGameRender)) {
            currentGame->draw(dma_display);
            presentFrame(dma_display);
          }

          // -----------------------------------------------------
          // GAME OVER button mapping (global, consistent):
          // - A: new game
          // - B: back to menu
          // - START: back to menu (nothing to pause)
          // -----------------------------------------------------
          // IMPORTANT: We still evaluate edges every frame so holding a button
          // doesn't trigger immediately when the game-over state appears.
          const bool isOver = currentGame->isGameOver();
          const int8_t aPad = firstPadWithAEdge(globalControllerManager);
          const int8_t bPad = firstPadWithBEdge(globalControllerManager);
          const int8_t startPad = firstPadWithStartEdge(globalControllerManager);

          if (isOver) {
            if (aPad >= 0) {
              currentGame->reset();
              currentGameRunId++; // treat as a new run for leaderboard submission
              forceGameRender = true;
              delay(250);
            } else if (bPad >= 0 || startPad >= 0) {
              if (startPad >= 0) globalAudio.uiStartStop();
              delete currentGame;
              currentGame = nullptr;
              currentState = STATE_MENU;
              dma_display->clearScreen();
              forceMenuRender = true;
              delay(300);
            }
          } else {
            // START in-game: open the pause menu (do NOT exit the game).
            if (startPad >= 0) {
              globalAudio.uiStartStop();
              pauseMenu.beginForPad((uint8_t)startPad);
              currentState = STATE_PAUSE;
              forceGameRender = true;
              delay(300);  // Debounce START press
            }
          }
        }
      }
      break;
  }

  // Small yield to feed Watchdog Timer (WDT)
  // Bluepad32 and DMA lib usually play nice, but this is safe practice
  delay(1);
}