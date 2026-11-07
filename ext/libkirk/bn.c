// Copyright 2007-2022  Segher Boessenkool  <segher@kernel.crashing.org>
// Licensed under the terms of the GNU GPL, either version 2 or version 3
// https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
// https://www.gnu.org/licenses/gpl-3.0.html
// Updated and simplified for use by Kirk Engine - July 2011
// Rewritten on 32-bit words for speed (it used bytes), still Montgomery multiplication - 2026

#include <stdint.h>
#include <string.h>

// Include definitions from kirk header
#include "kirk_engine.h"

void bn_from_bytes(u32 *d, const u8 *b, u32 n)
{
	u32 i;

	memset(d, 0, BN_WORDS * sizeof(u32));
	for (i = 0; i < n && i < BN_WORDS * 4; i++)
		d[i / 4] |= (u32)b[n - 1 - i] << (8 * (i % 4));
}

void bn_to_bytes(u8 *b, const u32 *a, u32 n)
{
	u32 i;

	for (i = 0; i < n; i++)
		b[n - 1 - i] = i < BN_WORDS * 4 ? (u8)(a[i / 4] >> (8 * (i % 4))) : 0;
}

int bn_is_zero(const u32 *a)
{
	u32 i, x = 0;

	for (i = 0; i < BN_WORDS; i++)
		x |= a[i];
	return x == 0;
}

int bn_equal(const u32 *a, const u32 *b)
{
	return memcmp(a, b, BN_WORDS * sizeof(u32)) == 0;
}

static int bn_compare(const u32 *a, const u32 *b)
{
	int i;

	for (i = BN_WORDS - 1; i >= 0; i--) {
		if (a[i] < b[i])
			return -1;
		if (a[i] > b[i])
			return 1;
	}
	return 0;
}

// d = a + b, returning the carry out.
static u32 bn_add_1(u32 *d, const u32 *a, const u32 *b)
{
	uint64_t c = 0;
	u32 i;

	for (i = 0; i < BN_WORDS; i++) {
		c += (uint64_t)a[i] + b[i];
		d[i] = (u32)c;
		c >>= 32;
	}
	return (u32)c;
}

// d = a - b, returning the borrow out.
static u32 bn_sub_1(u32 *d, const u32 *a, const u32 *b)
{
	uint64_t c = 0;
	u32 i;

	for (i = 0; i < BN_WORDS; i++) {
		c = (uint64_t)a[i] - b[i] - c;
		d[i] = (u32)c;
		c = (c >> 32) & 1;
	}
	return (u32)c;
}

void bn_reduce(u32 *d, const bn_mod *M)
{
	if (bn_compare(d, M->m) >= 0)
		bn_sub_1(d, d, M->m);
}

void bn_add(u32 *d, const u32 *a, const u32 *b, const bn_mod *M)
{
	// Below 2m, so the carry out (which the subtraction wraps away) or a compare tells.
	if (bn_add_1(d, a, b) || bn_compare(d, M->m) >= 0)
		bn_sub_1(d, d, M->m);
}

void bn_sub(u32 *d, const u32 *a, const u32 *b, const bn_mod *M)
{
	if (bn_sub_1(d, a, b))
		bn_add_1(d, d, M->m);
}

void bn_mon_mul(u32 *d, const u32 *a, const u32 *b, const bn_mod *M)
{
	// Coarsely integrated operand scanning: t stays below 2m after each step.
	u32 t[BN_WORDS + 2] = { 0 };
	u32 i, j;

	for (i = 0; i < BN_WORDS; i++) {
		uint64_t c = 0, s;
		u32 q;

		for (j = 0; j < BN_WORDS; j++) {
			c += (uint64_t)a[j] * b[i] + t[j];
			t[j] = (u32)c;
			c >>= 32;
		}
		c += t[BN_WORDS];
		t[BN_WORDS] = (u32)c;
		t[BN_WORDS + 1] = (u32)(c >> 32);

		q = t[0] * M->minv;
		c = ((uint64_t)q * M->m[0] + t[0]) >> 32;
		for (j = 1; j < BN_WORDS; j++) {
			c += (uint64_t)q * M->m[j] + t[j];
			t[j - 1] = (u32)c;
			c >>= 32;
		}
		s = c + t[BN_WORDS];
		t[BN_WORDS - 1] = (u32)s;
		t[BN_WORDS] = t[BN_WORDS + 1] + (u32)(s >> 32);
	}

	if (t[BN_WORDS] || bn_compare(t, M->m) >= 0)
		bn_sub_1(t, t, M->m);
	memcpy(d, t, BN_WORDS * sizeof(u32));
}

void bn_to_mon(u32 *d, const u32 *a, const bn_mod *M)
{
	bn_mon_mul(d, a, M->rr, M);
}

void bn_from_mon(u32 *d, const u32 *a, const bn_mod *M)
{
	static const u32 one[BN_WORDS] = { 1 };
	bn_mon_mul(d, a, one, M);
}

void bn_mod_init(bn_mod *M, const u8 *m, u32 n)
{
	u32 x, i;

	bn_from_bytes(M->m, m, n);

	// Newton's iteration for m^-1 mod 2^32, each step doubling the correct low bits (m is odd, so m
	// itself is right to 3).
	x = M->m[0];
	for (i = 0; i < 4; i++)
		x *= 2 - M->m[0] * x;
	M->minv = (u32)0 - x;

	// R mod m and R^2 mod m by doubling 1, 160 and 320 times.
	memset(M->one, 0, sizeof(M->one));
	M->one[0] = 1;
	for (i = 0; i < 32 * BN_WORDS; i++)
		bn_add(M->one, M->one, M->one, M);
	memcpy(M->rr, M->one, sizeof(M->rr));
	for (i = 0; i < 32 * BN_WORDS; i++)
		bn_add(M->rr, M->rr, M->rr, M);
}

// Fermat: a^(m-2), so m has to be prime.
void bn_mon_inv(u32 *d, const u32 *a, const bn_mod *M)
{
	static const u32 two[BN_WORDS] = { 2 };
	u32 e[BN_WORDS], t[BN_WORDS];
	int i;

	bn_sub_1(e, M->m, two);
	memcpy(t, M->one, sizeof(t));
	for (i = 32 * BN_WORDS - 1; i >= 0; i--) {
		bn_mon_mul(t, t, t, M);
		if ((e[i / 32] >> (i % 32)) & 1)
			bn_mon_mul(t, t, a, M);
	}
	memcpy(d, t, sizeof(t));
}
