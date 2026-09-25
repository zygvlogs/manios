/* SHA-256 (FIPS 180-4) and HMAC-SHA-256 (RFC 2104), for authenticating
 * ZRP sessions (M13). Integer-only, no 64-bit multiplication. */
#ifndef ZKT_KERNEL_SHA256_H
#define ZKT_KERNEL_SHA256_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SHA256_BYTES 32
#define SHA256_BLOCK 64

struct sha256 {
	uint32_t h[8];
	uint8_t block[SHA256_BLOCK];
	uint32_t used;         /* bytes in block */
	uint32_t bits_lo, bits_hi;
};

void sha256_init(struct sha256 *c);
void sha256_update(struct sha256 *c, const void *data, size_t len);
void sha256_final(struct sha256 *c, uint8_t out[SHA256_BYTES]);
void sha256(const void *data, size_t len, uint8_t out[SHA256_BYTES]);

struct hmac_sha256 {
	struct sha256 inner;
	uint8_t outer_key[SHA256_BLOCK];
};

void hmac_sha256_init(struct hmac_sha256 *h, const void *key, size_t key_len);
void hmac_sha256_update(struct hmac_sha256 *h, const void *data, size_t len);
void hmac_sha256_final(struct hmac_sha256 *h, uint8_t out[SHA256_BYTES]);

/* Compares in time independent of where they differ. */
bool equal_secret(const void *a, const void *b, size_t len);

/* Known-answer tests (FIPS 180-4 examples, RFC 4231); false on failure. */
bool sha256_selftest(void);

#endif
