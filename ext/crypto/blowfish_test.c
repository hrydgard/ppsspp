/*********************************************************************
* Filename:   blowfish_test.c
* Author:     Brad Conte (brad AT bradconte.com)
* Copyright:
* Disclaimer: This code is presented "as is" without any guarantees.
* Details:    Performs known-answer tests on the corresponding Blowfish
	          implementation. These tests do not encompass the full
	          range of available test vectors, however, if the tests
	          pass it is very, very likely that the code is correct
	          and was compiled properly. This code also serves as
	          example usage of the functions.
*********************************************************************/

/*************************** HEADER FILES ***************************/
#include <stdio.h>
#include <memory.h>
#include "blowfish.h"

/*********************** FUNCTION DEFINITIONS ***********************/
int blowfish_test()
{
	BYTE key1[8]  = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
	BYTE key2[8]  = {0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
	BYTE key3[24] = {0xF0,0xE1,0xD2,0xC3,0xB4,0xA5,0x96,0x87,
	                 0x78,0x69,0x5A,0x4B,0x3C,0x2D,0x1E,0x0F,
	                 0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77};
	BYTE p1[BLOWFISH_BLOCK_SIZE] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
	BYTE p2[BLOWFISH_BLOCK_SIZE] = {0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
	BYTE p3[BLOWFISH_BLOCK_SIZE] = {0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10};

	BYTE c1[BLOWFISH_BLOCK_SIZE] = {0x4e,0xf9,0x97,0x45,0x61,0x98,0xdd,0x78};
	BYTE c2[BLOWFISH_BLOCK_SIZE] = {0x51,0x86,0x6f,0xd5,0xb8,0x5e,0xcb,0x8a};
	BYTE c3[BLOWFISH_BLOCK_SIZE] = {0x05,0x04,0x4b,0x62,0xfa,0x52,0xd0,0x80};

	BYTE enc_buf[BLOWFISH_BLOCK_SIZE];
	BLOWFISH_KEY key;
	int pass = 1;

	// Test vector 1.
	blowfish_key_setup(key1, &key, BLOWFISH_BLOCK_SIZE);
	blowfish_encrypt(p1, enc_buf, &key);
	pass = pass && !memcmp(c1, enc_buf, BLOWFISH_BLOCK_SIZE);
	blowfish_decrypt(c1, enc_buf, &key);
	pass = pass && !memcmp(p1, enc_buf, BLOWFISH_BLOCK_SIZE);

	// Test vector 2.
	blowfish_key_setup(key2, &key, BLOWFISH_BLOCK_SIZE);
	blowfish_encrypt(p2, enc_buf, &key);
	pass = pass && !memcmp(c2, enc_buf, BLOWFISH_BLOCK_SIZE);
	blowfish_decrypt(c2, enc_buf, &key);
	pass = pass && !memcmp(p2, enc_buf, BLOWFISH_BLOCK_SIZE);

	// Test vector 3.
	blowfish_key_setup(key3, &key, 24);
	blowfish_encrypt(p3, enc_buf, &key);
	pass = pass && !memcmp(c3, enc_buf, BLOWFISH_BLOCK_SIZE);
	blowfish_decrypt(c3, enc_buf, &key);
	pass = pass && !memcmp(p3, enc_buf, BLOWFISH_BLOCK_SIZE);

	return(pass);
}

int main()
{
	printf("Blowfish tests: %s\n", blowfish_test() ? "SUCCEEDED" : "FAILED");

	return(0);
}
