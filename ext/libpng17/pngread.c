
/* pngread.c - read a PNG file
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
 * This file contains routines that an application calls directly to
 * read a PNG file or stream.
 */

#include "pngpriv.h"
#if defined(PNG_SIMPLIFIED_READ_SUPPORTED) && defined(PNG_STDIO_SUPPORTED)
#  include <errno.h>
#endif
#define PNG_SRC_FILE PNG_SRC_FILE_pngread

#ifdef PNG_READ_SUPPORTED
/* Create a PNG structure for reading, and allocate any memory needed. */
PNG_FUNCTION(png_structp,PNGAPI
png_create_read_struct,(png_const_charp user_png_ver, png_voidp error_ptr,
    png_error_ptr error_fn, png_error_ptr warn_fn),PNG_ALLOCATED)
{
#ifndef PNG_USER_MEM_SUPPORTED
   png_structp png_ptr = png_create_png_struct(user_png_ver, error_ptr,
        error_fn, warn_fn, NULL, NULL, NULL);
#else
   return png_create_read_struct_2(user_png_ver, error_ptr, error_fn,
        warn_fn, NULL, NULL, NULL);
}

/* Alternate create PNG structure for reading, and allocate any memory
 * needed.
 */
PNG_FUNCTION(png_structp,PNGAPI
png_create_read_struct_2,(png_const_charp user_png_ver, png_voidp error_ptr,
    png_error_ptr error_fn, png_error_ptr warn_fn, png_voidp mem_ptr,
    png_malloc_ptr malloc_fn, png_free_ptr free_fn),PNG_ALLOCATED)
{
   png_structp png_ptr = png_create_png_struct(user_png_ver, error_ptr,
       error_fn, warn_fn, mem_ptr, malloc_fn, free_fn);
#endif /* USER_MEM */

   if (png_ptr != NULL)
   {
      png_ptr->read_struct = 1;
      png_ptr->critical_crc = crc_error_quit;
      png_ptr->ancillary_crc = crc_warn_discard;

#     ifdef PNG_BENIGN_ERRORS_SUPPORTED
#        if !PNG_RELEASE_BUILD
            /* Always quit on error prior to release */
            png_ptr->benign_error_action = PNG_ERROR;
            png_ptr->app_warning_action = PNG_WARN;
            png_ptr->app_error_action = PNG_ERROR;
#        else /* RELEASE_BUILD */
            /* Allow benign errors on read, subject to app control. */
            png_ptr->benign_error_action = PNG_WARN;
#           ifdef PNG_BENIGN_READ_ERRORS_SUPPORTED
               png_ptr->app_error_action = PNG_WARN;
               png_ptr->app_warning_action = PNG_WARN;
#           else /* !BENIGN_READ_ERRORS */
               /* libpng build without benign error support; the application
                * author has to be assumed to be correct, so:
                */
               png_ptr->app_warning_action = PNG_WARN;
               png_ptr->app_error_action = PNG_ERROR;
#           endif /* !BENIGN_READ_ERRORS */
#        endif /* RELEASE_BUILD */

         /* This is always png_error unless explicitly changed: */
         png_ptr->IDAT_error_action = PNG_ERROR;
#     endif /* BENIGN_ERRORS */

#  ifdef PNG_SEQUENTIAL_READ_SUPPORTED
      png_ptr->IDAT_size = PNG_IDAT_READ_SIZE;
#  endif /* SEQUENTIAL_READ */

#  ifdef PNG_READ_GAMMA_SUPPORTED
      /* Default gamma correction values: */
#     if 0 /*NYI*/
         png_ptr->gamma_accuracy = PNG_DEFAULT_GAMMA_ACCURACY;
#     endif /*NYI*/
      png_ptr->gamma_threshold = PNG_GAMMA_THRESHOLD_FIXED;
#  endif /* READ_GAMMA */
   }

   return png_ptr;
}


#ifdef PNG_SEQUENTIAL_READ_SUPPORTED
/* Read the chunk header (length + type name).
 * Put the type name into png_ptr->chunk_name, and return the length.
 */
static void
png_read_chunk_header(png_structrp png_ptr)
{
   png_byte buf[8];

#ifdef PNG_IO_STATE_SUPPORTED
   png_ptr->io_state = PNG_IO_READING | PNG_IO_CHUNK_HDR;
#endif

   /* Read the length and the chunk name.
    * This must be performed in a single I/O call.
    */
   png_read_data(png_ptr, buf, 8);

   /* Put the chunk name into png_ptr->chunk_name. */
   png_ptr->chunk_length = png_get_uint_31(png_ptr, buf);
   png_ptr->chunk_name = PNG_CHUNK_FROM_STRING(buf+4);

   png_debug2(0, "Reading %lx chunk, length = %lu",
       (unsigned long)png_ptr->chunk_name,
       (unsigned long)png_ptr->chunk_length);

   /* Reset the crc and run it over the chunk name. */
   png_reset_crc(png_ptr, buf + 4);

#ifdef PNG_IO_STATE_SUPPORTED
   png_ptr->io_state = PNG_IO_READING | PNG_IO_CHUNK_DATA;
#endif
}

static void
png_read_sequential_unknown(png_structrp png_ptr, png_inforp info_ptr)
{
#ifdef PNG_READ_UNKNOWN_CHUNKS_SUPPORTED
   /* Read the data for an unknown chunk.  The read buffer is used: */
   png_bytep buffer = png_read_buffer(png_ptr, png_ptr->chunk_length,
      PNG_CHUNK_ANCILLARY(png_ptr->chunk_name)); /* error if critical */

   if (buffer != NULL)
   {
      if (png_ptr->chunk_length > 0U)
         png_crc_read(png_ptr, buffer, png_ptr->chunk_length);

      png_crc_finish(png_ptr, 0);
      png_handle_unknown(png_ptr, info_ptr, buffer);
   }

   else /* out of memory on an ancillary chunk; skip the chunk */
#else /* !READ_UNKNOWN_CHUNKS */
      /* or, no support for reading unknown chunks, so just skip it. */
      PNG_UNUSED(info_ptr)
#endif /* !READ_UNKNOWN_CHUNKS */
      png_crc_finish(png_ptr, png_ptr->chunk_length);
}

/* Read the information before the actual image data.  This has been
 * changed in v0.90 to allow reading a file that already has the magic
 * bytes read from the stream.  You can tell libpng how many bytes have
 * been read from the beginning of the stream (up to the maximum of 8)
 * via png_set_sig_bytes(), and we will only check the remaining bytes
 * here.  The application can then have access to the signature bytes we
 * read if it is determined that this isn't a valid PNG file.
 */
void PNGAPI
png_read_info(png_structrp png_ptr, png_inforp info_ptr)
{
   png_debug(1, "in png_read_info");

   if (png_ptr == NULL || info_ptr == NULL)
      return;

   /* Read and check the PNG file signature (this may do nothing if it has
    * already been read.)
    */
   png_read_sig(png_ptr, info_ptr);

   /* Loop reading chunks until an IDAT is encountered or we reach the end of
    * the stream (IEND).
    *
    * Prior to 1.7.0 this function behaved very weirdly if called after the
    * IDATs had been read; it would keep on reading chunks util it found
    * another IDAT.  This could cause it to read beyond IEND, damaging the
    * state in the host stream.  This is now caught by the check below.
    */
   while ((png_ptr->mode & (PNG_HAVE_IEND|PNG_HAVE_IDAT)) == 0)
   {
      png_read_chunk_header(png_ptr);
      switch (png_find_chunk_op(png_ptr))
      {
         default:
            impossible("invalid chunk op");
            /* FALL THROUGH */
         case png_chunk_skip:
            png_crc_finish(png_ptr, png_ptr->chunk_length);
            break;

         case png_chunk_unknown:
            png_read_sequential_unknown(png_ptr, info_ptr);
            break;

         case png_chunk_process_all:
            png_handle_chunk(png_ptr, info_ptr);
            break;

         case png_chunk_process_part:
            debug(png_ptr->mode & PNG_HAVE_IDAT);
            return;
      }
   }

   /* The loop was ended by IDAT or IEND, but if an IEND was seen the read code
    * (png_handle_position in pngrutil.c) should have errored out, therefore:
    */
#ifdef PNG_HANDLE_AS_UNKNOWN_SUPPORTED
      affirm(png_ptr->chunk_name == png_IDAT && ((png_ptr->known_unknown)&1U));
#else
      debug(png_ptr->chunk_name == png_IDAT);
      impossible("unknown IDAT");
#endif

   /* And the code cannot have left it unread; it must have called one of the
    * handlers, so we are skipping IDAT.
    */
}

/* Initialize palette, background, etc, after transformations
 * are set, but before any reading takes place.  This allows
 * the user to obtain a gamma-corrected palette, for example.
 * If the user doesn't call this, we will do it ourselves.
 */
void PNGAPI
png_start_read_image(png_structrp png_ptr)
{
   png_debug(1, "in png_start_read_image");

   if (png_ptr != NULL)
   {
      if (png_ptr->zowner != png_IDAT)
         png_read_start_IDAT(png_ptr);

      /* New in 1.6.0 this avoids the bug of doing the initializations twice,
       * it could be a warning but in practice it indicates that the app may
       * have made png_get_ calls on png_ptr assuming that it hadn't been
       * 'started'.
       */
      else
         png_app_error(png_ptr,
             "png_start_read_image/png_read_update_info: duplicate call");
   }
}

static void
png_read_IDAT(png_structrp png_ptr)
{
   /* Read more input data, up to png_struct::IDAT_size, stop at the end of the
    * IDAT stream.  pngset.c checks png_struct::IDAT_size to ensure that it will
    * fit in a uInt.
    */
   const uInt buffer_size = (uInt)/*SAFE*/png_ptr->IDAT_size;
   uInt IDAT_size = 0;
   png_bytep buffer =
      png_read_buffer(png_ptr, buffer_size, 0/*error*/);

   png_ptr->zstream.next_in = buffer;

   while (png_ptr->chunk_name == png_IDAT && IDAT_size < buffer_size)
   {
      png_uint_32 l = png_ptr->chunk_length;

      while (l == 0) /* end of this IDAT */
      {
         png_crc_finish(png_ptr, 0);
         png_read_chunk_header(png_ptr);

         if (png_ptr->chunk_name != png_IDAT) /* end of all IDAT */
         {
            png_ptr->mode |= PNG_AFTER_IDAT;
            goto done;
         }

         l = png_ptr->chunk_length;
      }

      /* Read from the IDAT chunk into the buffer, up to png_struct::IDAT_size:
       */
      if (l > buffer_size - IDAT_size) /* SAFE: while check */
         l = buffer_size - IDAT_size;

      png_crc_read(png_ptr, buffer+IDAT_size, l);
      IDAT_size += (uInt)/*SAFE*/l;
      png_ptr->chunk_length -= l;
   }
done:

   /* IDAT_size may be zero if the compressed image stream is truncated;
    * this is likely given a broken PNG.
    */
   png_ptr->zstream.next_in = buffer;
   png_ptr->zstream.avail_in = IDAT_size;
}

void PNGAPI
png_read_row(png_structrp png_ptr, png_bytep row, png_bytep dsp_row)
   /* It is valid to call this API with both 'row' and 'dsp_row' NULL, all
    * the processing gets done.  This is only useful for, either, performance
    * testing (but it skips png_combine_row) or if there is a user transform
    * or user row callback which actually uses the row data.
    */
{
   if (png_ptr == NULL)
      return;

   png_debug2(1, "in png_read_row (row %lu, pass %d)",
       (unsigned long)png_ptr->row_number, png_ptr->pass);

   /* Check the row number; if png_read_process_IDAT is called too many times
    * if issues an affirm, but, while this is appropriate for the progressive
    * reader, it is an app error if it happens here.
    *
    * Note that when the app does the interlace handling the last row will
    * typically be before the last row in the image.
    */
   if (png_ptr->read_started &&
       (png_ptr->interlaced == PNG_INTERLACE_NONE ?
         png_ptr->row_number == png_ptr->height-1U : (
#     ifdef PNG_READ_INTERLACING_SUPPORTED
         png_ptr->do_interlace ?
            png_ptr->pass == 6U && png_ptr->row_number == png_ptr->height-1U :
#     endif /* READ_INTERLACING */
            png_ptr->pass == PNG_LAST_PASS(png_ptr->width, png_ptr->height) &&
               PNG_LAST_PASS_ROW(png_ptr->row_number, png_ptr->pass,
                  png_ptr->height)
         )
       ))
   {
      png_app_error(png_ptr, "Too many calls to png_read_row");
      return;
   }

   /* Check this right at the start; functions like png_read_process_IDAT
    * regard this condition as an internal error:
    */
   if (png_ptr->zowner != png_IDAT)
      png_read_start_IDAT(png_ptr);

   /* So reading has started: */
   png_ptr->read_started = 1;

   for (;;)
   {
      if (png_ptr->zstream.avail_in == 0)
         png_read_IDAT(png_ptr);

      /* So... zstream.next_in may still be 0, but this may be enough for the
       * next row if zlib is storing enough output state (it only need be enough
       * for one byte, because png_read_process_IDAT keeps the next filter byte,
       * so on the last row of the image only one byte might be required.)
       *
       * png_read_process_IDAT handles the case where the input has ended; mode
       * has PNG_AFTER_IDAT set, by either doing png_error or using 0 bytes for
       * the data (after issuing a warning.)
       */
      switch (png_read_process_IDAT(png_ptr, row, dsp_row, 0/*no save*/))
      {
         case png_row_incomplete:
            /* more IDAT data needed for row */
            debug(png_ptr->zstream.avail_in == 0);
            continue;

         case png_row_repeat:
            /* row not in this pass, but the existing row in row_buffer or (if
             * transforms are happening) png_struct::transformed_row is
             * available from a previous row.
             */
            /* FALL THROUGH */
         case png_row_skip:
            /* row not in pass and no appropriate data; skip this row, nothing
             * more need be done, except the read_row_fn and then only if libpng
             * is doing the interlace handling (this is the historical
             * behavior!)
             */
#           ifdef PNG_READ_INTERLACING_SUPPORTED
               if (!png_ptr->do_interlace) continue;
#           else /* !do_interlace */
               continue;
#           endif /* !do_interlace */
            /* FALL THROUGH */
         case png_row_process:
            /* png_read_process_IDAT has done everything we need, the only extra
             * required is to call the application row callback.
             */
            if (png_ptr->read_row_fn != NULL)
               png_ptr->read_row_fn(png_ptr, png_ptr->row_number,
                  png_ptr->pass);
            /* And return now because the next row has been processed; so there
             * is exactly one read_row_fn callback for each call to
             * png_read_process_IDAT.
             */
            return;

         default:
            impossible("not reached");
      }
   }
}

/* Read one or more rows of image data.  If the image is interlaced,
 * and png_set_interlace_handling() has been called, the rows need to
 * contain the contents of the rows from the previous pass.  If the
 * image has alpha or transparency, and png_handle_alpha()[*] has been
 * called, the rows contents must be initialized to the contents of the
 * screen.
 *
 * "row" holds the actual image, and pixels are placed in it
 * as they arrive.  If the image is displayed after each pass, it will
 * appear to "sparkle" in.  "display_row" can be used to display a
 * "chunky" progressive image, with finer detail added as it becomes
 * available.  If you do not want this "chunky" display, you may pass
 * NULL for display_row.  If you do not want the sparkle display, and
 * you have not called png_handle_alpha(), you may pass NULL for rows.
 * If you have called png_handle_alpha(), and the image has either an
 * alpha channel or a transparency chunk, you must provide a buffer for
 * rows.  In this case, you do not have to provide a display_row buffer
 * also, but you may.  If the image is not interlaced, or if you have
 * not called png_set_interlace_handling(), the display_row buffer will
 * be ignored, so pass NULL to it.
 *
 * [*] png_handle_alpha() does not exist yet, as of this version of libpng
 */

void PNGAPI
png_read_rows(png_structrp png_ptr, png_bytepp row,
    png_bytepp display_row, png_uint_32 num_rows)
{
   png_uint_32 i;
   png_bytepp rp;
   png_bytepp dp;

   png_debug(1, "in png_read_rows");

   if (png_ptr == NULL)
      return;

   rp = row;
   dp = display_row;
   if (rp != NULL && dp != NULL)
      for (i = 0; i < num_rows; i++)
      {
         png_bytep rptr = *rp++;
         png_bytep dptr = *dp++;

         png_read_row(png_ptr, rptr, dptr);
      }

   else if (rp != NULL)
      for (i = 0; i < num_rows; i++)
      {
         png_bytep rptr = *rp;
         png_read_row(png_ptr, rptr, NULL);
         rp++;
      }

   else if (dp != NULL)
      for (i = 0; i < num_rows; i++)
      {
         png_bytep dptr = *dp;
         png_read_row(png_ptr, NULL, dptr);
         dp++;
      }
}
#endif /* SEQUENTIAL_READ */

#ifdef PNG_READ_IMAGE_SUPPORTED
/* Read the entire image.  If the image has an alpha channel or a tRNS
 * chunk, and you have called png_handle_alpha()[*], you will need to
 * initialize the image to the current image that PNG will be overlaying.
 * We set the num_rows again here, in case it was incorrectly set in
 * png_read_start_IDAT() by a call to png_read_update_info() or
 * png_start_read_image() if png_set_interlace_handling() wasn't called
 * prior to either of these functions like it should have been.  You can
 * only call this function once.  If you desire to have an image for
 * each pass of a interlaced image, use png_read_rows() instead.
 *
 * [*] png_handle_alpha() does not exist yet, as of this version of libpng
 */
