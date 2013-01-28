/*
  Draan proudly presents:
  
  With huge help from community:
  coyotebean, Davee, hitchhikr, kgsws, liquidzigong, Mathieulh, Proxima, SilverSpring
  
  ******************** KIRK-ENGINE ********************
  An Open-Source implementation of KIRK (PSP crypto engine) algorithms and keys.
  Includes also additional routines for hash forging.
  
  ********************
  
  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "kirk_engine.h"
#include "AES.h"
#include "SHA1.h"

/* ------------------------- KEY VAULT ------------------------- */

u8 kirk1_key[] =   {0x98, 0xC9, 0x40, 0x97, 0x5C, 0x1D, 0x10, 0xE8, 0x7F, 0xE6, 0x0E, 0xA3, 0xFD, 0x03, 0xA8, 0xBA};
u8 kirk7_key02[] = {0xB8, 0x13, 0xC3, 0x5E, 0xC6, 0x44, 0x41, 0xE3, 0xDC, 0x3C, 0x16, 0xF5, 0xB4, 0x5E, 0x64, 0x84}; // New from PS3
u8 kirk7_key03[] = {0x98, 0x02, 0xC4, 0xE6, 0xEC, 0x9E, 0x9E, 0x2F, 0xFC, 0x63, 0x4C, 0xE4, 0x2F, 0xBB, 0x46, 0x68};
u8 kirk7_key04[] = {0x99, 0x24, 0x4C, 0xD2, 0x58, 0xF5, 0x1B, 0xCB, 0xB0, 0x61, 0x9C, 0xA7, 0x38, 0x30, 0x07, 0x5F};
u8 kirk7_key05[] = {0x02, 0x25, 0xD7, 0xBA, 0x63, 0xEC, 0xB9, 0x4A, 0x9D, 0x23, 0x76, 0x01, 0xB3, 0xF6, 0xAC, 0x17};
u8 kirk7_key07[] = {0x76, 0x36, 0x8B, 0x43, 0x8F, 0x77, 0xD8, 0x7E, 0xFE, 0x5F, 0xB6, 0x11, 0x59, 0x39, 0x88, 0x5C}; // New from PS3
u8 kirk7_key0C[] = {0x84, 0x85, 0xC8, 0x48, 0x75, 0x08, 0x43, 0xBC, 0x9B, 0x9A, 0xEC, 0xA7, 0x9C, 0x7F, 0x60, 0x18};
u8 kirk7_key0D[] = {0xB5, 0xB1, 0x6E, 0xDE, 0x23, 0xA9, 0x7B, 0x0E, 0xA1, 0x7C, 0xDB, 0xA2, 0xDC, 0xDE, 0xC4, 0x6E};
u8 kirk7_key0E[] = {0xC8, 0x71, 0xFD, 0xB3, 0xBC, 0xC5, 0xD2, 0xF2, 0xE2, 0xD7, 0x72, 0x9D, 0xDF, 0x82, 0x68, 0x82};
u8 kirk7_key0F[] = {0x0A, 0xBB, 0x33, 0x6C, 0x96, 0xD4, 0xCD, 0xD8, 0xCB, 0x5F, 0x4B, 0xE0, 0xBA, 0xDB, 0x9E, 0x03};
u8 kirk7_key10[] = {0x32, 0x29, 0x5B, 0xD5, 0xEA, 0xF7, 0xA3, 0x42, 0x16, 0xC8, 0x8E, 0x48, 0xFF, 0x50, 0xD3, 0x71};
u8 kirk7_key11[] = {0x46, 0xF2, 0x5E, 0x8E, 0x4D, 0x2A, 0xA5, 0x40, 0x73, 0x0B, 0xC4, 0x6E, 0x47, 0xEE, 0x6F, 0x0A};
u8 kirk7_key12[] = {0x5D, 0xC7, 0x11, 0x39, 0xD0, 0x19, 0x38, 0xBC, 0x02, 0x7F, 0xDD, 0xDC, 0xB0, 0x83, 0x7D, 0x9D};
u8 kirk7_key38[] = {0x12, 0x46, 0x8D, 0x7E, 0x1C, 0x42, 0x20, 0x9B, 0xBA, 0x54, 0x26, 0x83, 0x5E, 0xB0, 0x33, 0x03};
u8 kirk7_key39[] = {0xC4, 0x3B, 0xB6, 0xD6, 0x53, 0xEE, 0x67, 0x49, 0x3E, 0xA9, 0x5F, 0xBC, 0x0C, 0xED, 0x6F, 0x8A};
u8 kirk7_key3A[] = {0x2C, 0xC3, 0xCF, 0x8C, 0x28, 0x78, 0xA5, 0xA6, 0x63, 0xE2, 0xAF, 0x2D, 0x71, 0x5E, 0x86, 0xBA};
u8 kirk7_key44[] = {0x7D, 0xF4, 0x92, 0x65, 0xE3, 0xFA, 0xD6, 0x78, 0xD6, 0xFE, 0x78, 0xAD, 0xBB, 0x3D, 0xFB, 0x63};  // New from PS3
u8 kirk7_key4B[] = {0x0C, 0xFD, 0x67, 0x9A, 0xF9, 0xB4, 0x72, 0x4F, 0xD7, 0x8D, 0xD6, 0xE9, 0x96, 0x42, 0x28, 0x8B}; //1.xx game eboot.bin
u8 kirk7_key53[] = {0xAF, 0xFE, 0x8E, 0xB1, 0x3D, 0xD1, 0x7E, 0xD8, 0x0A, 0x61, 0x24, 0x1C, 0x95, 0x92, 0x56, 0xB6};
u8 kirk7_key57[] = {0x1C, 0x9B, 0xC4, 0x90, 0xE3, 0x06, 0x64, 0x81, 0xFA, 0x59, 0xFD, 0xB6, 0x00, 0xBB, 0x28, 0x70};
u8 kirk7_key5D[] = {0x11, 0x5A, 0x5D, 0x20, 0xD5, 0x3A, 0x8D, 0xD3, 0x9C, 0xC5, 0xAF, 0x41, 0x0F, 0x0F, 0x18, 0x6F};
u8 kirk7_key63[] = {0x9C, 0x9B, 0x13, 0x72, 0xF8, 0xC6, 0x40, 0xCF, 0x1C, 0x62, 0xF5, 0xD5, 0x92, 0xDD, 0xB5, 0x82};
u8 kirk7_key64[] = {0x03, 0xB3, 0x02, 0xE8, 0x5F, 0xF3, 0x81, 0xB1, 0x3B, 0x8D, 0xAA, 0x2A, 0x90, 0xFF, 0x5E, 0x61};

