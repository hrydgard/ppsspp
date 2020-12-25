/*********************************************************************
* Filename:   des.c
* Author:     Brad Conte (brad AT radconte.com)
* Copyright:
* Disclaimer: This code is presented "as is" without any guarantees.
* Details:    Implementation of the DES encryption algorithm.
              Modes of operation (such as CBC) are not included.
              The formal NIST algorithm specification can be found here:
               * http://csrc.nist.gov/publications/fips/fips46-3/fips46-3.pdf
*********************************************************************/

/*************************** HEADER FILES ***************************/
#include <stdlib.h>
#include <memory.h>
#include "des.h"

/****************************** MACROS ******************************/
// Obtain bit "b" from the left and shift it "c" places from the right
#define BITNUM(a,b,c) (((a[(b)/8] >> (7 - (b%8))) & 0x01) << (c))
#define BITNUMINTR(a,b,c) ((((a) >> (31 - (b))) & 0x00000001) << (c))
#define BITNUMINTL(a,b,c) ((((a) << (b)) & 0x80000000) >> (c))

// This macro converts a 6 bit block with the S-Box row defined as the first and last
// bits to a 6 bit block with the row defined by the first two bits.
#define SBOXBIT(a) (((a) & 0x20) | (((a) & 0x1f) >> 1) | (((a) & 0x01) << 4))

/**************************** VARIABLES *****************************/
static const BYTE sbox1[64] = {
	14,  4,  13,  1,   2, 15,  11,  8,   3, 10,   6, 12,   5,  9,   0,  7,
	 0, 15,   7,  4,  14,  2,  13,  1,  10,  6,  12, 11,   9,  5,   3,  8,
	 4,  1,  14,  8,  13,  6,   2, 11,  15, 12,   9,  7,   3, 10,   5,  0,
	15, 12,   8,  2,   4,  9,   1,  7,   5, 11,   3, 14,  10,  0,   6, 13
};
static const BYTE sbox2[64] = {
	15,  1,   8, 14,   6, 11,   3,  4,   9,  7,   2, 13,  12,  0,   5, 10,
	 3, 13,   4,  7,  15,  2,   8, 14,  12,  0,   1, 10,   6,  9,  11,  5,
	 0, 14,   7, 11,  10,  4,  13,  1,   5,  8,  12,  6,   9,  3,   2, 15,
	13,  8,  10,  1,   3, 15,   4,  2,  11,  6,   7, 12,   0,  5,  14,  9
};
static const BYTE sbox3[64] = {
	10,  0,   9, 14,   6,  3,  15,  5,   1, 13,  12,  7,  11,  4,   2,  8,
	13,  7,   0,  9,   3,  4,   6, 10,   2,  8,   5, 14,  12, 11,  15,  1,
	13,  6,   4,  9,   8, 15,   3,  0,  11,  1,   2, 12,   5, 10,  14,  7,
	 1, 10,  13,  0,   6,  9,   8,  7,   4, 15,  14,  3,  11,  5,   2, 12
};
static const BYTE sbox4[64] = {
	 7, 13,  14,  3,   0,  6,   9, 10,   1,  2,   8,  5,  11, 12,   4, 15,
	13,  8,  11,  5,   6, 15,   0,  3,   4,  7,   2, 12,   1, 10,  14,  9,
	10,  6,   9,  0,  12, 11,   7, 13,  15,  1,   3, 14,   5,  2,   8,  4,
	 3, 15,   0,  6,  10,  1,  13,  8,   9,  4,   5, 11,  12,  7,   2, 14
};
static const BYTE sbox5[64] = {
	 2, 12,   4,  1,   7, 10,  11,  6,   8,  5,   3, 15,  13,  0,  14,  9,
	14, 11,   2, 12,   4,  7,  13,  1,   5,  0,  15, 10,   3,  9,   8,  6,
	 4,  2,   1, 11,  10, 13,   7,  8,  15,  9,  12,  5,   6,  3,   0, 14,
	11,  8,  12,  7,   1, 14,   2, 13,   6, 15,   0,  9,  10,  4,   5,  3
};
static const BYTE sbox6[64] = {
	12,  1,  10, 15,   9,  2,   6,  8,   0, 13,   3,  4,  14,  7,   5, 11,
	10, 15,   4,  2,   7, 12,   9,  5,   6,  1,  13, 14,   0, 11,   3,  8,
	 9, 14,  15,  5,   2,  8,  12,  3,   7,  0,   4, 10,   1, 13,  11,  6,
	 4,  3,   2, 12,   9,  5,  15, 10,  11, 14,   1,  7,   6,  0,   8, 13
};
static const BYTE sbox7[64] = {
	 4, 11,   2, 14,  15,  0,   8, 13,   3, 12,   9,  7,   5, 10,   6,  1,
	13,  0,  11,  7,   4,  9,   1, 10,  14,  3,   5, 12,   2, 15,   8,  6,
	 1,  4,  11, 13,  12,  3,   7, 14,  10, 15,   6,  8,   0,  5,   9,  2,
	 6, 11,  13,  8,   1,  4,  10,  7,   9,  5,   0, 15,  14,  2,   3, 12
};
static const BYTE sbox8[64] = {
	13,  2,   8,  4,   6, 15,  11,  1,  10,  9,   3, 14,   5,  0,  12,  7,
	 1, 15,  13,  8,  10,  3,   7,  4,  12,  5,   6, 11,   0, 14,   9,  2,
	 7, 11,   4,  1,   9, 12,  14,  2,   0,  6,  10, 13,  15,  3,   5,  8,
	 2,  1,  14,  7,   4, 10,   8, 13,  15, 12,   9,  0,   3,  5,   6, 11
};

