/*
 * PSP Software Development Kit - http://www.pspdev.org
 * -----------------------------------------------------------------------
 * Licensed under the BSD license, see LICENSE in PSPSDK root for details.
 *
 * psf.c - PSF parsing routines
 *
 * Copyright (c) 2005 Jim Paris <jim@jtan.com>
 * Coypright (c) 2005 psp123
 *
 * $Id: psf.c 1560 2005-12-10 01:16:32Z jim $
 */

#include "psf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <pspchnnlsv.h>

/* Find to the named section in the PSF file, and return an
   absolute pointer to it and the section size. */
int find_psf_section(const char *name,
		     unsigned char *data,
		     int dataLen,
		     unsigned char **location,
		     int *size)
{
	unsigned short int nameLoc;
        int i, magicHead, strLoc, headLen, numSects;
	int sectCurLen, sectBufLen, sectBufLoc, curPos;

	if (dataLen < 0x14)
		return -1;

	/* Get the basics from the header */
        magicHead = *(unsigned int *)&data[0x00];
        strLoc = *(unsigned int *)&data[0x08];
        headLen = *(unsigned int *)&data[0x0C];
        numSects = *(unsigned int *)&data[0x10];

        /* Do some error checking */
        if (magicHead != 0x46535000)
                return -2;

        /* Verify strLoc is proper */
        if ((strLoc > headLen) || (strLoc >= dataLen))
                return -3;

        /* Verify headLen is proper */
        if (headLen >= dataLen)
                return -4;

        /* Verify numSects is proper */
        if (numSects != ((strLoc - 0x14) / 0x10))
                return -5;

	/* Process all sections */
        for (i = 0; i < numSects; i++)
        {
                /* Get the curPos */
                curPos = 0x14 + (i * 0x10);

                /* Verify curPos is proper */
                if (curPos >= strLoc)
                        return -6;

                /* Get some basic info about this section */
                nameLoc = *(unsigned short *)&data[curPos];
                sectCurLen = *(unsigned short *)&data[curPos + 0x04];
                sectBufLen = *(unsigned short *)&data[curPos + 0x08];
                sectBufLoc = *(unsigned short *)&data[curPos + 0x0C];

                /* Do some error checking */
                if ((nameLoc < dataLen) && (sectCurLen < dataLen)
		    && (sectBufLen < dataLen) && (sectBufLoc < dataLen))
                {
                        /* Check if this is the section we want */
                        if (!stricmp((char *)&data[strLoc + nameLoc], name))
                        {
                                /* Update the location and size */
                                *location = &data[headLen + sectBufLoc];
                                *size = sectBufLen;
                                return 0;
                        }
                }
        }

        /* Section was not found if it makes it here */
        return -7;
}

/* Find the named file inside the FILE_LIST, and return
   an absolute pointer to it. */
int find_psf_datafile(const char *name,
		      unsigned char *filelist,
		      int size,
		      unsigned char **location)
{
        int i;
	
        /* Process all files */
        for (i = 0; (i + 0x0d) <= size; i += 0x20)
        {
                /* Check if this is the filename we want */
		if (!strncasecmp((char *)&filelist[i], name, 0x0d)) {
			*location = &filelist[i];
			return 0;
		}
        }
	
        /* File was not found if it makes it here */
        return -1;
}
