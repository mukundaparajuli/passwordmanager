#include "crypto.h"

#include <mbedtls/aes.h>
#include <mbedtls/md.h>
#include <esp_random.h>
#include <time.h>

bool aesEncrypt(const uint8_t *key, const uint8_t *iv,
                const uint8_t *plaintext, size_t plainLen,
                uint8_t *ciphertext, size_t &cipherLen) {
    size_t padLen = AES_BLOCK_SIZE - (plainLen % AES_BLOCK_SIZE);
    size_t totalLen = plainLen + padLen;

    uint8_t *padded = (uint8_t *)malloc(totalLen);
    if (!padded) return false;

    memcpy(padded, plaintext, plainLen);
    memset(padded + plainLen, (uint8_t)padLen, padLen);

    uint8_t ivCopy[IV_SIZE];
    memcpy(ivCopy, iv, IV_SIZE);

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);

    int ret = mbedtls_aes_setkey_enc(&ctx, key, AES_KEY_SIZE * 8);
    if (ret != 0) {
        mbedtls_aes_free(&ctx);
        free(padded);
        return false;
    }

    ret = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT,
                                 totalLen, ivCopy, padded, ciphertext);
    mbedtls_aes_free(&ctx);
    free(padded);

    if (ret != 0) return false;

    cipherLen = totalLen;
    return true;
}

bool aesDecrypt(const uint8_t *key, const uint8_t *iv,
                const uint8_t *ciphertext, size_t cipherLen,
                uint8_t *plaintext, size_t &plainLen) {
    if (cipherLen == 0 || cipherLen % AES_BLOCK_SIZE != 0) return false;

    uint8_t ivCopy[IV_SIZE];
    memcpy(ivCopy, iv, IV_SIZE);

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);

    int ret = mbedtls_aes_setkey_dec(&ctx, key, AES_KEY_SIZE * 8);
    if (ret != 0) {
        mbedtls_aes_free(&ctx);
        return false;
    }

    ret = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT,
                                 cipherLen, ivCopy, ciphertext, plaintext);
    mbedtls_aes_free(&ctx);

    if (ret != 0) return false;

    uint8_t padVal = plaintext[cipherLen - 1];
    if (padVal < 1 || padVal > AES_BLOCK_SIZE) return false;

    for (size_t i = 0; i < padVal; i++) {
        if (plaintext[cipherLen - 1 - i] != padVal) return false;
    }

    plainLen = cipherLen - padVal;
    return true;
}

void generateIV(uint8_t *iv) {
    for (int i = 0; i < IV_SIZE; i += 4) {
        uint32_t r = esp_random();
        int remaining = IV_SIZE - i;
        int copyLen = (remaining < 4) ? remaining : 4;
        memcpy(iv + i, &r, copyLen);
    }
}

static const char BASE32_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

int base32Decode(const char *encoded, uint8_t *output, int outputLen) {
    int buffer = 0;
    int bitsLeft = 0;
    int count = 0;

    for (const char *p = encoded; *p && *p != '='; p++) {
        char c = toupper(*p);
        if (c == ' ' || c == '-') continue;

        const char *pos = strchr(BASE32_CHARS, c);
        if (!pos) return -1;

        buffer <<= 5;
        buffer |= (pos - BASE32_CHARS);
        bitsLeft += 5;

        if (bitsLeft >= 8) {
            if (count < outputLen) {
                output[count++] = (buffer >> (bitsLeft - 8)) & 0xFF;
            }
            bitsLeft -= 8;
        }
    }

    return count;
}

String generateTOTP(const String &base32Secret) {
    if (base32Secret.length() == 0) return "";

    uint8_t secret[64];
    int secretLen = base32Decode(base32Secret.c_str(), secret, sizeof(secret));
    if (secretLen <= 0) return "INVALID";

    time_t now = time(nullptr);
    if (now < 1000000) return "NO_TIME";

    uint64_t timeStep = now / TOTP_PERIOD;

    uint8_t timeBytes[8];
    for (int i = 7; i >= 0; i--) {
        timeBytes[i] = timeStep & 0xFF;
        timeStep >>= 8;
    }

    uint8_t hmacResult[20];
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);

    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, info, 1);
    mbedtls_md_hmac_starts(&ctx, secret, secretLen);
    mbedtls_md_hmac_update(&ctx, timeBytes, 8);
    mbedtls_md_hmac_finish(&ctx, hmacResult);
    mbedtls_md_free(&ctx);

    memset(secret, 0, sizeof(secret));

    int offset = hmacResult[19] & 0x0F;
    uint32_t code = ((hmacResult[offset] & 0x7F) << 24) |
                    ((hmacResult[offset + 1] & 0xFF) << 16) |
                    ((hmacResult[offset + 2] & 0xFF) << 8) |
                    (hmacResult[offset + 3] & 0xFF);

    code = code % 1000000;

    char buf[7];
    snprintf(buf, sizeof(buf), "%06u", code);
    return String(buf);
}