void PNGAPI
png_read_image(png_structrp png_ptr, png_bytepp image)
{
   png_uint_32 image_height;
   int pass, j;

   png_debug(1, "in png_read_image");

   if (png_ptr == NULL)
      return;

   if (png_ptr->zowner != png_IDAT)
      pass = png_set_interlace_handling(png_ptr);

   else
   {
      if (png_ptr->interlaced == 0)
         pass = 1;

      else
         pass = PNG_INTERLACE_ADAM7_PASSES;
   }

   for (j = 0, image_height = png_ptr->height; j < pass; ++j)
   {
      png_bytepp rp = image;
      png_uint_32 i;

      for (i = 0; i < image_height; i++)
      {
         png_read_row(png_ptr, *rp, NULL);
         rp++;
      }
   }
}
#endif /* READ_IMAGE */

#ifdef PNG_SEQUENTIAL_READ_SUPPORTED
/* Read the end of the PNG file.  Will not read past the end of the
 * file, will verify the end is accurate, and will read any comments
 * or time information at the end of the file, if info is not NULL.
 */
void PNGAPI
png_read_end(png_structrp png_ptr, png_inforp info_ptr)
{
   png_debug(1, "in png_read_end");

   if (png_ptr == NULL)
      return;

   /* When this routine is entered it is possible that an IDAT chunk still
    * remains to be read.  There are three conditions:
    *
    * 1) The app decided to handle IDAT as unknown, libpng will have consumed
    *    the first IDAT in png_read_info, the rest will be consumed as normal
    *    chunks by calls to png_handle_chunk below.
    *
    * 2) The app did not start to read an image, so png_read_start_IDAT was
    *    not called and png_struct::zowner is not png_IDAT.  The first IDAT
    *    must still be skipped then the code below will skip the remainder.
    *
    * 3) The app did start to read the image.  png_struct::zowner is png_IDAT
    *    and we need to close down the IDAT reading code.  There may also be
    *    pending IDAT chunks, these are passed to png_read_finish_IDAT here so
    *    that error detection happens.  If the app didn't read all the rows
    *    libpng will issue an 'extra compressed data' error, we could supress
    *    that by warning that not all the rows have been read and setting
    *    png_struct::zstream_error if necessary.
    */
#  ifdef PNG_HANDLE_AS_UNKNOWN_SUPPORTED
   if (!(png_ptr->known_unknown & 1U))
#  endif
   {
      if (png_ptr->zowner == png_IDAT)
      {
         /* Normal case: read to the end of the IDAT chunks.  In about
          * 5/PNG_IDAT_READ_SIZE cases (typically that's 1:820) zlib will have
          * returned all the image data but not read up to the end of the
          * Adler32 because the end of the stream had not been read.  Make sure
          * it gets read here:
          */
         if (png_ptr->zstream.avail_in == 0)
            png_read_IDAT(png_ptr);

         while (!png_read_finish_IDAT(png_ptr)) {
            /* This will adjust zstream.next/avail_in appropriately and if
             * necessary read the next chunk.  After this avail_in may still
             * be zero, but if it is then PNG_AFTER_IDAT should be set.
             */
            debug(png_ptr->zstream.avail_in == 0);
            png_read_IDAT(png_ptr);
            debug(png_ptr->zstream.avail_in > 0 ||
                  (png_ptr->mode & PNG_AFTER_IDAT) != 0);
         }

         debug(png_ptr->zstream.avail_in == 0 && png_ptr->zowner == 0);

         /* If this is still an IDAT then it hasn't been finished; at least
          * the CRC has not been read.  If there is data left in it then
          * an error may need to be output.  Note that the code below handles
          * any additional chunks.
          */
         if (png_ptr->chunk_name == png_IDAT)
         {
            if (png_ptr->chunk_length > 0 && !png_ptr->zstream_error)
            {
               png_chunk_benign_error(png_ptr, "too much IDAT data (read)");
               png_ptr->zstream_error = 1;
            }

            png_crc_finish(png_ptr, png_ptr->chunk_length);
            png_read_chunk_header(png_ptr);
         }
      }

      else if (png_ptr->chunk_name == png_IDAT)
      {
         /* This IDAT has not been processed, the remainder will be finished
          * in the loop.  This is the case where IDAT is being skipped because
          * the rows weren't read, this is OK, but warn anyway.
          */
         png_crc_finish(png_ptr, png_ptr->chunk_length);
         png_app_warning(png_ptr, "image reading skipped");
         png_ptr->zstream_error = 1; /* Prevent 'too much IDAT' errors */
         png_read_chunk_header(png_ptr);
      }

      else /* This might work, if the signature was read, but just in case: */
         png_app_error(png_ptr, "Missing call to png_read_info");
   }

#  ifdef PNG_HANDLE_AS_UNKNOWN_SUPPORTED
   else
   {
      /* IDAT is unknown, the chunk that terminated the loop must be an IDAT
       * and it has been processed.  Get a new chunk header.
       */
      if (png_ptr->chunk_name == png_IDAT)
         png_read_chunk_header(png_ptr);

      else
         png_app_error(png_ptr,
               "Missing call to png_read_info with unknown IDAT");
   }
#  endif

   if ((png_ptr->mode & PNG_HAVE_IEND) == 0) for (;;)
   {
      switch (png_find_chunk_op(png_ptr))
      {
         default:
            impossible("invalid chunk op");
            /* FALL THROUGH */
         case png_chunk_skip:
            png_crc_finish(png_ptr, png_ptr->chunk_length);
            break;

         case png_chunk_unknown:
            png_read_sequential_unknown(png_ptr, info_ptr);
            break;

         case png_chunk_process_all:
            png_handle_chunk(png_ptr, info_ptr);
            break;

         case png_chunk_process_part:
            debug(png_ptr->chunk_name == png_IDAT);
            debug(!(png_ptr->mode & PNG_AFTER_IDAT));
            if (png_ptr->chunk_length > 0 && !png_ptr->zstream_error)
            {
               png_chunk_benign_error(png_ptr, "too many IDAT chunks");
               png_ptr->zstream_error = 1;
            }

            /* Skip it: */
            png_crc_finish(png_ptr, png_ptr->chunk_length);
            return;
      }

      if ((png_ptr->mode & PNG_HAVE_IEND) != 0)
         break;

      png_read_chunk_header(png_ptr);
   }
}
#endif /* SEQUENTIAL_READ */

/* Free all memory used in the read struct */
static void
png_read_destroy(png_structrp png_ptr)
{
   png_debug(1, "in png_read_destroy");

   png_read_free_row_buffers(png_ptr);

   png_free(png_ptr, png_ptr->read_buffer);
   png_ptr->read_buffer = NULL;

   if (png_ptr->palette != NULL)
   {
      png_free(png_ptr, png_ptr->palette);
      png_ptr->num_palette = 0;
      png_ptr->palette = NULL;
   }

#ifdef PNG_READ_tRNS_SUPPORTED
   if (png_ptr->trans_alpha != NULL)
   {
      png_free(png_ptr, png_ptr->trans_alpha);
      png_ptr->num_trans = 0;
      png_ptr->trans_alpha = NULL;
   }
#endif

   if (png_ptr->zstream.state != NULL)
   {
      int ret = inflateEnd(&png_ptr->zstream);

      if (ret != Z_OK)
      {
         png_zstream_error(&png_ptr->zstream, ret);
         png_warning(png_ptr, png_ptr->zstream.msg);
      }
   }

#ifdef PNG_TRANSFORM_MECH_SUPPORTED
   png_transform_free(png_ptr, &png_ptr->transform_list);
#endif

#ifdef PNG_PROGRESSIVE_READ_SUPPORTED
   png_free(png_ptr, png_ptr->save_buffer);
   png_ptr->save_buffer = NULL;
#endif

#ifdef PNG_SET_UNKNOWN_CHUNKS_SUPPORTED
   png_free(png_ptr, png_ptr->chunk_list);
   png_ptr->chunk_list = NULL;
#endif

   /* NOTE: the 'setjmp' buffer may still be allocated and the memory and error
    * callbacks are still set at this point.  They are required to complete the
    * destruction of the png_struct itself.
    */
}

/* Free all memory used by the read */
void PNGAPI
png_destroy_read_struct(png_structpp png_ptr_ptr, png_infopp info_ptr_ptr,
    png_infopp end_info_ptr_ptr)
{
   png_structrp png_ptr = NULL;

   png_debug(1, "in png_destroy_read_struct");

   if (png_ptr_ptr != NULL)
      png_ptr = *png_ptr_ptr;

   if (png_ptr == NULL)
      return;

   /* libpng 1.6.0: use the API to destroy info structs to ensure consistent
    * behavior.  Prior to 1.6.0 libpng did extra 'info' destruction in this API.
    * The extra was, apparently, unnecessary yet this hides memory leak bugs.
    */
   png_destroy_info_struct(png_ptr, end_info_ptr_ptr);
   png_destroy_info_struct(png_ptr, info_ptr_ptr);

   *png_ptr_ptr = NULL;
   png_read_destroy(png_ptr);
   png_destroy_png_struct(png_ptr);
}

void PNGAPI
png_set_read_status_fn(png_structrp png_ptr, png_read_status_ptr read_row_fn)
{
   if (png_ptr == NULL)
      return;

   png_ptr->read_row_fn = read_row_fn;
}


#ifdef PNG_READ_PNG_SUPPORTED
#ifdef __GNUC__
/* This exists solely to work round a warning from GNU C. */
static int /* PRIVATE */
png_gt(size_t a, size_t b)
{
   return a > b;
}
#else
#   define png_gt(a,b) ((a) > (b))
#endif

void PNGAPI
png_read_png(png_structrp png_ptr, png_inforp info_ptr, int transforms,
    voidp params)
{
   if (png_ptr == NULL || info_ptr == NULL)
      return;

   /* png_read_info() gives us all of the information from the
    * PNG file before the first IDAT (image data chunk).
    */
   png_read_info(png_ptr, info_ptr);
   if (png_gt(info_ptr->height, PNG_SIZE_MAX/(sizeof (png_bytep))))
      png_error(png_ptr, "Image is too high to process with png_read_png()");

   /* -------------- image transformations start here ------------------- */
   /* libpng 1.6.10: add code to cause a png_app_error if a selected TRANSFORM
    * is not implemented.  This will only happen in de-configured (non-default)
    * libpng builds.  The results can be unexpected - png_read_png may return
    * short or mal-formed rows because the transform is skipped.
    */

   /* Tell libpng to strip 16-bit/color files down to 8 bits per color.
    */
   if ((transforms & PNG_TRANSFORM_SCALE_16) != 0)
     /* Added at libpng-1.5.4. "strip_16" produces the same result that it
      * did in earlier versions, while "scale_16" is now more accurate.
      */
#ifdef PNG_READ_SCALE_16_TO_8_SUPPORTED
      png_set_scale_16(png_ptr);
#else
      png_app_error(png_ptr, "PNG_TRANSFORM_SCALE_16 not supported");
#endif

   /* If both SCALE and STRIP are required pngrtran will effectively cancel the
    * latter by doing SCALE first.  This is ok and allows apps not to check for
    * which is supported to get the right answer.
    */
   if ((transforms & PNG_TRANSFORM_STRIP_16) != 0)
#ifdef PNG_READ_STRIP_16_TO_8_SUPPORTED
      png_set_strip_16(png_ptr);
#else
      png_app_error(png_ptr, "PNG_TRANSFORM_STRIP_16 not supported");
#endif

   /* Strip alpha bytes from the input data without combining with
    * the background (not recommended).
    */
   if ((transforms & PNG_TRANSFORM_STRIP_ALPHA) != 0)
#ifdef PNG_READ_STRIP_ALPHA_SUPPORTED
      png_set_strip_alpha(png_ptr);
#else
      png_app_error(png_ptr, "PNG_TRANSFORM_STRIP_ALPHA not supported");
#endif

   /* Extract multiple pixels with bit depths of 1, 2, or 4 from a single
    * byte into separate bytes (useful for paletted and grayscale images).
    */
   if ((transforms & PNG_TRANSFORM_PACKING) != 0)
#ifdef PNG_READ_PACK_SUPPORTED
      png_set_packing(png_ptr);
#else
      png_app_error(png_ptr, "PNG_TRANSFORM_PACKING not supported");
#endif

   /* Change the order of packed pixels to least significant bit first
    * (not useful if you are using png_set_packing).
    */
   if ((transforms & PNG_TRANSFORM_PACKSWAP) != 0)
#ifdef PNG_READ_PACKSWAP_SUPPORTED
      png_set_packswap(png_ptr);
#else
      png_app_error(png_ptr, "PNG_TRANSFORM_PACKSWAP not supported");
#endif

   /* Expand paletted colors into true RGB triplets
    * Expand grayscale images to full 8 bits from 1, 2, or 4 bits/pixel
    * Expand paletted or RGB images with transparency to full alpha
    * channels so the data will be available as RGBA quartets.
    */
   if ((transforms & PNG_TRANSFORM_EXPAND) != 0)
#ifdef PNG_READ_EXPAND_SUPPORTED
      png_set_expand(png_ptr);
#else
      png_app_error(png_ptr, "PNG_TRANSFORM_EXPAND not supported");
#endif

   /* We don't handle background color, gamma transformation, or quantizing. */

   /* Invert monochrome files to have 0 as white and 1 as black */
   if ((transforms & PNG_TRANSFORM_INVERT_MONO) != 0)
#ifdef PNG_READ_INVERT_SUPPORTED
      png_set_invert_mono(png_ptr);
#else
      png_app_error(png_ptr, "PNG_TRANSFORM_INVERT_MONO not supported");
#endif

   /* If you want to shift the pixel values from the range [0,255] or
    * [0,65535] to the original [0,7] or [0,31], or whatever range the
    * colors were originally in:
    */
   if ((transforms & PNG_TRANSFORM_SHIFT) != 0)
#ifdef PNG_READ_SHIFT_SUPPORTED
      if ((info_ptr->valid & PNG_INFO_sBIT) != 0)
         png_set_shift(png_ptr, &info_ptr->sig_bit);
#else
      png_app_error(png_ptr, "PNG_TRANSFORM_SHIFT not supported");
#endif

   /* Flip the RGB pixels to BGR (or RGBA to BGRA) */
   if ((transforms & PNG_TRANSFORM_BGR) != 0)
#ifdef PNG_READ_BGR_SUPPORTED
      png_set_bgr(png_ptr);
#else
      png_app_error(png_ptr, "PNG_TRANSFORM_BGR not supported");
#endif

   /* Swap the RGBA or GA data to ARGB or AG (or BGRA to ABGR) */
   if ((transforms & PNG_TRANSFORM_SWAP_ALPHA) != 0)
#ifdef PNG_READ_SWAP_ALPHA_SUPPORTED
      png_set_swap_alpha(png_ptr);
#else
      png_app_error(png_ptr, "PNG_TRANSFORM_SWAP_ALPHA not supported");
#endif

   /* Swap bytes of 16-bit files to least significant byte first */
   if ((transforms & PNG_TRANSFORM_SWAP_ENDIAN) != 0)
#ifdef PNG_READ_SWAP_SUPPORTED
      png_set_swap(png_ptr);
#else
      png_app_error(png_ptr, "PNG_TRANSFORM_SWAP_ENDIAN not supported");
#endif

/* Added at libpng-1.2.41 */
   /* Invert the alpha channel from opacity to transparency */
   if ((transforms & PNG_TRANSFORM_INVERT_ALPHA) != 0)
#ifdef PNG_READ_INVERT_ALPHA_SUPPORTED
      png_set_invert_alpha(png_ptr);
#else
      png_app_error(png_ptr, "PNG_TRANSFORM_INVERT_ALPHA not supported");
#endif

/* Added at libpng-1.2.41 */
   /* Expand grayscale image to RGB */
   if ((transforms & PNG_TRANSFORM_GRAY_TO_RGB) != 0)
#ifdef PNG_READ_GRAY_TO_RGB_SUPPORTED
      png_set_gray_to_rgb(png_ptr);
#else
      png_app_error(png_ptr, "PNG_TRANSFORM_GRAY_TO_RGB not supported");
#endif

/* Added at libpng-1.5.4 */
   if ((transforms & PNG_TRANSFORM_EXPAND_16) != 0)
#ifdef PNG_READ_EXPAND_16_SUPPORTED
      png_set_expand_16(png_ptr);
#else
      png_app_error(png_ptr, "PNG_TRANSFORM_EXPAND_16 not supported");
#endif

   /* We don't handle adding filler bytes */

   /* We use png_read_image and rely on that for interlace handling, but we also
    * call png_read_update_info therefore must turn on interlace handling now:
    */
   (void)png_set_interlace_handling(png_ptr);

   /* Optional call to gamma correct and add the background to the palette
    * and update info structure.  REQUIRED if you are expecting libpng to
    * update the palette for you (i.e., you selected such a transform above).
    */
   png_read_update_info(png_ptr, info_ptr);

   /* -------------- image transformations end here ------------------- */

   png_free_data(png_ptr, info_ptr, PNG_FREE_ROWS, 0);
   if (info_ptr->row_pointers == NULL)
   {
      png_uint_32 iptr;
      png_alloc_size_t rowbytes = png_get_rowbytes(png_ptr, info_ptr);

      info_ptr->row_pointers = png_voidcast(png_bytepp, png_malloc(png_ptr,
          info_ptr->height * (sizeof (png_bytep))));

      for (iptr=0; iptr<info_ptr->height; iptr++)
         info_ptr->row_pointers[iptr] = NULL;

      info_ptr->free_me |= PNG_FREE_ROWS;

      for (iptr = 0; iptr < info_ptr->height; iptr++)
         info_ptr->row_pointers[iptr] = png_voidcast(png_bytep,
            png_malloc(png_ptr, rowbytes));
   }

   png_read_image(png_ptr, info_ptr->row_pointers);
   info_ptr->valid |= PNG_INFO_IDAT;

   /* Read rest of file, and get additional chunks in info_ptr - REQUIRED */
   png_read_end(png_ptr, info_ptr);

   PNG_UNUSED(params)
}
#endif /* READ_PNG */

