#pragma once
#include <Arduino.h>
#include <EEPROM.h>

/**
 * EepromManager
 * -------------
 * Centralized EEPROM initialization and management for ESP32.
 *
 * IMPORTANT (ESP32 Arduino):
 * - EEPROM is implemented on top of NVS and requires EEPROM.begin(size) ONCE.
 * - If different translation units each have their own `static` state, you can
 *   end up with "initialized in one file, not initialized in another".
 *
 * To avoid that, EepromManager state and functions are implemented in
 * `EepromManager.cpp` (single definition).
 */
namespace EepromManager {
  // Layout:
  // - 0..63: Settings + future expansion
  // - 64..127: UserProfiles
  // - 128..: Leaderboard
  //
  // Leaderboard Storage is ~633 bytes starting at 128 => ~761 total.
  // Round up for safety / future growth.
  constexpr size_t TOTAL_SIZE = 1024;

  bool begin();
  bool isInitialized();
  bool commit();
  uint8_t readByte(size_t address);
  void writeByte(size_t address, uint8_t value);
}

