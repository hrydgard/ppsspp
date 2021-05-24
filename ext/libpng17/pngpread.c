
/* pngpread.c - read a png file in push mode
 *
 * Last changed in libpng 1.7.0 [(PENDING RELEASE)]
 * Copyright (c) 1998-2002,2004,2006-2016 Glenn Randers-Pehrson
 * (Version 0.96 Copyright (c) 1996, 1997 Andreas Dilger)
 * (Version 0.88 Copyright (c) 1995, 1996 Guy Eric Schalnat, Group 42, Inc.)
 *
 * This code is released under the libpng license.
 * For conditions of distribution and use, see the disclaimer
 * and license in png.h
 */

#include "pngpriv.h"
#define PNG_SRC_FILE PNG_SRC_FILE_pngpread

#ifdef PNG_PROGRESSIVE_READ_SUPPORTED

/* Standard callbacks */
static void PNGCBAPI
png_push_fill_buffer(png_structp png_ptr, png_bytep buffer, png_size_t length)
{
   png_bytep ptr;

   if (png_ptr == NULL)
      return;

   ptr = buffer;
   debug(length > 0);

   if (length > 0 && png_ptr->save_buffer_size > 0)
   {
      png_size_t save_size;

      if (length < png_ptr->save_buffer_size)
         save_size = length;

      else
         save_size = png_ptr->save_buffer_size;

      memcpy(ptr, png_ptr->save_buffer_ptr, save_size);
      length -= save_size;
      ptr += save_size;
      png_ptr->buffer_size -= save_size;
      png_ptr->save_buffer_size -= save_size;
      png_ptr->save_buffer_ptr += save_size;
   }

   if (length > 0 && png_ptr->current_buffer_size > 0)
   {
      png_size_t save_size;

      if (length < png_ptr->current_buffer_size)
         save_size = length;

      else
         save_size = png_ptr->current_buffer_size;

      memcpy(ptr, png_ptr->current_buffer_ptr, save_size);
      png_ptr->buffer_size -= save_size;
      png_ptr->current_buffer_size -= save_size;
      png_ptr->current_buffer_ptr += save_size;
   }
}

/* Push model modes: png_chunk_op plus extras */
typedef enum
{
   /* all the png_chunk_op codes, plus: */
   png_read_signature = 0, /* starting value */
   png_read_chunk_header,
   png_read_end_IDAT,
   png_read_done,
   png_read_chunk, /* Not a state, use these derived from png_chunk_op: */
   png_read_chunk_skip = png_read_chunk+png_chunk_skip,
   png_read_chunk_unknown = png_read_chunk+png_chunk_unknown,
   png_read_chunk_process_all = png_read_chunk+png_chunk_process_all,
   png_read_chunk_process_part = png_read_chunk+png_chunk_process_part
} png_read_mode;

static void
png_push_save_buffer_partial(png_structrp png_ptr, size_t amount)
{
   /* Copy 'amount' bytes to the end of the save buffer from the current
    * buffer.
    */
   png_bytep buffer;
   size_t save_size = png_ptr->save_buffer_size;

   if (save_size > PNG_SIZE_MAX - amount)
      png_error(png_ptr, "save buffer overflow");

   if (png_ptr->save_buffer_max < save_size + amount)
   {
      /* Reallocate the save buffer. */
      buffer = png_voidcast(png_bytep, png_malloc(png_ptr, save_size + amount));
      memcpy(buffer, png_ptr->save_buffer_ptr, save_size);
      png_free(png_ptr, png_ptr->save_buffer);
      png_ptr->save_buffer_ptr = png_ptr->save_buffer = buffer;
   }

   else if (png_ptr->save_buffer_max -
      (png_ptr->save_buffer_ptr - png_ptr->save_buffer) < save_size + amount)
   {
      /* Move the existing saved data */
      buffer = png_ptr->save_buffer;
      memmove(buffer, png_ptr->save_buffer_ptr, save_size);
      png_ptr->save_buffer_ptr = buffer;
   }

   else /* Just copy the data */
      buffer = png_ptr->save_buffer_ptr;

   memcpy(buffer+save_size, png_ptr->current_buffer_ptr, amount);
   png_ptr->save_buffer_size = save_size + amount;
   png_ptr->current_buffer_ptr += amount;
   png_ptr->current_buffer_size -= amount;
   png_ptr->buffer_size -= amount;
}

