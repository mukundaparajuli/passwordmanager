#include "web.h"

#include <Arduino.h>
#include <new>
#include <string.h>
#include <WiFi.h>

#include <LittleFS.h>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include <ArduinoJson.h>
#include <AsyncJson.h>

#include "auth.h"
#include "config.h"
#include "crypto.h"
#include "device_state.h"
#include "storage.h"

static AsyncWebServer server(80);

static const char *methodToString(int method) {
    if (method == HTTP_GET) return "GET";
    if (method == HTTP_POST) return "POST";
    if (method == HTTP_DELETE) return "DELETE";
    if (method == HTTP_PUT) return "PUT";
    if (method == HTTP_PATCH) return "PATCH";
    if (method == HTTP_OPTIONS) return "OPTIONS";
    return "OTHER";
}

static void logRequest(AsyncWebServerRequest *request) {
    const String ip = request->client() ? request->client()->remoteIP().toString() : String("?");
    Serial.printf("[WEB] %s %s from %s (len=%u, heap=%u, sta=%u)\n",
                  methodToString((int)request->method()),
                  request->url().c_str(),
                  ip.c_str(),
                  (unsigned)request->contentLength(),
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)WiFi.softAPgetStationNum());
}

static AsyncJsonResponse *newJsonResponse(AsyncWebServerRequest *request, int status) {
    AsyncJsonResponse *res = new (std::nothrow) AsyncJsonResponse();
    if (!res) {
        request->send(500, "text/plain", "Out of memory");
        return nullptr;
    }
    res->setCode(status);
    return res;
}

static void sendJsonError(AsyncWebServerRequest *request, int status, const String &error, uint32_t lockoutMs = 0) {
    AsyncJsonResponse *res = newJsonResponse(request, status);
    if (!res) return;
    JsonObject root = res->getRoot().to<JsonObject>();
    root["error"] = error;
    if (lockoutMs > 0) root["lockout_ms"] = lockoutMs;
    res->setLength();
    request->send(res);
}

static bool requireAuth(AsyncWebServerRequest *request) {
    if (!isAuthenticated()) {
        sendJsonError(request, 401, "Locked");
        return false;
    }

    const String token = request->header("X-Session-Token");
    if (token.length() == 0 || !authVerifySessionToken(token)) {
        sendJsonError(request, 401, "Invalid session token");
        return false;
    }

    authRecordActivity();
    return true;
}

static bool parseIdFromUrl(const String &url, const char *prefix, int &outId) {
    if (!url.startsWith(prefix)) return false;
    String rest = url.substring(strlen(prefix));
    if (rest.length() == 0) return false;
    for (size_t i = 0; i < rest.length(); i++) {
        if (!isDigit(rest[i])) return false;
    }
    outId = rest.toInt();
    return true;
}

static void copyTrunc(char *dst, size_t dstSize, const String &src) {
    if (dstSize == 0) return;
    size_t n = src.length();
    if (n >= dstSize) n = dstSize - 1;
    memcpy(dst, src.c_str(), n);
    dst[n] = '\0';
}

static bool buildCredentialFromJson(JsonObject obj, credential_entry_t &out, String &err) {
    String service = obj["service"] | "";
    String url = obj["url"] | "";
    String username = obj["username"] | "";
    String password = obj["password"] | "";
    String totpSecret = obj["totp_secret"] | "";

    service.trim();
    url.trim();
    username.trim();
    totpSecret.trim();

    if (service.length() == 0) {
        err = "service is required";
        return false;
    }
    if (password.length() == 0) {
        err = "password is required";
        return false;
    }

    memset(&out, 0, sizeof(out));
    copyTrunc(out.service, sizeof(out.service), service);
    copyTrunc(out.url, sizeof(out.url), url);
    copyTrunc(out.username, sizeof(out.username), username);
    copyTrunc(out.password, sizeof(out.password), password);
    copyTrunc(out.totp_secret, sizeof(out.totp_secret), totpSecret);

    out.flags = 0;
    if (strlen(out.totp_secret) > 0) out.flags |= CRED_FLAG_HAS_TOTP;
    return true;
}