u8 kirk16_key[]  = {0x47, 0x5E, 0x09, 0xF4, 0xA2, 0x37, 0xDA, 0x9B, 0xEF, 0xFF, 0x3B, 0xC0, 0x77, 0x14, 0x3D, 0x8A};

/* ECC Curves for Kirk 1 and Kirk 0x11 */
// Common Curve paramters p and a
static u8 ec_p[20] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static u8 ec_a[20] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC}; // mon

// Kirk 0xC,0xD,0x10,0x11,(likely 0x12)- Unique curve parameters for b, N, and base point G for Kirk 0xC,0xD,0x10,0x11,(likely 0x12) service
// Since public key is variable, it is not specified here
static u8 ec_b2[20] = {0xA6, 0x8B, 0xED, 0xC3, 0x34, 0x18, 0x02, 0x9C, 0x1D, 0x3C, 0xE3, 0x3B, 0x9A, 0x32, 0x1F, 0xCC, 0xBB, 0x9E, 0x0F, 0x0B};// mon
static u8 ec_N2[21] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xB5, 0xAE, 0x3C, 0x52, 0x3E, 0x63, 0x94, 0x4F, 0x21, 0x27};
static u8 Gx2[20] = {0x12, 0x8E, 0xC4, 0x25, 0x64, 0x87, 0xFD, 0x8F, 0xDF, 0x64, 0xE2, 0x43, 0x7B, 0xC0, 0xA1, 0xF6, 0xD5, 0xAF, 0xDE, 0x2C };
static u8 Gy2[20] = {0x59, 0x58, 0x55, 0x7E, 0xB1, 0xDB, 0x00, 0x12, 0x60, 0x42, 0x55, 0x24, 0xDB, 0xC3, 0x79, 0xD5, 0xAC, 0x5F, 0x4A, 0xDF };

// KIRK 1 - Unique curve parameters for b, N, and base point G
// Since public key is hard coded, it is also included

