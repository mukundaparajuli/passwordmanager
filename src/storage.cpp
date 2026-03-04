#include "storage.h"

#include <Preferences.h>

static Preferences prefs;
static const char *NS = "credentials";

static String serializeCredential(const Credential &cred) {
    String s;
    s += cred.serviceName;
    s += FIELD_SEPARATOR;
    s += cred.serviceUrl;
    s += FIELD_SEPARATOR;
    s += cred.identifier;
    s += FIELD_SEPARATOR;
    s += cred.password;
    s += FIELD_SEPARATOR;
    s += cred.totpSecret;
    return s;
}

static bool deserializeCredential(const uint8_t *data, size_t len, Credential &cred) {
    String raw;
    raw.reserve(len);
    for (size_t i = 0; i < len; i++) {
        raw += (char)data[i];
    }

    int p0 = raw.indexOf(FIELD_SEPARATOR);
    if (p0 == -1) return false;
    int p1 = raw.indexOf(FIELD_SEPARATOR, p0 + 1);
    if (p1 == -1) return false;
    int p2 = raw.indexOf(FIELD_SEPARATOR, p1 + 1);
    if (p2 == -1) return false;
    int p3 = raw.indexOf(FIELD_SEPARATOR, p2 + 1);
    if (p3 == -1) return false;

    cred.serviceName = raw.substring(0, p0);
    cred.serviceUrl = raw.substring(p0 + 1, p1);
    cred.identifier = raw.substring(p1 + 1, p2);
    cred.password = raw.substring(p2 + 1, p3);
    cred.totpSecret = raw.substring(p3 + 1);
    return true;
}

void storageInit() {
    // Validate that stored count matches actual data.
    // If IVs are missing the count is stale – reset it.
    prefs.begin(NS, false);
    int count = prefs.getInt("count", 0);

    if (count > 0) {
        bool valid = true;
        for (int i = 0; i < count; i++) {
            String ivKey = "iv" + String(i);
            if (prefs.getBytesLength(ivKey.c_str()) != IV_SIZE) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            Serial.println("[STORAGE] Stale credential count detected – resetting to 0");
            prefs.putInt("count", 0);
        }
    }

    prefs.end();
}

int getCredentialCount() {
    prefs.begin(NS, true);
    int count = prefs.getInt("count", 0);
    prefs.end();
    return count;
}

bool storeCredential(const Credential &cred, const uint8_t *encKey) {
    if (!encKey) return false;

    String serialized = serializeCredential(cred);
    size_t plainLen = serialized.length();

    size_t maxCipherLen = plainLen + AES_BLOCK_SIZE;
    uint8_t *ciphertext = (uint8_t *)malloc(maxCipherLen);
    if (!ciphertext) return false;

    uint8_t iv[IV_SIZE];
    generateIV(iv);

    size_t cipherLen = 0;
    bool ok = aesEncrypt(encKey, iv,
                         (const uint8_t *)serialized.c_str(), plainLen,
                         ciphertext, cipherLen);
    if (!ok) {
        free(ciphertext);
        return false;
    }

    prefs.begin(NS, false);
    int count = prefs.getInt("count", 0);

    String ivKey = "iv" + String(count);
    String dataKey = "data" + String(count);

    prefs.putBytes(ivKey.c_str(), iv, IV_SIZE);
    prefs.putBytes(dataKey.c_str(), ciphertext, cipherLen);
    prefs.putInt("count", count + 1);
    prefs.end();

    free(ciphertext);
    return true;
}

bool getCredential(int index, Credential &cred, const uint8_t *encKey) {
    if (!encKey) return false;

    prefs.begin(NS, true);

    String ivKey = "iv" + String(index);
    String dataKey = "data" + String(index);

    uint8_t iv[IV_SIZE];
    size_t ivRead = prefs.getBytes(ivKey.c_str(), iv, IV_SIZE);
    if (ivRead != IV_SIZE) {
        prefs.end();
        return false;
    }

    size_t cipherLen = prefs.getBytesLength(dataKey.c_str());
    if (cipherLen == 0) {
        prefs.end();
        return false;
    }

    uint8_t *ciphertext = (uint8_t *)malloc(cipherLen);
    if (!ciphertext) {
        prefs.end();
        return false;
    }

    prefs.getBytes(dataKey.c_str(), ciphertext, cipherLen);
    prefs.end();

    uint8_t *plaintext = (uint8_t *)malloc(cipherLen);
    if (!plaintext) {
        free(ciphertext);
        return false;
    }

    size_t plainLen = 0;
    bool ok = aesDecrypt(encKey, iv, ciphertext, cipherLen, plaintext, plainLen);
    free(ciphertext);

    if (!ok) {
        free(plaintext);
        return false;
    }

    ok = deserializeCredential(plaintext, plainLen, cred);
    memset(plaintext, 0, plainLen);
    free(plaintext);

    if (ok) {
        memcpy(cred.iv, iv, IV_SIZE);
    }

    return ok;
}

bool updateCredential(int index, const Credential &cred, const uint8_t *encKey) {
    if (!encKey) return false;

    prefs.begin(NS, true);
    int count = prefs.getInt("count", 0);
    prefs.end();

    if (index < 0 || index >= count) return false;

    String serialized = serializeCredential(cred);
    size_t plainLen = serialized.length();

    size_t maxCipherLen = plainLen + AES_BLOCK_SIZE;
    uint8_t *ciphertext = (uint8_t *)malloc(maxCipherLen);
    if (!ciphertext) return false;

    uint8_t iv[IV_SIZE];
    generateIV(iv);

    size_t cipherLen = 0;
    bool ok = aesEncrypt(encKey, iv,
                         (const uint8_t *)serialized.c_str(), plainLen,
                         ciphertext, cipherLen);
    if (!ok) {
        free(ciphertext);
        return false;
    }

    prefs.begin(NS, false);
    String ivKey = "iv" + String(index);
    String dataKey = "data" + String(index);
    prefs.putBytes(ivKey.c_str(), iv, IV_SIZE);
    prefs.putBytes(dataKey.c_str(), ciphertext, cipherLen);
    prefs.end();

    free(ciphertext);
    return true;
}

bool deleteCredential(int index, const uint8_t *encKey) {
    if (!encKey) return false;

    prefs.begin(NS, false);
    int count = prefs.getInt("count", 0);
    if (index < 0 || index >= count) {
        prefs.end();
        return false;
    }

    for (int i = index; i < count - 1; i++) {
        String srcIv = "iv" + String(i + 1);
        String srcData = "data" + String(i + 1);
        String dstIv = "iv" + String(i);
        String dstData = "data" + String(i);

        uint8_t iv[IV_SIZE];
        prefs.getBytes(srcIv.c_str(), iv, IV_SIZE);

        size_t dataLen = prefs.getBytesLength(srcData.c_str());
        uint8_t *buf = (uint8_t *)malloc(dataLen);
        if (buf) {
            prefs.getBytes(srcData.c_str(), buf, dataLen);
            prefs.putBytes(dstIv.c_str(), iv, IV_SIZE);
            prefs.putBytes(dstData.c_str(), buf, dataLen);
            free(buf);
        }
    }

    String lastIv = "iv" + String(count - 1);
    String lastData = "data" + String(count - 1);
    prefs.remove(lastIv.c_str());
    prefs.remove(lastData.c_str());
    prefs.putInt("count", count - 1);
    prefs.end();

    return true;
}