/*********************** FUNCTION DEFINITIONS ***********************/
// Initial (Inv)Permutation step
void IP(WORD state[], const BYTE in[])
{
	state[0] = BITNUM(in,57,31) | BITNUM(in,49,30) | BITNUM(in,41,29) | BITNUM(in,33,28) |
				  BITNUM(in,25,27) | BITNUM(in,17,26) | BITNUM(in,9,25) | BITNUM(in,1,24) |
				  BITNUM(in,59,23) | BITNUM(in,51,22) | BITNUM(in,43,21) | BITNUM(in,35,20) |
				  BITNUM(in,27,19) | BITNUM(in,19,18) | BITNUM(in,11,17) | BITNUM(in,3,16) |
				  BITNUM(in,61,15) | BITNUM(in,53,14) | BITNUM(in,45,13) | BITNUM(in,37,12) |
				  BITNUM(in,29,11) | BITNUM(in,21,10) | BITNUM(in,13,9) | BITNUM(in,5,8) |
				  BITNUM(in,63,7) | BITNUM(in,55,6) | BITNUM(in,47,5) | BITNUM(in,39,4) |
				  BITNUM(in,31,3) | BITNUM(in,23,2) | BITNUM(in,15,1) | BITNUM(in,7,0);

	state[1] = BITNUM(in,56,31) | BITNUM(in,48,30) | BITNUM(in,40,29) | BITNUM(in,32,28) |
				  BITNUM(in,24,27) | BITNUM(in,16,26) | BITNUM(in,8,25) | BITNUM(in,0,24) |
				  BITNUM(in,58,23) | BITNUM(in,50,22) | BITNUM(in,42,21) | BITNUM(in,34,20) |
				  BITNUM(in,26,19) | BITNUM(in,18,18) | BITNUM(in,10,17) | BITNUM(in,2,16) |
				  BITNUM(in,60,15) | BITNUM(in,52,14) | BITNUM(in,44,13) | BITNUM(in,36,12) |
				  BITNUM(in,28,11) | BITNUM(in,20,10) | BITNUM(in,12,9) | BITNUM(in,4,8) |
				  BITNUM(in,62,7) | BITNUM(in,54,6) | BITNUM(in,46,5) | BITNUM(in,38,4) |
				  BITNUM(in,30,3) | BITNUM(in,22,2) | BITNUM(in,14,1) | BITNUM(in,6,0);
}

