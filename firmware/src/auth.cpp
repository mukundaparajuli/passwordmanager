#include "auth.h"

#include <Preferences.h>
#include <esp_system.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <string.h>

#include "crypto.h"
#include "storage.h"

namespace {

static const char *NS = "vault";

static constexpr size_t SALT_LEN = 16;
static constexpr size_t VERIFY_CIPHER_LEN = 32;
static constexpr size_t SESSION_TOKEN_LEN = 32;

static constexpr uint32_t KDF_ITERS = 10000;
static constexpr uint32_t AUTO_LOCK_MS = 120UL * 1000UL;

static constexpr uint8_t MAX_FAILED_ATTEMPTS = 5;
static constexpr uint32_t LOCKOUT_BASE_MS = 30UL * 1000UL;
static constexpr uint32_t LOCKOUT_MAX_MS = 15UL * 60UL * 1000UL;

static const uint8_t VERIFY_PLAINTEXT[16] = {
    'V','A','U','L','T','K','E','Y','_','V','E','R','I','F','Y','_'
};

static uint8_t encKey[AES_KEY_SIZE];
static uint8_t sessionToken[SESSION_TOKEN_LEN];
static bool authenticated = false;
static uint32_t lastActivityMs = 0;

static uint8_t failedAttempts = 0;
static uint8_t lockoutLevel = 0;
static uint32_t lockoutUntilMs = 0;

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

static void randomBytes(uint8_t *out, size_t len) {
    esp_fill_random(out, len);
}

static bool deriveKeyPBKDF2(const uint8_t *salt, size_t saltLen,
                            const String &pin,
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
                                    KDF_ITERS,
                                    AES_KEY_SIZE,
                                    outKey);
    mbedtls_md_free(&ctx);
    return ret == 0;
}

static bool validatePinFormat(const String &pin) {
    if (pin.length() < 4 || pin.length() > 8) return false;
    for (size_t i = 0; i < pin.length(); i++) {
        if (!isDigit(pin[i])) return false;
    }
    return true;
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

static uint32_t computeLockoutMs(uint8_t level) {
    uint32_t ms = LOCKOUT_BASE_MS;
    for (uint8_t i = 1; i < level; i++) {
        if (ms >= (LOCKOUT_MAX_MS / 2)) return LOCKOUT_MAX_MS;
        ms *= 2;
    }
    if (ms > LOCKOUT_MAX_MS) ms = LOCKOUT_MAX_MS;
    return ms;
}

static bool readSaltAndVerify(uint8_t salt[SALT_LEN], uint8_t verify[VERIFY_CIPHER_LEN]) {
    Preferences prefs;
    prefs.begin(NS, true);
    size_t saltRead = prefs.getBytes("salt", salt, SALT_LEN);
    size_t verifyLen = prefs.getBytesLength("verify");
    if (verifyLen != VERIFY_CIPHER_LEN) {
        prefs.end();
        return false;
    }
    prefs.getBytes("verify", verify, VERIFY_CIPHER_LEN);
    prefs.end();
    return saltRead == SALT_LEN;
}

static bool writeSaltAndVerify(const uint8_t salt[SALT_LEN], const uint8_t verify[VERIFY_CIPHER_LEN]) {
    Preferences prefs;
    prefs.begin(NS, false);
    bool ok = prefs.putBytes("salt", salt, SALT_LEN) == SALT_LEN &&
              prefs.putBytes("verify", verify, VERIFY_CIPHER_LEN) == VERIFY_CIPHER_LEN;
    prefs.end();
    return ok;
}

static bool writeVerifyForKey(const uint8_t key[AES_KEY_SIZE]) {
    uint8_t iv[IV_SIZE] = {0};
    uint8_t verifyCipher[VERIFY_CIPHER_LEN];
    size_t verifyCipherLen = 0;

    if (!aesEncrypt(key, iv, VERIFY_PLAINTEXT, sizeof(VERIFY_PLAINTEXT), verifyCipher, verifyCipherLen)) {
        return false;
    }
    if (verifyCipherLen != VERIFY_CIPHER_LEN) return false;

    Preferences prefs;
    prefs.begin(NS, false);
    bool ok = prefs.putBytes("verify", verifyCipher, verifyCipherLen) == verifyCipherLen;
    prefs.end();

    memset(verifyCipher, 0, sizeof(verifyCipher));
    return ok;
}

static bool verifyKeyAgainstStored(const uint8_t key[AES_KEY_SIZE]) {
    uint8_t salt[SALT_LEN];
    uint8_t verifyCipher[VERIFY_CIPHER_LEN];
    if (!readSaltAndVerify(salt, verifyCipher)) {
        memset(salt, 0, sizeof(salt));
        memset(verifyCipher, 0, sizeof(verifyCipher));
        return false;
    }

    uint8_t iv[IV_SIZE] = {0};
    uint8_t plain[VERIFY_CIPHER_LEN];
    size_t plainLen = 0;

    bool ok = aesDecrypt(key, iv, verifyCipher, VERIFY_CIPHER_LEN, plain, plainLen);
    bool match = ok && plainLen == sizeof(VERIFY_PLAINTEXT) &&
                 safeCompare(plain, VERIFY_PLAINTEXT, sizeof(VERIFY_PLAINTEXT));

    memset(salt, 0, sizeof(salt));
    memset(verifyCipher, 0, sizeof(verifyCipher));
    memset(plain, 0, sizeof(plain));
    return match;
}

} // namespace

