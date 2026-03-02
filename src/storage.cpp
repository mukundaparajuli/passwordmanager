#include "storage.h"

#include <Preferences.h>

static Preferences prefs;

void storageInit() {
  // Nothing to initialize — Preferences opens/closes per operation
}

int getCredentialCount() {
  prefs.begin("credentials", true);
  int count = prefs.getInt("count", 0);
  prefs.end();
  return count;
}

String getCredentialByIndex(int index) {
  prefs.begin("credentials", true);
  String key = "cred" + String(index);
  String value = prefs.getString(key.c_str(), "");
  prefs.end();
  return value;
}

bool parseCredential(const String &raw, String &service, String &username, String &password) {
  int p1 = raw.indexOf('|');
  int p2 = raw.indexOf('|', p1 + 1);

  if (p1 == -1 || p2 == -1)
    return false;

  service = raw.substring(0, p1);
  username = raw.substring(p1 + 1, p2);
  password = raw.substring(p2 + 1);
  return true;
}

void storeCredential(const String &service, const String &username, const String &password) {
  prefs.begin("credentials", false);

  int count = prefs.getInt("count", 0);

  String key = "cred" + String(count);
  String value = service + "|" + username + "|" + password;

  prefs.putString(key.c_str(), value.c_str());
  prefs.putInt("count", count + 1);

  prefs.end();

  Serial.println("Credential Stored: " + value);
}
