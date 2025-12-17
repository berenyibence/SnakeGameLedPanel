#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "ControllerManager.h"
#include "SmallFont.h"
#include "ScrollableList.h"
#include "UserProfiles.h"

/**
 * UserSelectMenu
 * --------------
 * Simple applet to select or create a 3-letter uppercase user tag for a given
 * controller index.
 *
 * Flow:
 * - If users exist: show list of users + "NEW" item.
 * - If "NEW" or no users: show 3-letter editor (D-pad left/right move cursor,
 *   up/down change letter A..Z). A confirms, B cancels back to list.
 */
class UserSelectMenu {
public:
    static constexpr int HUD_H = 8;

    void beginForPad(uint8_t padIndexIn) {
        targetPad = padIndexIn;
        // If no users exist, skip list and go straight to editor.
        if (UserProfiles::userCount() == 0) {
            mode = MODE_EDITOR;
            // Prevent the "connect / open" button press from immediately confirming AAA.
            ignoreConfirmUntilMs = (uint32_t)millis() + 350;
        } else {
            mode = MODE_LIST;
        }
        list.selectedActual = 0;
        cursor = 0;
        // Default new tag "AAA".
        editingTag[0] = 'A';
        editingTag[1] = 'A';
        editingTag[2] = 'A';
        editingTag[3] = '\0';

        // Reset editor debounce timers (per instance).
        lastMoveMs = 0;
        lastChangeMs = 0;
        lastAms = 0;
        lastBms = 0;
    }

    void draw(MatrixPanel_I2S_DMA* display, ControllerManager* input) {
        (void)input;
        display->fillScreen(0);

        // HUD
        SmallFont::drawString(display, 2, 6, "USER", COLOR_CYAN);
        for (int x = 0; x < PANEL_RES_X; x += 2) display->drawPixel(x, HUD_H - 1, COLOR_BLUE);

        if (mode == MODE_LIST) drawList(display);
        else drawEditor(display);
    }

    /**
     * Returns true when a user has been selected/created and bound to targetPad.
     */
    bool update(ControllerManager* input) {
        ControllerPtr ctl = input ? input->getController(targetPad) : nullptr;
        if (!ctl) return false;

        if (mode == MODE_LIST) {
            return updateList(input);
        } else {
            return updateEditor(ctl);
        }
    }

private:
    enum Mode : uint8_t { MODE_LIST, MODE_EDITOR };
    Mode mode = MODE_LIST;

    uint8_t targetPad = 0;

    // List mode
    ScrollableList list;

    class UsersModel : public ListModel {
    public:
        int itemCount() const override {
            const int base = (int)UserProfiles::userCount();
            // +1 for "NEW" option.
            return base + 1;
        }

        const char* label(int actualIndex) const override {
            const uint8_t count = UserProfiles::userCount();
            if (actualIndex < (int)count) {
                const char* t = UserProfiles::userTag((uint8_t)actualIndex);
                return t ? t : "---";
            }
            return "NEW";
        }
    } model;

    // Editor mode
    char editingTag[4] = { 'A', 'A', 'A', '\0' };
    uint8_t cursor = 0; // 0..2
    uint32_t ignoreConfirmUntilMs = 0;

    // Editor debounces (per instance; do NOT make static)
    uint32_t lastMoveMs = 0;
    uint32_t lastChangeMs = 0;
    uint32_t lastAms = 0;
    uint32_t lastBms = 0;

    void drawList(MatrixPanel_I2S_DMA* display) {
        const int total = model.itemCount();
        list.selectedActual = constrain(list.selectedActual, 0, max(0, total - 1));

        ScrollableList::Layout lay;
        lay.hudH = HUD_H;
        lay.visibleRows = min(7, total);
        lay.labelX = 10;
        list.draw(display, model, lay);
    }

    bool updateList(ControllerManager* input) {
        const int sel = list.update(input, model);
        if (sel != -1) {
            const uint8_t userCount = UserProfiles::userCount();
            if (sel < (int)userCount) {
                // Existing user selected -> bind and finish.
                UserProfiles::setPadUserIndex(targetPad, (int8_t)sel);
                return true;
            }
            // "NEW" selected -> go to editor.
            mode = MODE_EDITOR;
            // Debounce: ignore the same A press that selected "NEW".
            ignoreConfirmUntilMs = (uint32_t)millis() + 350;
        }
        return false;
    }

    void drawEditor(MatrixPanel_I2S_DMA* display) {
        // Show hint
        SmallFont::drawString(display, 2, HUD_H + 10, "SET TAG", COLOR_WHITE);
        SmallFont::drawString(display, 2, HUD_H + 20, "LR:MOVE", COLOR_WHITE);
        SmallFont::drawString(display, 2, HUD_H + 30, "UD:CHAR", COLOR_WHITE);
        SmallFont::drawString(display, 2, HUD_H + 40, "A:OK B:CANCEL", COLOR_WHITE);

        // Draw the three characters, highlighting the active cursor.
        const int baseX = 20;
        const int y = HUD_H + 50;
        for (int i = 0; i < 3; i++) {
            const bool active = (i == (int)cursor);
            const uint16_t ccol = active ? COLOR_YELLOW : COLOR_WHITE;
            const uint16_t bgcol = active ? COLOR_BLUE : COLOR_BLACK;
            
            // Draw background box for active character
            if (active) {
                display->fillRect(baseX + i * 10 - 1, y - 1, 7, 9, bgcol);
            }
            
            char buf[2] = { editingTag[i], '\0' };
            SmallFont::drawString(display, baseX + i * 10, y, buf, ccol);
            
            // Underline for active character
            if (active) {
                for (int px = 0; px < 5; px++) {
                    display->drawPixel(baseX + i * 10 + px, y + 8, ccol);
                }
            }
        }
    }

    bool updateEditor(ControllerPtr ctl) {
        const uint32_t now = (uint32_t)millis();

        const uint8_t d = ctl->dpad();
        const bool up = (d & 0x01) != 0;
        const bool down = (d & 0x02) != 0;
        const bool right = (d & 0x04) != 0;
        const bool left = (d & 0x08) != 0;

        // Left/right: move cursor (debounced).
        if ((right || left) && (now - lastMoveMs > 160)) {
            lastMoveMs = now;
            if (right && cursor < 2) cursor++;
            else if (left && cursor > 0) cursor--;
        }

        // Up/down: change letter (debounced).
        if ((up || down) && (now - lastChangeMs > 120)) {
            lastChangeMs = now;
            char& c = editingTag[cursor];
            if (c < 'A' || c > 'Z') c = 'A';
            if (up) {
                c = (c == 'A') ? 'Z' : (char)(c - 1);
            } else if (down) {
                c = (c == 'Z') ? 'A' : (char)(c + 1);
            }
        }

        // A: confirm and create + bind user.
        if (now >= ignoreConfirmUntilMs && ctl->a() && (now - lastAms > 200)) {
            lastAms = now;
            // Ensure uppercase + NUL.
            for (int i = 0; i < 3; i++) {
                char& c = editingTag[i];
                if (c < 'A' || c > 'Z') c = 'A';
            }
            editingTag[3] = '\0';
            const uint8_t idx = UserProfiles::createUser(editingTag);
            UserProfiles::setPadUserIndex(targetPad, (int8_t)idx);
            return true;
        }

        // B: cancel back to list (if there are existing users).
        if (ctl->b() && (now - lastBms > 200)) {
            lastBms = now;
            if (UserProfiles::userCount() > 0) {
                mode = MODE_LIST;
            }
        }

        return false;
    }
};


