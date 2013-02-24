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

#ifndef KIRK_ENGINE
#define KIRK_ENGINE
typedef unsigned char u8;
typedef unsigned short int u16;
typedef unsigned int u32;

//Kirk return values
#define KIRK_OPERATION_SUCCESS 0
#define KIRK_NOT_ENABLED 1
#define KIRK_INVALID_MODE 2
#define KIRK_HEADER_HASH_INVALID 3
#define KIRK_DATA_HASH_INVALID 4
#define KIRK_SIG_CHECK_INVALID 5
#define KIRK_UNK_1 6
#define KIRK_UNK_2 7
#define KIRK_UNK_3 8
#define KIRK_UNK_4 9
#define KIRK_UNK_5 0xA
#define KIRK_UNK_6 0xB
#define KIRK_NOT_INITIALIZED 0xC
#define KIRK_INVALID_OPERATION 0xD
#define KIRK_INVALID_SEED_CODE 0xE
#define KIRK_INVALID_SIZE 0xF
#define KIRK_DATA_SIZE_ZERO 0x10

typedef struct
{
	int mode;    //0
	int unk_4;   //4
	int unk_8;   //8
	int keyseed; //C
	int data_size;   //10
} KIRK_AES128CBC_HEADER; //0x14

typedef struct
{
	u8  AES_key[16];            //0
	u8  CMAC_key[16];           //10
	u8  CMAC_header_hash[16];   //20
	u8  CMAC_data_hash[16];     //30
	u8  unused[32];             //40
	u32 mode;                   //60
	u8  ecdsa_hash;             //64
	u8  unk3[11];               //65
	u32 data_size;              //70
	u32 data_offset;            //74  
	u8  unk4[8];                //78
	u8  unk5[16];               //80
} KIRK_CMD1_HEADER; //0x90

typedef struct
{
	u8  AES_key[16];            //0
	u8  header_sig_r[20];           //10
	u8  header_sig_s[20];   //24
	u8  data_sig_r[20];     //38
	u8  data_sig_s[20];     //4C
	u32 mode;                   //60
	u8  ecdsa_hash;             //64
	u8  unk3[11];               //65
	u32 data_size;              //70
	u32 data_offset;            //74  
	u8  unk4[8];                //78
	u8  unk5[16];               //80
} KIRK_CMD1_ECDSA_HEADER; //0x90


typedef struct
{
	u8 r[0x14];
	u8 s[0x14];
} ECDSA_SIG; //0x28
typedef struct
{
	u8 x[0x14];
	u8 y[0x14];
} ECDSA_POINT; //0x28

typedef struct
{
    u32 data_size;             //0     
} KIRK_SHA1_HEADER;            //4

typedef struct
{
	u8 private_key[0x14];
	ECDSA_POINT public_key;
} KIRK_CMD12_BUFFER;

typedef struct
{
	u8 multiplier[0x14];
	ECDSA_POINT public_key;
} KIRK_CMD13_BUFFER;


typedef struct
{
	u8 enc_private[0x20];					//0
	u8 message_hash[0x14];				//20
} KIRK_CMD16_BUFFER;//0x34

typedef struct
{
	ECDSA_POINT public_key;					//0
	u8 message_hash[0x14];				//28
	ECDSA_SIG signature;					//3C
} KIRK_CMD17_BUFFER;//0x64

//mode passed to sceUtilsBufferCopyWithRange
#define KIRK_CMD_DECRYPT_PRIVATE 1
#define KIRK_CMD_2 2
#define KIRK_CMD_3 3
#define KIRK_CMD_ENCRYPT_IV_0 4
#define KIRK_CMD_ENCRYPT_IV_FUSE 5
#define KIRK_CMD_ENCRYPT_IV_USER 6
#define KIRK_CMD_DECRYPT_IV_0 7
#define KIRK_CMD_DECRYPT_IV_FUSE 8
#define KIRK_CMD_DECRYPT_IV_USER 9
#define KIRK_CMD_PRIV_SIGN_CHECK 10
#define KIRK_CMD_SHA1_HASH 11
#define KIRK_CMD_ECDSA_GEN_KEYS 12
#define KIRK_CMD_ECDSA_MULTIPLY_POINT 13
#define KIRK_CMD_PRNG 14
#define KIRK_CMD_15 15
#define KIRK_CMD_ECDSA_SIGN 16
#define KIRK_CMD_ECDSA_VERIFY 17

//"mode" in header
#define KIRK_MODE_CMD1 1
#define KIRK_MODE_CMD2 2
#define KIRK_MODE_CMD3 3
#define KIRK_MODE_ENCRYPT_CBC 4
#define KIRK_MODE_DECRYPT_CBC 5