void authInit() {
    authLock();
    failedAttempts = 0;
    lockoutLevel = 0;
    lockoutUntilMs = 0;
}

bool isPinConfigured() {
    Preferences prefs;
    prefs.begin(NS, true);
    bool hasSalt = prefs.getBytesLength("salt") == SALT_LEN;
    bool hasVerify = prefs.getBytesLength("verify") == VERIFY_CIPHER_LEN;
    prefs.end();
    return hasSalt && hasVerify;
}

UnlockResult authUnlock(const String &pin) {
    UnlockResult res;

    uint32_t remaining = 0;
    const uint32_t now = millis();
    if (isLockoutActive(now, remaining)) {
        res.locked_out = true;
        res.lockout_ms_remaining = remaining;
        res.error_code = "locked_out";
        return res;
    }

    if (!validatePinFormat(pin)) {
        res.error_code = "bad_pin";
        return res;
    }

    if (!isPinConfigured()) {
        uint8_t salt[SALT_LEN];
        randomBytes(salt, sizeof(salt));

        uint8_t key[AES_KEY_SIZE];
        if (!deriveKeyPBKDF2(salt, sizeof(salt), pin, key)) {
            memset(salt, 0, sizeof(salt));
            res.error_code = "kdf_failed";
            return res;
        }

        uint8_t iv[IV_SIZE] = {0};
        uint8_t verifyCipher[VERIFY_CIPHER_LEN];
        size_t verifyCipherLen = 0;
        if (!aesEncrypt(key, iv, VERIFY_PLAINTEXT, sizeof(VERIFY_PLAINTEXT), verifyCipher, verifyCipherLen) ||
            verifyCipherLen != VERIFY_CIPHER_LEN) {
            memset(salt, 0, sizeof(salt));
            memset(key, 0, sizeof(key));
            memset(verifyCipher, 0, sizeof(verifyCipher));
            res.error_code = "verify_encrypt_failed";
            return res;
        }

        if (!writeSaltAndVerify(salt, verifyCipher)) {
            memset(salt, 0, sizeof(salt));
            memset(key, 0, sizeof(key));
            memset(verifyCipher, 0, sizeof(verifyCipher));
            res.error_code = "nvs_write_failed";
            return res;
        }

        memset(salt, 0, sizeof(salt));
        memset(verifyCipher, 0, sizeof(verifyCipher));

        memcpy(encKey, key, sizeof(encKey));
        memset(key, 0, sizeof(key));

        authenticated = true;
        randomBytes(sessionToken, sizeof(sessionToken));
        lastActivityMs = millis();
        failedAttempts = 0;
        lockoutLevel = 0;
        lockoutUntilMs = 0;

        res.ok = true;
        res.token = bytesToHex(sessionToken, sizeof(sessionToken));
        return res;
    }

    uint8_t salt[SALT_LEN];
    uint8_t verifyCipher[VERIFY_CIPHER_LEN];
    if (!readSaltAndVerify(salt, verifyCipher)) {
        memset(salt, 0, sizeof(salt));
        memset(verifyCipher, 0, sizeof(verifyCipher));
        res.error_code = "not_initialized";
        return res;
    }

    uint8_t key[AES_KEY_SIZE];
    if (!deriveKeyPBKDF2(salt, sizeof(salt), pin, key)) {
        memset(salt, 0, sizeof(salt));
        memset(verifyCipher, 0, sizeof(verifyCipher));
        res.error_code = "kdf_failed";
        return res;
    }

    memset(salt, 0, sizeof(salt));
    memset(verifyCipher, 0, sizeof(verifyCipher));

    if (!verifyKeyAgainstStored(key)) {
        memset(key, 0, sizeof(key));
        failedAttempts++;

        if (failedAttempts >= MAX_FAILED_ATTEMPTS) {
            failedAttempts = 0;
            if (lockoutLevel < 16) lockoutLevel++;
            const uint32_t lockMs = computeLockoutMs(lockoutLevel);
            lockoutUntilMs = millis() + lockMs;
            res.locked_out = true;
            res.lockout_ms_remaining = lockMs;
            res.error_code = "locked_out";
            return res;
        }

        res.wrong_pin = true;
        res.failed_attempts = failedAttempts;
        res.max_attempts = MAX_FAILED_ATTEMPTS;
        res.error_code = "wrong_pin";
        return res;
    }

    memcpy(encKey, key, sizeof(encKey));
    memset(key, 0, sizeof(key));

    authenticated = true;
    randomBytes(sessionToken, sizeof(sessionToken));
    lastActivityMs = millis();
    failedAttempts = 0;
    lockoutLevel = 0;
    lockoutUntilMs = 0;

    res.ok = true;
    res.token = bytesToHex(sessionToken, sizeof(sessionToken));
    return res;
}

