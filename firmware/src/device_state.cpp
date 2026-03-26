#include "device_state.h"

#include <Arduino.h>

static int selectedIndex = 0;
static DeviceUiState uiState = DeviceUiState::LOCKED;

void deviceStateInit() {
    selectedIndex = 0;
    uiState = DeviceUiState::LOCKED;
}

int deviceGetSelectedIndex() {
    return selectedIndex;
}

void deviceSetSelectedIndex(int index) {
    if (index < 0) index = 0;
    selectedIndex = index;
}

void deviceClampSelectedIndex(int count) {
    if (count <= 0) {
        selectedIndex = 0;
        uiState = DeviceUiState::IDLE;
        return;
    }
    if (selectedIndex < 0) selectedIndex = 0;
    if (selectedIndex >= count) selectedIndex = count - 1;
}

DeviceUiState deviceGetUiState() {
    return uiState;
}

void deviceSetUiState(DeviceUiState state) {
    uiState = state;
}
