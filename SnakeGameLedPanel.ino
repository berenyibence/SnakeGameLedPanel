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
#include "ControllerManager.h"
#include "SnakeGame.h"
#include "PongGame.h"
#include "Menu.h"

// ---------------------------------------------------------
// Globals
// ---------------------------------------------------------
MatrixPanel_I2S_DMA* dma_display = nullptr;

Menu menu;
GameBase* currentGame = nullptr;

// ---------------------------------------------------------
// App State
// ---------------------------------------------------------
enum AppState {
  STATE_NO_CONTROLLER,
  STATE_MENU,
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
  //mxconfig.double_buff = false;
  mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_10M;

  // Try one of these (test both with panel power-cycle)
  mxconfig.clkphase = false;  // then try true
  //mxconfig.clkphase = true;

  // Optional if supported by your library build
  mxconfig.driver = HUB75_I2S_CFG::FM6126A;


  dma_display = new MatrixPanel_I2S_DMA(mxconfig);


  //dma_display->setBrightness8(10);  // try 10–30
  //mxconfig.latch_blanking = 4;  // if available in your 3.0.13 build

  if (!dma_display->begin()) {
    Serial.println("ERROR: Display begin() failed");
    while (true) {}
  }
  //dma_display->setBrightness8(255);
  dma_display->setBrightness8(80);
  dma_display->clearScreen();


  // -----------------------------------------------------
  // Display sanity check
  // -----------------------------------------------------
  dma_display->setTextColor(dma_display->color565(255, 255, 255));
  dma_display->setCursor(0, 0);
  dma_display->println("DISPLAY OK");

  Serial.println("[Init] Display Service Started");
}

// ---------------------------------------------------------
// Main Loop
// ---------------------------------------------------------
void loop() {
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
      } else {
        // Render waiting screen
        static unsigned long lastFrame = 0;
        if (millis() - lastFrame > 500) {  // Blink effect
          dma_display->fillScreen(0);
          dma_display->setCursor(2, 20);
          dma_display->setTextColor(COLOR_RED);
          dma_display->print("NO GAMEPAD");

          dma_display->setCursor(2, 35);
          dma_display->setTextColor(COLOR_WHITE);
          dma_display->print("Connect BT");

          dma_display->setCursor(2, 50);
          dma_display->setTextColor(COLOR_BLUE);
          dma_display->print("Scanning...");
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
        // Draw Menu
        menu.draw(dma_display, globalControllerManager->getConnectedCount());

        // Handle Input
        int gameSelection = menu.update(globalControllerManager);
        if (gameSelection != -1) {
          // Valid selection made
          if (currentGame != nullptr) delete currentGame;
          
          if (gameSelection == 0) {  // Snake
            currentGame = new SnakeGame();
          } else if (gameSelection == 1) {  // Pong
            currentGame = new PongGame();
          }
          
          if (currentGame != nullptr) {
            currentGame->start();
            currentState = STATE_GAME_RUNNING;
          }
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
          // 1. Update Physics/Logic
          currentGame->update(globalControllerManager);

          // 2. Render Frame
          currentGame->draw(dma_display);

          // 3. Check for User Exit (Hold 'B' for 2 seconds?)
          // For now, relies on Game Over state to reset.
          // Implementation of "Exit to Menu" can be added here:
          ControllerPtr p1 = globalControllerManager->getController(0);
          if (p1 && p1->b() && currentGame->isGameOver()) {
            delete currentGame;
            currentGame = nullptr;
            currentState = STATE_MENU;
            dma_display->clearScreen();
            delay(500);  // Debounce 'B' press
          }
        }
      }
      break;
  }

  // Small yield to feed Watchdog Timer (WDT)
  // Bluepad32 and DMA lib usually play nice, but this is safe practice
  delay(1);
}