static void
png_push_save_buffer(png_structrp png_ptr)
{
   png_push_save_buffer_partial(png_ptr, png_ptr->current_buffer_size);
   /* This terminates the process loop. */
   png_ptr->buffer_size = 0;
}

#define PNG_PUSH_SAVE_BUFFER_IF_FULL \
if (png_ptr->chunk_length + 4 > png_ptr->buffer_size) \
   { png_push_save_buffer(png_ptr); return; }
#define PNG_PUSH_SAVE_BUFFER_IF_LT(N) \
if (png_ptr->buffer_size < N) \
   { png_push_save_buffer(png_ptr); return; }

png_size_t PNGAPI
png_process_data_pause(png_structrp png_ptr, int save)
{
   if (png_ptr != NULL)
   {
      /* It's easiest for the caller if we do the save; then the caller doesn't
       * have to supply the same data again:
       */
      if (save != 0)
         png_push_save_buffer(png_ptr);
      else
      {
         /* This includes any pending saved bytes: */
         png_size_t remaining = png_ptr->buffer_size;
         png_ptr->buffer_size = 0; /* Terminate the process loop */

         /* So subtract the saved buffer size, unless all the data
          * is actually 'saved', in which case we just return 0
          */
         if (png_ptr->save_buffer_size < remaining)
            return remaining - png_ptr->save_buffer_size;
      }
   }

   return 0;
}

png_uint_32 PNGAPI
png_process_data_skip(png_structrp png_ptr)
{
   if (png_ptr != NULL && png_ptr->process_mode == png_read_chunk_skip)
   {
      /* At the end of png_process_data the buffer size must be 0 (see the loop
       * above) so we can detect a broken call here:
       */
      if (png_ptr->buffer_size != 0)
         png_app_error(png_ptr,
            "png_process_data_skip called inside png_process_data");

      /* If is impossible for there to be a saved buffer at this point -
       * otherwise we could not be in SKIP mode.  This will also happen if
       * png_process_skip is called inside png_process_data (but only very
       * rarely.)
       */
      else if (png_ptr->save_buffer_size != 0)
         png_app_error(png_ptr, "png_process_data_skip called with saved data");

      else
      {
         /* Skipping png_ptr->chunk_length of data then checking the CRC, after
          * that a new chunk header will be read.
          */
         png_ptr->process_mode = png_read_chunk_header;
         return png_ptr->chunk_length + 4;
      }
   }

   return 0;
}

static void
png_push_restore_buffer(png_structrp png_ptr, png_bytep buffer,
    png_size_t buffer_length)
{
   png_ptr->current_buffer_size = buffer_length;
   png_ptr->buffer_size = buffer_length + png_ptr->save_buffer_size;
   png_ptr->current_buffer_ptr = buffer;
}

/* Read any remaining signature bytes from the stream and compare them with
 * the correct PNG signature.  It is possible that this routine is called
 * with bytes already read from the signature, either because they have been
 * checked by the calling application, or because of multiple calls to this
 * routine.
 */
static void
png_push_read_signature(png_structrp png_ptr, png_inforp info_ptr)
{
   unsigned int num_checked = png_ptr->sig_bytes;
   unsigned int num_to_check = 8 - num_checked;

   if (png_ptr->buffer_size < num_to_check)
      num_to_check = (int)/*SAFE*/png_ptr->buffer_size;

   png_push_fill_buffer(png_ptr, &(info_ptr->signature[num_checked]),
       num_to_check);
   png_ptr->sig_bytes = png_check_byte(png_ptr,
       png_ptr->sig_bytes + num_to_check);

   if (png_sig_cmp(info_ptr->signature, num_checked, num_to_check))
   {
      if (num_checked < 4 &&
          png_sig_cmp(info_ptr->signature, num_checked, num_to_check - 4))
         png_error(png_ptr, "Not a PNG file");

      else
         png_error(png_ptr, "PNG file corrupted by ASCII conversion");
   }

   else if (png_ptr->sig_bytes >= 8)
      png_ptr->process_mode = png_read_chunk_header;
}

