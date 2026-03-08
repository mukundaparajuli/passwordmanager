#include "auth.h"

#include <Preferences.h>
#include <esp_system.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <string.h>

#include "crypto.h"

static const char *AUTH_NAMESPACE = "vault";

static const size_t SALT_LEN = 16;
static const size_t SESSION_TOKEN_LEN = 32;

static const uint32_t DEFAULT_KDF_ITERS = 50000;
static const uint32_t AUTO_LOCK_MS = 60UL * 1000UL;
static const uint32_t LOCKOUT_MS = 5UL * 60UL * 1000UL;
static const uint8_t MAX_FAILED_ATTEMPTS = 5;

static uint8_t encKey[AES_KEY_SIZE];
static uint8_t sessionToken[SESSION_TOKEN_LEN];
static bool authenticated = false;
static bool sessionActive = false;

static uint32_t lastActivityMs = 0;

static uint8_t failedAttempts = 0;
static uint32_t lockoutUntilMs = 0;

static const uint8_t VERIFY_PLAINTEXT[] = {
    'V','A','U','L','T','K','E','Y','_','V','E','R','I','F','Y','1'
};

static bool safeCompare(const uint8_t *a, const uint8_t *b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}

static String bytesToHex(const uint8_t *bytes, size_t len) {
    static const char HEX_CHARS[] = "0123456789abcdef";
    String out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        out += HEX_CHARS[(bytes[i] >> 4) & 0x0F];
        out += HEX_CHARS[bytes[i] & 0x0F];
    }
    return out;
}

