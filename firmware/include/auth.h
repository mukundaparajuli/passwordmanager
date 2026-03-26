#ifndef AUTH_H
#define AUTH_H

#include <Arduino.h>

struct UnlockResult {
    bool ok = false;
    bool locked_out = false;
    bool wrong_pin = false;
    uint8_t failed_attempts = 0;
    uint8_t max_attempts = 5;
    uint32_t lockout_ms_remaining = 0;
    String token;
    String error_code;
};

struct ChangePinResult {
    bool ok = false;
    bool locked_out = false;
    uint32_t lockout_ms_remaining = 0;
    String error_code;
};

void authInit();

bool isPinConfigured();
UnlockResult authUnlock(const String &pin);
ChangePinResult authChangePin(const String &oldPin, const String &newPin);

void authLock();
void authLoop();
void authRecordActivity();

const uint8_t *getEncryptionKey();
bool isAuthenticated();

#endif
