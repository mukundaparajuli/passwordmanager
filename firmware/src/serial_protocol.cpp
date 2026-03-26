#include "serial_protocol.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "auth.h"
#include "crypto.h"
#include "device_state.h"
#include "storage.h"

namespace {

constexpr size_t RX_BUF_SIZE = 16384;
char rxBuf[RX_BUF_SIZE];
size_t rxLen = 0;

void writeJsonLine(const JsonDocument &doc) {
    serializeJson(doc, Serial);
    Serial.write('\n');
}

void sendOk() {
    StaticJsonDocument<64> doc;
    doc["status"] = "ok";
    writeJsonLine(doc);
}

void sendOkWithToken(const String &token) {
    StaticJsonDocument<192> doc;
    doc["status"] = "ok";
    doc["token"] = token;
    writeJsonLine(doc);
}

void sendError(const char *message) {
    StaticJsonDocument<128> doc;
    doc["status"] = "error";
    doc["message"] = message;
    writeJsonLine(doc);
}

void sendErrorWithLockout(uint32_t lockoutMs) {
    StaticJsonDocument<192> doc;
    doc["status"] = "error";
    doc["message"] = "locked_out";
    doc["lockout_ms_remaining"] = lockoutMs;
    writeJsonLine(doc);
}

void sendErrorWithAttempts(uint8_t failedAttempts, uint8_t maxAttempts) {
    StaticJsonDocument<192> doc;
    doc["status"] = "error";
    doc["message"] = "wrong_pin";
    doc["failed_attempts"] = failedAttempts;
    doc["max_attempts"] = maxAttempts;
    writeJsonLine(doc);
}

bool copyTrunc(char *dst, size_t dstSize, const char *src) {
    if (!dst || dstSize == 0) return false;
    if (!src) src = "";
    size_t n = strnlen(src, dstSize - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
    return true;
}

bool buildCredentialFromJson(JsonObjectConst obj, credential_entry_t &out) {
    const char *service = obj["service"] | "";
    const char *url = obj["url"] | "";
    const char *username = obj["username"] | "";
    const char *password = obj["password"] | "";
    const char *totpSecret = obj["totp_secret"] | "";

    if (!service || strlen(service) == 0) return false;
    if (!password || strlen(password) == 0) return false;

    memset(&out, 0, sizeof(out));
    copyTrunc(out.service, sizeof(out.service), service);
    copyTrunc(out.url, sizeof(out.url), url);
    copyTrunc(out.username, sizeof(out.username), username);
    copyTrunc(out.password, sizeof(out.password), password);
    copyTrunc(out.totp_secret, sizeof(out.totp_secret), totpSecret);

    out.flags = 0;
    if (strlen(out.totp_secret) > 0) out.flags |= CRED_FLAG_HAS_TOTP;
    // Default HID mode: username + TAB + password
    out.flags |= (uint8_t)((1 << CRED_FLAG_HID_MODE_SHIFT) & CRED_FLAG_HID_MODE_MASK);

    if (obj["hid_mode"].is<int>()) {
        int mode = obj["hid_mode"].as<int>();
        if (mode >= 0 && mode <= 2) {
            out.flags = (out.flags & (uint8_t)~CRED_FLAG_HID_MODE_MASK) |
                        (uint8_t)((mode << CRED_FLAG_HID_MODE_SHIFT) & CRED_FLAG_HID_MODE_MASK);
        }
    } else if (obj["flags"].is<int>()) {
        out.flags = (uint8_t)obj["flags"].as<int>();
    }

    // Ensure has-totp flag always matches stored secret (best effort).
    if (strlen(out.totp_secret) > 0) out.flags |= CRED_FLAG_HAS_TOTP;
    else out.flags &= (uint8_t)~CRED_FLAG_HAS_TOTP;

    return true;
}

bool handlePing() {
    sendOk();
    return true;
}

bool handleUnlock(JsonObjectConst obj) {
    const char *pin = obj["pin"] | "";
    UnlockResult r = authUnlock(String(pin));
    if (!r.ok) {
        if (r.locked_out) {
            sendErrorWithLockout(r.lockout_ms_remaining);
            return true;
        }
        if (r.wrong_pin) {
            sendErrorWithAttempts(r.failed_attempts, r.max_attempts);
            return true;
        }
        sendError(r.error_code.length() ? r.error_code.c_str() : "unlock_failed");
        return true;
    }

    sendOkWithToken(r.token);
    return true;
}

bool handleSyncTime(JsonObjectConst obj) {
    if (!obj["timestamp"].is<int64_t>() && !obj["timestamp"].is<uint32_t>() && !obj["timestamp"].is<int>()) {
        sendError("bad_request");
        return true;
    }

    int64_t ts = 0;
    if (obj["timestamp"].is<int64_t>()) ts = obj["timestamp"].as<int64_t>();
    else ts = (int64_t)obj["timestamp"].as<long>();

    if (ts < 0) {
        sendError("bad_request");
        return true;
    }

    timeval tv;
    tv.tv_sec = (time_t)ts;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);

    sendOk();
    return true;
}

bool handleLock() {
    authLock();
    sendOk();
    return true;
}

bool handleList() {
    const uint8_t *key = getEncryptionKey();
    if (!key) {
        sendError("locked");
        return true;
    }

    const int count = getCredentialCount();

    DynamicJsonDocument doc(16384);
    doc["status"] = "ok";
    JsonArray arr = doc.createNestedArray("credentials");

    for (int i = 0; i < count; i++) {
        credential_entry_t entry;
        if (!getCredential(i, entry, key)) continue;

        JsonObject c = arr.createNestedObject();
        c["id"] = i;
        c["service"] = entry.service;
        c["url"] = entry.url;
        c["username"] = entry.username;

        memset(&entry, 0, sizeof(entry));
        delay(0);
    }

    writeJsonLine(doc);
    return true;
}

bool handleSelect(JsonObjectConst obj) {
    if (!obj["id"].is<int>()) {
        sendError("bad_request");
        return true;
    }
    const int id = obj["id"].as<int>();
    const int count = getCredentialCount();
    if (id < 0 || id >= count) {
        sendError("not_found");
        return true;
    }

    deviceSetSelectedIndex(id);
    deviceSetUiState(DeviceUiState::SELECTED);
    sendOk();
    return true;
}

bool handleGetTotp(JsonObjectConst obj) {
    if (!obj["id"].is<int>()) {
        sendError("bad_request");
        return true;
    }

    const uint8_t *key = getEncryptionKey();
    if (!key) {
        sendError("locked");
        return true;
    }

    const int id = obj["id"].as<int>();
    credential_entry_t entry;
    if (!getCredential(id, entry, key)) {
        sendError("not_found");
        return true;
    }

    if (strlen(entry.totp_secret) == 0) {
        memset(&entry, 0, sizeof(entry));
        sendError("no_totp");
        return true;
    }

    const String code = generateTOTP(String(entry.totp_secret));
    memset(&entry, 0, sizeof(entry));

    if (code == "NO_TIME") {
        sendError("no_time");
        return true;
    }
    if (code == "INVALID") {
        sendError("invalid_secret");
        return true;
    }

    const time_t now = time(nullptr);
    const int expiresIn = (int)(TOTP_PERIOD - (now % TOTP_PERIOD));

    StaticJsonDocument<128> doc;
    doc["status"] = "ok";
    doc["totp"] = code;
    doc["expires_in"] = expiresIn;
    writeJsonLine(doc);
    return true;
}

bool handleAdd(JsonObjectConst obj) {
    const uint8_t *key = getEncryptionKey();
    if (!key) {
        sendError("locked");
        return true;
    }

    if (getCredentialCount() >= MAX_CREDENTIALS) {
        sendError("full");
        return true;
    }

    credential_entry_t entry;
    if (!buildCredentialFromJson(obj, entry)) {
        sendError("bad_request");
        return true;
    }

    int id = -1;
    if (!storeCredential(entry, key, id)) {
        memset(&entry, 0, sizeof(entry));
        sendError("store_failed");
        return true;
    }
    memset(&entry, 0, sizeof(entry));

    deviceSetSelectedIndex(id);
    deviceSetUiState(DeviceUiState::SELECTED);

    StaticJsonDocument<96> doc;
    doc["status"] = "ok";
    doc["id"] = id;
    writeJsonLine(doc);
    return true;
}

bool handleDelete(JsonObjectConst obj) {
    if (!obj["id"].is<int>()) {
        sendError("bad_request");
        return true;
    }

    const uint8_t *key = getEncryptionKey();
    if (!key) {
        sendError("locked");
        return true;
    }

    const int id = obj["id"].as<int>();
    if (!deleteCredential(id, key)) {
        sendError("not_found");
        return true;
    }

    deviceClampSelectedIndex(getCredentialCount());
    deviceSetUiState(DeviceUiState::IDLE);
    sendOk();
    return true;
}

bool handleImport(JsonObjectConst obj) {
    const uint8_t *key = getEncryptionKey();
    if (!key) {
        sendError("locked");
        return true;
    }

    if (!obj["entries"].is<JsonArrayConst>()) {
        sendError("bad_request");
        return true;
    }

    JsonArrayConst entries = obj["entries"].as<JsonArrayConst>();
    int imported = 0;
    int skipped = 0;
    int currentCount = getCredentialCount();

    for (JsonVariantConst v : entries) {
        if (currentCount >= MAX_CREDENTIALS) {
            skipped += (int)(entries.size() - (imported + skipped));
            break;
        }

        if (!v.is<JsonObjectConst>()) {
            skipped++;
            continue;
        }

        credential_entry_t entry;
        if (!buildCredentialFromJson(v.as<JsonObjectConst>(), entry)) {
            skipped++;
            continue;
        }

        int id = -1;
        if (!storeCredential(entry, key, id)) {
            memset(&entry, 0, sizeof(entry));
            skipped++;
            continue;
        }

        memset(&entry, 0, sizeof(entry));
        imported++;
        currentCount++;
        delay(0);
    }

    StaticJsonDocument<128> doc;
    doc["status"] = "ok";
    doc["imported"] = imported;
    doc["skipped"] = skipped;
    writeJsonLine(doc);
    return true;
}

bool handleChangePin(JsonObjectConst obj) {
    const char *oldPin = obj["old_pin"] | "";
    const char *newPin = obj["new_pin"] | "";

    ChangePinResult r = authChangePin(String(oldPin), String(newPin));
    if (!r.ok) {
        if (r.locked_out) {
            sendErrorWithLockout(r.lockout_ms_remaining);
            return true;
        }
        sendError(r.error_code.length() ? r.error_code.c_str() : "change_pin_failed");
        return true;
    }

    sendOk();
    return true;
}

void processLine(const char *line, size_t len) {
    if (len == 0) return;

    size_t capacity = (len * 2) + 512;
    if (capacity < 1024) capacity = 1024;
    if (capacity > 32768) capacity = 32768;
    DynamicJsonDocument doc(capacity);
    DeserializationError err = deserializeJson(doc, line, len);
    if (err) {
        sendError("bad_json");
        return;
    }

    if (!doc.is<JsonObject>()) {
        sendError("bad_json");
        return;
    }

    JsonObjectConst obj = doc.as<JsonObjectConst>();
    const char *cmd = obj["cmd"] | "";
    if (!cmd || strlen(cmd) == 0) {
        sendError("bad_request");
        return;
    }

    if (strcmp(cmd, "ping") == 0) {
        handlePing();
        return;
    }

    if (strcmp(cmd, "unlock") == 0) {
        handleUnlock(obj);
        return;
    }

    // Allow time sync while locked.
    if (strcmp(cmd, "sync_time") == 0) {
        handleSyncTime(obj);
        return;
    }

    if (!isAuthenticated()) {
        sendError("locked");
        return;
    }
    authRecordActivity();

    if (strcmp(cmd, "lock") == 0) {
        handleLock();
        return;
    }
    if (strcmp(cmd, "list") == 0) {
        handleList();
        return;
    }
    if (strcmp(cmd, "select") == 0) {
        handleSelect(obj);
        return;
    }
    if (strcmp(cmd, "get_totp") == 0) {
        handleGetTotp(obj);
        return;
    }
    if (strcmp(cmd, "add") == 0) {
        handleAdd(obj);
        return;
    }
    if (strcmp(cmd, "delete") == 0) {
        handleDelete(obj);
        return;
    }
    if (strcmp(cmd, "import") == 0) {
        handleImport(obj);
        return;
    }
    if (strcmp(cmd, "change_pin") == 0) {
        handleChangePin(obj);
        return;
    }

    sendError("unknown_cmd");
}

} // namespace

void serialProtocolInit() {
    rxLen = 0;
    memset(rxBuf, 0, sizeof(rxBuf));
}

void serialProtocolLoop() {
    while (Serial.available() > 0) {
        const int ch = Serial.read();
        if (ch < 0) break;

        if (ch == '\r') continue;

        if (ch == '\n') {
            rxBuf[rxLen] = '\0';
            processLine(rxBuf, rxLen);
            rxLen = 0;
            continue;
        }

        if (rxLen + 1 >= sizeof(rxBuf)) {
            rxLen = 0;
            sendError("line_too_long");
            continue;
        }

        rxBuf[rxLen++] = (char)ch;
    }
}
