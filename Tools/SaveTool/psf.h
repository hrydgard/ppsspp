/*
 * PSP Software Development Kit - http://www.pspdev.org
 * -----------------------------------------------------------------------
 * Licensed under the BSD license, see LICENSE in PSPSDK root for details.
 *
 * psf.h - Declarations for functions in psf.c
 *
 * Copyright (c) 2005 Jim Paris <jim@jtan.com>
 * Coypright (c) 2005 psp123
 *
 * $Id: psf.h 1559 2005-12-10 01:10:11Z jim $
 */

#include <pspchnnlsv.h>

/* Find the named section in the PSF file, and return an
   absolute pointer to it and the section size. */
int find_psf_section(const char *name,
		     unsigned char *data,
		     int dataLen,
		     unsigned char **location,
		     int *size);

/* Find the named file inside the FILE_LIST, and return
   an absolute pointer to it. */
int find_psf_datafile(const char *name,
		      unsigned char *filelist,
		      int size,
		      unsigned char **location);
