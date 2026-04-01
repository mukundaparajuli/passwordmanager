#include <Arduino.h>
#include <string.h>

#if defined(__INTELLISENSE__) || defined(__clang__)
#ifndef CONFIG_TINYUSB_ENABLED
#define CONFIG_TINYUSB_ENABLED 1
#endif
#ifndef CONFIG_TINYUSB_HID_ENABLED
#define CONFIG_TINYUSB_HID_ENABLED 1
#endif
#endif

#include <USB.h>
#include <USBHIDKeyboard.h>
USBHIDKeyboard Keyboard;

#include "auth.h"
#include "device_state.h"
#include "input.h"
#include "serial_protocol.h"
#include "storage.h"
#include "ui.h"

static uint8_t getHidModeFromFlags(uint8_t flags) {
    return (uint8_t)((flags & CRED_FLAG_HID_MODE_MASK) >> CRED_FLAG_HID_MODE_SHIFT);
}

static void typeSelectedViaHid(int id) {
    // Serial.println("[HID] Starting HID type...");
    const uint8_t *key = getEncryptionKey();
    if (!key) {
        // Serial.println("[HID] ERROR: No encryption key");
        return;
    }

    credential_entry_t cred;
    if (!getCredential(id, cred, key)) {
        // Serial.println("[HID] ERROR: Failed to get credential");
        return;
    }

    // Serial.print("[HID] Got credential, mode: ");
    // Serial.println(getHidModeFromFlags(cred.flags));

    const uint8_t mode = getHidModeFromFlags(cred.flags);

    if (mode == 0) {
        Keyboard.print(cred.password);
    } else {
        if (strlen(cred.username) > 0) Keyboard.print(cred.username);
        Keyboard.write(KEY_TAB);
        Keyboard.print(cred.password);
        if (mode == 2) Keyboard.write(KEY_RETURN);
    }

    Keyboard.releaseAll();
    // Serial.println("[HID] HID type complete");
    memset(&cred, 0, sizeof(cred));
}

void setup() {
    delay(500); // Wait for USB to stabilize
    Serial.begin(115200); // USB CDC (WebSerial)
    delay(100); // Give Serial time to initialize
    Keyboard.begin();
    // When `ARDUINO_USB_MODE=0` (TinyUSB), USB is started automatically on boot
    // when `ARDUINO_USB_CDC_ON_BOOT=1`. Starting it again can cause a disconnect.
#if ARDUINO_USB_MODE
    USB.begin();
#endif

    authInit();
    storageInit();
    inputInit();
    deviceStateInit();
    serialProtocolInit();

    if (!uiInit()) {
        // Serial.println("[WARN] OLED init failed – running without display");
    } else {
        uiShowBootScreen();
    }
    delay(400);
    deviceSetUiState(DeviceUiState::LOCKED);
    uiRender(deviceGetUiState(), deviceGetSelectedIndex());
}

void loop() {
    serialProtocolLoop();
    authLoop();

    static bool lastAuth = false;
    static int cachedCount = 0;
    static uint32_t lastCountMs = 0;

    const bool authed = isAuthenticated();
    if (authed != lastAuth) {
        if (authed) {
            deviceSetUiState(DeviceUiState::IDLE);
        } else {
            deviceSetUiState(DeviceUiState::LOCKED);
        }
        cachedCount = 0;
        lastCountMs = 0;
        lastAuth = authed;
    }
    // Serial.print("[AUTH] Authenticated: ");
    // Serial.println(authed);
    InputEvents ev;
    inputPoll(ev);

    if (authed && (ev.up_pressed || ev.down_pressed || ev.confirm_short || ev.confirm_long)) {
        authRecordActivity();
    }

    if (authed) {
        const uint32_t now = millis();
        if (lastCountMs == 0 || (uint32_t)(now - lastCountMs) > 500) {
            cachedCount = getCredentialCount();
            lastCountMs = now;
        }

        const int count = cachedCount;
        deviceClampSelectedIndex(count);

        if (count > 0) {
            int selected = deviceGetSelectedIndex();

            if (ev.up_pressed) {
                selected = (selected - 1 + count) % count;
                deviceSetSelectedIndex(selected);
                deviceSetUiState(DeviceUiState::SELECTED);
            }
            if (ev.down_pressed) {
                selected = (selected + 1) % count;
                deviceSetSelectedIndex(selected);
                deviceSetUiState(DeviceUiState::SELECTED);
            }
            Serial.println("here1");
            Serial.println(ev.up_pressed);
            Serial.println(ev.down_pressed);
            Serial.println(ev.confirm_short);
            if (ev.up_pressed || ev.down_pressed) {
                Serial.println("here2");
                Serial.println("[INPUT] Confirm short pressed");
                const DeviceUiState state = deviceGetUiState();
                Serial.print("[INPUT] Confirm pressed, state: ");
                // Serial.println((int)state);
                if (state == DeviceUiState::IDLE || state == DeviceUiState::SELECTED) {
                    // Serial.print("[INPUT] Triggering HID for credential: ");
                    // Serial.println(selected);
                    typeSelectedViaHid(selected);
                    deviceSetUiState(DeviceUiState::IDLE);
                }
            }
        }
    }

    static DeviceUiState lastState = DeviceUiState::LOCKED;
    static int lastSelected = -1;
    static uint32_t lastRenderMs = 0;

    const DeviceUiState state = deviceGetUiState();
    const int selected = deviceGetSelectedIndex();

    const uint32_t now = millis();
    const uint32_t intervalMs = (state == DeviceUiState::TOTP) ? 200 : 750;
    const bool shouldRender = (state != lastState) ||
                              (selected != lastSelected) ||
                              lastRenderMs == 0 ||
                              (uint32_t)(now - lastRenderMs) > intervalMs;

    if (shouldRender) {
        uiRender(state, selected);
        lastState = state;
        lastSelected = selected;
        lastRenderMs = now;
    }

    delay(5);
}
