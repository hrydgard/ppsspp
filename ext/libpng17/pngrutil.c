#ifdef _MSC_VER
#pragma warning (disable:4018)
#pragma warning (disable:4146)
#endif

/* pngrutil.c - utilities to read a PNG file
 *
 * Last changed in libpng 1.7.0 [(PENDING RELEASE)]
 * Copyright (c) 1998-2002,2004,2006-2017 Glenn Randers-Pehrson
 * (Version 0.96 Copyright (c) 1996, 1997 Andreas Dilger)
 * (Version 0.88 Copyright (c) 1995, 1996 Guy Eric Schalnat, Group 42, Inc.)
 *
 * This code is released under the libpng license.
 * For conditions of distribution and use, see the disclaimer
 * and license in png.h
 *
 * This file contains routines that are only called from within
 * libpng itself during the course of reading an image.
 */

#include "pngpriv.h"
#define PNG_SRC_FILE PNG_SRC_FILE_pngrutil

#ifdef PNG_READ_SUPPORTED

#if defined(PNG_READ_gAMA_SUPPORTED) || defined(PNG_READ_cHRM_SUPPORTED)
/* The following is a variation on the above for use with the fixed
 * point values used for gAMA and cHRM.  Instead of png_error it
 * issues a warning and returns (-1) - an invalid value because both
 * gAMA and cHRM use *unsigned* integers for fixed point values.
 */
#define PNG_FIXED_ERROR (-1)

static png_fixed_point /* PRIVATE */
png_get_fixed_point(png_structrp png_ptr, png_const_bytep buf)
{
   png_uint_32 uval = png_get_uint_32(buf);

   if (uval <= PNG_UINT_31_MAX)
      return (png_fixed_point)uval; /* known to be in range */

   /* The caller can turn off the warning by passing NULL. */
   if (png_ptr != NULL)
      png_warning(png_ptr, "PNG fixed point integer out of range");

   return PNG_FIXED_ERROR;
}
#endif /* READ_gAMA or READ_cHRM */

#ifdef PNG_READ_INT_FUNCTIONS_SUPPORTED
/* NOTE: the read macros will obscure these definitions, so that if
 * PNG_USE_READ_MACROS is set the library will not use them internally,
 * but the APIs will still be available externally.
 *
 * The parentheses around "PNGAPI function_name" in the following three
 * functions are necessary because they allow the macros to co-exist with
 * these (unused but exported) functions.
 */

/* Grab an unsigned 32-bit integer from a buffer in big-endian format. */
png_uint_32 (PNGAPI
png_get_uint_32)(png_const_bytep buf)
{
   return PNG_U32(buf[0], buf[1], buf[2], buf[3]);
}

/* Grab a signed 32-bit integer from a buffer in big-endian format.  The
 * data is stored in the PNG file in two's complement format and there
 * is no guarantee that a 'png_int_32' is exactly 32 bits, therefore
 * the following code does a two's complement to native conversion.
 */
png_int_32 (PNGAPI
png_get_int_32)(png_const_bytep buf)
{
   return PNG_S32(buf[0], buf[1], buf[2], buf[3]);
}

/* Grab an unsigned 16-bit integer from a buffer in big-endian format. */
png_uint_16 (PNGAPI
png_get_uint_16)(png_const_bytep buf)
{
   return PNG_U16(buf[0], buf[1]);
}
#endif /* READ_INT_FUNCTIONS */

/* This is an exported function however its error handling is too harsh for most
 * internal use.  For example if it were used for reading the chunk parameters
 * it would error out even on ancillary chunks that can be ignored.
 */
png_uint_32 PNGAPI
png_get_uint_31(png_const_structrp png_ptr, png_const_bytep buf)
{
   png_uint_32 uval = png_get_uint_32(buf);

   if (uval > PNG_UINT_31_MAX)
      png_error(png_ptr, "PNG unsigned integer out of range");

   return uval;
}

/* Read and check the PNG file signature */
void /* PRIVATE */
png_read_sig(png_structrp png_ptr, png_inforp info_ptr)
{
   png_size_t num_checked, num_to_check;

   /* Exit if the user application does not expect a signature. */
   if (png_ptr->sig_bytes >= 8)
      return;

   num_checked = png_ptr->sig_bytes;
   num_to_check = 8 - num_checked;

#ifdef PNG_IO_STATE_SUPPORTED
   png_ptr->io_state = PNG_IO_READING | PNG_IO_SIGNATURE;
#endif

   /* The signature must be serialized in a single I/O call. */
   png_read_data(png_ptr, &(info_ptr->signature[num_checked]), num_to_check);
   png_ptr->sig_bytes = 8;

   if (png_sig_cmp(info_ptr->signature, num_checked, num_to_check))
   {
      if (num_checked < 4 &&
          png_sig_cmp(info_ptr->signature, num_checked, num_to_check - 4))
         png_error(png_ptr, "Not a PNG file");
      else
         png_error(png_ptr, "PNG file corrupted by ASCII conversion");
   }
   if (num_checked < 3)
      png_ptr->mode |= PNG_HAVE_PNG_SIGNATURE;
}

/* Read data, and (optionally) run it through the CRC. */
void /* PRIVATE */
png_crc_read(png_structrp png_ptr, png_voidp buf, png_uint_32 length)
{
   if (png_ptr == NULL)
      return;

   png_read_data(png_ptr, buf, length);
   png_calculate_crc(png_ptr, buf, length);
}

/* Optionally skip data and then check the CRC.  Depending on whether we are
 * reading an ancillary or critical chunk, and how the program has set things
 * up, we may calculate the CRC on the data and print a message.  Returns true
 * if the chunk should be discarded, otherwise false.
 */
int /* PRIVATE */
png_crc_finish(png_structrp png_ptr, png_uint_32 skip)
{
   /* The size of the local buffer for inflate is a good guess as to a
    * reasonable size to use for buffering reads from the application.
    */
   while (skip > 0)
   {
      png_uint_32 len;
      png_byte tmpbuf[PNG_INFLATE_BUF_SIZE];

      len = (sizeof tmpbuf);
      if (len > skip)
         len = skip;
      skip -= len;

      png_crc_read(png_ptr, tmpbuf, len);
   }

   /* Compare the CRC stored in the PNG file with that calculated by libpng from
    * the data it has read thus far.  Do any required error handling.  The
    * second parameter is to allow a critical chunk (specifically PLTE) to be
    * treated as ancillary.
    */
   {
      png_byte crc_bytes[4];

#     ifdef PNG_IO_STATE_SUPPORTED
         png_ptr->io_state = PNG_IO_READING | PNG_IO_CHUNK_CRC;
#     endif

      png_read_data(png_ptr, crc_bytes, 4);

      if (png_ptr->current_crc != crc_quiet_use &&
          png_get_uint_32(crc_bytes) != png_ptr->crc)
      {
         if (png_ptr->current_crc == crc_error_quit)
            png_chunk_error(png_ptr, "CRC");

         else
            png_chunk_warning(png_ptr, "CRC");

         /* The only way to discard a chunk at present is to issue a warning.
          * TODO: quiet_discard.
          */
         return png_ptr->current_crc == crc_warn_discard;
      }
   }

   return 0;
}

#if defined(PNG_READ_iCCP_SUPPORTED) || defined(PNG_READ_iTXt_SUPPORTED) ||\
    defined(PNG_READ_pCAL_SUPPORTED) || defined(PNG_READ_sCAL_SUPPORTED) ||\
    defined(PNG_READ_sPLT_SUPPORTED) || defined(PNG_READ_tEXt_SUPPORTED) ||\
    defined(PNG_READ_zTXt_SUPPORTED) || defined(PNG_SEQUENTIAL_READ_SUPPORTED)
/* Manage the read buffer; this simply reallocates the buffer if it is not small
 * enough (or if it is not allocated).  The routine returns a pointer to the
 * buffer; if an error occurs and 'warn' is set the routine returns NULL, else
 * it will call png_error (via png_malloc) on failure.  (warn == 2 means
 * 'silent').
 */
png_bytep /* PRIVATE */
png_read_buffer(png_structrp png_ptr, png_alloc_size_t new_size, int warn)
{
   png_bytep buffer = png_ptr->read_buffer;

   if (buffer != NULL && new_size > png_ptr->read_buffer_size)
   {
      png_ptr->read_buffer = NULL;
      png_ptr->read_buffer_size = 0;
      png_free(png_ptr, buffer);
      buffer = NULL;
   }

   if (buffer == NULL)
   {
      buffer = png_voidcast(png_bytep, png_malloc_base(png_ptr, new_size));

      if (buffer != NULL)
      {
         png_ptr->read_buffer = buffer;
         png_ptr->read_buffer_size = new_size;
      }

      else if (warn < 2) /* else silent */
      {
         if (warn != 0)
            png_chunk_warning(png_ptr, "insufficient memory to read chunk");

         else
            png_chunk_error(png_ptr, "insufficient memory to read chunk");
      }
   }

   return buffer;
}
#endif /* READ_iCCP|iTXt|pCAL|sCAL|sPLT|tEXt|zTXt|SEQUENTIAL_READ */

/* png_inflate_claim: claim the zstream for some nefarious purpose that involves
 * decompression.  Returns Z_OK on success, else a zlib error code.  It checks
 * the owner but, in final release builds, just issues a warning if some other
 * chunk apparently owns the stream.  Prior to release it does a png_error.
 */
static int
png_inflate_claim(png_structrp png_ptr, png_uint_32 owner)
{
   if (png_ptr->zowner != 0)
   {
      char msg[64];

      PNG_STRING_FROM_CHUNK(msg, png_ptr->zowner);
      /* So the message that results is "<chunk> using zstream"; this is an
       * internal error, but is very useful for debugging.  i18n requirements
       * are minimal.
       */
      (void)png_safecat(msg, (sizeof msg), 4, " using zstream");
#if PNG_RELEASE_BUILD
      png_chunk_warning(png_ptr, msg);
      png_ptr->zowner = 0;
#else
      png_chunk_error(png_ptr, msg);
#endif
   }

   /* Implementation note: unlike 'png_deflate_claim' this internal function
    * does not take the size of the data as an argument.  Some efficiency could
    * be gained by using this when it is known *if* the zlib stream itself does
    * not record the number; however, this is an illusion: the original writer
    * of the PNG may have selected a lower window size, and we really must
    * follow that because, for systems with with limited capabilities, we
    * would otherwise reject the application's attempts to use a smaller window
    * size (zlib doesn't have an interface to say "this or lower"!).
    *
    * inflateReset2 was added to zlib 1.2.4; before this the window could not be
    * reset, therefore it is necessary to always allocate the maximum window
    * size with earlier zlibs just in case later compressed chunks need it.
    */
   {
      int ret; /* zlib return code */
#if ZLIB_VERNUM >= 0x1240
      int window_bits = 0;

# if defined(PNG_SET_OPTION_SUPPORTED) && \
  defined(PNG_MAXIMUM_INFLATE_WINDOW)

            if (png_ptr->maximum_inflate_window)
               window_bits = 15;

# endif
#endif /* ZLIB_VERNUM >= 0x1240 */

      /* Initialize the alloc/free callbacks every time: */
      png_ptr->zstream.zalloc = png_zalloc;
      png_ptr->zstream.zfree = png_zfree;
      png_ptr->zstream.opaque = png_ptr;

      /* Set this for safety, just in case the previous owner left pointers to
       * memory allocations.
       */
      png_ptr->zstream.next_in = NULL;
      png_ptr->zstream.avail_in = 0;
      png_ptr->zstream.next_out = NULL;
      png_ptr->zstream.avail_out = 0;

      /* If png_struct::zstream has been used before for decompression it does
       * not need to be re-initialized, just reset.
       */
      if (png_ptr->zstream.state != NULL)
      {
#if ZLIB_VERNUM >= 0x1240
         ret = inflateReset2(&png_ptr->zstream, window_bits);
#else
         ret = inflateReset(&png_ptr->zstream);
#endif
      }

      else
      {
#if ZLIB_VERNUM >= 0x1240
         ret = inflateInit2(&png_ptr->zstream, window_bits);
#else
         ret = inflateInit(&png_ptr->zstream);
#endif
      }

#if ZLIB_VERNUM >= 0x1240
      /* Turn off validation of the ADLER32 checksum */
      if (png_ptr->current_crc == crc_quiet_use)
         ret = inflateReset2(&png_ptr->zstream, -window_bits);
#endif

      if (ret == Z_OK && png_ptr->zstream.state != NULL)
      {
         png_ptr->zowner = owner;
         png_ptr->zstream_ended = 0;
      }

      else
      {
         png_zstream_error(&png_ptr->zstream, ret);
         png_ptr->zstream_ended = 1;
      }

      return ret;
   }

#  ifdef window_bits
#     undef window_bits
#  endif
}

/* This is a wrapper for the zlib deflate call which will handle larger buffer
 * sizes than uInt.  The input is limited to png_uint_32, because invariably
 * the input comes from a chunk which has a 31-bit length, the output can be
 * anything that fits in a png_alloc_size_t.
 *
 * This internal function sets png_struct::zstream_ended when the end of the
 * decoded data has been encountered; this includes both a normal end and
 * error conditions.
 */
static int
png_zlib_inflate(png_structrp png_ptr, png_uint_32 owner, int finish,
    /* INPUT: */ png_const_bytep *next_in_ptr, png_uint_32p avail_in_ptr,
    /* OUTPUT: */ png_bytep *next_out_ptr, png_alloc_size_t *avail_out_ptr)
{
   if (png_ptr->zowner == owner) /* Else not claimed */
   {
      int ret;
      png_alloc_size_t avail_out = *avail_out_ptr;
      png_uint_32 avail_in = *avail_in_ptr;
      png_bytep output = *next_out_ptr;
      png_const_bytep input = *next_in_ptr;

      /* zlib can't necessarily handle more than 65535 bytes at once (i.e. it
       * can't even necessarily handle 65536 bytes) because the type uInt is
       * "16 bits or more".  Consequently it is necessary to chunk the input to
       * zlib.  This code uses ZLIB_IO_MAX, from pngpriv.h, as the maximum (the
       * maximum value that can be stored in a uInt.)  It is possible to set
       * ZLIB_IO_MAX to a lower value in pngpriv.h and this may sometimes have
       * a performance advantage, because it reduces the amount of data accessed
       * at each step and that may give the OS more time to page it in.
       */
      png_ptr->zstream.next_in = PNGZ_INPUT_CAST(input);
      /* avail_in and avail_out are set below from 'size' */
      png_ptr->zstream.avail_in = 0;
      png_ptr->zstream.avail_out = 0;

      /* Read directly into the output if it is available (this is set to
       * a local buffer below if output is NULL).
       */
      if (output != NULL)
         png_ptr->zstream.next_out = output;

      do
      {
         uInt avail;
         Byte local_buffer[PNG_INFLATE_BUF_SIZE];

         /* zlib INPUT BUFFER */
         /* The setting of 'avail_in' used to be outside the loop; by setting it
          * inside it is possible to chunk the input to zlib and simply rely on
          * zlib to advance the 'next_in' pointer.  This allows arbitrary
          * amounts of data to be passed through zlib at the unavoidable cost of
          * requiring a window save (memcpy of up to 32768 output bytes)
          * every ZLIB_IO_MAX input bytes.
          */
         avail_in += png_ptr->zstream.avail_in; /* not consumed last time */
         avail = ZLIB_IO_MAX;

         if (avail_in < avail)
            avail = (uInt)avail_in; /* safe: < than ZLIB_IO_MAX */

         avail_in -= avail;
         png_ptr->zstream.avail_in = avail;

         /* zlib OUTPUT BUFFER */
         avail_out += png_ptr->zstream.avail_out; /* not written last time */
         avail = ZLIB_IO_MAX; /* maximum zlib can process */

         if (output == NULL)
         {
            /* Reset the output buffer each time round if output is NULL and
             * make available the full buffer, up to 'remaining_space'
             */
            png_ptr->zstream.next_out = local_buffer;
            if ((sizeof local_buffer) < avail)
               avail = (sizeof local_buffer);
         }

         if (avail_out < avail)
            avail = (uInt)avail_out; /* safe: < ZLIB_IO_MAX */

         png_ptr->zstream.avail_out = avail;
         avail_out -= avail;

         /* zlib inflate call */
         /* In fact 'avail_out' may be 0 at this point, that happens at the end
          * of the read when the final LZ end code was not passed at the end of
          * the previous chunk of input data.  Tell zlib if we have reached the
          * end of the output buffer.
          */
         ret = inflate(&png_ptr->zstream, avail_out > 0 ? Z_NO_FLUSH :
             (finish ? Z_FINISH : Z_SYNC_FLUSH));
      } while (ret == Z_OK);

      /* For safety kill the local buffer pointer now */
      if (output == NULL)
         png_ptr->zstream.next_out = NULL;

      /* Claw back the 'size' and 'remaining_space' byte counts. */
      avail_in += png_ptr->zstream.avail_in;
      avail_out += png_ptr->zstream.avail_out;

      /* Update the input and output sizes; the updated values are the amount
       * consumed or written, effectively the inverse of what zlib uses.
       */
      *avail_out_ptr = avail_out;
      if (output != NULL)
         *next_out_ptr = png_ptr->zstream.next_out;

      *avail_in_ptr = avail_in;
      *next_in_ptr = png_ptr->zstream.next_in;

      /* Ensure png_ptr->zstream.msg is set, ret can't be Z_OK at this point.
       */
      debug(ret != Z_OK);

      if (ret != Z_BUF_ERROR)
         png_ptr->zstream_ended = 1;

      png_zstream_error(&png_ptr->zstream, ret);
      return ret;
   }

   else
   {
      /* This is a bad internal error.  The recovery assigns to the zstream msg
       * pointer, which is not owned by the caller, but this is safe; it's only
       * used on errors!  (The {next,avail}_{in,out} values are not changed.)
       */
      png_ptr->zstream.msg = PNGZ_MSG_CAST("zstream unclaimed");
      return Z_STREAM_ERROR;
   }
}

#ifdef PNG_READ_COMPRESSED_TEXT_SUPPORTED
/* png_inflate now returns zlib error codes including Z_OK and Z_STREAM_END to
 * allow the caller to do multiple calls if required.  If the 'finish' flag is
 * set Z_FINISH will be passed to the final inflate() call and Z_STREAM_END must
 * be returned or there has been a problem, otherwise Z_SYNC_FLUSH is used and
 * Z_OK or Z_STREAM_END will be returned on success.
 *
 * The input and output sizes are updated to the actual amounts of data consumed
 * or written, not the amount available (as in a z_stream).  The data pointers
 * are not changed, so the next input is (data+input_size) and the next
 * available output is (output+output_size).
 */
static int
png_inflate(png_structrp png_ptr, png_uint_32 owner, int finish,
    /* INPUT: */ png_const_bytep input, png_uint_32p input_size_ptr,
    /* OUTPUT: */ png_bytep output, png_alloc_size_t *output_size_ptr)
{
   png_uint_32 avail_in = *input_size_ptr;
   png_alloc_size_t avail_out = *output_size_ptr;
   int ret = png_zlib_inflate(png_ptr, owner, finish,
       &input, &avail_in, &output, &avail_out);

   /* And implement the non-zlib semantics (the size values are updated to the
    * amounts consumed and written, not the amount remaining.)
    */
   *input_size_ptr -= avail_in;
   *output_size_ptr -= avail_out;
   return ret;
}

/* Decompress trailing data in a chunk.  The assumption is that read_buffer
 * points at an allocated area holding the contents of a chunk with a
 * trailing compressed part.  What we get back is an allocated area
 * holding the original prefix part and an uncompressed version of the
 * trailing part (the malloc area passed in is freed).
 */
