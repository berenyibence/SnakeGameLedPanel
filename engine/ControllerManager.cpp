#include "ControllerManager.h"

ControllerManager* globalControllerManager = nullptr;

ControllerManager::ControllerManager() {
    connectedCount = 0;
    for (int i = 0; i < MAX_GAMEPADS; i++) {
        controllers[i] = nullptr;
    }
    globalControllerManager = this;
}

void ControllerManager::setup() {
    BP32.setup(&ControllerManager::onConnectedController,
               &ControllerManager::onDisconnectedController);
    BP32.enableVirtualDevice(false);
}

void ControllerManager::update() {
    BP32.update();
}

ControllerPtr ControllerManager::getController(int index) {
    if (index < 0 || index >= MAX_GAMEPADS) return nullptr;
    return controllers[index];
}

int ControllerManager::getConnectedCount() const {
    return connectedCount;
}

void ControllerManager::onConnectedController(ControllerPtr ctl) {
    if (!globalControllerManager) return;

    for (int i = 0; i < MAX_GAMEPADS; i++) {
        if (globalControllerManager->controllers[i] == nullptr) {
            globalControllerManager->controllers[i] = ctl;
            globalControllerManager->connectedCount++;
            break;
        }
    }
}

void ControllerManager::onDisconnectedController(ControllerPtr ctl) {
    if (!globalControllerManager) return;

    for (int i = 0; i < MAX_GAMEPADS; i++) {
        if (globalControllerManager->controllers[i] == ctl) {
            globalControllerManager->controllers[i] = nullptr;
            globalControllerManager->connectedCount--;
            break;
        }
    }
}
