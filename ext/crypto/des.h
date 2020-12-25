/*********************************************************************
* Filename:   des.h
* Author:     Brad Conte (brad AT bradconte.com)
* Copyright:
* Disclaimer: This code is presented "as is" without any guarantees.
* Details:    Defines the API for the corresponding DES implementation.
              Note that encryption and decryption are defined by how
              the key setup is performed, the actual en/de-cryption is
              performed by the same function.
*********************************************************************/

#ifndef DES_H
#define DESH

/*************************** HEADER FILES ***************************/
#include <stddef.h>

/****************************** MACROS ******************************/
#define DES_BLOCK_SIZE 8                // DES operates on 8 bytes at a time

/**************************** DATA TYPES ****************************/
typedef unsigned char BYTE;             // 8-bit byte
typedef unsigned int  WORD;             // 32-bit word, change to "long" for 16-bit machines

typedef enum {
	DES_ENCRYPT,
	DES_DECRYPT
} DES_MODE;

/*********************** FUNCTION DECLARATIONS **********************/
void des_key_setup(const BYTE key[], BYTE schedule[][6], DES_MODE mode);
void des_crypt(const BYTE in[], BYTE out[], const BYTE key[][6]);

void three_des_key_setup(const BYTE key[], BYTE schedule[][16][6], DES_MODE mode);
void three_des_crypt(const BYTE in[], BYTE out[], const BYTE key[][16][6]);

#endif   // DES_H
