#include "auth.h"

#include <Preferences.h>
#include <esp_random.h>
#include <mbedtls/md.h>

// ================= Constants =================

static const char *AUTH_NAMESPACE = "auth";
static const int SALT_LEN = 16;
static const int HASH_LEN = 32; // SHA-256 output
static const int ROUNDS = 10000;

// ================= Internal helpers =================

// Derive a 32-byte key from salt + pin using iterated SHA-256.
// round 0 : H = SHA256(salt || pin)
// round i : H = SHA256(salt || H_prev || pin)   (for i = 1 … rounds-1)
static void deriveKey(const uint8_t *salt, size_t saltLen,
                      const String &pin, int rounds,
                      uint8_t *out) {
  mbedtls_md_context_t ctx;
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, info, 0);

  // Initial round: H = SHA256(salt || pin)
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, salt, saltLen);
  mbedtls_md_update(&ctx, (const uint8_t *)pin.c_str(), pin.length());
  mbedtls_md_finish(&ctx, out);

  // Subsequent rounds: H = SHA256(salt || H_prev || pin)
  for (int i = 1; i < rounds; i++) {
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, salt, saltLen);
    mbedtls_md_update(&ctx, out, HASH_LEN);
    mbedtls_md_update(&ctx, (const uint8_t *)pin.c_str(), pin.length());
    mbedtls_md_finish(&ctx, out);
  }

  mbedtls_md_free(&ctx);
}

// Constant-time comparison to prevent timing attacks
static bool safeCompare(const uint8_t *a, const uint8_t *b, size_t len) {
  uint8_t diff = 0;
  for (size_t i = 0; i < len; i++) {
    diff |= a[i] ^ b[i];
  }
  return diff == 0;
}

// ================= Public API =================

bool isPinConfigured() {
  Preferences prefs;
  prefs.begin(AUTH_NAMESPACE, true); // read-only
  bool exists = prefs.isKey("hash");
  prefs.end();
  return exists;
}

bool setupPin(const String &pin) {
  if (pin.length() != 4) {
    Serial.println("[AUTH] PIN must be exactly 4 digits.");
    return false;
  }

  // Validate all characters are digits
  for (unsigned int i = 0; i < pin.length(); i++) {
    if (!isDigit(pin[i])) {
      Serial.println("[AUTH] PIN must contain only digits.");
      return false;
    }
  }

  // Generate cryptographic random salt
  uint8_t salt[SALT_LEN];
  for (int i = 0; i < SALT_LEN; i += 4) {
    uint32_t r = esp_random();
    int remaining = SALT_LEN - i;
    int copyLen = (remaining < 4) ? remaining : 4;
    memcpy(salt + i, &r, copyLen);
  }

  // Derive key
  uint8_t hash[HASH_LEN];
  deriveKey(salt, SALT_LEN, pin, ROUNDS, hash);

  // Persist salt, hash, rounds
  Preferences prefs;
  prefs.begin(AUTH_NAMESPACE, false); // read-write
  prefs.putBytes("salt", salt, SALT_LEN);
  prefs.putBytes("hash", hash, HASH_LEN);
  prefs.putInt("rounds", ROUNDS);
  prefs.end();

  Serial.println("[AUTH] PIN configured successfully.");
  return true;
}

bool verifyPin(const String &pin) {
  if (pin.length() != 4)
    return false;

  Preferences prefs;
  prefs.begin(AUTH_NAMESPACE, true);

  uint8_t storedSalt[SALT_LEN];
  uint8_t storedHash[HASH_LEN];
  int rounds = prefs.getInt("rounds", ROUNDS);

  size_t saltRead = prefs.getBytes("salt", storedSalt, SALT_LEN);
  size_t hashRead = prefs.getBytes("hash", storedHash, HASH_LEN);

  prefs.end();

  if (saltRead != SALT_LEN || hashRead != HASH_LEN)
    return false;

  // Re-derive key with entered PIN
  uint8_t derived[HASH_LEN];
  deriveKey(storedSalt, SALT_LEN, pin, rounds, derived);

  return safeCompare(derived, storedHash, HASH_LEN);
}

String readPinFromSerial() {
  String pin = "";

  Serial.print("Enter 4-digit PIN: ");

  while (pin.length() < 4) {
    if (Serial.available()) {
      char c = Serial.read();

      if (isDigit(c)) {
        pin += c;
        Serial.print('*'); // mask the digit
      }
    }
    delay(10); // small yield to avoid busy-wait
  }

  Serial.println(); // newline after masked input
  return pin;
}