static u8 ec_b1[20] = {0x65, 0xD1, 0x48, 0x8C, 0x03, 0x59, 0xE2, 0x34, 0xAD, 0xC9, 0x5B, 0xD3, 0x90, 0x80, 0x14, 0xBD, 0x91, 0xA5, 0x25, 0xF9};
static u8 ec_N1[21] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x01, 0xB5, 0xC6, 0x17, 0xF2, 0x90, 0xEA, 0xE1, 0xDB, 0xAD, 0x8F};
static u8 Gx1[20] = {0x22, 0x59, 0xAC, 0xEE, 0x15, 0x48, 0x9C, 0xB0, 0x96, 0xA8, 0x82, 0xF0, 0xAE, 0x1C, 0xF9, 0xFD, 0x8E, 0xE5, 0xF8, 0xFA };
static u8 Gy1[20] = {0x60, 0x43, 0x58, 0x45, 0x6D, 0x0A, 0x1C, 0xB2, 0x90, 0x8D, 0xE9, 0x0F, 0x27, 0xD7, 0x5C, 0x82, 0xBE, 0xC1, 0x08, 0xC0 };

static u8 Px1[20] = {0xED, 0x9C, 0xE5, 0x82, 0x34, 0xE6, 0x1A, 0x53, 0xC6, 0x85, 0xD6, 0x4D, 0x51, 0xD0, 0x23, 0x6B, 0xC3, 0xB5, 0xD4, 0xB9 };
static u8 Py1[20] = {0x04, 0x9D, 0xF1, 0xA0, 0x75, 0xC0, 0xE0, 0x4F, 0xB3, 0x44, 0x85, 0x8B, 0x61, 0xB7, 0x9B, 0x69, 0xA6, 0x3D, 0x2C, 0x39 };

/* ------------------------- KEY VAULT END ------------------------- */

/* ------------------------- INTERNAL STUFF ------------------------- */
typedef struct blah
{
  u8 fuseid[8]; //0
  u8 mesh[0x40];  //0x8
} kirk16_data; //0x48
 
typedef struct header_keys
{
  u8 AES[16];
  u8 CMAC[16];
}header_keys;  //small struct for temporary keeping AES & CMAC key from CMD1 header


u32 g_fuse90;  // This is to match FuseID HW at BC100090 and BC100094
u32 g_fuse94;

AES_ctx aes_kirk1; //global
u8 PRNG_DATA[0x14];

char is_kirk_initialized; //"init" emulation

/* ------------------------- INTERNAL STUFF END ------------------------- */


/* ------------------------- IMPLEMENTATION ------------------------- */

int kirk_CMD0(u8* outbuff, u8* inbuff, int size, int generate_trash)
{
  KIRK_CMD1_HEADER* header = (KIRK_CMD1_HEADER*)outbuff;
  header_keys *keys = (header_keys *)outbuff; //0-15 AES key, 16-31 CMAC key
  int chk_size;
  AES_ctx k1;
  AES_ctx cmac_key;
  u8 cmac_header_hash[16];
  u8 cmac_data_hash[16];
    
  if(is_kirk_initialized == 0) return KIRK_NOT_INITIALIZED;

  memcpy(outbuff, inbuff, size);
   
  if(header->mode != KIRK_MODE_CMD1) return KIRK_INVALID_MODE;
  
  //FILL PREDATA WITH RANDOM DATA
  if(generate_trash) kirk_CMD14(outbuff+sizeof(KIRK_CMD1_HEADER), header->data_offset);
  
  //Make sure data is 16 aligned
  chk_size = header->data_size;
  if(chk_size % 16) chk_size += 16 - (chk_size % 16);
  
  //ENCRYPT DATA
  AES_set_key(&k1, keys->AES, 128);
  AES_cbc_encrypt(&k1, inbuff+sizeof(KIRK_CMD1_HEADER)+header->data_offset, (u8*)outbuff+sizeof(KIRK_CMD1_HEADER)+header->data_offset, chk_size);
  
  //CMAC HASHES
  AES_set_key(&cmac_key, keys->CMAC, 128);
  AES_CMAC(&cmac_key, outbuff+0x60, 0x30, cmac_header_hash);
  AES_CMAC(&cmac_key, outbuff+0x60, 0x30 + chk_size + header->data_offset, cmac_data_hash);
  
  memcpy(header->CMAC_header_hash, cmac_header_hash, 16);
  memcpy(header->CMAC_data_hash, cmac_data_hash, 16);
  
  //ENCRYPT KEYS
  AES_cbc_encrypt(&aes_kirk1, inbuff, outbuff, 16*2);
  return KIRK_OPERATION_SUCCESS;
}

