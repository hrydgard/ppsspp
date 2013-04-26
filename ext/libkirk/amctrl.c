/*
 *  amctrl.c  -- Reverse engineering of amctrl.prx
 *               written by tpu.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kirk_engine.h"
#include "AES.h"
#include "SHA1.h"
#include "amctrl.h"

/*************************************************************/

static const u8 loc_1CD4[16] = {0xE3, 0x50, 0xED, 0x1D, 0x91, 0x0A, 0x1F, 0xD0, 0x29, 0xBB, 0x1C, 0x3E, 0xF3, 0x40, 0x77, 0xFB};
static const u8 loc_1CE4[16] = {0x13, 0x5F, 0xA4, 0x7C, 0xAB, 0x39, 0x5B, 0xA4, 0x76, 0xB8, 0xCC, 0xA9, 0x8F, 0x3A, 0x04, 0x45};
static const u8 loc_1CF4[16] = {0x67, 0x8D, 0x7F, 0xA3, 0x2A, 0x9C, 0xA0, 0xD1, 0x50, 0x8A, 0xD8, 0x38, 0x5E, 0x4B, 0x01, 0x7E};

static u8 kirk_buf[0x0814]; // 1DC0 1DD4

/*************************************************************/

static int kirk4(u8 *buf, int size, int type)
{
	int retv;
	u32 *header = (u32*)buf;

	header[0] = 4;
	header[1] = 0;
	header[2] = 0;
	header[3] = type;
	header[4] = size;

	retv = sceUtilsBufferCopyWithRange(buf, size+0x14, buf, size, 4);

	if(retv)
		return 0x80510311;

	return 0;
}

static int kirk7(u8 *buf, int size, int type)
{
	int retv;
	u32 *header = (u32*)buf;

	header[0] = 5;
	header[1] = 0;
	header[2] = 0;
	header[3] = type;
	header[4] = size;

	retv = sceUtilsBufferCopyWithRange(buf, size+0x14, buf, size, 7);
	if(retv)
		return 0x80510311;

	return 0;
}

static int kirk5(u8 *buf, int size)
{
	int retv;
	u32 *header = (u32*)buf;

	header[0] = 4;
	header[1] = 0;
	header[2] = 0;
	header[3] = 0x0100;
	header[4] = size;

	retv = sceUtilsBufferCopyWithRange(buf, size+0x14, buf, size, 5);
	if(retv)
		return 0x80510312;

	return 0;
}

static int kirk8(u8 *buf, int size)
{
	int retv;
	u32 *header = (u32*)buf;

	header[0] = 5;
	header[1] = 0;
	header[2] = 0;
	header[3] = 0x0100;
	header[4] = size;

	retv = sceUtilsBufferCopyWithRange(buf, size+0x14, buf, size, 8);
	if(retv)
		return 0x80510312;

	return 0;
}

static int kirk14(u8 *buf)
{
	int retv;

	retv = sceUtilsBufferCopyWithRange(buf, 0x14, 0, 0, 14);
	if(retv)
		return 0x80510315;

	return 0;
}

/*************************************************************/

// Called by sceDrmBBMacUpdate
// encrypt_buf
static int sub_158(u8 *buf, int size, u8 *key, int key_type)
{
	int i, retv;

	for(i=0; i<16; i++){
		buf[0x14+i] ^= key[i];
	}

	retv = kirk4(buf, size, key_type);
	if(retv)
		return retv;

	// copy last 16 bytes to keys
	memcpy(key, buf+size+4, 16);

	return 0;
}


// type:
//      2: use fuse id
//      3: use fixed id
int sceDrmBBMacInit(MAC_KEY *mkey, int type)
{
	mkey->type = type;
	mkey->pad_size = 0;

	memset(mkey->key, 0, 16);
	memset(mkey->pad, 0, 16);

	return 0;
}

