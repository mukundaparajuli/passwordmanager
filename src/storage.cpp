#include "storage.h"

#include <ArduinoJson.h>
#include <Preferences.h>

static const char *NS = "vault";
static const char *META_KEY = "meta";

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

static void resetMeta(Preferences &prefs) {
    StaticJsonDocument<128> doc;
    doc["count"] = 0;
    doc.createNestedArray("entries");
    String out;
    serializeJson(doc, out);
    prefs.putString(META_KEY, out);
}

static bool loadMeta(Preferences &prefs, DynamicJsonDocument &doc) {
    String meta = prefs.getString(META_KEY, "");
    if (meta.length() == 0) {
        resetMeta(prefs);
        meta = prefs.getString(META_KEY, "");
    }

    DeserializationError err = deserializeJson(doc, meta);
    if (err) {
        resetMeta(prefs);
        doc.clear();
        meta = prefs.getString(META_KEY, "");
        err = deserializeJson(doc, meta);
        if (err) return false;
    }

    if (!doc["entries"].is<JsonArray>()) {
        doc.remove("entries");
        doc.createNestedArray("entries");
    }
    if (!doc["count"].is<int>()) {
        doc["count"] = doc["entries"].as<JsonArray>().size();
    }
    return true;
}

static bool saveMeta(Preferences &prefs, const DynamicJsonDocument &doc) {
    String out;
    serializeJson(doc, out);
    return prefs.putString(META_KEY, out) > 0;
}

static bool getMetaEntry(const DynamicJsonDocument &doc, int index, String &outIvHex, int &outLen, bool &outHasTotp) {
    JsonArrayConst entries = doc["entries"].as<JsonArrayConst>();
    if (entries.isNull()) return false;
    if (index < 0 || index >= (int)entries.size()) return false;
    JsonObjectConst e = entries[index].as<JsonObjectConst>();
    if (e.isNull()) return false;

    const char *iv = e["iv"] | "";
    outIvHex = iv;
    outLen = e["len"] | 0;
    outHasTotp = e["has_totp"] | false;
    return true;
}

void storageInit() {
    Preferences prefs;
    prefs.begin(NS, false);
    if (!prefs.isKey(META_KEY)) {
        resetMeta(prefs);
    } else {
        DynamicJsonDocument doc(4096);
        if (!loadMeta(prefs, doc)) {
            resetMeta(prefs);
        }
    }
    prefs.end();
}

int getCredentialCount() {
    Preferences prefs;
    prefs.begin(NS, true);
    String meta = prefs.getString(META_KEY, "");
    prefs.end();

    if (meta.length() == 0) return 0;
    DynamicJsonDocument doc(4096);
    if (deserializeJson(doc, meta)) return 0;
    return doc["count"] | 0;
}

bool storeCredential(const credential_entry_t &cred, const uint8_t *encKey, int &outId) {
    if (!encKey) return false;

    Preferences prefs;
    prefs.begin(NS, false);

    DynamicJsonDocument doc(4096);
    if (!loadMeta(prefs, doc)) {
        prefs.end();
        return false;
    }

    int count = doc["count"] | 0;
    JsonArray entries = doc["entries"].as<JsonArray>();
    if (!entries.isNull() && count != (int)entries.size()) {
        count = (int)entries.size();
        doc["count"] = count;
    }

    uint8_t iv[IV_SIZE];
    generateIV(iv);

    const uint8_t *plain = (const uint8_t *)&cred;
    const size_t plainLen = sizeof(credential_entry_t);

    const size_t maxCipherLen = plainLen + AES_BLOCK_SIZE;
    uint8_t *cipher = (uint8_t *)malloc(maxCipherLen);
    if (!cipher) {
        prefs.end();
        return false;
    }

    size_t cipherLen = 0;
    bool ok = aesEncrypt(encKey, iv, plain, plainLen, cipher, cipherLen);
    if (!ok) {
        free(cipher);
        prefs.end();
        return false;
    }

    const String keyName = "svc_" + String(count);
    prefs.putBytes(keyName.c_str(), cipher, cipherLen);
    free(cipher);

    JsonObject e = entries.createNestedObject();
    e["iv"] = bytesToHex(iv, sizeof(iv));
    e["len"] = (int)cipherLen;
    e["has_totp"] = (cred.flags & CRED_FLAG_HAS_TOTP) != 0;

    doc["count"] = count + 1;
    bool saved = saveMeta(prefs, doc);
    prefs.end();

    if (!saved) return false;
    outId = count;
    return true;
}

