/**
 * @file crypto_manager.h
 * @brief AES-256 encryption/decryption for password storage
 *
 * Provides secure encryption and decryption of passwords using AES-256-CBC.
 * Encryption key is derived from the user's PIN using PBKDF2.
 */

#ifndef CRYPTO_MANAGER_H
#define CRYPTO_MANAGER_H

#include "config.h"
#include <Arduino.h>

class CryptoManager {
public:
  /**
   * @brief Initialize the crypto manager
   */
  void begin();

  /**
   * @brief Derive an encryption key from a PIN using PBKDF2
   * @param pin The PIN to derive from
   * @param salt Salt for key derivation
   * @param saltLength Length of the salt
   * @param key Buffer to store the derived key (must be AES_KEY_SIZE bytes)
   * @return true if successful, false otherwise
   */
  bool deriveKeyFromPIN(const String &pin, const uint8_t *salt,
                        size_t saltLength, uint8_t *key);

  /**
   * @brief Encrypt plaintext data using AES-256-CBC
   * @param plaintext The data to encrypt
   * @param plaintextLen Length of plaintext
   * @param key Encryption key (AES_KEY_SIZE bytes)
   * @param iv Initialization vector (AES_IV_SIZE bytes)
   * @param ciphertext Buffer to store encrypted data (must be large enough)
   * @param ciphertextLen Pointer to store the length of encrypted data
   * @return true if successful, false otherwise
   */
  bool encryptAES256(const uint8_t *plaintext, size_t plaintextLen,
                     const uint8_t *key, const uint8_t *iv, uint8_t *ciphertext,
                     size_t *ciphertextLen);

  /**
   * @brief Decrypt ciphertext data using AES-256-CBC
   * @param ciphertext The data to decrypt
   * @param ciphertextLen Length of ciphertext
   * @param key Decryption key (AES_KEY_SIZE bytes)
   * @param iv Initialization vector (AES_IV_SIZE bytes)
   * @param plaintext Buffer to store decrypted data
   * @param plaintextLen Pointer to store the length of decrypted data
   * @return true if successful, false otherwise
   */
  bool decryptAES256(const uint8_t *ciphertext, size_t ciphertextLen,
                     const uint8_t *key, const uint8_t *iv, uint8_t *plaintext,
                     size_t *plaintextLen);

  /**
   * @brief Generate a random initialization vector (IV)
   * @param iv Buffer to store the IV (must be AES_IV_SIZE bytes)
   */
  void generateIV(uint8_t *iv);

  /**
   * @brief Encrypt a string password
   * @param password The password to encrypt
   * @param key Encryption key
   * @param iv Initialization vector
   * @param encryptedPassword Buffer to store encrypted password
   * @param encryptedLen Pointer to store encrypted length
   * @return true if successful, false otherwise
   */
  bool encryptPassword(const String &password, const uint8_t *key,
                       const uint8_t *iv, uint8_t *encryptedPassword,
                       size_t *encryptedLen);

  /**
   * @brief Decrypt an encrypted password
   * @param encryptedPassword The encrypted password data
   * @param encryptedLen Length of encrypted data
   * @param key Decryption key
   * @param iv Initialization vector
   * @param password String to store the decrypted password
   * @return true if successful, false otherwise
   */
  bool decryptPassword(const uint8_t *encryptedPassword, size_t encryptedLen,
                       const uint8_t *key, const uint8_t *iv, String &password);

  /**
   * @brief Securely clear sensitive data from memory
   * @param data Pointer to data to clear
   * @param length Length of data
   */
  void secureClear(void *data, size_t length);

private:
  /**
   * @brief Apply PKCS#7 padding to data
   * @param data Buffer containing data to pad
   * @param dataLen Current length of data
   * @param blockSize Block size for padding
   * @return New length after padding
   */
  size_t addPadding(uint8_t *data, size_t dataLen, size_t blockSize);

  /**
   * @brief Remove PKCS#7 padding from data
   * @param data Buffer containing padded data
   * @param paddedLen Length of padded data
   * @return Original length before padding, or 0 if invalid padding
   */
  size_t removePadding(const uint8_t *data, size_t paddedLen);
};

#endif // CRYPTO_MANAGER_H