static void
png_push_crc_finish(png_structrp png_ptr)
   /* CRC the remainder of the chunk data; png_struct::chunk_length must be the
    * amount of data left to read excluding the CRC.
    */
{
   if (png_ptr->chunk_length != 0 && png_ptr->save_buffer_size != 0)
   {
      png_size_t save_size = png_ptr->save_buffer_size;
      png_uint_32 skip_length = png_ptr->chunk_length;

      /* We want the smaller of 'skip_length' and 'save_buffer_size', but
       * they are of different types and we don't know which variable has the
       * fewest bits.  Carefully select the smaller and cast it to the type of
       * the larger - this cannot overflow.  Do not cast in the following test
       * - it will break on either 16 or 64 bit platforms.
       */
      if (skip_length < save_size)
         save_size = (png_size_t)/*SAFE*/skip_length;

      else
         skip_length = (png_uint_32)/*SAFE*/save_size;

      png_calculate_crc(png_ptr, png_ptr->save_buffer_ptr, save_size);

      png_ptr->chunk_length -= skip_length;
      png_ptr->buffer_size -= save_size;
      png_ptr->save_buffer_size -= save_size;
      png_ptr->save_buffer_ptr += save_size;
   }

   if (png_ptr->chunk_length != 0 && png_ptr->current_buffer_size != 0)
   {
      png_size_t save_size = png_ptr->current_buffer_size;
      png_uint_32 skip_length = png_ptr->chunk_length;

      /* We want the smaller of 'skip_length' and 'current_buffer_size', here,
       * the same problem exists as above and the same solution.
       */
      if (skip_length < save_size)
         save_size = (png_size_t)/*SAFE*/skip_length;

      else
         skip_length = (png_uint_32)/*SAFE*/save_size;

      png_calculate_crc(png_ptr, png_ptr->current_buffer_ptr, save_size);

      png_ptr->chunk_length -= skip_length;
      png_ptr->buffer_size -= save_size;
      png_ptr->current_buffer_size -= save_size;
      png_ptr->current_buffer_ptr += save_size;
   }

   if (png_ptr->chunk_length == 0)
   {
      PNG_PUSH_SAVE_BUFFER_IF_LT(4)
      png_crc_finish(png_ptr, 0);
      png_ptr->process_mode = png_read_chunk_header;
   }
}

static void
png_push_read_unknown(png_structrp png_ptr, png_inforp info_ptr)
{
   /* Handle an unknown chunk.  All the data is available but it may not
    * all be in the same buffer.  png_handle_unknown needs the chunk data in
    * just one buffer.
    */
   png_bytep buffer;
   png_uint_32 chunk_length = png_ptr->chunk_length;

   if (png_ptr->save_buffer_size > 0)
   {
      png_size_t save_size = png_ptr->save_buffer_size;

      if (save_size < chunk_length)
      {
         /* Copy the current_buffer_ptr data into the save buffer. */
         png_push_save_buffer_partial(png_ptr, chunk_length - save_size);
         save_size = chunk_length;
      }

      buffer = png_ptr->save_buffer_ptr;
      png_ptr->save_buffer_ptr = buffer+chunk_length;
      png_ptr->save_buffer_size = save_size-chunk_length;
      png_ptr->buffer_size -= chunk_length;
      affirm(png_ptr->buffer_size >= 4);
   }

   else
   {
      affirm(png_ptr->current_buffer_size >= chunk_length+4);
      buffer = png_ptr->current_buffer_ptr;
      png_ptr->current_buffer_ptr = buffer+chunk_length;
      png_ptr->current_buffer_size -= chunk_length;
      png_ptr->buffer_size -= chunk_length;
   }

   /* Now check the CRC, before attempting the unknown handling. */
   png_calculate_crc(png_ptr, buffer, chunk_length);
   png_crc_finish(png_ptr, 0);
#  ifdef PNG_READ_UNKNOWN_CHUNKS_SUPPORTED
      png_handle_unknown(png_ptr, info_ptr, buffer);
#  else /* !READ_UNKNOWN_CHUNKS */
      PNG_UNUSED(info_ptr)
#  endif /* !READ_UNKNOWN_CHUNKS */
   png_ptr->process_mode = png_read_chunk_header;
}

static void
png_push_have_row(png_structrp png_ptr, png_bytep row)
{
   if (png_ptr->row_fn != NULL)
   {
      png_uint_32 row_number = png_ptr->row_number;
      png_byte pass = png_ptr->pass;

      if (png_ptr->interlaced)
      {
         /* If the row de-interlace is not being done by PNG this wacky API
          * delivers the row number in the pass to the caller.  We know that
          * if we get here the row exists, so the number is just one less than
          * the height of an interlaced image with just the rows up to this
          * one:
          */
#        ifdef PNG_READ_INTERLACING_SUPPORTED
            if (!png_ptr->do_interlace)
#        endif /* READ_INTERLACING */
         {
            affirm(PNG_ROW_IN_INTERLACE_PASS(row_number, pass) && row != NULL);
            row_number = PNG_PASS_ROWS(row_number+1, pass);
            affirm(row_number > 0);
            --row_number;
         }
      }

      (*(png_ptr->row_fn))(png_ptr, row, row_number, pass);
   }
}

