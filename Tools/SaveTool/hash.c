/*
 * PSP Software Development Kit - http://www.pspdev.org
 * -----------------------------------------------------------------------
 * Licensed under the BSD license, see LICENSE in PSPSDK root for details.
 *
 * hash.c - Hashing routines using sceChnnlsv
 *
 * Copyright (c) 2005 Jim Paris <jim@jtan.com>
 * Coypright (c) 2005 psp123
 *
 * $Id: hash.c 1560 2005-12-10 01:16:32Z jim $
 */

#include "hash.h"
#include "psf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <pspchnnlsv.h>
#include "kernelcall/kernelcall.h"

static inline int align16(unsigned int v)
{
	return ((v + 0xF) >> 4) << 4;
}

/* Update the hashes in the param.sfo data, using
   the given file hash, and by computing the param.sfo hashes.
   filehash must be a multiple of 16 bytes, and is reused to
   store other hashes.  The filename is e.g. "DATA.BIN". */
int update_hashes(unsigned char *data,
		  int len,
		  const char *filename,
		  unsigned char *filehash,
		  int encryptmode)
{
	int alignedLen = align16(len);
	unsigned char *datafile, *savedata_params;
	int listLen, paramsLen;
	int ret;

	/* Locate SAVEDATA_PARAM section in the param.sfo. */
	if ((ret = find_psf_section("SAVEDATA_PARAMS", data, 0x1330,
				    &savedata_params, &paramsLen)) < 0) {
		return ret - 100;
	}
	
	/* Locate the pointer for this DATA.BIN equivalent */ 
	if ((ret = find_psf_section("SAVEDATA_FILE_LIST", data, 0x1330,
				    &datafile, &listLen)) < 0) {
		return ret - 200;
	}

	if ((ret = find_psf_datafile(filename, datafile, 
				     listLen, &datafile)) < 0) {
		return ret - 300;
	}
	
	/* Check minimum sizes based on where we want to write */
	if ((listLen < 0x20) || (paramsLen < 0x80)) {
		return -1;
	}
	
	/* Clear params and insert file hash */
	memset(savedata_params, 0, paramsLen);
	memcpy(datafile + 0x0D, filehash, 0x10);

	/* Compute 11D0 hash over entire file */
	if ((ret = build_hash(filehash, data, len, alignedLen, 
			      (encryptmode & 2) ? 4 : 2, NULL)) < 0) {	// Not sure about "2"
		return ret - 400;
	}

	/* Copy 11D0 hash to param.sfo and set flag indicating it's there */
	memcpy(savedata_params + 0x20, filehash, 0x10);
	*savedata_params |= 0x01;

	/* If new encryption mode, compute and insert the 1220 hash. */
	if (encryptmode & 2) {

		/* Enable the hash bit first */
		*savedata_params |= 0x20;
		
		if ((ret = build_hash(filehash, data, len, alignedLen, 
				      3, 0)) < 0) {
			return ret - 500;
		}
		memcpy(savedata_params + 0x70, filehash, 0x10);
	}

	/* Compute and insert the 11C0 hash. */
	if ((ret = build_hash(filehash, data, len, alignedLen, 1, 0)) < 0) {
		return ret - 600;
	}
	memcpy(savedata_params + 0x10, filehash, 0x10);

	/* All done. */
	return 0;
}

/* Build a single hash using the given data and mode. 
   data and alignedLen must be multiples of 0x10.
   cryptkey is NULL for savedata. */
int build_hash(unsigned char *output,
	       unsigned char *data,
	       unsigned int len,
	       unsigned int alignedLen,
	       int mode,
	       unsigned char *cryptkey)
{
	pspChnnlsvContext1 ctx1;

	/* Set up buffers */
	memset(&ctx1, 0, sizeof(pspChnnlsvContext1));
	memset(output, 0, 0x10);
	memset(data + len, 0, alignedLen - len);

	/* Perform the magic */
	if (sceChnnlsv_E7833020_(&ctx1, mode & 0xFF) < 0)
                return -1;
        if (sceChnnlsv_F21A1FCA_(&ctx1, data, alignedLen) < 0)
                return -2;
        if (sceChnnlsv_C4C494F8_(&ctx1, output, cryptkey) < 0)
                return -3;

	/* All done. */
	return 0;
}
