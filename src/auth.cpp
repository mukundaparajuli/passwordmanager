#include "auth.h"

#include <Preferences.h>
#include <esp_random.h>
#include <mbedtls/md.h>

static const char *AUTH_NAMESPACE = "auth";
static const int SALT_LEN = 16;
static const int HASH_LEN = 32;
static const int ROUNDS = 10000;

static uint8_t encKey[HASH_LEN];
static bool authenticated = false;

static void deriveKey(const uint8_t *salt, size_t saltLen,
                      const String &pin, int rounds,
                      uint8_t *out) {
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, info, 0);

    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, salt, saltLen);
    mbedtls_md_update(&ctx, (const uint8_t *)pin.c_str(), pin.length());
    mbedtls_md_finish(&ctx, out);

    for (int i = 1; i < rounds; i++) {
        mbedtls_md_starts(&ctx);
        mbedtls_md_update(&ctx, salt, saltLen);
        mbedtls_md_update(&ctx, out, HASH_LEN);
        mbedtls_md_update(&ctx, (const uint8_t *)pin.c_str(), pin.length());
        mbedtls_md_finish(&ctx, out);
    }

    mbedtls_md_free(&ctx);
}

static void deriveEncKey(const uint8_t *salt, size_t saltLen,
                         const String &pin, int rounds) {
    static const uint8_t prefix[] = "ENC";
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, info, 0);

    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, prefix, 3);
    mbedtls_md_update(&ctx, salt, saltLen);
    mbedtls_md_update(&ctx, (const uint8_t *)pin.c_str(), pin.length());
    mbedtls_md_finish(&ctx, encKey);

    for (int i = 1; i < rounds; i++) {
        mbedtls_md_starts(&ctx);
        mbedtls_md_update(&ctx, prefix, 3);
        mbedtls_md_update(&ctx, salt, saltLen);
        mbedtls_md_update(&ctx, encKey, HASH_LEN);
        mbedtls_md_update(&ctx, (const uint8_t *)pin.c_str(), pin.length());
        mbedtls_md_finish(&ctx, encKey);
    }

    mbedtls_md_free(&ctx);
    authenticated = true;
}

static bool safeCompare(const uint8_t *a, const uint8_t *b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

bool isPinConfigured() {
    Preferences prefs;
    prefs.begin(AUTH_NAMESPACE, true);
    bool exists = prefs.isKey("hash");
    prefs.end();
    return exists;
}

bool setupPin(const String &pin) {
    if (pin.length() != 4) return false;

    for (unsigned int i = 0; i < pin.length(); i++) {
        if (!isDigit(pin[i])) return false;
    }

    uint8_t salt[SALT_LEN];
    for (int i = 0; i < SALT_LEN; i += 4) {
        uint32_t r = esp_random();
        int remaining = SALT_LEN - i;
        int copyLen = (remaining < 4) ? remaining : 4;
        memcpy(salt + i, &r, copyLen);
    }

    uint8_t hash[HASH_LEN];
    deriveKey(salt, SALT_LEN, pin, ROUNDS, hash);

    Preferences prefs;
    prefs.begin(AUTH_NAMESPACE, false);
    prefs.putBytes("salt", salt, SALT_LEN);
    prefs.putBytes("hash", hash, HASH_LEN);
    prefs.putInt("rounds", ROUNDS);
    prefs.end();

    deriveEncKey(salt, SALT_LEN, pin, ROUNDS);

    return true;
}

bool verifyPin(const String &pin) {
    if (pin.length() != 4) return false;

    Preferences prefs;
    prefs.begin(AUTH_NAMESPACE, true);

    uint8_t storedSalt[SALT_LEN];
    uint8_t storedHash[HASH_LEN];
    int rounds = prefs.getInt("rounds", ROUNDS);

    size_t saltRead = prefs.getBytes("salt", storedSalt, SALT_LEN);
    size_t hashRead = prefs.getBytes("hash", storedHash, HASH_LEN);
    prefs.end();

    if (saltRead != SALT_LEN || hashRead != HASH_LEN) return false;

    uint8_t derived[HASH_LEN];
    deriveKey(storedSalt, SALT_LEN, pin, rounds, derived);

    if (!safeCompare(derived, storedHash, HASH_LEN)) return false;

    deriveEncKey(storedSalt, SALT_LEN, pin, rounds);
    return true;
}

const uint8_t *getEncryptionKey() {
    return authenticated ? encKey : nullptr;
}

bool isAuthenticated() {
    return authenticated;
}

String readPinFromSerial() {
    String pin = "";
    Serial.print("Enter 4-digit PIN: ");

    while (pin.length() < 4) {
        if (Serial.available()) {
            char c = Serial.read();
            if (isDigit(c)) {
                pin += c;
                Serial.print('*');
            }
        }
        delay(10);
    }

    Serial.println();
    return pin;
}
