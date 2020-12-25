/*********************************************************************
* Filename:   arcfour_test.c
* Author:     Brad Conte (brad AT bradconte.com)
* Copyright:
* Disclaimer: This code is presented "as is" without any guarantees.
* Details:    Performs known-answer tests on the corresponding ARCFOUR
	          implementation. These tests do not encompass the full
	          range of available test vectors, however, if the tests
	          pass it is very, very likely that the code is correct
	          and was compiled properly. This code also serves as
	          example usage of the functions.
*********************************************************************/

/*************************** HEADER FILES ***************************/
#include <stdio.h>
#include <memory.h>
#include "arcfour.h"

/*********************** FUNCTION DEFINITIONS ***********************/
int rc4_test()
{
	BYTE state[256];
	BYTE key[3][10] = {{"Key"}, {"Wiki"}, {"Secret"}};
	BYTE stream[3][10] = {{0xEB,0x9F,0x77,0x81,0xB7,0x34,0xCA,0x72,0xA7,0x19},
	                      {0x60,0x44,0xdb,0x6d,0x41,0xb7},
	                      {0x04,0xd4,0x6b,0x05,0x3c,0xa8,0x7b,0x59}};
	int stream_len[3] = {10,6,8};
	BYTE buf[1024];
	int idx;
	int pass = 1;

	// Only test the output stream. Note that the state can be reused.
	for (idx = 0; idx < 3; idx++) {
		arcfour_key_setup(state, key[idx], strlen(key[idx]));
		arcfour_generate_stream(state, buf, stream_len[idx]);
		pass = pass && !memcmp(stream[idx], buf, stream_len[idx]);
	}

	return(pass);
}

int main()
{
	printf("ARCFOUR tests: %s\n", rc4_test() ? "SUCCEEDED" : "FAILED");

	return(0);
}