#ifdef PNG_SIMPLIFIED_READ_SUPPORTED
/* SIMPLIFIED READ
 *
 * This code currently relies on the sequential reader, though it could easily
 * be made to work with the progressive one.
 */
/* Arguments to png_image_finish_read: */

/* Encoding of PNG data (used by the color-map code) */
#  define P_NOTSET  0 /* File encoding not yet known */
#  define P_sRGB    1 /* 8-bit encoded to sRGB gamma */
#  define P_LINEAR  2 /* 16-bit linear: not encoded, NOT pre-multiplied! */
#  define P_FILE    3 /* 8-bit encoded to file gamma, not sRGB or linear */
#  define P_LINEAR8 4 /* 8-bit linear: only from a file value */
#  define P_FILE8   5 /* 8-bit encoded to file gamma but not significant bits */

/* Color-map processing: after libpng has run on the PNG image further
 * processing may be needed to convert the data to color-map indices.
 */
#define PNG_CMAP_NONE      0
#define PNG_CMAP_GA        1 /* Process GA data to a color-map with alpha */
#define PNG_CMAP_TRANS     2 /* Process GA data to a background index */
#define PNG_CMAP_RGB       3 /* Process RGB data */
#define PNG_CMAP_RGB_ALPHA 4 /* Process RGBA data */

/* The following document where the background is for each processing case. */
#define PNG_CMAP_NONE_BACKGROUND      256
#define PNG_CMAP_GA_BACKGROUND        231
#define PNG_CMAP_TRANS_BACKGROUND     254
#define PNG_CMAP_RGB_BACKGROUND       256
#define PNG_CMAP_RGB_ALPHA_BACKGROUND 216

typedef struct
{
   /* Arguments: */
   png_imagep image;
   png_voidp  buffer;
   ptrdiff_t  row_stride;
   png_voidp  colormap;
   png_const_colorp background;
   /* Local variables: */
   png_voidp       local_row;
   png_voidp       first_row;
   ptrdiff_t       row_bytes;           /* step between rows */
   int             file_encoding;       /* E_ values above */
   png_fixed_point file_to_sRGB;        /* Cached correction factor */
   int             colormap_processing; /* PNG_CMAP_ values above */
   png_byte        sBIT[4];             /* Significant bits for channels */
} png_image_read_control;

/* Do all the *safe* initialization - 'safe' means that png_error won't be
 * called, so setting up the jmp_buf is not required.  This means that anything
 * called from here must *not* call png_malloc - it has to call png_malloc_warn
 * instead so that control is returned safely back to this routine.
 */
static int
png_image_read_init(png_imagep image)
{
   if (image->opaque == NULL)
   {
      png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, image,
          png_safe_error, png_safe_warning);

      /* And set the rest of the structure to NULL to ensure that the various
       * fields are consistent.
       */
      memset(image, 0, (sizeof *image));
      image->version = PNG_IMAGE_VERSION;

      if (png_ptr != NULL)
      {
         png_infop info_ptr = png_create_info_struct(png_ptr);

         if (info_ptr != NULL)
         {
            png_controlp control = png_voidcast(png_controlp,
                png_malloc_warn(png_ptr, (sizeof *control)));

            if (control != NULL)
            {
               memset(control, 0, (sizeof *control));

               control->png_ptr = png_ptr;
               control->info_ptr = info_ptr;
               control->for_write = 0;

               image->opaque = control;
               return 1;
            }

            /* Error clean up */
            png_destroy_info_struct(png_ptr, &info_ptr);
         }

         png_destroy_read_struct(&png_ptr, NULL, NULL);
      }

      return png_image_error(image, "png_image_read: out of memory");
   }

   return png_image_error(image, "png_image_read: opaque pointer not NULL");
}

/* Utility to find the base format of a PNG file from a png_struct. */
static png_uint_32
png_image_format(png_structrp png_ptr)
{
   png_uint_32 format = 0;

   if ((png_ptr->color_type & PNG_COLOR_MASK_COLOR) != 0)
      format |= PNG_FORMAT_FLAG_COLOR;

   if ((png_ptr->color_type & PNG_COLOR_MASK_ALPHA) != 0)
      format |= PNG_FORMAT_FLAG_ALPHA;

   /* Use png_ptr here, not info_ptr, because by examination png_handle_tRNS
    * sets the png_struct fields; that's all we are interested in here.  The
    * precise interaction with an app call to png_set_tRNS and PNG file reading
    * is unclear.
    */
   else if (png_ptr->num_trans > 0)
      format |= PNG_FORMAT_FLAG_ALPHA;

   if (png_ptr->bit_depth == 16)
      format |= PNG_FORMAT_FLAG_LINEAR;

   if ((png_ptr->color_type & PNG_COLOR_MASK_PALETTE) != 0)
      format |= PNG_FORMAT_FLAG_COLORMAP;

   return format;
}

/* Is the given gamma significantly different from sRGB?
 */
static int
png_gamma_not_sRGB(png_fixed_point g)
{
   /* An uninitialized gamma is assumed to be sRGB for the simplified API. */
   return g != 0 && !PNG_GAMMA_IS_sRGB(g);
}

/* Do the main body of a 'png_image_begin_read' function; read the PNG file
 * header and fill in all the information.  This is executed in a safe context,
 * unlike the init routine above.
 */
static int
png_image_read_header(png_voidp argument)
{
   png_imagep image = png_voidcast(png_imagep, argument);
   png_structrp png_ptr = image->opaque->png_ptr;
   png_inforp info_ptr = image->opaque->info_ptr;

#ifdef PNG_BENIGN_ERRORS_SUPPORTED
   png_set_benign_errors(png_ptr, 1/*warn*/);
#endif
   png_read_info(png_ptr, info_ptr);

   /* Do this the fast way; just read directly out of png_struct. */
   image->width = png_ptr->width;
   image->height = png_ptr->height;

   {
      png_uint_32 format = png_image_format(png_ptr);

      image->format = format;

#ifdef PNG_COLORSPACE_SUPPORTED
      /* Does the colorspace match sRGB?  If there is no color endpoint
       * (colorant) information assume yes, otherwise require the
       * 'ENDPOINTS_MATCHP_sRGB' colorspace flag to have been set.  If the
       * colorspace has been determined to be invalid ignore it.
       */
      if ((format & PNG_FORMAT_FLAG_COLOR) != 0 && ((png_ptr->colorspace.flags
         & (PNG_COLORSPACE_HAVE_ENDPOINTS|PNG_COLORSPACE_ENDPOINTS_MATCH_sRGB|
            PNG_COLORSPACE_INVALID)) == PNG_COLORSPACE_HAVE_ENDPOINTS))
         image->flags |= PNG_IMAGE_FLAG_COLORSPACE_NOT_sRGB;
#endif
   }

   /* We need the maximum number of entries regardless of the format the
    * application sets here.
    */
   {
      png_uint_32 cmap_entries;

      switch (png_ptr->color_type)
      {
         case PNG_COLOR_TYPE_GRAY:
            cmap_entries = 1U << png_ptr->bit_depth;
            break;

         case PNG_COLOR_TYPE_PALETTE:
            cmap_entries = png_ptr->num_palette;
            break;

         default:
            cmap_entries = 256;
            break;
      }

      if (cmap_entries > 256)
         cmap_entries = 256;

      image->colormap_entries = cmap_entries;
   }

   return 1;
}

static void
png_image_get_sBIT(png_image_read_control *display)
   /* Utility to cache the sBIT values.  This uses the information from the
    * png_struct not png_info because it may be needed after the sBIT
    * information in png_info has been invalidated.
    */
{
   if (display->sBIT[0] == 0)
   {
      const png_const_structrp png_ptr = display->image->opaque->png_ptr;
      const unsigned int color_type = png_ptr->color_type;
      const png_byte bit_depth =
         (color_type & PNG_COLOR_MASK_PALETTE) ? 8U : png_ptr->bit_depth;

      memset(display->sBIT, bit_depth, sizeof display->sBIT);

      if (color_type & PNG_COLOR_MASK_COLOR)
      {
         if (png_ptr->sig_bit.red > 0 && png_ptr->sig_bit.red < bit_depth)
            display->sBIT[0] = png_ptr->sig_bit.red;
         if (png_ptr->sig_bit.green > 0 && png_ptr->sig_bit.green < bit_depth)
            display->sBIT[1] = png_ptr->sig_bit.green;
         if (png_ptr->sig_bit.blue > 0 && png_ptr->sig_bit.blue < bit_depth)
            display->sBIT[2] = png_ptr->sig_bit.blue;
      }

      else
      {
         if (png_ptr->sig_bit.gray > 0 && png_ptr->sig_bit.gray < bit_depth)
            display->sBIT[2] = display->sBIT[1] = display->sBIT[0] =
               png_ptr->sig_bit.gray;
      }

      if (color_type & PNG_COLOR_MASK_ALPHA)
      {
         if (png_ptr->sig_bit.alpha > 0 && png_ptr->sig_bit.alpha < bit_depth)
            display->sBIT[3] = png_ptr->sig_bit.alpha;
      }
   }
}

#ifdef PNG_STDIO_SUPPORTED
int PNGAPI
png_image_begin_read_from_stdio(png_imagep image, FILE* file)
{
   if (image != NULL && image->version == PNG_IMAGE_VERSION)
   {
      if (file != NULL)
      {
         if (png_image_read_init(image) != 0 &&
             png_image_init_io(image, file) != 0)
            return png_safe_execute(image, png_image_read_header, image);
      }

      else
         return png_image_error(image,
             "png_image_begin_read_from_stdio: invalid argument");
   }

   else if (image != NULL)
      return png_image_error(image,
          "png_image_begin_read_from_stdio: incorrect PNG_IMAGE_VERSION");

   return 0;
}

int PNGAPI
png_image_begin_read_from_file(png_imagep image, const char *file_name)
{
   if (image != NULL && image->version == PNG_IMAGE_VERSION)
   {
      if (file_name != NULL)
      {
         FILE *fp = fopen(file_name, "rb");

         if (fp != NULL)
         {
            if (png_image_read_init(image) != 0 &&
                png_image_init_io(image, fp) != 0)
            {
               image->opaque->owned_file = 1;
               return png_safe_execute(image, png_image_read_header, image);
            }

            /* Clean up: just the opened file. */
            (void)fclose(fp);
         }

         else
            return png_image_error(image, strerror(errno));
      }

      else
         return png_image_error(image,
             "png_image_begin_read_from_file: invalid argument");
   }

   else if (image != NULL)
      return png_image_error(image,
          "png_image_begin_read_from_file: incorrect PNG_IMAGE_VERSION");

   return 0;
}
#endif /* STDIO */

static void PNGCBAPI
png_image_memory_read(png_structp png_ptr, png_bytep out, png_size_t need)
{
   if (png_ptr != NULL)
   {
      png_imagep image = png_voidcast(png_imagep, png_ptr->io_ptr);
      if (image != NULL)
      {
         png_controlp cp = image->opaque;
         if (cp != NULL)
         {
            png_const_bytep memory = cp->memory;
            png_size_t size = cp->size;

            if (memory != NULL && size >= need)
            {
               memcpy(out, memory, need);
               cp->memory = memory + need;
               cp->size = size - need;
               return;
            }

            png_error(png_ptr, "read beyond end of data");
         }
      }

      png_error(png_ptr, "invalid memory read");
   }
}

static int
image_init_memory_io(png_voidp param)
   /* Set the read function and pointer for a memory read, the io pointer is
    * just the imagep so it is passed in directly.
    */
{
   png_imagep image = png_voidcast(png_imagep, param);
   png_set_read_fn(image->opaque->png_ptr, image, png_image_memory_read);
   return 1;
}

int PNGAPI
png_image_begin_read_from_memory(png_imagep image, png_const_voidp memory,
    png_size_t size)
{
   if (image != NULL && image->version == PNG_IMAGE_VERSION)
   {
      if (memory != NULL && size > 0)
      {
         if (png_image_read_init(image) != 0)
         {
            /* Now set the IO functions to read from the memory buffer and
             * store it into io_ptr.  Again do this in-place to avoid calling a
             * libpng function that requires error handling.
             */
            image->opaque->memory = png_voidcast(png_const_bytep, memory);
            image->opaque->size = size;

            return png_safe_execute(image, image_init_memory_io, image) &&
               png_safe_execute(image, png_image_read_header, image);
         }
      }

      else
         return png_image_error(image,
             "png_image_begin_read_from_memory: invalid argument");
   }

   else if (image != NULL)
      return png_image_error(image,
          "png_image_begin_read_from_memory: incorrect PNG_IMAGE_VERSION");

   return 0;
}

/* Utility function to skip chunks that are not used by the simplified image
 * read functions and an appropriate macro to call it.
 */
#ifdef PNG_HANDLE_AS_UNKNOWN_SUPPORTED
static void
png_image_skip_unused_chunks(png_structrp png_ptr)
{
   /* Prepare the reader to ignore all recognized chunks whose data will not
    * be used, i.e., all chunks recognized by libpng except for those
    * involved in basic image reading:
    *
    *    IHDR, PLTE, IDAT, IEND
    *
    * Or image data handling:
    *
    *    tRNS, bKGD, gAMA, cHRM, sRGB, [iCCP] and sBIT.
    *
    * This provides a small performance improvement and eliminates any
    * potential vulnerability to security problems in the unused chunks.
    *
    * At present the iCCP chunk data isn't used, so iCCP chunk can be ignored
    * too.  This allows the simplified API to be compiled without iCCP support,
    * however if the support is there the chunk is still checked to detect
    * errors (which are unfortunately quite common.)
    */
   {
         static PNG_CONST png_byte chunks_to_process[] = {
            98,  75,  71,  68, '\0',  /* bKGD */
            99,  72,  82,  77, '\0',  /* cHRM */
           103,  65,  77,  65, '\0',  /* gAMA */
#        ifdef PNG_READ_iCCP_SUPPORTED
           105,  67,  67,  80, '\0',  /* iCCP */
#        endif
           115,  66,  73,  84, '\0',  /* sBIT */
           115,  82,  71,  66, '\0',  /* sRGB */
           };

       /* Ignore unknown chunks and all other chunks except for the
        * IHDR, PLTE, tRNS, IDAT, and IEND chunks.
        */
       png_set_keep_unknown_chunks(png_ptr, PNG_HANDLE_CHUNK_NEVER,
           NULL, -1);

       /* But do not ignore image data handling chunks */
       png_set_keep_unknown_chunks(png_ptr, PNG_HANDLE_CHUNK_AS_DEFAULT,
           chunks_to_process, (int)/*SAFE*/(sizeof chunks_to_process)/5);
   }
}

#  define PNG_SKIP_CHUNKS(p) png_image_skip_unused_chunks(p)
#else
#  define PNG_SKIP_CHUNKS(p) ((void)0)
#endif /* HANDLE_AS_UNKNOWN */

/* The following macro gives the exact rounded answer for all values in the
 * range 0..255 (it actually divides by 51.2, but the rounding still generates
 * the correct numbers 0..5
 */
#define PNG_DIV51(v8) (((v8) * 5 + 130) >> 8)

/* Utility functions to make particular color-maps */
static void
set_file_encoding(png_image_read_control *display)
{
   /* First test for an encoding close to linear: */
   if (png_need_gamma_correction(display->image->opaque->png_ptr,
            0/*PNG gamma*/, 0/*not sRGB*/))
   {
      png_fixed_point g = display->image->opaque->png_ptr->colorspace.gamma;

      /* Now look for one close to sRGB: */
      if (png_gamma_not_sRGB(g))
         display->file_encoding = P_FILE;

      else
         display->file_encoding = P_sRGB;
   }

   else
      display->file_encoding = P_LINEAR8;
}

/* For colormap entries we end up doing the gamma correction here and the
 * following routines are provided to separate out the code.  In all cases the
 * input value is in the range 0..255 and is encoded P_FILE with the gamma value
 * stored in the png_struct colorspace.
 */
static void
init_correct(png_const_structrp png_ptr, png_fixed_point *correct)
{
   /* Record the convertion necessary to get from the encoding values to
    * sRGB.  If this overflows just store FP_1.
    *
    * NOTE: this code used to store, and use, a convertion factor to
    * linear then use the sRGB encoding tables to get back to sRGB, but
    * this smashes the low values; the ones which fall in the linear part
    * of the sRGB transfer function.
    *
    * The new version of this code assumes an encoding which is neither
    * linear nor sRGB is a power law transform of the sRGB curve, not
    * linear values.  This is somewhat at odds with a precise reading of
    * the PNG spec, but given that we are trying to produce sRGB values
    * here it is most likely to be correct.
    */
   affirm(png_ptr->colorspace.gamma > 0);

   if (!png_muldiv(correct, PNG_GAMMA_sRGB_INVERSE, PNG_FP_1,
            png_ptr->colorspace.gamma))
      *correct = PNG_FP_1;
}

