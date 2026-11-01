// Fast helper for adhoc_decrypt.py's key recovery, for the cases that are too slow in pure Python:
// bruting a few unknown plaintext bytes, and bruting the 4 tag bytes of the 20-byte session key.
// See docs/adhoc-packet-decryption.md for the workflow. This recovers an ephemeral per-session key from
// a capture that contains known plaintext; it is not a break of the key exchange.
//
//   cc -O3 adhoc_recover.c -o adhoc_recover          (uses macOS CommonCrypto)
//   or: cc -O3 adhoc_recover.c -lcrypto -o adhoc_recover   with -DUSE_OPENSSL on Linux
//
//   ./adhoc_recover half TEMPLATE CT16 COUNTER
//       Recover 8 key bytes. TEMPLATE is the 16-byte first-block plaintext in hex, with "xx" for
//       each unknown byte (at most ~5), which are brute-forced; the frame's counter validates the
//       guess. Run once for an even-counter frame and once for an odd-counter frame; concatenate the
//       even half then the odd half to get the 16-byte key that decrypts the capture.
//
//   ./adhoc_recover tag KEY16 CT16 COUNTER TAG16
//       Brute the last 4 bytes of the 20-byte session key (needed only to verify integrity tags),
//       given the 16-byte key and any one frame's first 16 ciphertext bytes, its counter and its tag.

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#ifdef USE_OPENSSL
#include <openssl/aes.h>
#include <openssl/sha.h>
static void aes_ecb_enc(const uint8_t *k, const uint8_t *in, uint8_t *out) {
	AES_KEY ek; AES_set_encrypt_key(k, 128, &ek); AES_encrypt(in, out, &ek);
}
static void sha1_16(const uint8_t *in, size_t n, uint8_t *out20) { SHA1(in, n, out20); }
#else
#include <CommonCrypto/CommonCryptor.h>
#include <CommonCrypto/CommonDigest.h>
static void aes_ecb_enc(const uint8_t *k, const uint8_t *in, uint8_t *out) {
	size_t m; CCCrypt(kCCEncrypt, kCCAlgorithmAES, kCCOptionECBMode, k, 16, NULL, in, 16, out, 16, &m);
}
static void sha1_16(const uint8_t *in, size_t n, uint8_t *out20) { CC_SHA1(in, n, out20); }
#endif

static const uint8_t KEY44[16] = {0x7D,0xF4,0x92,0x65,0xE3,0xFA,0xD6,0x78,0xD6,0xFE,0x78,0xAD,0xBB,0x3D,0xFB,0x63};

static int hx(const char *s, uint8_t *o, int n) { for (int i = 0; i < n; i++) if (sscanf(s + 2 * i, "%2hhx", &o[i]) != 1) return 0; return 1; }

// Parse a 16-byte template "aabbxx..." into base bytes + the indices of the unknown ("xx") bytes.
static int parse_template(const char *s, uint8_t *base, int *unk, int *nunk) {
	if (strlen(s) != 32) return 0;
	*nunk = 0;
	for (int i = 0; i < 16; i++) {
		if (s[2 * i] == 'x' && s[2 * i + 1] == 'x') { base[i] = 0; unk[(*nunk)++] = i; }
		else if (sscanf(s + 2 * i, "%2hhx", &base[i]) != 1) return 0;
	}
	return 1;
}

static int do_half(const char *tmpl, const char *cths, const char *ctrs) {
	uint8_t base[16], ct[16]; int unk[16], nunk;
	if (!parse_template(tmpl, base, unk, &nunk) || !hx(cths, ct, 16)) { fprintf(stderr, "bad template/ct\n"); return 2; }
	uint64_t counter = strtoull(ctrs, 0, 10);
	if (nunk > 6) { fprintf(stderr, "too many unknown bytes (%d); brute space too large\n", nunk); return 2; }
	uint64_t total = 1; for (int i = 0; i < nunk; i++) total <<= 8;
	for (uint64_t v = 0; v < total; v++) {
		uint8_t pt[16]; memcpy(pt, base, 16);
		uint64_t t = v; for (int i = 0; i < nunk; i++) { pt[unk[i]] = t & 0xff; t >>= 8; }
		uint8_t x[16], blk[16];
		for (int i = 0; i < 16; i++) x[i] = ct[i] ^ pt[i];
		aes_ecb_enc(KEY44, x, blk);
		uint64_t c; memcpy(&c, blk, 8);
		if (c == counter) {
			printf("half: "); for (int i = 8; i < 16; i++) printf("%02x", blk[i]);
			printf("  (plaintext "); for (int i = 0; i < 16; i++) printf("%02x", pt[i]); printf(")\n");
			return 0;
		}
	}
	printf("no match (wrong template or ciphertext)\n");
	return 1;
}

static uint8_t g_key[20], g_ct[16], g_want[16]; static uint64_t g_ctr;
static volatile int g_found; static uint32_t g_tail;
static void *tagworker(void *arg) {
	long id = (long)arg;
	uint8_t buf[52]; memcpy(buf, g_key, 16);
	uint8_t hdr[16] = {0}; memcpy(hdr, &g_ctr, 8); memcpy(buf + 20, hdr, 16); memcpy(buf + 36, g_ct, 16);
	for (uint64_t x = id; x < 0x100000000ULL && !g_found; x += 8) {
		buf[16] = x; buf[17] = x >> 8; buf[18] = x >> 16; buf[19] = x >> 24;
		uint8_t h[20]; sha1_16(buf, 52, h);
		if (!memcmp(h, g_want, 16)) { g_tail = (uint32_t)x; g_found = 1; return 0; }
	}
	return 0;
}

static int do_tag(const char *k16, const char *cths, const char *ctrs, const char *tags) {
	if (!hx(k16, g_key, 16) || !hx(cths, g_ct, 16) || !hx(tags, g_want, 16)) { fprintf(stderr, "bad hex\n"); return 2; }
	g_ctr = strtoull(ctrs, 0, 10);
	pthread_t t[8]; for (long i = 0; i < 8; i++) pthread_create(&t[i], 0, tagworker, (void *)i);
	for (int i = 0; i < 8; i++) pthread_join(t[i], 0);
	if (!g_found) { printf("tag tail not found (wrong key or frame)\n"); return 1; }
	printf("key20: "); for (int i = 0; i < 16; i++) printf("%02x", g_key[i]);
	printf("%02x%02x%02x%02x\n", g_tail & 0xff, (g_tail >> 8) & 0xff, (g_tail >> 16) & 0xff, (g_tail >> 24) & 0xff);
	return 0;
}

int main(int argc, char **argv) {
	if (argc == 5 && !strcmp(argv[1], "half")) return do_half(argv[2], argv[3], argv[4]);
	if (argc == 6 && !strcmp(argv[1], "tag"))  return do_tag(argv[2], argv[3], argv[4], argv[5]);
	fprintf(stderr,
		"usage:\n"
		"  %s half TEMPLATE16hex(xx=unknown) CT16hex COUNTER\n"
		"  %s tag  KEY16hex CT16hex COUNTER TAG16hex\n", argv[0], argv[0]);
	return 2;
}
