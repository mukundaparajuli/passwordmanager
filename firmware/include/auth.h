#ifndef AUTH_H
#define AUTH_H

#include <Arduino.h>

struct UnlockResult {
    bool ok = false;
    bool needs_pin_confirm = false;
    bool locked_out = false;
    uint32_t lockout_ms_remaining = 0;
    String token;
    String error;
};

void authInit();

bool isPinConfigured();
UnlockResult authUnlock(const String &pin, const String &pinConfirm);

void authLock();
void authLoop();
void authRecordActivity();

bool authVerifySessionToken(const String &tokenHex);

const uint8_t *getEncryptionKey();
bool isAuthenticated();

#endif
