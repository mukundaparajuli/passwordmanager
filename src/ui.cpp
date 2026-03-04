#include "ui.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

#include "auth.h"
#include "config.h"
#include "crypto.h"
#include "storage.h"

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool uiInit() {
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println("OLED init failed!");
        return false;
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    return true;
}

void uiShowBootScreen() {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Shield Key Ready");
    display.display();
}

void uiShowNoCredentials() {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("No Credentials");
    display.display();
}

void uiShowCredentialList(int selectedIndex, int credentialCount) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Vault");

    const uint8_t *key = getEncryptionKey();
    if (!key) return;

    const int maxVisible = 6;
    int startIndex = 0;

    if (selectedIndex >= maxVisible)
        startIndex = selectedIndex - maxVisible + 1;

    for (int i = 0; i < maxVisible; i++) {
        int credIndex = startIndex + i;
        if (credIndex >= credentialCount) break;

        Credential cred;
        if (!getCredential(credIndex, cred, key)) continue;

        if (credIndex == selectedIndex)
            display.print("-> ");
        else
            display.print("   ");

        display.println(cred.serviceName);
    }

    display.display();
}

void uiShowCredentialDetail(int index) {
    const uint8_t *key = getEncryptionKey();
    if (!key) return;

    Credential cred;
    if (!getCredential(index, cred, key)) return;

    // Mask a string: show first char then asterisks, or all asterisks if short
    auto mask = [](const String &s) -> String {
        if (s.length() == 0) return "****";
        if (s.length() <= 2) return String("****");
        String masked;
        masked += s[0];
        for (unsigned int i = 1; i < s.length(); i++) masked += '*';
        return masked;
    };

    display.clearDisplay();
    display.setCursor(0, 0);

    display.print("Svc: ");
    display.println(cred.serviceName);

    if (cred.serviceUrl.length() > 0) {
        display.print("URL: ");
        display.println(cred.serviceUrl);
    }

    display.print("ID:  ");
    display.println(cred.identifier);

    display.print("Pwd: ");
    display.println(mask(cred.password));

    if (cred.totpSecret.length() > 0) {
        display.print("TOTP: ");
        display.println("******");
    }

    display.display();
}

void uiShowPinSetup() {
    display.clearDisplay();
    display.setCursor(0, 10);
    display.println("No PIN set.");
    display.println();
    display.println("Enter 4-digit PIN");
    display.println("via Serial Monitor");
    display.display();
}

void uiShowPinEntry() {
    display.clearDisplay();
    display.setCursor(0, 10);
    display.println("Device Locked");
    display.println();
    display.println("Enter PIN via");
    display.println("Serial Monitor");
    display.display();
}

void uiShowPinOk() {
    display.clearDisplay();
    display.setCursor(0, 20);
    display.setTextSize(2);
    display.println("Unlocked!");
    display.setTextSize(1);
    display.display();
}

void uiShowPinFail() {
    display.clearDisplay();
    display.setCursor(0, 20);
    display.setTextSize(2);
    display.println("Wrong PIN");
    display.setTextSize(1);
    display.display();
}

void uiShowPinCreated() {
    display.clearDisplay();
    display.setCursor(0, 20);
    display.setTextSize(2);
    display.println("PIN Set!");
    display.setTextSize(1);
    display.display();
}