static png_uint_32
update_for_sBIT(png_uint_32 value, unsigned int significant_bits,
      unsigned int bit_depth)
   /* Return a bit_depth value adjusted for the number of significant bits in
    * the value.
    */
{
   if (significant_bits < bit_depth)
   {
      value >>= bit_depth - significant_bits;
      /* Now scale back to bit_depth, taking care not to overflow when 'value'
       * is (1<<significant_bits)-1 by rounding *down* the rounding add below
       * (so, e.g. rather than 2, 1 is used when significant bits is 2).
       */
      value = (value * ((1U<<bit_depth)-1U) + ((1U<<(significant_bits-1U))-1U))
         / ((1U<<significant_bits)-1U);
   }

   return value;

}

static png_uint_32
convert_to_sRGB(png_image_read_control *display, png_uint_32 value,
   unsigned int significant_bits)
{
   /* Converts an 8-bit value from P_FILE to P_sRGB */
   png_const_structrp png_ptr = display->image->opaque->png_ptr;

   debug(value <= 255U && significant_bits <= 8U && significant_bits > 0U);

   if (display->file_to_sRGB == 0)
      init_correct(png_ptr, &display->file_to_sRGB);

   /* Now simply apply this correction factor and scale back to 8 bits. */
   if (display->file_to_sRGB != PNG_FP_1)
      value = png_gamma_nxmbit_correct(value >> (8U-significant_bits),
            display->file_to_sRGB, significant_bits, 8U);

   else if (significant_bits < 8U)
      value = update_for_sBIT(value, significant_bits, 8U);

   return value;
}

static png_uint_32
convert_to_linear(png_image_read_control *display, png_uint_32 value,
      unsigned int significant_bits)
{
   /* Converts an 8-bit value from P_FILE to 16-bit P_LINEAR */
   png_const_structrp png_ptr = display->image->opaque->png_ptr;

   debug(value <= 255U && significant_bits <= 8U && significant_bits > 0U);

   if (display->file_to_sRGB == 0)
      init_correct(png_ptr, &display->file_to_sRGB);

   /* Use this correction to get a 16-bit sRGB value: */
   if (display->file_to_sRGB != PNG_FP_1)
      value = png_gamma_nxmbit_correct(value >> (8U-significant_bits),
            display->file_to_sRGB, significant_bits, 16U);

   else
   {
      value *= 257U;

      if (significant_bits < 8U)
         value = update_for_sBIT(value, significant_bits, 16U);
   }

   /* Now convert this back to linear, using the correct transfer function. */
   if (value <= 2650U /* 65535 * 0.04045 */)
   {
      /* We want to divide a 12-bit number by 12.92, do this by scaling to 32
       * bits then dividing by 2^24, with rounding:
       */
      value = (value * 1298546U + 649273U) >> 24;
   }

   else
   {
      /* Calculate for v in the range 0.04045..1.0 calculate:
       *
       *    ((v + 0.055)/1.055)^2.4
       *
       * the gamma correction function needs a 16-bit value:
       */
      value *= 62119U;
      value += 223904831U+32768U; /* cannot overflow; test with 65535 */
      value = png_gamma_nxmbit_correct(value >> 16, 240000, 16U, 16U);
   }

   return value;
}

static unsigned int
decode_gamma(png_image_read_control *display, png_uint_32 value,
      unsigned int significant_bits, int encoding)
{
   int do_sBIT = 0;

   if (encoding == P_FILE) /* double check */
      encoding = display->file_encoding, do_sBIT = 1;

   if (encoding == P_NOTSET) /* must be the file encoding */
   {
      set_file_encoding(display);
      encoding = display->file_encoding;
   }

   switch (encoding)
   {
      case P_FILE:
         /* This is a file value, so the sBIT, if any, needs to be used. */
         value = convert_to_linear(display, value, significant_bits);
         break;

      case P_sRGB:
         if (do_sBIT)
            value = update_for_sBIT(value, significant_bits, 8U);

         value = png_sRGB_table[value];
         break;

      case P_LINEAR:
         if (do_sBIT)
            value = update_for_sBIT(value, significant_bits, 16U);
         break;

      case P_LINEAR8:
         value *= 257;
         if (do_sBIT)
            value = update_for_sBIT(value, significant_bits, 16U);
         break;

      default:
         png_impossiblepp(display->image->opaque->png_ptr,
             "unexpected encoding");
         break;
   }

   return value;
}

static png_uint_32
png_colormap_compose(png_image_read_control *display,
    png_uint_32 foreground, unsigned int foreground_significant_bits,
    int foreground_encoding, png_uint_32 alpha,
    png_uint_32 background, int encoding)
{
   /* The file value is composed on the background, the background has the given
    * encoding and so does the result, the file is encoded with P_FILE and the
    * file and alpha are 8-bit values.  The (output) encoding will always be
    * P_LINEAR or P_sRGB.
    */
   png_uint_32 f = decode_gamma(display, foreground,
         foreground_significant_bits, foreground_encoding);
   png_uint_32 b = decode_gamma(display, background, 0U/*UNUSED*/, encoding);

   /* The alpha is always an 8-bit value (it comes from the palette), the value
    * scaled by 255 is what PNG_sRGB_FROM_LINEAR requires.
    */
   f = f * alpha + b * (255-alpha);

   if (encoding == P_LINEAR)
   {
      /* Scale to 65535; divide by 255, approximately (in fact this is extremely
       * accurate, it divides by 255.00000005937181414556, with no overflow.)
       */
      f *= 257; /* Now scaled by 65535 */
      f += f >> 16;
      f = (f+32768) >> 16;
   }

   else /* P_sRGB */
      f = PNG_sRGB_FROM_LINEAR(display->image->opaque->png_ptr, f);

   return f;
}

/* NOTE: P_LINEAR values to this routine must be 16-bit, but P_FILE values must
 * be 8-bit.
 */
static void
png_create_colormap_entry(png_image_read_control *display,
    png_uint_32 ip, png_uint_32 red, png_uint_32 green, png_uint_32 blue,
    png_uint_32 alpha, int encoding)
{
   png_imagep image = display->image;
#  define png_ptr image->opaque->png_ptr /* for error messages */
   const int output_encoding = (image->format & PNG_FORMAT_FLAG_LINEAR) != 0 ?
      P_LINEAR : P_sRGB;
   const int convert_to_Y = (image->format & PNG_FORMAT_FLAG_COLOR) == 0 &&
       (red != green || green != blue);
   int use_sBIT = encoding == P_FILE;

   affirm(ip <= 255);
   implies(encoding != P_LINEAR, red <= 255U && green <= 255U && blue <= 255U
        && display->sBIT[0] <= 8U && display->sBIT[1] <= 8U
        && display->sBIT[2] <= 8U && display->sBIT[3] <= 8U);

   /* This is a hack for the grayscale colormap below. */
   if (encoding == P_FILE8)
      encoding = P_FILE;

   /* Update the cache with whether the file gamma is significantly different
    * from sRGB.
    */
   if (encoding == P_FILE)
   {
      if (display->file_encoding == P_NOTSET)
         set_file_encoding(display);

      /* Note that the cached value may be P_FILE too. */
      encoding = display->file_encoding;
      if (use_sBIT)
         png_image_get_sBIT(display);
   }

   if (encoding == P_FILE)
   {
      if (convert_to_Y != 0 || output_encoding == P_LINEAR)
      {
         red = convert_to_linear(display, red,
             use_sBIT ? display->sBIT[0] : 8U);
         green = convert_to_linear(display, green,
             use_sBIT ? display->sBIT[1] : 8U);
         blue = convert_to_linear(display, blue,
             use_sBIT ? display->sBIT[2] : 8U);
         alpha *= 257U;
         if (use_sBIT)
            alpha = update_for_sBIT(alpha, display->sBIT[3], 16U);

         encoding = P_LINEAR;
         use_sBIT = 0;
      }

      else
      {
         red = convert_to_sRGB(display, red,
             use_sBIT ? display->sBIT[0] : 8U);
         green = convert_to_sRGB(display, green,
             use_sBIT ? display->sBIT[1] : 8U);
         blue = convert_to_sRGB(display, blue,
             use_sBIT ? display->sBIT[2] : 8U);
         if (use_sBIT)
            alpha = update_for_sBIT(alpha, display->sBIT[3], 8U);
         encoding = P_sRGB;
         use_sBIT = 0;
      }
   }

   else if (encoding == P_LINEAR8)
   {
      /* This encoding corresponds to a colormap with linear RGB entries, this
       * is not a very sensible encoding but it does happen with the PNGSuite
       * test images.
       */
      red *= 257;
      green *= 257;
      blue *= 257;
      alpha *= 257;
      if (use_sBIT)
      {
         red = update_for_sBIT(red, display->sBIT[0], 16U);
         green = update_for_sBIT(green, display->sBIT[1], 16U);
         blue = update_for_sBIT(blue, display->sBIT[2], 16U);
         alpha = update_for_sBIT(alpha, display->sBIT[3], 16U);
         use_sBIT = 0;
      }
      encoding = P_LINEAR;
   }

   else if (encoding == P_sRGB &&
       (convert_to_Y != 0 || output_encoding == P_LINEAR))
   {
      /* The values are 8-bit sRGB values, but must be converted to 16-bit
       * linear.
       */
      if (use_sBIT)
      {

         red = convert_to_linear(display, red, display->sBIT[0]);
         green = convert_to_linear(display, green, display->sBIT[1]);
         blue = convert_to_linear(display, blue, display->sBIT[2]);
         alpha = update_for_sBIT(alpha * 257U, display->sBIT[3], 16U);
         use_sBIT = 0;
      }

      else
      {
         red = png_sRGB_table[red];
         green = png_sRGB_table[green];
         blue = png_sRGB_table[blue];
         alpha *= 257;
      }

      encoding = P_LINEAR;
   }

   else if (encoding == P_sRGB && use_sBIT)
   {
      debug(output_encoding == P_sRGB); /* P_LINEAR handled above */
      red = update_for_sBIT(red, display->sBIT[0], 8U);
      green = update_for_sBIT(green, display->sBIT[1], 8U);
      blue = update_for_sBIT(blue, display->sBIT[2], 8U);
      alpha = update_for_sBIT(alpha, display->sBIT[3], 8U);
      use_sBIT = 0;
   }

   debug(!use_sBIT); /* it should have been handled above */

   /* This is set if the color isn't gray but the output is. */
   if (encoding == P_LINEAR)
   {
      if (convert_to_Y != 0)
      {
         /* NOTE: these values are copied from png_do_rgb_to_gray */
         png_uint_32 y = 6968 * red  + 23434 * green + 2366 * blue;

         if (output_encoding == P_LINEAR)
            y = (y + 16384) >> 15;

         else
         {
            /* y is scaled by 32768, we need it scaled by 255: */
            y = (y + 128) >> 8;
            y *= 255;
            y = PNG_sRGB_FROM_LINEAR(png_ptr, (y + 64) >> 7);
            alpha = PNG_DIV257(alpha);
            encoding = P_sRGB;
         }

         blue = red = green = y;
      }

      else if (output_encoding == P_sRGB)
      {
         red = PNG_sRGB_FROM_LINEAR(png_ptr, red * 255);
         green = PNG_sRGB_FROM_LINEAR(png_ptr, green * 255);
         blue = PNG_sRGB_FROM_LINEAR(png_ptr, blue * 255);
         alpha = PNG_DIV257(alpha);
         encoding = P_sRGB;
      }
   }

   if (encoding != output_encoding)
      png_impossiblepp(png_ptr, "bad encoding");

   /* Store the value. */
   {
#  ifdef PNG_FORMAT_AFIRST_SUPPORTED
      const int afirst = (image->format & PNG_FORMAT_FLAG_AFIRST) != 0 &&
          (image->format & PNG_FORMAT_FLAG_ALPHA) != 0;
#  else
#    define afirst 0
#  endif
#  ifdef PNG_FORMAT_BGR_SUPPORTED
      const int bgr = (image->format & PNG_FORMAT_FLAG_BGR) != 0 ? 2 : 0;
#  else
#     define bgr 0
#  endif

      if (output_encoding == P_LINEAR)
      {
         png_uint_16p entry = png_voidcast(png_uint_16p, display->colormap);

         entry += ip * PNG_IMAGE_SAMPLE_CHANNELS(image->format);

         /* The linear 16-bit values must be pre-multiplied by the alpha channel
          * value, if less than 65535 (this is, effectively, composite on black
          * if the alpha channel is removed.)
          */
         switch (PNG_IMAGE_SAMPLE_CHANNELS(image->format))
         {
            case 4:
               entry[afirst ? 0 : 3] = png_check_u16(png_ptr, alpha);
               /* FALL THROUGH */

            case 3:
               if (alpha < 65535)
               {
                  if (alpha > 0)
                  {
                     blue = (blue * alpha + 32767U)/65535U;
                     green = (green * alpha + 32767U)/65535U;
                     red = (red * alpha + 32767U)/65535U;
                  }

                  else
                     red = green = blue = 0;
               }
               entry[afirst + (2 ^ bgr)] = png_check_u16(png_ptr, blue);
               entry[afirst + 1] = png_check_u16(png_ptr, green);
               entry[afirst + bgr] = png_check_u16(png_ptr, red);
               break;

            case 2:
               entry[1 ^ afirst] = png_check_u16(png_ptr, alpha);
               /* FALL THROUGH */

            case 1:
               if (alpha < 65535)
               {
                  if (alpha > 0)
                     green = (green * alpha + 32767U)/65535U;

                  else
                     green = 0;
               }
               entry[afirst] = png_check_u16(png_ptr, green);
               break;

            default:
               break;
         }
      }

      else /* output encoding is P_sRGB */
      {
         png_bytep entry = png_voidcast(png_bytep, display->colormap);

         entry += ip * PNG_IMAGE_SAMPLE_CHANNELS(image->format);

         png_affirmpp(png_ptr, output_encoding == P_sRGB);

         switch (PNG_IMAGE_SAMPLE_CHANNELS(image->format))
         {
            case 4:
               entry[afirst ? 0 : 3] = png_check_byte(png_ptr, alpha);

            case 3:
               entry[afirst + (2 ^ bgr)] = png_check_byte(png_ptr, blue);
               entry[afirst + 1] = png_check_byte(png_ptr, green);
               entry[afirst + bgr] = png_check_byte(png_ptr, red);
               break;

            case 2:
               entry[1 ^ afirst] = png_check_byte(png_ptr, alpha);

            case 1:
               entry[afirst] = png_check_byte(png_ptr, green);
               break;

            default:
               break;
         }
      }

#  ifdef afirst
#     undef afirst
#  endif
#  ifdef bgr
#     undef bgr
#  endif
   }

#  undef png_ptr
}

static int
make_gray_file_colormap(png_image_read_control *display)
{
   unsigned int i;

   for (i=0; i<256; ++i)
      png_create_colormap_entry(display, i, i, i, i, 255, P_FILE8);

   return i;
}

static int
make_gray_colormap(png_image_read_control *display)
{
   unsigned int i;

   for (i=0; i<256; ++i)
      png_create_colormap_entry(display, i, i, i, i, 255, P_sRGB);

   return i;
}
#define PNG_GRAY_COLORMAP_ENTRIES 256

static int
make_ga_colormap(png_image_read_control *display)
{
   unsigned int i, a;

   /* Alpha is retained, the output will be a color-map with entries
    * selected by six levels of alpha.  One transparent entry, 6 gray
    * levels for all the intermediate alpha values, leaving 230 entries
    * for the opaque grays.  The color-map entries are the six values
    * [0..5]*51, the GA processing uses PNG_DIV51(value) to find the
    * relevant entry.
    *
    * if (alpha > 229) // opaque
    * {
    *    // The 231 entries are selected to make the math below work:
    *    base = 0;
    *    entry = (231 * gray + 128) >> 8;
    * }
    * else if (alpha < 26) // transparent
    * {
    *    base = 231;
    *    entry = 0;
    * }
    * else // partially opaque
    * {
    *    base = 226 + 6 * PNG_DIV51(alpha);
    *    entry = PNG_DIV51(gray);
    * }
    */
   i = 0;
   while (i < 231)
   {
      unsigned int gray = (i * 256 + 115) / 231;
      png_create_colormap_entry(display, i++, gray, gray, gray, 255, P_sRGB);
   }

   /* 255 is used here for the component values for consistency with the code
    * that undoes premultiplication in pngwrite.c.
    */
   png_create_colormap_entry(display, i++, 255, 255, 255, 0, P_sRGB);

   for (a=1; a<5; ++a)
   {
      unsigned int g;

      for (g=0; g<6; ++g)
         png_create_colormap_entry(display, i++, g*51, g*51, g*51, a*51,
             P_sRGB);
   }

   return i;
}

#define PNG_GA_COLORMAP_ENTRIES 256

static int
make_rgb_colormap(png_image_read_control *display)
{
   unsigned int i, r;

   /* Build a 6x6x6 opaque RGB cube */
   for (i=r=0; r<6; ++r)
   {
      unsigned int g;

      for (g=0; g<6; ++g)
      {
         unsigned int b;

         for (b=0; b<6; ++b)
            png_create_colormap_entry(display, i++, r*51, g*51, b*51, 255,
                P_sRGB);
      }
   }

   return i;
}