static int
png_decompress_chunk(png_structrp png_ptr,
    png_uint_32 chunklength, png_uint_32 prefix_size,
    png_alloc_size_t *newlength /* must be initialized to the maximum! */,
    int terminate /*add a '\0' to the end of the uncompressed data*/)
{
   /* TODO: implement different limits for different types of chunk.
    *
    * The caller supplies *newlength set to the maximum length of the
    * uncompressed data, but this routine allocates space for the prefix and
    * maybe a '\0' terminator too.  We have to assume that 'prefix_size' is
    * limited only by the maximum chunk size.
    */
   png_alloc_size_t limit = PNG_SIZE_MAX;

#ifdef PNG_SET_USER_LIMITS_SUPPORTED
   if (png_ptr->user_chunk_malloc_max > 0 &&
      png_ptr->user_chunk_malloc_max < limit)
      limit = png_ptr->user_chunk_malloc_max;
#elif PNG_USER_CHUNK_MALLOC_MAX > 0
   if (PNG_USER_CHUNK_MALLOC_MAX < limit)
      limit = PNG_USER_CHUNK_MALLOC_MAX;
#endif

   if (limit >= prefix_size + (terminate != 0))
   {
      int ret;

      limit -= prefix_size + (terminate != 0);

      if (limit < *newlength)
         *newlength = limit;

      /* Now try to claim the stream. */
      ret = png_inflate_claim(png_ptr, png_ptr->chunk_name);

      if (ret == Z_OK)
      {
         png_uint_32 lzsize = chunklength - prefix_size;

         ret = png_inflate(png_ptr, png_ptr->chunk_name, 1/*finish*/,
             /* input: */ png_ptr->read_buffer + prefix_size, &lzsize,
             /* output: */ NULL, newlength);

         if (ret == Z_STREAM_END)
         {
            /* Use 'inflateReset' here, not 'inflateReset2' because this
             * preserves the previously decided window size (otherwise it would
             * be necessary to store the previous window size.)  In practice
             * this doesn't matter anyway, because png_inflate will call inflate
             * with Z_FINISH in almost all cases, so the window will not be
             * maintained.
             */
            if (inflateReset(&png_ptr->zstream) == Z_OK)
            {
               /* Because of the limit checks above we know that the new,
                * expanded, size will fit in a size_t (let alone an
                * png_alloc_size_t).  Use png_malloc_base here to avoid an
                * extra OOM message.
                */
               png_alloc_size_t new_size = *newlength;
               png_alloc_size_t buffer_size = prefix_size + new_size +
                   (terminate != 0);
               png_bytep text = png_voidcast(png_bytep, png_malloc_base(png_ptr,
                   buffer_size));

               if (text != NULL)
               {
                  ret = png_inflate(png_ptr, png_ptr->chunk_name, 1/*finish*/,
                      png_ptr->read_buffer + prefix_size, &lzsize,
                      text + prefix_size, newlength);

                  if (ret == Z_STREAM_END)
                  {
                     if (new_size == *newlength)
                     {
                        if (terminate != 0)
                           text[prefix_size + *newlength] = 0;

                        if (prefix_size > 0)
                           memcpy(text, png_ptr->read_buffer, prefix_size);

                        {
                           png_bytep old_ptr = png_ptr->read_buffer;

                           png_ptr->read_buffer = text;
                           png_ptr->read_buffer_size = buffer_size;
                           text = old_ptr; /* freed below */
                        }
                     }

                     else
                     {
                        /* The size changed on the second read, there can be no
                         * guarantee that anything is correct at this point.
                         * The 'msg' pointer has been set to "unexpected end of
                         * LZ stream", which is fine, but return an error code
                         * that the caller won't accept.
                         */
                        ret = PNG_UNEXPECTED_ZLIB_RETURN;
                     }
                  }

                  else if (ret == Z_OK)
                     ret = PNG_UNEXPECTED_ZLIB_RETURN; /* for safety */

                  /* Free the text pointer (this is the old read_buffer on
                   * success)
                   */
                  png_free(png_ptr, text);

                  /* This really is very benign, but it's still an error because
                   * the extra space may otherwise be used as a Trojan Horse.
                   */
                  if (ret == Z_STREAM_END &&
                      chunklength - prefix_size != lzsize)
                     png_chunk_benign_error(png_ptr, "extra compressed data");
               }

               else
               {
                  /* Out of memory allocating the buffer */
                  ret = Z_MEM_ERROR;
                  png_zstream_error(&png_ptr->zstream, Z_MEM_ERROR);
               }
            }

            else
            {
               /* inflateReset failed, store the error message */
               png_zstream_error(&png_ptr->zstream, ret);

               if (ret == Z_STREAM_END)
                  ret = PNG_UNEXPECTED_ZLIB_RETURN;
            }
         }

         else if (ret == Z_OK)
            ret = PNG_UNEXPECTED_ZLIB_RETURN;

         /* Release the claimed stream */
         png_ptr->zowner = 0;
      }

      else /* the claim failed */ if (ret == Z_STREAM_END) /* impossible! */
         ret = PNG_UNEXPECTED_ZLIB_RETURN;

      return ret;
   }

   else
   {
      /* Application/configuration limits exceeded */
      png_zstream_error(&png_ptr->zstream, Z_MEM_ERROR);
      return Z_MEM_ERROR;
   }
}
#endif /* READ_COMPRESSED_TEXT */

#ifdef PNG_READ_iCCP_SUPPORTED
/* Perform a partial read and decompress, producing 'avail_out' bytes and
 * reading from the current chunk as required.
 */
static int
png_inflate_read(png_structrp png_ptr, png_bytep read_buffer, uInt read_size,
    png_uint_32p chunk_bytes, png_bytep next_out, png_alloc_size_t *out_size,
    int finish)
{
   if (png_ptr->zowner == png_ptr->chunk_name)
   {
      int ret;

      /* next_in and avail_in must have been initialized by the caller. */
      png_ptr->zstream.next_out = next_out;
      png_ptr->zstream.avail_out = 0; /* set in the loop */

      do
      {
         if (png_ptr->zstream.avail_in == 0)
         {
            if (read_size > *chunk_bytes)
               read_size = (uInt)*chunk_bytes;
            *chunk_bytes -= read_size;

            if (read_size > 0)
               png_crc_read(png_ptr, read_buffer, read_size);

            png_ptr->zstream.next_in = read_buffer;
            png_ptr->zstream.avail_in = read_size;
         }

         if (png_ptr->zstream.avail_out == 0)
         {
            uInt avail = ZLIB_IO_MAX;
            if (avail > *out_size)
               avail = (uInt)*out_size;
            *out_size -= avail;

            png_ptr->zstream.avail_out = avail;
         }

         /* Use Z_SYNC_FLUSH when there is no more chunk data to ensure that all
          * the available output is produced; this allows reading of truncated
          * streams.
          */
         ret = inflate(&png_ptr->zstream,
             *chunk_bytes > 0 ? Z_NO_FLUSH : (finish ? Z_FINISH :
             Z_SYNC_FLUSH));
      }
      while (ret == Z_OK && (*out_size > 0 || png_ptr->zstream.avail_out > 0));

      *out_size += png_ptr->zstream.avail_out;
      png_ptr->zstream.avail_out = 0; /* Should not be required, but is safe */

      /* Ensure the error message pointer is always set: */
      png_zstream_error(&png_ptr->zstream, ret);
      return ret;
   }

   else
   {
      png_ptr->zstream.msg = PNGZ_MSG_CAST("zstream unclaimed");
      return Z_STREAM_ERROR;
   }
}
#endif /* READ_iCCP */

/* Chunk handling error handlers and utilities: */
/* Utility to read the chunk data from the start without processing it;
 * a skip function.
 */
static void
png_handle_skip(png_structrp png_ptr)
   /* Skip the entire chunk after the name,length header has been read: */
{
   png_crc_finish(png_ptr, png_ptr->chunk_length);
}

static void
png_handle_error(png_structrp png_ptr
#  ifdef PNG_ERROR_TEXT_SUPPORTED
    , png_const_charp error
#  else
#     define png_handle_error(pp,e) png_handle_error(pp)
#  endif
   )
   /* Handle an error detected immediately after the chunk header has been
    * read; this skips the rest of the chunk data and the CRC then signals
    * a *benign* chunk error.
    */
{
   png_handle_skip(png_ptr);
   png_chunk_benign_error(png_ptr, error);
}

#if defined (PNG_READ_gAMA_SUPPORTED) || defined (PNG_READ_sBIT_SUPPORTED) ||\
   defined (PNG_READ_cHRM_SUPPORTED) || defined (PNG_READ_sRGB_SUPPORTED) ||\
   defined (PNG_READ_iCCP_SUPPORTED) || defined (PNG_READ_tRNS_SUPPORTED) ||\
   defined (PNG_READ_bKGD_SUPPORTED) || defined (PNG_READ_hIST_SUPPORTED) ||\
   defined (PNG_READ_pHYs_SUPPORTED) || defined (PNG_READ_oFFs_SUPPORTED) ||\
   defined (PNG_READ_sCAL_SUPPORTED) || defined (PNG_READ_tIME_SUPPORTED)
static void
png_handle_bad_length(png_structrp png_ptr)
{
   png_handle_error(png_ptr, "invalid length");
}
#endif /* chunks that can generate length errors */

/* Read and check the IDHR chunk */
static void
png_handle_IHDR(png_structrp png_ptr, png_inforp info_ptr)
{
   png_byte buf[13];
   png_uint_32 width, height;
   png_byte bit_depth, color_type, compression_type, filter_method;
   png_byte interlace_type;

   png_debug(1, "in png_handle_IHDR");

   /* Check the length (this is a chunk error; not benign) */
   if (png_ptr->chunk_length != 13)
      png_chunk_error(png_ptr, "invalid length");

   png_crc_read(png_ptr, buf, 13);
   png_crc_finish(png_ptr, 0);

   width = png_get_uint_31(png_ptr, buf);
   height = png_get_uint_31(png_ptr, buf + 4);
   bit_depth = buf[8];
   color_type = buf[9];
   compression_type = buf[10];
   filter_method = buf[11];
   interlace_type = buf[12];

   /* Set internal variables */
   png_ptr->width = width;
   png_ptr->height = height;
   png_ptr->bit_depth = bit_depth;
   png_ptr->interlaced = interlace_type;
   png_ptr->color_type = color_type;
   png_ptr->filter_method = filter_method;

   png_set_IHDR(png_ptr, info_ptr, width, height, bit_depth,
       color_type, interlace_type, compression_type, filter_method);
}

/* Read and check the palette */
static void
png_handle_PLTE(png_structrp png_ptr, png_inforp info_ptr)
{
   png_color palette[PNG_MAX_PALETTE_LENGTH];
   png_uint_32 length = png_ptr->chunk_length;
   png_uint_32 max_palette_length, num, i;

   png_debug(1, "in png_handle_PLTE");

   if (info_ptr == NULL)
      return;

   if (!(png_ptr->color_type & PNG_COLOR_MASK_COLOR))
   {
      png_handle_error(png_ptr, "ignored in grayscale PNG");
      return;
   }

#ifndef PNG_READ_OPT_PLTE_SUPPORTED
   if (png_ptr->color_type != PNG_COLOR_TYPE_PALETTE)
   {
      /* Skip the whole chunk: */
      png_handle_skip(png_ptr);
      return;
   }
#endif

   if (length > 3*PNG_MAX_PALETTE_LENGTH || length % 3)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_report(png_ptr, "invalid length",
         ((png_ptr->color_type != PNG_COLOR_TYPE_PALETTE) ? PNG_CHUNK_ERROR :
            PNG_CHUNK_FATAL));
      return;
   }

   /* The cast is safe because 'length' is less than 3*PNG_MAX_PALETTE_LENGTH */
   num = length / 3U;

   /* If the palette has 256 or fewer entries but is too large for the bit
    * depth, we don't issue an error, to preserve the behavior of previous
    * libpng versions. We silently truncate the unused extra palette entries
    * here.
    */
   if (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
      max_palette_length = (1U << png_ptr->bit_depth);
   else
      max_palette_length = PNG_MAX_PALETTE_LENGTH;

   if (num > max_palette_length)
      num = max_palette_length;

   for (i = 0; i < num; ++i)
   {
      png_byte buf[3];

      png_crc_read(png_ptr, buf, 3);
      palette[i].red = buf[0];
      palette[i].green = buf[1];
      palette[i].blue = buf[2];
   }

   png_crc_finish(png_ptr, length - num * 3U);
   png_set_PLTE(png_ptr, info_ptr, palette, num);

   /* Ok, make our own copy since the set succeeded: */
   debug(png_ptr->palette == NULL); /* should only get set once */
   png_ptr->palette = png_voidcast(png_colorp, png_malloc(png_ptr,
         sizeof (png_color[PNG_MAX_PALETTE_LENGTH])));
   /* This works because we know png_set_PLTE also expands the palette to the
    * full size:
    */
   memcpy(png_ptr->palette, info_ptr->palette,
      sizeof (png_color[PNG_MAX_PALETTE_LENGTH]));
   png_ptr->num_palette = info_ptr->num_palette;

   /* The three chunks, bKGD, hIST and tRNS *must* appear after PLTE and before
    * IDAT.  Prior to 1.6.0 this was not checked; instead the code merely
    * checked the apparent validity of a tRNS chunk inserted before PLTE on a
    * palette PNG.  1.6.0 attempts to rigorously follow the standard and
    * therefore does a benign error if the erroneous condition is detected *and*
    * cancels the tRNS if the benign error returns.  The alternative is to
    * amend the standard since it would be rather hypocritical of the standards
    * maintainers to ignore it.
    */
#ifdef PNG_READ_tRNS_SUPPORTED
   if (png_ptr->num_trans > 0 ||
       (info_ptr->valid & PNG_INFO_tRNS) != 0)
   {
      /* Cancel this because otherwise it would be used if the transforms
       * require it.  Don't cancel the 'valid' flag because this would prevent
       * detection of duplicate chunks.
       */
      png_ptr->num_trans = 0;
      info_ptr->num_trans = 0;

      png_chunk_benign_error(png_ptr, "tRNS must be after");
   }
#endif /* READ_tRNS */

#ifdef PNG_READ_hIST_SUPPORTED
   if ((info_ptr->valid & PNG_INFO_hIST) != 0)
      png_chunk_benign_error(png_ptr, "hIST must be after");
#endif /* READ_hIST */

#ifdef PNG_READ_bKGD_SUPPORTED
   if ((info_ptr->valid & PNG_INFO_bKGD) != 0)
      png_chunk_benign_error(png_ptr, "bKGD must be after");
#endif /* READ_bKGD */
}

static void
png_handle_IEND(png_structrp png_ptr, png_inforp info_ptr)
{
   png_debug(1, "in png_handle_IEND");

   png_crc_finish(png_ptr, png_ptr->chunk_length);

   /* Treat this as benign and terminate the PNG anyway: */
   if (png_ptr->chunk_length != 0)
      png_chunk_benign_error(png_ptr, "invalid length");

   PNG_UNUSED(info_ptr)
}

#ifdef PNG_READ_gAMA_SUPPORTED
static void
png_handle_gAMA(png_structrp png_ptr, png_inforp info_ptr)
{
   png_fixed_point igamma;
   png_byte buf[4];

   png_debug(1, "in png_handle_gAMA");

   if (png_ptr->chunk_length != 4)
   {
      png_handle_bad_length(png_ptr);
      return;
   }

   png_crc_read(png_ptr, buf, 4);

   if (png_crc_finish(png_ptr, 0))
      return;

   igamma = png_get_fixed_point(NULL, buf);

   png_colorspace_set_gamma(png_ptr, &png_ptr->colorspace, igamma);
   png_colorspace_sync(png_ptr, info_ptr);
}
#else
#  define png_handle_gAMA NULL
#endif /* READ_gAMA */

