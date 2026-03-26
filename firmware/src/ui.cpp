#include "ui.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <string.h>
#include <time.h>

#include "auth.h"
#include "config.h"
#include "crypto.h"
#include "storage.h"

namespace {

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

String ellipsize(const char *s, size_t maxChars) {
    if (!s) return "";
    String str(s);
    str.trim();
    if (str.length() <= (int)maxChars) return str;
    if (maxChars <= 1) return str.substring(0, maxChars);
    return str.substring(0, maxChars - 1) + ".";
}

void drawProgressBar(int x, int y, int w, int h, float pct) {
    if (pct < 0) pct = 0;
    if (pct > 1) pct = 1;
    display.drawRect(x, y, w, h, SSD1306_WHITE);
    const int fillW = (int)((w - 2) * pct);
    if (fillW > 0) display.fillRect(x + 1, y + 1, fillW, h - 2, SSD1306_WHITE);
}

} // namespace

bool uiInit() {
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println("[UI] OLED init failed");
        return false;
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.display();
    return true;
}

void uiShowBootScreen() {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("VaultKey");
    display.println();
    display.println("Booting...");
    display.display();
}

void uiRender(DeviceUiState state, int selectedId) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    if (state == DeviceUiState::LOCKED) {
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println("VaultKey");
        display.println();
        display.println("LOCKED");
        display.println();
        display.println("Unlock via browser");
        display.display();
        return;
    }

    const uint8_t *key = getEncryptionKey();
    if (!key) {
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println("VaultKey");
        display.println();
        display.println("LOCKED");
        display.display();
        return;
    }

    const int count = getCredentialCount();
    if (count <= 0) {
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println("VaultKey");
        display.println();
        display.println("No credentials");
        display.display();
        return;
    }

    credential_entry_t cred;
    memset(&cred, 0, sizeof(cred));
    bool haveCred = getCredential(selectedId, cred, key);

    if (!haveCred) {
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println("VaultKey");
        display.println();
        display.println("Not found");
        display.display();
        return;
    }

    const String svc = ellipsize(cred.service, 20);
    const String user = ellipsize(cred.username, 21);

    if (state == DeviceUiState::IDLE) {
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println("VaultKey  OK");
        display.println();
        display.println("Selected:");
        display.println(svc.length() ? svc : "(unnamed)");
        display.display();
        memset(&cred, 0, sizeof(cred));
        return;
    }

    // TODO [ESP32-S2/S3]: Uncomment CONFIRM_HID UI block when USB HID is available.
    // if (state == DeviceUiState::CONFIRM_HID) {
    //     display.setTextSize(1);
    //     display.setCursor(0, 0);
    //     display.println("Password:");
    //     display.println();
    //     display.println(svc.length() ? svc : "(unnamed)");
    //     display.println();
    //     display.println(ellipsize(cred.password, 21));
    //     display.println();
    //     display.println("CONFIRM: Type");
    //     display.display();
    //     memset(&cred, 0, sizeof(cred));
    //     return;
    // }

    if (state == DeviceUiState::TOTP) {
        if (strlen(cred.totp_secret) == 0) {
            display.setTextSize(1);
            display.setCursor(0, 0);
            display.println("No TOTP");
            display.println();
            display.println(svc.length() ? svc : "(unnamed)");
            display.display();
            memset(&cred, 0, sizeof(cred));
            return;
        }

        const String code = generateTOTP(String(cred.totp_secret));
        if (code == "NO_TIME") {
            display.setTextSize(1);
            display.setCursor(0, 0);
            display.println("No time set");
            display.println();
            display.println("Unlock via browser");
            display.display();
            memset(&cred, 0, sizeof(cred));
            return;
        }
        if (code == "INVALID") {
            display.setTextSize(1);
            display.setCursor(0, 0);
            display.println("Bad TOTP");
            display.println();
            display.println(svc.length() ? svc : "(unnamed)");
            display.display();
            memset(&cred, 0, sizeof(cred));
            return;
        }

        const time_t now = time(nullptr);
        const int expiresIn = (int)(TOTP_PERIOD - (now % TOTP_PERIOD));

        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println(svc.length() ? svc : "(unnamed)");

        display.setTextSize(3);
        display.setCursor(0, 16);
        display.println(code);

        display.setTextSize(1);
        display.setCursor(0, 54);
        display.print("in ");
        display.print(expiresIn);
        display.print("s");

        drawProgressBar(70, 56, 56, 7, (float)expiresIn / (float)TOTP_PERIOD);

        display.display();
        memset(&cred, 0, sizeof(cred));
        return;
    }

    // SELECTED
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println(ellipsize(cred.service, 10));

    display.setTextSize(1);
    display.setCursor(0, 28);
    display.print("User: ");
    display.println(user.length() ? user : "-");

    display.setCursor(0, 44);
    display.println("CONFIRM: Type");
    display.setCursor(0, 54);
    display.println("Hold: TOTP");

    display.display();
    memset(&cred, 0, sizeof(cred));
}
