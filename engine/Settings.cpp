#include "Settings.h"

Settings globalSettings;

// -----------------------------------------------------
// Settings static data definitions
// -----------------------------------------------------
const uint16_t Settings::PLAYER_COLORS[Settings::PLAYER_COLOR_COUNT] = {
    COLOR_GREEN,
    COLOR_CYAN,
    COLOR_ORANGE,
    COLOR_PURPLE,
    COLOR_YELLOW,
    COLOR_MAGENTA,
    COLOR_RED,
    COLOR_WHITE
};

const char* const Settings::PLAYER_COLOR_NAMES[Settings::PLAYER_COLOR_COUNT] = {
    "GRN",
    "CYN",
    "ORG",
    "PUR",
    "YEL",
    "MAG",
    "RED",
    "WHT"
};