int kirk_CMD1(u8* outbuff, u8* inbuff, int size)
{
  KIRK_CMD1_HEADER* header = (KIRK_CMD1_HEADER*)inbuff;
  header_keys keys; //0-15 AES key, 16-31 CMAC key
  AES_ctx k1;
	
	if(size < 0x90) return KIRK_INVALID_SIZE;
  if(is_kirk_initialized == 0) return KIRK_NOT_INITIALIZED;
  if(header->mode != KIRK_MODE_CMD1) return KIRK_INVALID_MODE;
  
  AES_cbc_decrypt(&aes_kirk1, inbuff, (u8*)&keys, 16*2); //decrypt AES & CMAC key to temp buffer
  
  if(header->ecdsa_hash == 1)
  {
  	SHA_CTX sha;
  	KIRK_CMD1_ECDSA_HEADER* eheader = (KIRK_CMD1_ECDSA_HEADER*) inbuff;
  	u8 kirk1_pub[40];
  	u8 header_hash[20];u8 data_hash[20];
  	ecdsa_set_curve(ec_p,ec_a,ec_b1,ec_N1,Gx1,Gy1);
  	memcpy(kirk1_pub,Px1,20);
  	memcpy(kirk1_pub+20,Py1,20);
  	ecdsa_set_pub(kirk1_pub);
		//Hash the Header
		SHAInit(&sha);
		SHAUpdate(&sha, (u8*)eheader+0x60, 0x30);
		SHAFinal(header_hash, &sha);		
		
	  if(!ecdsa_verify(header_hash,eheader->header_sig_r,eheader->header_sig_s)) {
	    return KIRK_HEADER_HASH_INVALID;
	  }
	  SHAInit(&sha);
		SHAUpdate(&sha, (u8*)eheader+0x60, size-0x60);
		SHAFinal(data_hash, &sha);  
		
	  if(!ecdsa_verify(data_hash,eheader->data_sig_r,eheader->data_sig_s)) {
	    return KIRK_DATA_HASH_INVALID;
	  }

  } else  {
    int ret = kirk_CMD10(inbuff, size);
    if(ret != KIRK_OPERATION_SUCCESS) return ret;
  }
  
  AES_set_key(&k1, keys.AES, 128);
  AES_cbc_decrypt(&k1, inbuff+sizeof(KIRK_CMD1_HEADER)+header->data_offset, outbuff, header->data_size);  
  
  return KIRK_OPERATION_SUCCESS;
}

int kirk_CMD4(u8* outbuff, u8* inbuff, int size)
{
  KIRK_AES128CBC_HEADER *header = (KIRK_AES128CBC_HEADER*)inbuff;
  u8* key;
  AES_ctx aesKey;
  
  if(is_kirk_initialized == 0) return KIRK_NOT_INITIALIZED;
  if(header->mode != KIRK_MODE_ENCRYPT_CBC) return KIRK_INVALID_MODE;
  if(header->data_size == 0) return KIRK_DATA_SIZE_ZERO;
  
  key = kirk_4_7_get_key(header->keyseed);
  if(key == (u8*)KIRK_INVALID_SIZE) return KIRK_INVALID_SIZE;
  
  //Set the key
  AES_set_key(&aesKey, key, 128);
  AES_cbc_encrypt(&aesKey, inbuff+sizeof(KIRK_AES128CBC_HEADER), outbuff+sizeof(KIRK_AES128CBC_HEADER), header->data_size);
  
  return KIRK_OPERATION_SUCCESS;
}

int kirk_CMD7(u8* outbuff, u8* inbuff, int size)
{
  KIRK_AES128CBC_HEADER *header = (KIRK_AES128CBC_HEADER*)inbuff;
  u8* key;
  AES_ctx aesKey;
  
  if(is_kirk_initialized == 0) return KIRK_NOT_INITIALIZED;
  if(header->mode != KIRK_MODE_DECRYPT_CBC) return KIRK_INVALID_MODE;
  if(header->data_size == 0) return KIRK_DATA_SIZE_ZERO;
  
  key = kirk_4_7_get_key(header->keyseed);
  if(key == (u8*)KIRK_INVALID_SIZE) return KIRK_INVALID_SIZE;
  
  //Set the key
  AES_set_key(&aesKey, key, 128);
  AES_cbc_decrypt(&aesKey, inbuff+sizeof(KIRK_AES128CBC_HEADER), outbuff, header->data_size);
  
  return KIRK_OPERATION_SUCCESS;
}

