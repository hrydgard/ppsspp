/*********************************************************************
* Filename:   rot-13_test.c
* Author:     Brad Conte (brad AT bradconte.com)
* Copyright:
* Disclaimer: This code is presented "as is" without any guarantees.
* Details:    Performs known-answer tests on the corresponding ROT-13
	          implementation. These tests do not encompass the full
	          range of available test vectors, however, if the tests
	          pass it is very, very likely that the code is correct
	          and was compiled properly. This code also serves as
	          example usage of the functions.
*********************************************************************/

/*************************** HEADER FILES ***************************/
#include <stdio.h>
#include <string.h>
#include "rot-13.h"

/*********************** FUNCTION DEFINITIONS ***********************/
int rot13_test()
{
	char text[] = {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"};
	char code[] = {"NOPQRSTUVWXYZABCDEFGHIJKLMnopqrstuvwxyzabcdefghijklm"};
	char buf[1024];
	int pass = 1;

	// To encode, just apply ROT-13.
	strcpy(buf, text);
	rot13(buf);
	pass = pass && !strcmp(code, buf);

	// To decode, just re-apply ROT-13.
	rot13(buf);
	pass = pass && !strcmp(text, buf);

	return(pass);
}

int main()
{
	printf("ROT-13 tests: %s\n", rot13_test() ? "SUCCEEDED" : "FAILED");

	return(0);
}
