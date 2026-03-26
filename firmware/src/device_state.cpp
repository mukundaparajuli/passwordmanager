#include "device_state.h"

#include <Arduino.h>

static int selectedIndex = 0;
static bool showingDetail = false;

void deviceStateInit() {
    selectedIndex = 0;
    showingDetail = false;
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
        showingDetail = false;
        return;
    }
    if (selectedIndex < 0) selectedIndex = 0;
    if (selectedIndex >= count) selectedIndex = count - 1;
}

bool deviceIsShowingDetail() {
    return showingDetail;
}

void deviceSetShowingDetail(bool showing) {
    showingDetail = showing;
}