static void
png_push_read_sync_zstream(png_structp png_ptr, png_bytep *bufferp,
   size_t *buffer_lengthp)
   /* Synchronize the png_struct progressive read buffer
    * {*bufferp,*buffer_lengthp} with png_struct::zstream.next_in, on the
    * assumption that the zstream had previously been set up with *bufferp.
    */
{
   png_bytep original_start = *bufferp;
   png_alloc_size_t bytes_consumed = png_ptr->zstream.next_in - original_start;

   affirm(buffer_lengthp != NULL);

   /* Calculate the CRC for the consumed data: */
   png_calculate_crc(png_ptr, original_start, bytes_consumed);

   /* Update the buffer pointers and the various lengths: */
   *bufferp = original_start + bytes_consumed; /* == png_ptr->zstream.next_in */

   affirm(bytes_consumed <= *buffer_lengthp);
   *buffer_lengthp -= (size_t)/*SAFE*/bytes_consumed;

   affirm(bytes_consumed <= png_ptr->chunk_length);
   png_ptr->chunk_length -= (png_uint_32)/*SAFE*/bytes_consumed;

   affirm(bytes_consumed <= png_ptr->buffer_size);
   png_ptr->buffer_size -= (size_t)/*SAFE*/bytes_consumed;
}

static void
png_push_read_process_IDAT(png_structp png_ptr, png_bytep *bufferp,
   size_t *buffer_lengthp)
   /* If the the *buffer_lengthp parameter is NULL there is no more input,
    * png_struct::mode & PNG_AFTER_IDAT must be set at this point.
    */
{
   png_alloc_size_t buffer_length;

   if (buffer_lengthp != NULL)
      buffer_length = *buffer_lengthp;

   else /* end of IDAT */
   {
      /* SECURITY: if this affirm fails the code would go into an infinite loop;
       * see the handling of avail_in == 0  in png_inflate_IDAT.
       */
      affirm(png_ptr->mode & PNG_AFTER_IDAT);
      buffer_length = 0;
   }

   /* This routine attempts to process all the data it has been given before
    * returning, calling the row callback as required to handle the
    * uncompressed results.
    *
    * If a pause happens during processing (png_ptr->buffer_size is set to 0)
    * or the end of the chunk is encountered the routine may return without
    * handling all the input data.
    */
   if (buffer_length > png_ptr->chunk_length)
   {
      buffer_length = png_ptr->chunk_length;

      /* This works because the last part of a 'skip' is to read and check the
       * CRC, then the process mode is set to png_read_chunk_header.
       */
      if (buffer_length == 0)
         png_ptr->process_mode = png_read_chunk_skip;
   }

   /* It is possble for buffer_length to be zero at this point if the stream
    * caontains a zero length IDAT.  This is handled below.
    */
   png_ptr->zstream.next_in = *bufferp;

   while (buffer_length > 0 || buffer_lengthp == NULL)
   {
      if (buffer_length >= ZLIB_IO_MAX)
      {
         png_ptr->zstream.avail_in = ZLIB_IO_MAX;
         buffer_length -= ZLIB_IO_MAX;
      }

      else
      {
         png_ptr->zstream.avail_in = (uInt)/*SAFE*/buffer_length;
         buffer_length = 0;
      }

      /* The last row may already have been processed.
       *
       * row_number is the *current* row number in the range 0..height-1.  It is
       * updated only by the call to png_read_process_IDAT that follows the call
       * which returns something other than png_row_incomplete.
       *
       * At the end of the image that call must *NOT* be made; png_process_IDAT
       * must not be called after the last row.  png_struct::zstream_eod is set
       * below to allow this condition to be detected.
       *
       * Note that png_read_process_IDAT handles errors in the LZ compressed
       * data (i.e. the cases where png_struct::zstream_error is set) by filling
       * the rows in with 0, which is a safe value, so keep calling it until we
       * reach the end of the image.
       */
      if (!png_ptr->zstream_eod)
      {
         png_bytep row_buffer = NULL;
         png_row_op row_op =
            png_read_process_IDAT(png_ptr, NULL, NULL, 1/*save row*/);

         if (row_op != png_row_incomplete)
         {
            /* Have a complete row, so check for end-of-image; do this here
             * because interlaced images can end on earlier rows or passes but
             * we keep calling png_read_process_IDAT until it updates row_number
             * to the last row of the actual image:
             */
            if (png_ptr->row_number+1 >= png_ptr->height &&
                (!png_ptr->interlaced || png_ptr->pass == 6))
               png_ptr->zstream_eod = 1; /* end of image */
         }

         switch (row_op)
         {
            case png_row_incomplete:
               /* more IDAT data needed for row */
               debug(png_ptr->zstream.avail_in == 0);

               /* png_inflate_IDAT is supposed to handle this and recognize a
                * call with 0 avail_in as end of stream:
                */
               affirm(buffer_lengthp != NULL);
               continue;

            case png_row_process:
               /* If a row was obtained after the end of the IDAT this was done
                * by fabricating data, ensure this is reported, else there is a
                * security issue; normally libpng does a png_error in this
                * case, even if the error is ignored png_struct::zstream_error
                * should be set so somehow the error wasn't noticed!
                */
               affirm(buffer_lengthp != NULL || png_ptr->zstream_error);

               /* png_struct::transformed_row contains a complete, transformed,
                * row; this is processed in both 'sparkle' and 'block' mode.
                */
#              ifdef PNG_TRANSFORM_MECH_SUPPORTED
                  row_buffer = png_ptr->transformed_row;
                  if (row_buffer == NULL)
#              endif /* TRANSFORM_MECH */
                  row_buffer = png_ptr->row_buffer;
               break;

            case png_row_repeat:
               /* row not in this pass, but the existing row in
                * png_struct::transformed_row may be used, this is only required
                * if the 'block' or 'rectangle' mode of display is done and
                * libpng is handling the de-interlace; when the app does it it
                * only see the real rows.
                */
#              ifdef PNG_READ_INTERLACING_SUPPORTED
                  if (png_ptr->do_interlace)
                  {
#                    ifdef PNG_TRANSFORM_MECH_SUPPORTED
                        row_buffer = png_ptr->transformed_row;
                        if (row_buffer == NULL)
#                    endif /* TRANSFORM_MECH */
                        row_buffer = png_ptr->row_buffer;
                     break;
                  }
#              endif /* READ_INTERLACING */
               continue;

            case png_row_skip:
               /* row not in pass and no appropriate data; skip this row,
                * nothing more need be done, except the read_row_fn.  The use
                * of 'NULL' to mean this row doesn't contribute to the output
                * is historical and not documented;
                */
#              ifdef PNG_READ_INTERLACING_SUPPORTED
                  if (png_ptr->do_interlace)
                     break;
#              endif /* READ_INTERLACING */
               continue;

            default:
               impossible("not reached");
         }

         /* Here if there is a row to process. */

         /* Now adjust the buffer pointers before calling png_push_have_row
          * because the callback might call png_process_data_pause and that
          * calls png_push_save_row.  (Yes, this is insane; it was forced on
          * libpng by writers of an external app that ignored the instructions
          * not to fiddle with the insides of png_struct in version 1.4.  It
          * will probably be fixed here before 1.7.0 is released by removing
          * the need for the save buffer entirely.)
          */
         if (buffer_lengthp != NULL)
            png_push_read_sync_zstream(png_ptr, bufferp, buffer_lengthp);

         /* Process one row: */
         png_push_have_row(png_ptr, row_buffer);

         /* The buffer pointer and size may have changed at this point,
          * so everything needs to be reloaded if we can continue reading.
          */
         if (buffer_lengthp != NULL) /* not at end of IDATs */
         {
            if (png_ptr->chunk_length == 0)
               png_ptr->process_mode = png_read_chunk_skip;

            /* If the buffer_size has been set to zero more input is required,
             * this may be a 'pause', and if the specific input buffer being
             * processed has been exhaused then more input is also required.
             * Otherwise we can keep going, however the input buffer may have
             * been changed by the app callback, so do a complete reload:
             */
            else if (png_ptr->buffer_size > 0 && *buffer_lengthp > 0)
               png_push_read_process_IDAT(png_ptr, bufferp, buffer_lengthp);

            return;
         }

         /* If we can't continue reading because there is no more IDAT data this
          * may still be a pause.
          */
         if (png_ptr->buffer_size == 0)
            return;

         /* Else continue, with zero data: */
         continue;
      }

      affirm(png_ptr->zstream_eod);

      if (png_ptr->zowner == 0 || png_read_finish_IDAT(png_ptr))
      {
         /* The zlib stream has ended, there may still be input data in
          * png_ptr->zstream.next_in, restore this.
          */
         debug(png_ptr->zowner == 0 && png_ptr->zstream_ended);

         if (buffer_lengthp != NULL)
         {
            png_push_read_sync_zstream(png_ptr, bufferp, buffer_lengthp);

            /* If the chunk_length is greater than 0 then there is extra data,
             * report this once.  Notice that for IDAT after the end of the
             * stream we keep coming to this point and doing the skip.
             */
            if (png_ptr->chunk_length > 0)
            {
               if (!png_ptr->zstream_error)
               {
                  png_chunk_benign_error(png_ptr,
                     "too much IDAT data (progressive read)");
                  png_ptr->zstream_error = 1;
               }
            }

            /* In any case CRC the chunk, skipping any unneeded data: */
            png_ptr->process_mode = png_read_chunk_skip;
         }
         return;
      }

      /* else more input is required */
      /* NOTE: this test only fires on a small (less than 5 byte) IDAT chunk
       * which just contains the LZ EOF and the Adler32 CRC.
       */
      affirm(png_ptr->zowner == png_IDAT && !png_ptr->zstream_ended);
   }

   /* At this point all the input has been consumed, however the CRC has not
    * been done and the three length fields in png_struct, *buffer_lengthp,
    * buffer_size and chunk_length, all need updating.
    */
   png_push_read_sync_zstream(png_ptr, bufferp, buffer_lengthp);
}

