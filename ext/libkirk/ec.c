// Copyright 2007,2008,2010  Segher Boessenkool  <segher@kernel.crashing.org>
// Licensed under the terms of the GNU GPL, version 2
// http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt


// Modified for Kirk engine by setting single curve and internal function
// to support Kirk elliptic curve options.- July 2011

#include <string.h>
#include <stdio.h>

// Include definitions from kirk header
#include "kirk_engine.h"

struct point {
  u8 x[20];
  u8 y[20];
};
// Simplified for use by Kirk Engine since it has only 1 curve

u8 ec_p[20];
u8 ec_a[20];
u8 ec_b[20];
u8 ec_N[21];
struct point ec_G;  // mon
struct point ec_Q;  // mon
u8 ec_k[21];



void hex_dump(char *str, u8 *buf, int size)
{
  int i;

  if(str)
    printf("%s:", str);

  for(i=0; i<size; i++){
    if((i%32)==0){
      printf("\n%4X:", i);
    }
    printf(" %02X", buf[i]);
  }
  printf("\n\n");
}

static void elt_copy(u8 *d, u8 *a)
{
  memcpy(d, a, 20);
}

static void elt_zero(u8 *d)
{
  memset(d, 0, 20);
}

static int elt_is_zero(u8 *d)
{
  u32 i;

  for (i = 0; i < 20; i++)
    if (d[i] != 0)
      return 0;

  return 1;
}

static void elt_add(u8 *d, u8 *a, u8 *b)
{
  bn_add(d, a, b, ec_p, 20);
}

static void elt_sub(u8 *d, u8 *a, u8 *b)
{
  bn_sub(d, a, b, ec_p, 20);
}

static void elt_mul(u8 *d, u8 *a, u8 *b)
{
  bn_mon_mul(d, a, b, ec_p, 20);
}

static void elt_square(u8 *d, u8 *a)
{
  elt_mul(d, a, a);
}

static void elt_inv(u8 *d, u8 *a)
{
  u8 s[20];
  elt_copy(s, a);
  bn_mon_inv(d, s, ec_p, 20);
}

static void point_to_mon(struct point *p)
{
  bn_to_mon(p->x, ec_p, 20);
  bn_to_mon(p->y, ec_p, 20);
}

static void point_from_mon(struct point *p)
{
  bn_from_mon(p->x, ec_p, 20);
  bn_from_mon(p->y, ec_p, 20);
}

#if 0
static int point_is_on_curve(u8 *p)
{
  u8 s[20], t[20];
  u8 *x, *y;

  x = p;
  y = p + 20;

  elt_square(t, x);
  elt_mul(s, t, x);

  elt_mul(t, x, ec_a);
  elt_add(s, s, t);

  elt_add(s, s, ec_b);

  elt_square(t, y);
  elt_sub(s, s, t);

  return elt_is_zero(s);
}
#endif

static void point_zero(struct point *p)
{
  elt_zero(p->x);
  elt_zero(p->y);
}

static int point_is_zero(struct point *p)
{
  return elt_is_zero(p->x) && elt_is_zero(p->y);
}

static void point_double(struct point *r, struct point *p)
{
  u8 s[20], t[20];
  struct point pp;
  u8 *px, *py, *rx, *ry;

  pp = *p;

  px = pp.x;
  py = pp.y;
  rx = r->x;
  ry = r->y;

  if (elt_is_zero(py)) {
    point_zero(r);
    return;
  }

  elt_square(t, px);  // t = px*px
  elt_add(s, t, t); // s = 2*px*px
  elt_add(s, s, t); // s = 3*px*px
  elt_add(s, s, ec_a);  // s = 3*px*px + a
  elt_add(t, py, py); // t = 2*py
  elt_inv(t, t);    // t = 1/(2*py)
  elt_mul(s, s, t); // s = (3*px*px+a)/(2*py)

  elt_square(rx, s);  // rx = s*s
  elt_add(t, px, px); // t = 2*px
  elt_sub(rx, rx, t); // rx = s*s - 2*px

  elt_sub(t, px, rx); // t = -(rx-px)
  elt_mul(ry, s, t);  // ry = -s*(rx-px)
  elt_sub(ry, ry, py);  // ry = -s*(rx-px) - py
}