void InvIP(WORD state[], BYTE in[])
{
	in[0] = BITNUMINTR(state[1],7,7) | BITNUMINTR(state[0],7,6) | BITNUMINTR(state[1],15,5) |
			  BITNUMINTR(state[0],15,4) | BITNUMINTR(state[1],23,3) | BITNUMINTR(state[0],23,2) |
			  BITNUMINTR(state[1],31,1) | BITNUMINTR(state[0],31,0);

	in[1] = BITNUMINTR(state[1],6,7) | BITNUMINTR(state[0],6,6) | BITNUMINTR(state[1],14,5) |
			  BITNUMINTR(state[0],14,4) | BITNUMINTR(state[1],22,3) | BITNUMINTR(state[0],22,2) |
			  BITNUMINTR(state[1],30,1) | BITNUMINTR(state[0],30,0);

	in[2] = BITNUMINTR(state[1],5,7) | BITNUMINTR(state[0],5,6) | BITNUMINTR(state[1],13,5) |
			  BITNUMINTR(state[0],13,4) | BITNUMINTR(state[1],21,3) | BITNUMINTR(state[0],21,2) |
			  BITNUMINTR(state[1],29,1) | BITNUMINTR(state[0],29,0);

	in[3] = BITNUMINTR(state[1],4,7) | BITNUMINTR(state[0],4,6) | BITNUMINTR(state[1],12,5) |
			  BITNUMINTR(state[0],12,4) | BITNUMINTR(state[1],20,3) | BITNUMINTR(state[0],20,2) |
			  BITNUMINTR(state[1],28,1) | BITNUMINTR(state[0],28,0);

	in[4] = BITNUMINTR(state[1],3,7) | BITNUMINTR(state[0],3,6) | BITNUMINTR(state[1],11,5) |
			  BITNUMINTR(state[0],11,4) | BITNUMINTR(state[1],19,3) | BITNUMINTR(state[0],19,2) |
			  BITNUMINTR(state[1],27,1) | BITNUMINTR(state[0],27,0);

	in[5] = BITNUMINTR(state[1],2,7) | BITNUMINTR(state[0],2,6) | BITNUMINTR(state[1],10,5) |
			  BITNUMINTR(state[0],10,4) | BITNUMINTR(state[1],18,3) | BITNUMINTR(state[0],18,2) |
			  BITNUMINTR(state[1],26,1) | BITNUMINTR(state[0],26,0);

	in[6] = BITNUMINTR(state[1],1,7) | BITNUMINTR(state[0],1,6) | BITNUMINTR(state[1],9,5) |
			  BITNUMINTR(state[0],9,4) | BITNUMINTR(state[1],17,3) | BITNUMINTR(state[0],17,2) |
			  BITNUMINTR(state[1],25,1) | BITNUMINTR(state[0],25,0);

	in[7] = BITNUMINTR(state[1],0,7) | BITNUMINTR(state[0],0,6) | BITNUMINTR(state[1],8,5) |
			  BITNUMINTR(state[0],8,4) | BITNUMINTR(state[1],16,3) | BITNUMINTR(state[0],16,2) |
			  BITNUMINTR(state[1],24,1) | BITNUMINTR(state[0],24,0);
}

WORD f(WORD state, const BYTE key[])
{
	BYTE lrgstate[6]; //,i;
	WORD t1,t2;

	// Expantion Permutation
	t1 = BITNUMINTL(state,31,0) | ((state & 0xf0000000) >> 1) | BITNUMINTL(state,4,5) |
		  BITNUMINTL(state,3,6) | ((state & 0x0f000000) >> 3) | BITNUMINTL(state,8,11) |
		  BITNUMINTL(state,7,12) | ((state & 0x00f00000) >> 5) | BITNUMINTL(state,12,17) |
		  BITNUMINTL(state,11,18) | ((state & 0x000f0000) >> 7) | BITNUMINTL(state,16,23);

	t2 = BITNUMINTL(state,15,0) | ((state & 0x0000f000) << 15) | BITNUMINTL(state,20,5) |
		  BITNUMINTL(state,19,6) | ((state & 0x00000f00) << 13) | BITNUMINTL(state,24,11) |
		  BITNUMINTL(state,23,12) | ((state & 0x000000f0) << 11) | BITNUMINTL(state,28,17) |
		  BITNUMINTL(state,27,18) | ((state & 0x0000000f) << 9) | BITNUMINTL(state,0,23);

	lrgstate[0] = (t1 >> 24) & 0x000000ff;
	lrgstate[1] = (t1 >> 16) & 0x000000ff;
	lrgstate[2] = (t1 >> 8) & 0x000000ff;
	lrgstate[3] = (t2 >> 24) & 0x000000ff;
	lrgstate[4] = (t2 >> 16) & 0x000000ff;
	lrgstate[5] = (t2 >> 8) & 0x000000ff;

	// Key XOR
	lrgstate[0] ^= key[0];
	lrgstate[1] ^= key[1];
	lrgstate[2] ^= key[2];
	lrgstate[3] ^= key[3];
	lrgstate[4] ^= key[4];
	lrgstate[5] ^= key[5];

	// S-Box Permutation
	state = (sbox1[SBOXBIT(lrgstate[0] >> 2)] << 28) |
			  (sbox2[SBOXBIT(((lrgstate[0] & 0x03) << 4) | (lrgstate[1] >> 4))] << 24) |
			  (sbox3[SBOXBIT(((lrgstate[1] & 0x0f) << 2) | (lrgstate[2] >> 6))] << 20) |
			  (sbox4[SBOXBIT(lrgstate[2] & 0x3f)] << 16) |
			  (sbox5[SBOXBIT(lrgstate[3] >> 2)] << 12) |
			  (sbox6[SBOXBIT(((lrgstate[3] & 0x03) << 4) | (lrgstate[4] >> 4))] << 8) |
			  (sbox7[SBOXBIT(((lrgstate[4] & 0x0f) << 2) | (lrgstate[5] >> 6))] << 4) |
				sbox8[SBOXBIT(lrgstate[5] & 0x3f)];

	// P-Box Permutation
	state = BITNUMINTL(state,15,0) | BITNUMINTL(state,6,1) | BITNUMINTL(state,19,2) |
			  BITNUMINTL(state,20,3) | BITNUMINTL(state,28,4) | BITNUMINTL(state,11,5) |
			  BITNUMINTL(state,27,6) | BITNUMINTL(state,16,7) | BITNUMINTL(state,0,8) |
			  BITNUMINTL(state,14,9) | BITNUMINTL(state,22,10) | BITNUMINTL(state,25,11) |
			  BITNUMINTL(state,4,12) | BITNUMINTL(state,17,13) | BITNUMINTL(state,30,14) |
			  BITNUMINTL(state,9,15) | BITNUMINTL(state,1,16) | BITNUMINTL(state,7,17) |
			  BITNUMINTL(state,23,18) | BITNUMINTL(state,13,19) | BITNUMINTL(state,31,20) |
			  BITNUMINTL(state,26,21) | BITNUMINTL(state,2,22) | BITNUMINTL(state,8,23) |
			  BITNUMINTL(state,18,24) | BITNUMINTL(state,12,25) | BITNUMINTL(state,29,26) |
			  BITNUMINTL(state,5,27) | BITNUMINTL(state,21,28) | BITNUMINTL(state,10,29) |
			  BITNUMINTL(state,3,30) | BITNUMINTL(state,24,31);

	// Return the final state value
	return(state);
}

