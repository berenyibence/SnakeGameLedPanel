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
static constexpr uint8_t VERSION = 3;

// EEPROM arena is 1024 bytes (see EepromManager). With Entry ~= 52 bytes,
// MAX_GAMES=16 keeps storage under the available arena (starting at 128).
static constexpr uint8_t MAX_GAMES = 16;
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

// Forward declarations (Arduino header compilation order can be surprising).
static inline bool isRamHeaderSane();
static inline bool isRamChecksumSane();
static inline void ensureLoadedAndSane();

static inline void save() {
    #if DEBUG_LEADERBOARD
    Serial.print(F("[Leaderboard] save() called: gameCount="));
    Serial.println(gStore.gameCount);
    Serial.print(F("[Leaderboard] freeHeap="));
    Serial.println(ESP.getFreeHeap());
    #endif
    
    // IMPORTANT:
    // Do NOT validate checksum here. We are about to mutate checksum based on the
    // current RAM contents. Validating the *old* checksum would incorrectly mark
    // normal modifications (new scores) as "corruption" and prevent saving.
    if (!isRamHeaderSane()) {
        Serial.println(F("[Leaderboard] ERROR: RAM header invalid, refusing to save"));
        return;
    }
    
    // Compute checksum excluding checksum byte itself.
    gStore.checksum = checksumXor((const uint8_t*)&gStore, CHECKSUM_LEN);
    #if DEBUG_LEADERBOARD
    Serial.print(F("[Leaderboard] Computed checksum: 0x"));
    Serial.println(gStore.checksum, HEX);
    #endif
    
    const size_t bytes = sizeof(Storage);
    #if DEBUG_LEADERBOARD
    Serial.print(F("[Leaderboard] Writing "));
    Serial.print((unsigned)bytes);
    Serial.print(F(" bytes to EEPROM @"));
    Serial.println(EEPROM_BASE_ADDR);
    #endif

    // Bounds check against configured EEPROM arena size.
    if ((size_t)EEPROM_BASE_ADDR + bytes > EepromManager::TOTAL_SIZE) {
        #if DEBUG_LEADERBOARD
        Serial.println(F("[Leaderboard] ERROR: EEPROM arena too small for leaderboard storage!"));
        #endif
        return;
    }

    // Write byte-by-byte (more predictable than EEPROM.put on some cores) + yield.
    const uint8_t* p = (const uint8_t*)&gStore;
    for (size_t i = 0; i < bytes; i++) {
        EEPROM.write((int)(EEPROM_BASE_ADDR + i), p[i]);
        if ((i & 0x3F) == 0x3F) { // every 64 bytes
            delay(0);
        }
    }
    #if DEBUG_LEADERBOARD
    Serial.println(F("[Leaderboard] EEPROM.write() finished"));
    #endif
    delay(0);
    
    const bool ok = EepromManager::commit();
    if (!ok) {
        #if DEBUG_LEADERBOARD
        Serial.println(F("[Leaderboard] ERROR: EEPROM commit failed!"));
        #endif
    } else {
        #if DEBUG_LEADERBOARD
        Serial.println(F("[Leaderboard] save() completed successfully"));
        #endif
    }
    #if DEBUG_LEADERBOARD
    Serial.flush();
    #endif
}

