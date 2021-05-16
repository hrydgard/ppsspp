
/* pngrio.c - functions for data input
 *
 * Last changed in libpng 1.7.0 [(PENDING RELEASE)]
 * Copyright (c) 1998-2002,2004,2006-2016 Glenn Randers-Pehrson
 * (Version 0.96 Copyright (c) 1996, 1997 Andreas Dilger)
 * (Version 0.88 Copyright (c) 1995, 1996 Guy Eric Schalnat, Group 42, Inc.)
 *
 * This code is released under the libpng license.
 * For conditions of distribution and use, see the disclaimer
 * and license in png.h
 *
 * This file provides a location for all input.  Users who need
 * special handling are expected to write a function that has the same
 * arguments as this and performs a similar function, but that possibly
 * has a different input method.  Note that you shouldn't change this
 * function, but rather write a replacement function and then make
 * libpng use it at run time with png_set_read_fn(...).
 */

#include "pngpriv.h"
#define PNG_SRC_FILE PNG_SRC_FILE_pngrio

#ifdef PNG_READ_SUPPORTED

/* Read the data from whatever input you are using.  The default routine
 * reads from a file pointer.  Note that this routine sometimes gets called
 * with very small lengths, so you should implement some kind of simple
 * buffering if you are using unbuffered reads.  This should never be asked
 * to read more than 64K on a 16 bit machine.
 */
void /* PRIVATE */
png_read_data(png_structrp png_ptr, png_voidp data, png_size_t length)
{
   png_debug1(4, "reading %d bytes", (int)length);

   /* This was guaranteed by prior versions of libpng, so app callbacks may
    * assume it even though it isn't documented to be the case.
    */
   debug(length > 0U);

   if (png_ptr->rw_data_fn != NULL)
      png_ptr->rw_data_fn(png_ptr, png_voidcast(png_bytep,data), length);

   else
      png_app_error(png_ptr, "No read function");
}

#ifdef PNG_STDIO_SUPPORTED
/* This is the function that does the actual reading of data.  If you are
 * not reading from a standard C stream, you should create a replacement
 * read_data function and use it at run time with png_set_read_fn(), rather
 * than changing the library.
 */
void PNGCBAPI
png_default_read_data(png_structp png_ptr, png_bytep data, png_size_t length)
{
   png_size_t check;

   if (png_ptr == NULL)
      return;

   /* fread() returns 0 on error, so it is OK to store this in a png_size_t
    * instead of an int, which is what fread() actually returns.
    */
   check = fread(data, 1, length, png_voidcast(png_FILE_p, png_ptr->io_ptr));

   if (check != length)
      png_error(png_ptr, "Read Error");
}
#endif /* STDIO */

/* This function allows the application to supply a new input function
 * for libpng if standard C streams aren't being used.
 *
 * This function takes as its arguments:
 *
 * png_ptr      - pointer to a png input data structure
 *
 * io_ptr       - pointer to user supplied structure containing info about
 *                the input functions.  May be NULL.
 *
 * read_data_fn - pointer to a new input function that takes as its
 *                arguments a pointer to a png_struct, a pointer to
 *                a location where input data can be stored, and a 32-bit
 *                unsigned int that is the number of bytes to be read.
 *                To exit and output any fatal error messages the new write
 *                function should call png_error(png_ptr, "Error msg").
 *                May be NULL, in which case libpng's default function will
 *                be used.
 */
void PNGAPI
png_set_read_fn(png_structrp png_ptr, png_voidp io_ptr,
    png_rw_ptr read_data_fn)
{
   if (png_ptr == NULL)
      return;

   if (!png_ptr->read_struct)
   {
      png_app_error(png_ptr, "cannot set a read function on a write struct");
      return;
   }

   if (read_data_fn == NULL)
   {
      png_app_error(png_ptr, "API change: png_set_read_fn requires a function");
      return;
   }

   png_ptr->io_ptr = io_ptr;
   png_ptr->rw_data_fn = read_data_fn;
}
#endif /* READ */
