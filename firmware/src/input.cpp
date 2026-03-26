#include "input.h"

#include "config.h"

namespace {

constexpr uint32_t DEBOUNCE_MS = 30;
constexpr uint32_t CONFIRM_LONG_MS = 1500;

struct ButtonState {
    bool stablePressed = false;
    bool lastRawPressed = false;
    uint32_t lastChangeMs = 0;
};

ButtonState upBtn;
ButtonState downBtn;
ButtonState confirmBtn;

uint32_t confirmPressStartMs = 0;
bool confirmLongFired = false;

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
    pinMode(BTN_UP_PIN, INPUT_PULLUP);
    pinMode(BTN_DOWN_PIN, INPUT_PULLUP);
    pinMode(BTN_CONFIRM_PIN, INPUT_PULLUP);

    upBtn = {};
    downBtn = {};
    confirmBtn = {};
    confirmPressStartMs = 0;
    confirmLongFired = false;
}

void inputPoll(InputEvents &events) {
    events = {};

    bool upPressedEdge = false;
    bool upReleasedEdge = false;
    pollButton(BTN_UP_PIN, upBtn, upPressedEdge, upReleasedEdge);
    if (upPressedEdge) events.up_pressed = true;

    bool downPressedEdge = false;
    bool downReleasedEdge = false;
    pollButton(BTN_DOWN_PIN, downBtn, downPressedEdge, downReleasedEdge);
    if (downPressedEdge) events.down_pressed = true;

    bool confirmPressedEdge = false;
    bool confirmReleasedEdge = false;
    const bool confirmHeld = pollButton(BTN_CONFIRM_PIN, confirmBtn, confirmPressedEdge, confirmReleasedEdge);

    const uint32_t now = millis();
    if (confirmPressedEdge) {
        confirmPressStartMs = now;
        confirmLongFired = false;
    }

    if (confirmHeld && !confirmLongFired && confirmPressStartMs != 0 &&
        (uint32_t)(now - confirmPressStartMs) >= CONFIRM_LONG_MS) {
        confirmLongFired = true;
        events.confirm_long = true;
    }

    if (confirmReleasedEdge) {
        if (!confirmLongFired && confirmPressStartMs != 0) {
            events.confirm_short = true;
        }
        confirmPressStartMs = 0;
        confirmLongFired = false;
    }
}