int kirk_CMD10(u8* inbuff, int insize)
{
  KIRK_CMD1_HEADER* header = (KIRK_CMD1_HEADER*)inbuff;
  header_keys keys; //0-15 AES key, 16-31 CMAC key
  u8 cmac_header_hash[16];
  u8 cmac_data_hash[16];
  AES_ctx cmac_key;
  int chk_size;
  
  if(is_kirk_initialized == 0) return KIRK_NOT_INITIALIZED;
  if(!(header->mode == KIRK_MODE_CMD1 || header->mode == KIRK_MODE_CMD2 || header->mode == KIRK_MODE_CMD3)) return KIRK_INVALID_MODE;
  if(header->data_size == 0) return KIRK_DATA_SIZE_ZERO;
  
  if(header->mode == KIRK_MODE_CMD1)
  {
    AES_cbc_decrypt(&aes_kirk1, inbuff, (u8*)&keys, 32); //decrypt AES & CMAC key to temp buffer
    AES_set_key(&cmac_key, keys.CMAC, 128);
    AES_CMAC(&cmac_key, inbuff+0x60, 0x30, cmac_header_hash);
  
    //Make sure data is 16 aligned
    chk_size = header->data_size;
    if(chk_size % 16) chk_size += 16 - (chk_size % 16);
    AES_CMAC(&cmac_key, inbuff+0x60, 0x30 + chk_size + header->data_offset, cmac_data_hash);
  
    if(memcmp(cmac_header_hash, header->CMAC_header_hash, 16) != 0) return KIRK_HEADER_HASH_INVALID;
    if(memcmp(cmac_data_hash, header->CMAC_data_hash, 16) != 0) return KIRK_DATA_HASH_INVALID;
  
    return KIRK_OPERATION_SUCCESS;
  }
  return KIRK_SIG_CHECK_INVALID; //Checks for cmd 2 & 3 not included right now
}

int kirk_CMD11(u8* outbuff, u8* inbuff, int size)
{
  KIRK_SHA1_HEADER *header = (KIRK_SHA1_HEADER *)inbuff;
  SHA_CTX sha;
  if(is_kirk_initialized == 0) return KIRK_NOT_INITIALIZED;
  if(header->data_size == 0 || size == 0) return KIRK_DATA_SIZE_ZERO;
  
	SHAInit(&sha);
	SHAUpdate(&sha, inbuff+sizeof(KIRK_SHA1_HEADER), header->data_size);
	SHAFinal(outbuff, &sha);
  return KIRK_OPERATION_SUCCESS;
}

// Generate an ECDSA Key pair
// offset 0 = private key (0x14 len)
// offset 0x14 = public key point (0x28 len)
int kirk_CMD12(u8 * outbuff, int outsize) {
  u8 k[0x15];
  KIRK_CMD12_BUFFER * keypair = (KIRK_CMD12_BUFFER *) outbuff;

  if(outsize != 0x3C) return KIRK_INVALID_SIZE;
  ecdsa_set_curve(ec_p,ec_a,ec_b2,ec_N2,Gx2,Gy2);
  k[0] = 0;
  kirk_CMD14(k+1,0x14);
  ec_priv_to_pub(k, (u8*)keypair->public_key.x);
  memcpy(keypair->private_key,k+1,0x14);
  
  return KIRK_OPERATION_SUCCESS;
}
// Point multiplication
// offset 0 = mulitplication value (0x14 len)
// offset 0x14 = point to multiply (0x28 len)
int kirk_CMD13(u8 * outbuff, int outsize,u8 * inbuff, int insize) {
  u8 k[0x15];
  KIRK_CMD13_BUFFER * pointmult = (KIRK_CMD13_BUFFER *) inbuff;
  k[0]=0;
  if(outsize != 0x28) return KIRK_INVALID_SIZE;
  if(insize != 0x3C) return KIRK_INVALID_SIZE;
  ecdsa_set_curve(ec_p,ec_a,ec_b2,ec_N2,Gx2,Gy2);
  ecdsa_set_pub((u8*)pointmult->public_key.x);
  memcpy(k+1,pointmult->multiplier,0x14);
  ec_pub_mult(k, outbuff);
  return KIRK_OPERATION_SUCCESS;
}