static void point_add(struct point *r, struct point *p, struct point *q)
{
  u8 s[20], t[20], u[20];
  u8 *px, *py, *qx, *qy, *rx, *ry;
  struct point pp, qq;

  pp = *p;
  qq = *q;

  px = pp.x;
  py = pp.y;
  qx = qq.x;
  qy = qq.y;
  rx = r->x;
  ry = r->y;

  if (point_is_zero(&pp)) {
    elt_copy(rx, qx);
    elt_copy(ry, qy);
    return;
  }

  if (point_is_zero(&qq)) {
    elt_copy(rx, px);
    elt_copy(ry, py);
    return;
  }

  elt_sub(u, qx, px);

  if (elt_is_zero(u)) {
    elt_sub(u, qy, py);
    if (elt_is_zero(u))
      point_double(r, &pp);
    else
      point_zero(r);

    return;
  }

  elt_inv(t, u);    // t = 1/(qx-px)
  elt_sub(u, qy, py); // u = qy-py
  elt_mul(s, t, u); // s = (qy-py)/(qx-px)

  elt_square(rx, s);  // rx = s*s
  elt_add(t, px, qx); // t = px+qx
  elt_sub(rx, rx, t); // rx = s*s - (px+qx)

  elt_sub(t, px, rx); // t = -(rx-px)
  elt_mul(ry, s, t);  // ry = -s*(rx-px)
  elt_sub(ry, ry, py);  // ry = -s*(rx-px) - py
}

static void point_mul(struct point *d, u8 *a, struct point *b)  // a is bignum
{
  u32 i;
  u8 mask;

  point_zero(d);

  for (i = 0; i < 21; i++)
    for (mask = 0x80; mask != 0; mask >>= 1) {
      point_double(d, d);
      if ((a[i] & mask) != 0)
        point_add(d, d, b);
    }
}
// Modified from original to support kirk engine use - July 2011
// Added call to Kirk Random number generator rather than /dev/random

static void generate_ecdsa(u8 *outR, u8 *outS, u8 *k, u8 *hash)
{
  u8 e[21];
  u8 kk[21];
  u8 m[21];
  u8 R[21];
  u8 S[21];
  u8 minv[21];
  struct point mG;

  e[0] = 0;R[0] = 0;S[0] = 0;
  memcpy(e + 1, hash, 20);
  bn_reduce(e, ec_N, 21);

  // Original removed for portability
//try_again:
  //fp = fopen("/dev/random", "rb");
  //if (fread(m, sizeof m, 1, fp) != 1)
    //fail("reading random");
  //fclose(fp);
  //m[0] = 0;
  //if (bn_compare(m, ec_N, 21) >= 0)
    //goto try_again;

  //  R = (mG).x
  
  // Added call back to kirk PRNG - July 2011
  kirk_CMD14(m+1, 20);
  m[0] = 0;
  
  point_mul(&mG, m, &ec_G);
  point_from_mon(&mG);
  R[0] = 0;
  elt_copy(R+1, mG.x);

  //  S = m**-1*(e + Rk) (mod N)

  bn_copy(kk, k, 21);
  bn_reduce(kk, ec_N, 21);
  bn_to_mon(m, ec_N, 21);
  bn_to_mon(e, ec_N, 21);
  bn_to_mon(R, ec_N, 21);
  bn_to_mon(kk, ec_N, 21);

  bn_mon_mul(S, R, kk, ec_N, 21);
  bn_add(kk, S, e, ec_N, 21);
  bn_mon_inv(minv, m, ec_N, 21);
  bn_mon_mul(S, minv, kk, ec_N, 21);

  bn_from_mon(R, ec_N, 21);
  bn_from_mon(S, ec_N, 21);
  memcpy(outR,R+1,0x20);
  memcpy(outS,S+1,0x20);
}

    // Signing = 
    // r = k *G;
    // s = x*r+m / k
    
    // Verify =
    // r/s * P = m/s * G

