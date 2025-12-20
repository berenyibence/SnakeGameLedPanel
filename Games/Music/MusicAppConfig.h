// MusicAppConfig.h
// -----------------------------------------------------------------------------
// Music player "song list" configuration.
//
// Songs are stored as RTTTL strings (Nokia Composer-style ringtones).
// RTTTL is tiny, monophonic, and perfect for a buzzer.
//
// UI note:
// - Names are auto-trimmed by `ScrollableList` so they never overflow the 64px screen.
// -----------------------------------------------------------------------------
#pragma once

#include <Arduino.h>

namespace MusicAppConfig {

struct Song {
    const char* name;
    const char* rtttl; // "name:d=...,o=...,b=...:..."
};

// Your provided RTTTL set (placeholders removed).
static constexpr const Song SONGS[] = {
    { "007", "007:o=5,d=4,b=320,b=320:c,8d,8d,d,2d,c,c,c,c,8d#,8d#,2d#,d,d,d,c,8d,8d,d,2d,c,c,c,c,8d#,8d#,d#,2d#,d,c#,c,c6,1b.,g,f,1g." },
    { "7Days", "7Days:o=6,d=16,b=140,b=140:g#,d#,8a#5,g#,d#,8a#5,g#,d#,8a#5,g#,d#,8a#5,f,1p5,g#,d#,8a#5,g#,d#,8a#5,g#,d#,8a#5,g#,d#,8a#5,f,1p5,g#,d#,8c,g#,d#,8c,g#,d#,8c,g#,d#,8c,f,1p5,g#,d#,8c,g#,d#,8c,g#,d#,8c,g#,d#,8c,f,1p5,d#,c,8g#5,d#,c,8g#5,d#,c,8g#5,d#,c,8g#5,f,1p5,d#,c,8g#5,d#,c,8g#5,d#,c,8g#5,d#,c,8g#5,f,1p5,d#,c#,8a#5,d#,c#,8a#5,d#,c#,8a#5,d#,c#,8a#5,f,1p5,d#,c#,8a#5,d#,c#,8a#5,d#,c#,8a#5,d#,c#,8a#5,f." },
    { "Adams Family", "Adams Family:o=5,d=8,b=160,b=160:c,4f,a,4f,c,4b4,2g,f,4e,g,4e,g4,4c,2f,c,4f,a,4f,c,4b4,2g,f,4e,c,4d,e,1f,c,d,e,f,1p,d,e,f#,g,1p,d,e,f#,g,4p,d,e,f#,g,4p,c,d,e,f" },
    { "Agadoo", "Agadoo:o=5,d=8,b=125,b=125:b,g#,4e,e,e,4e,e,e,e,e,d#,e,4f#,a,f#,4d#,d#,d#,4d#,d#,d#,d#,d#,c#,d#,4e" },
    { "Alice Cooper-Poison", "Alice Cooper-Poison:o=5,d=8,b=112,b=112:d,d,a,d,e6,d,d6,d,f#,g,c6,f#,g,c6,e,d,d,d,a,d,e6,d,d6,d,f#,g,c6,f#,g,c6,e,d,c,d,a,d,e6,d,d6,d,f#,g,c6,f#,g,c6,e,d,c,d,a,d,e6,d,d6,d,a,d,e6,d,d6" },
    { "Alvin + Chipmonks", "Alvin and the Chipmonks:o=5,d=4,b=285,b=285:g,p,c,c,a,c,c,c,p,b,b,c6,c6,p,c,c,p,a,g,d,2g,d,2p,a,g,d,2g,a,p,g,p,c,c,a,c,c,c,p,b,b,c6,c6,p,c,c,p,a,g,d,2g,d,2p,a,a,g,2a,g,p,2a,b,2a,g,p,c,c,c,c,c,e,e,e,e,2a,g,2a,2g,2a,a,2g,2g.,p,2a,g,2a,g,p,c,c,c,c,c,e,e,e,e,2a,g,2a,2g,2c6,b,2b,1b,2c6" },
    { "Amazing Grace", "Amazing Grace:o=5,d=16,b=80,b=80:8c,2f,a,g,f,2a,8a,8g,2f,4d,2c,8c,2f,a,g,f,2a,8g,8a,2c6." },
    { "Axel", "Axel:o=5,d=8,b=125,b=125:16g,16g,a#.,16g,16p,16g,c6,g,f,4g,d6.,16g,16p,16g,d#6,d6,a#,g,d6,g6,16g,16f,16p,16f,d,a#,2g,4p,16f6,d6,c6,a#,4g,a#.,16g,16p,16g,c6,g,f,4g,d6.,16g,16p,16g,d#6,d6,a#,g,d6,g6,16g,16f,16p,16f,d,a#,2g" },
    { "Ba Ba Black Sheep", "Ba Ba Black Sheep:o=5,d=8,b=150,b=150:c,4p,c,4p,g,4p,g,4p,4a,4b,4c6,4a,4g,4p,f,4p,f,4p,e,4p,e,4p,d,4p,d,4p,4c" },
    { "Back to Future", "Back to the Future:o=5,d=16,b=200,b=200:4g.,p,4c.,p,2f#.,p,g.,p,a.,p,8g,p,8e,p,8c,p,4f#,p,g.,p,a.,p,8g.,p,8d.,p,8g.,p,8d6.,p,4d6.,p,4c#6,p,b.,p,c#6.,p,2d6." },
    { "Barbie Girl", "Barbie Girl:o=5,d=8,b=125,b=125:g#,e,g#,c#6,4a,4p,f#,d#,f#,b,4g#,f#,e,4p,e,c#,4f#,4c#,4p,f#,e,4g#,4f#" },
    { "Batman", "Batman:o=5,d=8,b=180,b=180:d,d,c#,c#,c,c,c#,c#,d,d,c#,c#,c,c,c#,c#,d,d#,c,c#,c,c,c#,c#,f,p,4f" },
    { "Benny Hill", "Benny Hill:o=5,d=16,b=125,b=125:8d.,e,8g,8g,e,d,a4,b4,d,b4,8e,d,b4,a4,b4,8a4,a4,a#4,b4,d,e,d,4g,4p,d,e,d,8g,8g,e,d,a4,b4,d,b4,8e,d,b4,a4,b4,8d,d,d,f#,a,8f,4d,4p,d,e,d,8g,g,g,8g,g,g,8g,8g,e,8e.,8c,8c,8c,8c,e,g,a,g,a#,8g,a,b,a#,b,a,b,8d6,a,b,d6,8b,8g,8d,e6,b,b,d,8a,8g,4g" },
    { "Bethoven", "Bethoven:o=5,d=4,b=160,b=160:c,e,c,g,c,c6,8b,8a,8g,8a,8g,8f,8e,8f,8e,8d,c,e,g,e,c6,g." },
    { "Birdy Song", "Birdy Song:o=5,d=16,b=100,b=100:g,g,a,a,e,e,8g,g,g,a,a,e,e,8g,g,g,a,a,c6,c6,8b,8b,8a,8g,8f,f,f,g,g,d,d,8f,f,f,g,g,d,d,8f,f,f,g,g,a,b,8c6,8a,8g,8e,4c" },
    { "Black Bear", "Black Bear:o=5,d=4,b=180,b=180:d#,d#,8g.,16d#,8a#.,16g,d#,d#,8g.,16d#,8a#.,16g,f,8c.,16b4,c,8f.,16d#,8d.,16d#,8c.,16d,8a#4.,16c,8d.,16a#4,d#,d#,8g.,16d#,8a#.,16g,d#,d#,8g.,16d#,8a#.,16g,f,f,f,8g.,16f,d#,g,2d#" },
    { "Bolero", "Bolero:o=5,d=16,b=80,b=80:c6,8c6,b,c6,d6,c6,b,a,8c6,c6,a,4c6,8c6,b,c6,a,g,e,f,2g,g,f,e,d,e,f,g,a,4g,4g,g,a,b,a,g,f,e,d,e,d,8c,8c,c,d,8e,8f,4d,2g" },
    { "Cantina", "Cantina:o=5,d=8,b=250,b=250:a,p,d6,p,a,p,d6,p,a,d6,p,a,p,g#,4a,a,g#,a,4g,f#,g,f#,4f.,d.,16p,4p.,a,p,d6,p,a,p,d6,p,a,d6,p,a,p,g#,a,p,g,p,4g.,f#,g,p,c6,4a#,4a,4g" },
    { "Camberwick Green", "Camberwick Green:o=5,d=16,b=63,b=63:8d,d,8a,a,g,a,g,8d.,8d,g,8a,c6,b,a,g,a,b,c6,2d6,p,c6,b,a,4g,f#,e,4d.,8e6.,4b.,8a.,8d,d,8a,a,g,a,g,8d.,8d,g,8a,c6,b,a,g,a,b,c6,2d6,p,c6,b,a,4g,f#,e,4d.,8e6.,4b.,8a.,8d,g,8e,a,8f#,b,8e,a,8d,g,8e,a,8f#,b,8e,a,8d,g,2e" },
    { "Canon", "Canon:o=5,d=8,b=80,b=80:d,f#,a,d6,c#,e,a,c#6,d,f#,b,d6,a,c#,f#,a,b,d,g,b,a,d,f#,a,b,f#,g,b,c#,e,a,c#6,4f#6,f#,a,4e6,e,a,4d6,f#,a,4c#6,c#,e,4b,d,g,4a,f#,d,4b,d,g,4c#6." },
    { "Mission Impossible", "Mission Impossible:o=5,d=16,b=100,b=100:32d,32d#,32d,32d#,32d,32d#,32d,32d#,32d,32d,32d#,32e,32f,32f#,32g,g,8p,g,8p,a#,p,c6,p,g,8p,g,8p,f,p,f#,p,g,8p,g,8p,a#,p,c6,p,g,8p,g,8p,f,p,f#,p,g,8p,g,8p,a#,p,c6,p,g,8p,g,8p,f,p,f#,p,a#,g,2d,32p,a#,g,2c#,32p,a#,g,2c,p,a#4,c" },
    { "Simpsons", "Simpsons:o=5,d=8,b=160,b=160:c6.,4e6,4f#6,a6,4g6.,4e6,4c6,a,f#,f#,f#,2g,p,p,f#,f#,f#,g,4a#.,c6,c6,c6,4c6" },
    { "Star Trek", "Star Trek:o=5,d=16,b=63,b=63:8f.,a#,4d#6.,8d6,a#.,g.,c6.,4f6" },
    { "Star Wars", "Star Wars:o=6,d=8,b=180,b=180:f5,f5,f5,2a#5.,2f.,d#,d,c,2a#.,4f.,d#,d,c,2a#.,4f.,d#,d,d#,2c,4p,f5,f5,f5,2a#5.,2f.,d#,d,c,2a#.,4f.,d#,d,c,2a#.,4f.,d#,d,d#,2c" },
    { "YMCA", "YMCA:o=5,d=8,b=160,b=160:c#6,a#,2p,a#,g#,f#,g#,a#,4c#6,a#,4c#6,d#6,a#,2p,a#,g#,f#,g#,a#,4c#6,a#,4c#6,d#6,b,2p,b,a#,g#,a#,b,4d#6,f#6,4d#6,4f6.,4d#6.,4c#6.,4b.,4a#,4g#" },
    { "Walk of Life", "Walk of Life:o=5,d=8,b=160,b=160:4g.,32p,4g,4p.,d,e,4g,e,4d,4c.,4c,2p,d,e,4g.,4g,4p.,d,e,4g,e,4d,4c.,4c" },
    { "Wannabe", "Wannabe:o=5,d=8,b=125,b=125:16g,16g,16g,16g,g,a,g,e,p,16c,16d,16c,d,d,c,4e,4p,g,g,g,a,g,e,p,4c6,c6,b,g,a,16b,16a,4g" },
    { "Take On Me", "Take On Me:o=5,d=8,b=160,b=160:f#,f#,f#,d,p,b4,p,e,p,e,p,e,g#,g#,a,b,a,a,a,e,p,d,p,f#,p,f#,p,f#,e,e,f#,e,f#,f#,f#,d,p,b4,p,e,p,e,p,e,g#,g#,a,b,a,a,a,e,p,d,p,f#,p,f#,p,f#,e,e" },
    { "Super Man", "Super Man:o=6,d=8,b=180,b=180:g5,g5,g5,4c,c,2g,p,g,a.,16g,f,1g,p,g5,g5,g5,4c,c,2g,p,g,a.,16g,f,a,2g.,4p,c,c,c,2b.,4g.,c,c,c,2b.,4g.,c,c,c,b,a,b,2c7,c,c,c,c,c,2c." },
    { "Mozart", "Mozart:o=5,d=16,b=125,b=125:16d#,c#,c,c#,8e,8p,f#,e,d#,e,8g#,8p,a,g#,g,g#,d#6,c#6,c6,c#6,d#6,c#6,c6,c#6,4e6,8c#6,8e6,32b,32c#6,d#6,8c#6,8b,8c#6,32b,32c#6,d#6,8c#6,8b,8c#6,32b,32c#6,d#6,8c#6,8b,8a#,4g#,d#,32c#,c,c#,8e,8p,f#,e,d#,e,8g#,8p,a,g#,g,g#,d#6,c#6,c6,c#6,d#6,c#6,c6,c#6,4e6,8c#6,8e6,32b,32c#6,d#6,8c#6,8b,8c#6,32b,32c#6,d#6,8c#6,8b,8c#6,32b,32c#6,d#6,8c#6,8b,8a#,4g#" },
};

static constexpr int SONG_COUNT = (int)(sizeof(SONGS) / sizeof(SONGS[0]));

} // namespace MusicAppConfig


