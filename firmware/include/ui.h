#ifndef UI_H
#define UI_H

#include "device_state.h"

bool uiInit();
void uiShowBootScreen();
void uiRender(DeviceUiState state, int selectedId);

#endif