void des_key_setup(const BYTE key[], BYTE schedule[][6], DES_MODE mode)
{
	WORD i, j, to_gen, C, D;
	const WORD key_rnd_shift[16] = {1,1,2,2,2,2,2,2,1,2,2,2,2,2,2,1};
	const WORD key_perm_c[28] = {56,48,40,32,24,16,8,0,57,49,41,33,25,17,
	                             9,1,58,50,42,34,26,18,10,2,59,51,43,35};
	const WORD key_perm_d[28] = {62,54,46,38,30,22,14,6,61,53,45,37,29,21,
	                             13,5,60,52,44,36,28,20,12,4,27,19,11,3};
	const WORD key_compression[48] = {13,16,10,23,0,4,2,27,14,5,20,9,
	                                  22,18,11,3,25,7,15,6,26,19,12,1,
	                                  40,51,30,36,46,54,29,39,50,44,32,47,
	                                  43,48,38,55,33,52,45,41,49,35,28,31};

	// Permutated Choice #1 (copy the key in, ignoring parity bits).
	for (i = 0, j = 31, C = 0; i < 28; ++i, --j)
		C |= BITNUM(key,key_perm_c[i],j);
	for (i = 0, j = 31, D = 0; i < 28; ++i, --j)
		D |= BITNUM(key,key_perm_d[i],j);

	// Generate the 16 subkeys.
	for (i = 0; i < 16; ++i) {
		C = ((C << key_rnd_shift[i]) | (C >> (28-key_rnd_shift[i]))) & 0xfffffff0;
		D = ((D << key_rnd_shift[i]) | (D >> (28-key_rnd_shift[i]))) & 0xfffffff0;

		// Decryption subkeys are reverse order of encryption subkeys so
		// generate them in reverse if the key schedule is for decryption useage.
		if (mode == DES_DECRYPT)
			to_gen = 15 - i;
		else /*(if mode == DES_ENCRYPT)*/
			to_gen = i;
		// Initialize the array
		for (j = 0; j < 6; ++j)
			schedule[to_gen][j] = 0;
		for (j = 0; j < 24; ++j)
			schedule[to_gen][j/8] |= BITNUMINTR(C,key_compression[j],7 - (j%8));
		for ( ; j < 48; ++j)
			schedule[to_gen][j/8] |= BITNUMINTR(D,key_compression[j] - 28,7 - (j%8));
	}
}

void des_crypt(const BYTE in[], BYTE out[], const BYTE key[][6])
{
	WORD state[2],idx,t;

	IP(state,in);

	for (idx=0; idx < 15; ++idx) {
		t = state[1];
		state[1] = f(state[1],key[idx]) ^ state[0];
		state[0] = t;
	}
	// Perform the final loop manually as it doesn't switch sides
	state[0] = f(state[1],key[15]) ^ state[0];

	InvIP(state,out);
}

void three_des_key_setup(const BYTE key[], BYTE schedule[][16][6], DES_MODE mode)
{
	if (mode == DES_ENCRYPT) {
		des_key_setup(&key[0],schedule[0],mode);
		des_key_setup(&key[8],schedule[1],!mode);
		des_key_setup(&key[16],schedule[2],mode);
	}
	else /*if (mode == DES_DECRYPT*/ {
		des_key_setup(&key[16],schedule[0],mode);
		des_key_setup(&key[8],schedule[1],!mode);
		des_key_setup(&key[0],schedule[2],mode);
	}
}

void three_des_crypt(const BYTE in[], BYTE out[], const BYTE key[][16][6])
{
	des_crypt(in,out,key[0]);
	des_crypt(out,out,key[1]);
	des_crypt(out,out,key[2]);
}