int kirk_CMD14(u8 * outbuff, int outsize) {
  u8 temp[0x104];
  KIRK_SHA1_HEADER *header = (KIRK_SHA1_HEADER *) temp;
  
  // Some randomly selected data for a "key" to add to each randomization
  u8 key[0x10] = { 0xA7, 0x2E, 0x4C, 0xB6, 0xC3, 0x34, 0xDF, 0x85, 0x70, 0x01, 0x49, 0xFC, 0xC0, 0x87, 0xC4, 0x77 };
  u32 curtime;
  //if(outsize != 0x14) return KIRK_INVALID_SIZE; // Need real error code
  if(outsize <=0) return KIRK_OPERATION_SUCCESS;
    
  memcpy(temp+4, PRNG_DATA,0x14);
  // This uses the standard C time function for portability.
  curtime=(u32)time(0);
  temp[0x18] = curtime &0xFF;
  temp[0x19] = (curtime>>8) &0xFF;
  temp[0x1A] = (curtime>>16) &0xFF;
  temp[0x1B] = (curtime>>24) &0xFF;
  memcpy(&temp[0x1C], key, 0x10);
  //This leaves the remainder of the 0x100 bytes in temp to whatever remains on the stack 
  // in an uninitialized state. This should add unpredicableness to the results as well
  header->data_size=0x100;
  kirk_CMD11(PRNG_DATA, temp, 0x104);
  while(outsize)
  {
    int blockrem= outsize %0x14;
    int block = outsize /0x14;
    
    if(block)
    {
      memcpy(outbuff, PRNG_DATA, 0x14);
      outbuff+=0x14;
      outsize -= 0x14;
      kirk_CMD14(outbuff, outsize);
    } else {
      if(blockrem)
      {
        memcpy(outbuff, PRNG_DATA, blockrem);
        outsize -= blockrem;
      }
    }
    
  }
  return KIRK_OPERATION_SUCCESS;
}

void decrypt_kirk16_private(u8 *dA_out, u8 *dA_enc)
{
  int i, k;
  kirk16_data keydata;
  u8 subkey_1[0x10], subkey_2[0x10];
  rijndael_ctx aes_ctx;

  keydata.fuseid[7] = g_fuse90 &0xFF;
  keydata.fuseid[6] = (g_fuse90>>8) &0xFF;
  keydata.fuseid[5] = (g_fuse90>>16) &0xFF;
  keydata.fuseid[4] = (g_fuse90>>24) &0xFF; 
  keydata.fuseid[3] = g_fuse94 &0xFF;
  keydata.fuseid[2] = (g_fuse94>>8) &0xFF;
  keydata.fuseid[1] = (g_fuse94>>16) &0xFF;
  keydata.fuseid[0] = (g_fuse94>>24) &0xFF;
 
  /* set encryption key */
  rijndael_set_key(&aes_ctx, kirk16_key, 128);
 
  /* set the subkeys */
  for (i = 0; i < 0x10; i++)
  {
    /* set to the fuseid */
    subkey_2[i] = subkey_1[i] = keydata.fuseid[i % 8];
  }
 
  /* do aes crypto */
  for (i = 0; i < 3; i++)
  {
    /* encrypt + decrypt */
    rijndael_encrypt(&aes_ctx, subkey_1, subkey_1);
    rijndael_decrypt(&aes_ctx, subkey_2, subkey_2);
  }
 
  /* set new key */
  rijndael_set_key(&aes_ctx, subkey_1, 128);
 
  /* now lets make the key mesh */
  for (i = 0; i < 3; i++)
  {
    /* do encryption in group of 3 */
    for (k = 0; k < 3; k++)
    {
      /* crypto */
      rijndael_encrypt(&aes_ctx, subkey_2, subkey_2);
    }
 
    /* copy to out block */
    memcpy(&keydata.mesh[i * 0x10], subkey_2, 0x10);
  }
 
  /* set the key to the mesh */
  rijndael_set_key(&aes_ctx, &keydata.mesh[0x20], 128);
 
  /* do the encryption routines for the aes key */
  for (i = 0; i < 2; i++)
  {
    /* encrypt the data */
    rijndael_encrypt(&aes_ctx, &keydata.mesh[0x10], &keydata.mesh[0x10]);
  }
 
  /* set the key to that mesh shit */
  rijndael_set_key(&aes_ctx, &keydata.mesh[0x10], 128);
 
  /* cbc decrypt the dA */
  AES_cbc_decrypt((AES_ctx *)&aes_ctx, dA_enc, dA_out, 0x20);
}
 
