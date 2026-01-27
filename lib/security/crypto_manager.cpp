#include "crypto_manager.h"
#include "config.h"
#include <mbedtls/aes.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>

void CryptoManager::begin() {}

bool CryptoManager::deriveKeyFromPIN(const String &pin, const uint8_t *salt,
                                     size_t saltLength, uint8_t *key) {
  //   Implement PBKDF2 key derivation here
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);

  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

  if (info == nullptr) {
    mbedtls_md_free(&ctx);
    return false;
  }

  if (mbedtls_md_setup(&ctx, info, 1) != 0) {
    mbedtls_md_free(&ctx);
    return false;
  }

  // derive key using PBKDF2
  int result = mbedtls_pkcs5_pbkdf2_hmac(
      &ctx, (const unsigned char *)pin.c_str(), pin.length(), salt, saltLength,
      PBKDF2_ITERATIONS, AES_KEY_SIZE, key);
  mbedtls_md_free(&ctx);

  if (result != 0) {
    DEBUG_PRINTLN("Key derivation failed");
    return false;
  }
  DEBUG_PRINTLN("Key derivation successful");
  return true;
}

void CryptoManager::generateIV(uint8_t *iv) {
  for (size_t i = 0; i < AES_IV_SIZE; i++) {
    iv[i] = (uint8_t)esp_random();
  }
  DEBUG_PRINTLN("IV generated");
  return;
}

bool CryptoManager::encryptAES256(const uint8_t *plaintext, size_t plaintextLen,
                                  const uint8_t *key, const uint8_t *iv,
                                  uint8_t *ciphertext, size_t *ciphertextLen) {
  //   Implement AES-256-CBC encryption here
  return true;
}

bool CryptoManager::decryptAES256(const uint8_t *ciphertext,
                                  size_t ciphertextLen, const uint8_t *key,
                                  const uint8_t *iv, uint8_t *plaintext,
                                  size_t *plaintextLen) {
  //   Implement AES-256-CBC decryption here
  return true;
}

bool CryptoManager::encryptPassword(const String &password, const uint8_t *key,
                                    const uint8_t *iv,
                                    uint8_t *encryptedPassword,
                                    size_t *encryptedLen) {
  //   Implement password encryption here
  return true;
}

bool CryptoManager::decryptPassword(const uint8_t *encryptedPassword,
                                    size_t encryptedLength, const uint8_t *key,
                                    const uint8_t *iv,
                                    String &decryptedPassword) {
  //   Implement password decryption here
  return true;
}

size_t CryptoManager::addPadding(uint8_t *data, size_t dataLen,
                                 size_t blockSize) {

  size_t padding = AES_BLOCK_SIZE - (dataLen % AES_BLOCK_SIZE);
  for (size_t i = 0; i < padding; i++) {
    data[dataLen + i] = (uint8_t)padding;
  }
  return dataLen + padding;
}

size_t CryptoManager::removePadding(const uint8_t *data, size_t dataLen) {
  if (dataLen == 0) {
    return 0;
  }
  uint8_t padding = data[dataLen - 1];
  if (padding > AES_BLOCK_SIZE) {
    return dataLen; // Invalid padding
  }
  return dataLen - padding;
}