bool getCredential(int index, credential_entry_t &cred, const uint8_t *encKey) {
    if (!encKey) return false;

    Preferences prefs;
    prefs.begin(NS, true);

    DynamicJsonDocument doc(4096);
    if (!loadMeta(prefs, doc)) {
        prefs.end();
        return false;
    }

    int count = doc["count"] | 0;
    if (index < 0 || index >= count) {
        prefs.end();
        return false;
    }

    String ivHex;
    int expectedLen = 0;
    bool hasTotp = false;
    if (!getMetaEntry(doc, index, ivHex, expectedLen, hasTotp)) {
        prefs.end();
        return false;
    }

    uint8_t iv[IV_SIZE];
    if (!hexToBytes(ivHex, iv, sizeof(iv))) {
        prefs.end();
        return false;
    }

    const String keyName = "svc_" + String(index);
    size_t cipherLen = prefs.getBytesLength(keyName.c_str());
    if (cipherLen == 0 || (expectedLen > 0 && cipherLen != (size_t)expectedLen)) {
        prefs.end();
        return false;
    }

    uint8_t *cipher = (uint8_t *)malloc(cipherLen);
    if (!cipher) {
        prefs.end();
        return false;
    }
    prefs.getBytes(keyName.c_str(), cipher, cipherLen);
    prefs.end();

    uint8_t *plain = (uint8_t *)malloc(cipherLen);
    if (!plain) {
        free(cipher);
        return false;
    }

    size_t plainLen = 0;
    bool ok = aesDecrypt(encKey, iv, cipher, cipherLen, plain, plainLen);
    memset(cipher, 0, cipherLen);
    free(cipher);

    if (!ok || plainLen != sizeof(credential_entry_t)) {
        memset(plain, 0, cipherLen);
        free(plain);
        return false;
    }

    memcpy(&cred, plain, sizeof(credential_entry_t));
    memset(plain, 0, cipherLen);
    free(plain);

    // Ensure flags reflect meta (best effort)
    if (hasTotp) cred.flags |= CRED_FLAG_HAS_TOTP;
    else cred.flags &= (uint8_t)~CRED_FLAG_HAS_TOTP;

    return true;
}

bool updateCredential(int index, const credential_entry_t &cred, const uint8_t *encKey) {
    if (!encKey) return false;

    Preferences prefs;
    prefs.begin(NS, false);

    DynamicJsonDocument doc(4096);
    if (!loadMeta(prefs, doc)) {
        prefs.end();
        return false;
    }

    int count = doc["count"] | 0;
    if (index < 0 || index >= count) {
        prefs.end();
        return false;
    }

    String ivHex;
    int expectedLen = 0;
    bool hasTotp = false;
    if (!getMetaEntry(doc, index, ivHex, expectedLen, hasTotp)) {
        prefs.end();
        return false;
    }

    uint8_t iv[IV_SIZE];
    generateIV(iv);

    const uint8_t *plain = (const uint8_t *)&cred;
    const size_t plainLen = sizeof(credential_entry_t);

    const size_t maxCipherLen = plainLen + AES_BLOCK_SIZE;
    uint8_t *cipher = (uint8_t *)malloc(maxCipherLen);
    if (!cipher) {
        prefs.end();
        return false;
    }

    size_t cipherLen = 0;
    bool ok = aesEncrypt(encKey, iv, plain, plainLen, cipher, cipherLen);
    if (!ok) {
        free(cipher);
        prefs.end();
        return false;
    }

    const String keyName = "svc_" + String(index);
    prefs.putBytes(keyName.c_str(), cipher, cipherLen);
    free(cipher);

    JsonArray entries = doc["entries"].as<JsonArray>();
    JsonObject e = entries[index].as<JsonObject>();
    e["iv"] = bytesToHex(iv, sizeof(iv));
    e["len"] = (int)cipherLen;
    e["has_totp"] = (cred.flags & CRED_FLAG_HAS_TOTP) != 0;

    bool saved = saveMeta(prefs, doc);
    prefs.end();
    return saved;
}

bool deleteCredential(int index, const uint8_t *encKey) {
    if (!encKey) return false;
    Preferences prefs;
    prefs.begin(NS, false);

    DynamicJsonDocument doc(4096);
    if (!loadMeta(prefs, doc)) {
        prefs.end();
        return false;
    }

    int count = doc["count"] | 0;
    if (index < 0 || index >= count) {
        prefs.end();
        return false;
    }

    // Shift svc blobs down
    for (int i = index; i < count - 1; i++) {
        const String srcKey = "svc_" + String(i + 1);
        const String dstKey = "svc_" + String(i);

        size_t dataLen = prefs.getBytesLength(srcKey.c_str());
        if (dataLen == 0) {
            prefs.remove(dstKey.c_str());
            continue;
        }

        uint8_t *buf = (uint8_t *)malloc(dataLen);
        if (!buf) {
            prefs.end();
            return false;
        }
        prefs.getBytes(srcKey.c_str(), buf, dataLen);
        prefs.putBytes(dstKey.c_str(), buf, dataLen);
        memset(buf, 0, dataLen);
        free(buf);
    }

    const String lastKey = "svc_" + String(count - 1);
    prefs.remove(lastKey.c_str());

    // Update meta
    JsonArray entries = doc["entries"].as<JsonArray>();
    entries.remove(index);
    doc["count"] = count - 1;

    bool saved = saveMeta(prefs, doc);
    prefs.end();
    return saved;
}