#ifdef PNG_READ_sBIT_SUPPORTED
static void
png_handle_sBIT(png_structrp png_ptr, png_inforp info_ptr)
{
   unsigned int truelen, i;
   png_byte sample_depth;
   png_byte buf[4];

   png_debug(1, "in png_handle_sBIT");

   if (info_ptr != NULL && (info_ptr->valid & PNG_INFO_sBIT))
   {
      png_handle_error(png_ptr, "duplicate");
      return;
   }

   if (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
   {
      truelen = 3;
      sample_depth = 8;
   }

   else
   {
      truelen = PNG_CHANNELS(*png_ptr);
      sample_depth = png_ptr->bit_depth;
      affirm(truelen <= 4);
   }

   if (png_ptr->chunk_length != truelen)
   {
      png_handle_bad_length(png_ptr);
      return;
   }

   buf[0] = buf[1] = buf[2] = buf[3] = sample_depth;
   png_crc_read(png_ptr, buf, truelen);

   if (png_crc_finish(png_ptr, 0))
      return;

   for (i=0; i<truelen; ++i)
      if (buf[i] == 0 || buf[i] > sample_depth)
      {
         png_chunk_benign_error(png_ptr, "invalid");
         return;
      }

   if (png_ptr->color_type & PNG_COLOR_MASK_COLOR)
   {
      png_ptr->sig_bit.red = buf[0];
      png_ptr->sig_bit.green = buf[1];
      png_ptr->sig_bit.blue = buf[2];
      png_ptr->sig_bit.alpha = buf[3];
   }

   else
   {
      png_ptr->sig_bit.gray = buf[0];
      png_ptr->sig_bit.red = buf[0];
      png_ptr->sig_bit.green = buf[0];
      png_ptr->sig_bit.blue = buf[0];
      png_ptr->sig_bit.alpha = buf[1];
   }

   png_set_sBIT(png_ptr, info_ptr, &(png_ptr->sig_bit));
}
#else
#  define png_handle_sBIT NULL
#endif /* READ_sBIT */

#ifdef PNG_READ_cHRM_SUPPORTED
static void
png_handle_cHRM(png_structrp png_ptr, png_inforp info_ptr)
{
   png_byte buf[32];
   png_xy xy;

   png_debug(1, "in png_handle_cHRM");

   if (png_ptr->chunk_length != 32)
   {
      png_handle_bad_length(png_ptr);
      return;
   }

   png_crc_read(png_ptr, buf, 32);

   if (png_crc_finish(png_ptr, 0))
      return;

   xy.whitex = png_get_fixed_point(NULL, buf);
   xy.whitey = png_get_fixed_point(NULL, buf + 4);
   xy.redx   = png_get_fixed_point(NULL, buf + 8);
   xy.redy   = png_get_fixed_point(NULL, buf + 12);
   xy.greenx = png_get_fixed_point(NULL, buf + 16);
   xy.greeny = png_get_fixed_point(NULL, buf + 20);
   xy.bluex  = png_get_fixed_point(NULL, buf + 24);
   xy.bluey  = png_get_fixed_point(NULL, buf + 28);

   if (xy.whitex == PNG_FIXED_ERROR ||
       xy.whitey == PNG_FIXED_ERROR ||
       xy.redx   == PNG_FIXED_ERROR ||
       xy.redy   == PNG_FIXED_ERROR ||
       xy.greenx == PNG_FIXED_ERROR ||
       xy.greeny == PNG_FIXED_ERROR ||
       xy.bluex  == PNG_FIXED_ERROR ||
       xy.bluey  == PNG_FIXED_ERROR)
   {
      png_chunk_benign_error(png_ptr, "invalid");
      return;
   }

   /* If a colorspace error has already been output skip this chunk */
   if (png_ptr->colorspace.flags & PNG_COLORSPACE_INVALID)
      return;

   if (png_ptr->colorspace.flags & PNG_COLORSPACE_FROM_cHRM)
   {
      png_ptr->colorspace.flags |= PNG_COLORSPACE_INVALID;
      png_colorspace_sync(png_ptr, info_ptr);
      png_chunk_benign_error(png_ptr, "duplicate");
      return;
   }

   png_ptr->colorspace.flags |= PNG_COLORSPACE_FROM_cHRM;
   (void)png_colorspace_set_chromaticities(png_ptr, &png_ptr->colorspace, &xy,
       1/*prefer cHRM values*/);
   png_colorspace_sync(png_ptr, info_ptr);
}
#else
#  define png_handle_cHRM NULL
#endif /* READ_cHRM */

#ifdef PNG_READ_sRGB_SUPPORTED
static void
png_handle_sRGB(png_structrp png_ptr, png_inforp info_ptr)
{
   png_byte intent;

   png_debug(1, "in png_handle_sRGB");

   if (png_ptr->chunk_length != 1)
   {
      png_handle_bad_length(png_ptr);
      return;
   }

   png_crc_read(png_ptr, &intent, 1);

   if (png_crc_finish(png_ptr, 0))
      return;

   /* If a colorspace error has already been output skip this chunk */
   if (png_ptr->colorspace.flags & PNG_COLORSPACE_INVALID)
      return;

   /* Only one sRGB or iCCP chunk is allowed, use the HAVE_INTENT flag to detect
    * this.
    */
   if (png_ptr->colorspace.flags & PNG_COLORSPACE_HAVE_INTENT)
   {
      png_ptr->colorspace.flags |= PNG_COLORSPACE_INVALID;
      png_colorspace_sync(png_ptr, info_ptr);
      png_chunk_benign_error(png_ptr, "too many profiles");
      return;
   }

   (void)png_colorspace_set_sRGB(png_ptr, &png_ptr->colorspace, intent);
   png_colorspace_sync(png_ptr, info_ptr);
}
#else
#  define png_handle_sRGB NULL
#endif /* READ_sRGB */

#ifdef PNG_READ_iCCP_SUPPORTED
static void
png_handle_iCCP(png_structrp png_ptr, png_inforp info_ptr)
/* Note: this does not properly handle profiles that are > 64K under DOS */
{
   png_const_charp errmsg = NULL; /* error message output, or no error */
   png_uint_32 length = png_ptr->chunk_length;
   int finished = 0; /* crc checked */

   png_debug(1, "in png_handle_iCCP");

   /* Consistent with all the above colorspace handling an obviously *invalid*
    * chunk is just ignored, so does not invalidate the color space.  An
    * alternative is to set the 'invalid' flags at the start of this routine
    * and only clear them in they were not set before and all the tests pass.
    * The minimum 'deflate' stream is assumed to be just the 2 byte header and
    * 4 byte checksum.  The keyword must be at least one character and there is
    * a terminator (0) byte and the compression method.
    */
   if (length < 9)
   {
      png_handle_bad_length(png_ptr);
      return;
   }

   /* If a colorspace error has already been output skip this chunk */
   if ((png_ptr->colorspace.flags & PNG_COLORSPACE_INVALID) != 0)
   {
      png_crc_finish(png_ptr, length);
      return;
   }

   /* Only one sRGB or iCCP chunk is allowed, use the HAVE_INTENT flag to detect
    * this.
    */
   if ((png_ptr->colorspace.flags & PNG_COLORSPACE_HAVE_INTENT) == 0)
   {
      uInt read_length, keyword_length;
      char keyword[81];

      /* Find the keyword; the keyword plus separator and compression method
       * bytes can be at most 81 characters long.
       */
      read_length = 81; /* maximum */
      if (read_length > length)
         read_length = (uInt)/*SAFE*/length;

      png_crc_read(png_ptr, (png_bytep)keyword, read_length);
      length -= read_length;

      keyword_length = 0;
      while (keyword_length < 80 && keyword_length < read_length &&
         keyword[keyword_length] != 0)
         ++keyword_length;

      /* TODO: make the keyword checking common */
      if (keyword_length >= 1 && keyword_length <= 79)
      {
         /* We only understand '0' compression - deflate - so if we get a
          * different value we can't safely decode the chunk.
          */
         if (keyword_length+1 < read_length &&
            keyword[keyword_length+1] == PNG_COMPRESSION_TYPE_BASE)
         {
            read_length -= keyword_length+2;

            if (png_inflate_claim(png_ptr, png_iCCP) == Z_OK)
            {
               Byte profile_header[132];
               Byte local_buffer[PNG_INFLATE_BUF_SIZE];
               png_alloc_size_t size = (sizeof profile_header);

               png_ptr->zstream.next_in = (Bytef*)keyword + (keyword_length+2);
               png_ptr->zstream.avail_in = read_length;
               (void)png_inflate_read(png_ptr, local_buffer,
                   (sizeof local_buffer), &length, profile_header, &size,
                   0/*finish: don't, because the output is too small*/);

               if (size == 0)
               {
                  /* We have the ICC profile header; do the basic header checks.
                   */
                  const png_uint_32 profile_length =
                     png_get_uint_32(profile_header);

                  if (png_icc_check_length(png_ptr, &png_ptr->colorspace,
                      keyword, profile_length))
                  {
                     /* The length is apparently ok, so we can check the 132
                      * byte header.
                      */
                     if (png_icc_check_header(png_ptr, &png_ptr->colorspace,
                         keyword, profile_length, profile_header,
                         (png_ptr->color_type & PNG_COLOR_MASK_COLOR) != 0))
                     {
                        /* Now read the tag table; a variable size buffer is
                         * needed at this point, allocate one for the whole
                         * profile.  The header check has already validated
                         * that none of these stuff will overflow.
                         */
                        const png_uint_32 tag_count = png_get_uint_32(
                            profile_header+128);
                        png_bytep profile = png_read_buffer(png_ptr,
                            profile_length, 2/*silent*/);

                        if (profile != NULL)
                        {
                           memcpy(profile, profile_header,
                               (sizeof profile_header));

                           size = 12 * tag_count;

                           (void)png_inflate_read(png_ptr, local_buffer,
                               (sizeof local_buffer), &length,
                               profile + (sizeof profile_header), &size, 0);

                           /* Still expect a buffer error because we expect
                            * there to be some tag data!
                            */
                           if (size == 0)
                           {
                              if (png_icc_check_tag_table(png_ptr,
                                  &png_ptr->colorspace, keyword, profile_length,
                                  profile))
                              {
                                 /* The profile has been validated for basic
                                  * security issues, so read the whole thing in.
                                  */
                                 size = profile_length - (sizeof profile_header)
                                     - 12 * tag_count;

                                 (void)png_inflate_read(png_ptr, local_buffer,
                                     (sizeof local_buffer), &length,
                                     profile + (sizeof profile_header) +
                                     12 * tag_count, &size, 1/*finish*/);

                                 if (length > 0
#                                    ifdef PNG_BENIGN_READ_ERRORS_SUPPORTED
                                     && png_ptr->benign_error_action ==
                                     PNG_ERROR
#                                    endif /* BENIGN_READ_ERRORS */
                                      )
                                    errmsg = "extra compressed data";

                                 /* But otherwise allow extra data: */
                                 else if (size == 0)
                                 {
                                    if (length > 0)
                                    {
                                       /* This can be handled completely, so
                                        * keep going.
                                        */
                                       png_chunk_warning(png_ptr,
                                           "extra compressed data");
                                    }

                                    png_crc_finish(png_ptr, length);
                                    finished = 1;

# if defined(PNG_sRGB_SUPPORTED) && PNG_sRGB_PROFILE_CHECKS >= 0
                                       /* Check for a match against sRGB */
                                       png_icc_set_sRGB(png_ptr,
                                           &png_ptr->colorspace, profile,
                                           png_ptr->zstream.adler);
# endif

                                    /* Steal the profile for info_ptr. */
                                    if (info_ptr != NULL)
                                    {
                                       png_free_data(png_ptr, info_ptr,
                                           PNG_FREE_ICCP, 0);

                                       info_ptr->iccp_name = png_voidcast(char*,
                                           png_malloc_base(png_ptr,
                                           keyword_length+1));
                                       if (info_ptr->iccp_name != NULL)
                                       {
                                          memcpy(info_ptr->iccp_name, keyword,
                                              keyword_length+1);
                                          info_ptr->iccp_profile = profile;
                                          png_ptr->read_buffer = NULL; /*steal*/
                                          info_ptr->free_me |= PNG_FREE_ICCP;
                                          info_ptr->valid |= PNG_INFO_iCCP;
                                       }

                                       else
                                       {
                                          png_ptr->colorspace.flags |=
                                              PNG_COLORSPACE_INVALID;
                                          errmsg = "out of memory";
                                       }
                                    }

                                    /* else the profile remains in the read
                                     * buffer which gets reused for subsequent
                                     * chunks.
                                     */

                                    if (info_ptr != NULL)
                                       png_colorspace_sync(png_ptr, info_ptr);

                                    if (errmsg == NULL)
                                    {
                                       png_ptr->zowner = 0;
                                       return;
                                    }
                                 }

                                 else if (size > 0)
                                    errmsg = "truncated";
                              }

                              /* else png_icc_check_tag_table output an error */
                           }

                           else /* profile truncated */
                              errmsg = png_ptr->zstream.msg;
                        }

                        else
                           errmsg = "out of memory";
                     }

                     /* else png_icc_check_header output an error */
                  }

                  /* else png_icc_check_length output an error */
               }

               else /* profile truncated */
                  errmsg = png_ptr->zstream.msg;

               /* Release the stream */
               png_ptr->zowner = 0;
            }

            else /* png_inflate_claim failed */
               errmsg = png_ptr->zstream.msg;
         }

         else
            errmsg = "bad compression method"; /* or missing */
      }

      else
         errmsg = "bad keyword";
   }

   else
      errmsg = "too many profiles";

   /* Failure: the reason is in 'errmsg' */
   if (finished == 0)
      png_crc_finish(png_ptr, length);

   png_ptr->colorspace.flags |= PNG_COLORSPACE_INVALID;
   png_colorspace_sync(png_ptr, info_ptr);
   if (errmsg != NULL) /* else already output */
      png_chunk_benign_error(png_ptr, errmsg);
}
#else
#  define png_handle_iCCP NULL
#endif /* READ_iCCP */

#ifdef PNG_READ_sPLT_SUPPORTED
static void
png_handle_sPLT(png_structrp png_ptr, png_inforp info_ptr)
/* Note: this does not properly handle chunks that are > 64K under DOS */
{
   png_uint_32 length = png_ptr->chunk_length;
   png_bytep entry_start, buffer;
   png_sPLT_t new_palette;
   png_sPLT_entryp pp;
   png_uint_32 data_length;
   int entry_size, i;
   png_uint_32 skip = 0;
   png_uint_32 dl;
   png_size_t max_dl;

   png_debug(1, "in png_handle_sPLT");

#ifdef PNG_USER_LIMITS_SUPPORTED
   if (png_ptr->user_chunk_cache_max != 0)
   {
      if (png_ptr->user_chunk_cache_max == 1)
      {
         png_crc_finish(png_ptr, length);
         return;
      }

      if (--png_ptr->user_chunk_cache_max == 1)
      {
         /* Warn the first time */
         png_chunk_benign_error(png_ptr, "no space in chunk cache");
         png_crc_finish(png_ptr, length);
         return;
      }
   }
#endif /* USER_LIMITS */

   buffer = png_read_buffer(png_ptr, length+1, 2/*silent*/);
   if (buffer == NULL)
   {
      png_crc_finish(png_ptr, length);
      png_chunk_benign_error(png_ptr, "out of memory");
      return;
   }

   /* WARNING: this may break if size_t is less than 32 bits; it is assumed
    * that the PNG_MAX_MALLOC_64K test is enabled in this case, but this is a
    * potential breakage point if the types in pngconf.h aren't exactly right.
    */
   png_crc_read(png_ptr, buffer, length);

   if (png_crc_finish(png_ptr, skip))
      return;

   buffer[length] = 0;

   for (entry_start = buffer; *entry_start; entry_start++)
      /* Empty loop to find end of name */ ;

   ++entry_start;

   /* A sample depth should follow the separator, and we should be on it  */
   if (length < 2U || entry_start > buffer + (length - 2U))
   {
      png_chunk_benign_error(png_ptr, "malformed");
      return;
   }

   new_palette.depth = *entry_start++;
   entry_size = (new_palette.depth == 8 ? 6 : 10);
   /* This must fit in a png_uint_32 because it is derived from the original
    * chunk data length.
    */
   data_length = length - (png_uint_32)(entry_start - buffer);

   /* Integrity-check the data length */
   if (data_length % entry_size)
   {
      png_chunk_benign_error(png_ptr, "invalid length");
      return;
   }

   dl = (png_int_32)(data_length / entry_size);
   max_dl = PNG_SIZE_MAX / (sizeof (png_sPLT_entry));

   if (dl > max_dl)
   {
      png_chunk_benign_error(png_ptr, "exceeds system limits");
      return;
   }

   new_palette.nentries = (png_int_32)(data_length / entry_size);

   new_palette.entries = png_voidcast(png_sPLT_entryp, png_malloc_base(
       png_ptr, new_palette.nentries * (sizeof (png_sPLT_entry))));

   if (new_palette.entries == NULL)
   {
      png_chunk_benign_error(png_ptr, "out of memory");
      return;
   }

   for (i = 0; i < new_palette.nentries; i++)
   {
      pp = new_palette.entries + i;

      if (new_palette.depth == 8)
      {
         pp->red = *entry_start++;
         pp->green = *entry_start++;
         pp->blue = *entry_start++;
         pp->alpha = *entry_start++;
      }

      else
      {
         pp->red   = png_get_uint_16(entry_start); entry_start += 2;
         pp->green = png_get_uint_16(entry_start); entry_start += 2;
         pp->blue  = png_get_uint_16(entry_start); entry_start += 2;
         pp->alpha = png_get_uint_16(entry_start); entry_start += 2;
      }

      pp->frequency = png_get_uint_16(entry_start); entry_start += 2;
   }

   /* Discard all chunk data except the name and stash that */
   new_palette.name = (png_charp)buffer;

   png_set_sPLT(png_ptr, info_ptr, &new_palette, 1);

   png_free(png_ptr, new_palette.entries);
}
#else
#  define png_handle_sPLT NULL
#endif /* READ_sPLT */

#ifdef PNG_READ_tRNS_SUPPORTED
static void
png_handle_tRNS(png_structrp png_ptr, png_inforp info_ptr)
{
   png_uint_32 num_trans;
   png_byte readbuf[PNG_MAX_PALETTE_LENGTH];

   png_debug(1, "in png_handle_tRNS");

   png_ptr->num_trans = 0U; /* safety */

   if (info_ptr != NULL && (info_ptr->valid & PNG_INFO_tRNS))
   {
      /* libpng 1.7.0: this used to be a benign error, but it doesn't look very
       * benign because it has security implications; libpng ignores the second
       * tRNS, so if you can find something that ignores the first instead you
       * can choose which image the user sees depending on the PNG decoder.
       */
      png_crc_finish(png_ptr, png_ptr->chunk_length);
      png_chunk_error(png_ptr, "duplicate");
      return;
   }

   if (png_ptr->color_type == PNG_COLOR_TYPE_GRAY)
   {
      png_byte buf[2];

      if (png_ptr->chunk_length != 2)
      {
         png_handle_bad_length(png_ptr);
         return;
      }

      png_crc_read(png_ptr, buf, 2);
      num_trans = 1U;
      png_ptr->trans_color.gray = png_get_uint_16(buf);
   }

   else if (png_ptr->color_type == PNG_COLOR_TYPE_RGB)
   {
      png_byte buf[6];

      if (png_ptr->chunk_length != 6)
      {
         png_handle_bad_length(png_ptr);
         return;
      }

      png_crc_read(png_ptr, buf, 6);
      num_trans = 1U;
      png_ptr->trans_color.red = png_get_uint_16(buf);
      png_ptr->trans_color.green = png_get_uint_16(buf + 2);
      png_ptr->trans_color.blue = png_get_uint_16(buf + 4);
   }

   else if (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
   {
      /* png_find_chunk_op checks this: */
      debug(png_ptr->mode & PNG_HAVE_PLTE);

      num_trans = png_ptr->chunk_length;

      if (num_trans > png_ptr->num_palette || num_trans == 0)
      {
         png_handle_bad_length(png_ptr);
         return;
      }

      png_crc_read(png_ptr, readbuf, num_trans);
   }

   else
   {
      png_handle_error(png_ptr, "invalid");
      return;
   }

   if (png_crc_finish(png_ptr, 0))
      return;

   /* Set it into the info_struct: */
   png_set_tRNS(png_ptr, info_ptr, readbuf, num_trans, &png_ptr->trans_color);

   /* Now make a copy of the buffer if one is required (palette images). */
   debug(png_ptr->trans_alpha == NULL);
   if (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
   {
      png_ptr->trans_alpha = png_voidcast(png_bytep,
            png_malloc(png_ptr, PNG_MAX_PALETTE_LENGTH));
      memset(png_ptr->trans_alpha, 0xFFU, PNG_MAX_PALETTE_LENGTH);
      memcpy(png_ptr->trans_alpha, info_ptr->trans_alpha, num_trans);
   }

   png_ptr->num_trans = png_check_bits(png_ptr, num_trans, 9);
}
#else
#  define png_handle_tRNS NULL
#endif /* READ_tRNS */

#ifdef PNG_READ_bKGD_SUPPORTED
static void
png_handle_bKGD(png_structrp png_ptr, png_inforp info_ptr)
{
   unsigned int truelen;
   png_byte buf[6];
   png_color_16 background;

   png_debug(1, "in png_handle_bKGD");

   if (info_ptr != NULL && (info_ptr->valid & PNG_INFO_bKGD))
   {
      png_handle_error(png_ptr, "duplicate");
      return;
   }

   if (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
      truelen = 1;

   else if (png_ptr->color_type & PNG_COLOR_MASK_COLOR)
      truelen = 6;

   else
      truelen = 2;

   if (png_ptr->chunk_length != truelen)
   {
      png_handle_bad_length(png_ptr);
      return;
   }

   png_crc_read(png_ptr, buf, truelen);

   if (png_crc_finish(png_ptr, 0))
      return;

   /* We convert the index value into RGB components so that we can allow
    * arbitrary RGB values for background when we have transparency, and
    * so it is easy to determine the RGB values of the background color
    * from the info_ptr struct.
    */
   if (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
   {
      background.index = buf[0];

      if (info_ptr && info_ptr->num_palette)
      {
         if (buf[0] >= info_ptr->num_palette)
         {
            png_chunk_benign_error(png_ptr, "invalid index");
            return;
         }

         background.red = png_check_u16(png_ptr, png_ptr->palette[buf[0]].red);
         background.green =
            png_check_u16(png_ptr, png_ptr->palette[buf[0]].green);
         background.blue =
            png_check_u16(png_ptr, png_ptr->palette[buf[0]].blue);
      }

      else
         background.red = background.green = background.blue = 0;

      background.gray = 0;
   }

   else if (!(png_ptr->color_type & PNG_COLOR_MASK_COLOR)) /* GRAY */
   {
      background.index = 0;
      background.red =
      background.green =
      background.blue =
      background.gray = png_get_uint_16(buf);
   }

   else
   {
      background.index = 0;
      background.red = png_get_uint_16(buf);
      background.green = png_get_uint_16(buf + 2);
      background.blue = png_get_uint_16(buf + 4);
      background.gray = 0;
   }

   png_set_bKGD(png_ptr, info_ptr, &background);
}
#else
#  define png_handle_bKGD NULL
#endif /* READ_bKGD */

#ifdef PNG_READ_hIST_SUPPORTED
static void
png_handle_hIST(png_structrp png_ptr, png_inforp info_ptr)
{
   unsigned int num, i;
   png_uint_16 readbuf[PNG_MAX_PALETTE_LENGTH];

   png_debug(1, "in png_handle_hIST");

   if (info_ptr != NULL && (info_ptr->valid & PNG_INFO_hIST))
   {
      png_handle_error(png_ptr, "duplicate");
      return;
   }

   num = png_ptr->chunk_length / 2;

   if (num != png_ptr->num_palette || 2*num != png_ptr->chunk_length)
   {
      png_handle_bad_length(png_ptr);
      return;
   }

   for (i = 0; i < num; i++)
   {
      png_byte buf[2];

      png_crc_read(png_ptr, buf, 2);
      readbuf[i] = png_get_uint_16(buf);
   }

   if (png_crc_finish(png_ptr, 0))
      return;

   png_set_hIST(png_ptr, info_ptr, readbuf);
}
#else
#  define png_handle_hIST NULL
#endif /* READ_hIST */

#ifdef PNG_READ_pHYs_SUPPORTED
static void
png_handle_pHYs(png_structrp png_ptr, png_inforp info_ptr)
{
   png_byte buf[9];
   png_uint_32 res_x, res_y;
   int unit_type;

   png_debug(1, "in png_handle_pHYs");

   if (info_ptr != NULL && (info_ptr->valid & PNG_INFO_pHYs))
   {
      png_handle_error(png_ptr, "duplicate");
      return;
   }

   if (png_ptr->chunk_length != 9)
   {
      png_handle_bad_length(png_ptr);
      return;
   }

   png_crc_read(png_ptr, buf, 9);

   if (png_crc_finish(png_ptr, 0))
      return;

   res_x = png_get_uint_32(buf);
   res_y = png_get_uint_32(buf + 4);
   unit_type = buf[8];
   png_set_pHYs(png_ptr, info_ptr, res_x, res_y, unit_type);
}
#else
#  define png_handle_pHYs NULL
#endif /* READ_pHYs */

#ifdef PNG_READ_oFFs_SUPPORTED /* EXTENSION, before IDAT, no duplicates */
static void
png_handle_oFFs(png_structrp png_ptr, png_inforp info_ptr)
{
   png_byte buf[9];
   png_int_32 offset_x, offset_y;
   int unit_type;

   png_debug(1, "in png_handle_oFFs");

   if (info_ptr != NULL && (info_ptr->valid & PNG_INFO_oFFs))
   {
      png_handle_error(png_ptr, "duplicate");
      return;
   }

   if (png_ptr->chunk_length != 9)
   {
      png_handle_bad_length(png_ptr);
      return;
   }

   png_crc_read(png_ptr, buf, 9);

   if (png_crc_finish(png_ptr, 0))
      return;

   offset_x = png_get_int_32(buf);
   offset_y = png_get_int_32(buf + 4);
   unit_type = buf[8];
   png_set_oFFs(png_ptr, info_ptr, offset_x, offset_y, unit_type);
}
#else
#  define png_handle_oFFs NULL
#endif /* READ_oFFs */

#ifdef PNG_READ_pCAL_SUPPORTED /* EXTENSION: before IDAT, no duplicates */
static void
png_handle_pCAL(png_structrp png_ptr, png_inforp info_ptr)
{
   png_int_32 X0, X1;
   png_byte type, nparams;
   png_bytep buffer, buf, units, endptr;
   png_charpp params;
   int i;

   png_debug(1, "in png_handle_pCAL");

   if (info_ptr != NULL && (info_ptr->valid & PNG_INFO_pCAL))
   {
      png_handle_error(png_ptr, "duplicate");
      return;
   }

   png_debug1(2, "Allocating and reading pCAL chunk data (%u bytes)",
       png_ptr->chunk_length + 1);

   buffer = png_read_buffer(png_ptr, png_ptr->chunk_length+1, 2/*silent*/);

   if (buffer == NULL)
   {
      png_handle_error(png_ptr, "out of memory");
      return;
   }

   png_crc_read(png_ptr, buffer, png_ptr->chunk_length);

   if (png_crc_finish(png_ptr, 0))
      return;

   buffer[png_ptr->chunk_length] = 0; /* Null terminate the last string */

   png_debug(3, "Finding end of pCAL purpose string");
   for (buf = buffer; *buf; buf++)
      /* Empty loop */ ;

   endptr = buffer + png_ptr->chunk_length;

   /* We need to have at least 12 bytes after the purpose string
    * in order to get the parameter information.
    */
   if (endptr - buf <= 12)
   {
      png_chunk_benign_error(png_ptr, "invalid");
      return;
   }

   png_debug(3, "Reading pCAL X0, X1, type, nparams, and units");
   X0 = png_get_int_32((png_bytep)buf+1);
   X1 = png_get_int_32((png_bytep)buf+5);
   type = buf[9];
   nparams = buf[10];
   units = buf + 11;

   png_debug(3, "Checking pCAL equation type and number of parameters");
   /* Check that we have the right number of parameters for known
    * equation types.
    */
   if ((type == PNG_EQUATION_LINEAR && nparams != 2) ||
       (type == PNG_EQUATION_BASE_E && nparams != 3) ||
       (type == PNG_EQUATION_ARBITRARY && nparams != 3) ||
       (type == PNG_EQUATION_HYPERBOLIC && nparams != 4))
   {
      png_chunk_benign_error(png_ptr, "invalid parameter count");
      return;
   }

   else if (type >= PNG_EQUATION_LAST)
   {
      png_chunk_benign_error(png_ptr, "unrecognized equation type");
      return;
   }

   for (buf = units; *buf; buf++)
      /* Empty loop to move past the units string. */ ;

   png_debug(3, "Allocating pCAL parameters array");

   params = png_voidcast(png_charpp, png_malloc_base(png_ptr,
       nparams * (sizeof (png_charp))));

   if (params == NULL)
   {
      png_chunk_benign_error(png_ptr, "out of memory");
      return;
   }

   /* Get pointers to the start of each parameter string. */
   for (i = 0; i < nparams; i++)
   {
      buf++; /* Skip the null string terminator from previous parameter. */

      png_debug1(3, "Reading pCAL parameter %d", i);

      for (params[i] = (png_charp)buf; buf <= endptr && *buf != 0; buf++)
         /* Empty loop to move past each parameter string */ ;

      /* Make sure we haven't run out of data yet */
      if (buf > endptr)
      {
         png_free(png_ptr, params);
         png_chunk_benign_error(png_ptr, "invalid data");
         return;
      }
   }

   png_set_pCAL(png_ptr, info_ptr, (png_charp)buffer, X0, X1, type, nparams,
       (png_charp)units, params);

   png_free(png_ptr, params);
}
#else
#  define png_handle_pCAL NULL
#endif /* READ_pCAL */

#ifdef PNG_READ_sCAL_SUPPORTED
/* Read the sCAL chunk */
static void
png_handle_sCAL(png_structrp png_ptr, png_inforp info_ptr)
{
   png_uint_32 length = png_ptr->chunk_length;
   png_bytep buffer;
   png_size_t i;
   int state;

   png_debug(1, "in png_handle_sCAL");

   if (info_ptr != NULL && (info_ptr->valid & PNG_INFO_sCAL))
   {
      png_handle_error(png_ptr, "duplicate");
      return;
   }

   /* Need unit type, width, \0, height: minimum 4 bytes */
   if (length < 4)
   {
      png_handle_bad_length(png_ptr);
      return;
   }

   png_debug1(2, "Allocating and reading sCAL chunk data (%u bytes)",
       length + 1);

   buffer = png_read_buffer(png_ptr, length+1, 2/*silent*/);

   if (buffer == NULL)
   {
      png_handle_error(png_ptr, "out of memory");
      return;
   }

   png_crc_read(png_ptr, buffer, length);
   buffer[length] = 0; /* Null terminate the last string */

   if (png_crc_finish(png_ptr, 0))
      return;

   /* Validate the unit. */
   if (buffer[0] != 1 && buffer[0] != 2)
   {
      png_chunk_benign_error(png_ptr, "invalid unit");
      return;
   }

   /* Validate the ASCII numbers, need two ASCII numbers separated by
    * a '\0' and they need to fit exactly in the chunk data.
    */
   i = 1;
   state = 0;

   if (!png_check_fp_number((png_const_charp)buffer, length, &state, &i) ||
       i >= length || buffer[i++] != 0)
      png_chunk_benign_error(png_ptr, "bad width format");

   else if (!PNG_FP_IS_POSITIVE(state))
      png_chunk_benign_error(png_ptr, "non-positive width");

   else
   {
      png_size_t heighti = i;

      state = 0;
      if (!png_check_fp_number((png_const_charp)buffer, length, &state, &i) ||
         i != length)
         png_chunk_benign_error(png_ptr, "bad height format");

      else if (!PNG_FP_IS_POSITIVE(state))
         png_chunk_benign_error(png_ptr, "non-positive height");

      else
         /* This is the (only) success case. */
         png_set_sCAL_s(png_ptr, info_ptr, buffer[0],
             (png_charp)buffer+1, (png_charp)buffer+heighti);
   }
}
#else
#  define png_handle_sCAL NULL
#endif /* READ_sCAL */

#ifdef PNG_READ_tIME_SUPPORTED
static void
png_handle_tIME(png_structrp png_ptr, png_inforp info_ptr)
{
   png_byte buf[7];
   png_time mod_time;

   png_debug(1, "in png_handle_tIME");

   if (info_ptr != NULL && (info_ptr->valid & PNG_INFO_tIME))
   {
      png_handle_error(png_ptr, "duplicate");
      return;
   }

   if (png_ptr->chunk_length != 7)
   {
      png_handle_bad_length(png_ptr);
      return;
   }

   png_crc_read(png_ptr, buf, 7);

   if (png_crc_finish(png_ptr, 0))
      return;

   mod_time.second = buf[6];
   mod_time.minute = buf[5];
   mod_time.hour = buf[4];
   mod_time.day = buf[3];
   mod_time.month = buf[2];
   mod_time.year = png_get_uint_16(buf);

   png_set_tIME(png_ptr, info_ptr, &mod_time);
}
#else
#  define png_handle_tIME NULL
#endif /* READ_tIME */

#ifdef PNG_READ_tEXt_SUPPORTED
static void
png_handle_tEXt(png_structrp png_ptr, png_inforp info_ptr)
{
   png_uint_32 length = png_ptr->chunk_length;
   png_text  text_info;
   png_bytep buffer;
   png_charp key;
   png_charp text;
   png_uint_32 skip = 0;

   png_debug(1, "in png_handle_tEXt");

#ifdef PNG_USER_LIMITS_SUPPORTED
   if (png_ptr->user_chunk_cache_max != 0)
   {
      if (png_ptr->user_chunk_cache_max == 1)
      {
         png_crc_finish(png_ptr, length);
         return;
      }

      if (--png_ptr->user_chunk_cache_max == 1)
      {
         png_handle_error(png_ptr, "no space in chunk cache");
         return;
      }
   }
#endif /* USER_LIMITS */

   buffer = png_read_buffer(png_ptr, length+1, 1/*warn*/);

   if (buffer == NULL)
   {
     png_handle_error(png_ptr, "out of memory");
     return;
   }

   png_crc_read(png_ptr, buffer, length);

   if (png_crc_finish(png_ptr, skip))
      return;

   key = (png_charp)buffer;
   key[length] = 0;

   for (text = key; *text; text++)
      /* Empty loop to find end of key */ ;

   if (text != key + length)
      text++;

   text_info.compression = PNG_TEXT_COMPRESSION_NONE;
   text_info.key = key;
   text_info.lang = NULL;
   text_info.lang_key = NULL;
   text_info.itxt_length = 0;
   text_info.text = text;
   text_info.text_length = strlen(text);

   if (png_set_text_2(png_ptr, info_ptr, &text_info, 1))
      png_warning(png_ptr, "Insufficient memory to process text chunk");
}
#else
#  define png_handle_tEXt NULL
#endif /* READ_tEXt */

#ifdef PNG_READ_zTXt_SUPPORTED
static void
png_handle_zTXt(png_structrp png_ptr, png_inforp info_ptr)
{
   png_uint_32     length = png_ptr->chunk_length;
   png_const_charp errmsg = NULL;
   png_bytep       buffer;
   png_uint_32     keyword_length;

   png_debug(1, "in png_handle_zTXt");

#ifdef PNG_USER_LIMITS_SUPPORTED
   if (png_ptr->user_chunk_cache_max != 0)
   {
      if (png_ptr->user_chunk_cache_max == 1)
      {
         png_crc_finish(png_ptr, length);
         return;
      }

      if (--png_ptr->user_chunk_cache_max == 1)
      {
         png_handle_error(png_ptr, "no space in chunk cache");
         return;
      }
   }
#endif /* USER_LIMITS */

   /* Note, "length" is sufficient here; we won't be adding
    * a null terminator later.
    */
   buffer = png_read_buffer(png_ptr, length, 2/*silent*/);

   if (buffer == NULL)
   {
      png_handle_error(png_ptr, "out of memory");
      return;
   }

   png_crc_read(png_ptr, buffer, length);

   if (png_crc_finish(png_ptr, 0))
      return;

   /* TODO: also check that the keyword contents match the spec! */
   for (keyword_length = 0;
      keyword_length < length && buffer[keyword_length] != 0;
      ++keyword_length)
      /* Empty loop to find end of name */ ;

   if (keyword_length > 79 || keyword_length < 1)
      errmsg = "bad keyword";

   /* zTXt must have some LZ data after the keyword, although it may expand to
    * zero bytes; we need a '\0' at the end of the keyword, the compression type
    * then the LZ data:
    */
   else if (keyword_length + 3 > length)
      errmsg = "truncated";

   else if (buffer[keyword_length+1] != PNG_COMPRESSION_TYPE_BASE)
      errmsg = "unknown compression type";

   else
   {
      png_alloc_size_t uncompressed_length = PNG_SIZE_MAX;

      /* TODO: at present png_decompress_chunk imposes a single application
       * level memory limit, this should be split to different values for iCCP
       * and text chunks.
       */
      if (png_decompress_chunk(png_ptr, length, keyword_length+2,
          &uncompressed_length, 1/*terminate*/) == Z_STREAM_END)
      {
         png_text text;

         /* It worked; png_ptr->read_buffer now looks like a tEXt chunk except
          * for the extra compression type byte and the fact that it isn't
          * necessarily '\0' terminated.
          */
         buffer = png_ptr->read_buffer;
         buffer[uncompressed_length+(keyword_length+2)] = 0;

         text.compression = PNG_TEXT_COMPRESSION_zTXt;
         text.key = (png_charp)buffer;
         text.text = (png_charp)(buffer + keyword_length+2);
         text.text_length = uncompressed_length;
         text.itxt_length = 0;
         text.lang = NULL;
         text.lang_key = NULL;

         if (png_set_text_2(png_ptr, info_ptr, &text, 1))
            errmsg = "insufficient memory";
      }

      else
         errmsg = png_ptr->zstream.msg;
   }

   if (errmsg != NULL)
      png_chunk_benign_error(png_ptr, errmsg);
}
#else
#  define png_handle_zTXt NULL
#endif /* READ_zTXt */

#ifdef PNG_READ_iTXt_SUPPORTED
static void
png_handle_iTXt(png_structrp png_ptr, png_inforp info_ptr)
{
   png_uint_32 length = png_ptr->chunk_length;
   png_const_charp errmsg = NULL;
   png_bytep buffer;
   png_uint_32 prefix_length;

   png_debug(1, "in png_handle_iTXt");

#ifdef PNG_USER_LIMITS_SUPPORTED
   if (png_ptr->user_chunk_cache_max != 0)
   {
      if (png_ptr->user_chunk_cache_max == 1)
      {
         png_crc_finish(png_ptr, length);
         return;
      }

      if (--png_ptr->user_chunk_cache_max == 1)
      {
         png_handle_error(png_ptr, "no space in chunk cache");
         return;
      }
   }
#endif /* USER_LIMITS */

   buffer = png_read_buffer(png_ptr, length+1, 1/*warn*/);

   if (buffer == NULL)
   {
      png_handle_error(png_ptr, "out of memory");
      return;
   }

   png_crc_read(png_ptr, buffer, length);

   if (png_crc_finish(png_ptr, 0))
      return;

   /* First the keyword. */
   for (prefix_length=0;
      prefix_length < length && buffer[prefix_length] != 0;
      ++prefix_length)
      /* Empty loop */ ;

   /* Perform a basic check on the keyword length here. */
   if (prefix_length > 79 || prefix_length < 1)
      errmsg = "bad keyword";

   /* Expect keyword, compression flag, compression type, language, translated
    * keyword (both may be empty but are 0 terminated) then the text, which may
    * be empty.
    */
   else if (prefix_length + 5 > length)
      errmsg = "truncated";

   else if (buffer[prefix_length+1] == 0 ||
      (buffer[prefix_length+1] == 1 &&
      buffer[prefix_length+2] == PNG_COMPRESSION_TYPE_BASE))
   {
      int compressed = buffer[prefix_length+1] != 0;
      png_uint_32 language_offset, translated_keyword_offset;
      png_alloc_size_t uncompressed_length = 0;

      /* Now the language tag */
      prefix_length += 3;
      language_offset = prefix_length;

      for (; prefix_length < length && buffer[prefix_length] != 0;
         ++prefix_length)
         /* Empty loop */ ;

      /* WARNING: the length may be invalid here, this is checked below. */
      translated_keyword_offset = ++prefix_length;

      for (; prefix_length < length && buffer[prefix_length] != 0;
         ++prefix_length)
         /* Empty loop */ ;

      /* prefix_length should now be at the trailing '\0' of the translated
       * keyword, but it may already be over the end.  None of this arithmetic
       * can overflow because chunks are at most 2^31 bytes long, but on 16-bit
       * systems the available allocation may overflow.
       */
      ++prefix_length;

      if (!compressed && prefix_length <= length)
         uncompressed_length = length - prefix_length;

      else if (compressed && prefix_length < length)
      {
         uncompressed_length = PNG_SIZE_MAX;

         /* TODO: at present png_decompress_chunk imposes a single application
          * level memory limit, this should be split to different values for
          * iCCP and text chunks.
          */
         if (png_decompress_chunk(png_ptr, length, prefix_length,
             &uncompressed_length, 1/*terminate*/) == Z_STREAM_END)
            buffer = png_ptr->read_buffer;

         else
            errmsg = png_ptr->zstream.msg;
      }

      else
         errmsg = "truncated";

      if (errmsg == NULL)
      {
         png_text text;

         buffer[uncompressed_length+prefix_length] = 0;

         if (compressed == 0)
            text.compression = PNG_ITXT_COMPRESSION_NONE;

         else
            text.compression = PNG_ITXT_COMPRESSION_zTXt;

         text.key = (png_charp)buffer;
         text.lang = (png_charp)buffer + language_offset;
         text.lang_key = (png_charp)buffer + translated_keyword_offset;
         text.text = (png_charp)buffer + prefix_length;
         text.text_length = 0;
         text.itxt_length = uncompressed_length;

         if (png_set_text_2(png_ptr, info_ptr, &text, 1))
            errmsg = "insufficient memory";
      }
   }

   else
      errmsg = "bad compression info";

   if (errmsg != NULL)
      png_chunk_benign_error(png_ptr, errmsg);
}
#else
#  define png_handle_iTXt NULL
#endif /* READ_iTXt */

/* UNSUPPORTED CHUNKS */
#define png_handle_sTER NULL
#define png_handle_fRAc NULL
#define png_handle_gIFg NULL
#define png_handle_gIFt NULL
#define png_handle_gIFx NULL
#define png_handle_dSIG NULL

/* IDAT has special treatment below */
#define png_handle_IDAT NULL

/******************************************************************************
 * UNKNOWN HANDLING LOGIC
 *
 * There are three ways an unknown chunk may arise:
 *
 * 1) Chunks not in the spec.
 * 2) Chunks in the spec where libpng support doesn't exist or has been compiled
 *    out.  These are recognized, for a very small performance benefit at the
 *    cost of maintaining a png_known_chunks entry for each one.
 * 3) Chunks supported by libpng which have been marked as 'unknown' by the
 *    application.
 *
 * Prior to 1.7.0 all three cases are handled the same way, in 1.7.0 some
 * attempt is made to optimize (2) and (3) by storing flags in
 * png_struct::known_unknown for chunks in the spec which have been marked for
 * unknown handling.
 *
 * There are three things libpng can do with an unknown chunk, in order of
 * preference:
 *
 * 1) If PNG_READ_USER_CHUNKS_SUPPORTED call an application supplied callback
 *    with all the chunk data.  If this doesn't handle the chunk in prior
 *    versions of libpng the chunk would be stored if safe otherwise skipped.
 *    In 1.7.0 the specified chunk unknown handling is used.
 * 2) If PNG_SAVE_UNKNOWN_CHUNKS_SUPPOPRTED the chunk may be saved in the
 *    info_struct (if there is one.)
 * 3) The chunk can be skipped.
 *
 * In effect libpng tries each option in turn.  (2) looks at any per-chunk
 * unknown handling then, if one wasn't specified, the overall default.
 *
 * IHDR and IEND cannot be treated as unknown.  PLTE and IDAT can.  Prior to
 * 1.7.0 they couldn't be skipped without a png_error.  1.7.0 adds an extension
 * which allows any critical chunk to be skipped so long as IDAT is skipped; the
 * logic for failing on critical chunks only applies if the image data is being
 * processed.
 *
 * The default behavior is (3); unknown chunks are simply skipped.  1.7.0 uses
 * this to optimize the read code when possible.
 *
 * In the read code PNG_READ_UNKNOWN_CHUNKS_SUPPORTED is set only if either (1)
 * or (2) or both are supported.
 *
 *****************************************************************************/
#ifdef PNG_SAVE_UNKNOWN_CHUNKS_SUPPORTED
static int
png_chunk_unknown_handling(png_const_structrp png_ptr, png_uint_32 chunk_name)
{
   png_byte chunk_string[5];

   PNG_CSTRING_FROM_CHUNK(chunk_string, chunk_name);
   return png_handle_as_unknown(png_ptr, chunk_string);
}
#endif /* SAVE_UNKNOWN_CHUNKS */

#ifdef PNG_READ_UNKNOWN_CHUNKS_SUPPORTED
/* Utility function for png_handle_unknown; set up png_ptr::unknown_chunk */
static void
png_make_unknown_chunk(png_structrp png_ptr, png_unknown_chunkp chunk,
    png_bytep data)
{
   chunk->data = data;
   chunk->size = png_ptr->chunk_length;
   PNG_CSTRING_FROM_CHUNK(chunk->name, png_ptr->chunk_name);
   /* 'mode' is a flag array, only three of the bottom four bits are public: */
   chunk->location =
      png_ptr->mode & (PNG_HAVE_IHDR+PNG_HAVE_PLTE+PNG_AFTER_IDAT);
}

/* Handle an unknown, or known but disabled, chunk */
void /* PRIVATE */
png_handle_unknown(png_structrp png_ptr, png_inforp info_ptr,
    png_bytep chunk_data)
{
   png_debug(1, "in png_handle_unknown");

   /* NOTE: this code is based on the code in libpng-1.4.12 except for fixing
    * the bug which meant that setting a non-default behavior for a specific
    * chunk would be ignored (the default was always used unless a user
    * callback was installed).
    *
    * 'keep' is the value from the png_chunk_unknown_handling, the setting for
    * this specific chunk_name, if PNG_HANDLE_AS_UNKNOWN_SUPPORTED, if not it
    * will always be PNG_HANDLE_CHUNK_AS_DEFAULT and it needs to be set here.
    * This is just an optimization to avoid multiple calls to the lookup
    * function.
    *
    * One of the following methods will read the chunk or skip it (at least one
    * of these is always defined because this is the only way to switch on
    * PNG_READ_UNKNOWN_CHUNKS_SUPPORTED)
    */
#  ifdef PNG_READ_USER_CHUNKS_SUPPORTED
      /* The user callback takes precedence over the chunk handling option: */
      if (png_ptr->read_user_chunk_fn != NULL)
      {
         png_unknown_chunk unknown_chunk;
         int ret;

         /* Callback to user unknown chunk handler */
         png_make_unknown_chunk(png_ptr, &unknown_chunk, chunk_data);
         ret = png_ptr->read_user_chunk_fn(png_ptr, &unknown_chunk);

         /* ret is:
          * negative: An error occurred; png_chunk_error will be called.
          *     zero: The chunk was not handled, the chunk will be discarded
          *           unless png_set_keep_unknown_chunks has been used to set
          *           a 'keep' behavior for this particular chunk, in which
          *           case that will be used.  A critical chunk will cause an
          *           error at this point unless it is to be saved.
          * positive: The chunk was handled, libpng will ignore/discard it.
          */
         if (ret > 0)
            return;

         else if (ret < 0)
            png_chunk_error(png_ptr, "application error");

         /* Else: use the default handling. */
      }
#  endif /* READ_USER_CHUNKS */

#  ifdef PNG_SAVE_UNKNOWN_CHUNKS_SUPPORTED
      {
         int keep = png_chunk_unknown_handling(png_ptr, png_ptr->chunk_name);

         /* keep is currently just the per-chunk setting, if there was no
          * setting change it to the global default now (note that this may
          * still be AS_DEFAULT) then obtain the cache of the chunk if required,
          * if not simply skip the chunk.
          */
         if (keep == PNG_HANDLE_CHUNK_AS_DEFAULT)
            keep = png_ptr->unknown_default;

         if (keep == PNG_HANDLE_CHUNK_ALWAYS ||
             (keep == PNG_HANDLE_CHUNK_IF_SAFE &&
             PNG_CHUNK_ANCILLARY(png_ptr->chunk_name)))
#        ifdef PNG_USER_LIMITS_SUPPORTED
            switch (png_ptr->user_chunk_cache_max)
            {
               case 2:
                  png_ptr->user_chunk_cache_max = 1;
                  png_chunk_benign_error(png_ptr, "no space in chunk cache");
                  /* FALL THROUGH */
               case 1:
                  /* NOTE: prior to 1.6.0 this case resulted in an unknown
                   * critical chunk being skipped, now there will be a hard
                   * error below.
                   */
                  break;

               default: /* not at limit */
                  --(png_ptr->user_chunk_cache_max);
                  /* FALL THROUGH */
               case 0: /* no limit */
#        endif /* USER_LIMITS */
                  /* Here when the limit isn't reached or when limits are
                   * compiled out; store the chunk.
                   */
                  {
                     png_unknown_chunk unknown_chunk;

                     png_make_unknown_chunk(png_ptr, &unknown_chunk,
                         chunk_data);
                     png_set_unknown_chunks(png_ptr, info_ptr, &unknown_chunk,
                         1);
                     return;
                  }
#        ifdef PNG_USER_LIMITS_SUPPORTED
            }
#        endif /* USER_LIMITS */
      }
#  else /* !SAVE_UNKNOWN_CHUNKS */
      PNG_UNUSED(info_ptr)
#  endif /* !SAVE_UNKNOWN_CHUNKS */

   /* This is the 'skip' case, where the read callback (if any) returned 0 and
    * the save code did not save the chunk.
    */
   if (PNG_CHUNK_CRITICAL(png_ptr->chunk_name))
      png_chunk_error(png_ptr, "unhandled critical chunk");
}
#endif /* READ_UNKNOWN_CHUNKS */

/* This function is called to verify that a chunk name is valid.
 * This function can't have the "critical chunk check" incorporated
 * into it, since in the future we will need to be able to call user
 * functions to handle unknown critical chunks after we check that
 * the chunk name itself is valid.
 */

/* Bit hacking: the test for an invalid byte in the 4 byte chunk name is:
 *
 * ((c) < 65 || (c) > 122 || ((c) > 90 && (c) < 97))
 */

void /* PRIVATE */
png_check_chunk_name(png_const_structrp png_ptr, const png_uint_32 chunk_name)
{
   int i;
   png_uint_32 cn=chunk_name;

   png_debug(1, "in png_check_chunk_name");

   for (i=1; i<=4; ++i)
   {
      int c = cn & 0xff;

      if (c < 65 || c > 122 || (c > 90 && c < 97))
         png_chunk_error(png_ptr, "invalid chunk type");

      cn >>= 8;
   }
}
void /* PRIVATE */
png_check_chunk_length(png_const_structrp png_ptr, const png_uint_32 length)
{
   png_alloc_size_t limit = PNG_UINT_31_MAX;

# ifdef PNG_SET_USER_LIMITS_SUPPORTED
   if (png_ptr->user_chunk_malloc_max > 0 &&
       png_ptr->user_chunk_malloc_max < limit)
      limit = png_ptr->user_chunk_malloc_max;
# elif PNG_USER_CHUNK_MALLOC_MAX > 0
   if (PNG_USER_CHUNK_MALLOC_MAX < limit)
      limit = PNG_USER_CHUNK_MALLOC_MAX;
# endif
   if (png_ptr->chunk_name == png_IDAT)
   {
      /* color_type   0 x 2 3 4 x 6 */
      int channels[]={1,0,3,1,2,0,4};
      png_alloc_size_t idat_limit = PNG_UINT_31_MAX;
      size_t row_factor =
         (png_ptr->width * channels[png_ptr->color_type] *
             (png_ptr->bit_depth > 8? 2: 1)
          + 1 + (png_ptr->interlaced? 6: 0));
      if (png_ptr->height > PNG_UINT_32_MAX/row_factor)
         idat_limit=PNG_UINT_31_MAX;
      else
         idat_limit = png_ptr->height * row_factor;
      row_factor = row_factor > 32566? 32566 : row_factor;
      idat_limit += 6 + 5*(idat_limit/row_factor+1); /* zlib+deflate overhead */
      idat_limit=idat_limit < PNG_UINT_31_MAX? idat_limit : PNG_UINT_31_MAX;
      limit = limit < idat_limit? idat_limit : limit;
   }

   if (length > limit)
   {
      png_debug2(0," length = %lu, limit = %lu",
         (unsigned long)length,(unsigned long)limit);
      png_chunk_error(png_ptr, "chunk data is too large");
   }
}

/* This is the known chunk table; it contains an entry for each supported
 * chunk.
 */
static const struct
{
   void         (*handle)(png_structrp png_ptr, png_infop info_ptr);
   png_uint_32    name;
   unsigned int   before     :5;
   unsigned int   after      :5;
}
png_known_chunks[] =
/* To make the code easier to write the following defines are used, note that
 * before_end should never trip - it would indicate that libpng attempted to
 * read beyond the IEND chunk.
 *
 * 'within_IDAT' is used for IDAT chunks; PNG_AFTER_IDAT must not be set, but
 * PNG_HAVE_IDAT may be set.
 */
#define before_end   PNG_HAVE_IEND                /* Should be impossible */
#define within_IDAT  (before_end+PNG_AFTER_IDAT)
#define before_IDAT  (within_IDAT+PNG_HAVE_IDAT)
#define before_PLTE  (before_IDAT+PNG_HAVE_PLTE)
#define before_start (before_PLTE+PNG_HAVE_IHDR)
#define at_start     0
#define after_start  PNG_HAVE_IHDR
#define after_PLTE   (after_start+PNG_HAVE_PLTE)  /* NOTE: PLTE optional */
#define after_IDAT   (after_PLTE+PNG_AFTER_IDAT)  /* NOTE: PLTE optional */

/* See pngchunk.h for how this works: */
#define PNG_CHUNK_END(n, c1, c2, c3, c4, before, after)\
   { png_handle_ ## n, png_ ##n, before, after }
#define PNG_CHUNK(n, c1, c2, c3, c4, before, after)\
   PNG_CHUNK_END(n, c1, c2, c3, c4, before, after),
#define PNG_CHUNK_BEGIN(n, c1, c2, c3, c4, before, after)\
   PNG_CHUNK_END(n, c1, c2, c3, c4, before, after),
{
#  include "pngchunk.h"
};
#undef PNG_CHUNK_START
#undef PNG_CHUNK
#undef PNG_CHUNK_END

#define C_KNOWN ((sizeof png_known_chunks)/(sizeof png_known_chunks[0]))

/* See: scripts/chunkhash.c for code to generate this.  This reads the same
 * description file (pngchunk.h) as is included above.  Whenever
 * that file is changed chunkhash needs to be re-run to generate the lines
 * following this comment.
 *
 * PNG_CHUNK_HASH modifes its argument and returns an index.  png_chunk_index is
 * a function which does the same thing without modifying the value of the
 * argument.  Both macro and function always return a valid index; to detect
 * known chunks it is necessary to check png_known_chunks[index].name against
 * the hashed name.
 */
static const png_byte png_chunk_lut[64] =
{
   10, 20,  7,  3,  0, 23,  8,  0,  0, 11, 24,  0,  0,  0,  0,  4,
   12,  0,  0,  0, 13,  0,  0,  0, 25,  0,  0,  0,  2,  0,  0,  0,
    0,  6, 17,  0, 15,  0,  5, 19, 26,  0,  0,  0, 18,  0,  0,  9,
    1,  0, 21,  0, 22, 14,  0,  0,  0,  0,  0,  0, 16,  0,  0,  0
};

#define PNG_CHUNK_HASH(n)\
   png_chunk_lut[0x3f & (((n += n >> 2),n += n >> 8),n += n >> 16)]

static png_byte
png_chunk_index(png_uint_32 name)
{
   name += name >> 2;
   name += name >> 8;
   name += name >> 16;
   return png_chunk_lut[name & 0x3f];
}

#ifdef PNG_HANDLE_AS_UNKNOWN_SUPPORTED
/* Mark a known chunk to be handled as unknown. */
void /*PRIVATE*/
png_cache_known_unknown(png_structrp png_ptr, png_const_bytep add, int keep)
   /* Update the png_struct::known_unknown bit cache which stores whether each
    * known chunk should be treated as unknown.
    *
    * This cache exists to avoid doing the search loop on every chunk while
    * handling chunks.  This code is only ever used if unknown handling is
    * invoked, and the loop is isolated code; the function is called from
    * add_one_chunk in pngset.c once for each unknown and while this is
    * happening no other code is being run in this thread.
    */
{
   /* The cache only stores whether or not to handle the chunk; specifically
    * whether or not keep is 0.
    */
   png_uint_32 name = PNG_CHUNK_FROM_STRING(add);

   debug(PNG_HANDLE_CHUNK_AS_DEFAULT == 0 && C_KNOWN <= 32);

   /* But do not treat IHDR or IEND as unknown.  This is historical; it
    * always was this way, it's not clear if PLTE can always safely be
    * treated as unknown, but it is allowed.
    */
   if (name != png_IHDR && name != png_IEND)
   {
      png_byte i = png_chunk_index(name);

      if (png_known_chunks[i].name == name)
      {
         {
            if (keep != PNG_HANDLE_CHUNK_AS_DEFAULT)
            {
               png_ptr->known_unknown |= 1U << i;

#              ifdef PNG_SAVE_UNKNOWN_CHUNKS_SUPPORTED
                  if (keep == PNG_HANDLE_CHUNK_ALWAYS ||
                      (keep == PNG_HANDLE_CHUNK_IF_SAFE &&
                       PNG_CHUNK_ANCILLARY(name)))
                     png_ptr->save_unknown |= 1U << i;

                  else /* PNG_HANDLE_CHUNK_NEVER || !SAFE */
                     png_ptr->save_unknown &= ~(1U << i);
#              endif /* SAVE_UNKNOWN_CHUNKS */
            }

            else
               png_ptr->known_unknown &= ~(1U << i);
         }
      }

      /* else this is not a known chunk */
   }

   else /* 1.7.0: inform the app writer; */
      png_app_warning(png_ptr, "IHDR, IEND cannot be treated as unknown");

}
#endif /* HANDLE_AS_UNKNOWN */

/* Handle chunk position requirements in a consistent way.  The chunk must
 * come after 'after' and before 'before', either of which may be 0.  If it
 * does the function returns true, if it does not an appropriate chunk error
 * is issued; benign for non-critical chunks, fatal for critical ones.
 */
static int
png_handle_position(png_const_structrp png_ptr, unsigned int chunk)
{
   unsigned int before = png_known_chunks[chunk].before;
   unsigned int after = png_known_chunks[chunk].after;

#  ifdef PNG_ERROR_TEXT_SUPPORTED
      png_const_charp error = NULL;
#  endif /* ERROR_TEXT */

   /* PLTE is optional with all color types except PALETTE, so for the other
    * color types clear it from the 'after' bits.
    *
    * TODO: find some better way of recognizing the case where there is a PLTE
    * and it follows after_PLTE chunks (see the complex stuff in handle_PLTE.)
    */
   if (png_ptr->color_type != PNG_COLOR_TYPE_PALETTE)
      after &= PNG_BIC_MASK(PNG_HAVE_PLTE);

   if ((png_ptr->mode & before) == 0 &&
       (png_ptr->mode & after) == after)
      return 1;

   /* The error case; do before first (it is normally more important) */
#  ifdef PNG_ERROR_TEXT_SUPPORTED
      switch (before & -before) /* Lowest set bit */
      {
         case 0:
            /* Check 'after'; only one bit set. */
            switch (after)
            {
               case PNG_HAVE_IHDR:
                  error = "missing IHDR";
                  break;

               case PNG_HAVE_PLTE:
                  error = "must occur after PLTE";
                  break;

               case PNG_AFTER_IDAT:
                  error = "must come after IDAT";
                  break;

               default:
                  impossible("invalid 'after' position");
            }
            break;

         case PNG_HAVE_IHDR:
            error = "must occur first";
            break;

         case PNG_HAVE_PLTE:
            error = "must come before PLTE";
            break;

         case PNG_HAVE_IDAT:
            error = "must come before IDAT";
            break;

         default:
            impossible("invalid 'before' position");
      }
#  endif /* ERROR_TEXT */

      png_chunk_report(png_ptr, error, PNG_CHUNK_CRITICAL(png_ptr->chunk_name) ?
         PNG_CHUNK_FATAL : PNG_CHUNK_ERROR);
      return 0;
}

/* This is the shared chunk handling function, used for both the sequential and
 * progressive reader.
 */
png_chunk_op /* PRIVATE */
png_find_chunk_op(png_structrp png_ptr)
{
   /* Given a chunk in png_struct::{chunk_name,chunk_length} validate the name
    * and work out how it should be handled.  This function checks the chunk
    * location using png_struct::mode and will set the PNG_AFTER_IDAT bit if
    * appropriate but otherwise makes no changes to the stream read state.
    *
    *    png_chunk_skip         Skip this chunk
    *    png_chunk_unknown      This is an unknown chunk which can't be skipped;
    *                           the unknown handler must be called with all the
    *                           chunk data.
    *    png_chunk_process_all  The caller must call png_chunk_handle to handle
    *                           the chunk, when this call is made all the chunk
    *                           data must be available to the handler.
    *    png_chunk_process_part The handler expects data in png_struct::zstream.
    *                           {next,avail}_in and does not require all of the
    *                           data at once (as png_read_process_IDAT).
    */
   png_uint_32  chunk_name = png_ptr->chunk_name;
   unsigned int mode = png_ptr->mode;
   unsigned int index;

   /* This function should never be called if IEND has been set:
    */
   debug((mode & PNG_HAVE_IEND) == 0);

   /* IDAT logic: we are only *after* IDAT when we start reading the first
    * following (non-IDAT) chunk, this may already have been set in the IDAT
    * handling code, but if IDAT is handled as unknown this doesn't happen.
    */
   if (chunk_name != png_IDAT && (mode & PNG_HAVE_IDAT) != 0)
      mode = png_ptr->mode |= PNG_AFTER_IDAT;

   index = png_chunk_index(chunk_name);

   if (png_known_chunks[index].name == chunk_name)
   {
      /* Known chunks have a position requirement; check it, badly positioned
       * chunks that do not error out in png_handle_position are simply skipped.
       *
       * API CHANGE: libpng 1.7.0: prior versions of libpng did not check
       * ordering requirements for known chunks where the support for reading
       * them had been configured out of libpng.  This seems dangerous; the
       * user chunk callback could still see them and crash as a result.
       */
      if (!png_handle_position(png_ptr, index))
         return png_chunk_skip;

      /* Do the mode update.
       *
       * API CHANGE 1.7.0: the 'HAVE' flags are now consistently set *before*
       * the chunk is handled.  Previously only IDAT was handled this way.  This
       * can only affect an app that was previously handling PLTE itself in a
       * callback, however this seems to be impossible.
       */
      switch (chunk_name)
      {
         case png_IHDR: png_ptr->mode |= PNG_HAVE_IHDR; break;
         case png_PLTE: png_ptr->mode |= PNG_HAVE_PLTE; break;
         case png_IDAT: png_ptr->mode |= PNG_HAVE_IDAT; break;
         case png_IEND: png_ptr->mode |= PNG_HAVE_IEND; break;
         default: break;
      }

#     ifdef PNG_HANDLE_AS_UNKNOWN_SUPPORTED
         /* A known chunk may still be treated as unknown.  Check for that. */
         if (!((png_ptr->known_unknown >> index) & 1U))
#     endif /* HANDLE_AS_UNKNOWN */
      {
         /* This is a known chunk that is not being treated as unknown.  If
          * it is IDAT then partial processing is done, otherwise (at present)
          * the whole thing is processed in one shot
          *
          * TODO: this is a feature of the legacy use of the sequential read
          * code in the handlers, fix this.
          */
         if (chunk_name == png_IDAT)
            return png_chunk_process_part;

         /* Check for a known chunk where support has been compiled out of
          * libpng.  We know it cannot be a critical chunk; support for those
          * cannot be removed.
          */
         if (png_known_chunks[index].handle != NULL)
            return png_chunk_process_all;

#        ifdef PNG_READ_USER_CHUNKS_SUPPORTED
            if (png_ptr->read_user_chunk_fn != NULL)
               return png_chunk_unknown;
#        endif /* READ_USER_CHUNKS */

#        ifdef PNG_SAVE_UNKNOWN_CHUNKS_SUPPORTED
            /* There is no per-chunk special handling set for this chunk
             * (because of the test on known_unknown above) so only the
             * default unknown handling behavior matters.  We skip the chunk
             * if the behavior is 'NEVER' or 'DEFAULT'.  This is irrelevant
             * if SAVE_UNKNOWN_CHUNKS is not supported.
             */
            if (png_ptr->unknown_default > PNG_HANDLE_CHUNK_NEVER)
               return png_chunk_unknown;
#        endif /* SAVE_UNKNOWN_CHUNKS */

         return png_chunk_skip;
      }

#     ifdef PNG_HANDLE_AS_UNKNOWN_SUPPORTED
         else
         {
            /* Else this is a known chunk that is being treated as unknown.  If
             * there is a user callback the whole shebang is required:
             */
#           ifdef PNG_READ_USER_CHUNKS_SUPPORTED
               if (png_ptr->read_user_chunk_fn != NULL)
                  return png_chunk_unknown;
#           endif /* READ_USER_CHUNKS */

            /* No user callback, there is a possibility that we can skip this
             * chunk:
             */
#           ifdef PNG_SAVE_UNKNOWN_CHUNKS_SUPPORTED
               if ((png_ptr->save_unknown >> index) & 1U)
                  return png_chunk_unknown;
#           endif /* SAVE_UNKNOWN_CHUNKS */

            /* If this is a critical chunk and IDAT is not being skipped then
             * this is an error.  The only possibility here is PLTE on an
             * image which is palette mapped.  If the app ignores this error
             * then there will be a more definate one in png_handle_unknown.
             */
            if (chunk_name == png_PLTE &&
                png_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
               png_app_error(png_ptr, "skipping PLTE on palette image");

            return png_chunk_skip;
         }
#     endif /* HANDLE_AS_UNKNOWN */
   }

   else /* unknown chunk */
   {
      /* The code above implicitly validates the chunk name, however if a chunk
       * name/type is not recognized it is necessary to validate it to ensure
       * that the PNG stream isn't hopelessly damaged:
       */
      png_check_chunk_name(png_ptr, chunk_name);

#     ifdef PNG_READ_USER_CHUNKS_SUPPORTED
         if (png_ptr->read_user_chunk_fn != NULL)
            return png_chunk_unknown;
#     endif /* READ_USER_CHUNKS */

#     ifdef PNG_SAVE_UNKNOWN_CHUNKS_SUPPORTED
         /* There may be per-chunk handling, otherwise the default is used, this
          * is the one place where the list needs to be searched:
          */
         {
            int keep = png_chunk_unknown_handling(png_ptr, chunk_name);

            if (keep == PNG_HANDLE_CHUNK_AS_DEFAULT)
               keep = png_ptr->unknown_default;

            if (keep == PNG_HANDLE_CHUNK_ALWAYS ||
                (keep == PNG_HANDLE_CHUNK_IF_SAFE &&
                 PNG_CHUNK_ANCILLARY(chunk_name)))
               return png_chunk_unknown;
         }
#     endif /* SAVE_UNKNOWN_CHUNKS */

      /* The chunk will be skipped so it must not be a critical chunk, unless
       * IDATs are being skipped too.
       */
      if (PNG_CHUNK_CRITICAL(chunk_name)
#        ifdef PNG_HANDLE_AS_UNKNOWN_SUPPORTED
            && !png_IDATs_skipped(png_ptr)
#        endif /* HANDLE_AS_UNKNOWN */
         )
         png_chunk_error(png_ptr, "unhandled critical chunk");

      return png_chunk_skip;
   }
}

void /* PRIVATE */
png_handle_chunk(png_structrp png_ptr, png_inforp info_ptr)
   /* The chunk to handle is in png_struct::chunk_name,chunk_length.
    *
    * NOTE: at present it is only valid to call this after png_find_chunk_op
    * has returned png_chunk_process_all and all the data is available for
    * png_handle_chunk (via the libpng read callback.)
    */
{
   png_uint_32  chunk_name = png_ptr->chunk_name;
   unsigned int index = png_chunk_index(chunk_name);

   /* So this must be true: */
   affirm(png_known_chunks[index].name == chunk_name &&
          png_known_chunks[index].handle != NULL);

   png_known_chunks[index].handle(png_ptr, info_ptr);
}

static void
copy_row(png_const_structrp png_ptr, png_bytep dp, png_const_bytep sp,
   png_uint_32 x/*in INPUT*/, png_uint_32 width/*of INPUT*/, int clear)
   /* Copy the row in row_buffer; this is the 'simple' case of combine_row
    * where no adjustment to the pixel spacing is required.
    */
{
   png_copy_row(png_ptr, dp, sp, x, width,
#     ifdef PNG_TRANSFORM_MECH_SUPPORTED
         png_ptr->row_bit_depth * PNG_FORMAT_CHANNELS(png_ptr->row_format),
#     else
         PNG_PIXEL_DEPTH(*png_ptr),
#     endif
      clear/*clear partial byte at end of row*/, 1/*sp -> dp[x]*/);
}

#ifdef PNG_READ_INTERLACING_SUPPORTED
static void
combine_row(png_const_structrp png_ptr, png_bytep dp, png_const_bytep sp,
   png_uint_32 x/*in INPUT*/, png_uint_32 width/*of INPUT*/, int display)
   /* 1.7.0: API CHANGE: prior to 1.7.0 read de-interlace was done in two steps,
    * the first would expand a narrow pass by replicating pixels according to
    * the inter-pixel spacing of the pixels from the pass in the image.  It did
    * not take account of any offset from the start of the image row of the
    * first pixel.  The second step happened in png_combine_row where the result
    * was merged into the output rows.
    *
    * In 1.7.0 this is no longer done.  Instead all the work happens here.  This
    * is only an API change for the progressive reader if the app didn't call
    * png_combine_row, but rather expected an expanded row.  It's not obvious
    * why any user of the progressive reader would want this, particularly given
    * the weird non-offseting of the start in the original
    * 'png_do_read_interlace'; the behavior was completely undocumented.
    *
    * In 1.7.0 combine_row does all the work.  It expects a raw uncompressed,
    * de-filtered, transformed row and it either copies it if:
    *
    * 1) It is not interlaced.
    * 2) libpng isn't handling the de-interlace.
    * 3) This is pass 7 (i.e. '6' using the libpng 0-based numbering).
    *
    * The input data comes from png_struct and sp:
    *
    *    sp[width(pixels)];      the row data from input[x(pixels)...]
    *    png_struct::pass;       the pass
    *    png_struct::row_number; the row number in the *image*
    *    png_struct::row_bit_depth,
    *    png_struct::row_format; the pixel format, if TRANSFORM_MECH, else:
    *    png_struct::bit_depth,
    *    png_struct::color_type; the pixel format otherwise
    *
    * The destination pointer (but not size) and how to handle intermediate
    * passes are arguments to the API.  The destination is the pointer to the
    * entire row buffer, not just the part from output[x] on.  'display' is
    * interpreted as:
    *
    *    0: only overwrite destination pixels that will correspond to the source
    *       pixel in the final image.  'sparkle' mode.
    *    1: overwrite the corresponding destination pixel and all following
    *       pixels (horizontally and, eventually, vertically) that will come
    *       from *later* passes.  'block' mode.
    */
{
   const unsigned int pass = png_ptr->pass;

   png_debug(1, "in png_combine_row");

   /* Factor out the copy case first, the 'display' argument is irrelevant in
    * these cases:
    */
   if (!png_ptr->do_interlace || png_ptr->pass == 6)
   {
      copy_row(png_ptr, dp, sp, x, width, 0/*do not clear*/);
      return;
   }

   else /* not a simple copy */
   {
      const unsigned int pixel_depth =
#     ifdef PNG_TRANSFORM_MECH_SUPPORTED
         png_ptr->row_bit_depth * PNG_IMAGE_PIXEL_CHANNELS(png_ptr->row_format);
#     else
         PNG_PIXEL_DEPTH(*png_ptr);
#     endif
      png_uint_32 row_width = png_ptr->width; /* output width */
      /* The first source pixel is written to PNG_COL_FROM_PASS of the
       * destination:
       */
      png_uint_32 dx = PNG_COL_FROM_PASS_COL(x, pass);
      /* The corresponding offset within the 8x8 block: */
      const unsigned int dstart = dx & 0x7U;
      /* Find the first pixel written in any 8x8 block IN THIS PASS: */
      const unsigned int pass_start = PNG_PASS_START_COL(pass);
      /* Subsequent pixels are written PNG_PASS_COL_OFFSET further on: */
      const unsigned int doffset = PNG_PASS_COL_OFFSET(pass);
      /* In 'block' mode when PNG_PASS_START_COL(pass) is 0 (PNG passes 1,3,5,7)
       * the same pixel is replicated doffset times, when PNG_PASS_START_COL is
       * non-zero (PNG passes 2,4,6) it is replicated PNG_PASS_START_COL times.
       * For 'sparkle' mode only one copy of the pixel is written:
       */
      unsigned int drep = display ? (pass_start ? pass_start : doffset) : 1;

      /* Standard check for byte alignment */
      debug(((x * pixel_depth/*OVERFLOW OK*/) & 0x7U) == 0U);

      /* The caller should have excluded the narrow cases: */
      affirm(row_width > dx);
      row_width -= dx;
      /* Advance dp to the start of the 8x8 block containing the first pixel to
       * write, adjust dx to be an offset within the block:
       */
      dp += png_calc_rowbytes(png_ptr, pixel_depth, dx & ~0x7U);
      dx &= 0x7U;

      /* So each source pixel sp[i] is written to:
       *
       *    dp[dstart + i*doffset]..dp[dstart + i*doffset + (drep-1)]
       *
       * Until we get to row_width.  This is easy for pixels that are 8 or more
       * bits deep; whole bytes are read and written, slightly more difficult
       * when pixel_depth * drep is at least 8 bits, because then dstart *
       * pixel_depth will always be a whole byte and most complex when source
       * and destination require sub-byte addressing.
       *
       * Cherry pick the easy cases:
       */
      if (pixel_depth > 8U)
      {
         /* Convert to bytes: */
         const unsigned int pixel_bytes = pixel_depth >> 3;

         dp += dstart * pixel_bytes;

         for (;;)
         {
            unsigned int c;

            if (drep > row_width)
               drep = row_width;

            for (c=0U; c<drep; ++c)
               memcpy(dp, sp, pixel_bytes), dp += pixel_bytes;

            if (doffset >= row_width)
               break;

            row_width -= doffset;
            dp += (doffset-drep) * pixel_bytes;
            sp += pixel_bytes;
         }
      }

      else if (pixel_depth == 8U)
      {
         /* Optimize the common 1-byte per pixel case (typical case for palette
          * mapped images):
          */
         dp += dstart;

         for (;;)
         {
            if (drep > row_width)
               drep = row_width;

            memset(dp, *sp++, drep);

            if (doffset >= row_width)
               break;

            row_width -= doffset;
            dp += doffset;
         }
      }

      else /* pixel_depth < 8 */
      {
         /* Pixels are 1, 2 or 4 bits in size. */
         unsigned int spixel = *sp++;
         unsigned int dbrep = pixel_depth * drep;
         unsigned int spos = 0U;
#        ifdef PNG_READ_PACKSWAP_SUPPORTED
            const int lsb =
               (png_ptr->row_format & PNG_FORMAT_FLAG_SWAPPED) != 0;
#        endif /* READ_PACKSWAP */

         if (dbrep >= 8U)
         {
            /* brep must be greater than 1, the destination does not require
             * sub-byte addressing except, maybe, at the end.
             *
             * db is the count of bytes required to replicate the source pixel
             * drep times.
             */
            debug((dbrep & 7U) == 0U);
            dbrep >>= 3;
            debug((dstart * pixel_depth & 7U) == 0U);
            dp += (dstart * pixel_depth) >> 3;

            for (;;)
            {
               /* Fill a byte with copies of the next pixel: */
               unsigned int spixel_rep = spixel;

#              ifdef PNG_READ_PACKSWAP_SUPPORTED
                  if (lsb)
                     spixel_rep >>= spos;
                  else
#              endif /* READ_PACKSWAP */
               spixel_rep >>= (8U-pixel_depth)-spos;

               switch (pixel_depth)
               {
                  case 1U: spixel_rep &=  1U; spixel_rep |= spixel_rep << 1;
                           /*FALL THROUGH*/
                  case 2U: spixel_rep &=  3U; spixel_rep |= spixel_rep << 2;
                           /*FALL THROUGH*/
                  case 4U: spixel_rep &= 15U; spixel_rep |= spixel_rep << 4;
                           /*FALL THROUGH*/
                  default: break;
               }

               /* This may leave some pixels unwritten when there is a partial
                * byte write required at the end:
                */
               if (drep > row_width)
                  drep = row_width, dbrep = (pixel_depth * drep) >> 3;

               memset(dp, spixel_rep, dbrep);

               if (doffset >= row_width)
               {
                  /* End condition; were all 'drep' pixels written at the end?
                   */
                  drep = (pixel_depth * drep - (dbrep << 3));

                  if (drep)
                  {
                     unsigned int mask;

                     debug(drep < 8U);
                     dp += dbrep;

                     /* Set 'mask' to have 0's where *dp must be overwritten
                      * with spixel_rep:
                      */
#                    ifdef PNG_READ_PACKSWAP_SUPPORTED
                        if (lsb)
                           mask = 0xff << drep;
                        else
#                    endif /* READ_PACKSWAP */
                     mask = 0xff >> drep;

                     *dp = PNG_BYTE((*dp & mask) | (spixel_rep & ~mask));
                  }

                  break;
               }

               row_width -= doffset;
               dp += (doffset * pixel_depth) >> 3;
               spos += pixel_depth;
               if (spos == 8U)
                  spixel = *sp++, spos = 0U;
            } /* for (;;) */
         } /* pixel_depth * drep >= 8 */

         else /* pixel_depth * drep < 8 */
         {
            /* brep may be 1, pixel_depth may be 1, 2 or 4, dbrep is the number
             * of bits to set.
             */
            unsigned int bstart = dstart * pixel_depth; /* in bits */
            unsigned int dpixel;

            dp += bstart >> 3;
            bstart &= 7U;
            dpixel = *dp;

            /* dpixel:  current *dp, being modified
             * bstart:  bit offset within dpixel
             * drep:    pixel size to write (used as a check against row_width)
             * doffset: pixel step to next written destination
             *
             * spixel:  current *sp, being read, and:
             *           spixel_rep: current pixel, replicated to fill a byte
             * spos:    bit offset within spixel
             *
             * Set dbrep to a mask for the bits to set:
             */
            dbrep = (1U<<dbrep)-1U;
            for (;;)
            {
               /* Fill a byte with copies of the next pixel: */
               unsigned int spixel_rep = spixel;

#              ifdef PNG_READ_PACKSWAP_SUPPORTED
                  if (lsb)
                     spixel_rep >>= spos;
                  else
#              endif /* READ_PACKSWAP */
               spixel_rep >>= (8U-pixel_depth)-spos;

               switch (pixel_depth)
               {
                  case 1U: spixel_rep &=  1U; spixel_rep |= spixel_rep << 1;
                           /*FALL THROUGH*/
                  case 2U: spixel_rep &=  3U; spixel_rep |= spixel_rep << 2;
                           /*FALL THROUGH*/
                  case 4U: spixel_rep &= 15U; spixel_rep |= spixel_rep << 4;
                           /*FALL THROUGH*/
                  default: break;
               }

               /* This may leave some pixels unwritten when there is a partial
                * byte write required at the end:
                */
               if (drep > row_width)
                  drep = row_width, dbrep = (1U<<(pixel_depth*drep))-1U;

               {
                  unsigned int mask;

                  /* Mask dbrep bits at bstart: */
#                 ifdef PNG_READ_PACKSWAP_SUPPORTED
                     if (lsb)
                        mask = bstart;
                     else
#                 endif /* READ_PACKSWAP */
                  mask = (8U-pixel_depth)-bstart;
                  mask = dbrep << mask;

                  dpixel &= ~mask;
                  dpixel |= spixel_rep & mask;
               }

               if (doffset >= row_width)
               {
                  *dp = PNG_BYTE(dpixel);
                  break;
               }

               row_width -= doffset;
               bstart += doffset * pixel_depth;

               if (bstart >= 8U)
               {
                  *dp = PNG_BYTE(dpixel);
                  dp += bstart >> 3;
                  bstart &= 7U;
                  dpixel = *dp;
               }

               spos += pixel_depth;
               if (spos == 8U)
                  spixel = *sp++, spos = 0U;
            } /* for (;;) */
         } /* pixel_depth * drep < 8 */
      } /* pixel_depth < 8 */
   } /* not a simple copy */
}

#ifdef PNG_PROGRESSIVE_READ_SUPPORTED
void PNGAPI
png_progressive_combine_row(png_const_structrp png_ptr, png_bytep old_row,
    png_const_bytep new_row)
{
   /* new_row is a flag here - if it is NULL then the app callback was called
    * from an empty row (see the calls to png_struct::row_fn above), otherwise
    * it must be png_struct::transformed_row
    */
   if (png_ptr != NULL && new_row != NULL)
   {
      if (new_row != png_ptr->row_buffer
#        ifdef PNG_TRANSFORM_MECH_SUPPORTED
            && new_row != png_ptr->transformed_row
#        endif /* TRANSFORM_MECH */
         )
         png_app_error(png_ptr, "invalid call to png_progressive_combine_row");
      else
      {
         png_uint_32 width = png_ptr->width;

         if (png_ptr->interlaced == PNG_INTERLACE_ADAM7)
         {
            const unsigned int pass = png_ptr->pass;
            width = PNG_PASS_COLS(width, pass);
         }

         combine_row(png_ptr, old_row, new_row, 0U, width, 1/*blocky display*/);
      }
   }
}
#endif /* PROGRESSIVE_READ */
#else /* !READ_INTERLACING */
   /* No read deinterlace support, so 'combine' always reduces to 'copy', there
    * is no 'display' argument:
    */
#  define combine_row(pp, dp, sp, x, w, display)\
      copy_row(pp, dp, sp, x, w, 0/*!clear*/)
#endif /* !READ_INTERLACING */

static void
png_read_filter_row_sub(png_alloc_size_t row_bytes, unsigned int bpp,
    png_bytep row, png_const_bytep prev_row, png_const_bytep prev_pixels)
{
   while (row_bytes >= bpp)
   {
      unsigned int i;

      for (i=0; i<bpp; ++i)
         row[i] = PNG_BYTE(row[i] + prev_pixels[i]);

      prev_pixels = row;
      row += bpp;
      row_bytes -= bpp;
   }

   PNG_UNUSED(prev_row)
}

static void
png_read_filter_row_up(png_alloc_size_t row_bytes, unsigned int bpp,
    png_bytep row, png_const_bytep prev_row, png_const_bytep prev_pixels)
{
   while (row_bytes > 0)
   {
      *row = PNG_BYTE(*row + *prev_row);
      ++row;
      ++prev_row;
      --row_bytes;
   }

   PNG_UNUSED(bpp)
   PNG_UNUSED(prev_pixels)
}

static void
png_read_filter_row_avg(png_alloc_size_t row_bytes, unsigned int bpp,
    png_bytep row, png_const_bytep prev_row, png_const_bytep prev_pixels)
{
   while (row_bytes >= bpp)
   {
      unsigned int i;

      for (i=0; i<bpp; ++i)
         row[i] = PNG_BYTE(row[i] + (prev_pixels[i] + prev_row[i])/2U);

      prev_pixels = row;
      row += bpp;
      prev_row += bpp;
      row_bytes -= bpp;
   }
}

static void
png_read_filter_row_paeth_1byte_pixel(png_alloc_size_t row_bytes,
    unsigned int bpp, png_bytep row, png_const_bytep prev_row,
    png_const_bytep prev_pixels)
{
   png_const_bytep rp_end = row + row_bytes;
   png_byte a, c;

   /* prev_pixels stores pixel a then c */
   a = prev_pixels[0];
   c = prev_pixels[1];

   while (row < rp_end)
   {
      png_byte b;
      int pa, pb, pc, p;

      b = *prev_row++;

      p = b - c;
      pc = a - c;

#     ifdef PNG_USE_ABS
         pa = abs(p);
         pb = abs(pc);
         pc = abs(p + pc);
#     else
         pa = p < 0 ? -p : p;
         pb = pc < 0 ? -pc : pc;
         pc = (p + pc) < 0 ? -(p + pc) : p + pc;
#     endif

      /* Find the best predictor, the least of pa, pb, pc favoring the earlier
       * ones in the case of a tie.
       */
      if (pb < pa)
      {
         pa = pb; a = b;
      }
      if (pc < pa) a = c;

      /* Calculate the current pixel in a, and move the previous row pixel to c
       * for the next time round the loop
       */
      c = b;
      a = 0xFFU & (a + *row);
      *row++ = a;
   }

   PNG_UNUSED(bpp)
}

static void
png_read_filter_row_paeth_multibyte_pixel(png_alloc_size_t row_bytes,
    unsigned int bpp, png_bytep row, png_const_bytep prev_row,
    png_const_bytep prev_pixels)
{
   png_bytep rp_end = row + bpp;

   /* 'a' and 'c' for the first pixel come from prev_pixels: */
   while (row < rp_end)
   {
      png_byte a, b, c;
      int pa, pb, pc, p;

      /* prev_pixels stores bpp bytes for 'a', the bpp for 'c': */
      c = *(prev_pixels+bpp);
      a = *prev_pixels++;
      b = *prev_row++;

      p = b - c;
      pc = a - c;

#     ifdef PNG_USE_ABS
         pa = abs(p);
         pb = abs(pc);
         pc = abs(p + pc);
#     else
         pa = p < 0 ? -p : p;
         pb = pc < 0 ? -pc : pc;
         pc = (p + pc) < 0 ? -(p + pc) : p + pc;
#     endif

      if (pb < pa)
      {
         pa = pb; a = b;
      }
      if (pc < pa) a = c;

      a = 0xFFU & (a + *row);
      *row++ = a;
   }

   /* Remainder */
   rp_end += row_bytes - bpp;

   while (row < rp_end)
   {
      png_byte a, b, c;
      int pa, pb, pc, p;

      c = *(prev_row-bpp);
      a = *(row-bpp);
      b = *prev_row++;

      p = b - c;
      pc = a - c;

#     ifdef PNG_USE_ABS
         pa = abs(p);
         pb = abs(pc);
         pc = abs(p + pc);
#     else
         pa = p < 0 ? -p : p;
         pb = pc < 0 ? -pc : pc;
         pc = (p + pc) < 0 ? -(p + pc) : p + pc;
#     endif

      if (pb < pa)
      {
         pa = pb; a = b;
      }
      if (pc < pa) a = c;

      a = 0xFFU & (a + *row);
      *row++ = a;
   }
}

static void
png_init_filter_functions(png_structrp pp, unsigned int bpp)
   /* This function is called once for every PNG image (except for PNG images
    * that only use PNG_FILTER_VALUE_NONE for all rows) to set the
    * implementations required to reverse the filtering of PNG rows.  Reversing
    * the filter is the first transformation performed on the row data.  It is
    * performed in place, therefore an implementation can be selected based on
    * the image pixel format.  If the implementation depends on image width then
    * take care to ensure that it works correctly if the image is interlaced -
    * interlacing causes the actual row width to vary.
    */
{
   pp->read_filter[PNG_FILTER_VALUE_SUB-1] = png_read_filter_row_sub;
   pp->read_filter[PNG_FILTER_VALUE_UP-1] = png_read_filter_row_up;
   pp->read_filter[PNG_FILTER_VALUE_AVG-1] = png_read_filter_row_avg;
   if (bpp == 1)
      pp->read_filter[PNG_FILTER_VALUE_PAETH-1] =
         png_read_filter_row_paeth_1byte_pixel;
   else
      pp->read_filter[PNG_FILTER_VALUE_PAETH-1] =
         png_read_filter_row_paeth_multibyte_pixel;

#ifdef PNG_FILTER_OPTIMIZATIONS
   /* To use this define PNG_FILTER_OPTIMIZATIONS as the name of a function to
    * call to install hardware optimizations for the above functions; simply
    * replace whatever elements of the pp->read_filter[] array with a hardware
    * specific (or, for that matter, generic) optimization.
    *
    * To see an example of this examine what configure.ac does when
    * --enable-arm-neon is specified on the command line.
    */
   PNG_FILTER_OPTIMIZATIONS(pp, bpp);
#endif
}

/* This is an IDAT specific wrapper for png_zlib_inflate; the input is already
 * in png_ptr->zstream.{next,avail}_in however the output uses the full
 * capabilities of png_zlib_inflate, returning a byte count of bytes read.
 * This is just a convenience for IDAT processing.
 *
 * NOTE: this function works just fine after the zstream has ended, it just
 * fills the buffer with zeros (outputing an error message once.)
 */
static png_alloc_size_t
png_inflate_IDAT(png_structrp png_ptr, int finish,
    /* OUTPUT: */ png_bytep output, png_alloc_size_t output_size)
{
   /* Expect Z_OK if !finsh and Z_STREAM_END if finish; if Z_STREAM_END is
    * delivered when finish is not set the IDAT stream is truncated, if Z_OK is
    * delivered when finish is set this is harmless and indicates that the
    * stream end code has not been read.
    *
    * finish should be set as follows:
    *
    *    0: not reading the last row, stream not expected to end
    *    1: reading the last row, stream expected to end
    *    2: looking for stream end after the last row has been read, expect no
    *       more output and stream end.
    */
   png_alloc_size_t original_size = output_size;
   int ret = Z_STREAM_END; /* In case it ended ok before. */

   if (!png_ptr->zstream_ended)
   {
      png_const_bytep next_in = png_ptr->zstream.next_in;
      png_uint_32 avail_in = png_ptr->zstream.avail_in;

      ret = png_zlib_inflate(png_ptr, png_IDAT, finish,
          &next_in, &avail_in, &output, &output_size/*remaining*/);

      debug(next_in == png_ptr->zstream.next_in);
      debug(avail_in == png_ptr->zstream.avail_in);
      debug(output == png_ptr->zstream.next_out);
      /* But zstream.avail_out may be truncated to uInt */

      switch (ret)
      {
         case Z_STREAM_END:
            /* The caller must set finish on the last row of the image (not
             * the last row of the pass!)
             */
            debug(png_ptr->zstream_ended);

            if (!finish) /* early end */
               break;

            if (output_size > 0) /* incomplete read */
            {
               if (finish == 2) /* looking for end; it has been found */
                  return original_size - output_size;

               /* else those bytes are really needed: */
               break;
            }

            /* else: FALL THROUGH: success */

         case Z_BUF_ERROR:
            /* this is the success case: output or input is empty: */
            original_size -= output_size; /* bytes written */

            if (output_size > 0)
            {
               /* Some output still needed; if the next chunk is known
                * to not be an IDAT then this is the truncation case.
                */
               affirm(avail_in == 0);

               if ((png_ptr->mode & PNG_AFTER_IDAT) != 0)
               {
                  /* Zlib doesn't know we are out of data, so this must be
                   * done here:
                   */
                  png_ptr->zstream_ended = 1;
                  break;
               }
            }

            return original_size; /* bytes written */

         default:
            /* error */
            break;
      }

      /* The 'ended' flag should always be set if we get here, the success
       * cases where the LZ stream hasn't reached an end or an error leave
       * the function at the return above.
       */
      debug(png_ptr->zstream_ended);
   }

   /* This is the error return case; there was missing data, or an error.
    * Either continue with a warning (once; hence the zstream_error flag)
    * or png_error.
    */
   if (!png_ptr->zstream_error) /* first time */
   {
#ifdef PNG_BENIGN_READ_ERRORS_SUPPORTED
      switch (png_ptr->IDAT_error_action)
      {
         case PNG_ERROR:
            if(!strncmp(png_ptr->zstream.msg,"incorrect data check",20))
            {
               if (png_ptr->current_crc != crc_quiet_use)
                  png_chunk_warning(png_ptr, "ADLER32 checksum mismatch");
            }

            else
            {
               png_chunk_error(png_ptr, png_ptr->zstream.msg);
            }
            break;

         case PNG_WARN:
            png_chunk_warning(png_ptr, png_ptr->zstream.msg);
            break;

         default: /* ignore */
            /* Keep going */
            break;
      }
#else
      {
         if(!strncmp(png_ptr->zstream.msg,"incorrect data check",20))
            png_chunk_warning(png_ptr, "ADLER32 checksum mismatch");
         else
            png_chunk_error(png_ptr, png_ptr->zstream.msg);
      }
#endif /* !BENIGN_ERRORS */

      /* And prevent the report about too many IDATs on streams with internal
       * LZ errors:
       */
      png_ptr->zstream_error = 1;
   }

   /* This is the error recovery case; fill the buffer with zeros.  This is
    * safe because it makes the filter byte 'NONE' and the row fairly innocent.
    */
   memset(output, 0, output_size);
   return original_size;
}

/* SHARED IDAT HANDLING.
 *
 * This is the 1.7+ common read code; shared by both the progressive and
 * sequential readers.
 */
/* Initialize the row buffers, etc. */
void /* PRIVATE */
png_read_start_IDAT(png_structrp png_ptr)
{
#  ifdef PNG_HANDLE_AS_UNKNOWN_SUPPORTED
      /* This won't work at all if the app turned on unknown handling for IDAT
       * chunks; the first IDAT has already been consumed!
       */
      if (png_ptr->known_unknown & 1U)
         png_error(png_ptr, "Attempt to read image with unknown IDAT");
#  endif /* HANDLE_AS_UNKNOWN */

   /* This is a missing read of the header information; we still haven't
    * countered the first IDAT chunk.  This can only happen in the sequential
    * reader if the app didn't call png_read_info.
    */
   if (png_ptr->chunk_name != png_IDAT)
      png_error(png_ptr, "Missing call to png_read_info");

   /* Two things need to happen: first work out the effect of any
    * transformations (if supported) on the row size, second, allocate
    * row_buffer and claim the zstream.
    */
   png_init_row_info(png_ptr);

   /* Now allocate the row buffer and, if that succeeds, claim the zstream.
    */
   png_ptr->row_buffer = png_voidcast(png_bytep, png_malloc(png_ptr,
      png_calc_rowbytes(png_ptr, PNG_PIXEL_DEPTH(*png_ptr), png_ptr->width)));

   if (png_inflate_claim(png_ptr, png_IDAT) != Z_OK)
      png_error(png_ptr, png_ptr->zstream.msg);
}

/* The process function gets called when there is some IDAT data to process
 * and it just does the right thing with it.  The zstream must have been claimed
 * (owner png_IDAT) and the input data is in zstream.{next,avail}_in.  The
 * output next_{in,out} must not be changed by the caller; it is used
 * internally.
 *
 * Result codes are as follows:
 *
 *    png_row_incomplete: Insufficient IDAT data (from zstream) was present to
 *       process the next row.  zstream.avail_in will be 0.
 *    png_row_process: A new row is available in the input buffer, it should be
 *       handled before the next call (if any) to this function.
 *    png_row_repeat: For interlaced images (only) this row is not in the pass,
 *       however the existing buffer may be displayed in lieu; if doing the
 *       'blocky' (not 'sparkle') display the row should be displayed,
 *       otherwise treat as:
 *    png_row_skip: For interlaced images (only) the interlace pass has no data
 *       appropriate to this row, it should be skipped.
 *
 * In both of the two cases zstream.avail_in may be non-0, indicating that some
 * IDAT data at zstream.next_in remains to be consumed.  This data must be
 * preserved and preset at the next call to the function.
 *
 * The function may also call png_error if an unrecoverable error occurs.
 *
 * The caller passes in a callback function and parameter to be called when row
 * data is available.  The callback is called repeatedly for each row to handle
 * all the transformed row data.
 */
png_row_op /*PRIVATE*/
png_read_process_IDAT(png_structrp png_ptr, png_bytep transformed_row,
    png_bytep display_row, int save_row)
{
   /* Common sub-expressions.  These are all constant across the whole PNG, but
    * are recalculated here each time because this is fast and it only happens
    * once per row + once per block of input data.
    */
   const unsigned int max_pixels = png_max_pixel_block(png_ptr);
   const unsigned int pixel_depth = png_ptr->row_input_pixel_depth;
   /* The number of input bytes read each time (cannot overflow because it is
    * limited by PNG_ROW_BUFFER_SIZE):
    */
   const unsigned int input_byte_count = (max_pixels * pixel_depth) / 8U;
   const unsigned int bpp = (pixel_depth+0x7U)>>3;
   const png_uint_32 width = png_ptr->width;
   const unsigned int interlaced = png_ptr->interlaced != PNG_INTERLACE_NONE;

   png_uint_32 row_number = png_ptr->row_number;
   unsigned int pass = png_ptr->pass;
   enum anonymous {
      start_of_row     = 0U, /* at the start of the row; read a filter byte */
      need_row_bytes   = 2U, /* reading the row */
      processing_row   = 3U  /* control returned to caller to process the row */
   } state = png_upcast(enum anonymous, png_ptr->row_state);

   /* The caller is responsible for calling png_read_start_IDAT: */
   affirm(png_ptr->zowner == png_IDAT);

   /* Basic sanity checks: */
   affirm(pixel_depth > 0U && pixel_depth <= 64U &&
         input_byte_count <= PNG_ROW_BUFFER_SIZE &&
         pixel_depth <= 8U*PNG_MAX_PIXEL_BYTES);

   for (;;) switch (state)
   {
      png_alloc_size_t row_bytes_processed;
      png_alloc_size_t bytes_read;  /* bytes in pixel_buffer */
      png_uint_32      pass_width;
      png_byte         row_filter;
      union
      {
         PNG_ROW_BUFFER_ALIGN_TYPE force_buffer_alignment;
         png_byte buffer[16U];
      }  previous_pixels;
      union
      {
         PNG_ROW_BUFFER_ALIGN_TYPE force_buffer_alignment;
         png_byte buffer[PNG_ROW_BUFFER_SIZE];
      }  pixel_buffer;

      case need_row_bytes:
         /* The above variables need to be restored: */
         row_bytes_processed = png_ptr->row_bytes_read;
         bytes_read = row_bytes_processed % input_byte_count;
         row_bytes_processed -= bytes_read;

         pass_width = width;
         if (interlaced)
            pass_width = PNG_PASS_COLS(pass_width, pass);

         memcpy(pixel_buffer.buffer, png_ptr->scratch, bytes_read);
         memcpy(previous_pixels.buffer, png_ptr->scratch+bytes_read, 2*bpp);
         row_filter = png_ptr->scratch[bytes_read+2*bpp];

         goto pixel_loop;

      case processing_row:
         /* When there was a previous row (not at the start of the image) the
          * row number needs to be updated and, possibly, the pass number.
          */
         if (++row_number == png_ptr->height)
         {
            affirm(interlaced && pass < 6); /* else too many calls */

            /* Start a new pass: there never is a pending filter byte so it
             * is always necessary to read the filter byte of the next row.
             */
            png_ptr->pass = ++pass & 0x7;
            row_number = 0U;
         } /* end of pass */

         png_ptr->row_number = row_number;

         /* This is a new row, but it may not be in the pass data so it
          * may be possible to simply return control to the caller to
          * skip it or use the previous row as appropriate.
          */
         if (interlaced)
         {
            debug(pass <= 6);

            /* This macro cannot overflow because the PNG width (and height)
             * have already been checked to ensure that they are less than
             * 2^31 (i.e. they are 31-bit values, not 32-bit values.)
             */
            pass_width = PNG_PASS_COLS(width, pass);

            /* On average most rows are skipped, so do this first: */
            if (pass_width == 0 ||
                !PNG_ROW_IN_INTERLACE_PASS(row_number, pass))
            {
               /* Using the PNG specification numbering (pass+1), passes 1,
                * 2, 4, 6 contribute to all the rows in 'block' interlaced
                * filling mode.  Pass 3 contributes to four rows (5,6,7,8),
                * pass 5 to two rows (3,4 then 7,8) and pass 7 only to one
                * (the one on which it is processed).  have_row must be set
                * appropriately; it is set when a row is processed (end of
                * this function) and remains set while the 'block' mode of
                * interlace handling should reuse the previous row for this
                * row.
                *
                * Each pass row can be used in a fixed number of rows, shown
                * in 'rows' below, the '*' indicates that the row is actually
                * in the pass, the '^' that the previous '*' row is used in
                * block display update and the '@' that the pass doesn't
                * contribte at all to that row in block display mode:
                *
                * PASS:  0   1   2   3   4   5   6
                * rows:  8   8   4   4   2   2   1
                *    0:  *   *   @   *   @   *   @
                *    1:  ^   ^   @   ^   @   ^   *
                *    2:  ^   ^   @   ^   *   *   @
                *    3:  ^   ^   @   ^   ^   ^   *
                *    4:  ^   ^   *   *   @   *   @
                *    5:  ^   ^   ^   ^   @   ^   *
                *    6:  ^   ^   ^   ^   *   *   @
                *    7:  ^   ^   ^   ^   ^   ^   *
                *
                * The '@' signs are the interesting thing, since we know that
                * this row isn't present in the pass data.  Rewriting the
                * above table with '1' for '@', little endian (i.e. row 0 at
                * the LSB end):
                *
                * row:    76543210
                * Pass 0: 00000000 0x00 [bit 3, 0x8 of row unset (always)]
                * Pass 1: 00000000 0x00
                * Pass 2: 00001111 0x0F [bit 2, 0x4 of row unset]
                * Pass 3: 00000000 0x00
                * Pass 4: 00110011 0x33 [bit 1, 0x2 of row unset]
                * Pass 5: 00000000 0x00
                * Pass 6: 01010101 0x55 [bit 0, 0x1 of row unset]
                *
                * PNG_PASS_BLOCK_SKIP(pass, row) can be written two ways;
                *
                * As a shift and a mask:
                *    (0x55330F00 >> ((pass >> 1) + (row & 7))) & ~pass & 1
                *
                * And, somewhat simpler, as a bit check on the low bits of
                * row:
                *
                *    ~((row) >> (3-(pass >> 1))) & ~pass & 1
                */
#              define PNG_PASS_BLOCK_SKIP(pass, row)\
                  (~((row) >> (3U-((pass) >> 1))) & ~(pass) & 0x1U)

               /* Hence: */
               debug(png_ptr->row_state == processing_row);
               return pass_width == 0 || PNG_PASS_BLOCK_SKIP(pass,
                     row_number) ? png_row_skip : png_row_repeat;
            } /* skipped row */

            /* processed; fall through to start_of_row */
         } /* interlaced */

         /* FALL THROUGH */
      case start_of_row:
         {
            /* Read the filter byte for the next row, previous_pixels is just
             * used as a temporary buffer; it is reset below.
             */
            png_alloc_size_t cb = png_inflate_IDAT(png_ptr, 0/*finish*/,
                  previous_pixels.buffer, 1U);

            /* This can be temporary; it verifies the invariants on how
             * png_inflate_IDAT updates the {next,avail}_out fields:
             */
#ifndef __COVERITY__  /* Suppress bogus Coverity complaint */
            debug(png_ptr->zstream.avail_out == 1-cb &&
                  png_ptr->zstream.next_out == cb + previous_pixels.buffer);
#endif

            /* next_out points to previous_pixels, for security do this: */
            png_ptr->zstream.next_out = NULL;
            png_ptr->zstream.avail_out = 0U;

            /* One byte, so we either got it or have to get more input data: */
            if (cb != 1U)
            {
               affirm(cb == 0U && png_ptr->zstream.avail_in == 0U);
               png_ptr->row_state = start_of_row;
               return png_row_incomplete;
            }
         }

         /* Check the filter byte. */
         row_filter = previous_pixels.buffer[0];
         if (row_filter >= PNG_FILTER_VALUE_LAST)
            png_chunk_error(png_ptr, "invalid PNG filter");

         /* These are needed for the filter check below: */
         pass_width = width;
         if (interlaced)
            pass_width = PNG_PASS_COLS(pass_width, pass);

         /* The filter is followed by the row data, but first check the
          * filter byte; the spec requires that we invent an empty row
          * if the first row of a pass requires it.
          *
          * Note that row_number is the image row.
          */
         if (row_number == PNG_PASS_START_ROW(pass)) switch (row_filter)
         {
            case PNG_FILTER_VALUE_UP:
               /* x-0 == x, so do this optimization: */
               row_filter = PNG_FILTER_VALUE_NONE;
               break;

            case PNG_FILTER_VALUE_PAETH:
               /* The Paeth predictor is always the preceding (leftwards)
                * value, so this is the same as sub:
                */
               row_filter = PNG_FILTER_VALUE_SUB;
               break;

            case PNG_FILTER_VALUE_AVG:
               /* It would be possible to 'invent' a new filter that did
                * AVG using only the previous byte; it's 'SUB' of half the
                * preceding value, but this seems pointless.  Zero out the
                * row buffer to make AVG work.
                */
               memset(png_ptr->row_buffer, 0U,
                        PNG_ROWBYTES(pixel_depth, pass_width));
               break;

            default:
               break;
         } /* switch row_filter */

         /* Always zero the 'previous pixel' out at the start of a row; this
          * allows the filter code to ignore the row start.
          */
         memset(previous_pixels.buffer, 0U, sizeof previous_pixels.buffer);

         row_bytes_processed = 0U;
         bytes_read = 0U;

      pixel_loop:
         /* At this point the following must be set correctly:
          *
          *    row_bytes_processed: bytes processed so far
          *    pass_width:          width of a row in this pass in pixels
          *    pixel_depth:         depth in bits of a pixel
          *    bytes_read:          count of filtered bytes in pixel_buffer
          *    row_filter:          filter byte for this row
          *    previous_pixels[0]:  pixel 'a' for the filter
          *    previous_pixels[1]:  pixel 'c' for the filter
          *    pixel_buffer[]:      bytes_read filtered bytes
          *
          * The code in the loop decompresses PNG_ROW_BUFFER_SIZE filterd pixels
          * from the input, unfilters and transforms them, then saves them to
          * png_struct::row_buffer[row_bytes_processed...].
          */
         { /* pixel loop */
            const png_alloc_size_t row_bytes =
               PNG_ROWBYTES(pixel_depth, pass_width);
            png_bytep row_buffer = png_ptr->row_buffer + row_bytes_processed;
            unsigned int pixels;
            png_uint_32 x;

            /* Sanity check for potential buffer overwrite: */
            affirm(row_bytes > row_bytes_processed);

            /* Work out the current pixel index of the pixel at the start of the
             * row buffer:
             */
            switch (pixel_depth)
            {
               case 1U: x = (png_uint_32)/*SAFE*/(row_bytes_processed << 3);
                        break;
               case 2U: x = (png_uint_32)/*SAFE*/(row_bytes_processed << 2);
                        break;
               case 4U: x = (png_uint_32)/*SAFE*/(row_bytes_processed << 1);
                        break;
               case 8U: x = (png_uint_32)/*SAFE*/row_bytes_processed;
                        break;
               default: x = (png_uint_32)/*SAFE*/(row_bytes_processed / bpp);
                        debug(row_bytes_processed % bpp == 0U);
                        break;
            }

            for (pixels = max_pixels; x < pass_width; x += pixels)
            {
               if (pixels > pass_width - x)
                  pixels = (unsigned int)/*SAFE*/(pass_width - x);

               /* At the end of the image pass Z_FINISH to zlib to optimize the
                * final read (very slightly, is this worth doing?)  To do this
                * work out if we are at the end.
                */
               {
                  const png_uint_32 height = png_ptr->height;

                  /* last_pass_row indicates that this is the last row in this
                   * pass (the test is optimized for the non-interlaced case):
                   */
                  const int last_pass_row = row_number+1 >= height ||
                     (interlaced && PNG_LAST_PASS_ROW(row_number,pass,height));

                  /* Set 'finish' if this is the last row in the last pass of
                   * the image:
                   */
                  const int finish = last_pass_row && (!interlaced ||
                        pass >= PNG_LAST_PASS(width, height));

                  const png_alloc_size_t bytes_to_read =
                     PNG_ROWBYTES(pixel_depth, pixels);

                  png_alloc_size_t cb;

                  affirm(bytes_to_read > bytes_read);
                  cb = png_inflate_IDAT(png_ptr, finish,
                        pixel_buffer.buffer + bytes_read,
                        bytes_to_read - bytes_read);
                  bytes_read += cb;

                  if (bytes_read < bytes_to_read)
                  {
                     /* Fewer bytes were read than needed: we need to stash all
                      * the information required at pixel_loop in png_struct so
                      * that the need_row_bytes case can restore it when more
                      * input is available.
                      */
                     debug(png_ptr->zstream.avail_in == 0U);
                     png_ptr->zstream.next_out = NULL;
                     png_ptr->zstream.avail_out = 0U;

                     png_ptr->row_bytes_read = row_bytes_processed + bytes_read;
                     memcpy(png_ptr->scratch, pixel_buffer.buffer, bytes_read);
                     memcpy(png_ptr->scratch+bytes_read, previous_pixels.buffer,
                           2*bpp);
                     png_ptr->scratch[bytes_read+2*bpp] = row_filter;
                     png_ptr->row_state = need_row_bytes;
                     return png_row_incomplete;
                  }

                  debug(bytes_read == bytes_to_read);
               } /* fill pixel_buffer */

               /* The buffer is full or the row is complete but the calculation
                * was done using the pixel count, so double check against the
                * byte count here:
                */
               implies(bytes_read != input_byte_count,
                  bytes_read == row_bytes - row_bytes_processed);

               /* At this point all the required information to process the next
                * block of pixels in the row has been read from the input stream
                * and the original, filtered, row data is held in pixel_buffer.
                *
                * Because the buffer will be transformed after the unfilter
                * operation we require whole pixels:
                */
               debug(bytes_read >= bpp && bytes_read % bpp == 0);

               if (row_filter > PNG_FILTER_VALUE_NONE)
               {
                  /* This is checked in the read code above: */
                  debug(row_filter < PNG_FILTER_VALUE_LAST);

                  /* Lazy init of the read functions, which allows hand crafted
                   * optimizations for 'bpp' (which does not change.)
                   */
                  if (png_ptr->read_filter[0] == NULL)
                     png_init_filter_functions(png_ptr, bpp);

                  /* Pixels 'a' then 'c' are in previous_pixels, pixel 'b' is in
                   * row_buffer and pixel 'x' (filtered) is in pixel_buffer.
                   */
                  png_ptr->read_filter[row_filter-1](bytes_read, bpp,
                     pixel_buffer.buffer, row_buffer, previous_pixels.buffer);
               } /* do the filter */

               /* Now pixel_buffer.buffer contains the *un*filtered bytes of the
                * current row and row_buffer needs updating with these.  First
                * preserve pixels 'a' and 'c' for the next time round the loop
                * (if necessary).
                */
               if (bytes_read < row_bytes - row_bytes_processed)
               {
                  debug(bytes_read == input_byte_count);
                  memcpy(previous_pixels.buffer/* pixel 'a' */,
                        pixel_buffer.buffer + bytes_read - bpp, bpp);
                  memcpy(previous_pixels.buffer+bpp/* pixel 'c' */,
                        row_buffer + bytes_read - bpp, bpp);
               }

               /* Now overwrite the previous row pixels in row_buffer with the
                * current row pixels:
                */
               memcpy(row_buffer, pixel_buffer.buffer, bytes_read);
               row_buffer += bytes_read;
               row_bytes_processed += bytes_read;
               bytes_read = 0U; /* for next buffer */

               /* Any transforms can now be performed along with any output
                * handling (copy or interlace handling).
                */
#              ifdef PNG_TRANSFORM_MECH_SUPPORTED
                  if (png_ptr->transform_list != NULL)
                  {
                     unsigned int max_depth;
                     png_transform_control tc;

                     png_init_transform_control(&tc, png_ptr);

                     tc.width = pixels;
                     tc.sp = tc.dp = pixel_buffer.buffer;

                     /* Run the list.  It is ok if it doesn't end up doing
                      * anything; this can happen with a lazy init.
                      *
                      * NOTE: if the only thing in the list is a palette check
                      * function it can remove itself at this point.
                      */
                     max_depth = png_run_transform_list_forwards(png_ptr, &tc);

                     /* This is too late, a stack overwrite has already
                      * happened, but it may still prevent exploits:
                      */
                     affirm(max_depth <= png_ptr->row_max_pixel_depth);

                     /* It is very important that the transform produces the
                      * same pixel format as the TC_INIT steps:
                      */
                     affirm(png_ptr->row_format == tc.format &&
                        png_ptr->row_range == tc.range &&
                        png_ptr->row_bit_depth == tc.bit_depth);
#                    ifdef PNG_READ_GAMMA_SUPPORTED
                        /* This checks the output gamma taking into account the
                         * fact that small gamma changes are eliminated.
                         */
                        debug(png_ptr->row_gamma == tc.gamma ||
                              png_gamma_check(png_ptr, &tc));
#                    endif /* READ_GAMMA */

                     /* If the caller needs the row saved (for the progressive
                      * read API) or if this PNG is interlaced and this row may
                      * be required in a subsequent pass (any pass before the
                      * last one) then it is stored in
                      * png_struct::transformed_row, and that may need to be
                      * allocated here.
                      */
#                    if defined(PNG_PROGRESSIVE_READ_SUPPORTED) ||\
                        defined(PNG_READ_INTERLACING_SUPPORTED)
                        if (png_ptr->transform_list != NULL &&
                            (save_row
#                            ifdef PNG_READ_INTERLACING_SUPPORTED
                              || (png_ptr->do_interlace && pass < 6U)
#                            endif /* READ_INTERLACING */
                            ))
                        {
                           if (png_ptr->transformed_row == NULL)
                              png_ptr->transformed_row = png_voidcast(png_bytep,
                                 png_malloc(png_ptr, png_calc_rowbytes(png_ptr,
                                    png_ptr->row_bit_depth *
                                       PNG_FORMAT_CHANNELS(png_ptr->row_format),
                                    save_row ? width : (width+1U)>>1)));

                           copy_row(png_ptr, png_ptr->transformed_row,
                              pixel_buffer.buffer, x, pixels, 1/*clear*/);
                        }
#                    endif /* PROGRESSIVE_READ || READ_INTERLACING */
                  } /* transform_list != NULL */
#              endif /* TRANSFORM_MECH */

               /* There are now 'pixels' possibly transformed pixels starting at
                * row pixel x, where 'x' is an index in the interlaced row if
                * interlacing is happening.  Handle this row.
                */
               if (transformed_row != NULL)
                  combine_row(png_ptr, transformed_row, pixel_buffer.buffer,
                        x, pixels, 0/*!display*/);

               if (display_row != NULL)
                  combine_row(png_ptr, display_row, pixel_buffer.buffer, x,
                        pixels, 1/*display*/);
            } /* for x < pass_width */
         } /* pixel loop */

         png_ptr->row_state = processing_row;
         return png_row_process;

      default:
         impossible("bad row state");
   } /* forever switch */

   PNG_UNUSED(save_row) /* May not be used above */
}

void /* PRIVATE */
png_read_free_row_buffers(png_structrp png_ptr)
{
   /* The transformed row only gets saved if needed: */
#  if (defined(PNG_PROGRESSIVE_READ_SUPPORTED) ||\
       defined(PNG_READ_INTERLACING_SUPPORTED)) &&\
      defined(PNG_TRANSFORM_MECH_SUPPORTED)
      if (png_ptr->transformed_row != NULL)
      {
         png_free(png_ptr, png_ptr->transformed_row);
         png_ptr->transformed_row = NULL;
      }
#  endif /* PROGRESSIVE_READ || READ_INTERLACING */

   if (png_ptr->row_buffer != NULL)
   {
      png_free(png_ptr, png_ptr->row_buffer);
      png_ptr->row_buffer = NULL;
   }
}

/* Complete reading of the IDAT chunks.  This returns 0 if more data is to
 * be read, 1 if the zlib stream has terminated.  Call this routine with
 * zstream.avail_in greater than zero unless there is no more input data.
 * When zstream_avail_in is 0 on entry and the stream does not terminate
 * an "IDAT truncated" error will be output.
 */
int /* PRIVATE */
png_read_finish_IDAT(png_structrp png_ptr)
{
   enum
   {
      no_error = 0,
      LZ_too_long,
      IDAT_too_long,
      IDAT_truncated
   }  error = no_error;

   /* Release the rowd buffers first; they can use considerable amounts of
    * memory.
    */
   png_read_free_row_buffers(png_ptr);

   affirm(png_ptr->zowner == png_IDAT); /* else this should not be called */

   /* We don't need any more data and the stream should have ended, however the
    * LZ end code may actually not have been processed.  In this case we must
    * read it otherwise stray unread IDAT data or, more likely, an IDAT chunk
    * may still remain to be consumed.
    */
   if (!png_ptr->zstream_ended)
   {
      int end_of_IDAT = png_ptr->zstream.avail_in == 0;
      png_byte b[1];
      png_alloc_size_t cb = png_inflate_IDAT(png_ptr, 2/*finish*/, b, 1);

      debug(png_ptr->zstream.avail_out == 1-cb &&
            png_ptr->zstream.next_out == cb + b);

      /* As above, for safety do this: */
      png_ptr->zstream.next_out = NULL;
      png_ptr->zstream.avail_out = 0;

      /* No data is expected, either compressed or in the IDAT: */
      if (cb != 0)
         error = LZ_too_long;

      else if (png_ptr->zstream.avail_in == 0 /* && cb == 0 */)
      {
         /* This is the normal case but there may still be some waiting codes
          * (including the adler32 that follow the LZ77 end code; so we can
          * have at least 5 bytes after the end of the row data before the
          * end of the stream.
          */
         if (!png_ptr->zstream_ended)
         {
            if (!end_of_IDAT)
               return 0; /* keep reading, no detectable error yet */

            error = IDAT_truncated;
         }

         /* Else there may still be an error; too much IDAT, but we can't
          * tell.
          */
      }
   }

   /* If there is still pending zstream input then there was too much IDAT
    * data:
    */
   if (!error && png_ptr->zstream.avail_in > 0)
      error = IDAT_too_long;

   /* Either this is the success case or an error has been detected and
    * warned about.
    */
   {
      int ret = inflateEnd(&png_ptr->zstream);

      /* In fact we expect this to always succeed, so it is a good idea to
       * catch it in pre-release builds:
       */
      debug_handled(ret == Z_OK);

      if (ret != Z_OK)
      {
         /* This is just a warning; it's safe, and the zstream_error flag is
          * not set.
          */
         png_zstream_error(&png_ptr->zstream, ret);
         png_chunk_warning(png_ptr, png_ptr->zstream.msg);
      }
   }

   /* Output an error message if required: */
   if (error && !png_ptr->zstream_error)
   {
      switch (error)
      {
         case LZ_too_long:
            png_benign_error(png_ptr, "compressed data too long");
            break;

         case IDAT_too_long:
            png_benign_error(png_ptr, "uncompressed data too long");
            break;

         case IDAT_truncated:
            png_benign_error(png_ptr, "data truncated");
            break;

         default:
         case no_error: /* Satisfy the compiler */
            break;
      }

      png_ptr->zstream_error = 1;
   }

   /* WARNING: leave {next,avail}_in set here, the progressive reader uses these
    * to complete the PNG chunk CRC calculation.
    */
   png_ptr->zstream_ended = 1;
   png_ptr->zowner = 0;

   return 1; /* end of stream */
}

/* Optional call to update the users info_ptr structure, can be used from both
 * the progressive and sequential reader, but the app must call it.
 */
void PNGAPI
png_read_update_info(png_structrp png_ptr, png_inforp info_ptr)
{
   png_debug(1, "in png_read_update_info");

   if (png_ptr != NULL)
   {
      if (png_ptr->zowner != png_IDAT)
      {
         png_read_start_IDAT(png_ptr);

#        ifdef PNG_READ_TRANSFORMS_SUPPORTED
            png_read_transform_info(png_ptr, info_ptr);
#        else
            PNG_UNUSED(info_ptr)
#        endif
      }

      /* New in 1.6.0 this avoids the bug of doing the initializations twice */
      else
         png_app_error(png_ptr,
            "png_read_update_info/png_start_read_image: duplicate call");
   }
}

png_int_32 /* PRIVATE */
png_read_setting(png_structrp png_ptr, png_uint_32 setting,
    png_uint_32 parameter, png_int_32 value)
{
   /* Caller checks the arguments for basic validity */
   int only_get = (setting & PNG_SF_GET) != 0U;

   if (only_get) /* spurious: in case it isn't used */
      setting &= ~PNG_SF_GET;

   switch (setting)
   {
#     ifdef PNG_SEQUENTIAL_READ_SUPPORTED
         case PNG_SR_COMPRESS_buffer_size:
            if (parameter > 0 && parameter <= ZLIB_IO_MAX)
            {
               png_ptr->IDAT_size = parameter;
               return 0; /* Cannot return a 32-bit value */
            }

            else
               return PNG_EINVAL;
#     endif /* SEQUENTIAL_READ */

#     ifdef PNG_READ_GAMMA_SUPPORTED
         case PNG_SR_GAMMA_threshold:
            if (parameter <= 0xFFFF)
            {
               if (!only_get)
                  png_ptr->gamma_threshold = PNG_UINT_16(parameter);

               return (png_int_32)/*SAFE*/parameter;
            }

            return PNG_EDOM;

#if 0 /*NYI*/
         case PNG_SR_GAMMA_accuracy:
            if (parameter <= 1600)
            {
               if (!only_get)
                  png_ptr->gamma_accuracy = parameter;

               return (png_int_32)/*SAFE*/parameter;
            }

            return PNG_EDOM;
#endif /*NYI*/
#     endif /* READ_GAMMA */

      case PNG_SR_CRC_ACTION:
         /* Tell libpng how we react to CRC errors in critical chunks */
         switch (parameter)
         {
            case PNG_CRC_NO_CHANGE:    /* Leave setting as is */
               break;

            case PNG_CRC_WARN_USE:     /* Warn/use data */
               png_ptr->critical_crc = crc_warn_use;
               break;

            case PNG_CRC_QUIET_USE:    /* Quiet/use data */
               png_ptr->critical_crc = crc_quiet_use;
               break;

            default:
            case PNG_CRC_WARN_DISCARD: /* Not valid for critical data */
               return PNG_EINVAL;

            case PNG_CRC_ERROR_QUIT:   /* Error/quit */
            case PNG_CRC_DEFAULT:
               png_ptr->critical_crc = crc_error_quit;
               break;
         }

         /* Tell libpng how we react to CRC errors in ancillary chunks */
         switch (value)
         {
            case PNG_CRC_NO_CHANGE:    /* Leave setting as is */
               break;

            case PNG_CRC_WARN_USE:     /* Warn/use data */
               png_ptr->ancillary_crc = crc_warn_use;
               break;

            case PNG_CRC_QUIET_USE:    /* Quiet/use data */
               png_ptr->ancillary_crc = crc_quiet_use;
               break;

            case PNG_CRC_ERROR_QUIT:   /* Error/quit */
               png_ptr->ancillary_crc = crc_error_quit;
               break;

            case PNG_CRC_WARN_DISCARD: /* Warn/discard data */
            case PNG_CRC_DEFAULT:
               png_ptr->ancillary_crc = crc_warn_discard;
               break;

            default:
               return PNG_EINVAL;
         }

         return 0; /* success */

#     ifdef PNG_SET_OPTION_SUPPORTED
         case PNG_SRW_OPTION:
            switch (parameter)
            {
               case PNG_MAXIMUM_INFLATE_WINDOW:
                  if (png_ptr->maximum_inflate_window)
                  {
                     if (!value && !only_get)
                        png_ptr->maximum_inflate_window = 0U;
                     return PNG_OPTION_ON;
                  }

                  else
                  {
                     if (value && !only_get)
                        png_ptr->maximum_inflate_window = 1U;
                     return PNG_OPTION_OFF;
                  }

               default:
                  return PNG_OPTION_UNSET;
            }
#     endif /* SET_OPTION */

#     ifdef PNG_READ_CHECK_FOR_INVALID_INDEX_SUPPORTED
         case PNG_SRW_CHECK_FOR_INVALID_INDEX:
            /* The 'enabled' value is a FORTRAN style three-state: */
            if (value > 0)
               png_ptr->palette_index_check = PNG_PALETTE_CHECK_ON;

            else if (value < 0)
               png_ptr->palette_index_check = PNG_PALETTE_CHECK_OFF;

            else
               png_ptr->palette_index_check = PNG_PALETTE_CHECK_DEFAULT;

            return 0;
#     endif /* READ_CHECK_FOR_INVALID_INDEX */

#     ifdef PNG_BENIGN_READ_ERRORS_SUPPORTED
         case PNG_SRW_ERROR_HANDLING:
            /* The parameter is a bit mask of what to set, the value is what to
             * set it to.
             */
            if (value >= PNG_IGNORE && value <= PNG_ERROR &&
                parameter <= PNG_ALL_ERRORS)
            {
               if ((parameter & PNG_BENIGN_ERRORS) != 0U)
                  png_ptr->benign_error_action = value & 0x3U;

               if ((parameter & PNG_APP_WARNINGS) != 0U)
                  png_ptr->app_warning_action = value & 0x3U;

               if ((parameter & PNG_APP_ERRORS) != 0U)
                  png_ptr->app_error_action = value & 0x3U;

               if ((parameter & PNG_IDAT_ERRORS) != 0U)
                  png_ptr->IDAT_error_action = value & 0x3U;

               return 0;
            }

            return PNG_EINVAL;
#     endif /* BENIGN_READ_ERRORS */

      default:
         return PNG_ENOSYS; /* not supported (whatever it is) */
   }
}
#endif /* READ */