#define PNG_RGB_COLORMAP_ENTRIES 216

/* Return a palette index to the above palette given three 8-bit sRGB values. */
#define PNG_RGB_INDEX(r,g,b) \
   (png_check_byte(image->opaque->png_ptr,\
                   6 * (6 * PNG_DIV51(r) + PNG_DIV51(g)) + PNG_DIV51(b)))

static int
png_image_read_colormap(png_voidp argument)
{
   png_image_read_control *display =
      png_voidcast(png_image_read_control*, argument);
   const png_imagep image = display->image;

   const png_structrp png_ptr = image->opaque->png_ptr;
   const png_uint_32 output_format = image->format;
   const int output_encoding = (output_format & PNG_FORMAT_FLAG_LINEAR) != 0 ?
      P_LINEAR : P_sRGB;

   unsigned int cmap_entries;
   unsigned int output_processing;        /* Output processing option */
   unsigned int data_encoding = P_NOTSET; /* Encoding libpng must produce */

   /* Background information; the background color and the index of this color
    * in the color-map if it exists (else 256).
    */
   unsigned int background_index = 256;
   png_uint_32 back_r, back_g, back_b;

   /* Flags to accumulate things that need to be done to the input. */
   int expand_tRNS = 0;

   /* Exclude the NYI feature of compositing onto a color-mapped buffer; it is
    * very difficult to do, the results look awful, and it is difficult to see
    * what possible use it is because the application can't control the
    * color-map.
    */
   if (((png_ptr->color_type & PNG_COLOR_MASK_ALPHA) != 0 ||
         png_ptr->num_trans > 0) /* alpha in input */ &&
      ((output_format & PNG_FORMAT_FLAG_ALPHA) == 0) /* no alpha in output */)
   {
      if (output_encoding == P_LINEAR) /* compose on black */
         back_b = back_g = back_r = 0;

      else if (display->background == NULL /* no way to remove it */)
         png_error(png_ptr,
             "background color must be supplied to remove alpha/transparency");

      /* Get a copy of the background color (this avoids repeating the checks
       * below.)  The encoding is 8-bit sRGB or 16-bit linear, depending on the
       * output format.
       */
      else
      {
         back_g = display->background->green;
         if ((output_format & PNG_FORMAT_FLAG_COLOR) != 0)
         {
            back_r = display->background->red;
            back_b = display->background->blue;
         }
         else
            back_b = back_r = back_g;
      }
   }

   else if (output_encoding == P_LINEAR)
      back_b = back_r = back_g = 65535;

   else
      back_b = back_r = back_g = 255;

   /* Default the input file gamma if required - this is necessary because
    * libpng assumes that if no gamma information is present the data is in the
    * output format, but the simplified API deduces the gamma from the input
    * format.
    */
   if ((png_ptr->colorspace.flags & PNG_COLORSPACE_HAVE_GAMMA) == 0)
   {
      /* Do this directly, not using the png_colorspace functions, to ensure
       * that it happens even if the colorspace is invalid (though probably if
       * it is the setting will be ignored)  Note that the same thing can be
       * achieved at the application interface with png_set_gAMA.
       */
      if (png_ptr->bit_depth == 16 &&
         (image->flags & PNG_IMAGE_FLAG_16BIT_sRGB) == 0)
         png_ptr->colorspace.gamma = PNG_GAMMA_LINEAR;

      else
         png_ptr->colorspace.gamma = PNG_GAMMA_sRGB_INVERSE;

      /* Make sure libpng doesn't ignore the setting: */
      if (png_ptr->colorspace.flags & PNG_COLORSPACE_INVALID)
         png_ptr->colorspace.flags = PNG_COLORSPACE_HAVE_GAMMA;

      else
         png_ptr->colorspace.flags |= PNG_COLORSPACE_HAVE_GAMMA;
   }

   /* Decide what to do based on the PNG color type of the input data.  The
    * utility function png_create_colormap_entry deals with most aspects of the
    * output transformations; this code works out how to produce bytes of
    * color-map entries from the original format.
    */
   switch (png_ptr->color_type)
   {
      case PNG_COLOR_TYPE_GRAY:
         if (png_ptr->bit_depth <= 8)
         {
            /* There at most 256 colors in the output, regardless of
             * transparency.
             */
            unsigned int step, i, val, trans = 256/*ignore*/, back_alpha = 0;

            cmap_entries = 1U << png_ptr->bit_depth;
            if (cmap_entries > image->colormap_entries)
               png_error(png_ptr, "gray[8] color-map: too few entries");

            step = 255 / (cmap_entries - 1);
            output_processing = PNG_CMAP_NONE;

            /* If there is a tRNS chunk then this either selects a transparent
             * value or, if the output has no alpha, the background color.
             */
            if (png_ptr->num_trans > 0)
            {
               trans = png_ptr->trans_color.gray;

               if ((output_format & PNG_FORMAT_FLAG_ALPHA) == 0)
                  back_alpha = output_encoding == P_LINEAR ? 65535 : 255;
            }

            /* png_create_colormap_entry just takes an RGBA and writes the
             * corresponding color-map entry using the format from 'image',
             * including the required conversion to sRGB or linear as
             * appropriate.  The input values are always either sRGB (if the
             * gamma correction flag is 0) or 0..255 scaled file encoded values
             * (if the function must gamma correct them).
             */
            for (i=val=0; i<cmap_entries; ++i, val += step)
            {
               /* 'i' is a file value.  While this will result in duplicated
                * entries for 8-bit non-sRGB encoded files it is necessary to
                * have non-gamma corrected values to do tRNS handling.
                */
               if (i != trans)
                  png_create_colormap_entry(display, i, val, val, val, 255,
                      P_FILE/*8-bit with file gamma*/);

               /* Else this entry is transparent.  The colors don't matter if
                * there is an alpha channel (back_alpha == 0), but it does no
                * harm to pass them in; the values are not set above so this
                * passes in white.
                *
                * NOTE: this preserves the full precision of the application
                * supplied background color when it is used.
                */
               else
               {
#ifdef __COVERITY__
                  /* Coverity says back_r|g|b might be 16-bit values */
                  png_affirmpp(png_ptr, back_r < 256 && back_g < 256 &&
                      back_b < 256);
#endif
                  png_create_colormap_entry(display, i, back_r, back_g, back_b,
                      back_alpha, output_encoding);
               }
            }

            /* We need libpng to preserve the original encoding. */
            data_encoding = P_FILE;

            /* The rows from libpng, while technically gray values, are now also
             * color-map indices; however, they may need to be expanded to 1
             * byte per pixel.  This is what png_set_packing does (i.e., it
             * unpacks the bit values into bytes.)
             */
            if (png_ptr->bit_depth < 8)
               png_set_packing(png_ptr);
         }

         else /* bit depth is 16 */
         {
            /* The 16-bit input values can be converted directly to 8-bit gamma
             * encoded values; however, if a tRNS chunk is present 257 color-map
             * entries are required.  This means that the extra entry requires
             * special processing; add an alpha channel, sacrifice gray level
             * 254 and convert transparent (alpha==0) entries to that.
             *
             * Use libpng to chop the data to 8 bits.  Convert it to sRGB at the
             * same time to minimize quality loss.  If a tRNS chunk is present
             * this means libpng must handle it too; otherwise it is impossible
             * to do the exact match on the 16-bit value.
             *
             * If the output has no alpha channel *and* the background color is
             * gray then it is possible to let libpng handle the substitution by
             * ensuring that the corresponding gray level matches the background
             * color exactly.
             */
            data_encoding = P_sRGB;

            if (PNG_GRAY_COLORMAP_ENTRIES > image->colormap_entries)
               png_error(png_ptr, "gray[16] color-map: too few entries");

            cmap_entries = make_gray_colormap(display);

            if (png_ptr->num_trans > 0)
            {
               unsigned int back_alpha;

               if ((output_format & PNG_FORMAT_FLAG_ALPHA) != 0)
                  back_alpha = 0;

               else
               {
                  if (back_r == back_g && back_g == back_b)
                  {
                     /* Background is gray; no special processing will be
                      * required.
                      */
                     png_color_16 c;
                     png_uint_32 gray = back_g;

                     if (output_encoding == P_LINEAR)
                     {
                        gray = PNG_sRGB_FROM_LINEAR(png_ptr, gray * 255);

                        /* And make sure the corresponding palette entry
                         * matches.
                         */
                        png_create_colormap_entry(display, gray, back_g, back_g,
                            back_g, 65535, P_LINEAR);
                     }

                     /* The background passed to libpng, however, must be the
                      * sRGB value.
                      */
                     c.index = 0; /*unused*/
                     c.gray = c.red = c.green = c.blue =
                         png_check_u16(png_ptr, gray);

                     /* NOTE: does this work without expanding tRNS to alpha?
                      * It should be the color->gray case below apparently
                      * doesn't.
                      */
                     png_set_background_fixed(png_ptr, &c,
                         PNG_BACKGROUND_GAMMA_SCREEN, 0/*need_expand*/,
                         0/*gamma: not used*/);

                     output_processing = PNG_CMAP_NONE;
                     break;
                  }
                  /* Coverity claims that output_encoding cannot be 2 (P_LINEAR)
                   * here.
                   */
                  affirm(output_encoding != P_LINEAR);
                  back_alpha = 255U;
               }

               /* output_processing means that the libpng-processed row will be
                * 8-bit GA and it has to be processing to single byte color-map
                * values.  Entry 254 is replaced by either a completely
                * transparent entry or by the background color at full
                * precision (and the background color is not a simple gray
                * level in this case.)
                */
               expand_tRNS = 1;
               output_processing = PNG_CMAP_TRANS;
               background_index = 254;

               /* And set (overwrite) color-map entry 254 to the actual
                * background color at full precision.
                */
#ifdef __COVERITY__
               /* Coverity says back_r|g|b might be 16-bit values */
               png_affirmpp(png_ptr, back_r < 256 && back_g < 256 &&
                   back_b < 256);
#endif
               png_create_colormap_entry(display, 254, back_r, back_g, back_b,
                   back_alpha, output_encoding);
            }

            else
               output_processing = PNG_CMAP_NONE;
         }
         break;

      case PNG_COLOR_TYPE_GRAY_ALPHA:
         /* 8-bit or 16-bit PNG with two channels - gray and alpha.  A minimum
          * of 65536 combinations.  If, however, the alpha channel is to be
          * removed there are only 256 possibilities if the background is gray.
          * (Otherwise there is a subset of the 65536 possibilities defined by
          * the triangle between black, white and the background color.)
          *
          * Reduce 16-bit files to 8-bit and sRGB encode the result.  No need to
          * worry about tRNS matching - tRNS is ignored if there is an alpha
          * channel.
          */
         data_encoding = P_sRGB;

         if ((output_format & PNG_FORMAT_FLAG_ALPHA) != 0)
         {
            if (PNG_GA_COLORMAP_ENTRIES > image->colormap_entries)
               png_error(png_ptr, "gray+alpha color-map: too few entries");

            cmap_entries = make_ga_colormap(display);

            background_index = PNG_CMAP_GA_BACKGROUND;
            output_processing = PNG_CMAP_GA;
         }

         else /* alpha is removed */
         {
            /* Alpha must be removed as the PNG data is processed when the
             * background is a color because the G and A channels are
             * independent and the vector addition (non-parallel vectors) is a
             * 2-D problem.
             *
             * This can be reduced to the same algorithm as above by making a
             * colormap containing gray levels (for the opaque grays), a
             * background entry (for a transparent pixel) and a set of four six
             * level color values, one set for each intermediate alpha value.
             * See the comments in make_ga_colormap for how this works in the
             * per-pixel processing.
             *
             * If the background is gray, however, we only need a 256 entry gray
             * level color map.  It is sufficient to make the entry generated
             * for the background color be exactly the color specified.
             */
            if ((output_format & PNG_FORMAT_FLAG_COLOR) == 0 ||
               (back_r == back_g && back_g == back_b))
            {
               /* Background is gray; no special processing will be required. */
               png_color_16 c;
               png_uint_32 gray = back_g;

               if (PNG_GRAY_COLORMAP_ENTRIES > image->colormap_entries)
                  png_error(png_ptr, "gray-alpha color-map: too few entries");

               cmap_entries = make_gray_colormap(display);

               if (output_encoding == P_LINEAR)
               {
                  gray = PNG_sRGB_FROM_LINEAR(png_ptr, gray * 255);

                  /* And make sure the corresponding palette entry matches. */
                  png_create_colormap_entry(display, gray, back_g, back_g,
                      back_g, 65535, P_LINEAR);
               }

               /* The background passed to libpng, however, must be the sRGB
                * value.
                */
               c.index = 0; /*unused*/
               c.gray = c.red = c.green = c.blue = png_check_u16(png_ptr, gray);

               png_set_background_fixed(png_ptr, &c,
                   PNG_BACKGROUND_GAMMA_SCREEN, 0/*need_expand*/,
                   0/*gamma: not used*/);

               output_processing = PNG_CMAP_NONE;
            }

            else
            {
               png_uint_32 i, a;

               /* This is the same as png_make_ga_colormap, above, except that
                * the entries are all opaque.
                */
               if (PNG_GA_COLORMAP_ENTRIES > image->colormap_entries)
                  png_error(png_ptr, "ga-alpha color-map: too few entries");

               i = 0;
               while (i < 231)
               {
                  png_uint_32 gray = (i * 256 + 115) / 231;
                  png_create_colormap_entry(display, i++, gray, gray, gray,
                      255, P_sRGB);
               }

               /* NOTE: this preserves the full precision of the application
                * background color.
                *
                * Coverity claims that output_encoding cannot be 2 (P_LINEAR)
                */
               affirm(output_encoding != P_LINEAR);
               background_index = i;
               png_create_colormap_entry(display, i++, back_r, back_g, back_b,
                   255U, output_encoding);

               /* For non-opaque input composite on the sRGB background - this
                * requires inverting the encoding for each component.  The input
                * is still converted to the sRGB encoding because this is a
                * reasonable approximate to the logarithmic curve of human
                * visual sensitivity, at least over the narrow range which PNG
                * represents.  Consequently 'G' is always sRGB encoded, while
                * 'A' is linear.  We need the linear background colors.
                */
               if (output_encoding == P_sRGB) /* else already linear */
               {
                  /* This may produce a value not exactly matching the
                   * background, but that's ok because these numbers are only
                   * used when alpha != 0
                   */
                  back_r = png_sRGB_table[back_r];
                  back_g = png_sRGB_table[back_g];
                  back_b = png_sRGB_table[back_b];
               }

               for (a=1; a<5; ++a)
               {
                  unsigned int g;

                  /* PNG_sRGB_FROM_LINEAR expects a 16-bit linear value scaled
                   * by an 8-bit alpha value (0..255).
                   */
                  png_uint_32 alpha = 51 * a;
                  png_uint_32 back_rx = (255-alpha) * back_r;
                  png_uint_32 back_gx = (255-alpha) * back_g;
                  png_uint_32 back_bx = (255-alpha) * back_b;

                  for (g=0; g<6; ++g)
                  {
                     png_uint_32 gray = png_sRGB_table[g*51] * alpha;

                     png_create_colormap_entry(display, i++,
                        PNG_sRGB_FROM_LINEAR(png_ptr, gray + back_rx),
                        PNG_sRGB_FROM_LINEAR(png_ptr, gray + back_gx),
                        PNG_sRGB_FROM_LINEAR(png_ptr, gray + back_bx),
                        255, P_sRGB);
                  }
               }

               cmap_entries = i;
               output_processing = PNG_CMAP_GA;
            }
         }
         break;

      case PNG_COLOR_TYPE_RGB:
      case PNG_COLOR_TYPE_RGB_ALPHA:
         /* Exclude the case where the output is gray; we can always handle this
          * with the cases above.
          */
         if ((output_format & PNG_FORMAT_FLAG_COLOR) == 0)
         {
            /* The color-map will be grayscale, so we may as well convert the
             * input RGB values to a simple grayscale and use the grayscale
             * code above.
             *
             * NOTE: calling this apparently damages the recognition of the
             * transparent color in background color handling; call
             * png_set_tRNS_to_alpha before png_set_background_fixed.
             */
            png_set_rgb_to_gray_fixed(png_ptr, PNG_ERROR_ACTION_NONE, -1,
                -1);
            data_encoding = P_sRGB;

            /* The output will now be one or two 8-bit gray or gray+alpha
             * channels.  The more complex case arises when the input has alpha.
             */
            if ((png_ptr->color_type == PNG_COLOR_TYPE_RGB_ALPHA ||
               png_ptr->num_trans > 0) &&
               (output_format & PNG_FORMAT_FLAG_ALPHA) != 0)
            {
               /* Both input and output have an alpha channel, so no background
                * processing is required; just map the GA bytes to the right
                * color-map entry.
                */
               expand_tRNS = 1;

               if (PNG_GA_COLORMAP_ENTRIES > image->colormap_entries)
                  png_error(png_ptr, "rgb[ga] color-map: too few entries");

               cmap_entries = make_ga_colormap(display);
               background_index = PNG_CMAP_GA_BACKGROUND;
               output_processing = PNG_CMAP_GA;
            }

            else
            {
               /* Either the input or the output has no alpha channel, so there
                * will be no non-opaque pixels in the color-map; it will just be
                * grayscale.
                */
               if (PNG_GRAY_COLORMAP_ENTRIES > image->colormap_entries)
                  png_error(png_ptr, "rgb[gray] color-map: too few entries");

               /* Ideally this code would use libpng to do the gamma correction,
                * but if an input alpha channel is to be removed we will hit the
                * libpng bug in gamma+compose+rgb-to-gray (the double gamma
                * correction bug).  Fix this by dropping the gamma correction in
                * this case and doing it in the palette; this will result in
                * duplicate palette entries, but that's better than the
                * alternative of double gamma correction.
                */
               if ((png_ptr->color_type == PNG_COLOR_TYPE_RGB_ALPHA ||
                  png_ptr->num_trans > 0) &&
                  png_gamma_not_sRGB(png_ptr->colorspace.gamma) != 0)
               {
                  cmap_entries = make_gray_file_colormap(display);
                  data_encoding = P_FILE;
               }

               else
                  cmap_entries = make_gray_colormap(display);

               /* But if the input has alpha or transparency it must be removed
                */
               if (png_ptr->color_type == PNG_COLOR_TYPE_RGB_ALPHA ||
                  png_ptr->num_trans > 0)
               {
                  png_color_16 c;
                  png_uint_32 gray = back_g;

                  /* We need to ensure that the application background exists in
                   * the colormap and that completely transparent pixels map to
                   * it.  Achieve this simply by ensuring that the entry
                   * selected for the background really is the background color.
                   */
                  if (data_encoding == P_FILE) /* from the fixup above */
                  {
                     /* The app supplied a gray which is in output_encoding, we
                      * need to convert it to a value of the input (P_FILE)
                      * encoding then set this palette entry to the required
                      * output encoding.
                      */
                     if (output_encoding == P_sRGB)
                        gray = png_sRGB_table[gray]; /* now P_LINEAR */

                     gray = png_gamma_nxmbit_correct(gray,
                        png_ptr->colorspace.gamma, 16U, 8U);

                     /* And make sure the corresponding palette entry contains
                      * exactly the required sRGB value.
                      */
                     png_create_colormap_entry(display, gray, back_g, back_g,
                        back_g, 0/*unused*/, output_encoding);
                  }

                  else if (output_encoding == P_LINEAR)
                  {
                     gray = PNG_sRGB_FROM_LINEAR(png_ptr, gray * 255);

                     /* And make sure the corresponding palette entry matches.
                      */
                     png_create_colormap_entry(display, gray, back_g, back_g,
                        back_g, 0/*unused*/, P_LINEAR);
                  }

                  /* The background passed to libpng, however, must be the
                   * output (normally sRGB) value.
                   */
                  c.index = 0; /*unused*/
                  c.gray = c.red = c.green = c.blue =
                     png_check_u16(png_ptr, gray);

                  /* NOTE: the following is apparently a bug in libpng. Without
                   * it the transparent color recognition in
                   * png_set_background_fixed seems to go wrong.
                   */
                  expand_tRNS = 1;
                  png_set_background_fixed(png_ptr, &c,
                      PNG_BACKGROUND_GAMMA_SCREEN, 0/*need_expand*/,
                      0/*gamma: not used*/);
               }

               output_processing = PNG_CMAP_NONE;
            }
         }

         else /* output is color */
         {
            /* We could use png_quantize here so long as there is no transparent
             * color or alpha; png_quantize ignores alpha.  Easier overall just
             * to do it once and using PNG_DIV51 on the 6x6x6 reduced RGB cube.
             * Consequently we always want libpng to produce sRGB data.
             */
            data_encoding = P_sRGB;

            /* Is there any transparency or alpha? */
            if (png_ptr->color_type == PNG_COLOR_TYPE_RGB_ALPHA ||
               png_ptr->num_trans > 0)
            {
               /* Is there alpha in the output too?  If so all four channels are
                * processed into a special RGB cube with alpha support.
                */
               if ((output_format & PNG_FORMAT_FLAG_ALPHA) != 0)
               {
                  png_uint_32 r;

                  if (PNG_RGB_COLORMAP_ENTRIES+1+27 > image->colormap_entries)
                     png_error(png_ptr, "rgb+alpha color-map: too few entries");

                  cmap_entries = make_rgb_colormap(display);

                  /* Add a transparent entry. */
                  png_create_colormap_entry(display, cmap_entries, 255, 255,
                      255, 0, P_sRGB);

                  /* This is stored as the background index for the processing
                   * algorithm.
                   */
                  background_index = cmap_entries++;

                  /* Add 27 r,g,b entries each with alpha 0.5. */
                  for (r=0; r<256; r = (r << 1) | 0x7f)
                  {
                     png_uint_32 g;

                     for (g=0; g<256; g = (g << 1) | 0x7f)
                     {
                        png_uint_32 b;

                        /* This generates components with the values 0, 127 and
                         * 255
                         */
                        for (b=0; b<256; b = (b << 1) | 0x7f)
                           png_create_colormap_entry(display, cmap_entries++,
                               r, g, b, 128, P_sRGB);
                     }
                  }

                  expand_tRNS = 1;
                  output_processing = PNG_CMAP_RGB_ALPHA;
               }

               else
               {
                  /* Alpha/transparency must be removed.  The background must
                   * exist in the color map (achieved by setting adding it after
                   * the 666 color-map).  If the standard processing code will
                   * pick up this entry automatically that's all that is
                   * required; libpng can be called to do the background
                   * processing.
                   */
                  unsigned int sample_size =
                     PNG_IMAGE_SAMPLE_SIZE(output_format);
                  png_uint_32 r, g, b; /* sRGB background */

                  if (PNG_RGB_COLORMAP_ENTRIES+1+27 > image->colormap_entries)
                     png_error(png_ptr, "rgb-alpha color-map: too few entries");

                  cmap_entries = make_rgb_colormap(display);

                  png_create_colormap_entry(display, cmap_entries, back_r,
                      back_g, back_b, 0/*unused*/, output_encoding);

                  if (output_encoding == P_LINEAR)
                  {
                     r = PNG_sRGB_FROM_LINEAR(png_ptr, back_r * 255);
                     g = PNG_sRGB_FROM_LINEAR(png_ptr, back_g * 255);
                     b = PNG_sRGB_FROM_LINEAR(png_ptr, back_b * 255);
                  }

                  else
                  {
                     r = back_r;
                     g = back_g;
                     b = back_g;
                  }

                  /* Compare the newly-created color-map entry with the one the
                   * PNG_CMAP_RGB algorithm will use.  If the two entries don't
                   * match, add the new one and set this as the background
                   * index.
                   */
                  if (memcmp((png_const_bytep)display->colormap +
                      sample_size * cmap_entries,
                      (png_const_bytep)display->colormap +
                          sample_size * PNG_RGB_INDEX(r,g,b),
                     sample_size) != 0)
                  {
                     /* The background color must be added. */
                     background_index = cmap_entries++;

                     /* Add 27 r,g,b entries each with created by composing with
                      * the background at alpha 0.5.
                      */
                     for (r=0; r<256; r = (r << 1) | 0x7f)
                     {
                        for (g=0; g<256; g = (g << 1) | 0x7f)
                        {
                           /* This generates components with the values 0, 127
                            * and 255
                            */
                           for (b=0; b<256; b = (b << 1) | 0x7f)
                              png_create_colormap_entry(display, cmap_entries++,
                                 png_colormap_compose(display, r, 8U, P_sRGB,
                                    128U, back_r, output_encoding),
                                 png_colormap_compose(display, g, 8U, P_sRGB,
                                    128U, back_g, output_encoding),
                                 png_colormap_compose(display, b, 8U, P_sRGB,
                                    128U, back_b, output_encoding),
                                 0/*unused*/, output_encoding);
                        }
                     }

                     expand_tRNS = 1;
                     output_processing = PNG_CMAP_RGB_ALPHA;
                  }

                  else /* background color is in the standard color-map */
                  {
                     png_color_16 c;

                     c.index = 0; /*unused*/
                     c.red = png_check_u16(png_ptr, back_r);
                     c.gray = c.green = png_check_u16(png_ptr, back_g);
                     c.blue = png_check_u16(png_ptr, back_b);

                     png_set_background_fixed(png_ptr, &c,
                         PNG_BACKGROUND_GAMMA_SCREEN, 0/*need_expand*/,
                         0/*gamma: not used*/);

                     output_processing = PNG_CMAP_RGB;
                  }
               }
            }

            else /* no alpha or transparency in the input */
            {
               /* Alpha in the output is irrelevant, simply map the opaque input
                * pixels to the 6x6x6 color-map.
                */
               if (PNG_RGB_COLORMAP_ENTRIES > image->colormap_entries)
                  png_error(png_ptr, "rgb color-map: too few entries");

               cmap_entries = make_rgb_colormap(display);
               output_processing = PNG_CMAP_RGB;
            }
         }
         break;

      case PNG_COLOR_TYPE_PALETTE:
         /* It's already got a color-map.  It may be necessary to eliminate the
          * tRNS entries though.
          */
         {
            unsigned int num_trans = png_ptr->num_trans;
            png_const_bytep trans = num_trans > 0 ? png_ptr->trans_alpha : NULL;
            png_const_colorp colormap = png_ptr->palette;
            const int do_background = trans != NULL &&
               (output_format & PNG_FORMAT_FLAG_ALPHA) == 0;
            unsigned int i;

            /* Just in case: */
            if (trans == NULL)
               num_trans = 0;

            output_processing = PNG_CMAP_NONE;
            data_encoding = P_FILE; /* Don't change from color-map indices */
            cmap_entries = png_ptr->num_palette;
            if (cmap_entries > 256)
               cmap_entries = 256;

            if (cmap_entries > image->colormap_entries)
               png_error(png_ptr, "palette color-map: too few entries");

            for (i=0; i < cmap_entries; ++i)
            {
               if (do_background != 0 && i < num_trans && trans[i] < 255)
               {
                  if (trans[i] == 0)
                     png_create_colormap_entry(display, i, back_r, back_g,
                         back_b, 0, output_encoding);

                  else
                  {
                     unsigned int alpha;

                     /* Must compose the PNG file color in the color-map entry
                      * on the sRGB color in 'back'.
                      */
                     png_image_get_sBIT(display);
                     alpha = update_for_sBIT(trans[i], display->sBIT[3], 8U);

                     /* Do the sBIT handling here because it only applies to the
                      * values from the colormap, not the background.  Passing
                      * output_encoding to png_create_colormap_entry prevents
                      * this being duplicated.
                      */
                     png_create_colormap_entry(display, i,
                        png_colormap_compose(display, colormap[i].red,
                           display->sBIT[0], P_FILE, alpha, back_r,
                           output_encoding),
                        png_colormap_compose(display, colormap[i].green,
                           display->sBIT[1], P_FILE, alpha, back_g,
                           output_encoding),
                        png_colormap_compose(display, colormap[i].blue,
                           display->sBIT[2], P_FILE, alpha, back_b,
                           output_encoding),
                        output_encoding == P_LINEAR ?
                           update_for_sBIT(alpha*257U, display->sBIT[3], 16U) :
                           trans[i],
                        output_encoding);
                  }
               }

               else
                  png_create_colormap_entry(display, i, colormap[i].red,
                     colormap[i].green, colormap[i].blue,
                     i < num_trans ? trans[i] : 255U, P_FILE/*8-bit*/);
            }

            /* The PNG data may have indices packed in fewer than 8 bits, it
             * must be expanded if so.
             */
            if (png_ptr->bit_depth < 8)
               png_set_packing(png_ptr);
         }
         break;

      default:
         png_error(png_ptr, "invalid PNG color type");
         /*NOT REACHED*/
   }

   /* Now deal with the output processing */
   if (expand_tRNS != 0 && png_ptr->num_trans > 0 &&
       (png_ptr->color_type & PNG_COLOR_MASK_ALPHA) == 0)
      png_set_tRNS_to_alpha(png_ptr);

   switch (data_encoding)
   {
      default:
         impossible("bad data option");
         break;

      case P_sRGB:
         /* Change to 8-bit sRGB */
         png_set_alpha_mode_fixed(png_ptr, PNG_ALPHA_PNG, PNG_GAMMA_sRGB);
         /* FALL THROUGH */

      case P_FILE:
         if (png_ptr->bit_depth > 8)
            png_set_scale_16(png_ptr);
         break;
   }

   affirm(cmap_entries <= 256 && cmap_entries <= image->colormap_entries);

   image->colormap_entries = cmap_entries;

   /* Double check using the recorded background index */
   switch (output_processing)
   {
      case PNG_CMAP_NONE:
         if (background_index != PNG_CMAP_NONE_BACKGROUND)
            goto bad_background;
         break;

      case PNG_CMAP_GA:
         if (background_index != PNG_CMAP_GA_BACKGROUND)
            goto bad_background;
         break;

      case PNG_CMAP_TRANS:
         if (background_index >= cmap_entries ||
            background_index != PNG_CMAP_TRANS_BACKGROUND)
            goto bad_background;
         break;

      case PNG_CMAP_RGB:
         if (background_index != PNG_CMAP_RGB_BACKGROUND)
            goto bad_background;
         break;

      case PNG_CMAP_RGB_ALPHA:
         if (background_index != PNG_CMAP_RGB_ALPHA_BACKGROUND)
            goto bad_background;
         break;

      default:
         impossible("bad processing option");

      bad_background:
         impossible("bad background index");
   }

   display->colormap_processing = output_processing;

   return 1/*ok*/;
}

