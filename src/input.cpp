#include "input.h"

#include "auth.h"
#include "config.h"
#include "storage.h"

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

bool handleSerialInput() {
    if (!Serial.available()) return false;

    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.startsWith("ADD |")) {
        int p1 = input.indexOf('|');
        int p2 = input.indexOf('|', p1 + 1);
        int p3 = input.indexOf('|', p2 + 1);
        int p4 = input.indexOf('|', p3 + 1);

        if (p1 == -1 || p2 == -1 || p3 == -1 || p4 == -1) {
            Serial.println("Format: ADD | service | url | identifier | password");
            Serial.println("   or:  ADD | service | url | identifier | password | totpSecret");
            return false;
        }

        int p5 = input.indexOf('|', p4 + 1);

        Credential cred;
        cred.serviceName = input.substring(p1 + 1, p2);
        cred.serviceUrl = input.substring(p2 + 1, p3);
        cred.identifier = input.substring(p3 + 1, p4);

        if (p5 != -1) {
            cred.password = input.substring(p4 + 1, p5);
            cred.totpSecret = input.substring(p5 + 1);
        } else {
            cred.password = input.substring(p4 + 1);
            cred.totpSecret = "";
        }

        cred.serviceName.trim();
        cred.serviceUrl.trim();
        cred.identifier.trim();
        cred.password.trim();
        cred.totpSecret.trim();

        const uint8_t *key = getEncryptionKey();
        if (!key) {
            Serial.println("Not authenticated");
            return false;
        }

        if (storeCredential(cred, key)) {
            Serial.println("Credential stored (encrypted)");
            return true;
        } else {
            Serial.println("Failed to store credential");
        }
    }

    // TOTP | index | secret  — add/update TOTP secret for a credential
    if (input.startsWith("TOTP |") || input.startsWith("TOTP|")) {
        int p1 = input.indexOf('|');
        int p2 = input.indexOf('|', p1 + 1);

        if (p1 == -1 || p2 == -1) {
            Serial.println("Format: TOTP | index | totpSecret");
            return false;
        }

        String idxStr = input.substring(p1 + 1, p2);
        String secret = input.substring(p2 + 1);
        idxStr.trim();
        secret.trim();

        int idx = idxStr.toInt();
        const uint8_t *key = getEncryptionKey();
        if (!key) {
            Serial.println("Not authenticated");
            return false;
        }

        Credential cred;
        if (!getCredential(idx, cred, key)) {
            Serial.println("Credential not found at index " + String(idx));
            return false;
        }

        cred.totpSecret = secret;
        if (updateCredential(idx, cred, key)) {
            Serial.println("TOTP secret updated for: " + cred.serviceName);
            return true;
        } else {
            Serial.println("Failed to update TOTP secret");
        }
    }

    // EDITPWD | index | newPassword  — update password for a credential
    if (input.startsWith("EDITPWD |") || input.startsWith("EDITPWD|")) {
        int p1 = input.indexOf('|');
        int p2 = input.indexOf('|', p1 + 1);

        if (p1 == -1 || p2 == -1) {
            Serial.println("Format: EDITPWD | index | newPassword");
            return false;
        }

        String idxStr = input.substring(p1 + 1, p2);
        String newPwd = input.substring(p2 + 1);
        idxStr.trim();
        newPwd.trim();

        int idx = idxStr.toInt();
        const uint8_t *key = getEncryptionKey();
        if (!key) {
            Serial.println("Not authenticated");
            return false;
        }

        Credential cred;
        if (!getCredential(idx, cred, key)) {
            Serial.println("Credential not found at index " + String(idx));
            return false;
        }

        cred.password = newPwd;
        if (updateCredential(idx, cred, key)) {
            Serial.println("Password updated for: " + cred.serviceName);
            return true;
        } else {
            Serial.println("Failed to update password");
        }
    }

    return false;
}