static void
png_push_read_IDAT(png_structrp png_ptr)
{
   if (png_ptr->save_buffer_size > 0)
   {
       png_push_read_process_IDAT(png_ptr, &png_ptr->save_buffer_ptr,
         &png_ptr->save_buffer_size);

       /* This is a slight optimization; normally when the process mode changes
        * there will still be something in the buffer:
        */
       if (png_ptr->save_buffer_size > 0)
          return;

       /* Check for a change in process mode or an application pause before
        * checking the current input buffer.  This is only rarely reached.
        */
       if (png_ptr->process_mode != png_read_chunk_process_part ||
           png_ptr->buffer_size == 0)
          return;
   }

   if (png_ptr->current_buffer_size > 0)
      png_push_read_process_IDAT(png_ptr, &png_ptr->current_buffer_ptr,
         &png_ptr->current_buffer_size);
}

static void
png_push_finish_IDAT(png_structrp png_ptr)
   /* Called once when the first chunk after IDAT is seen. */
{
   /* All of the IDAT data has been processed, however the stream may have
    * been truncated and the image rows may not all have been processed.
    * Clean up here (this doesn't read anything.)
    *
    * 1.7.0: this attempts some measure of compatibility with the sequential
    * API, if the IDAT is truncated and the resultant error reporting doesn't
    * abort the read the image is filled in using zeros of pixel data.  This
    * actually happens inside png_inflate_IDAT (pngrutil.c) when called with
    * z_stream::avail_in == 0.
    */
   while (png_ptr->zowner == png_IDAT)
   {
      png_byte b = 0, *pb = &b;

      png_push_read_process_IDAT(png_ptr, &pb, NULL/*end of IDAT*/);

      if (png_ptr->zowner == 0)
         break;

      if (png_ptr->buffer_size == 0) /* pause */
         return;
   }

    png_ptr->process_mode = png_check_bits(png_ptr, png_ptr->process_mode >> 4,
      4);
}

