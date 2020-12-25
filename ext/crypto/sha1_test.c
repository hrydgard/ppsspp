/*********************************************************************
* Filename:   sha1_test.c
* Author:     Brad Conte (brad AT bradconte.com)
* Copyright:
* Disclaimer: This code is presented "as is" without any guarantees.
* Details:    Performs known-answer tests on the corresponding SHA1
	          implementation. These tests do not encompass the full
	          range of available test vectors, however, if the tests
	          pass it is very, very likely that the code is correct
	          and was compiled properly. This code also serves as
	          example usage of the functions.
*********************************************************************/

/*************************** HEADER FILES ***************************/
#include <stdio.h>
#include <memory.h>
#include <string.h>
#include "sha1.h"

/*********************** FUNCTION DEFINITIONS ***********************/
int sha1_test()
{
	BYTE text1[] = {"abc"};
	BYTE text2[] = {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"};
	BYTE text3[] = {"aaaaaaaaaa"};
	BYTE hash1[SHA1_BLOCK_SIZE] = {0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,0xba,0x3e,0x25,0x71,0x78,0x50,0xc2,0x6c,0x9c,0xd0,0xd8,0x9d};
	BYTE hash2[SHA1_BLOCK_SIZE] = {0x84,0x98,0x3e,0x44,0x1c,0x3b,0xd2,0x6e,0xba,0xae,0x4a,0xa1,0xf9,0x51,0x29,0xe5,0xe5,0x46,0x70,0xf1};
	BYTE hash3[SHA1_BLOCK_SIZE] = {0x34,0xaa,0x97,0x3c,0xd4,0xc4,0xda,0xa4,0xf6,0x1e,0xeb,0x2b,0xdb,0xad,0x27,0x31,0x65,0x34,0x01,0x6f};
	BYTE buf[SHA1_BLOCK_SIZE];
	int idx;
	SHA1_CTX ctx;
	int pass = 1;

	sha1_init(&ctx);
	sha1_update(&ctx, text1, strlen(text1));
	sha1_final(&ctx, buf);
	pass = pass && !memcmp(hash1, buf, SHA1_BLOCK_SIZE);

	sha1_init(&ctx);
	sha1_update(&ctx, text2, strlen(text2));
	sha1_final(&ctx, buf);
	pass = pass && !memcmp(hash2, buf, SHA1_BLOCK_SIZE);

	sha1_init(&ctx);
	for (idx = 0; idx < 100000; ++idx)
	   sha1_update(&ctx, text3, strlen(text3));
	sha1_final(&ctx, buf);
	pass = pass && !memcmp(hash3, buf, SHA1_BLOCK_SIZE);

	return(pass);
}

int main()
{
	printf("SHA1 tests: %s\n", sha1_test() ? "SUCCEEDED" : "FAILED");

	return(0);
}