//sceUtilsBufferCopyWithRange errors
#define SUBCWR_NOT_16_ALGINED 0x90A
#define SUBCWR_HEADER_HASH_INVALID 0x920
#define SUBCWR_BUFFER_TOO_SMALL 0x1000

/*
      // Private Sig + Cipher
      0x01: Super-Duper decryption (no inverse)
      0x02: Encrypt Operation (inverse of 0x03)
      0x03: Decrypt Operation (inverse of 0x02)

      // Cipher
      0x04: Encrypt Operation (inverse of 0x07) (IV=0)
      0x05: Encrypt Operation (inverse of 0x08) (IV=FuseID)
      0x06: Encrypt Operation (inverse of 0x09) (IV=UserDefined)
      0x07: Decrypt Operation (inverse of 0x04)
      0x08: Decrypt Operation (inverse of 0x05)
      0x09: Decrypt Operation (inverse of 0x06)
	  
      // Sig Gens
      0x0A: Private Signature Check (checks for private SCE sig)
      0x0B: SHA1 Hash
      0x0C: Mul1
      0x0D: Mul2
      0x0E: Random Number Gen
      0x0F: (absolutely no idea ?could be KIRK initialization)
      0x10: Signature Gen
      // Sig Checks
      0x11: Signature Check (checks for generated sigs)
      0x12: Certificate Check (idstorage signatures)
*/

//kirk-like funcs
int kirk_CMD0(u8* outbuff, u8* inbuff, int size, int generate_trash);
int kirk_CMD1(u8* outbuff, u8* inbuff, int size);

int kirk_CMD4(u8* outbuff, u8* inbuff, int size);
int kirk_CMD7(u8* outbuff, u8* inbuff, int size);
int kirk_CMD10(u8* inbuff, int insize);
int kirk_CMD11(u8* outbuff, u8* inbuff, int size);
int kirk_CMD12(u8* outbuff, int outsize);
int kirk_CMD13(u8* outbuff, int outsize,u8* inbuff, int insize);
int kirk_CMD14(u8* outbuff, int outsize);
int kirk_CMD16(u8* outbuff, int outsize,u8* inbuff, int insize);
int kirk_CMD17(u8* inbuff, int insize);

int kirk_init(); //CMD 0xF?
int kirk_init2(u8 *, u32, u32, u32);

//helper funcs
u8* kirk_4_7_get_key(int key_type);

//kirk "ex" functions
int kirk_CMD1_ex(u8* outbuff, u8* inbuff, int size, KIRK_CMD1_HEADER* header);

//sce-like funcs
int sceUtilsSetFuseID(u8*fuse);
int sceUtilsBufferCopyWithRange(u8* outbuff, int outsize, u8* inbuff, int insize, int cmd);
void decrypt_kirk16_private(u8 *dA_out, u8 *dA_enc);
void encrypt_kirk16_private(u8 *dA_out, u8 *dA_dec);

// Prototypes for the Elliptic Curve and Big Number functions
int ecdsa_get_params(u32 type, u8 *p, u8 *a, u8 *b, u8 *N, u8 *Gx, u8 *Gy);
int ecdsa_set_curve(u8* p,u8* a,u8* b,u8* N,u8* Gx,u8* Gy);
void ecdsa_set_pub(u8 *Q);
void ecdsa_set_priv(u8 *k);
int ecdsa_verify(u8 *hash, u8 *R, u8 *S);
void ecdsa_sign(u8 *hash, u8 *R, u8 *S);
void ec_priv_to_pub(u8 *k, u8 *Q);
void ec_pub_mult(u8 *k, u8 *Q);
void bn_copy(u8 *d, u8 *a, u32 n);
int bn_compare(u8 *a, u8 *b, u32 n);
void bn_reduce(u8 *d, u8 *N, u32 n);
void bn_add(u8 *d, u8 *a, u8 *b, u8 *N, u32 n);
void bn_sub(u8 *d, u8 *a, u8 *b, u8 *N, u32 n);
void bn_to_mon(u8 *d, u8 *N, u32 n);
void bn_from_mon(u8 *d, u8 *N, u32 n);
void bn_mon_mul(u8 *d, u8 *a, u8 *b, u8 *N, u32 n);
void bn_mon_inv(u8 *d, u8 *a, u8 *N, u32 n);
void hex_dump(char *str, u8 *buf, int size);

#define		round_up(x,n)	(-(-(x) & -(n)))

#define		array_size(x)	(sizeof(x) / sizeof(*(x)))
#endif
