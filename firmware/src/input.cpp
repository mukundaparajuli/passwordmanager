#include "input.h"

#include "config.h"

void inputInit() {
    pinMode(BTN_UP_OK, INPUT_PULLUP);
    pinMode(BTN_DOWN_BACK, INPUT_PULLUP);
}

void readButtons(bool &upPressed, bool &downPressed) {
    static unsigned long lastDebounceTime = 0;
    const unsigned long debounceDelay = 50;

    upPressed = false;
    downPressed = false;

    if (millis() - lastDebounceTime > debounceDelay) {
        if (digitalRead(BTN_UP_OK) == LOW)
            upPressed = true;
        if (digitalRead(BTN_DOWN_BACK) == LOW)
            downPressed = true;
        lastDebounceTime = millis();
    }
}
