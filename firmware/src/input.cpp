#include "input.h"

#include "config.h"

namespace {

constexpr uint32_t DEBOUNCE_MS = 30;

struct ButtonState {
    bool stablePressed = false;
    bool lastRawPressed = false;
    uint32_t lastChangeMs = 0;
};

ButtonState nextBtn;
ButtonState confirmBtn;

bool rawPressed(uint8_t pin) {
    return digitalRead(pin) == LOW;
}

bool pollButton(uint8_t pin, ButtonState &state, bool &outPressedEdge, bool &outReleasedEdge) {
    outPressedEdge = false;
    outReleasedEdge = false;

    const uint32_t now = millis();
    const bool raw = rawPressed(pin);

    if (raw != state.lastRawPressed) {
        state.lastRawPressed = raw;
        state.lastChangeMs = now;
    }

    if ((uint32_t)(now - state.lastChangeMs) < DEBOUNCE_MS) return state.stablePressed;

    if (raw != state.stablePressed) {
        state.stablePressed = raw;
        if (state.stablePressed) outPressedEdge = true;
        else outReleasedEdge = true;
    }

    return state.stablePressed;
}

} // namespace

void inputInit() {
    pinMode(BTN_NEXT_PIN, INPUT_PULLUP);
    pinMode(BTN_CONFIRM_PIN, INPUT_PULLUP);

    nextBtn = {};
    confirmBtn = {};
}

void inputPoll(InputEvents &events) {
    events = {};

    bool nextPressedEdge = false;
    bool nextReleasedEdge = false;
    pollButton(BTN_NEXT_PIN, nextBtn, nextPressedEdge, nextReleasedEdge);
    if (nextPressedEdge) events.next_pressed = true;

    bool confirmPressedEdge = false;
    bool confirmReleasedEdge = false;
    pollButton(BTN_CONFIRM_PIN, confirmBtn, confirmPressedEdge, confirmReleasedEdge);
    if (confirmPressedEdge) events.confirm_pressed = true;
}

