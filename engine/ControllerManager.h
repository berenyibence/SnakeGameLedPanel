#pragma once
#include <Arduino.h>
#include <Bluepad32.h>
#include "config.h"

class ControllerManager {
public:
    ControllerManager();

    void setup();
    void update();

    ControllerPtr getController(int index);
    int getConnectedCount() const;

    static void onConnectedController(ControllerPtr ctl);
    static void onDisconnectedController(ControllerPtr ctl);

private:
    ControllerPtr controllers[MAX_GAMEPADS];
    int connectedCount;
};

extern ControllerManager* globalControllerManager;