static void
png_push_have_info(png_structrp png_ptr, png_inforp info_ptr)
{
   if (png_ptr->info_fn != NULL)
      (*(png_ptr->info_fn))(png_ptr, info_ptr);
}

static void
png_push_have_end(png_structrp png_ptr, png_inforp info_ptr)
{
   if (png_ptr->end_fn != NULL)
      (*(png_ptr->end_fn))(png_ptr, info_ptr);
}

static void
png_push_read_chunk_header(png_structrp png_ptr, png_infop info_ptr)
{
   /* Called to read a new chunk header and work out how to handle the remainder
    * of the data.
    */
   unsigned int mode; /* mode prior to the header */
   png_byte chunk_header[8];

   PNG_PUSH_SAVE_BUFFER_IF_LT(8)
   png_push_fill_buffer(png_ptr, chunk_header, 8);
   png_ptr->chunk_length = png_get_uint_31(png_ptr, chunk_header);
   png_ptr->chunk_name = PNG_CHUNK_FROM_STRING(chunk_header+4);
   png_reset_crc(png_ptr, chunk_header+4);
   png_check_chunk_name(png_ptr, png_ptr->chunk_name);
   png_check_chunk_length(png_ptr, png_ptr->chunk_length);
   mode = png_ptr->mode;
   png_ptr->process_mode = png_check_bits(png_ptr,
      png_read_chunk+png_find_chunk_op(png_ptr), 4);

   /* Is this the first IDAT chunk? */
   if ((mode ^ png_ptr->mode) & PNG_HAVE_IDAT)
      png_push_have_info(png_ptr, info_ptr);

   /* Is it the chunk after the last IDAT chunk? */
   else if (((mode ^ png_ptr->mode) & PNG_AFTER_IDAT) != 0)
      png_ptr->process_mode = png_check_bits(png_ptr,
         (png_ptr->process_mode << 4) + png_read_end_IDAT, 8);
}