int sceDrmBBMacUpdate(MAC_KEY *mkey, u8 *buf, int size)
{
	int retv = 0, ksize, p, type;
	u8 *kbuf;

	if(mkey->pad_size>16){
		retv = 0x80510302;
		goto _exit;
	}

	if(mkey->pad_size+size<=16){
		memcpy(mkey->pad+mkey->pad_size, buf, size);
		mkey->pad_size += size;
		retv = 0;
	}else{
		kbuf = kirk_buf+0x14;
		// copy pad data first
		memcpy(kbuf, mkey->pad, mkey->pad_size);

		p = mkey->pad_size;

		mkey->pad_size += size;
		mkey->pad_size &= 0x0f;
		if(mkey->pad_size==0)
			mkey->pad_size = 16;

		size -= mkey->pad_size;
		// save last data to pad buf
		memcpy(mkey->pad, buf+size, mkey->pad_size);

		type = (mkey->type==2)? 0x3A : 0x38;

		while(size){
			ksize = (size+p>=0x0800)? 0x0800 : size+p;
			memcpy(kbuf+p, buf, ksize-p);
			retv = sub_158(kirk_buf, ksize, mkey->key, type);
			if(retv)
				goto _exit;
			size -= (ksize-p);
			buf += ksize-p;
			p = 0;
		}
	}

_exit:
	return retv;

}

int sceDrmBBMacFinal(MAC_KEY *mkey, u8 *buf, u8 *vkey)
{
	int i, retv, code;
	u8 *kbuf, tmp[16], tmp1[16];
	u32 t0, v0, v1;

	if(mkey->pad_size>16)
		return 0x80510302;

	code = (mkey->type==2)? 0x3A : 0x38;
	kbuf = kirk_buf+0x14;

	memset(kbuf, 0, 16);
	retv = kirk4(kirk_buf, 16, code);
	if(retv)
		goto _exit;
	memcpy(tmp, kbuf, 16);

	// left shift tmp 1 bit
	t0 = (tmp[0]&0x80)? 0x87 : 0;
	for(i=0; i<15; i++){
		v1 = tmp[i+0];
		v0 = tmp[i+1];
		v1 <<= 1;
		v0 >>= 7;
		v0 |= v1;
		tmp[i+0] = v0;
	}
	v0 = tmp[15];
	v0 <<= 1;
	v0 ^= t0;
	tmp[15] = v0;

	// padding remain data
	if(mkey->pad_size<16){
		// left shift tmp 1 bit
		t0 = (tmp[0]&0x80)? 0x87 : 0;
		for(i=0; i<15; i++){
			v1 = tmp[i+0];
			v0 = tmp[i+1];
			v1 <<= 1;
			v0 >>= 7;
			v0 |= v1;
			tmp[i+0] = v0;
		}
		v0 = tmp[15];
		v0 <<= 1;
		v0 ^= t0;
		tmp[15] = v0;

		mkey->pad[mkey->pad_size] = 0x80;
		if(mkey->pad_size+1<16)
			memset(mkey->pad+mkey->pad_size+1, 0, 16-mkey->pad_size-1);
	}

	for(i=0; i<16; i++){
		mkey->pad[i] ^= tmp[i];
	}

	memcpy(kbuf, mkey->pad, 16);
	memcpy(tmp1, mkey->key, 16);

	retv = sub_158(kirk_buf, 0x10, tmp1, code);
	if(retv)
		return retv;

	for(i=0; i<0x10; i++){
		tmp1[i] ^= loc_1CD4[i];
	}

	if(mkey->type==2){
		memcpy(kbuf, tmp1, 16);

		retv = kirk5(kirk_buf, 0x10);
		if(retv)
			goto _exit;

		retv = kirk4(kirk_buf, 0x10, code);
		if(retv)
			goto _exit;

		memcpy(tmp1, kbuf, 16);
	}

	if(vkey){
		for(i=0; i<0x10; i++){
			tmp1[i] ^= vkey[i];
		}
		memcpy(kbuf, tmp1, 16);

		retv = kirk4(kirk_buf, 0x10, code);
		if(retv)
			goto _exit;

		memcpy(tmp1, kbuf, 16);
	}

	memcpy(buf, tmp1, 16);

	memset(mkey->key, 0, 16);
	memset(mkey->pad, 0, 16);

	mkey->pad_size = 0;
	mkey->type = 0;
	retv = 0;

_exit:
	return retv;
}

