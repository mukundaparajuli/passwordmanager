#ifndef AUTH_H
#define AUTH_H

#include <Arduino.h>

bool isPinConfigured();
bool setupPin(const String &pin);
bool verifyPin(const String &pin);
String readPinFromSerial();
const uint8_t *getEncryptionKey();
bool isAuthenticated();

#endif
