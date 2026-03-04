#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include "crypto.h"

#define FIELD_SEPARATOR '\x1F'

struct Credential {
    String serviceName;
    String serviceUrl;
    String identifier;
    String password;
    String totpSecret;
    uint8_t iv[IV_SIZE];
};

void storageInit();
int getCredentialCount();
bool storeCredential(const Credential &cred, const uint8_t *encKey);
bool getCredential(int index, Credential &cred, const uint8_t *encKey);
bool updateCredential(int index, const Credential &cred, const uint8_t *encKey);
bool deleteCredential(int index, const uint8_t *encKey);

#endif
