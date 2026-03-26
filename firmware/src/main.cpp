#include <Arduino.h>

#include "auth.h"
#include "device_state.h"
#include "input.h"
#include "storage.h"
#include "ui.h"
#include "web.h"

static int upPressCount = 0;
static int downPressCount = 0;
static unsigned long upLastPressTime = 0;
static unsigned long downLastPressTime = 0;
static const unsigned long DOUBLE_CLICK_WINDOW = 400;

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n=== VaultKey Booting ===");

    authInit();
    storageInit();
    inputInit();
    deviceStateInit();

    if (!uiInit()) {
        while (true) delay(1000);
    }

    uiShowBootScreen();
    delay(1000);

    webInit();

    if (!isPinConfigured())
        uiShowPinSetup();
    else
        uiShowPinEntry();
}

void loop() {
    authLoop();

    static bool lastAuth = false;
    static int lastRenderedIndex = -1;
    static int lastRenderedCount = -1;
    static bool lastRenderedDetail = false;
    static unsigned long lastRenderMs = 0;
    static unsigned long lastCountPollMs = 0;
    static int cachedCount = 0;

    bool authed = isAuthenticated();
    if (authed != lastAuth) {
        if (authed) {
            uiShowPinOk();
            delay(800);
        } else {
            if (!isPinConfigured())
                uiShowPinSetup();
            else
                uiShowPinEntry();
        }
        lastAuth = authed;
        lastRenderedIndex = -1;
        lastRenderedCount = -1;
        lastRenderedDetail = false;
        lastRenderMs = 0;
        lastCountPollMs = 0;
        cachedCount = 0;
    }

    if (!authed) {
        delay(200);
        return;
    }

    bool upPressed, downPressed;
    readButtons(upPressed, downPressed);

    const unsigned long now = millis();
    if (lastCountPollMs == 0 || (unsigned long)(now - lastCountPollMs) > 500) {
        cachedCount = getCredentialCount();
        lastCountPollMs = now;
    }
    int credentialCount = cachedCount;

    if (credentialCount == 0) {
        if (lastRenderedCount != 0 || lastRenderMs == 0 || (unsigned long)(now - lastRenderMs) > 1000) {
            uiShowNoCredentials();
            lastRenderedIndex = 0;
            lastRenderedDetail = false;
            lastRenderedCount = 0;
            lastRenderMs = millis();
        }
        delay(200);
        return;
    }

    deviceClampSelectedIndex(credentialCount);
    int selectedIndex = deviceGetSelectedIndex();
    bool showingDetail = deviceIsShowingDetail();

    if (upPressed) {
        authRecordActivity();
        unsigned long now = millis();
        if (now - upLastPressTime > DOUBLE_CLICK_WINDOW)
            upPressCount = 0;

        upPressCount++;
        upLastPressTime = now;

        if (upPressCount == 2) {
            showingDetail = true;
            deviceSetShowingDetail(true);
            uiShowCredentialDetail(deviceGetSelectedIndex());
            lastRenderedIndex = deviceGetSelectedIndex();
            lastRenderedDetail = true;
            lastRenderedCount = credentialCount;
            lastRenderMs = millis();
            upPressCount = 0;
            delay(250);
            return;
        }

        if (!showingDetail) {
            selectedIndex--;
            if (selectedIndex < 0)
                selectedIndex = credentialCount - 1;
            deviceSetSelectedIndex(selectedIndex);
        }
        delay(120);
    }

    if (downPressed) {
        authRecordActivity();
        unsigned long now = millis();
        if (now - downLastPressTime > DOUBLE_CLICK_WINDOW)
            downPressCount = 0;

        downPressCount++;
        downLastPressTime = now;

        if (downPressCount == 2) {
            showingDetail = false;
            deviceSetShowingDetail(false);
            downPressCount = 0;
            delay(250);
            return;
        }

        if (!showingDetail) {
            selectedIndex++;
            if (selectedIndex >= credentialCount)
                selectedIndex = 0;
            deviceSetSelectedIndex(selectedIndex);
        }
        delay(120);
    }

    selectedIndex = deviceGetSelectedIndex();
    showingDetail = deviceIsShowingDetail();

    const bool shouldRender = (selectedIndex != lastRenderedIndex) ||
                              (showingDetail != lastRenderedDetail) ||
                              (credentialCount != lastRenderedCount) ||
                              (lastRenderMs == 0) ||
                              ((unsigned long)(millis() - lastRenderMs) > 1000);

    if (shouldRender) {
        if (showingDetail) uiShowCredentialDetail(selectedIndex);
        else uiShowCredentialList(selectedIndex, credentialCount);
        lastRenderedIndex = selectedIndex;
        lastRenderedDetail = showingDetail;
        lastRenderedCount = credentialCount;
        lastRenderMs = millis();
    }

    delay(30);
}