int sceDrmBBMacFinal2(MAC_KEY *mkey, u8 *out, u8 *vkey)
{
	int i, retv, type;
	u8 *kbuf, tmp[16];

	type = mkey->type;
	retv = sceDrmBBMacFinal(mkey, tmp, vkey);
	if(retv)
		return retv;

	kbuf = kirk_buf+0x14;

	// decrypt bbmac
	if(type==3){
		memcpy(kbuf, out, 0x10);
		kirk7(kirk_buf, 0x10, 0x63);
	}else{
		memcpy(kirk_buf, out, 0x10);
	}

	retv = 0;
	for(i=0; i<0x10; i++){
		if(kirk_buf[i]!=tmp[i]){
			retv = 0x80510300;
			break;
		}
	}

	return retv;
}

// get key from bbmac
int bbmac_getkey(MAC_KEY *mkey, u8 *bbmac, u8 *vkey)
{
	int i, retv, type, code;
	u8 *kbuf, tmp[16], tmp1[16];

	type = mkey->type;
	retv = sceDrmBBMacFinal(mkey, tmp, NULL);
	if(retv)
		return retv;

	kbuf = kirk_buf+0x14;

	// decrypt bbmac
	if(type==3){
		memcpy(kbuf, bbmac, 0x10);
		kirk7(kirk_buf, 0x10, 0x63);
	}else{
		memcpy(kirk_buf, bbmac, 0x10);
	}

	memcpy(tmp1, kirk_buf, 16);
	memcpy(kbuf, tmp1, 16);

	code = (type==2)? 0x3A : 0x38;
	kirk7(kirk_buf, 0x10, code);

	for(i=0; i<0x10; i++){
		vkey[i] = tmp[i] ^ kirk_buf[i];
	}

	return 0;
}

/*************************************************************/

static int sub_1F8(u8 *buf, int size, u8 *key, int key_type)
{
	int i, retv;
	u8 tmp[16];

	// copy last 16 bytes to tmp
	memcpy(tmp, buf+size+0x14-16, 16);

	retv = kirk7(buf, size, key_type);
	if(retv)
		return retv;

	for(i=0; i<16; i++){
		buf[i] ^= key[i];
	}

	// copy last 16 bytes to keys
	memcpy(key, tmp, 16);

	return 0;
}


static int sub_428(u8 *kbuf, u8 *dbuf, int size, CIPHER_KEY *ckey)
{
	int i, retv;
	u8 tmp1[16], tmp2[16];

	memcpy(kbuf+0x14, ckey->key, 16);

	for(i=0; i<16; i++){
		kbuf[0x14+i] ^= loc_1CF4[i];
	}
	
	if(ckey->type==2)
		retv = kirk8(kbuf, 16);
	else
		retv = kirk7(kbuf, 16, 0x39);
	if(retv)
		return retv;

	for(i=0; i<16; i++){
		kbuf[i] ^= loc_1CE4[i];
	}

	memcpy(tmp2, kbuf, 0x10);

	if(ckey->seed==1){
		memset(tmp1, 0, 0x10);
	}else{
		memcpy(tmp1, tmp2, 0x10);
		*(u32*)(tmp1+0x0c) = ckey->seed-1;
	}

	for(i=0; i<size; i+=16){
		memcpy(kbuf+0x14+i, tmp2, 12);
		*(u32*)(kbuf+0x14+i+12) = ckey->seed;
		ckey->seed += 1;
	}

	retv = sub_1F8(kbuf, size, tmp1, 0x63);
	if(retv)
		return retv;

	for(i=0; i<size; i++){
		dbuf[i] ^= kbuf[i];
	}

	return 0;
}

