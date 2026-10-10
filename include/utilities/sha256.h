#ifndef APP_UTILITIES_SHA256_H
#define APP_UTILITIES_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_SIZE 32

/* SHA-256, HMAC-SHA256 and PBKDF2 over it, for checking that a file is the
 * one tawk wrote. Small and plain, with no library behind them. */
typedef struct Sha256 {
    uint32_t state[8];
    uint64_t length;        /* bytes taken so far */
    uint8_t  block[64];
    size_t   filled;
} Sha256;

void sha256_init(Sha256 *h);
void sha256_update(Sha256 *h, const void *data, size_t len);
void sha256_final(Sha256 *h, uint8_t out[SHA256_SIZE]);

typedef struct HmacSha256 {
    Sha256  inner;
    uint8_t outer_pad[64];
} HmacSha256;

void hmac_sha256_init(HmacSha256 *h, const void *key, size_t key_len);
void hmac_sha256_update(HmacSha256 *h, const void *data, size_t len);
void hmac_sha256_final(HmacSha256 *h, uint8_t out[SHA256_SIZE]);

/* PBKDF2-HMAC-SHA256: `out_len` bytes from a passphrase and a salt. */
void pbkdf2_hmac_sha256(const void *pass, size_t pass_len, const void *salt, size_t salt_len,
                        uint32_t iterations, uint8_t *out, size_t out_len);
/* Compares without stopping at the first difference. 1 when equal. */
int  sha256_equal(const uint8_t *a, const uint8_t *b, size_t len);

#endif