ChangePinResult authChangePin(const String &oldPin, const String &newPin) {
    ChangePinResult res;

    if (!isAuthenticated()) {
        res.error_code = "locked";
        return res;
    }

    if (!validatePinFormat(oldPin) || !validatePinFormat(newPin)) {
        res.error_code = "bad_pin";
        return res;
    }

    if (oldPin == newPin) {
        res.error_code = "same_pin";
        return res;
    }

    uint8_t salt[SALT_LEN];
    Preferences prefs;
    prefs.begin(NS, true);
    size_t saltRead = prefs.getBytes("salt", salt, sizeof(salt));
    prefs.end();
    if (saltRead != sizeof(salt)) {
        memset(salt, 0, sizeof(salt));
        res.error_code = "not_initialized";
        return res;
    }

    uint8_t candidateOld[AES_KEY_SIZE];
    if (!deriveKeyPBKDF2(salt, sizeof(salt), oldPin, candidateOld)) {
        memset(salt, 0, sizeof(salt));
        res.error_code = "kdf_failed";
        return res;
    }

    if (!safeCompare(candidateOld, encKey, sizeof(encKey))) {
        memset(salt, 0, sizeof(salt));
        memset(candidateOld, 0, sizeof(candidateOld));
        res.error_code = "wrong_pin";
        return res;
    }

    uint8_t newKey[AES_KEY_SIZE];
    if (!deriveKeyPBKDF2(salt, sizeof(salt), newPin, newKey)) {
        memset(salt, 0, sizeof(salt));
        memset(candidateOld, 0, sizeof(candidateOld));
        res.error_code = "kdf_failed";
        return res;
    }

    memset(salt, 0, sizeof(salt));
    memset(candidateOld, 0, sizeof(candidateOld));

    const uint8_t *oldKey = encKey;
    const int count = getCredentialCount();

    credential_entry_t *plainCreds = nullptr;
    if (count > 0) {
        plainCreds = (credential_entry_t *)calloc((size_t)count, sizeof(credential_entry_t));
        if (!plainCreds) {
            memset(newKey, 0, sizeof(newKey));
            res.error_code = "oom";
            return res;
        }

        for (int i = 0; i < count; i++) {
            if (!getCredential(i, plainCreds[i], oldKey)) {
                for (int j = 0; j < count; j++) memset(&plainCreds[j], 0, sizeof(plainCreds[j]));
                free(plainCreds);
                memset(newKey, 0, sizeof(newKey));
                res.error_code = "decrypt_failed";
                return res;
            }
        }

        for (int i = 0; i < count; i++) {
            if (!updateCredential(i, plainCreds[i], newKey)) {
                // Best-effort rollback: re-encrypt already migrated entries back to old key.
                for (int j = 0; j < i; j++) {
                    updateCredential(j, plainCreds[j], oldKey);
                }

                for (int j = 0; j < count; j++) memset(&plainCreds[j], 0, sizeof(plainCreds[j]));
                free(plainCreds);
                memset(newKey, 0, sizeof(newKey));
                res.error_code = "reencrypt_failed";
                return res;
            }
            delay(0);
        }
    }

    if (!writeVerifyForKey(newKey)) {
        // Best-effort rollback to the old key if we can.
        if (plainCreds) {
            for (int i = 0; i < count; i++) {
                updateCredential(i, plainCreds[i], oldKey);
                delay(0);
            }
        }
        if (plainCreds) {
            for (int j = 0; j < count; j++) memset(&plainCreds[j], 0, sizeof(plainCreds[j]));
            free(plainCreds);
        }
        memset(newKey, 0, sizeof(newKey));
        res.error_code = "verify_write_failed";
        return res;
    }

    if (plainCreds) {
        for (int j = 0; j < count; j++) memset(&plainCreds[j], 0, sizeof(plainCreds[j]));
        free(plainCreds);
    }

    memcpy(encKey, newKey, sizeof(encKey));
    memset(newKey, 0, sizeof(newKey));

    randomBytes(sessionToken, sizeof(sessionToken));
    lastActivityMs = millis();

    res.ok = true;
    return res;
}

void authLock() {
    authenticated = false;
    lastActivityMs = 0;
    memset(encKey, 0, sizeof(encKey));
    memset(sessionToken, 0, sizeof(sessionToken));
}

void authRecordActivity() {
    if (!authenticated) return;
    lastActivityMs = millis();
}

void authLoop() {
    if (!authenticated) return;
    uint32_t now = millis();
    if (lastActivityMs != 0 && (uint32_t)(now - lastActivityMs) > AUTO_LOCK_MS) {
        authLock();
    }
}

const uint8_t *getEncryptionKey() {
    return authenticated ? encKey : nullptr;
}

bool isAuthenticated() {
    return authenticated;
}