void encrypt_kirk16_private(u8 *dA_out, u8 *dA_dec)
{
  int i, k;
  kirk16_data keydata;
  u8 subkey_1[0x10], subkey_2[0x10];
  rijndael_ctx aes_ctx;
 

  keydata.fuseid[7] = g_fuse90 &0xFF;
  keydata.fuseid[6] = (g_fuse90>>8) &0xFF;
  keydata.fuseid[5] = (g_fuse90>>16) &0xFF;
  keydata.fuseid[4] = (g_fuse90>>24) &0xFF; 
  keydata.fuseid[3] = g_fuse94 &0xFF;
  keydata.fuseid[2] = (g_fuse94>>8) &0xFF;
  keydata.fuseid[1] = (g_fuse94>>16) &0xFF;
  keydata.fuseid[0] = (g_fuse94>>24) &0xFF;
  /* set encryption key */
  rijndael_set_key(&aes_ctx, kirk16_key, 128);
 
  /* set the subkeys */
  for (i = 0; i < 0x10; i++)
  {
    /* set to the fuseid */
    subkey_2[i] = subkey_1[i] = keydata.fuseid[i % 8];
  }
 
  /* do aes crypto */
  for (i = 0; i < 3; i++)
  {
    /* encrypt + decrypt */
    rijndael_encrypt(&aes_ctx, subkey_1, subkey_1);
    rijndael_decrypt(&aes_ctx, subkey_2, subkey_2);
  }
 
  /* set new key */
  rijndael_set_key(&aes_ctx, subkey_1, 128);
 
  /* now lets make the key mesh */
  for (i = 0; i < 3; i++)
  {
    /* do encryption in group of 3 */
    for (k = 0; k < 3; k++)
    {
      /* crypto */
      rijndael_encrypt(&aes_ctx, subkey_2, subkey_2);
    }
 
    /* copy to out block */
    memcpy(&keydata.mesh[i * 0x10], subkey_2, 0x10);
  }
 
  /* set the key to the mesh */
  rijndael_set_key(&aes_ctx, &keydata.mesh[0x20], 128);
 
  /* do the encryption routines for the aes key */
  for (i = 0; i < 2; i++)
  {
    /* encrypt the data */
    rijndael_encrypt(&aes_ctx, &keydata.mesh[0x10], &keydata.mesh[0x10]);
  }
 
  /* set the key to that mesh shit */
  rijndael_set_key(&aes_ctx, &keydata.mesh[0x10], 128);
 
  /* cbc encrypt the dA */
  AES_cbc_encrypt((AES_ctx *)&aes_ctx, dA_dec, dA_out, 0x20);
}

int kirk_CMD16(u8 * outbuff, int outsize, u8 * inbuff, int insize) {
	u8 dec_private[0x20];
	KIRK_CMD16_BUFFER * signbuf = (KIRK_CMD16_BUFFER *) inbuff;
	ECDSA_SIG * sig = (ECDSA_SIG *) outbuff;
	if(insize != 0x34) return KIRK_INVALID_SIZE;
	if(outsize != 0x28) return KIRK_INVALID_SIZE;
	decrypt_kirk16_private(dec_private,signbuf->enc_private);
	// Clear out the padding for safety
	memset(&dec_private[0x14], 0, 0xC);
	ecdsa_set_curve(ec_p,ec_a,ec_b2,ec_N2,Gx2,Gy2);
	ecdsa_set_priv(dec_private);
	ecdsa_sign(signbuf->message_hash,sig->r, sig->s);
  return KIRK_OPERATION_SUCCESS;
}

// ECDSA Verify
// inbuff structure:
// 00 = public key (0x28 length)
// 28 = message hash (0x14 length)
// 3C = signature R (0x14 length)
// 50 = signature S (0x14 length)
int kirk_CMD17(u8 * inbuff, int insize) {
	KIRK_CMD17_BUFFER * sig = (KIRK_CMD17_BUFFER *) inbuff;
  if(insize != 0x64) return KIRK_INVALID_SIZE;
  ecdsa_set_curve(ec_p,ec_a,ec_b2,ec_N2,Gx2,Gy2);
  ecdsa_set_pub(sig->public_key.x);
  // ecdsa_verify(u8 *hash, u8 *R, u8 *S)
  if(ecdsa_verify(sig->message_hash,sig->signature.r,sig->signature.s)) {
    return KIRK_OPERATION_SUCCESS;
  } else {
    return KIRK_SIG_CHECK_INVALID;
  }
}



int kirk_init()
{
  return kirk_init2((u8*)"Lazy Dev should have initialized!",33,0xBABEF00D, 0xDEADBEEF );;
}