// type: 1 use fixed key
//       2 use fuse id
// mode: 1 for encrypt
//       2 for decrypt
int sceDrmBBCipherInit(CIPHER_KEY *ckey, int type, int mode, u8 *header_key, u8 *version_key, u32 seed)
{
	int i, retv;
	u8 *kbuf;

	kbuf = kirk_buf+0x14;
	ckey->type = type;
	if(mode==2){
		ckey->seed = seed+1;
		for(i=0; i<16; i++){
			ckey->key[i] = header_key[i];
		}
		if(version_key){
			for(i=0; i<16; i++){
				ckey->key[i] ^= version_key[i];
			}
		}
		retv = 0;
	}else if(mode==1){
		ckey->seed = 1;
		retv = kirk14(kirk_buf);
		if(retv)
			return retv;

		memcpy(kbuf, kirk_buf, 0x10);
		memset(kbuf+0x0c, 0, 4);

		if(ckey->type==2){
			for(i=0; i<16; i++){
				kbuf[i] ^= loc_1CE4[i];
			}
			retv = kirk5(kirk_buf, 0x10);
			for(i=0; i<16; i++){
				kbuf[i] ^= loc_1CF4[i];
			}
		}else{
			for(i=0; i<16; i++){
				kbuf[i] ^= loc_1CE4[i];
			}
			retv = kirk4(kirk_buf, 0x10, 0x39);
			for(i=0; i<16; i++){
				kbuf[i] ^= loc_1CF4[i];
			}
		}
		if(retv)
			return retv;

		memcpy(ckey->key, kbuf, 0x10);
		memcpy(header_key, kbuf, 0x10);

		if(version_key){
			for(i=0; i<16; i++){
				ckey->key[i] ^= version_key[i];
			}
		}
	}else{
		retv = 0;
	}

	return retv;
}

int sceDrmBBCipherUpdate(CIPHER_KEY *ckey, u8 *data, int size)
{
	int p, retv, dsize;

	retv = 0;
	p = 0;

	while(size>0){
		dsize = (size>=0x0800)? 0x0800 : size;
		retv = sub_428(kirk_buf, data+p, dsize, ckey);
		if(retv)
			break;
		size -= dsize;
		p += dsize;
	}

	return retv;
}

int sceDrmBBCipherFinal(CIPHER_KEY *ckey)
{
	memset(ckey->key, 0, 16);
	ckey->type = 0;
	ckey->seed = 0;

	return 0;
}

/*************************************************************/

// AES128 encrypt key
static const u8 key_357C[0x30] = {
	0x07,0x3D,0x9E,0x9D,0xA8,0xFD,0x3B,0x2F,0x63,0x18,0x93,0x2E,0xF8,0x57,0xA6,0x64,
	0x37,0x49,0xB7,0x01,0xCA,0xE2,0xE0,0xC5,0x44,0x2E,0x06,0xB6,0x1E,0xFF,0x84,0xF2,
	0x9D,0x31,0xB8,0x5A,0xC8,0xFA,0x16,0x80,0x73,0x60,0x18,0x82,0x18,0x77,0x91,0x9D,
};

static const u8 key_363C[16] = {
	0x38,0x20,0xD0,0x11,0x07,0xA3,0xFF,0x3E,0x0A,0x4C,0x20,0x85,0x39,0x10,0xB5,0x54,
};

int sceNpDrmGetFixedKey(u8 *key, char *npstr, int type)
{
	AES_ctx akey;
	MAC_KEY mkey;
	char strbuf[0x30];
	int retv;

	if((type&0x01000000)==0)
		return 0x80550901;
	type &= 0x000000ff;

	memset(strbuf, 0, 0x30);
	strncpy(strbuf, npstr, 0x30);

	retv = sceDrmBBMacInit(&mkey, 1);
	if(retv)
		return retv;

	retv = sceDrmBBMacUpdate(&mkey, (u8*)strbuf, 0x30);
	if(retv)
		return retv;

	retv = sceDrmBBMacFinal(&mkey, key, (u8*)key_363C);
	if(retv)
		return 0x80550902;

	if(type==0)
		return 0;
	if(type>3)
		return 0x80550901;
	type = (type-1)*16;

	AES_set_key(&akey, &key_357C[type], 128);
	AES_encrypt(&akey, key, key);

	return 0;
}


/*************************************************************/


static const u8 dnas_key1A90[] = {0xED,0xE2,0x5D,0x2D,0xBB,0xF8,0x12,0xE5,0x3C,0x5C,0x59,0x32,0xFA,0xE3,0xE2,0x43};
static const u8 dnas_key1AA0[] = {0x27,0x74,0xFB,0xEB,0xA4,0xA0,   1,0xD7,   2,0x56,0x9E,0x33,0x8C,0x19,0x57,0x83};

