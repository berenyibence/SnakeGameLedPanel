// MusicApp.h
// -----------------------------------------------------------------------------
// "Music" player applet:
// - Shows a list of songs (RTTTL ringtones for now)
// - Press A to play/stop the selected song
// - Press B to stop playback (stays in the app)
//
// Exit behavior is host-driven (START -> pause menu -> quit).
// -----------------------------------------------------------------------------
#pragma once

#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "../../engine/GameBase.h"
#include "../../engine/ControllerManager.h"
#include "../../engine/AudioManager.h"
#include "../../engine/config.h"
#include "../../engine/Settings.h"
#include "../../component/SmallFont.h"
#include "../../component/ScrollableList.h"

#include "MusicAppConfig.h"

class MusicApp : public GameBase, public ListModel {
public:
    MusicApp() = default;

    // -----------------------------------------------------
    // GameBase
    // -----------------------------------------------------
    void start() override {
        playingIndex = -1;
        lastB = false;
        ignoreSelectUntilMs = (uint32_t)millis() + 300; // prevent "carry-over A" from menu selecting first song
        list.selectedActual = 0;
        globalAudio.stopRtttl();
    }

    void reset() override { start(); }

    void update(ControllerManager* input) override {
        // If a song finished naturally, clear our UI state.
        if (playingIndex >= 0 && !globalAudio.isRtttlActive()) {
            playingIndex = -1;
        }

        // List navigation + A handling (returns selected index on A).
        const int beforeSel = list.selectedActual;
        const int sel = list.update(input, *this);

        // If you navigate away from the currently playing song, stop it immediately.
        if (list.selectedActual != beforeSel && playingIndex >= 0) {
            stopPlayback();
        }

        // Prevent "autoplay" when entering the app while A is still held from the main menu.
        const uint32_t now = (uint32_t)millis();
        if (sel != -1 && (int32_t)(now - ignoreSelectUntilMs) >= 0) {
            togglePlay(sel);
        }

        // B stops playback (edge).
        ControllerPtr ctl = input ? input->getController(0) : nullptr;
        if (!ctl) return;
        const bool bNow = (bool)ctl->b();
        if (bNow && !lastB) {
            stopPlayback();
        }
        lastB = bNow;
    }

    void draw(MatrixPanel_I2S_DMA* display) override {
        if (!display) return;
        display->fillScreen(COLOR_BLACK);

        // HUD
        SmallFont::drawString(display, 2, 6, "MUSIC", COLOR_CYAN);
        for (int x = 0; x < PANEL_RES_X; x += 2) display->drawPixel(x, HUD_H - 1, COLOR_BLUE);

        // Right side HUD: volume + playing marker.
        SmallFont::drawStringF(display, 38, 6, COLOR_YELLOW, "V%02d", (int)globalSettings.getSoundVolumeLevel());
        if (playingIndex >= 0) {
            SmallFont::drawString(display, 56, 6, "PL", COLOR_GREEN);
        } else {
            SmallFont::drawString(display, 56, 6, "--", COLOR_WHITE);
        }

        // List
        ScrollableList::Layout lay;
        lay.hudH = HUD_H;
        lay.visibleRows = 7;
        list.draw(display, *this, lay);
    }

    bool isGameOver() override { return false; }

    uint16_t preferredRenderFps() const override { return 30; }

    // -----------------------------------------------------
    // ListModel
    // -----------------------------------------------------
    int itemCount() const override { return MusicAppConfig::SONG_COUNT; }
    const char* label(int actualIndex) const override { return MusicAppConfig::SONGS[actualIndex].name; }

private:
    static constexpr int HUD_H = 8;

    ScrollableList list;
    int playingIndex = -1;
    bool lastB = false;
    uint32_t ignoreSelectUntilMs = 0;

    void stopPlayback() {
        globalAudio.stopRtttl();
        playingIndex = -1;
    }

    void togglePlay(int index) {
        if (index < 0 || index >= MusicAppConfig::SONG_COUNT) return;

        if (playingIndex == index && globalAudio.isRtttlActive()) {
            stopPlayback();
            return;
        }

        const char* r = MusicAppConfig::SONGS[index].rtttl;
        globalAudio.playRtttl(r, /*loop=*/false);
        playingIndex = index;
    }
};


