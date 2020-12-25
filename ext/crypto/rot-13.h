/*********************************************************************
* Filename:   rot-13.h
* Author:     Brad Conte (brad AT bradconte.com)
* Copyright:
* Disclaimer: This code is presented "as is" without any guarantees.
* Details:    Defines the API for the corresponding ROT-13 implementation.
*********************************************************************/

#ifndef ROT13_H
#define ROT13_H

/*************************** HEADER FILES ***************************/
#include <stddef.h>

/*********************** FUNCTION DECLARATIONS **********************/
// Performs IN PLACE rotation of the input. Assumes input is NULL terminated.
// Preserves each charcter's case. Ignores non alphabetic characters.
void rot13(char str[]);

#endif   // ROT13_H
