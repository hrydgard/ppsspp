/*
 * PSP Software Development Kit - http://www.pspdev.org
 * -----------------------------------------------------------------------
 * Licensed under the BSD license, see LICENSE in PSPSDK root for details.
 *
 * hash.h - Declarations for functions in hash.c
 *
 * Copyright (c) 2005 Jim Paris <jim@jtan.com>
 * Coypright (c) 2005 psp123
 *
 * $Id: hash.h 1559 2005-12-10 01:10:11Z jim $
 */

#include <pspchnnlsv.h>

/* Update the hashes in the param.sfo data, using
   the given file hash, and by computing the param.sfo hashes.
   filehash must be a multiple of 16 bytes, and is reused to
   store other hashes.  The filename is e.g. "DATA.BIN". */
int update_hashes(unsigned char *data,
		  int len,
		  const char *filename,
		  unsigned char *filehash,
		  int encryptmode);

/* Build a single hash using the given data and mode. 
   data, and alignedLen must be multiples of 0x10.
   cryptkey is NULL for savedata.*/
int build_hash(unsigned char *output,
	       unsigned char *data,
	       unsigned int len,
	       unsigned int alignedLen,
	       int mode,
	       unsigned char *cryptkey);
