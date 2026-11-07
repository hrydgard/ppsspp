// Copyright 2007-2022  Segher Boessenkool  <segher@kernel.crashing.org>
// Licensed under the terms of the GNU GPL, either version 2 or version 3
// https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
// https://www.gnu.org/licenses/gpl-3.0.html

// Modified for Kirk elliptic curve options by setting single curve and internal function
// to support Kirk elliptic curve options.- July 2011
// Points in Jacobian coordinates (one inversion per scalar multiplication rather than one per
// addition), and u1*G + u2*Q verified in a single pass - 2026

#include <string.h>

// Include definitions from kirk header
#include "kirk_engine.h"

// Field elements are in Montgomery form. z == 0 is the point at infinity.
struct point {
  u32 x[BN_WORDS];
  u32 y[BN_WORDS];
  u32 z[BN_WORDS];
};

// Simplified for use by Kirk Engine since it has only 1 curve
static bn_mod ec_p;
static bn_mod ec_N;
static u32 ec_a[BN_WORDS];  // mon
static struct point ec_G;
static struct point ec_Q;
static u32 ec_k[BN_WORDS];

static void elt_add(u32 *d, const u32 *a, const u32 *b)
{
  bn_add(d, a, b, &ec_p);
}

static void elt_sub(u32 *d, const u32 *a, const u32 *b)
{
  bn_sub(d, a, b, &ec_p);
}

static void elt_mul(u32 *d, const u32 *a, const u32 *b)
{
  bn_mon_mul(d, a, b, &ec_p);
}

static void elt_square(u32 *d, const u32 *a)
{
  bn_mon_mul(d, a, a, &ec_p);
}

static void point_zero(struct point *p)
{
  memset(p, 0, sizeof(*p));
}

// From 40 big-endian bytes, x then y. (0, 0) stands for the point at infinity.
static void point_from_bytes(struct point *p, const u8 *b)
{
  u32 x[BN_WORDS], y[BN_WORDS];

  bn_from_bytes(x, b, 20);
  bn_from_bytes(y, b + 20, 20);
  if (bn_is_zero(x) && bn_is_zero(y)) {
    point_zero(p);
    return;
  }
  bn_to_mon(p->x, x, &ec_p);
  bn_to_mon(p->y, y, &ec_p);
  memcpy(p->z, ec_p.one, sizeof(p->z));
}

// To affine x and y, out of Montgomery form. Infinity comes out as (0, 0).
static void point_to_affine(u32 *x, u32 *y, const struct point *p)
{
  u32 zi[BN_WORDS], zi2[BN_WORDS], t[BN_WORDS];

  if (bn_is_zero(p->z)) {
    memset(x, 0, BN_WORDS * sizeof(u32));
    memset(y, 0, BN_WORDS * sizeof(u32));
    return;
  }

  bn_mon_inv(zi, p->z, &ec_p);
  elt_square(zi2, zi);
  elt_mul(t, p->x, zi2);
  bn_from_mon(x, t, &ec_p);
  elt_mul(t, p->y, zi2);
  elt_mul(t, t, zi);
  bn_from_mon(y, t, &ec_p);
}

static void point_to_bytes(u8 *b, const struct point *p)
{
  u32 x[BN_WORDS], y[BN_WORDS];

  point_to_affine(x, y, p);
  bn_to_bytes(b, x, 20);
  bn_to_bytes(b + 20, y, 20);
}

// dbl-2007-bl, for any a.
static void point_double(struct point *r, const struct point *p)
{
  u32 xx[BN_WORDS], yy[BN_WORDS], yyyy[BN_WORDS], zz[BN_WORDS];
  u32 s[BN_WORDS], m[BN_WORDS], t[BN_WORDS], z3[BN_WORDS];

  if (bn_is_zero(p->z) || bn_is_zero(p->y)) {
    point_zero(r);
    return;
  }

  elt_square(xx, p->x);
  elt_square(yy, p->y);
  elt_square(yyyy, yy);
  elt_square(zz, p->z);

  elt_mul(s, p->x, yy);
  elt_add(s, s, s);
  elt_add(s, s, s);       // s = 4*x*y^2

  elt_square(t, zz);
  elt_mul(t, t, ec_a);
  elt_add(m, xx, xx);
  elt_add(m, m, xx);
  elt_add(m, m, t);       // m = 3*x^2 + a*z^4

  elt_add(z3, p->y, p->y);
  elt_mul(z3, z3, p->z);  // z3 = 2*y*z

  elt_square(r->x, m);
  elt_sub(r->x, r->x, s);
  elt_sub(r->x, r->x, s); // x3 = m^2 - 2*s

  elt_sub(t, s, r->x);
  elt_mul(t, m, t);
  elt_add(yyyy, yyyy, yyyy);
  elt_add(yyyy, yyyy, yyyy);
  elt_add(yyyy, yyyy, yyyy);
  elt_sub(r->y, t, yyyy); // y3 = m*(s - x3) - 8*y^4

  memcpy(r->z, z3, sizeof(z3));
}

