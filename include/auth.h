#ifndef AUTH_H
#define AUTH_H

#include <Arduino.h>

// Check whether a PIN has been configured on this device
bool isPinConfigured();

// Hash and store a new 4-digit PIN. Returns true on success.
bool setupPin(const String &pin);

// Verify an entered PIN against the stored hash. Returns true if correct.
bool verifyPin(const String &pin);

// Blocking read of a 4-digit PIN from Serial. Echoes '*' for each digit.
String readPinFromSerial();

#endif
