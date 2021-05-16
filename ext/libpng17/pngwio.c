
/* pngwio.c - functions for data output
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
 * This file provides a location for all output.  Users who need
 * special handling are expected to write functions that have the same
 * arguments as these and perform similar functions, but that possibly
 * use different output methods.  Note that you shouldn't change these
 * functions, but rather write replacement functions and then change
 * them at run time with png_set_write_fn(...).
 */

#include "pngpriv.h"
#define PNG_SRC_FILE PNG_SRC_FILE_pngwio

#ifdef PNG_WRITE_SUPPORTED

/* Write the data to whatever output you are using.  The default routine
 * writes to a file pointer.  Note that this routine sometimes gets called
 * with very small lengths, so you should implement some kind of simple
 * buffering if you are using unbuffered writes.  This should never be asked
 * to write more than 64K on a 16 bit machine.
 */

void /* PRIVATE */
png_write_data(png_structrp png_ptr, png_const_voidp data, png_size_t length)
{
   /* This was guaranteed by prior versions of libpng, so app callbacks may
    * assume it even though it isn't documented to be the case.
    */
   debug(length > 0U);

   /* NOTE: write_data_fn must not change the buffer!
    * This cast is required because of the API; changing the type of the
    * callback would require every app to change the callback and that change
    * would have to be conditional on the libpng version.
    */
   if (png_ptr->rw_data_fn != NULL )
      png_ptr->rw_data_fn(png_ptr,
          png_constcast(png_bytep,png_voidcast(png_const_bytep,data)), length);

   else
      png_app_error(png_ptr, "No write function");
}

#ifdef PNG_STDIO_SUPPORTED
/* This is the function that does the actual writing of data.  If you are
 * not writing to a standard C stream, you should create a replacement
 * write_data function and use it at run time with png_set_write_fn(), rather
 * than changing the library.
 */
void PNGCBAPI
png_default_write_data(png_structp png_ptr, png_bytep data, png_size_t length)
{
   png_size_t check;

   if (png_ptr == NULL)
      return;

   check = fwrite(data, 1, length, png_voidcast(png_FILE_p, png_ptr->io_ptr));

   if (check != length)
      png_error(png_ptr, "Write Error");
}
#endif

#ifdef PNG_WRITE_FLUSH_SUPPORTED
#  ifdef PNG_STDIO_SUPPORTED
void PNGCBAPI
png_default_flush(png_structp png_ptr)
{
   if (png_ptr == NULL)
      return;

   fflush(png_voidcast(png_FILE_p, (png_ptr->io_ptr)));
}
#  endif
#endif

/* This function allows the application to supply new output functions for
 * libpng if standard C streams aren't being used.
 *
 * This function takes as its arguments:
 * png_ptr       - pointer to a png output data structure
 * io_ptr        - pointer to user supplied structure containing info about
 *                 the output functions.  May be NULL.
 * write_data_fn - pointer to a new output function that takes as its
 *                 arguments a pointer to a png_struct, a pointer to
 *                 data to be written, and a 32-bit unsigned int that is
 *                 the number of bytes to be written.  The new write
 *                 function should call png_error(png_ptr, "Error msg")
 *                 to exit and output any fatal error messages.  May be
 *                 NULL, in which case libpng's default function will
 *                 be used.
 * flush_data_fn - pointer to a new flush function that takes as its
 *                 arguments a pointer to a png_struct.  After a call to
 *                 the flush function, there should be no data in any buffers
 *                 or pending transmission.  If the output method doesn't do
 *                 any buffering of output, a function prototype must still be
 *                 supplied although it doesn't have to do anything.  If
 *                 PNG_WRITE_FLUSH_SUPPORTED is not defined at libpng compile
 *                 time, output_flush_fn will be ignored, although it must be
 *                 supplied for compatibility.  May be NULL, in which case
 *                 libpng's default function will be used, if
 *                 PNG_WRITE_FLUSH_SUPPORTED is defined.  This is not
 *                 a good idea if io_ptr does not point to a standard
 *                 *FILE structure.
 */
void PNGAPI
png_set_write_fn(png_structrp png_ptr, png_voidp io_ptr,
    png_rw_ptr write_data_fn, png_flush_ptr output_flush_fn)
{
   if (png_ptr == NULL)
      return;

   if (png_ptr->read_struct)
   {
      png_app_error(png_ptr, "cannot set a write function on a read struct");
      return;
   }

   if (write_data_fn == NULL)
   {
      png_app_error(png_ptr,
          "API change: png_set_write_fn requires a function");
      return;
   }

   png_ptr->io_ptr = io_ptr;
   png_ptr->rw_data_fn = write_data_fn;
#  ifdef PNG_WRITE_FLUSH_SUPPORTED
      if (output_flush_fn != NULL)
         png_ptr->output_flush_fn = output_flush_fn;
#  else
      PNG_UNUSED(output_flush_fn)
#  endif /* WRITE_FLUSH */
}
#endif /* WRITE */