/* The final part of the color-map read called from png_image_finish_read. */
static int
png_image_read_and_map(png_voidp argument)
{
   png_image_read_control *display = png_voidcast(png_image_read_control*,
       argument);
   png_imagep image = display->image;
   png_structrp png_ptr = image->opaque->png_ptr;
   int passes;

   /* Called when the libpng data must be transformed into the color-mapped
    * form.  There is a local row buffer in display->local and this routine must
    * do the interlace handling.
    */
   switch (png_ptr->interlaced)
   {
      case PNG_INTERLACE_NONE:
         passes = 1;
         break;

      case PNG_INTERLACE_ADAM7:
         passes = PNG_INTERLACE_ADAM7_PASSES;
         break;

      default:
         png_error(png_ptr, "unknown interlace type");
   }

   {
      png_uint_32  height = image->height;
      png_uint_32  width = image->width;
      int          proc = display->colormap_processing;
      png_bytep    first_row = png_voidcast(png_bytep, display->first_row);
      ptrdiff_t    step_row = display->row_bytes;
      int pass;

      for (pass = 0; pass < passes; ++pass)
      {
         unsigned int     startx, stepx, stepy;
         png_uint_32      y;

         if (png_ptr->interlaced == PNG_INTERLACE_ADAM7)
         {
            /* The row may be empty for a short image: */
            if (PNG_PASS_COLS(width, pass) == 0)
               continue;

            startx = PNG_PASS_START_COL(pass);
            stepx = PNG_PASS_COL_OFFSET(pass);
            y = PNG_PASS_START_ROW(pass);
            stepy = PNG_PASS_ROW_OFFSET(pass);
         }

         else
         {
            y = 0;
            startx = 0;
            stepx = stepy = 1;
         }

         for (; y<height; y += stepy)
         {
            png_bytep inrow = png_voidcast(png_bytep, display->local_row);
            png_bytep outrow = first_row + y * step_row;
            png_const_bytep end_row = outrow + width;

            /* Read read the libpng data into the temporary buffer. */
            png_read_row(png_ptr, inrow, NULL);

            /* Now process the row according to the processing option, note
             * that the caller verifies that the format of the libpng output
             * data is as required.
             */
            outrow += startx;
            switch (proc)
            {
               case PNG_CMAP_GA:
                  for (; outrow < end_row; outrow += stepx)
                  {
                     /* The data is always in the PNG order */
                     unsigned int gray = *inrow++;
                     unsigned int alpha = *inrow++;
                     unsigned int entry;

                     /* NOTE: this code is copied as a comment in
                      * make_ga_colormap above.  Please update the
                      * comment if you change this code!
                      */
                     if (alpha > 229) /* opaque */
                     {
                        entry = (231 * gray + 128) >> 8;
                     }
                     else if (alpha < 26) /* transparent */
                     {
                        entry = 231;
                     }
                     else /* partially opaque */
                     {
                        entry = 226 + 6 * PNG_DIV51(alpha) + PNG_DIV51(gray);
                     }

                     *outrow = png_check_byte(png_ptr, entry);
                  }
                  break;

               case PNG_CMAP_TRANS:
                  for (; outrow < end_row; outrow += stepx)
                  {
                     png_byte gray = *inrow++;
                     png_byte alpha = *inrow++;

                     if (alpha == 0)
                        *outrow = PNG_CMAP_TRANS_BACKGROUND;

                     else if (gray != PNG_CMAP_TRANS_BACKGROUND)
                        *outrow = gray;

                     else
                        *outrow = PNG_CMAP_TRANS_BACKGROUND+1;
                  }
                  break;

               case PNG_CMAP_RGB:
                  for (; outrow < end_row; outrow += stepx)
                  {
                     *outrow = PNG_RGB_INDEX(inrow[0], inrow[1], inrow[2]);
                     inrow += 3;
                  }
                  break;

               case PNG_CMAP_RGB_ALPHA:
                  for (; outrow < end_row; outrow += stepx)
                  {
                     unsigned int alpha = inrow[3];

                     /* Because the alpha entries only hold alpha==0.5 values
                      * split the processing at alpha==0.25 (64) and 0.75
                      * (196).
                      */

                     if (alpha >= 196)
                        *outrow = PNG_RGB_INDEX(inrow[0], inrow[1],
                            inrow[2]);

                     else if (alpha < 64)
                        *outrow = PNG_CMAP_RGB_ALPHA_BACKGROUND;

                     else
                     {
                        /* Likewise there are three entries for each of r, g
                         * and b.  We could select the entry by popcount on
                         * the top two bits on those architectures that
                         * support it, this is what the code below does,
                         * crudely.
                         */
                        unsigned int back_i = PNG_CMAP_RGB_ALPHA_BACKGROUND+1;

                        /* Here are how the values map:
                         *
                         * 0x00 .. 0x3f -> 0
                         * 0x40 .. 0xbf -> 1
                         * 0xc0 .. 0xff -> 2
                         *
                         * So, as above with the explicit alpha checks, the
                         * breakpoints are at 64 and 196.
                         */
                        if (inrow[0] & 0x80) back_i += 9; /* red */
                        if (inrow[0] & 0x40) back_i += 9;
                        if (inrow[0] & 0x80) back_i += 3; /* green */
                        if (inrow[0] & 0x40) back_i += 3;
                        if (inrow[0] & 0x80) back_i += 1; /* blue */
                        if (inrow[0] & 0x40) back_i += 1;

                        *outrow = png_check_byte(png_ptr, back_i);
                     }

                     inrow += 4;
                  }
                  break;

               default:
                  break;
            }
         }
      }
   }

   return 1;
}