void webInit() {
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type, X-Session-Token");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");

    if (!LittleFS.begin(true)) {
        Serial.println("[WEB] LittleFS mount failed");
    }

    WiFi.mode(WIFI_AP);
    const char *pwd = WIFI_AP_PASSWORD;
    if (pwd && strlen(pwd) >= 8) {
        WiFi.softAP(WIFI_AP_SSID, pwd);
    } else {
        WiFi.softAP(WIFI_AP_SSID);
    }

    Serial.print("[WEB] AP SSID: ");
    Serial.println(WIFI_AP_SSID);
    Serial.print("[WEB] AP IP:   ");
    Serial.println(WiFi.softAPIP());

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        logRequest(request);
        request->send(LittleFS, "/index.html", "text/html");
    });
    server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        logRequest(request);
        request->send(LittleFS, "/index.html", "text/html");
    });

    server.on("/api/ping", HTTP_GET, [](AsyncWebServerRequest *request) {
        logRequest(request);
        AsyncJsonResponse *res = newJsonResponse(request, 200);
        if (!res) return;
        JsonObject root = res->getRoot().to<JsonObject>();
        root["ok"] = true;
        root["authenticated"] = isAuthenticated();
        root["uptime_ms"] = (uint32_t)millis();
        root["free_heap"] = (uint32_t)ESP.getFreeHeap();
        root["sta"] = (uint32_t)WiFi.softAPgetStationNum();
        res->setLength();
        request->send(res);
    });

    server.on("/api/lock", HTTP_POST, [](AsyncWebServerRequest *request) {
        logRequest(request);
        if (!requireAuth(request)) return;
        authLock();
        AsyncJsonResponse *res = newJsonResponse(request, 200);
        if (!res) return;
        JsonObject root = res->getRoot().to<JsonObject>();
        root["ok"] = true;
        res->setLength();
        request->send(res);
    });

    auto *unlockHandler = new AsyncCallbackJsonWebHandler("/api/unlock", [](AsyncWebServerRequest *request, JsonVariant &json) {
        logRequest(request);
        JsonObject obj = json.as<JsonObject>();
        String pin = obj["pin"] | "";
        String pinConfirm = obj["pin_confirm"] | "";

        const uint32_t t0 = millis();
        UnlockResult r = authUnlock(pin, pinConfirm);
        Serial.printf("[WEB] /api/unlock took %ums (heap=%u)\n", (unsigned)(millis() - t0), (unsigned)ESP.getFreeHeap());
        if (!r.ok) {
            if (r.locked_out) {
                sendJsonError(request, 429, r.error.length() ? r.error : "Locked out", r.lockout_ms_remaining);
                return;
            }
            if (r.needs_pin_confirm) {
                sendJsonError(request, 400, r.error.length() ? r.error : "pin_confirm required");
                return;
            }
            sendJsonError(request, 403, r.error.length() ? r.error : "Unlock failed");
            return;
        }

        AsyncJsonResponse *res = newJsonResponse(request, 200);
        if (!res) return;
        JsonObject root = res->getRoot().to<JsonObject>();
        root["token"] = r.token;
        root["configured"] = isPinConfigured();
        res->setLength();
        request->send(res);
    });
    unlockHandler->setMethod(HTTP_POST);
    server.addHandler(unlockHandler);

    server.on("/api/credentials", HTTP_GET, [](AsyncWebServerRequest *request) {
        logRequest(request);
        if (!requireAuth(request)) return;
        const uint8_t *key = getEncryptionKey();
        if (!key) {
            sendJsonError(request, 401, "Locked");
            return;
        }

        const int count = getCredentialCount();

        AsyncJsonResponse *res = newJsonResponse(request, 200);
        if (!res) return;
        JsonObject root = res->getRoot().to<JsonObject>();
        JsonArray arr = root.createNestedArray("credentials");

        for (int i = 0; i < count; i++) {
            credential_entry_t entry;
            if (!getCredential(i, entry, key)) continue;
            JsonObject c = arr.createNestedObject();
            c["id"] = i;
            c["service"] = entry.service;
            c["url"] = entry.url;
            c["has_totp"] = (entry.flags & CRED_FLAG_HAS_TOTP) != 0;
            memset(&entry, 0, sizeof(entry));
            delay(0);
        }

        root["count"] = (int)arr.size();
        res->setLength();
        request->send(res);
    });

    auto *addHandler = new AsyncCallbackJsonWebHandler("/api/credentials", [](AsyncWebServerRequest *request, JsonVariant &json) {
        logRequest(request);
        if (!requireAuth(request)) return;
        const uint8_t *key = getEncryptionKey();
        if (!key) {
            sendJsonError(request, 401, "Locked");
            return;
        }

        JsonObject obj = json.as<JsonObject>();
        credential_entry_t entry;
        String err;
        if (!buildCredentialFromJson(obj, entry, err)) {
            sendJsonError(request, 400, err);
            return;
        }

        int id = -1;
        if (!storeCredential(entry, key, id)) {
            sendJsonError(request, 500, "Failed to store credential");
            return;
        }

        deviceSetSelectedIndex(id);
        deviceSetShowingDetail(false);

        AsyncJsonResponse *res = newJsonResponse(request, 200);
        if (!res) return;
        JsonObject root = res->getRoot().to<JsonObject>();
        root["id"] = id;
        res->setLength();
        request->send(res);
    });
    addHandler->setMethod(HTTP_POST);
    server.addHandler(addHandler);

    auto *importHandler = new AsyncCallbackJsonWebHandler("/api/credentials/import", [](AsyncWebServerRequest *request, JsonVariant &json) {
        logRequest(request);
        if (!requireAuth(request)) return;
        const uint8_t *key = getEncryptionKey();
        if (!key) {
            sendJsonError(request, 401, "Locked");
            return;
        }

        JsonArray creds;
        if (json.is<JsonArray>()) {
            creds = json.as<JsonArray>();
        } else if (json.is<JsonObject>() && json.as<JsonObject>()["credentials"].is<JsonArray>()) {
            creds = json.as<JsonObject>()["credentials"].as<JsonArray>();
        } else {
            sendJsonError(request, 400, "Expected an array (or {credentials:[...]})");
            return;
        }

        int imported = 0;
        int skipped = 0;
        for (JsonVariant v : creds) {
            if (!v.is<JsonObject>()) {
                skipped++;
                continue;
            }
            credential_entry_t entry;
            String err;
            if (!buildCredentialFromJson(v.as<JsonObject>(), entry, err)) {
                skipped++;
                continue;
            }
            int id = -1;
            if (!storeCredential(entry, key, id)) {
                skipped++;
                continue;
            }
            imported++;
            delay(0);
        }

        AsyncJsonResponse *res = newJsonResponse(request, 200);
        if (!res) return;
        JsonObject root = res->getRoot().to<JsonObject>();
        root["imported"] = imported;
        root["skipped"] = skipped;
        res->setLength();
        request->send(res);
    });
    importHandler->setMethod(HTTP_POST);
    server.addHandler(importHandler);

    server.onNotFound([](AsyncWebServerRequest *request) {
        logRequest(request);
        // CORS preflight
        if (request->method() == HTTP_OPTIONS) {
            request->send(204);
            return;
        }

        const String url = request->url();

        // DELETE /api/credentials/:id
        if (request->method() == HTTP_DELETE) {
            int id = -1;
            if (parseIdFromUrl(url, "/api/credentials/", id)) {
                if (!requireAuth(request)) return;
                const uint8_t *key = getEncryptionKey();
                if (!key) {
                    sendJsonError(request, 401, "Locked");
                    return;
                }
                if (!deleteCredential(id, key)) {
                    sendJsonError(request, 404, "Not found");
                    return;
                }
                deviceClampSelectedIndex(getCredentialCount());
                AsyncJsonResponse *res = newJsonResponse(request, 200);
                if (!res) return;
                JsonObject root = res->getRoot().to<JsonObject>();
                root["ok"] = true;
                res->setLength();
                request->send(res);
                return;
            }
        }

        // GET /api/totp/:id
        if (request->method() == HTTP_GET) {
            int id = -1;
            if (parseIdFromUrl(url, "/api/totp/", id)) {
                if (!requireAuth(request)) return;
                const uint8_t *key = getEncryptionKey();
                if (!key) {
                    sendJsonError(request, 401, "Locked");
                    return;
                }
                credential_entry_t entry;
                if (!getCredential(id, entry, key)) {
                    sendJsonError(request, 404, "Not found");
                    return;
                }
                if (strlen(entry.totp_secret) == 0) {
                    memset(&entry, 0, sizeof(entry));
                    sendJsonError(request, 404, "No TOTP secret for this entry");
                    return;
                }
                const String code = generateTOTP(String(entry.totp_secret));
                memset(&entry, 0, sizeof(entry));

                AsyncJsonResponse *res = newJsonResponse(request, 200);
                if (!res) return;
                JsonObject root = res->getRoot().to<JsonObject>();
                root["code"] = code;
                root["status"] = (code == "NO_TIME" || code == "INVALID") ? "error" : "ok";
                res->setLength();
                request->send(res);
                return;
            }
        }

        // POST /api/select/:id
        if (request->method() == HTTP_POST) {
            int id = -1;
            if (parseIdFromUrl(url, "/api/select/", id)) {
                if (!requireAuth(request)) return;
                const int count = getCredentialCount();
                if (id < 0 || id >= count) {
                    sendJsonError(request, 404, "Not found");
                    return;
                }
                deviceSetSelectedIndex(id);
                deviceSetShowingDetail(false);
                AsyncJsonResponse *res = newJsonResponse(request, 200);
                if (!res) return;
                JsonObject root = res->getRoot().to<JsonObject>();
                root["ok"] = true;
                res->setLength();
                request->send(res);
                return;
            }
        }

        sendJsonError(request, 404, "Not found");
    });

    server.begin();
}
