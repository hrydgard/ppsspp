#ifndef AMCTRL_H
#define AMCTRL_H

typedef struct {
	int type;
	u8 key[16];
	u8 pad[16];
	int pad_size;
} MAC_KEY;

typedef struct
{
	u32 type;
	u32 seed;
	u8 key[16];
} CIPHER_KEY;

typedef struct {
	u8  vkey[16];   // key to decrypt PGD header
	u8  dkey[16];   // key to decrypt PGD data

	u32 open_flag;
	u32 key_index;
	u32 drm_type;
	u32 mac_type;
	u32 cipher_type;

	u32 data_size;
	u32 align_size;
	u32 block_size;
	u32 block_nr;
	u32 data_offset;
	u32 table_offset;

	u8  *block_buf;
	u32 current_block;
	u32 file_offset;
}PGD_DESC;


// type:
//      2: use fuse id
//      3: use fixed key. MAC need encrypt again
int sceDrmBBMacInit(MAC_KEY *mkey, int type);
int sceDrmBBMacUpdate(MAC_KEY *mkey, u8 *buf, int size);
int sceDrmBBMacFinal(MAC_KEY *mkey, u8 *buf, u8 *vkey);
int sceDrmBBMacFinal2(MAC_KEY *mkey, u8 *out, u8 *vkey);
int bbmac_getkey(MAC_KEY *mkey, u8 *bbmac, u8 *vkey);

// type: 1 use fixed key
//       2 use fuse id
// mode: 1 for encrypt
//       2 for decrypt
int sceDrmBBCipherInit(CIPHER_KEY *ckey, int type, int mode, u8 *header_key, u8 *version_key, u32 seed);
int sceDrmBBCipherUpdate(CIPHER_KEY *ckey, u8 *data, int size);
int sceDrmBBCipherFinal(CIPHER_KEY *ckey);

// npdrm.prx
int sceNpDrmGetFixedKey(u8 *key, char *npstr, int type);

PGD_DESC *pgd_open(u8 *pgd_buf, int pgd_flag, u8 *pgd_vkey);
int pgd_decrypt_block(PGD_DESC *pgd, int block);
int pgd_close(PGD_DESC *pgd);

#endif

