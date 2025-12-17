#pragma once
#include <Arduino.h>
#include <EEPROM.h>
#include "EepromManager.h"
#include <stddef.h> // offsetof

/**
 * Leaderboard (EEPROM-backed)
 * ---------------------------
 * Stores high scores for any game, keyed by a stable string `gameId`.
 *
 * Design constraints:
 * - EEPROM is limited (ESP32 EEPROM emulation, currently initialized to 512 bytes in Settings).
 * - Keep layout fixed-size and forward-compatible (magic + version + checksum).
 * - "Dynamic": new games can appear simply by calling submitScore() with a new id/name.
 *
 * How games use it:
 * - Call: `Leaderboard::submitScore("snake", "Snake", score);`
 * - The engine also auto-submits on `isGameOver()` for games that implement
 *   the optional GameBase leaderboard methods (see `GameBase.h` changes).
 */

namespace Leaderboard {

// Keep this safely away from Settings and user profiles at EEPROM address 0.
// Settings uses the very beginning; user profiles reserve the next block;
// leaderboard starts further into the EEPROM arena.
static constexpr int EEPROM_BASE_ADDR = 128;

static constexpr uint32_t MAGIC = 0x4C424452; // 'LBDR'
static constexpr uint8_t VERSION = 2;

static constexpr uint8_t MAX_GAMES = 12;
static constexpr uint8_t TOP_SCORES = 5;
static constexpr uint8_t NAME_LEN = 7; // visible characters (stored as NAME_LEN+1 with NUL)

struct Entry {
    uint32_t idHash;                 // hash(gameId)
    char name[NAME_LEN + 1];         // display name (truncated)
    uint32_t scores[TOP_SCORES];     // sorted descending
    char initials[TOP_SCORES][4];    // 3-char tag + NUL for each score
} __attribute__((packed));

struct Storage {
    uint32_t magic;
    uint8_t version;
    uint8_t gameCount;
    uint8_t reserved[2]; // alignment / future use
    Entry entries[MAX_GAMES];
    uint8_t checksum;    // XOR checksum across all previous bytes
} __attribute__((packed));

static constexpr size_t CHECKSUM_LEN = offsetof(Storage, checksum);

static inline uint32_t fnv1a32(const char* s) {
    // 32-bit FNV-1a
    uint32_t h = 2166136261u;
    if (!s) return h;
    while (*s) {
        h ^= (uint8_t)(*s++);
        h *= 16777619u;
    }
    return h;
}

static inline uint8_t checksumXor(const uint8_t* data, size_t len) {
    uint8_t x = 0;
    for (size_t i = 0; i < len; i++) x ^= data[i];
    return x;
}

static inline void safeCopyName(char out[NAME_LEN + 1], const char* name) {
    // Truncate to NAME_LEN and always NUL-terminate.
    if (!name) {
        out[0] = '\0';
        return;
    }
    strncpy(out, name, NAME_LEN);
    out[NAME_LEN] = '\0';
}

// In-memory cache (loaded on first use).
static Storage gStore;
static bool gLoaded = false;

static inline void initEmpty() {
    memset(&gStore, 0, sizeof(gStore));
    gStore.magic = MAGIC;
    gStore.version = VERSION;
    gStore.gameCount = 0;
}

static inline void save() {
    // Compute checksum excluding checksum byte itself.
    gStore.checksum = checksumXor((const uint8_t*)&gStore, CHECKSUM_LEN);
    EEPROM.put(EEPROM_BASE_ADDR, gStore);
    const bool ok = EepromManager::commit();
    if (!ok) {
        Serial.println(F("[Leaderboard] ERROR: EEPROM commit failed!"));
    }
}

static inline void load() {
    if (gLoaded) return;
    gLoaded = true;

    Serial.println(F("[Leaderboard] load()"));
    EEPROM.get(EEPROM_BASE_ADDR, gStore);
    const uint8_t calc = checksumXor((const uint8_t*)&gStore, CHECKSUM_LEN);

    const bool ok = (gStore.magic == MAGIC) &&
                    (gStore.version == VERSION) &&
                    (gStore.checksum == calc) &&
                    (gStore.gameCount <= MAX_GAMES);

    if (!ok) {
        Serial.print(F("[Leaderboard] invalid -> magic=0x"));
        Serial.print(gStore.magic, HEX);
        Serial.print(F(" ver="));
        Serial.print(gStore.version);
        Serial.print(F(" games="));
        Serial.print(gStore.gameCount);
        Serial.print(F(" checksum="));
        Serial.print(gStore.checksum, HEX);
        Serial.print(F(" calc="));
        Serial.println(calc, HEX);

        Serial.print(F("[Leaderboard] dump @"));
        Serial.print(EEPROM_BASE_ADDR);
        Serial.print(F(": "));
        for (int i = 0; i < 32; i++) {
            Serial.print(EepromManager::readByte(EEPROM_BASE_ADDR + i), HEX);
            Serial.print(' ');
        }
        Serial.println();
        initEmpty();
        save();
    } else {
        Serial.print(F("[Leaderboard] OK games="));
        Serial.println(gStore.gameCount);
    }
}

static inline int findEntryIndex(uint32_t idHash) {
    load();
    for (int i = 0; i < (int)gStore.gameCount; i++) {
        if (gStore.entries[i].idHash == idHash) return i;
    }
    return -1;
}

static inline void sortAndClamp(Entry& e) {
    // Insertion sort (TOP_SCORES is tiny).
    for (int i = 1; i < (int)TOP_SCORES; i++) {
        uint32_t key = e.scores[i];
        int j = i - 1;
        while (j >= 0 && e.scores[j] < key) {
            e.scores[j + 1] = e.scores[j];
            j--;
        }
        e.scores[j + 1] = key;
    }
}

static inline void normalizeInitials(char out[4], const char* in) {
    if (!in || !in[0]) {
        out[0] = out[1] = out[2] = '-';
        out[3] = '\0';
        return;
    }
    for (int i = 0; i < 3; i++) {
        char c = in[i];
        if (c == '\0') {
            out[i] = '-';
        } else {
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            if (c < 'A' || c > 'Z') c = '-';
            out[i] = c;
        }
    }
    out[3] = '\0';
}

static inline void submitScore(const char* gameId, const char* gameName, uint32_t score, const char* playerInitials) {
    if (!gameId) return;
    load();

    // Ignore "empty" submissions by default.
    if (score == 0) return;

    char normInit[4];
    normalizeInitials(normInit, playerInitials);

    const uint32_t idHash = fnv1a32(gameId);
    int idx = findEntryIndex(idHash);

    if (idx < 0) {
        if (gStore.gameCount >= MAX_GAMES) {
            // EEPROM is full; do not evict automatically (keeps behavior predictable).
            return;
        }
        idx = (int)gStore.gameCount++;
        Entry& e = gStore.entries[idx];
        memset(&e, 0, sizeof(e));
        e.idHash = idHash;
        safeCopyName(e.name, gameName ? gameName : gameId);
    }

    Entry& e = gStore.entries[idx];
    // Update name if we previously had an empty string (first boot / older record).
    if (e.name[0] == '\0' && gameName) safeCopyName(e.name, gameName);

    // Insert score/initials into the TOP_SCORES array if it fits.
    for (int i = 0; i < (int)TOP_SCORES; i++) {
        if (score > e.scores[i]) {
            // Shift down and insert.
            for (int j = (int)TOP_SCORES - 1; j > i; j--) {
                e.scores[j] = e.scores[j - 1];
                memcpy(e.initials[j], e.initials[j - 1], 4);
            }
            e.scores[i] = score;
            memcpy(e.initials[i], normInit, 4);
            break;
        }
    }
    sortAndClamp(e);
    save();
}

// Backward-compatible helper for callers not yet providing initials.
static inline void submitScore(const char* gameId, const char* gameName, uint32_t score) {
    submitScore(gameId, gameName, score, "???");
}

static inline uint8_t gameCount() {
    load();
    return gStore.gameCount;
}

static inline const Entry* entryAt(uint8_t index) {
    load();
    if (index >= gStore.gameCount) return nullptr;
    return &gStore.entries[index];
}

static inline void clearAll() {
    initEmpty();
    save();
}

} // namespace Leaderboard