static int
png_image_read_colormapped(png_voidp argument)
{
   png_image_read_control *display = png_voidcast(png_image_read_control*,
       argument);
   png_imagep image = display->image;
   png_controlp control = image->opaque;
   png_structrp png_ptr = control->png_ptr;
   png_inforp info_ptr = control->info_ptr;
   int color_type, bit_depth;
   int passes = 0; /* As a flag */

   PNG_SKIP_CHUNKS(png_ptr);

   /* Update the 'info' structure and make sure the result is as required; first
    * make sure to turn on the interlace handling if it will be required
    * (because it can't be turned on *after* the call to png_read_update_info!)
    */
   if (display->colormap_processing == PNG_CMAP_NONE)
      passes = png_set_interlace_handling(png_ptr);

   png_read_update_info(png_ptr, info_ptr);

   /* Avoid the 'easy access' functions below because this allows them to be
    * disabled; there are not useful with the simplified API.
    */
   color_type = PNG_COLOR_TYPE_FROM_FORMAT(info_ptr->format);
   bit_depth = info_ptr->bit_depth;

   /* The expected output can be deduced from the colormap_processing option. */
   switch (display->colormap_processing)
   {
      case PNG_CMAP_NONE:
         /* Output must be one channel and one byte per pixel, the output
          * encoding can be anything.
          */
         if ((color_type == PNG_COLOR_TYPE_PALETTE ||
              color_type == PNG_COLOR_TYPE_GRAY) && bit_depth == 8)
            break;

         goto bad_output;

      case PNG_CMAP_TRANS:
      case PNG_CMAP_GA:
         /* Output must be two channels and the 'G' one must be sRGB, the latter
          * can be checked with an exact number because it should have been set
          * to this number above!
          */
         if (color_type == PNG_COLOR_TYPE_GRAY_ALPHA && bit_depth == 8 &&
             !png_need_gamma_correction(png_ptr, png_memory_gamma(png_ptr),
                1/*sRGB*/) &&
             image->colormap_entries == 256)
            break;

         goto bad_output;

      case PNG_CMAP_RGB:
         /* Output must be 8-bit sRGB encoded RGB */
         if (color_type == PNG_COLOR_TYPE_RGB && bit_depth == 8 &&
             !png_need_gamma_correction(png_ptr, png_memory_gamma(png_ptr),
                1/*sRGB*/) &&
             image->colormap_entries == 216)
            break;

         goto bad_output;

      case PNG_CMAP_RGB_ALPHA:
         /* Output must be 8-bit sRGB encoded RGBA */
         if (color_type == PNG_COLOR_TYPE_RGB_ALPHA && bit_depth == 8 &&
             !png_need_gamma_correction(png_ptr, png_memory_gamma(png_ptr),
                1/*sRGB*/) &&
             image->colormap_entries == 244 /* 216 + 1 + 27 */)
            break;

         /* goto bad_output; */
         /* FALL THROUGH */

      default:
      bad_output:
         impossible("bad color-map processing");
   }

   /* Now read the rows.  Do this here if it is possible to read directly into
    * the output buffer, otherwise allocate a local row buffer of the maximum
    * size libpng requires and call the relevant processing routine safely.
    */
   {
      png_voidp first_row = display->buffer;
      ptrdiff_t row_bytes = display->row_stride;

      /* The following expression is designed to work correctly whether it gives
       * a signed or an unsigned result.
       */
      if (row_bytes < 0)
      {
         char *ptr = png_voidcast(char*, first_row);
         ptr += (image->height-1) * (-row_bytes);
         first_row = png_voidcast(png_voidp, ptr);
      }

      display->first_row = first_row;
      display->row_bytes = row_bytes;
   }

   if (passes == 0)
   {
      int result;
      png_voidp row = png_malloc(png_ptr, png_get_rowbytes(png_ptr, info_ptr));

      display->local_row = row;
      result = png_safe_execute(image, png_image_read_and_map, display);
      display->local_row = NULL;
      png_free(png_ptr, row);

      return result;
   }

   else
   {
      png_alloc_size_t row_bytes = display->row_bytes;

      while (--passes >= 0)
      {
         png_uint_32      y = image->height;
         png_bytep        row = png_voidcast(png_bytep, display->first_row);

         while (y-- > 0)
         {
            png_read_row(png_ptr, row, NULL);
            row += row_bytes;
         }
      }

      return 1;
   }
}

/* Just the row reading part of png_image_read. */
static int
png_image_read_composite(png_voidp argument)
{
   png_image_read_control *display = png_voidcast(png_image_read_control*,
       argument);
   png_imagep image = display->image;
   png_structrp png_ptr = image->opaque->png_ptr;
   int passes;

   switch (png_ptr->interlaced)
   {
      case PNG_INTERLACE_NONE:
         passes = 1;
         break;

      case PNG_INTERLACE_ADAM7:
         passes = PNG_INTERLACE_ADAM7_PASSES;
         break;

      default:
         png_error(png_ptr, "unknown interlace type");
   }

   {
      png_uint_32  height = image->height;
      png_uint_32  width = image->width;
      ptrdiff_t    step_row = display->row_bytes;
      unsigned int channels =
          (image->format & PNG_FORMAT_FLAG_COLOR) != 0 ? 3 : 1;
      int pass;

      for (pass = 0; pass < passes; ++pass)
      {
         unsigned int     startx, stepx, stepy;
         png_uint_32      y;

         if (png_ptr->interlaced == PNG_INTERLACE_ADAM7)
         {
            /* The row may be empty for a short image: */
            if (PNG_PASS_COLS(width, pass) == 0)
               continue;

            startx = PNG_PASS_START_COL(pass) * channels;
            stepx = PNG_PASS_COL_OFFSET(pass) * channels;
            y = PNG_PASS_START_ROW(pass);
            stepy = PNG_PASS_ROW_OFFSET(pass);
         }

         else
         {
            y = 0;
            startx = 0;
            stepx = channels;
            stepy = 1;
         }

         for (; y<height; y += stepy)
         {
            png_bytep inrow = png_voidcast(png_bytep, display->local_row);
            png_bytep outrow;
            png_const_bytep end_row;

            /* Read the row, which is packed: */
            png_read_row(png_ptr, inrow, NULL);

            outrow = png_voidcast(png_bytep, display->first_row);
            outrow += y * step_row;
            end_row = outrow + width * channels;

            /* Now do the composition on each pixel in this row. */
            outrow += startx;
            for (; outrow < end_row; outrow += stepx)
            {
               png_byte alpha = inrow[channels];

               if (alpha > 0) /* else no change to the output */
               {
                  unsigned int c;

                  for (c=0; c<channels; ++c)
                  {
                     png_uint_32 component = inrow[c];

                     if (alpha < 255) /* else just use component */
                     {
                        /* This is PNG_OPTIMIZED_ALPHA, the component value
                         * is a linear 8-bit value.  Combine this with the
                         * current outrow[c] value which is sRGB encoded.
                         * Arithmetic here is 16-bits to preserve the output
                         * values correctly.
                         */
                        component *= 257*255; /* =65535 */
                        component += (255-alpha)*png_sRGB_table[outrow[c]];

                        /* So 'component' is scaled by 255*65535 and is
                         * therefore appropriate for the sRGB to linear
                         * conversion table.
                         */
                        component =
                           PNG_sRGB_FROM_LINEAR(png_ptr, component);
                     }

                     outrow[c] = png_check_byte(png_ptr, component);
                  }
               }

               inrow += channels+1; /* components and alpha channel */
            }
         }
      }
   }

   return 1;
}

/* The do_local_background case; called when all the following transforms are to
 * be done:
 *
 * PNG_READ_RGB_TO_GRAY
 * PNG_READ_COMPOSITE
 * PNG_READ_GAMMA
 *
 * This is a work-around for the fact that both the PNG_READ_RGB_TO_GRAY and
 * PNG_COMPOSITE code performs gamma correction, so we get double gamma
 * correction.  The fix-up is to prevent the PNG_COMPOSITE operation from
 * happening inside libpng, so this routine sees an 8 or 16-bit gray+alpha
 * row and handles the removal or pre-multiplication of the alpha channel.
 */
static int
png_image_read_background(png_voidp argument)
{
   png_image_read_control *display = png_voidcast(png_image_read_control*,
       argument);
   png_imagep image = display->image;
   png_structrp png_ptr = image->opaque->png_ptr;
   png_inforp info_ptr = image->opaque->info_ptr;
   png_uint_32 height = image->height;
   png_uint_32 width = image->width;
   int pass, passes;

   /* Double check the convoluted logic below.  We expect to get here with
    * libpng doing rgb to gray and gamma correction but background processing
    * left to the png_image_read_background function.  The rows libpng produce
    * might be 8 or 16-bit but should always have two channels; gray plus alpha.
    */
   affirm(PNG_COLOR_TYPE_FROM_FORMAT(info_ptr->format) ==
          PNG_COLOR_TYPE_GRAY_ALPHA);
   debug(png_get_channels(png_ptr, info_ptr) == 2);

   /* Expect the 8-bit case to always remove the alpha channel */
   if ((image->format & PNG_FORMAT_FLAG_LINEAR) == 0 &&
      (image->format & PNG_FORMAT_FLAG_ALPHA) != 0)
      png_error(png_ptr, "unexpected 8-bit transformation");

   switch (png_ptr->interlaced)
   {
      case PNG_INTERLACE_NONE:
         passes = 1;
         break;

      case PNG_INTERLACE_ADAM7:
         passes = PNG_INTERLACE_ADAM7_PASSES;
         break;

      default:
         png_error(png_ptr, "unknown interlace type");
   }

   /* Use direct access to info_ptr here because otherwise the simplified API
    * would require PNG_EASY_ACCESS_SUPPORTED (just for this.)  Note this is
    * checking the value after libpng expansions, not the original value in the
    * PNG.
    */
   switch (info_ptr->bit_depth)
   {
      default:
         png_error(png_ptr, "unexpected bit depth");
         break;

      case 8:
         /* 8-bit sRGB gray values with an alpha channel; the alpha channel is
          * to be removed by composing on a background: either the row if
          * display->background is NULL or display->background->green if not.
          * Unlike the code above ALPHA_OPTIMIZED has *not* been done.
          */
         {
            png_bytep first_row = png_voidcast(png_bytep, display->first_row);
            ptrdiff_t step_row = display->row_bytes;

            for (pass = 0; pass < passes; ++pass)
            {
               png_bytep row = png_voidcast(png_bytep, display->first_row);
               unsigned int     startx, stepx, stepy;
               png_uint_32      y;

               if (png_ptr->interlaced == PNG_INTERLACE_ADAM7)
               {
                  /* The row may be empty for a short image: */
                  if (PNG_PASS_COLS(width, pass) == 0)
                     continue;

                  startx = PNG_PASS_START_COL(pass);
                  stepx = PNG_PASS_COL_OFFSET(pass);
                  y = PNG_PASS_START_ROW(pass);
                  stepy = PNG_PASS_ROW_OFFSET(pass);
               }

               else
               {
                  y = 0;
                  startx = 0;
                  stepx = stepy = 1;
               }

               if (display->background == NULL)
               {
                  for (; y<height; y += stepy)
                  {
                     png_bytep inrow = png_voidcast(png_bytep,
                         display->local_row);
                     png_bytep outrow = first_row + y * step_row;
                     png_const_bytep end_row = outrow + width;

                     /* Read the row, which is packed: */
                     png_read_row(png_ptr, inrow, NULL);

                     /* Now do the composition on each pixel in this row. */
                     outrow += startx;
                     for (; outrow < end_row; outrow += stepx)
                     {
                        png_byte alpha = inrow[1];

                        if (alpha > 0) /* else no change to the output */
                        {
                           png_uint_32 component = inrow[0];

                           if (alpha < 255) /* else just use component */
                           {
                              /* Since PNG_OPTIMIZED_ALPHA was not set it is
                               * necessary to invert the sRGB transfer
                               * function and multiply the alpha out.
                               */
                              component = png_sRGB_table[component] * alpha;
                              component += png_sRGB_table[outrow[0]] *
                                 (255-alpha);
                              component =
                                 PNG_sRGB_FROM_LINEAR(png_ptr, component);
                           }

                           outrow[0] = png_check_byte(png_ptr, component);
                        }

                        inrow += 2; /* gray and alpha channel */
                     }
                  }
               }

               else /* constant background value */
               {
                  png_byte background8 = display->background->green;
                  png_uint_16 background = png_sRGB_table[background8];

                  for (; y<height; y += stepy)
                  {
                     png_bytep inrow = png_voidcast(png_bytep,
                         display->local_row);
                     png_bytep outrow = first_row + y * step_row;
                     png_const_bytep end_row = outrow + width;

                     /* Read the row, which is packed: */
                     png_read_row(png_ptr, inrow, NULL);

                     /* Now do the composition on each pixel in this row. */
                     outrow += startx;
                     for (; outrow < end_row; outrow += stepx)
                     {
                        png_byte alpha = inrow[1];

                        if (alpha > 0) /* else use background */
                        {
                           png_uint_32 component = inrow[0];

                           if (alpha < 255) /* else just use component */
                           {
                              component = png_sRGB_table[component] * alpha;
                              component += background * (255-alpha);
                              component =
                                 PNG_sRGB_FROM_LINEAR(png_ptr, component);
                           }

                           outrow[0] = png_check_byte(png_ptr, component);
                        }

                        else
                           outrow[0] = background8;

                        inrow += 2; /* gray and alpha channel */
                     }

                     row += display->row_bytes;
                  }
               }
            }
         }
         break;

      case 16:
         /* 16-bit linear with pre-multiplied alpha; the pre-multiplication must
          * still be done and, maybe, the alpha channel removed.  This code also
          * handles the alpha-first option.
          */
         {
            png_uint_16p first_row = png_voidcast(png_uint_16p,
                display->first_row);
            /* The division by two is safe because the caller passed in a
             * stride which was multiplied by 2 (below) to get row_bytes.
             */
            ptrdiff_t    step_row = display->row_bytes / 2;
            int preserve_alpha = (image->format & PNG_FORMAT_FLAG_ALPHA) != 0;
            unsigned int outchannels = 1+preserve_alpha;
            int swap_alpha = 0;

#ifdef PNG_SIMPLIFIED_READ_AFIRST_SUPPORTED
            if (preserve_alpha != 0 &&
                (image->format & PNG_FORMAT_FLAG_AFIRST) != 0)
               swap_alpha = 1;
#endif

            for (pass = 0; pass < passes; ++pass)
            {
               unsigned int     startx, stepx, stepy;
               png_uint_32      y;

               /* The 'x' start and step are adjusted to output components here.
                */
               if (png_ptr->interlaced == PNG_INTERLACE_ADAM7)
               {
                  /* The row may be empty for a short image: */
                  if (PNG_PASS_COLS(width, pass) == 0)
                     continue;

                  startx = PNG_PASS_START_COL(pass) * outchannels;
                  stepx = PNG_PASS_COL_OFFSET(pass) * outchannels;
                  y = PNG_PASS_START_ROW(pass);
                  stepy = PNG_PASS_ROW_OFFSET(pass);
               }

               else
               {
                  y = 0;
                  startx = 0;
                  stepx = outchannels;
                  stepy = 1;
               }

               for (; y<height; y += stepy)
               {
                  png_const_uint_16p inrow;
                  png_uint_16p outrow = first_row + y*step_row;
                  png_uint_16p end_row = outrow + width * outchannels;

                  /* Read the row, which is packed: */
                  png_read_row(png_ptr, png_voidcast(png_bytep,
                      display->local_row), NULL);
                  inrow = png_voidcast(png_const_uint_16p, display->local_row);

                  /* Now do the pre-multiplication on each pixel in this row.
                   */
                  outrow += startx;
                  for (; outrow < end_row; outrow += stepx)
                  {
                     png_uint_32 component = inrow[0];
                     png_uint_16 alpha = inrow[1];

                     if (alpha > 0) /* else 0 */
                     {
                        if (alpha < 65535) /* else just use component */
                        {
                           component *= alpha;
                           component += 32767;
                           component /= 65535;
                        }
                     }

                     else
                        component = 0;

                     outrow[swap_alpha] =
                        png_check_u16(png_ptr, component);
                     if (preserve_alpha != 0)
                        outrow[1 ^ swap_alpha] = alpha;

                     inrow += 2; /* components and alpha channel */
                  }
               }
            }
         }
         break;
   }

   return 1;
}

