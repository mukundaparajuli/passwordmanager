#include <Arduino.h>

#include "auth.h"
#include "input.h"
#include "storage.h"
#include "ui.h"

static int selectedIndex = 0;
static int credentialCount = 0;
static bool showingDetail = false;

static int upPressCount = 0;
static int downPressCount = 0;
static unsigned long upLastPressTime = 0;
static unsigned long downLastPressTime = 0;
static const unsigned long DOUBLE_CLICK_WINDOW = 400;

static void authenticateUser() {
    if (!isPinConfigured()) {
        uiShowPinSetup();
        Serial.println("\n[AUTH] No PIN configured. Please set a 4-digit PIN.");

        while (true) {
            String pin1 = readPinFromSerial();
            Serial.print("Confirm PIN: ");
            String pin2 = readPinFromSerial();

            if (pin1 != pin2) {
                Serial.println("[AUTH] PINs do not match. Try again.");
                uiShowPinFail();
                delay(1500);
                uiShowPinSetup();
                continue;
            }

            if (setupPin(pin1)) {
                uiShowPinCreated();
                delay(1500);
                break;
            }
        }
    }

    uiShowPinEntry();
    Serial.println("\n[AUTH] Device locked. Enter your 4-digit PIN.");

    while (true) {
        String pin = readPinFromSerial();

        if (verifyPin(pin)) {
            uiShowPinOk();
            Serial.println("[AUTH] PIN accepted.");
            delay(1000);
            return;
        }

        Serial.println("[AUTH] Wrong PIN. Try again.");
        uiShowPinFail();
        delay(1500);
        uiShowPinEntry();
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n=== Shield Key Booting ===");

    storageInit();
    inputInit();

    if (!uiInit()) {
        while (true) delay(1000);
    }

    uiShowBootScreen();
    delay(1000);

    authenticateUser();

    Serial.println("Commands:");
    Serial.println("  ADD | service | url | identifier | password");
    Serial.println("  ADD | service | url | identifier | password | totpSecret");
    Serial.println("  TOTP | index | totpSecret");
    Serial.println("  EDITPWD | index | newPassword");
}

void loop() {
    handleSerialInput();

    bool upPressed, downPressed;
    readButtons(upPressed, downPressed);

    credentialCount = getCredentialCount();

    if (credentialCount == 0) {
        uiShowNoCredentials();
        delay(200);
        return;
    }

    if (upPressed) {
        unsigned long now = millis();
        if (now - upLastPressTime > DOUBLE_CLICK_WINDOW)
            upPressCount = 0;

        upPressCount++;
        upLastPressTime = now;

        if (upPressCount == 2) {
            showingDetail = true;
            uiShowCredentialDetail(selectedIndex);
            upPressCount = 0;
            delay(250);
            return;
        }

        if (!showingDetail) {
            selectedIndex--;
            if (selectedIndex < 0)
                selectedIndex = credentialCount - 1;
        }
        delay(120);
    }

    if (downPressed) {
        unsigned long now = millis();
        if (now - downLastPressTime > DOUBLE_CLICK_WINDOW)
            downPressCount = 0;

        downPressCount++;
        downLastPressTime = now;

        if (downPressCount == 2) {
            showingDetail = false;
            downPressCount = 0;
            delay(250);
            return;
        }

        if (!showingDetail) {
            selectedIndex++;
            if (selectedIndex >= credentialCount)
                selectedIndex = 0;
        }
        delay(120);
    }

    if (showingDetail)
        uiShowCredentialDetail(selectedIndex);
    else
        uiShowCredentialList(selectedIndex, credentialCount);
}