// add-2007-bl without the doubling-friendly tweaks. r may be p or q.
static void point_add(struct point *r, const struct point *p, const struct point *q)
{
  u32 z1z1[BN_WORDS], z2z2[BN_WORDS], u1[BN_WORDS], u2[BN_WORDS], s1[BN_WORDS], s2[BN_WORDS];
  u32 h[BN_WORDS], rr[BN_WORDS], hh[BN_WORDS], hhh[BN_WORDS], v[BN_WORDS], t[BN_WORDS], z3[BN_WORDS];

  if (bn_is_zero(p->z)) {
    *r = *q;
    return;
  }
  if (bn_is_zero(q->z)) {
    *r = *p;
    return;
  }

  elt_square(z1z1, p->z);
  elt_square(z2z2, q->z);
  elt_mul(u1, p->x, z2z2);
  elt_mul(u2, q->x, z1z1);
  elt_mul(s1, p->y, q->z);
  elt_mul(s1, s1, z2z2);
  elt_mul(s2, q->y, p->z);
  elt_mul(s2, s2, z1z1);

  elt_sub(h, u2, u1);
  elt_sub(rr, s2, s1);
  if (bn_is_zero(h)) {
    if (bn_is_zero(rr))
      point_double(r, p);
    else
      point_zero(r);
    return;
  }

  elt_square(hh, h);
  elt_mul(hhh, h, hh);
  elt_mul(v, u1, hh);

  elt_mul(z3, p->z, q->z);
  elt_mul(z3, z3, h);     // z3 = z1*z2*h

  elt_square(r->x, rr);
  elt_sub(r->x, r->x, hhh);
  elt_sub(r->x, r->x, v);
  elt_sub(r->x, r->x, v); // x3 = r^2 - h^3 - 2*v

  elt_sub(t, v, r->x);
  elt_mul(t, rr, t);
  elt_mul(s1, s1, hhh);
  elt_sub(r->y, t, s1);   // y3 = r*(v - x3) - s1*h^3

  memcpy(r->z, z3, sizeof(z3));
}

// d = a*b, a being n big-endian bytes.
static void point_mul(struct point *d, const u8 *a, u32 n, const struct point *b)
{
  struct point r;
  u32 i;
  u8 mask;

  point_zero(&r);
  for (i = 0; i < n; i++)
    for (mask = 0x80; mask != 0; mask >>= 1) {
      point_double(&r, &r);
      if ((a[i] & mask) != 0)
        point_add(&r, &r, b);
    }
  *d = r;
}

// d = a*p + b*q, a and b being n big-endian bytes, sharing the doublings (Shamir's trick).
static void point_mul2(struct point *d, const u8 *a, const struct point *p, const u8 *b, const struct point *q, u32 n)
{
  struct point r, pq;
  u32 i;
  u8 mask;

  point_add(&pq, p, q);
  point_zero(&r);
  for (i = 0; i < n; i++)
    for (mask = 0x80; mask != 0; mask >>= 1) {
      int bits = ((a[i] & mask) ? 1 : 0) | ((b[i] & mask) ? 2 : 0);
      point_double(&r, &r);
      if (bits == 1)
        point_add(&r, &r, p);
      else if (bits == 2)
        point_add(&r, &r, q);
      else if (bits == 3)
        point_add(&r, &r, &pq);
    }
  *d = r;
}

// Modified from original to support kirk engine use - July 2011
// Added call to Kirk Random number generator rather than /dev/random

