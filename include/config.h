/**
 * @file config.h
 * @brief Hardware and system configuration for the project
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

//-----------------------------------------------------------------------------------------------------------
//  Hardware pins config
//-----------------------------------------------------------------------------------------------------------

// OLED Display pins

// Navigation Buttons

// Button debounce time (milliseconds)
#define DEBOUNCE_DELAY 50

//-------------------------------------------------------------------------------------------------------------
//  SECURITY CONFIGURATIONS
//-------------------------------------------------------------------------------------------------------------

// PIN config
#define PIN_MIN_LENGTH 4
#define PIN_MAX_LENGTH 6
#define MAX_PIN_ATTEMPTS 3
#define PIN_LOCKOUT_TIME 5 * 60 * 1000 // 5 minutes

// Encryption Config
#define AES_KEY_SIZE 32   // 256 bits
#define AES_BLOCK_SIZE 16 // 128 bits
#define AES_IV_SIZE 16    // 128 bits
#define SALT_SIZE 16      // 128 bits

// PBKDF2 Config (for key derivation from the pin)
#define PBKDF2_ITERATIONS 10000

//-------------------------------------------------------------------------------------------------------------
//  STORAGE CONFIGURATIONS
//-------------------------------------------------------------------------------------------------------------

// Maximum no of stored credentials
#define MAX_CREDENTIALS 50

// Maximum field lengths
#define MAX_SERVICE_NAME_LENGTH 32
#define MAX_USERNAME_LENGTH 64
#define MAX_PASSWORD_LENGTH 64
#define MAX_TOTP_SECRET_LENGTH 32

// EEPROM addresses
#define ADDR_PIN_HASH 0
#define ADDR_PIN_SALT 32
#define ADDR_CREDENTIALS_START 100

//-------------------------------------------------------------------------------------------------------------
//  DEBUG CONFIGURATIONS
//-------------------------------------------------------------------------------------------------------------

// Enable or disable debug prints
#define DEBUG_MODE

#ifdef DEBUG_MODE
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#endif

#endif // !CONFIG_H
