#ifndef CRYPTO_H
#define CRYPTO_H

#include <Arduino.h>

#define AES_KEY_SIZE 32
#define AES_BLOCK_SIZE 16
#define IV_SIZE 16
#define TOTP_DIGITS 6
#define TOTP_PERIOD 30

bool aesEncrypt(const uint8_t *key, const uint8_t *iv,
                const uint8_t *plaintext, size_t plainLen,
                uint8_t *ciphertext, size_t &cipherLen);

bool aesDecrypt(const uint8_t *key, const uint8_t *iv,
                const uint8_t *ciphertext, size_t cipherLen,
                uint8_t *plaintext, size_t &plainLen);

void generateIV(uint8_t *iv);

int base32Decode(const char *encoded, uint8_t *output, int outputLen);

String generateTOTP(const String &base32Secret);

#endif
