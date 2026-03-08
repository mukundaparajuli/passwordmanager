#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include <stdint.h>

#include "crypto.h"

static const uint8_t CRED_FLAG_HAS_TOTP = 0x01;

typedef struct {
    char service[32];
    char url[64];
    char username[32];
    char password[64];
    char totp_secret[32];
    uint8_t flags;
} credential_entry_t;

static_assert(sizeof(credential_entry_t) == 225, "credential_entry_t size must be 225 bytes");

void storageInit();
int getCredentialCount();
bool storeCredential(const credential_entry_t &cred, const uint8_t *encKey, int &outId);
bool getCredential(int index, credential_entry_t &cred, const uint8_t *encKey);
bool updateCredential(int index, const credential_entry_t &cred, const uint8_t *encKey);
bool deleteCredential(int index, const uint8_t *encKey);

#endif