static int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static bool hexToBytes(const String &hex, uint8_t *out, size_t outLen) {
    if (hex.length() != (int)(outLen * 2)) return false;
    for (size_t i = 0; i < outLen; i++) {
        int hi = hexNibble(hex[(int)(i * 2)]);
        int lo = hexNibble(hex[(int)(i * 2 + 1)]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static bool deriveKeyPBKDF2(const uint8_t *salt, size_t saltLen,
                            const String &pin, uint32_t iterations,
                            uint8_t *outKey) {
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info) return false;

    mbedtls_md_init(&ctx);
    int ret = mbedtls_md_setup(&ctx, info, 1);
    if (ret != 0) {
        mbedtls_md_free(&ctx);
        return false;
    }

    ret = mbedtls_pkcs5_pbkdf2_hmac(&ctx,
                                    (const unsigned char *)pin.c_str(), pin.length(),
                                    salt, saltLen,
                                    iterations,
                                    AES_KEY_SIZE,
                                    outKey);
    mbedtls_md_free(&ctx);
    return ret == 0;
}

static void randomBytes(uint8_t *out, size_t len) {
    esp_fill_random(out, len);
}

static bool isLockoutActive(uint32_t nowMs, uint32_t &outRemaining) {
    if (lockoutUntilMs == 0) {
        outRemaining = 0;
        return false;
    }
    if ((int32_t)(lockoutUntilMs - nowMs) <= 0) {
        lockoutUntilMs = 0;
        outRemaining = 0;
        return false;
    }
    outRemaining = lockoutUntilMs - nowMs;
    return true;
}

static bool validatePinFormat(const String &pin, String &outError) {
    if (pin.length() < 4 || pin.length() > 12) {
        outError = "PIN must be 4–12 digits.";
        return false;
    }
    for (size_t i = 0; i < pin.length(); i++) {
        if (!isDigit(pin[i])) {
            outError = "PIN must be numeric.";
            return false;
        }
    }
    return true;
}

static bool setupPinInternal(const String &pin, String &outError) {
    uint8_t salt[SALT_LEN];
    randomBytes(salt, sizeof(salt));

    uint8_t key[AES_KEY_SIZE];
    if (!deriveKeyPBKDF2(salt, sizeof(salt), pin, DEFAULT_KDF_ITERS, key)) {
        outError = "Key derivation failed.";
        return false;
    }

    uint8_t verifyIv[IV_SIZE];
    randomBytes(verifyIv, sizeof(verifyIv));

    uint8_t verifyCipher[sizeof(VERIFY_PLAINTEXT) + AES_BLOCK_SIZE];
    size_t verifyCipherLen = 0;

    if (!aesEncrypt(key, verifyIv,
                    VERIFY_PLAINTEXT, sizeof(VERIFY_PLAINTEXT),
                    verifyCipher, verifyCipherLen)) {
        memset(key, 0, sizeof(key));
        outError = "Verify encryption failed.";
        return false;
    }

    const size_t blobLen = IV_SIZE + verifyCipherLen;
    uint8_t *blob = (uint8_t *)malloc(blobLen);
    if (!blob) {
        memset(key, 0, sizeof(key));
        outError = "Out of memory.";
        return false;
    }
    memcpy(blob, verifyIv, IV_SIZE);
    memcpy(blob + IV_SIZE, verifyCipher, verifyCipherLen);

    Preferences prefs;
    prefs.begin(AUTH_NAMESPACE, false);
    prefs.putBytes("salt", salt, sizeof(salt));
    prefs.putUInt("kdf_iters", DEFAULT_KDF_ITERS);
    prefs.putBytes("verify", blob, blobLen);
    prefs.end();

    memset(blob, 0, blobLen);
    free(blob);

    memcpy(encKey, key, sizeof(encKey));
    memset(key, 0, sizeof(key));

    authenticated = true;
    sessionActive = true;
    randomBytes(sessionToken, sizeof(sessionToken));
    lastActivityMs = millis();
    failedAttempts = 0;
    lockoutUntilMs = 0;

    return true;
}

static bool verifyPinInternal(const String &pin, String &outError) {
    Preferences prefs;
    prefs.begin(AUTH_NAMESPACE, true);

    uint8_t salt[SALT_LEN];
    size_t saltRead = prefs.getBytes("salt", salt, sizeof(salt));
    uint32_t iters = prefs.getUInt("kdf_iters", DEFAULT_KDF_ITERS);

    size_t blobLen = prefs.getBytesLength("verify");
    if (saltRead != sizeof(salt) || blobLen < (IV_SIZE + AES_BLOCK_SIZE)) {
        prefs.end();
        outError = "Device not initialized (missing auth data).";
        return false;
    }

    uint8_t *blob = (uint8_t *)malloc(blobLen);
    if (!blob) {
        prefs.end();
        outError = "Out of memory.";
        return false;
    }
    prefs.getBytes("verify", blob, blobLen);
    prefs.end();

    uint8_t key[AES_KEY_SIZE];
    if (!deriveKeyPBKDF2(salt, sizeof(salt), pin, iters, key)) {
        memset(blob, 0, blobLen);
        free(blob);
        outError = "Key derivation failed.";
        return false;
    }

    uint8_t iv[IV_SIZE];
    memcpy(iv, blob, IV_SIZE);
    const uint8_t *cipher = blob + IV_SIZE;
    size_t cipherLen = blobLen - IV_SIZE;

    uint8_t *plaintext = (uint8_t *)malloc(cipherLen);
    if (!plaintext) {
        memset(key, 0, sizeof(key));
        memset(blob, 0, blobLen);
        free(blob);
        outError = "Out of memory.";
        return false;
    }

    size_t plainLen = 0;
    bool ok = aesDecrypt(key, iv, cipher, cipherLen, plaintext, plainLen);

    bool match = ok && plainLen == sizeof(VERIFY_PLAINTEXT) &&
                 safeCompare(plaintext, VERIFY_PLAINTEXT, sizeof(VERIFY_PLAINTEXT));

    memset(plaintext, 0, cipherLen);
    free(plaintext);

    memset(blob, 0, blobLen);
    free(blob);

    if (!match) {
        memset(key, 0, sizeof(key));
        outError = "Wrong PIN.";
        return false;
    }

    memcpy(encKey, key, sizeof(encKey));
    memset(key, 0, sizeof(key));

    authenticated = true;
    sessionActive = true;
    randomBytes(sessionToken, sizeof(sessionToken));
    lastActivityMs = millis();
    failedAttempts = 0;
    lockoutUntilMs = 0;

    return true;
}

void authInit() {
    authLock();
    failedAttempts = 0;
    lockoutUntilMs = 0;
}

bool isPinConfigured() {
    Preferences prefs;
    prefs.begin(AUTH_NAMESPACE, true);
    bool hasSalt = prefs.getBytesLength("salt") == SALT_LEN;
    bool hasVerify = prefs.getBytesLength("verify") > 0;
    prefs.end();
    return hasSalt && hasVerify;
}

UnlockResult authUnlock(const String &pin, const String &pinConfirm) {
    UnlockResult res;

    uint32_t remaining = 0;
    const uint32_t now = millis();
    if (isLockoutActive(now, remaining)) {
        res.locked_out = true;
        res.lockout_ms_remaining = remaining;
        res.error = "Too many attempts. Try again later.";
        return res;
    }

    String err;
    if (!validatePinFormat(pin, err)) {
        res.error = err;
        return res;
    }

    if (!isPinConfigured()) {
        if (pinConfirm.length() == 0) {
            res.needs_pin_confirm = true;
            res.error = "First-time setup: pin_confirm is required.";
            return res;
        }
        if (pinConfirm != pin) {
            res.error = "PINs do not match.";
            return res;
        }
        if (!setupPinInternal(pin, err)) {
            res.error = err;
            return res;
        }

        res.ok = true;
        res.token = bytesToHex(sessionToken, sizeof(sessionToken));
        return res;
    }

    if (!verifyPinInternal(pin, err)) {
        failedAttempts++;
        if (failedAttempts >= MAX_FAILED_ATTEMPTS) {
            lockoutUntilMs = millis() + LOCKOUT_MS;
            failedAttempts = 0;
            uint32_t lockRemaining = 0;
            isLockoutActive(millis(), lockRemaining);
            res.locked_out = true;
            res.lockout_ms_remaining = lockRemaining;
            res.error = "Too many attempts. Locked out.";
            return res;
        }

        res.error = err;
        return res;
    }

    res.ok = true;
    res.token = bytesToHex(sessionToken, sizeof(sessionToken));
    return res;
}

void authLock() {
    authenticated = false;
    sessionActive = false;
    lastActivityMs = 0;
    memset(encKey, 0, sizeof(encKey));
    memset(sessionToken, 0, sizeof(sessionToken));
}

void authRecordActivity() {
    if (!authenticated || !sessionActive) return;
    lastActivityMs = millis();
}

void authLoop() {
    if (!authenticated || !sessionActive) return;
    uint32_t now = millis();
    if (lastActivityMs != 0 && (uint32_t)(now - lastActivityMs) > AUTO_LOCK_MS) {
        authLock();
    }
}

bool authVerifySessionToken(const String &tokenHex) {
    if (!authenticated || !sessionActive) return false;
    uint8_t candidate[SESSION_TOKEN_LEN];
    if (!hexToBytes(tokenHex, candidate, sizeof(candidate))) return false;
    bool ok = safeCompare(candidate, sessionToken, sizeof(sessionToken));
    memset(candidate, 0, sizeof(candidate));
    return ok;
}

const uint8_t *getEncryptionKey() {
    return authenticated ? encKey : nullptr;
}

bool isAuthenticated() {
    return authenticated && sessionActive;
}