static inline void load() {
    if (gLoaded) return;
    gLoaded = true;

    #if DEBUG_LEADERBOARD
    Serial.println(F("[Leaderboard] load() called - reading from EEPROM"));
    Serial.print(F("[Leaderboard] Reading from address "));
    Serial.println(EEPROM_BASE_ADDR);
    #endif
    
    EEPROM.get(EEPROM_BASE_ADDR, gStore);
    const uint8_t calc = checksumXor((const uint8_t*)&gStore, CHECKSUM_LEN);

    #if DEBUG_LEADERBOARD
    Serial.print(F("[Leaderboard] Read: magic=0x"));
    Serial.print(gStore.magic, HEX);
    Serial.print(F(" ver="));
    Serial.print(gStore.version);
    Serial.print(F(" games="));
    Serial.print(gStore.gameCount);
    Serial.print(F(" checksum=0x"));
    Serial.print(gStore.checksum, HEX);
    Serial.print(F(" calc=0x"));
    Serial.println(calc, HEX);
    #endif

    const bool ok = (gStore.magic == MAGIC) &&
                    (gStore.version == VERSION) &&
                    (gStore.checksum == calc) &&
                    (gStore.gameCount <= MAX_GAMES);

    if (!ok) {
        #if DEBUG_LEADERBOARD
        Serial.println(F("[Leaderboard] INVALID - resetting to empty"));
        Serial.print(F("[Leaderboard] dump @"));
        Serial.print(EEPROM_BASE_ADDR);
        Serial.print(F(": "));
        for (int i = 0; i < 32; i++) {
            Serial.print(EepromManager::readByte(EEPROM_BASE_ADDR + i), HEX);
            Serial.print(' ');
        }
        Serial.println();
        #endif
        initEmpty();
        save();
    } else {
        #if DEBUG_LEADERBOARD
        Serial.print(F("[Leaderboard] load() OK - games="));
        Serial.println(gStore.gameCount);
        #endif
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
    // Sort by score descending, keeping initials paired with scores.
    for (int i = 0; i < (int)TOP_SCORES; i++) {
        for (int j = i + 1; j < (int)TOP_SCORES; j++) {
            if (e.scores[j] > e.scores[i]) {
                const uint32_t ts = e.scores[i];
                e.scores[i] = e.scores[j];
                e.scores[j] = ts;

                char ti[4];
                memcpy(ti, e.initials[i], 4);
                memcpy(e.initials[i], e.initials[j], 4);
                memcpy(e.initials[j], ti, 4);
            }
        }
    }
}

static inline bool isRamHeaderSane() {
    if (gStore.magic != MAGIC) return false;
    if (gStore.version != VERSION) return false;
    if (gStore.gameCount > MAX_GAMES) return false;
    return true;
}

static inline bool isRamChecksumSane() {
    const uint8_t calc = checksumXor((const uint8_t*)&gStore, CHECKSUM_LEN);
    return (gStore.checksum == calc);
}

static inline void ensureLoadedAndSane() {
    load();
    // Here we *do* validate checksum because we're trusting EEPROM contents.
    if (!isRamHeaderSane() || !isRamChecksumSane()) {
        Serial.println(F("[Leaderboard] WARNING: EEPROM/RAM mismatch -> reload from EEPROM"));
        gLoaded = false;
        load();
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
    #if DEBUG_LEADERBOARD
    Serial.print(F("[Leaderboard] submitScore() called: gameId="));
    Serial.print(gameId ? gameId : "NULL");
    Serial.print(F(" score="));
    Serial.print(score);
    Serial.print(F(" initials="));
    Serial.println(playerInitials ? playerInitials : "NULL");
    #endif
    
    if (!gameId) {
        #if DEBUG_LEADERBOARD
        Serial.println(F("[Leaderboard] ERROR: gameId is NULL, aborting"));
        #endif
        return;
    }
    
    // Force reload from EEPROM before each submission to ensure we have fresh data
    #if DEBUG_LEADERBOARD
    Serial.println(F("[Leaderboard] Forcing reload from EEPROM before submission"));
    #endif
    gLoaded = false;
    ensureLoadedAndSane();

    // Ignore "empty" submissions by default.
    if (score == 0) {
        #if DEBUG_LEADERBOARD
        Serial.println(F("[Leaderboard] Score is 0, ignoring submission"));
        #endif
        return;
    }

    char normInit[4];
    normalizeInitials(normInit, playerInitials);
    #if DEBUG_LEADERBOARD
    Serial.print(F("[Leaderboard] Normalized initials: "));
    Serial.println(normInit);
    #endif

    const uint32_t idHash = fnv1a32(gameId);
    #if DEBUG_LEADERBOARD
    Serial.print(F("[Leaderboard] Game ID hash: 0x"));
    Serial.println(idHash, HEX);
    #endif
    
    int idx = findEntryIndex(idHash);
    #if DEBUG_LEADERBOARD
    Serial.print(F("[Leaderboard] Found entry index: "));
    Serial.println(idx);
    #endif

    if (idx < 0) {
        #if DEBUG_LEADERBOARD
        Serial.print(F("[Leaderboard] New game entry, current gameCount="));
        Serial.println(gStore.gameCount);
        #endif
        if (gStore.gameCount >= MAX_GAMES) {
            #if DEBUG_LEADERBOARD
            Serial.println(F("[Leaderboard] ERROR: EEPROM full (MAX_GAMES reached), cannot add new game"));
            #endif
            return;
        }
        idx = (int)gStore.gameCount++;
        Entry& e = gStore.entries[idx];
        memset(&e, 0, sizeof(e));
        e.idHash = idHash;
        safeCopyName(e.name, gameName ? gameName : gameId);
        #if DEBUG_LEADERBOARD
        Serial.print(F("[Leaderboard] Created new entry at index "));
        Serial.print(idx);
        Serial.print(F(" with name: "));
        Serial.println(e.name);
        #endif
    }

    Entry& e = gStore.entries[idx];
    #if DEBUG_LEADERBOARD
    Serial.print(F("[Leaderboard] Working with entry: name="));
    Serial.print(e.name);
    Serial.print(F(" current top scores: ["));
    for (int i = 0; i < (int)TOP_SCORES; i++) {
        if (i > 0) Serial.print(F(", "));
        Serial.print(e.scores[i]);
    }
    Serial.println(F("]"));
    #endif
    
    // Update name if we previously had an empty string (first boot / older record).
    if (e.name[0] == '\0' && gameName) {
        safeCopyName(e.name, gameName);
        Serial.print(F("[Leaderboard] Updated empty name to: "));
        Serial.println(e.name);
    }

    // Insert score/initials into the TOP_SCORES array if it fits.
    bool inserted = false;
    int insertPos = -1;
    for (int i = 0; i < (int)TOP_SCORES; i++) {
        if (score > e.scores[i]) {
            insertPos = i;
            Serial.print(F("[Leaderboard] Score "));
            Serial.print(score);
            Serial.print(F(" beats entry at position "));
            Serial.print(i);
            Serial.print(F(" (current: "));
            Serial.print(e.scores[i]);
            Serial.println(F("), inserting..."));
            
            // Shift down and insert.
            for (int j = (int)TOP_SCORES - 1; j > i; j--) {
                e.scores[j] = e.scores[j - 1];
                memcpy(e.initials[j], e.initials[j - 1], 4);
            }
            e.scores[i] = score;
            memcpy(e.initials[i], normInit, 4);
            inserted = true;
            break;
        }
    }
    
    if (!inserted) {
        #if DEBUG_LEADERBOARD
        Serial.print(F("[Leaderboard] Score "));
        Serial.print(score);
        Serial.println(F(" did not beat any existing scores, not inserting"));
        #endif
    } else {
        #if DEBUG_LEADERBOARD
        Serial.print(F("[Leaderboard] Score inserted at position "));
        Serial.println(insertPos);
        #endif
    }
    
    sortAndClamp(e);
    
    #if DEBUG_LEADERBOARD
    Serial.print(F("[Leaderboard] After sort, top scores: ["));
    for (int i = 0; i < (int)TOP_SCORES; i++) {
        if (i > 0) Serial.print(F(", "));
        Serial.print(e.scores[i]);
    }
    Serial.println(F("]"));
    #endif
    
    #if DEBUG_LEADERBOARD
    Serial.println(F("[Leaderboard] Calling save()..."));
    #endif
    save();
    
    // Verify the save by reading back
    #if DEBUG_LEADERBOARD
    Serial.println(F("[Leaderboard] Verifying save by reading back from EEPROM..."));
    #endif
    // IMPORTANT (ESP32): avoid large stack allocations; keep verify buffer static.
    static Storage verify;
    EEPROM.get(EEPROM_BASE_ADDR, verify);
    const uint8_t verifyCalc = checksumXor((const uint8_t*)&verify, CHECKSUM_LEN);
    if (verify.magic == MAGIC && verify.version == VERSION && verify.checksum == verifyCalc) {
        if (idx < (int)verify.gameCount) {
            const Entry& ve = verify.entries[idx];
            bool found = false;
            for (int i = 0; i < (int)TOP_SCORES; i++) {
                if (ve.scores[i] == score && memcmp(ve.initials[i], normInit, 3) == 0) {
                    found = true;
                    #if DEBUG_LEADERBOARD
                    Serial.print(F("[Leaderboard] VERIFICATION OK: Score found at position "));
                    Serial.println(i);
                    #endif
                    break;
                }
            }
            if (!found) {
                #if DEBUG_LEADERBOARD
                Serial.println(F("[Leaderboard] VERIFICATION FAILED: Score not found in EEPROM after save!"));
                #endif
            }
        } else {
            #if DEBUG_LEADERBOARD
            Serial.println(F("[Leaderboard] VERIFICATION FAILED: Entry index out of range"));
            #endif
        }
    } else {
        #if DEBUG_LEADERBOARD
        Serial.println(F("[Leaderboard] VERIFICATION FAILED: EEPROM checksum/magic invalid"));
        #endif
    }
}

// Backward-compatible helper for callers not yet providing initials.
static inline void submitScore(const char* gameId, const char* gameName, uint32_t score) {
    submitScore(gameId, gameName, score, "???");
}

static inline uint8_t gameCount() {
    ensureLoadedAndSane();
    return gStore.gameCount;
}

static inline const Entry* entryAt(uint8_t index) {
    ensureLoadedAndSane();
    if (index >= gStore.gameCount) return nullptr;
    return &gStore.entries[index];
}

/**
 * Get the leaderboard entry for a specific game id (or nullptr if not present).
 */
static inline const Entry* entryForGameId(const char* gameId) {
    if (!gameId) return nullptr;
    ensureLoadedAndSane();
    const uint32_t idHash = fnv1a32(gameId);
    const int idx = findEntryIndex(idHash);
    if (idx < 0) return nullptr;
    return &gStore.entries[idx];
}

/**
 * Find rank (0..TOP_SCORES-1) of an exact score/initials pair inside a game's entry.
 * Returns -1 if not found (meaning it did not make it into the leaderboard).
 */
static inline int rankFor(const char* gameId, uint32_t score, const char* initials) {
    if (!gameId || score == 0) return -1;
    const Entry* e = entryForGameId(gameId);
    if (!e) return -1;

    char normInit[4];
    normalizeInitials(normInit, initials);

    // Prefer exact (score + initials) match.
    for (int i = 0; i < (int)TOP_SCORES; i++) {
        if (e->scores[i] == score && memcmp(e->initials[i], normInit, 4) == 0) return i;
    }
    // Fallback: match score only.
    for (int i = 0; i < (int)TOP_SCORES; i++) {
        if (e->scores[i] == score) return i;
    }
    return -1;
}

static inline void clearAll() {
    initEmpty();
    save();
}

} // namespace Leaderboard