int kirk_init2(u8 * rnd_seed, u32 seed_size, u32 fuseid_90, u32 fuseid_94) {
  u8 temp[0x104];
  
  KIRK_SHA1_HEADER *header = (KIRK_SHA1_HEADER *) temp;
  // Another randomly selected data for a "key" to add to each randomization
  u8 key[0x10] = {0x07, 0xAB, 0xEF, 0xF8, 0x96, 0x8C, 0xF3, 0xD6, 0x14, 0xE0, 0xEB, 0xB2, 0x9D, 0x8B, 0x4E, 0x74};
  u32 curtime;

  //Set PRNG_DATA initially, otherwise use what ever uninitialized data is in the buffer
  if(seed_size > 0) {
    u8 * seedbuf;
    KIRK_SHA1_HEADER *seedheader;;
    seedbuf=(u8*)malloc(seed_size+4);
    seedheader= (KIRK_SHA1_HEADER *) seedbuf;
    seedheader->data_size = seed_size;
    kirk_CMD11(PRNG_DATA, seedbuf, seed_size+4);    
    free(seedbuf);
  }
  memcpy(temp+4, PRNG_DATA,0x14);
  // This uses the standard C time function for portability.
  curtime=(u32)time(0);
  temp[0x18] = curtime &0xFF;
  temp[0x19] = (curtime>>8) &0xFF;
  temp[0x1A] = (curtime>>16) &0xFF;
  temp[0x1B] = (curtime>>24) &0xFF;
  memcpy(&temp[0x1C], key, 0x10);
  //This leaves the remainder of the 0x100 bytes in temp to whatever remains on the stack 
  // in an uninitialized state. This should add unpredicableness to the results as well
  header->data_size=0x100;
  kirk_CMD11(PRNG_DATA, temp, 0x104); 
  
  //Set Fuse ID
  g_fuse90=fuseid_90;
  g_fuse94=fuseid_94;
  
  //Set KIRK1 main key
  AES_set_key(&aes_kirk1, kirk1_key, 128);
  

  is_kirk_initialized = 1;
  return 0;
}
u8* kirk_4_7_get_key(int key_type)
{
  switch(key_type)
  {
  	case(0x02): return kirk7_key02; break;
    case(0x03): return kirk7_key03; break;
    case(0x04): return kirk7_key04; break;
    case(0x05): return kirk7_key05; break;
    case(0x07): return kirk7_key07; break;
    case(0x0C): return kirk7_key0C; break;
    case(0x0D): return kirk7_key0D; break;
    case(0x0E): return kirk7_key0E; break;
    case(0x0F): return kirk7_key0F; break;
    case(0x10): return kirk7_key10; break;
    case(0x11): return kirk7_key11; break;
    case(0x12): return kirk7_key12; break;
    case(0x38): return kirk7_key38; break;
    case(0x39): return kirk7_key39; break;
    case(0x3A): return kirk7_key3A; break;
    case(0x44): return kirk7_key44; break;
    case(0x4B): return kirk7_key4B; break;
    case(0x53): return kirk7_key53; break;
    case(0x57): return kirk7_key57; break;
    case(0x5D): return kirk7_key5D; break;
    case(0x63): return kirk7_key63; break;
    case(0x64): return kirk7_key64; break;
    default: return (u8*)KIRK_INVALID_SIZE; break; //need to get the real error code for that, placeholder now :)
  }
}

int kirk_CMD1_ex(u8* outbuff, u8* inbuff, int size, KIRK_CMD1_HEADER* header)
{
  u8* buffer = (u8*)malloc(size);
  int ret;
  
  memcpy(buffer, header, sizeof(KIRK_CMD1_HEADER));
  memcpy(buffer+sizeof(KIRK_CMD1_HEADER), inbuff, header->data_size);
  
  ret = kirk_CMD1(outbuff, buffer, size);
  free(buffer);
  return ret;
}


int sceUtilsBufferCopyWithRange(u8* outbuff, int outsize, u8* inbuff, int insize, int cmd)
{
  switch(cmd)
  {
    case KIRK_CMD_DECRYPT_PRIVATE: return kirk_CMD1(outbuff, inbuff, insize); break;
    case KIRK_CMD_ENCRYPT_IV_0: return kirk_CMD4(outbuff, inbuff, insize); break;
    case KIRK_CMD_DECRYPT_IV_0: return kirk_CMD7(outbuff, inbuff, insize); break;
    case KIRK_CMD_PRIV_SIGN_CHECK: return kirk_CMD10(inbuff, insize); break;
    case KIRK_CMD_SHA1_HASH: return kirk_CMD11(outbuff, inbuff, insize); break;
    case KIRK_CMD_ECDSA_GEN_KEYS: return kirk_CMD12(outbuff,outsize); break;
    case KIRK_CMD_ECDSA_MULTIPLY_POINT: return kirk_CMD13(outbuff,outsize, inbuff, insize); break;
    case KIRK_CMD_PRNG: return kirk_CMD14(outbuff,outsize); break;
    case KIRK_CMD_ECDSA_SIGN: return kirk_CMD16(outbuff, outsize, inbuff, insize); break;
    case KIRK_CMD_ECDSA_VERIFY: return kirk_CMD17(inbuff, insize); break;     
  }
  return -1;
}
