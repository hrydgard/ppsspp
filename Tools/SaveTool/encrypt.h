/*
 * PSP Software Development Kit - http://www.pspdev.org
 * -----------------------------------------------------------------------
 * Licensed under the BSD license, see LICENSE in PSPSDK root for details.
 *
 * encrypt.h - Declarations for functions in encrypt.c
 *
 * Copyright (c) 2005 Jim Paris <jim@jtan.com>
 * Coypright (c) 2005 psp123
 *
 * $Id: encrypt.h 1559 2005-12-10 01:10:11Z jim $
 */

#include <pspchnnlsv.h>

/* Encrypt the given plaintext file, and update the message
   authentication hashes in the param.sfo.  The data_filename is
   usually the final component of encrypted_filename, e.g. "DATA.BIN".
   See main.c for an example of usage. */
int encrypt_file(const char *plaintext_filename,
		 const char *encrypted_filename,
		 const char *data_filename,
		 const char *paramsfo_filename,
		 const char *paramsfo_filename_out,
		 const unsigned char *gamekey,
		 const int mainSdkVersion);

/* Do the actual hardware encryption.  
   mode is 3 for saves with a cryptkey, or 1 otherwise.
   data, alignedLen, cryptkey, and hash must be multiples of 0x10.
   cryptkey is NULL if mode == 1.
*/
int encrypt_data(unsigned int mode, 
		 unsigned char *data,
		 int *dataLen,
		 int *alignedLen,
		 unsigned char *hash,
		 unsigned char *cryptkey);