static void generate_ecdsa(KirkState *kirk, u8 *outR, u8 *outS, const u32 *k, const u8 *hash)
{
  u8 m[20];
  u32 e[BN_WORDS], mm[BN_WORDS], minv[BN_WORDS], R[BN_WORDS], S[BN_WORDS], t[BN_WORDS];
  struct point mG;

  bn_from_bytes(e, hash, 20);
  bn_reduce(e, &ec_N);

  // Added call back to kirk PRNG - July 2011
  kirk_CMD14(kirk, m, 20);

  //  R = (mG).x
  point_mul(&mG, m, 20, &ec_G);
  point_to_affine(R, t, &mG);
  bn_reduce(R, &ec_N);

  //  S = m**-1*(e + Rk) (mod N). A Montgomery product of a plain number and a Montgomery-form one
  //  comes out plain.
  bn_from_bytes(mm, m, 20);
  bn_reduce(mm, &ec_N);
  bn_to_mon(mm, mm, &ec_N);
  bn_mon_inv(minv, mm, &ec_N);

  bn_to_mon(t, k, &ec_N);
  bn_mon_mul(S, R, t, &ec_N);
  bn_add(S, S, e, &ec_N);
  bn_mon_mul(S, S, minv, &ec_N);

  bn_to_bytes(outR, R, 20);
  bn_to_bytes(outS, S, 20);
}

    // Signing =
    // r = k *G;
    // s = x*r+m / k

    // Verify =
    // r/s * P = m/s * G

// Slightly modified to support kirk compatible signature input - July 2011
static int check_ecdsa(const struct point *Q, const u8 *inR, const u8 *inS, const u8 *hash)
{
  u32 e[BN_WORDS], R[BN_WORDS], S[BN_WORDS], Sinv[BN_WORDS], w[BN_WORDS];
  u32 x[BN_WORDS], y[BN_WORDS];
  u8 w1[20], w2[20];
  struct point r;

  bn_from_bytes(e, hash, 20);
  bn_reduce(e, &ec_N);
  bn_from_bytes(R, inR, 20);
  bn_reduce(R, &ec_N);
  bn_from_bytes(S, inS, 20);
  bn_reduce(S, &ec_N);

  // Sinv = 1/S, then w1 = e/S and w2 = R/S, plain.
  bn_to_mon(S, S, &ec_N);
  bn_mon_inv(Sinv, S, &ec_N);
  bn_mon_mul(w, e, Sinv, &ec_N);
  bn_to_bytes(w1, w, 20);
  bn_mon_mul(w, R, Sinv, &ec_N);
  bn_to_bytes(w2, w, 20);

  // r = w1*G + w2*Q
  point_mul2(&r, w1, &ec_G, w2, Q, 20);
  point_to_affine(x, y, &r);
  bn_reduce(x, &ec_N);

  return bn_equal(x, R);
}


// Modified from original to support kirk engine use - July 2011
// k is replaced with k*2^168 mod N (k in the Montgomery form the byte-based version used), and that
// is what the public key is for. Command 0xC hands it out as the private key.
void ec_priv_to_pub(u8 *k, u8 *Q)
{
  u32 kk[BN_WORDS];
  struct point ec_temp;
  int i;

  bn_from_bytes(kk, k, 21);
  bn_reduce(kk, &ec_N);
  bn_to_mon(kk, kk, &ec_N);
  for (i = 0; i < 8; i++)
    bn_add(kk, kk, kk, &ec_N);
  bn_to_bytes(k, kk, 21);

  point_mul(&ec_temp, k, 21, &ec_G);
  point_to_bytes(Q, &ec_temp);
}

// Modified from original to support kirk engine use - July 2011
void ec_pub_mult(const u8 *k, u8 *Q)
{
  struct point ec_temp;
  point_mul(&ec_temp, k, 21, &ec_Q);
  point_to_bytes(Q, &ec_temp);
}

// Simplified for use by Kirk Engine - NO LONGER COMPATIABLE WITH ORIGINAL VERSION - July 2011
int ecdsa_set_curve(const u8* p, const u8* a, const u8* b, const u8* N, const u8* Gx, const u8* Gy)
{
  u32 t[BN_WORDS];
  u8 G[40];

  (void)b;  // Only needed to check that a point is on the curve.
  bn_mod_init(&ec_p, p, 20);
  bn_mod_init(&ec_N, N, 21);

  bn_from_bytes(t, a, 20);
  bn_to_mon(ec_a, t, &ec_p);

  memcpy(G, Gx, 20);
  memcpy(G + 20, Gy, 20);
  point_from_bytes(&ec_G, G);

  return 0;
}

void ecdsa_set_pub(u8 *Q)
{
  point_from_bytes(&ec_Q, Q);
}

void ecdsa_set_priv(u8 *ink)
{
  bn_from_bytes(ec_k, ink, 20);
  bn_reduce(ec_k, &ec_N);
}

int ecdsa_verify(const u8 *hash, const u8 *R, const u8 *S)
{
  return check_ecdsa(&ec_Q, R, S, hash);
}

void ecdsa_sign(KirkState *kirk, const u8 *hash, u8 *R, u8 *S)
{
  generate_ecdsa(kirk, R, S, ec_k, hash);
}