PGD_DESC *pgd_open(u8 *pgd_buf, int pgd_flag, u8 *pgd_vkey)
{
	PGD_DESC *pgd;
	MAC_KEY mkey;
	CIPHER_KEY ckey;
	u8 *fkey;
	int retv;

	//DEBUG_LOG(HLE, "Open PGD ...");

	pgd = (PGD_DESC*)malloc(sizeof(PGD_DESC));
	memset(pgd, 0, sizeof(PGD_DESC));

	pgd->key_index = *(u32*)(pgd_buf+4);
	pgd->drm_type  = *(u32*)(pgd_buf+8);

	if(pgd->drm_type==1){
		pgd->mac_type = 1;
		pgd_flag |= 4;
		if(pgd->key_index>1){
			pgd->mac_type = 3;
			pgd_flag |= 8;
		}
		pgd->cipher_type = 1;
	}else{
		pgd->mac_type = 2;
		pgd->cipher_type = 2;
	}
	pgd->open_flag = pgd_flag;

	// select fixed key
	fkey = NULL;
	if(pgd_flag&2)
		fkey = (u8*)dnas_key1A90;
	if(pgd_flag&1)
		fkey = (u8*)dnas_key1AA0;
	if(fkey==NULL){
		//ERROR_LOG(HLE, "pgd_open: invalid pgd_flag! %08x\n", pgd_flag);
		free(pgd);
		return NULL;
	}

	// MAC_0x80 check
	sceDrmBBMacInit(&mkey, pgd->mac_type);
	sceDrmBBMacUpdate(&mkey, pgd_buf+0x00, 0x80);
	retv = sceDrmBBMacFinal2(&mkey, pgd_buf+0x80, fkey);
	if(retv){
		//ERROR_LOG(HLE, "pgd_open: MAC_80 check failed!: %08x(%d)\n", retv, retv);
		free(pgd);
		return NULL;
	}

	// MAC_0x70
	sceDrmBBMacInit(&mkey, pgd->mac_type);
	sceDrmBBMacUpdate(&mkey, pgd_buf+0x00, 0x70);
	if(pgd_vkey){
		// use given vkey
		retv = sceDrmBBMacFinal2(&mkey, pgd_buf+0x70, pgd_vkey);
		if(retv){
			//ERROR_LOG(HLE, "pgd_open: MAC_70 check failed!: %08x(%d)\n", retv, retv);
			free(pgd);
			return NULL;
		}else{
			memcpy(pgd->vkey, pgd_vkey, 16);
		}
	}else{
		// get vkey from MAC_70
		bbmac_getkey(&mkey, pgd_buf+0x70, pgd->vkey);
	}

	// decrypt PGD_DESC
	sceDrmBBCipherInit(&ckey, pgd->cipher_type, 2, pgd_buf+0x10, pgd->vkey, 0);
	sceDrmBBCipherUpdate(&ckey, pgd_buf+0x30, 0x30);
	sceDrmBBCipherFinal(&ckey);

	pgd->data_size   = *(u32*)(pgd_buf+0x44);
	pgd->block_size  = *(u32*)(pgd_buf+0x48);
	pgd->data_offset = *(u32*)(pgd_buf+0x4c);
	memcpy(pgd->dkey, pgd_buf+0x30, 16);

	pgd->align_size = (pgd->data_size+15)&~15;
	pgd->table_offset = pgd->data_offset+pgd->align_size;
	pgd->block_nr = (pgd->align_size+pgd->block_size-1)&~(pgd->block_size-1);
	pgd->block_nr = pgd->block_nr/pgd->block_size;

	pgd->file_offset = 0;
	pgd->current_block = -1;
	pgd->block_buf = (u8*)malloc(pgd->block_size*2);

	return pgd;
}

int pgd_decrypt_block(PGD_DESC *pgd, int block)
{
	CIPHER_KEY ckey;
	u32 block_offset;

 	block_offset = block*pgd->block_size;

	// decrypt block data
	sceDrmBBCipherInit(&ckey, pgd->cipher_type, 2, pgd->dkey, pgd->vkey, block_offset>>4);
	sceDrmBBCipherUpdate(&ckey, pgd->block_buf, pgd->block_size);
	sceDrmBBCipherFinal(&ckey);

	return pgd->block_size;
}

int pgd_close(PGD_DESC *pgd)
{
	if(pgd){
		free(pgd->block_buf);
		free(pgd);
	}
	return 0;
}

/*************************************************************/

