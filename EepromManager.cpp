#include "EepromManager.h"

namespace EepromManager {

static bool gInitialized = false;

bool begin() {
  if (gInitialized) {
    Serial.println(F("[EEPROM] Already initialized"));
    return true;
  }

  Serial.print(F("[EEPROM] Initializing with size: "));
  Serial.println((unsigned)TOTAL_SIZE);

  const bool ok = EEPROM.begin(TOTAL_SIZE);
  if (!ok) {
    Serial.println(F("[EEPROM] ERROR: begin() failed!"));
    return false;
  }

  gInitialized = true;
  Serial.println(F("[EEPROM] Initialization successful"));
  return true;
}

bool isInitialized() { return gInitialized; }

bool commit() {
  if (!gInitialized) {
    Serial.println(F("[EEPROM] ERROR: commit() called before begin()!"));
    return false;
  }

  const uint32_t t0 = millis();
  Serial.println(F("[EEPROM] commit() start"));
  delay(0);
  const bool ok = EEPROM.commit();
  delay(0);
  const uint32_t dt = (uint32_t)(millis() - t0);
  if (!ok) Serial.println(F("[EEPROM] ERROR: commit() failed!"));
  else Serial.println(F("[EEPROM] commit() successful"));
  Serial.print(F("[EEPROM] commit() dtMs="));
  Serial.println(dt);
  return ok;
}

uint8_t readByte(size_t address) {
  if (!gInitialized) {
    Serial.println(F("[EEPROM] ERROR: readByte() called before begin()!"));
    return 0;
  }
  return EEPROM.read(address);
}

void writeByte(size_t address, uint8_t value) {
  if (!gInitialized) {
    Serial.println(F("[EEPROM] ERROR: writeByte() called before begin()!"));
    return;
  }
  EEPROM.write(address, value);
}

} // namespace EepromManager