/* The guts of png_image_finish_read as a png_safe_execute callback. */
static int
png_image_read_direct(png_voidp argument)
{
   png_image_read_control *display = png_voidcast(png_image_read_control*,
       argument);
   png_imagep image = display->image;
   png_structrp png_ptr = image->opaque->png_ptr;
   png_inforp info_ptr = image->opaque->info_ptr;

   png_uint_32 format = image->format;
   int linear = (format & PNG_FORMAT_FLAG_LINEAR) != 0;
   int do_local_compose = 0;
   int do_local_background = 0; /* to avoid double gamma correction bug */
   int passes = 0;

   /* Add transforms to ensure the correct output format is produced then check
    * that the required implementation support is there.  Always expand; always
    * need 8 bits minimum, no palette and expanded tRNS.
    */
   png_set_expand(png_ptr);

   /* Now check the format to see if it was modified. */
   {
      png_uint_32 base_format = png_image_format(png_ptr) &
         PNG_BIC_MASK(PNG_FORMAT_FLAG_COLORMAP) /* removed by png_set_expand */;
      png_uint_32 change = format ^ base_format;
      png_fixed_point output_gamma;
      int mode; /* alpha mode */

      /* Do this first so that we have a record if rgb to gray is happening. */
      if ((change & PNG_FORMAT_FLAG_COLOR) != 0)
      {
         /* gray<->color transformation required. */
         if ((format & PNG_FORMAT_FLAG_COLOR) != 0)
            png_set_gray_to_rgb(png_ptr);

         else
         {
            /* libpng can't do both rgb to gray and
             * background/pre-multiplication if there is also significant gamma
             * correction, because both operations require linear colors and
             * the code only supports one transform doing the gamma correction.
             * Handle this by doing the pre-multiplication or background
             * operation in this code, if necessary.
             *
             * TODO: fix this by rewriting pngrtran.c (!)
             *
             * For the moment (given that fixing this in pngrtran.c is an
             * enormous change) 'do_local_background' is used to indicate that
             * the problem exists.
             */
            if ((base_format & PNG_FORMAT_FLAG_ALPHA) != 0)
               do_local_background = 1/*maybe*/;

            png_set_rgb_to_gray_fixed(png_ptr, PNG_ERROR_ACTION_NONE,
                PNG_RGB_TO_GRAY_DEFAULT, PNG_RGB_TO_GRAY_DEFAULT);
         }

         change &= PNG_BIC_MASK(PNG_FORMAT_FLAG_COLOR);
      }

      /* Set the gamma appropriately, linear for 16-bit input, sRGB otherwise.
       */
      {
         png_fixed_point input_gamma_default;

         if ((base_format & PNG_FORMAT_FLAG_LINEAR) != 0 &&
             (image->flags & PNG_IMAGE_FLAG_16BIT_sRGB) == 0)
            input_gamma_default = PNG_GAMMA_LINEAR;
         else
            input_gamma_default = PNG_DEFAULT_sRGB;

         /* Call png_set_alpha_mode to set the default for the input gamma; the
          * output gamma is set by a second call below.
          */
         png_set_alpha_mode_fixed(png_ptr, PNG_ALPHA_PNG, input_gamma_default);
      }

      if (linear != 0)
      {
         /* If there *is* an alpha channel in the input it must be multiplied
          * out; use PNG_ALPHA_STANDARD, otherwise just use PNG_ALPHA_PNG.
          */
         if ((base_format & PNG_FORMAT_FLAG_ALPHA) != 0)
            mode = PNG_ALPHA_STANDARD; /* associated alpha */

         else
            mode = PNG_ALPHA_PNG;

         output_gamma = PNG_GAMMA_LINEAR;
      }

      else
      {
         mode = PNG_ALPHA_PNG;
         output_gamma = PNG_DEFAULT_sRGB;
      }

      /* If 'do_local_background' is set check for the presence of gamma
       * correction; this is part of the work-round for the libpng bug
       * described above.
       *
       * TODO: fix libpng and remove this.
       */
      if (do_local_background != 0)
      {
         /* This is intended to be a safe check to see if libpng will perform
          * gamma work in pngrtran.c; if it will *not* be performed the
          * do_local_background flag is cancelled.
          */
         if (!png_need_gamma_correction(png_ptr, 0/*PNG gamma*/,
                  output_gamma != PNG_GAMMA_LINEAR))
            do_local_background = 0;

         else if (mode == PNG_ALPHA_STANDARD)
         {
            do_local_background = 2/*required*/;
            mode = PNG_ALPHA_PNG; /* prevent libpng doing it */
         }

         /* else leave as 1 for the checks below */
      }

      /* If the bit-depth changes then handle that here. */
      if ((change & PNG_FORMAT_FLAG_LINEAR) != 0)
      {
         if (linear != 0 /*16-bit output*/)
            png_set_expand_16(png_ptr);

         else /* 8-bit output */
            png_set_scale_16(png_ptr);

         change &= PNG_BIC_MASK(PNG_FORMAT_FLAG_LINEAR);
      }

      /* Now the background/alpha channel changes. */
      if ((change & PNG_FORMAT_FLAG_ALPHA) != 0)
      {
         /* Removing an alpha channel requires composition for the 8-bit
          * formats; for the 16-bit it is already done, above, by the
          * pre-multiplication and the channel just needs to be stripped.
          */
         if ((base_format & PNG_FORMAT_FLAG_ALPHA) != 0)
         {
            /* If RGB->gray is happening the alpha channel must be left and the
             * operation completed locally.
             *
             * TODO: fix libpng and remove this.
             */
            if (do_local_background != 0)
               do_local_background = 2/*required*/;

            /* 16-bit output: just remove the channel */
            else if (linear != 0) /* compose on black (well, pre-multiply) */
               png_set_strip_alpha(png_ptr);

            /* 8-bit output: do an appropriate compose */
            else if (display->background != NULL)
            {
               png_color_16 c;

               c.index = 0; /*unused*/
               c.red = display->background->red;
               c.green = display->background->green;
               c.blue = display->background->blue;
               c.gray = display->background->green;

               /* This is always an 8-bit sRGB value, using the 'green' channel
                * for gray is much better than calculating the luminance here;
                * we can get off-by-one errors in that calculation relative to
                * the app expectations and that will show up in transparent
                * pixels.
                */
               png_set_background_fixed(png_ptr, &c,
                   PNG_BACKGROUND_GAMMA_SCREEN, 0/*need_expand*/,
                   0/*gamma: not used*/);
            }

            else /* compose on row: implemented below. */
            {
               do_local_compose = 1;
               /* This leaves the alpha channel in the output, so it has to be
                * removed by the code below.  Set the encoding to the 'OPTIMIZE'
                * one so the code only has to hack on the pixels that require
                * composition.
                */
               mode = PNG_ALPHA_OPTIMIZED;
            }
         }

         else /* output needs an alpha channel */
         {
            /* This is tricky because it happens before the swap operation has
             * been accomplished; however, the swap does *not* swap the added
             * alpha channel (weird API), so it must be added in the correct
             * place.
             */
            png_uint_32 filler; /* opaque filler */
            int where;

            if (linear != 0)
               filler = 65535;

            else
               filler = 255;

#ifdef PNG_FORMAT_AFIRST_SUPPORTED
            if ((format & PNG_FORMAT_FLAG_AFIRST) != 0)
            {
               where = PNG_FILLER_BEFORE;
               change &= PNG_BIC_MASK(PNG_FORMAT_FLAG_AFIRST);
            }

            else
#endif
               where = PNG_FILLER_AFTER;

            png_set_add_alpha(png_ptr, filler, where);
         }

         /* This stops the (irrelevant) call to swap_alpha below. */
         change &= PNG_BIC_MASK(PNG_FORMAT_FLAG_ALPHA);
      }

      /* Now set the alpha mode correctly; this is always done, even if there is
       * no alpha channel in either the input or the output because it correctly
       * sets the output gamma.
       */
      png_set_alpha_mode_fixed(png_ptr, mode, output_gamma);

#  ifdef PNG_FORMAT_BGR_SUPPORTED
      if ((change & PNG_FORMAT_FLAG_BGR) != 0)
      {
         /* Check only the output format; PNG is never BGR; don't do this if
          * the output is gray, but fix up the 'format' value in that case.
          */
         if ((format & PNG_FORMAT_FLAG_COLOR) != 0)
            png_set_bgr(png_ptr);

         else
            format &= PNG_BIC_MASK(PNG_FORMAT_FLAG_BGR);

         change &= PNG_BIC_MASK(PNG_FORMAT_FLAG_BGR);
      }
#  endif

#  ifdef PNG_FORMAT_AFIRST_SUPPORTED
      if ((change & PNG_FORMAT_FLAG_AFIRST) != 0)
      {
         /* Only relevant if there is an alpha channel - it's particularly
          * important to handle this correctly because do_local_compose may
          * be set above and then libpng will keep the alpha channel for this
          * code to remove.
          */
         if ((format & PNG_FORMAT_FLAG_ALPHA) != 0)
         {
            /* Disable this if doing a local background,
             * TODO: remove this when local background is no longer required.
             */
            if (do_local_background != 2)
               png_set_swap_alpha(png_ptr);
         }

         else
            format &= PNG_BIC_MASK(PNG_FORMAT_FLAG_AFIRST);

         change &= PNG_BIC_MASK(PNG_FORMAT_FLAG_AFIRST);
      }
#  endif

      /* If the *output* is 16-bit then we need to check for a byte-swap on this
       * architecture.
       */
      if (linear != 0)
      {
         PNG_CONST png_uint_16 le = 0x0001;

         if ((*(png_const_bytep) & le) != 0)
            png_set_swap(png_ptr);
      }

      /* If change is not now 0 some transformation is missing - error out. */
      if (change != 0)
         png_error(png_ptr, "png_read_image: unsupported transformation");
   }

   PNG_SKIP_CHUNKS(png_ptr);

   /* Update the 'info' structure and make sure the result is as required; first
    * make sure to turn on the interlace handling if it will be required
    * (because it can't be turned on *after* the call to png_read_update_info!)
    *
    * TODO: remove the do_local_background fixup below.
    */
   if (do_local_compose == 0 && do_local_background != 2)
      passes = png_set_interlace_handling(png_ptr);

   png_read_update_info(png_ptr, info_ptr);

   {
      png_uint_32 out_format = png_memory_format(png_ptr);

      /* Swapping is expected for the 16-bit format: */
      out_format &= PNG_BIC_MASK(PNG_FORMAT_FLAG_SWAPPED);

      /* The remaining upper bits should never be set: */
      affirm(!(out_format & ~0x3FU));

      if ((out_format & PNG_FORMAT_FLAG_ALPHA) != 0)
      {
         /* do_local_compose removes this channel below. */
         if (do_local_compose != 0 ||
               /* do_local_background does the same if required. */
               (do_local_background == 2 &&
                  (format & PNG_FORMAT_FLAG_ALPHA) == 0))
               out_format &= PNG_BIC_MASK(PNG_FORMAT_FLAG_ALPHA);
      }

      else
         affirm(do_local_compose == 0 /* else alpha channel lost */);

      switch (png_memory_channel_depth(png_ptr))
      {
         case 16: affirm((out_format & PNG_FORMAT_FLAG_LINEAR) != 0); break;
         case  8: affirm((out_format & PNG_FORMAT_FLAG_LINEAR) == 0); break;
         default: impossible("unexpected bit depth"); break;
      }

#  ifdef PNG_FORMAT_AFIRST_SUPPORTED
      if (do_local_background == 2)
      {
         /* do_local_background should be handling the swap: */
         affirm(!(out_format & PNG_FORMAT_FLAG_AFIRST));

         if ((format & PNG_FORMAT_FLAG_AFIRST) != 0)
            out_format |= PNG_FORMAT_FLAG_AFIRST;
      }
#  endif

      /* This is actually an internal error. */
      affirm(out_format == format /* else unimplemented transformations */);
   }

   /* Now read the rows.  If do_local_compose is set then it is necessary to use
    * a local row buffer.  The output will be GA, RGBA or BGRA and must be
    * converted to G, RGB or BGR as appropriate.  The 'local_row' member of the
    * display acts as a flag.
    */
   {
      png_voidp first_row = display->buffer;
      ptrdiff_t row_bytes = display->row_stride;

      if (linear != 0)
         row_bytes *= 2;

      /* The following expression is designed to work correctly whether it gives
       * a signed or an unsigned result.
       */
      if (row_bytes < 0)
      {
         char *ptr = png_voidcast(char*, first_row);
         ptr += (image->height-1) * (-row_bytes);
         first_row = png_voidcast(png_voidp, ptr);
      }

      display->first_row = first_row;
      display->row_bytes = row_bytes;
   }

   if (do_local_compose != 0)
   {
      int result;
      png_voidp row = png_malloc(png_ptr, png_get_rowbytes(png_ptr, info_ptr));

      display->local_row = row;
      result = png_safe_execute(image, png_image_read_composite, display);
      display->local_row = NULL;
      png_free(png_ptr, row);

      return result;
   }

   else if (do_local_background == 2)
   {
      int result;
      png_voidp row = png_malloc(png_ptr, png_get_rowbytes(png_ptr, info_ptr));

      display->local_row = row;
      result = png_safe_execute(image, png_image_read_background, display);
      display->local_row = NULL;
      png_free(png_ptr, row);

      return result;
   }

   else
   {
      png_alloc_size_t row_bytes = display->row_bytes;

      while (--passes >= 0)
      {
         png_uint_32      y = image->height;
         png_bytep        row = png_voidcast(png_bytep, display->first_row);

         while (y-- > 0)
         {
            png_read_row(png_ptr, row, NULL);
            row += row_bytes;
         }
      }

      return 1;
   }
}

int PNGAPI
png_image_finish_read(png_imagep image, png_const_colorp background,
    void *buffer, ptrdiff_t row_stride, void *colormap)
{
   if (image != NULL && image->version == PNG_IMAGE_VERSION)
   {
      /* Check for row_stride overflow.  This check is not performed on the
       * original PNG format because it may not occur in the output PNG format
       * and libpng deals with the issues of reading the original.
       */
      const unsigned int channels = PNG_IMAGE_PIXEL_CHANNELS(image->format);

      /* The test is slightly evil: it assumes that a signed pointer difference
       * (ptrdiff_t) can hold a maximum value of half, rounded down, of the
       * maximum of a (size_t).  This is almost certain to be true.
       */
      if (image->width <= (PNG_SIZE_MAX >> 1)/channels) /* no overflow */
      {
         png_alloc_size_t check;
         const png_alloc_size_t png_row_stride =
            (png_alloc_size_t)/*SAFE*/image->width * channels;

         if (row_stride == 0)
            row_stride = (ptrdiff_t)png_row_stride;

         if (row_stride < 0)
            check = -row_stride;

         else
            check = row_stride;

         if (image->opaque != NULL && buffer != NULL && check >= png_row_stride)
         {
            /* Now check for overflow of the image buffer calculation; this
             * limits the whole image size to PNG_SIZE_MAX bytes.
             *
             * The PNG_IMAGE_BUFFER_SIZE macro is:
             *
             *    (PNG_IMAGE_PIXEL_COMPONENT_SIZE(fmt)*height*(row_stride))
             *
             * We have no way of guaranteeing that the application used the
             * correct type for 'row_stride' if it used the macro, so this is
             * technically not completely safe, but this is the case throughout
             * libpng; the app is responsible for making sure the calcualtion of
             * buffer sizes does not overflow.
             */
            if (image->height <= PNG_SIZE_MAX /
                  PNG_IMAGE_PIXEL_COMPONENT_SIZE(image->format) / check)
            {
               if ((image->format & PNG_FORMAT_FLAG_COLORMAP) == 0 ||
                  (image->colormap_entries > 0 && colormap != NULL))
               {
                  int result;
                  png_image_read_control display;

                  memset(&display, 0, (sizeof display));
                  display.image = image;
                  display.buffer = buffer;
                  display.row_stride = row_stride;
                  display.colormap = colormap;
                  display.background = background;
                  display.local_row = NULL;

                  /* Choose the correct 'end' routine; for the color-map case
                   * all the setup has already been done.
                   */
                  if ((image->format & PNG_FORMAT_FLAG_COLORMAP) != 0)
                     result =
                         png_safe_execute(image,
                             png_image_read_colormap, &display) &&
                             png_safe_execute(image,
                             png_image_read_colormapped, &display);

                  else
                     result =
                        png_safe_execute(image,
                            png_image_read_direct, &display);

                  png_image_free(image);
                  return result;
               }

               else
                  return png_image_error(image,
                      "png_image_finish_read[color-map]: no color-map");
            }

            else
               return png_image_error(image,
                   "png_image_finish_read: image too large");
         }

         else
            return png_image_error(image,
                "png_image_finish_read: invalid argument");
      }

      else
         return png_image_error(image,
             "png_image_finish_read: row_stride too large");
   }

   else if (image != NULL)
      return png_image_error(image,
          "png_image_finish_read: damaged PNG_IMAGE_VERSION");

   return 0;
}

#endif /* SIMPLIFIED_READ */
#endif /* READ */
