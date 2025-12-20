#pragma once
#include <Arduino.h>
#include <EEPROM.h>
#include "config.h"
#include "EepromManager.h"
#include <stddef.h> // offsetof

/**
 * UserProfiles
 * ------------
 * Small EEPROM-backed list of 3-letter uppercase user tags, plus a runtime
 * mapping from controller index -> selected user.
 *
 * - Each user tag is exactly 3 characters (stored as 3 chars + NUL).
 * - Up to MAX_USERS profiles.
 * - Mapping from controller index is kept in RAM only (per session).
 */

namespace UserProfiles {

static constexpr int EEPROM_BASE_ADDR = 64;
static constexpr uint32_t MAGIC = 0x5550524F; // 'UPRO'
static constexpr uint8_t VERSION = 1;
static constexpr uint8_t MAX_USERS = 8;

struct User {
    char tag[4]; // 3 chars + NUL
} __attribute__((packed));

struct Storage {
    uint32_t magic;
    uint8_t version;
    uint8_t userCount;
    uint8_t reserved[2];
    User users[MAX_USERS];
    uint8_t checksum;
} __attribute__((packed));

static constexpr size_t CHECKSUM_LEN = offsetof(Storage, checksum);

static Storage gStore;
static bool gLoaded = false;
static int8_t gPadUserIndex[MAX_GAMEPADS] = { -1, -1, -1, -1 };

static inline uint8_t checksumXor(const uint8_t* data, size_t len) {
    uint8_t x = 0;
    for (size_t i = 0; i < len; i++) x ^= data[i];
    return x;
}

static inline void initEmpty() {
    memset(&gStore, 0, sizeof(gStore));
    gStore.magic = MAGIC;
    gStore.version = VERSION;
    gStore.userCount = 0;
}

static inline void save() {
    gStore.checksum = checksumXor((const uint8_t*)&gStore, CHECKSUM_LEN);
    EEPROM.put(EEPROM_BASE_ADDR, gStore);
    const bool ok = EepromManager::commit();
    if (!ok) {
        Serial.println(F("[UserProfiles] ERROR: EEPROM commit failed!"));
    }
}

static inline void load() {
    if (gLoaded) return;
    gLoaded = true;

    Serial.println(F("[UserProfiles] load()"));
    EEPROM.get(EEPROM_BASE_ADDR, gStore);
    const uint8_t calc = checksumXor((const uint8_t*)&gStore, CHECKSUM_LEN);
    const bool ok = (gStore.magic == MAGIC) &&
                    (gStore.version == VERSION) &&
                    (gStore.checksum == calc) &&
                    (gStore.userCount <= MAX_USERS);
    if (!ok) {
        Serial.print(F("[UserProfiles] invalid -> magic=0x"));
        Serial.print(gStore.magic, HEX);
        Serial.print(F(" ver="));
        Serial.print(gStore.version);
        Serial.print(F(" users="));
        Serial.print(gStore.userCount);
        Serial.print(F(" checksum="));
        Serial.print(gStore.checksum, HEX);
        Serial.print(F(" calc="));
        Serial.println(calc, HEX);

        Serial.print(F("[UserProfiles] dump @"));
        Serial.print(EEPROM_BASE_ADDR);
        Serial.print(F(": "));
        for (int i = 0; i < 24; i++) {
            Serial.print(EepromManager::readByte(EEPROM_BASE_ADDR + i), HEX);
            Serial.print(' ');
        }
        Serial.println();
        initEmpty();
        save();
    } else {
        Serial.print(F("[UserProfiles] OK users="));
        Serial.println(gStore.userCount);
    }
}

static inline uint8_t userCount() {
    load();
    return gStore.userCount;
}

static inline const char* userTag(uint8_t index) {
    load();
    if (index >= gStore.userCount) return nullptr;
    return gStore.users[index].tag;
}

static inline uint8_t createUser(const char tag[4]) {
    load();
    if (gStore.userCount >= MAX_USERS) return gStore.userCount;
    const uint8_t idx = gStore.userCount++;
    memcpy(gStore.users[idx].tag, tag, 4);
    save();
    return idx;
}

static inline void setPadUserIndex(uint8_t padIndex, int8_t userIndex) {
    if (padIndex >= MAX_GAMEPADS) return;
    gPadUserIndex[padIndex] = userIndex;
}

static inline int8_t padUserIndex(uint8_t padIndex) {
    if (padIndex >= MAX_GAMEPADS) return -1;
    return gPadUserIndex[padIndex];
}

static inline void getPadTag(uint8_t padIndex, char outTag[4]) {
    const int8_t idx = padUserIndex(padIndex);
    if (idx < 0 || (uint8_t)idx >= userCount()) {
        outTag[0] = outTag[1] = outTag[2] = '-';
        outTag[3] = '\0';
        return;
    }
    const char* src = userTag((uint8_t)idx);
    if (!src) {
        outTag[0] = outTag[1] = outTag[2] = '-';
        outTag[3] = '\0';
        return;
    }
    memcpy(outTag, src, 4);
}

} // namespace UserProfiles


