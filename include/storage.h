#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>

// Initialize the storage subsystem
void storageInit();

// Get the number of stored credentials
int getCredentialCount();

// Get a raw credential string by index (format: "service|username|password")
String getCredentialByIndex(int index);

// Parse a credential string into its components. Returns true on success.
bool parseCredential(const String &raw, String &service, String &username, String &password);

// Store a new credential
void storeCredential(const String &service, const String &username, const String &password);

#endif
