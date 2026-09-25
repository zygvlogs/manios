/* ZRP authentication (docs/zrp.md, ADR-0005): the node's cluster key,
 * nonces, and the proofs exchanged in Rauth and Tattach. */
#include "zrp.h"
#include "clock.h"
#include "cpu.h"
#include "kstring.h"
#include "rtc.h"
#include "sha256.h"
#include "timer.h"

static uint8_t key[ZRP_KEY];
static bool have_key;

/* The nonce generator's state. There is no hardware randomness on the
 * target machines; the state mixes the wall clock, uptime and where the
 * PIT is within its tick at every call, and a counter makes every
 * output distinct. Nonces must never repeat (so a recorded proof is
 * useless later), which this guarantees; they are not meant to be
 * secrets. */
static uint8_t pool[SHA256_BYTES];
static uint32_t counter;

const uint8_t *zrp_key(void)
{
	return have_key ? key : 0;
}

void zrp_key_set(const uint8_t *k)
{
	have_key = k != 0;
	if (k) {
		memcpy(key, k, ZRP_KEY);
	} else {
		memset(key, 0, sizeof(key));
	}
}

void zrp_key_derive(const char *passphrase, uint8_t out[ZRP_KEY])
{
	struct sha256 c;
	sha256_init(&c);
	sha256_update(&c, "ZRP2 key", 8);
	sha256_update(&c, passphrase, strlen(passphrase));
	sha256_final(&c, out);
}

void zrp_nonce(uint8_t out[ZRP_NONCE])
{
	uint32_t flags = cpu_irq_save();
	uint32_t n = counter++;
	struct {
		uint32_t counter, fine, wall;
		uint64_t ticks;
	} mix = { n, clock_fine(), rtc_time(), timer_ticks() };
	uint8_t digest[SHA256_BYTES];
	struct sha256 c;
	sha256_init(&c);
	sha256_update(&c, pool, sizeof(pool));
	sha256_update(&c, &mix, sizeof(mix));
	sha256_final(&c, digest);
	/* The state moves on one way, the output another. */
	sha256_init(&c);
	sha256_update(&c, "pool", 4);
	sha256_update(&c, digest, sizeof(digest));
	sha256_final(&c, pool);
	cpu_irq_restore(flags);
	sha256_init(&c);
	sha256_update(&c, "nonce", 5);
	sha256_update(&c, digest, sizeof(digest));
	sha256_final(&c, digest);
	memcpy(out, digest, ZRP_NONCE);
}

void zrp_auth_mac(const uint8_t k[ZRP_KEY], const char *role, const uint8_t cnonce[ZRP_NONCE],
                  const uint8_t snonce[ZRP_NONCE], uint8_t out[ZRP_MAC])
{
	struct hmac_sha256 h;
	hmac_sha256_init(&h, k, ZRP_KEY);
	hmac_sha256_update(&h, "ZRP2 ", 5);
	hmac_sha256_update(&h, role, strlen(role));
	hmac_sha256_update(&h, cnonce, ZRP_NONCE);
	hmac_sha256_update(&h, snonce, ZRP_NONCE);
	hmac_sha256_final(&h, out);
}