// Slightly modified to support kirk compatible signature input - July 2011
static int check_ecdsa(struct point *Q, u8 *inR, u8 *inS, u8 *hash)
{
  u8 Sinv[21];
  u8 e[21], R[21], S[21];
  u8 w1[21], w2[21];
  struct point r1, r2;
  u8 rr[21];

  e[0] = 0;
  memcpy(e + 1, hash, 20);
  bn_reduce(e, ec_N, 21);
  R[0] = 0;
  memcpy(R + 1, inR, 20);
  bn_reduce(R, ec_N, 21);
  S[0] = 0;
  memcpy(S + 1, inS, 20);
  bn_reduce(S, ec_N, 21);

  bn_to_mon(R, ec_N, 21);
  bn_to_mon(S, ec_N, 21);
  bn_to_mon(e, ec_N, 21);
  // make Sinv = 1/S
  bn_mon_inv(Sinv, S, ec_N, 21);
  // w1 = m * Sinv
  bn_mon_mul(w1, e, Sinv, ec_N, 21);
  // w2 = r * Sinv
  bn_mon_mul(w2, R, Sinv, ec_N, 21);

  // mod N both
  bn_from_mon(w1, ec_N, 21);
  bn_from_mon(w2, ec_N, 21);

  // r1 = m/s * G
  point_mul(&r1, w1, &ec_G);
  // r2 = r/s * P
  point_mul(&r2, w2, Q);

  //r1 = r1 + r2
  point_add(&r1, &r1, &r2);

  point_from_mon(&r1);

  rr[0] = 0;
  memcpy(rr + 1, r1.x, 20);
  bn_reduce(rr, ec_N, 21);

  bn_from_mon(R, ec_N, 21);
  bn_from_mon(S, ec_N, 21);

  return (bn_compare(rr, R, 21) == 0);
}


// Modified from original to support kirk engine use - July 2011
void ec_priv_to_pub(u8 *k, u8 *Q)
{
  struct point ec_temp;
  bn_to_mon(k, ec_N, 21);
  point_mul(&ec_temp, k, &ec_G);
  point_from_mon(&ec_temp); 
  //bn_from_mon(k, ec_N, 21);
  memcpy(Q,ec_temp.x,20);
  memcpy(Q+20,ec_temp.y,20);
}

// Modified from original to support kirk engine use - July 2011
void ec_pub_mult(u8 *k, u8 *Q)
{
  struct point ec_temp;
  //bn_to_mon(k, ec_N, 21);
  point_mul(&ec_temp, k, &ec_Q);
  point_from_mon(&ec_temp);
  //bn_from_mon(k, ec_N, 21);
  memcpy(Q,ec_temp.x,20);
  memcpy(Q+20,ec_temp.y,20);
}


// Simplified for use by Kirk Engine - NO LONGER COMPATIABLE WITH ORIGINAL VERSION - July 2011
int ecdsa_set_curve(u8* p,u8* a,u8* b,u8* N,u8* Gx,u8* Gy)
{
	memcpy(ec_p,p,20);
	memcpy(ec_a,a,20);
	memcpy(ec_b,b,20);
	memcpy(ec_N,N,21);
	
  bn_to_mon(ec_a, ec_p, 20);
  bn_to_mon(ec_b, ec_p, 20);

  memcpy(ec_G.x, Gx, 20);
  memcpy(ec_G.y, Gy, 20);
  point_to_mon(&ec_G);
  
  return 0;
}

void ecdsa_set_pub(u8 *Q)
{
  memcpy(ec_Q.x, Q, 20);
  memcpy(ec_Q.y, Q+20, 20);
  point_to_mon(&ec_Q);
}

void ecdsa_set_priv(u8 *ink)
{
	u8 k[21];
	k[0]=0;
	memcpy(k+1,ink,20);
	bn_reduce(k, ec_N, 21);
	
  memcpy(ec_k, k, sizeof ec_k);
}

int ecdsa_verify(u8 *hash, u8 *R, u8 *S)
{
  return check_ecdsa(&ec_Q, R, S, hash);
}

void ecdsa_sign(u8 *hash, u8 *R, u8 *S)
{
  generate_ecdsa(R, S, ec_k, hash);
}

int point_is_on_curve(u8 *p)
{
  u8 s[20], t[20];
  u8 *x, *y;

  x = p;
  y = p + 20;

  elt_square(t, x);
  elt_mul(s, t, x);// s = x^3

  elt_mul(t, x, ec_a);
  elt_add(s, s, t); //s = x^3 + a *x

  elt_add(s, s, ec_b);//s = x^3 + a *x + b

  elt_square(t, y); //t = y^2
  elt_sub(s, s, t); // is s - t = 0?
  hex_dump("S", s, 20);
  hex_dump("T", t,20);
  return elt_is_zero(s);
}

void dump_ecc(void) {
  hex_dump("P", ec_p, 20);
  hex_dump("a", ec_a, 20);
  hex_dump("b", ec_b, 20);
  hex_dump("N", ec_N, 21);
  hex_dump("Gx", ec_G.x, 20);
  hex_dump("Gy", ec_G.y, 20);
}
