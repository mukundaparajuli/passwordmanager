#ifndef DEVICE_STATE_H
#define DEVICE_STATE_H

#include <stdint.h>

enum class DeviceUiState : uint8_t {
    LOCKED = 0,
    IDLE,
    SELECTED,
    TOTP,
    CONFIRM_HID,
};

void deviceStateInit();

int deviceGetSelectedIndex();
void deviceSetSelectedIndex(int index);
void deviceClampSelectedIndex(int count);

DeviceUiState deviceGetUiState();
void deviceSetUiState(DeviceUiState state);

#endif