/* What we do with the incoming data depends on what we were previously
 * doing before we ran out of data...
 */
static void
png_process_some_data(png_structrp png_ptr, png_inforp info_ptr)
{
   if (png_ptr == NULL)
      return;

   switch (png_ptr->process_mode & 0xf)
   {
      case png_read_signature:
         png_push_read_signature(png_ptr, info_ptr);
         return;

      case png_read_chunk_header:
         png_push_read_chunk_header(png_ptr, info_ptr);
         return;

      case png_read_chunk_skip:
         png_push_crc_finish(png_ptr);
         return;

      case png_read_chunk_unknown:
         PNG_PUSH_SAVE_BUFFER_IF_FULL
         png_push_read_unknown(png_ptr, info_ptr);
         return;

      case png_read_chunk_process_all:
         PNG_PUSH_SAVE_BUFFER_IF_FULL
         png_handle_chunk(png_ptr, info_ptr);

         if (png_ptr->mode & PNG_HAVE_IEND)
         {
            png_ptr->process_mode = png_read_done;
            png_push_have_end(png_ptr, info_ptr);
            png_ptr->buffer_size = 0;
         }

         else
            png_ptr->process_mode = png_read_chunk_header;
         return;

      case png_read_chunk_process_part:
         debug(png_ptr->chunk_name == png_IDAT &&
               (png_ptr->mode & PNG_HAVE_IDAT) &&
               !(png_ptr->mode & PNG_AFTER_IDAT));

         if (png_ptr->zowner == 0 && !png_ptr->zstream_ended) /* first time */
            png_read_start_IDAT(png_ptr);

         png_push_read_IDAT(png_ptr);
         return;

      case png_read_end_IDAT:
         png_push_finish_IDAT(png_ptr);
         return;

      case png_read_done:
         png_app_error(png_ptr, "read beyond end of stream");
         png_ptr->buffer_size = 0;
         return;

      default:
         impossible("invalid process mode");
   }
}

void PNGAPI
png_process_data(png_structrp png_ptr, png_inforp info_ptr,
    png_bytep buffer, png_size_t buffer_size)
{
   if (png_ptr == NULL || info_ptr == NULL)
      return;

   png_push_restore_buffer(png_ptr, buffer, buffer_size);

   while (png_ptr->buffer_size)
      png_process_some_data(png_ptr, info_ptr);
}

void PNGAPI
png_set_progressive_read_fn(png_structrp png_ptr, png_voidp progressive_ptr,
    png_progressive_info_ptr info_fn, png_progressive_row_ptr row_fn,
    png_progressive_end_ptr end_fn)
{
   if (png_ptr == NULL)
      return;

   png_ptr->info_fn = info_fn;
   png_ptr->row_fn = row_fn;
   png_ptr->end_fn = end_fn;

   png_set_read_fn(png_ptr, progressive_ptr, png_push_fill_buffer);
}

png_voidp PNGAPI
png_get_progressive_ptr(png_const_structrp png_ptr)
{
   if (png_ptr == NULL)
      return (NULL);

   return png_ptr->io_ptr;
}
#endif /* PROGRESSIVE_READ */
