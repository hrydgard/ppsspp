#ifdef _MSC_VER
#pragma warning (disable:4018)
#pragma warning (disable:4028)
#pragma warning (disable:4146)
#pragma warning (disable:4334)
#endif

/* pngwutil.c - utilities to write a PNG file
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
#define PNG_SRC_FILE PNG_SRC_FILE_pngwutil

#ifdef PNG_WRITE_SUPPORTED

#ifdef PNG_WRITE_INT_FUNCTIONS_SUPPORTED
/* Place a 32-bit number into a buffer in PNG byte order.  We work
 * with unsigned numbers for convenience, although one supported
 * ancillary chunk uses signed (two's complement) numbers.
 */
void PNGAPI
png_save_uint_32(png_bytep buf, png_uint_32 i)
{
   buf[0] = PNG_BYTE(i >> 24);
   buf[1] = PNG_BYTE(i >> 16);
   buf[2] = PNG_BYTE(i >> 8);
   buf[3] = PNG_BYTE(i);
}

/* Place a 16-bit number into a buffer in PNG byte order.
 * The parameter is declared unsigned int, not png_uint_16,
 * just to avoid potential problems on pre-ANSI C compilers.
 */
void PNGAPI
png_save_uint_16(png_bytep buf, unsigned int i)
{
   buf[0] = PNG_BYTE(i >> 8);
   buf[1] = PNG_BYTE(i);
}
#endif /* WRITE_INT_FUNCTIONS */

/* Simple function to write the signature.  If we have already written
 * the magic bytes of the signature, or more likely, the PNG stream is
 * being embedded into another stream and doesn't need its own signature,
 * we should call png_set_sig_bytes() to tell libpng how many of the
 * bytes have already been written.
 */
void PNGAPI
png_write_sig(png_structrp png_ptr)
{
   png_byte png_signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};

#ifdef PNG_IO_STATE_SUPPORTED
   /* Inform the I/O callback that the signature is being written */
   png_ptr->io_state = PNG_IO_WRITING | PNG_IO_SIGNATURE;
#endif

   /* Write the rest of the 8 byte signature */
   png_write_data(png_ptr, &png_signature[png_ptr->sig_bytes],
       (png_size_t)(8 - png_ptr->sig_bytes));

   if (png_ptr->sig_bytes < 3)
      png_ptr->mode |= PNG_HAVE_PNG_SIGNATURE;
}

/* Write the start of a PNG chunk.  The type is the chunk type.
 * The total_length is the sum of the lengths of all the data you will be
 * passing in png_write_chunk_data().
 */
static void
png_write_chunk_header(png_structrp png_ptr, png_uint_32 chunk_name,
    png_uint_32 length)
{
   png_byte buf[8];

#if defined(PNG_DEBUG) && (PNG_DEBUG > 0)
   PNG_CSTRING_FROM_CHUNK(buf, chunk_name);
   png_debug2(0, "Writing %s chunk, length = %lu", buf, (unsigned long)length);
#endif

   if (png_ptr == NULL)
      return;

#ifdef PNG_IO_STATE_SUPPORTED
   /* Inform the I/O callback that the chunk header is being written.
    * PNG_IO_CHUNK_HDR requires a single I/O call.
    */
   png_ptr->io_state = PNG_IO_WRITING | PNG_IO_CHUNK_HDR;
#endif

   /* Write the length and the chunk name */
   png_save_uint_32(buf, length);
   png_save_uint_32(buf + 4, chunk_name);
   png_write_data(png_ptr, buf, 8);

   /* Put the chunk name into png_ptr->chunk_name */
   png_ptr->chunk_name = chunk_name;

   /* Reset the crc and run it over the chunk name */
   png_reset_crc(png_ptr, buf+4);

#ifdef PNG_IO_STATE_SUPPORTED
   /* Inform the I/O callback that chunk data will (possibly) be written.
    * PNG_IO_CHUNK_DATA does NOT require a specific number of I/O calls.
    */
   png_ptr->io_state = PNG_IO_WRITING | PNG_IO_CHUNK_DATA;
#endif
}

void PNGAPI
png_write_chunk_start(png_structrp png_ptr, png_const_bytep chunk_string,
    png_uint_32 length)
{
   png_write_chunk_header(png_ptr, PNG_CHUNK_FROM_STRING(chunk_string), length);
}

/* Write the data of a PNG chunk started with png_write_chunk_header().
 * Note that multiple calls to this function are allowed, and that the
 * sum of the lengths from these calls *must* add up to the total_length
 * given to png_write_chunk_header().
 */
void PNGAPI
png_write_chunk_data(png_structrp png_ptr, png_const_voidp data,
    png_size_t length)
{
   /* Write the data, and run the CRC over it */
   if (png_ptr == NULL)
      return;

   if (data != NULL && length > 0)
   {
      png_write_data(png_ptr, data, length);

      /* Update the CRC after writing the data,
       * in case the user I/O routine alters it.
       */
      png_calculate_crc(png_ptr, data, length);
   }
}

/* Finish a chunk started with png_write_chunk_header(). */
void PNGAPI
png_write_chunk_end(png_structrp png_ptr)
{
   png_byte buf[4];

   if (png_ptr == NULL) return;

#ifdef PNG_IO_STATE_SUPPORTED
   /* Inform the I/O callback that the chunk CRC is being written.
    * PNG_IO_CHUNK_CRC requires a single I/O function call.
    */
   png_ptr->io_state = PNG_IO_WRITING | PNG_IO_CHUNK_CRC;
#endif

   /* Write the crc in a single operation */
   png_save_uint_32(buf, png_ptr->crc);

   png_write_data(png_ptr, buf, (png_size_t)4);
}

/* Write a PNG chunk all at once.  The type is an array of ASCII characters
 * representing the chunk name.  The array must be at least 4 bytes in
 * length, and does not need to be null terminated.  To be safe, pass the
 * pre-defined chunk names here, and if you need a new one, define it
 * where the others are defined.  The length is the length of the data.
 * All the data must be present.  If that is not possible, use the
 * png_write_chunk_start(), png_write_chunk_data(), and png_write_chunk_end()
 * functions instead.
 */
static void
png_write_complete_chunk(png_structrp png_ptr, png_uint_32 chunk_name,
    png_const_voidp data, png_size_t length)
{
   if (png_ptr == NULL)
      return;

   /* On 64 bit architectures 'length' may not fit in a png_uint_32. */
   if (length > PNG_UINT_31_MAX)
      png_error(png_ptr, "length exceeds PNG maximum");

   png_write_chunk_header(png_ptr, chunk_name, (png_uint_32)/*SAFE*/length);
   png_write_chunk_data(png_ptr, data, length);
   png_write_chunk_end(png_ptr);
}

/* This is the API that calls the internal function above. */
void PNGAPI
png_write_chunk(png_structrp png_ptr, png_const_bytep chunk_string,
    png_const_voidp data, png_size_t length)
{
   png_write_complete_chunk(png_ptr, PNG_CHUNK_FROM_STRING(chunk_string), data,
       length);
}

static png_alloc_size_t
png_write_row_buffer_size(png_const_structrp png_ptr)
   /* Returns the width of the widest pass in the first row of an interlaced
    * image.  Passes in the first row are: 0.5.3.5.1.5.3.5, so the widest row is
    * normally the one from pass 5.  The only exception is if the image is only
    * one pixel wide, so:
    */
#define PNG_FIRST_ROW_MAX_WIDTH(w) (w > 1U ? PNG_PASS_COLS(w, 5U) : 1U)

   /* For interlaced images the count of pixels is rounded up to a the number of
    * pixels in the first pass (numbered 0).  This ensures that passes before
    * the last can be packed in the buffer without overflow.
    */
{
   png_alloc_size_t w;

   /* If the image is interlaced adjust 'w' for the interlacing: */
   if (png_ptr->interlaced != PNG_INTERLACE_NONE)
   {
      /* Take advantage of the fact that 1-row interlaced PNGs require half the
       * normal row width:
       */
      if (png_ptr->height == 1U) /* no pass 6 */
         w = PNG_FIRST_ROW_MAX_WIDTH(png_ptr->width);

      /* Otherwise round up to a multiple of 8.  This may waste a few (less
       * than 8) bytes for PNGs with a height less than 57 but this hardly
       * matters.
       */
      else
         w = (png_ptr->width + 7U) & ~7U;
   }

   else
      w = png_ptr->width;

   /* The rounding above may leave 'w' exactly 2^31 */
   debug(w <= 0x80000000U);

   switch (png_ptr->row_output_pixel_depth)
   {
      /* This would happen if the function is called before png_write_IHDR. */
      default: NOT_REACHED; return 0;

      case 1:  w = (w+7) >> 3; break;
      case 2:  w = (w+3) >> 2; break;
      case 4:  w = (w+1) >> 1; break;
      case 8:  break;
      case 16: w <<= 1; break; /* overflow: w is set to 0, which is OK */

         /* For the remaining cases the answer is w*bytes; where bytes is 3,4,6
          * or 8.  This may overflow 32 bits.  There is no way to compute the
          * result on an arbitrary platform, so test the maximum of a (size_t)
          * against w for each possible byte depth:
          */
#     define CASE(b)\
         case b*8:\
            if (w <= (PNG_SIZE_MAX/b)/*compile-time constant*/)\
               return w * b;\
            return 0;

      CASE(3)
      CASE(4)
      CASE(6)
      CASE(8)

#     undef CASE
   }

   /* This is the low bit depth case.  The following can never be false on
    * systems with a 32-bit or greater size_t:
    */
   if (w <= PNG_SIZE_MAX)
      return w;

   return 0U;
}

/* Release memory used by the deflate mechanism */
static void
png_deflateEnd(png_const_structrp png_ptr, z_stream *zs, int check)
{
   if (zs->state != NULL)
   {
      int ret = deflateEnd(zs);

      /* Z_DATA_ERROR means there was pending output. */
      if ((ret != Z_OK && (check || ret != Z_DATA_ERROR)) || zs->state != NULL)
      {
         png_zstream_error(zs, ret);

         if (check)
            png_error(png_ptr, zs->msg);

         else
            png_warning(png_ptr, zs->msg);

         zs->state = NULL;
      }
   }
}

/* compression_buffer (new in 1.6.0) is just a linked list of temporary buffers. * From 1.6.0 it is retained in png_struct so that it will be correctly freed in
 * the event of a write error (previous implementations just leaked memory.)
 *
 * From 1.7.0 the size is fixed to the same as the (uncompressed) row buffer
 * size.  This avoids allocating a large chunk of memory when compressing small
 * images.  This type is also opaque outside this file.
 */
typedef struct png_compression_buffer
{
   struct png_compression_buffer *next;
   png_byte                       output[PNG_ROW_BUFFER_SIZE];
} png_compression_buffer, *png_compression_bufferp;

/* png_compression_buffer methods */
/* Deleting a compression buffer deletes the whole list: */
static void
png_free_compression_buffer(png_const_structrp png_ptr,
    png_compression_bufferp *listp)
{
   png_compression_bufferp list = *listp;

   if (list != NULL)
   {
      *listp = NULL;

      do
      {
         png_compression_bufferp next = list->next;

         png_free(png_ptr, list);
         list = next;
      }
      while (list != NULL);
   }
}

/* Return the next compression buffer in the list, allocating it if necessary.
 * The caller must update 'end' if required; this just moves down the list.
 */
static png_compression_bufferp
png_get_compression_buffer(png_const_structrp png_ptr,
    png_compression_bufferp *end)
{
   png_compression_bufferp next = *end;

   if (next == NULL)
   {
      next = png_voidcast(png_compression_bufferp, png_malloc_base(png_ptr,
               sizeof *next));

      /* Check for OOM: this is a recoverable error for non-critical chunks, let
       * the caller decide what to do rather than issuing a png_error here.
       */
      if (next != NULL)
      {
         next->next = NULL; /* initialize the buffer */
         *end = next;
      }
   }

   return next; /* may still be NULL on OOM */
}

/* This structure is used to hold all the data for zlib compression of a single
 * stream of data.  It may be re-used, it stores the compressed data internally
 * and can handle arbitrary input and output.
 *
 * 'list' is the output data contained in compression buffers, 'end' points to
 * list at the start and is advanced down the compression buffer list (extending
 * it as required) as the data is written.  If 'end' points into a compression
 * buffer (does not point to 'list') that is the buffer in use in
 * z_stream::{next,avail}_out.
 *
 * Compression may be performed in multiple steps, '*end' always points to the
 * compression buffer *after* the one that is in use, so 'end' is pointing
 * *into* the one in use.
 *
 *    end(on entry) .... end ....... end(on exit)
 *          |             |                |
 *          |             |                |
 *          V        +----V-----+    +-----V----+    +----------+
 *         list ---> |   next --+--> |   next --+--> |   next   |
 *                   | output[] |    | output[] |    | output[] |
 *                   +----------+    +----------+    +----------+
 *                                     [in use]        [unused]
 *
 * These invariants should always hold:
 *
 * 1) If zs.state is NULL decompression is not in progress, list may be non-NULL
 *    but end could be anything;
 *
 * 2) Otherwise if zs.next_out is NULL list will be NULL and end will point at
 *    list, len, overflow and start will be 0;
 *
 * 3) Otherwise list is non-NULL and end points at the 'next' element of an
 *    in-use compression buffer.  zs.next_out points into the 'output' element
 *    of the same buffer.  {overflow, len} is the amount of compressed data, len
 *    being the low 31 bits, overflow being the higher bits.  start is used for
 *    writing and is the index of the first byte in list->output to write,
 *    {overflow, len} does not include start.
 */
typedef struct
{
   z_stream                 zs;       /* zlib compression data */
   png_compression_bufferp  list;     /* Head of the buffer list */
   png_compression_bufferp *end;      /* Pointer to last 'next' pointer */
   png_uint_32              len;      /* Bottom 31 bits of data length */
   unsigned int             overflow; /* Top bits of data length */
   unsigned int             start;    /* Start of data in first block */
}  png_zlib_compress, *png_zlib_compressp;

/* png_zlib_compress methods */
/* Initialize the compress structure.  The z_stream itself is not initialized,
 * however the the 'user' fields are set, including {next,avail}_{in,out}.  The
 * initialization does not change 'list', however it does set 'end' to point to
 * it, effectively truncating the list.
 */
static void
png_zlib_compress_init(png_structrp png_ptr, png_zlib_compressp pz)
{
   /* png_zlib_compress z_stream: */
   pz->zs.zalloc = png_zalloc;
   pz->zs.zfree = png_zfree;
   /* NOTE: this does not destroy 'restrict' because in all the functions herein
    * *png_ptr is only ever accessed via *either* pz->zs.opaque *or* a passed in
    * png_ptr.
    */
   pz->zs.opaque = png_ptr;

   pz->zs.next_in = NULL;
   pz->zs.avail_in = 0U;
   pz->zs.total_in = 0U;

   pz->zs.next_out = NULL;
   pz->zs.avail_out = 0U;
   pz->zs.total_out = 0U;

   pz->zs.msg = PNGZ_MSG_CAST("zlib success"); /* safety */

   /* pz->list preserved */
   pz->end = &pz->list;
   pz->len = 0U;
   pz->overflow = 0U;
   pz->start = 0U;
}

/* Return the png_ptr: this is defined here for all the remaining
 * png_zlib_compress methods because they are only ever called with zs
 * initialized.
 */
#define png_ptr png_voidcast(png_const_structrp, pz->zs.opaque)

#if PNG_RELEASE_BUILD
#  define png_zlib_compress_validate(pz, in_use) ((void)0)
#else /* !RELEASE_BUILD */
static void
png_zlib_compress_validate(png_zlib_compressp pz, int in_use)
{
   const uInt o_size = sizeof pz->list->output;

   affirm(pz->end != NULL && (in_use || (pz->zs.next_in == NULL &&
               pz->zs.avail_in == 0U && *pz->end == NULL)));

   if (pz->overflow == 0U && pz->len == 0U && pz->start == 0U) /* empty */
   {
      affirm((pz->end == &pz->list && pz->zs.next_out == NULL
              && pz->zs.avail_out == 0U) ||
             (pz->list != NULL && pz->end == &pz->list->next &&
              pz->zs.next_out == pz->list->output &&
              pz->zs.avail_out == o_size));
   }

   else /* not empty */
   {
      png_compression_bufferp *ep = &pz->list, list;
      png_uint_32 o, l;

      affirm(*ep != NULL && pz->zs.next_out != NULL);

      /* Check the list length: */
      o = pz->overflow;
      l = pz->len;
      affirm((l & 0x80000000U) == 0U && (o & 0x80000000U) == 0U);

      do
      {
         list = *ep;
         l -= o_size;
         if (l & 0x80000000U) --o, l &= 0x7FFFFFFFU;
         ep = &list->next;
      }
      while (ep != pz->end);

      l += pz->start;
      l += pz->zs.avail_out;
      if (l & 0x80000000U) ++o, l &= 0x7FFFFFFFU;

      affirm(o == 0U && l == 0U && pz->zs.next_out >= list->output &&
             pz->zs.next_out + pz->zs.avail_out == list->output + o_size);
   }
}
#endif /* !RELEASE_BUILD */

/* Destroy one zlib compress structure. */
static void
png_zlib_compress_destroy(png_zlib_compressp pz, int check)
{
   /* If the 'opaque' pointer is NULL this png_zlib_compress was never
    * initialized, so do nothing.
    */
   if (png_ptr != NULL)
   {
      if (pz->zs.state != NULL)
      {
         if (check)
            png_zlib_compress_validate(pz, 0/*in_use*/);

         png_deflateEnd(png_ptr, &pz->zs, check);
      }

      pz->end = &pz->list; /* safety */
      png_free_compression_buffer(png_ptr, &pz->list);
   }
}

/* Ensure that space is available for output, returns the amount of space
 * available, 0 on OOM.  This updates pz->zs.avail_out (etc) as required.
 */
static uInt
png_zlib_compress_avail_out(png_zlib_compressp pz)
{
   uInt avail_out = pz->zs.avail_out;

   png_zlib_compress_validate(pz, 1/*in_use*/);

   if (avail_out == 0U)
   {
      png_compression_bufferp next;

      affirm(pz->end == &pz->list || (pz->end != NULL && pz->list != NULL));
      next = png_get_compression_buffer(png_ptr, pz->end);

      if (next != NULL)
      {
         pz->zs.next_out = next->output;
         pz->zs.avail_out = avail_out = sizeof next->output;
         pz->end = &next->next;
      }

      /* else return 0: OOM */
   }

   else
      affirm(pz->end != NULL && pz->list != NULL);

   return avail_out;
}

/* Compress the given data given an initialized png_zlib_compress structure.
 * This may be called multiple times, interleaved with writes as required.
 *
 * The input data is passed in in pz->zs.next_in, however the length of the data
 * is in 'input_len' (to avoid the zlib uInt limit) and pz->zs.avail_in is
 * overwritten (and left at 0).
 *
 * The output information is used and the amount of compressed data is added on
 * to pz->{overflow,len}.
 *
 * If 'limit' is a limit on the amount of data to add to the output (not the
 * total amount).  The function will retun Z_BUF_ERROR if the limit is reached
 * and the function will never produce more (additional) compressed data than
 * the limit.
 *
 * All of zstream::next_in[input] is consumed if a success code is returned
 * (Z_OK or Z_STREAM_END if flush is Z_FINISH), otherwise next_in may be used to
 * determine how much was compressed.
 *
 * pz->overflow is not checked for overflow, so if 'limit' is not set overflow
 * is possible.  The caller must guard against this when supplying a limit of 0.
 */
static int
png_compress(
   png_zlib_compressp pz,
   png_alloc_size_t input_len,   /* Length of data to be compressed */
   png_uint_32 limit,            /* Limit on amount of compressed data made */
   int flush)                    /* Flush parameter at end of input */
{
   const int unlimited = (limit == 0U);

   /* Sanity checking: */
   affirm(pz->zs.state != NULL &&
          (pz->zs.next_out == NULL
           ? pz->end == &pz->list && pz->len == 0U && pz->overflow == 0U
           : pz->list != NULL && pz->end != NULL));
   implies(pz->zs.next_out == NULL, pz->zs.avail_out == 0);

   for (;;)
   {
      uInt extra;

      /* OUTPUT: make sure some space is available: */
      if (png_zlib_compress_avail_out(pz) == 0U)
         return Z_MEM_ERROR;

      /* INPUT: limit the deflate call input to ZLIB_IO_MAX: */
      /* Adjust the input counters: */
      {
         uInt avail_in = ZLIB_IO_MAX;

         if (avail_in > input_len)
            avail_in = (uInt)/*SAFE*/input_len;

         input_len -= avail_in;
         pz->zs.avail_in = avail_in;
      }

      if (!unlimited && pz->zs.avail_out > limit)
      {
         extra = (uInt)/*SAFE*/(pz->zs.avail_out - limit); /* unused bytes */
         pz->zs.avail_out = (uInt)/*SAFE*/limit;
         limit = 0U;
      }

      else
      {
         extra = 0U;
         limit -= pz->zs.avail_out; /* limit >= 0U */
      }

      pz->len += pz->zs.avail_out; /* maximum that can be produced */

      /* Compress the data */
      {
         int ret = deflate(&pz->zs, input_len > 0U ? Z_NO_FLUSH : flush);

         /* Claw back input data that was not consumed (because avail_in is
          * reset above every time round the loop) and correct the output
          * length.
          */
         input_len += pz->zs.avail_in;
         pz->zs.avail_in = 0; /* safety */
         pz->len -= pz->zs.avail_out;

         if (pz->len & 0x80000000U)
            ++pz->overflow, pz->len &= 0x7FFFFFFFU;

         limit += pz->zs.avail_out;
         pz->zs.avail_out += extra;

         /* Check the error code: */
         switch (ret)
         {
            case Z_OK:
               if (pz->zs.avail_out > extra)
               {
                  /* zlib had output space, so all the input should have been
                   * consumed:
                   */
                  affirm(input_len == 0U /* else unexpected stop */ &&
                         flush != Z_FINISH/* ret != Z_STREAM_END */);
                  return Z_OK;
               }

               else
               {
                  /* zlib ran out of output space, produce some more.  If the
                   * limit is 0 at this point, however, no more space is
                   * available.
                   */
                  if (unlimited || limit > 0U)
                     break; /* Allocate more output */

                  /* No more output space available, but the input may have all
                   * been consumed.
                   */
                  if (input_len == 0U && flush != Z_FINISH)
                     return Z_OK;

                  /* Input all consumed, but insufficient space to flush the
                   * output; this is the Z_BUF_ERROR case.
                   */
                  return Z_BUF_ERROR;
               }

            case Z_STREAM_END:
               affirm(input_len == 0U && flush == Z_FINISH);
               return Z_STREAM_END;

            case Z_BUF_ERROR:
               /* This means that we are flushing all the output; expect
                * avail_out and input_len to be 0.
                *
                * NOTE: if png_compress is called with input_len 0 and flush set
                * to Z_NO_FLUSH this affirm will fire because zlib will have no
                * work to do.
                */
               affirm(input_len == 0U && pz->zs.avail_out == extra);
               /* Allocate another buffer */
               break;

            default:
               /* An error */
               return ret;
         }
      }
   }
}

#undef png_ptr /* remove definition using a png_zlib_compressp */

/* All the compression state is held here, it is allocated when required.  This
 * ensures that the read code doesn't carry the overhead of the much less
 * frequently used write stuff.
 *
 * TODO: make png_create_write_struct allocate this stuff after the main
 * png_struct.
 */
struct filter_selector; /* Used only for filter selection */

typedef struct png_zlib_state
{
   png_zlib_compress        s;       /* Primary compression state */
   png_compression_bufferp  stash;   /* Unused compression buffers */

#  define ps_png_ptr(ps) png_upcast(png_const_structrp, (ps)->s.zs.opaque)
      /* A png_ptr, used below in functions that only have a png_zlib_state.
       * NOTE: the png_zlib_compress must have been initialized!
       */

   png_uint_32 zlib_max_pixels;
      /* Maximum number of pixels that zlib can handle at once; the lesser of
       * the PNG maximum and the maximum that will fit in (uInt)-1 bytes.  This
       * number of pixels may not be byte aligned.
       */
   png_uint_32 zlib_max_aligned_pixels;
      /* The maximum number of pixels that zlib can handle while maintaining a
       * buffer byte alignment of PNG_ROW_BUFFER_BYTE_ALIGN; <= the previous
       * value.
       */

   png_alloc_size_t write_row_size;
      /* Size of the PNG row (without the filter byte) in bytes or 0 if it is
       * too large to be cached.
       */

#  ifdef PNG_WRITE_FILTER_SUPPORTED
      /* During write libpng needs the previous row when writing a new row with
       * up, avg or paeth and one or more image rows when performing filter
       * selection.  So if performing filter selection typically two or more
       * rows are required while if no filter selection is to be done only the
       * previous row pointer is required.
       */
      png_bytep        previous_write_row; /* Last row written, if any */
#     ifdef PNG_SELECT_FILTER_SUPPORTED
         png_bytep     current_write_row;  /* Row being written */
         struct filter_selector *selector; /* Data for filter selection */
         png_uint_32  filter_select_window;
            /* The number of bytes of uncompressed PNG data which are assumed to
             * be relevant when doing filter selection.  Limited to 8453377
             * (about 2^23); the maximum number of bytes that can be encoded in
             * the largest deflate window.
             */
#        define PNG_FILTER_SELECT_WINDOW_MAX 8453377U
         png_byte filter_select_threshold;
            /* If the number of distinct codes seen in the PNG data are below
             * this threshold the PNG data will not be filtered (if the 'none'
             * filter is allowed).  If this is still true and a particular
             * filter does not add new codes that filter will be used.
             */
         png_byte filter_select_threshold2;
            /* If the number of distinct codes that result by using a particular
             * filter is below this second threshold that filter will be used.
             * (When multiple filters pass this criterion the lowest numbered
             * one producing the lowest number of new codes will be
             * chosen.)
             */
#     endif /* SELECT_FILTER */

      unsigned int row_buffer_max_pixels;
         /* The maximum number of pixels that can fit in PNG_ROW_BUFFER_SIZE
          * bytes; not necessary a whole number of bytes.
          */
      unsigned int row_buffer_max_aligned_pixels;
         /* The maximum number of pixels that can fit in PNG_ROW_BUFFER_SIZE
          * bytes while maintaining PNG_ROW_BUFFER_BYTE_ALIGN alignment.
          */

      unsigned int filter_mask :8; /* mask of filters to consider on NEXT row */
#     define PREVIOUS_ROW_FILTERS\
         (PNG_FILTER_UP|PNG_FILTER_AVG|PNG_FILTER_PAETH)
      unsigned int filters     :8; /* Filters for current row */
      unsigned int save_row    :2; /* As below: */
#     define SAVE_ROW_UNSET   0U
#     define SAVE_ROW_OFF     1U /* Previous-row filters will be ignored */
#     define SAVE_ROW_DEFAULT 2U /* Default to save rows set by libpng */
#     define SAVE_ROW_ON      3U /* Force rows to be saved */
#     define SAVE_ROW(ps) ((ps)->save_row >= SAVE_ROW_DEFAULT)
#  endif /* WRITE_FILTER */

   /* Compression settings: see below for how these are encoded. */
   png_uint_32 pz_IDAT;    /* Settings for the image */
   png_uint_32 pz_iCCP;    /* Settings for iCCP chunks */
   png_uint_32 pz_text;    /* Settings for text chunks */
   png_uint_32 pz_current; /* Last set settings */

#  ifdef PNG_WRITE_FLUSH_SUPPORTED
      png_uint_32   flush_dist; /* how many rows apart to flush, 0 - no flush */
      png_uint_32   flush_rows; /* number of rows written since last flush */
#  endif /* WRITE_FLUSH */
}  png_zlib_state;

/* Create the zlib state: */
static void
png_create_zlib_state(png_structrp png_ptr)
{
   png_zlib_statep ps = png_voidcast(png_zlib_state*,
         png_malloc(png_ptr, sizeof *ps));

   /* Clear to NULL/0: */
   memset(ps, 0, sizeof *ps);

   debug(png_ptr->zlib_state == NULL);
   png_ptr->zlib_state = ps;
   png_zlib_compress_init(png_ptr, &ps->s);

#  ifdef PNG_WRITE_FILTER_SUPPORTED
      ps->previous_write_row = NULL;
#     ifdef PNG_SELECT_FILTER_SUPPORTED
         ps->current_write_row = NULL;
         ps->selector = NULL;
#     endif /* SELECT_FILTER */
#  endif /* WRITE_FILTER */
#  ifdef PNG_WRITE_FLUSH_SUPPORTED
      /* Set this to prevent flushing by making it larger than the number
       * of rows in the largest interlaced PNG; PNG_UINT_31_MAX times
       * (1/8+1/8+1/8+1/4+1/4+1/2+1/2); 1.875, or 15/8
       */
      ps->flush_dist = 0xEFFFFFFFU;
#  endif /* WRITE_FLUSH */
}

static void
png_zlib_state_set_buffer_limits(png_const_structrp png_ptr, png_zlib_statep ps)
   /* Delayed initialization of the zlib state maxima; this is not done above in
    * case the zlib_state is created before the IHDR has been written, which
    * would lead to the various png_struct fields used below being
    * uninitialized.
    */
{
   /* Initialization of the buffer size constants. */
   const unsigned int bpp = PNG_PIXEL_DEPTH(*png_ptr);
   const unsigned int byte_pp = bpp >> 3; /* May be 0 */
   const unsigned int pixel_block =
      /* Number of pixels required to maintain PNG_ROW_BUFFER_BYTE_ALIGN
       * alignment.  For multi-byte pixels use the first set bit to determine
       * if the pixels have a greater alignment already.
       */
      bpp < 8U ?
         PNG_ROW_BUFFER_BYTE_ALIGN * (8U/bpp) :
         PNG_ROW_BUFFER_BYTE_ALIGN <= (byte_pp & -byte_pp) ?
            1U :
            PNG_ROW_BUFFER_BYTE_ALIGN / (byte_pp & -byte_pp);

   /* pixel_block must always be a power of two: */
   debug(bpp > 0 && pixel_block > 0 &&
         (pixel_block & -pixel_block) == pixel_block &&
         ((8U*PNG_ROW_BUFFER_BYTE_ALIGN-1U) & (pixel_block*bpp)) == 0U);

   /* Zlib maxima */
   {
      png_uint_32 max = (uInt)-1; /* max bytes */

      if (bpp <= 8U)
      {
         /* Maximum number of bytes PNG can generate in the lower bit depth
          * cases:
          */
         png_uint_32 png_max =
            (0x7FFFFFFF + PNG_ADDOF(bpp)) >> PNG_SHIFTOF(bpp);

         if (png_max < max)
            max = 0x7FFFFFFF;
      }

      else /* bpp > 8U */
      {
         max /= byte_pp;
         if (max > 0x7FFFFFFF)
            max = 0x7FFFFFFF;
      }

      /* So this is the maximum number of pixels regardless of alignment: */
      ps->zlib_max_pixels = max;

      /* For byte alignment the value has to be a multiple of pixel_block and
       * that is a power of 2, so:
       */
      ps->zlib_max_aligned_pixels = max & ~(pixel_block-1U);
   }

#  ifdef PNG_WRITE_FILTER_SUPPORTED
      /* PNG_ROW_BUFFER maxima; this is easier because PNG_ROW_BUFFER_SIZE is
       * limited so that the number of bits fits in any ANSI-C (unsigned int).
       */
      {
         const unsigned int max = (8U * PNG_ROW_BUFFER_SIZE) / bpp;

         ps->row_buffer_max_pixels = max;
         ps->row_buffer_max_aligned_pixels = max & ~(pixel_block-1U);
      }
#  endif /* WRITE_FILTER */

   /* NOTE: this will be 0 for very long rows on 32-bit or less systems */
   ps->write_row_size = png_write_row_buffer_size(png_ptr);
}

static png_zlib_statep
get_zlib_state(png_structrp png_ptr)
{
   if (png_ptr->zlib_state == NULL)
      png_create_zlib_state(png_ptr);

   return png_ptr->zlib_state;
}

/* Internal API to clean up all the deflate related stuff, including the buffer
 * lists.
 */
static void /* PRIVATE */
png_deflate_release(png_structrp png_ptr, png_zlib_statep ps, int check)
{
#  ifdef PNG_WRITE_FILTER_SUPPORTED
      /* Free any mode-specific data that is owned here: */
      if (ps->previous_write_row != NULL)
      {
         png_bytep p = ps->previous_write_row;
         ps->previous_write_row = NULL;
         png_free(png_ptr, p);
      }

#     ifdef PNG_SELECT_FILTER_SUPPORTED
         if (ps->current_write_row != NULL)
         {
            png_bytep p = ps->current_write_row;
            ps->current_write_row = NULL;
            png_free(png_ptr, p);
         }

         if (ps->selector != NULL)
         {
            struct filter_selector *s = ps->selector;
            ps->selector = NULL;
            png_free(png_ptr, s);
         }
#     endif /* SELECT_FILTER */
#  endif /* WRITE_FILTER */

   /* The main z_stream opaque pointer needs to remain set to png_ptr; it is
    * only set once.
    */
   png_zlib_compress_destroy(&ps->s, check);
   png_free_compression_buffer(png_ptr, &ps->stash);
}

void /* PRIVATE */
png_deflate_destroy(png_structrp png_ptr)
{
   png_zlib_statep ps = png_ptr->zlib_state;

   if (ps != NULL)
   {
      png_deflate_release(png_ptr, ps, 0/*check*/);
      png_ptr->zlib_state = NULL;
      png_free(png_ptr, ps);
   }
}

/* Compression settings.
 *
 * These are stored packed into a png_uint_32 to make comparison with the
 * current setting quick.  The packing method uses four bits for each setting
 * and reserves '0' for unset.
 *
 * ps_<setting>_base:   The lowest valid value (encoded as 1).
 * ps_<setting>_max:    The highest valid value.
 * ps_<setting>_pos:    The position in the range 0..3 (shift of 0..12).
 *
 * The low 16 bits are the zlib compression parameters:
 */
#define pz_level_base      (-1)
#define pz_level_max         9
#define pz_level_pos         0
#define pz_windowBits_base   8
#define pz_windowBits_max   15
#define pz_windowBits_pos    1
#define pz_memLevel_base     1
#define pz_memLevel_max      9
#define pz_memLevel_pos      2
#define pz_strategy_base     0
#define pz_strategy_max      4
#define pz_strategy_pos      3
#define pz_zlib_bits    0xFFFFU
/* Anything below this is not used directly by zlib: */
#define pz_png_level_base    0
#define pz_png_level_max     6
#define pz_png_level_pos     4

#define pz_offset(name)     (pz_ ## name ## _base - 1)
   /* setting_value == pz_offset(setting)+encoded_value */
#define pz_min(name)        pz_ ## name ## _base
#define pz_max(name)        pz_ ## name ## _max
#define pz_shift(name)      (4 * pz_ ## name ## _pos)

#define pz_bits(name,x)     ((int)(((x)>>pz_shift(name))&0xF))
   /* the encoded value, or 0 if unset */

/* Enquiries: */
#define pz_isset(name,x)    (pz_bits(name,x) != 0)
#define pz_value(name,x)    (pz_bits(name,x)+pz_offset(name))

/* Assignments: */
#define pz_clear(name,x)    ((x)&~((png_uint_32)0xFU<<pz_shift(name)))
#define pz_encode(name,v)   ((png_uint_32)((v)-pz_offset(name))<<pz_shift(name))
#define pz_change(name,x,v) (pz_clear(name,x) | pz_encode(name, v))

/* Direct use/modification: */
#define pz_var(ps, type)       ((ps)->pz_ ## type)
#define pz_get(ps, type, name, def)\
   (pz_isset(name, pz_var(ps, type)) ? pz_value(name, pz_var(ps, type)) : (def))
/* pz_assign checks for out-of-range values and clears the setting if these are
 * given.  No warning or error is generated.
 */
#define pz_assign(ps, type, name, value)\
   (pz_var(ps, type) = pz_clear(name, pz_var(ps, type)) |\
    ((value) >= pz_min(name) && (value) <= pz_max(name) ?\
     pz_encode(name, value) : 0))

static png_int_32
pz_compression_setting(png_structrp png_ptr, png_uint_32 owner,
      int min, int max, int shift, png_int_32 value, int only_get, int unset)
   /* This is a support function for png_write_setting below. */
{
   png_zlib_statep ps;
   png_uint_32p psettings;

   /* The value is only required for a 'set', eliminate out-of-range values
    * first:
    */
   if (!only_get && (value < min || value > max))
      return PNG_EDOM;

   /* If setting a value make sure the state exists: */
   if (!only_get)
      ps = get_zlib_state(png_ptr);

   else if (owner != 0U) /* ps may be NULL */
      ps = png_ptr->zlib_state;

   else /* get and owner is 0U */
      return 0; /* supported */

   psettings = NULL;
   switch (owner)
   {
      png_int_32 res;

      case png_IDAT:
         if (ps != NULL) psettings = &ps->pz_IDAT;
         break;

      case png_iCCP:
         if (ps != NULL) psettings = &ps->pz_iCCP;
         break;

      case 0U:
         /* All the settings.  At this point the 'get' case has returned 0
          * above, the value has been checked and the paramter is 0, therefore
          * valid.  Each of the following calls should succeed and it would be
          * reasonable to eliminate the PNG_FAILED tests in a world where
          * software engineers never made mistakes.
          */
         res = pz_compression_setting(png_ptr, png_IDAT, min, max, shift,
               value, 0/*set*/, 1/*iff unset*/);

         if (PNG_FAILED(res))
            return res;

         res = pz_compression_setting(png_ptr, png_iCCP, min, max, shift,
               value, 0/*set*/, 1/*iff unset*/);

         if (PNG_FAILED(res))
            return res;

         /* The text settings are changed regardless of the customize support
          * because if WRITE_CUSTOMIZE_ZTXT_COMPRESSION is not supported the old
          * behavior was to use the WRITE_CUSTOMIZE_COMPRESSION setting.
          *
          * However, when we get png_zTXt directly (from png_write_setting) and
          * the support is not compiled in return PNG_ENOSYS.
          */
         unset = 1; /* i.e. only if not already set */

#     ifdef PNG_WRITE_CUSTOMIZE_ZTXT_COMPRESSION_SUPPORTED
         case png_zTXt:
         case png_iTXt:
#     endif /* WRITE_CUSTOMIZE_ZTXT_COMPRESSION */
         if (ps != NULL) psettings = &ps->pz_text;
         break;

      default:
         /* Return PNG_ENOSYS, not PNG_EINVAL, to support future addition of new
          * compressed chunks and the fact that zTXt and iTXt customization can
          * be disabled.
          */
         return PNG_ENOSYS;
   }

   if (psettings == NULL)
      return PNG_UNSET; /* valid setting that is not set */

   {
      png_uint_32 settings = *psettings;
      png_uint_32 mask = 0xFU << shift;

      /* Do not set it if 'only_get' was passed in or if 'unset' is true and the
       * setting is not currently set:
       */
      if (!only_get && ((settings & mask) == 0U || !unset))
         *psettings = (settings & ~mask) +
            ((png_uint_32)/*SAFE*/(value-min+1) << shift);

      settings &= mask;

      if (settings == 0U)
         return PNG_UNSET;

      else
         return (int)/*SAFE*/((settings >> shift)-1U) + min;
   }
}

#define compression_setting(pp, owner, setting, value, get)\
   pz_compression_setting(pp, owner, pz_min(setting), pz_max(setting),\
         pz_shift(setting), value, get, 0/*always*/)

/* There is (as of zlib 1.2.8) a bug in the implementation of compression with a
 * window size of 256 which zlib works round by resetting windowBits from 8 to 9
 * whenever deflateInit2 is called with that value.  Fix this up here.
 */
static void
fix_cinfo(png_zlib_statep ps, png_bytep data, png_alloc_size_t data_size)
{
   /* Do this if the CINFO field is '1', meaning windowBits of 9.  The first
    * byte of the stream is the CMF value, CINFO is in the upper four bits.
    *
    * If zlib didn't futz with the value then it should match the value in
    * pz_current; check this is debug.  (See below for why this works in the
    * pz_default_settings call.)
    */
#  define png_ptr png_voidcast(png_const_structrp, ps->s.zs.opaque)
   if (data[0] == 0x18U &&
       pz_get(ps, current, windowBits, 0) == 8 /* i.e. it was requested */)
   {
      /* Double check this here; the fixup only works if the data was 256 bytes
       * or shorter *or* the window is never used.  For safety repeat the checks
       * done in pz_default_settings; technically we should be able to just skip
       * this test.
       *
       * TODO: set a 'fixup' flag in zlib_state to make this quicker?
       */
      if (data_size <= 256U ||
          pz_get(ps, current, strategy, Z_RLE) == Z_HUFFMAN_ONLY ||
          pz_get(ps, current, level, 1) == Z_NO_COMPRESSION)
      {
         unsigned int d1;

         data[0] = 0x08U;
         /* The header checksum must be fixed too.  The FCHECK (low 5 bits) make
          * CMF.FLG a multiple of 31:
          */
         d1 = data[1] & 0xE0U; /* top three bits */
         d1 += 31U - (0x0800U + d1) % 31U;
         data[1] = PNG_BYTE(d1);
      }

      else /* pz_default_settings is expected to guarantee the above */
         NOT_REACHED;
   }

   else if (data_size > 0U)
   {
      /* Prior to 1.7.0 libpng would shrink the windowBits even if the
       * application requested a particular value, so:
       */
      unsigned int z_cinfo = data[0] >> 4;
      unsigned int half_z_window_size = 1U << (z_cinfo + 7);

      if (data_size <= half_z_window_size && z_cinfo > 0)
      {
         unsigned int tmp;

         do
         {
            half_z_window_size >>= 1;
            --z_cinfo;
         }
         while (z_cinfo > 0 && data_size <= half_z_window_size);

         data[0] = PNG_BYTE((z_cinfo << 4) + 0x8U);
         tmp = data[1] & 0xE0U; /* top three bits */
         tmp += 31U - ((data[0] << 8) + tmp) % 31U;
         data[1] = PNG_BYTE(tmp);
      }
   }

   else
      NOT_REACHED; /* invalid data size (0) */
#  undef png_ptr
}

static png_uint_32
pz_default_settings(png_uint_32 settings, const png_uint_32 owner,
      const png_alloc_size_t data_size, const unsigned int filters/*for IDAT*/)
{
   int png_level, strategy, zlib_level, windowBits;

   /* The png 'level' parameter controls the defaults below.  It uses the same
    * numbering scheme as the Zlib compression level except that -1 invokes the
    * set of options and, in some cases, libpng behavior of libpng 1.6 and
    * earlier.
    *
    * In the comments below reference is made to the differences beteen the
    * legacy compression sizes from libpng 1.6 and earlier and the result of
    * using the various options.  These are quoted as an overall size change in
    * the compression of 147323 PNG test files.  The set of test files is
    * slightly restricted because pre-1.7 versions of png_read_png leave random
    * bits into the final byte of a row which ends with a partial byte.  This
    * affects the compression unpredictably so such files were omitted from the
    * measurements.
    */
   if (!pz_isset(png_level, settings))
   {
      png_level = PNG_DEFAULT_COMPRESSION_LEVEL;
      settings |= pz_encode(png_level, png_level);
   }

   else
      png_level = pz_value(png_level, settings);

   /* First default the strategy.  At lower data sizes other strategies do as
    * well as the zlib default compression strategy but they never seem to
    * improve on it with the 1.7 filtering.
    */
   if (!pz_isset(strategy, settings))
   {
      switch (png_level)
      {
         case PNG_COMPRESSION_COMPAT: /* Legacy setting */
            /* The pre-1.7 code used Z_FILTERED normally but uses
             * Z_DEFAULT_STRATEGY for palette or low-bit-depth images.
             *
             * In fact Z_DEFAULT_STRATEGY works best for filtered images as
             * well, however the change in results is small:
             *
             *    Z_DEFAULT_STRATEGY: -0.1%
             *    Z_FILTERED:         +0.1%
             *
             * NOTE: this happened even if WRITE_FILTER was *not* supported.
             */
            if (owner != png_IDAT || filters == PNG_FILTER_NONE)
               strategy = Z_DEFAULT_STRATEGY;

            else
               strategy = Z_FILTERED;
            break;

         case PNG_COMPRESSION_HIGH_SPEED:
            /* RLE is as fast as HUFFMAN_ONLY and can reduce size a lot in a few
             * cases.
             */
            strategy = Z_RLE;
            break;

         default: /* For GCC */
         case PNG_COMPRESSION_LOW:
         case PNG_COMPRESSION_MEDIUM:
            /* Z_FILTERED is almost as good as the default and can be
             * significantly faster. It biases the algorithm towards smaller
             * byte values.
             *
             * Using Z_DEFAULT_STRATEGY here, rather than Z_FILTERED, benefits
             * smaller 8 and 16-bit gray and larger 8 and 16-bit RGB images,
             * however the overall gain is only 0.1% because it is offset by
             * losses in larger 8-bit gray and alpha images.  It is extremely
             * difficult to deduce a pattern other than biases in the test set
             * of images.
             *
             * Looking at the pattern of behavior with the 1.6 filter selection
             * algorithm (none of palette or low-bit-depth, else all) produces
             * results as follows:
             */
            if (owner == png_IDAT)
            {
               if (filters == PNG_FILTER_NONE)
                  strategy = Z_DEFAULT_STRATEGY;

               else
                  strategy = Z_FILTERED;
            }

            else if (owner == png_iCCP)
               strategy = Z_DEFAULT_STRATEGY;

            /* TODO: investigate this, the observed behavior is suspicious: */
            else /* text chunk */
               strategy = Z_FILTERED; /* Always better for some reason */
            break;

         case PNG_COMPRESSION_LOW_MEMORY:
            /* Reduce memory at all costs, speed doesn't matter. */
         case PNG_COMPRESSION_HIGH_READ_SPEED:
         case PNG_COMPRESSION_HIGH:
            if (owner == png_IDAT || owner == png_iCCP)
               strategy = Z_DEFAULT_STRATEGY;

            else
               strategy = Z_FILTERED;
            break;
      }

      settings |= pz_encode(strategy, strategy);
   }

   else
      strategy = pz_value(strategy, settings);

   /* Next the zlib level; this just defaults to the png level, except that for
    * Huffman or RLE encoding the level setting for Zlib doesn't matter.
    */
   if (!pz_isset(level, settings))
   {
      switch (strategy)
      {
         case Z_HUFFMAN_ONLY:
         case Z_RLE:
            /* The 'level' doesn't make any significant difference to the
             * compression with these strategies; in a test set of about 3GByte
             * of PNG files the total compressed size changed under 20 bytes
             * with libpng 1.6!
             */
            zlib_level = 1;
            break;

         default: /* Z_FIXED, Z_FILTERED, Z_DEFAULT_STRATEGY */
            /* Everything that uses the window seems to show rapidly diminishing
             * returns above level 6 (at least with libpng 1.6).
             * Z_DEFAULT_COMPRESSION is, in fact, level 6 so Mark seems to
             * concur.  With libpng 1.6 the following results were obtained
             * using the full test set of files (including those with a partial
             * byte at the end of the row) and just varying the zlib level:
             *
             *  LEVEL SIZE(bytes) CHANGE TIME(s) CHANGE METRIC
             *    9   2550246600  -1.19%  1972   +227%   -77%
             *    8   2556675866  -0.94%  1215   +101%   -59%
             *    7   2572685552  -0.32%   679    +12%   -15%
             *    6   2581196708   0%      604      0%     0%
             *    5   2602831249  +0.84%   414    -30%   +87%
             *    4   2625206800  +1.71%   358    -40%  +153%
             *    3   2674752349  +3.62%   298    -50%  +303%
             *    2   2716261483  +5.23%   262    -56%  +537%
             *    1   2749875805  +6.53%   251    -57%  +662%
             *    0   7174488347           202    -66%
             *
             * The CHANGE columns express the change in compressed size
             * (positive is an increase; a decrease in compression) and time
             * (positive is an increase; an increase in time) relative to level
             * 6.  The METRIC column is a measure of the compression-per-second
             * relative to level 6; positive is an increase in
             * compression-per-second.
             *
             * The metric is derived by assuming the difference in time between
             * level 0 (which does no compression) and the level being
             * considered is spent doing the compression.  (Reasonable, since
             * only the level changed).  Just the inverse of the product of the
             * size and the time difference is a measure of compression per
             * second.  It can be seen that time dominates the metric;
             * compression only varies slightly (under 8%) across the level
             * range.
             */
            switch (png_level)
            {
               case PNG_COMPRESSION_COMPAT:
                  zlib_level = Z_DEFAULT_COMPRESSION; /* NOTE: -1 */
                  break;

               case PNG_COMPRESSION_HIGH_SPEED:
                  zlib_level = 1;
                  break;

               default: /* For GCC */
               case PNG_COMPRESSION_LOW:
                  zlib_level = 3;
                  break;

               case PNG_COMPRESSION_MEDIUM:
                  zlib_level = 6; /* Old default! */
                  break;

               case PNG_COMPRESSION_LOW_MEMORY:
               case PNG_COMPRESSION_HIGH_READ_SPEED:
               case PNG_COMPRESSION_HIGH:
                  zlib_level = 9;
                  break;
            }
            break;
      }

      settings |= pz_encode(level, zlib_level);
   }

   else
      zlib_level = pz_value(level, settings);

   /* Now default windowBits.  This is probably the most important of the
    * settings because it is pretty much the only one that affects decode
    * performance.  The smaller the better:
    */
   if (!pz_isset(windowBits, settings))
   {
      if (png_level == PNG_COMPRESSION_COMPAT/* Legacy */)
      {
         /* This is the libpng16 calculation (it is wrong; a misunderstanding of
          * what zlib actually requires!)
          *
          * Using the code below with the legacy choice of Z_FILTERED or
          * Z_DEFAULT_STRATEGY increases the size of the test files by only
          * 0.04%, however the settings below considerably reduce the windowBits
          * used potentially benefitting read code a lot.
          *
          * NOTE: the algorithm below was determined by experiment and
          * observation with the same set of test files; there is some
          * considerable possibility that a different set might show different
          * results.  Obtaining large, representative, test sets is both a
          * considerable amount of work and very error prone.  [JB 20160518]
          */
         windowBits = 15;

         if (data_size <= 16384U)
         {
            unsigned int half_window_size = 1U << (windowBits-1);

            while (data_size + 262U <= half_window_size)
            {
               half_window_size >>= 1;
               --windowBits;
            }
         }
      }

      /* The window size affects the memory used on both read and write but also
       * the time on write (but not normally read).  Handle the low memory
       * requirement first:
       */
      else if (zlib_level == Z_NO_COMPRESSION ||
               png_level == PNG_COMPRESSION_LOW_MEMORY)
         windowBits = 8;

      /* If the strategy has been set to something that doesn't benefit from
       * higher windowBits values take advantage of this.  Note that pz_value
       * returns an invalid value if pz_isset is false.
       *
       * The only png_level that affects this decision is HIGH_SPEED, because
       * a smaller windowBits should speed up the search, however the code above
       * chose zlib_level based on this so ignore that consideration and just
       * use zlib_level below.
       */
      else switch (strategy)
      {
         png_alloc_size_t test_size;

         case Z_HUFFMAN_ONLY:
            /* Use the minimum; the window doesn't get used */
            windowBits = 8;
            break;

         case Z_RLE:
            /* The longest length code is 258 bytes, the shortest string that
             * can achieve this is 259 bytes long; 259 copies of the same byte
             * which can be encoded as a code for the byte value then a string
             * of length 258 starting at the first byte.  So if the data is
             * longer than 256 bytes use '9' for the windowBits, otherwise use
             * 8:
             */
            if (data_size <= 256U)
               windowBits = 8;

            else
               windowBits = 9;
            break;

            /* By experiment using about 150,000 files the optimal windowBits
             * value across a range of files is somewhat less than implied by
             * the data size and depends on the zlib level and the strategy
             * used, the following values were determined by experiment using
             * those files:
             */
         case Z_FILTERED:
            /* The Z_FILTERED case changes suddenly at (zlib) level 4 to
             * benefit from looking at all the data:
             */
            if (zlib_level < 4 && zlib_level != Z_DEFAULT_COMPRESSION/*-1: 6*/)
               test_size = data_size / 8U;

            else
               test_size = data_size;

            goto check_test_size;

         case Z_FIXED:
            /* With the fixed Huffman tables better compression only ever comes
             * from looking for matches, so, logically:
             */
            test_size = data_size;
            goto check_test_size;

         default:
            /* The default algorithm always does better with a window smaller
             * than all the data and shows jumps at level 4 and level 8.  The
             * net effect with the test set of images is a very minor overall
             * improvement compared to the pre-1.7 calculation (data size +
             * 262).  The benefit is less than 0.01%, however smaller window
             * sizes reduce the memory zlib has to allocate in the decoder.
             */
            switch (zlib_level)
            {
               case 1: case 2: case 3:
                  test_size = data_size / 8U;
                  break;

               default: /* -1(Z_DEFAULT_COMPRESSION) == 6, 4..7 */
                  /* This includes, implicitly, ZLIB_NO_COMPRESSION, but that
                   * was eliminated in the 'if' above.
                   */
                  test_size = data_size / 4U;
                  break;

               case 8: case 9:
                  test_size = data_size / 3U;
                  break;
            }

            goto check_test_size;

         check_test_size:
            /* Find the smallest window that covers 'test_size' bytes, subject
             * to the constraint that if the actual data size is more than 256
             * bytes the minimum windowBits that can be supported is 9:
             */
            if (data_size <= 256U)
               windowBits = 8;

            else
               windowBits = 9;

            while (windowBits < 15 && (1U << windowBits) < test_size)
               ++windowBits;

            break;
      }

      settings |= pz_encode(windowBits, windowBits);
   }

   else
      windowBits = pz_value(windowBits, settings);

   /* zlib has a problem with 256 byte windows; 512 is used instead.
    * We can't work round this if the data size is more than 256 bytes and
    * the strategy actually uses the window (everything except huffman-only)
    * so fix the problem here.
    */
   if (windowBits == 8 && data_size > 256U && strategy != Z_HUFFMAN_ONLY &&
       zlib_level != Z_NO_COMPRESSION)
      settings = pz_change(windowBits, settings, 9);

   /* For memLevel this just increases the memory used but can help with the
    * Huffman code generation even to level 9 (the maximum), so just set the
    * max.  This affects memory used, not (apparently) compression speed so
    * the only relevant png_level is LOW_MEMORY.
    *
    * The legacy setting is '8'; this is the level that Zlib defaults to because
    * 16-bit iAPX86 systems could not handle '9'.  Because MAX_MEM_LEVEL is used
    * below this does not matter; zconf.h selects 8 or 9 as appropriate.
    *
    * In fact using '9' with the legacy settings increases the size of the test
    * set minutely; +0.007%.  This is hardly significant; 0.007% of the test
    * images equals 10 images.  (Nevertheless it is interesting, just as the
    * observation that decreasing windowBits can result in smaller compressed
    * sizes is interesting.)
    */
   if (!pz_isset(memLevel, settings))
   {
      int memLevel;

      switch (png_level)
      {
         case PNG_COMPRESSION_COMPAT:
            memLevel = 8;
            break;

         case PNG_COMPRESSION_LOW_MEMORY:
            memLevel = 1;
            break;

         default:
            memLevel = MAX_MEM_LEVEL/*from zconf.h*/;
            break;
      }

      settings |= pz_encode(memLevel, memLevel);
   }

   return settings;
}

/* This is used below to find the size of an image to pass to png_deflate_claim.
 * It returns 0 for images whose size would overflow a 32-bit integer or have
 * rows which cannot be allocated.
 */
static png_alloc_size_t
png_image_size(png_const_structrp png_ptr)
{
   /* The size returned here is limited to PNG_SIZE_MAX, if the size would
    * exceed that (or is close to exceeding that) 0 is returned.  See below for
    * a variant that limits the size of 0xFFFFFFFFU.
    */
   const png_alloc_size_t rowbytes = png_ptr->zlib_state->write_row_size;

   /* NON-INTERLACED: (1+rowbytes) * h
    * INTERLACED:     Each pixel is transmitted exactly once, so the size is
    *                 (rowbytes * h) + the count of filter bytes.  Each complete
    *                 block of 8 image rows generates at most 15 output rows
    *                 (less for narrow images), so the filter byte count is
    *                 at most (15*h/8)+14.  Because the original rows are split
    *                 extra byte passing may be introduced.  Account for this by
    *                 allowing an extra 1 byte per output row; that's two bytes
    *                 including the filer byte.
    *
    * So:
    *    NON-INTERLACED: (rowbytes * h) + h
    *    INTERLACED:     < (rowbytes * h) + 2*(15 * h/8) + 2*15
    *
    * Hence:
    */
   if (rowbytes != 0)
   {
      const png_uint_32 h = png_ptr->height;

      if (png_ptr->interlaced == PNG_INTERLACE_NONE)
      {
         const png_alloc_size_t limit = PNG_SIZE_MAX / h;

         /* On 16-bit systems the above might be 0, so: */
         if (rowbytes </*allow 1 for filter byte*/ limit)
            return (rowbytes+1U) * h;
      }

      else /* INTERLACED */
      {
         const png_uint_32 w = png_ptr->width;

         /* Interlacing makes the image larger because of the replication of
          * both the filter byte and the padding to a byte boundary.
          */
         png_alloc_size_t cb_base;
         int pass;

         for (cb_base=0, pass=0; pass<PNG_INTERLACE_ADAM7_PASSES; ++pass)
         {
            const png_uint_32 pass_w = PNG_PASS_COLS(w, pass);

            if (pass_w > 0)
            {
               const png_uint_32 pass_h = PNG_PASS_ROWS(h, pass);

               if (pass_h > 0)
               {
                  /* This is the number of bytes available for each row of this
                   * pass:
                   */
                  const png_alloc_size_t limit = (PNG_SIZE_MAX - cb_base)/pass_h;
                  /* This cannot overflow because if it did rowbytes would
                   * have been 0 above.
                   */
                  const png_alloc_size_t pass_bytes =
                     PNG_ROWBYTES(png_ptr->row_output_pixel_depth, pass_w);

                  if (pass_bytes </*allow 1 for filter byte*/ limit)
                     cb_base += (pass_bytes+1U) * pass_h;

                  else
                     return 0U; /* insufficient address space left */
               }
            }
         }

         return cb_base;
      }
   }

   /* Failure case: */
   return 0U;
}

/* Initialize the compressor for the appropriate type of compression. */
static png_zlib_statep
png_deflate_claim(png_structrp png_ptr, png_uint_32 owner,
    png_alloc_size_t data_size)
{
   png_zlib_statep ps = get_zlib_state(png_ptr);

   affirm(png_ptr->zowner == 0);

   {
      int ret; /* zlib return code */
      unsigned int filters = 0U;
      png_uint_32 settings;

      switch (owner)
      {
         case png_IDAT:
            debug(data_size == 0U);
            data_size = png_image_size(png_ptr);

            if (data_size == 0U)
               data_size = PNG_SIZE_MAX;

            settings = ps->pz_IDAT;
#           ifdef PNG_WRITE_FILTER_SUPPORTED
               filters = ps->filter_mask;
               debug(filters != 0U);
#           else /* !WRITE_FILTER */
               filters = PNG_FILTER_NONE;
#           endif /* !WRITE_FILTER */
            break;

         case png_iCCP:
            settings = ps->pz_iCCP;
            break;

         default: /* text chunk */
            settings = ps->pz_text;
            break;
      }

      settings = pz_default_settings(settings, owner, data_size, filters);

      /* Check against the previous initialized values, if any.  The relevant
       * settings are in the low 16 bits.
       */
      if (ps->s.zs.state != NULL &&
            ((settings ^ ps->pz_current) & pz_zlib_bits) != 0U)
         png_deflateEnd(png_ptr, &ps->s.zs, 0/*check*/);

      /* For safety clear out the input and output pointers (currently zlib
       * doesn't use them on Init, but it might in the future).
       */
      ps->s.zs.next_in = NULL;
      ps->s.zs.avail_in = 0;
      ps->s.zs.next_out = NULL;
      ps->s.zs.avail_out = 0;

      /* The length fields must be cleared too and the lists reset: */
      ps->s.overflow = ps->s.len = ps->s.start = 0U;

      if (ps->s.list != NULL) /* error in prior chunk writing */
      {
         debug(ps->stash == NULL);
         ps->stash = ps->s.list;
         ps->s.list = NULL;
      }

      ps->s.end = &ps->s.list;

      /* Now initialize if required, setting the new parameters, otherwise just
       * do a simple reset to the previous parameters.
       */
      if (ps->s.zs.state != NULL)
         ret = deflateReset(&ps->s.zs);

      else
         ret = deflateInit2(&ps->s.zs, pz_value(level, settings), Z_DEFLATED,
             pz_value(windowBits, settings), pz_value(memLevel, settings),
             pz_value(strategy, settings));

      ps->pz_current = settings;

      /* The return code is from either deflateReset or deflateInit2; they have
       * pretty much the same set of error codes.
       */
      if (ret == Z_OK && ps->s.zs.state != NULL)
         png_ptr->zowner = owner;

      else
      {
         png_zstream_error(&ps->s.zs, ret);
         png_error(png_ptr, ps->s.zs.msg);
      }
   }

   return ps;
}

#ifdef PNG_WRITE_COMPRESSED_TEXT_SUPPORTED /* includes iCCP */
/* Compress the block of data at the end of a chunk.  This claims and releases
 * png_struct::z_stream.  It returns the amount of data in the chunk list or
 * zero on error (a zlib stream always contains some bytes!)
 *
 * prefix_len is the amount of (uncompressed) data before the start of the
 * compressed data.  The routine will return 0 if the total of the compressed
 * data and the prefix exceeds PNG_UINT_MAX_31.
 *
 * NOTE: this function may not return; it only returns 0 if
 * png_chunk_report(PNG_CHUNK_WRITE_ERROR) returns (not the default).
 */
static int /* success */
png_compress_chunk_data(png_structrp png_ptr, png_uint_32 chunk_name,
    png_uint_32 prefix_len, png_const_voidp input, png_alloc_size_t input_len)
{
   /* To find the length of the output it is necessary to first compress the
    * input. The result is buffered rather than using the two-pass algorithm
    * that is used on the inflate side; deflate is assumed to be slower and a
    * PNG writer is assumed to have more memory available than a PNG reader.
    *
    * IMPLEMENTATION NOTE: the zlib API deflateBound() can be used to find an
    * upper limit on the output size, but it is always bigger than the input
    * size so it is likely to be more efficient to use this linked-list
    * approach.
    */
   png_zlib_statep ps = png_deflate_claim(png_ptr, chunk_name, input_len);

   affirm(ps != NULL);

   /* The data compression function always returns so that we can clean up. */
   ps->s.zs.next_in = PNGZ_INPUT_CAST(png_voidcast(const Bytef*, input));

   /* Use the stash, if available: */
   debug(ps->s.list == NULL);
   ps->s.list = ps->stash;
   ps->stash = NULL;

   {
      int ret = png_compress(&ps->s, input_len, PNG_UINT_31_MAX-prefix_len,
            Z_FINISH);

      ps->s.zs.next_out = NULL; /* safety */
      ps->s.zs.avail_out = 0;
      ps->s.zs.next_in = NULL;
      ps->s.zs.avail_in = 0;
      png_ptr->zowner = 0; /* release png_ptr::zstream */

      /* Since Z_FINISH was passed as the flush parameter any result other than
       * Z_STREAM_END is an error.  In any case in the event of an error free
       * the whole compression state; the only expected error is Z_MEM_ERROR.
       */
      if (ret != Z_STREAM_END)
      {
         png_zlib_compress_destroy(&ps->s, 0/*check*/);

         /* This is not very likely given the PNG_UINT_31_MAX limit above, but
          * if code is added to limit the size of the chunks produced it can
          * start to happen.
          */
         if (ret == Z_BUF_ERROR)
            ps->s.zs.msg = PNGZ_MSG_CAST("compressed chunk too long");

         else
            png_zstream_error(&ps->s.zs, ret);

         png_chunk_report(png_ptr, ps->s.zs.msg, PNG_CHUNK_WRITE_ERROR);
         return 0;
      }
   }

   /* png_compress is meant to guarantee this on a successful return: */
   affirm(ps->s.overflow == 0U && ps->s.len <= PNG_UINT_31_MAX - prefix_len);

   /* Correct the zlib CINFO field: */
   if (ps->s.len >= 2U)
      fix_cinfo(ps, ps->s.list->output, input_len);

   return 1;
}

/* Return the length of the compressed data; this is effectively a debug
 * function to catch inconsistencies caused by internal errors.  It will
 * disappear in a release build.
 */
#if PNG_RELEASE_BUILD
#  define png_length_compressed_chunk_data(pp, p) ((pp)->zlib_state->s.len)
#else /* !RELEASE_BUILD */
static png_uint_32
png_length_compressed_chunk_data(png_structrp png_ptr, png_uint_32 p)
{
   png_zlib_statep ps = png_ptr->zlib_state;

   debug(ps != NULL && ps->s.overflow == 0U && ps->s.len <= PNG_UINT_31_MAX-p);
   return ps->s.len;
}
#endif /* !RELEASE_BUILD */

/* Write all the data produced by the above function; the caller must write the
 * prefix and chunk header.
 */
static void
png_write_compressed_chunk_data(png_structrp png_ptr)
{
   png_zlib_statep ps = png_ptr->zlib_state;
   png_compression_bufferp next;
   png_uint_32 output_len;

   affirm(ps != NULL && ps->s.overflow == 0U);
   next = ps->s.list;

   for (output_len = ps->s.len; output_len > 0U; next = next->next)
   {
      png_uint_32 size = PNG_ROW_BUFFER_SIZE;

      /* If this affirm fails there is a bug in the calculation of
       * output_length above, or in the buffer_limit code in png_compress.
       */
      affirm(next != NULL && output_len > 0U);

      if (size > output_len)
         size = output_len;

      png_write_chunk_data(png_ptr, next->output, size);

      output_len -= size;
   }

   /* Release the list back to the stash. */
   debug(ps->stash == NULL);
   ps->stash = ps->s.list;
   ps->s.list = NULL;
   ps->s.end = &ps->s.list;
}
#endif /* WRITE_COMPRESSED_TEXT */

#if defined(PNG_WRITE_TEXT_SUPPORTED) || defined(PNG_WRITE_pCAL_SUPPORTED) || \
    defined(PNG_WRITE_iCCP_SUPPORTED) || defined(PNG_WRITE_sPLT_SUPPORTED)
/* Check that the tEXt or zTXt keyword is valid per PNG 1.0 specification,
 * and if invalid, correct the keyword rather than discarding the entire
 * chunk.  The PNG 1.0 specification requires keywords 1-79 characters in
 * length, forbids leading or trailing whitespace, multiple internal spaces,
 * and the non-break space (0x80) from ISO 8859-1.  Returns keyword length.
 *
 * The 'new_key' buffer must be at least 80 characters in size (for the keyword
 * plus a trailing '\0').  If this routine returns 0 then there was no keyword,
 * or a valid one could not be generated, and the caller must CHUNK_WRITE_ERROR.
 */
static unsigned int
png_check_keyword(png_structrp png_ptr, png_const_charp key, png_bytep new_key)
{
   png_const_charp orig_key = key;
   unsigned int key_len = 0;
   int bad_character = 0;
   int space = 1;

   png_debug(1, "in png_check_keyword");

   if (key == NULL)
   {
      *new_key = 0;
      return 0;
   }

   while (*key && key_len < 79)
   {
      png_byte ch = (png_byte)(0xff & *key++);

      if ((ch > 32 && ch <= 126) || (ch >= 161 /*&& ch <= 255*/))
         *new_key++ = ch, ++key_len, space = 0;

      else if (space == 0)
      {
         /* A space or an invalid character when one wasn't seen immediately
          * before; output just a space.
          */
         *new_key++ = 32, ++key_len, space = 1;

         /* If the character was not a space then it is invalid. */
         if (ch != 32)
            bad_character = ch;
      }

      else if (bad_character == 0)
         bad_character = ch; /* just skip it, record the first error */
   }

   if (key_len > 0 && space != 0) /* trailing space */
   {
      --key_len, --new_key;
      if (bad_character == 0)
         bad_character = 32;
   }

   /* Terminate the keyword */
   *new_key = 0;

   if (key_len == 0)
      return 0;

#ifdef PNG_WARNINGS_SUPPORTED
   /* Try to only output one warning per keyword: */
   if (*key != 0) /* keyword too long */
      png_app_warning(png_ptr, "keyword truncated");

   else if (bad_character != 0)
   {
      PNG_WARNING_PARAMETERS(p)

      png_warning_parameter(p, 1, orig_key);
      png_warning_parameter_signed(p, 2, PNG_NUMBER_FORMAT_02x, bad_character);

      png_formatted_warning(png_ptr, p, "keyword \"@1\": bad character '0x@2'");
   }
#endif /* WARNINGS */

   return key_len;
}
#endif /* WRITE_TEXT || WRITE_pCAL || WRITE_iCCP || WRITE_sPLT */

/* Write the IHDR chunk, and update the png_struct with the necessary
 * information.  Note that the rest of this code depends upon this
 * information being correct.
 */
void /* PRIVATE */
png_write_IHDR(png_structrp png_ptr, png_uint_32 width, png_uint_32 height,
    int bit_depth, int color_type, int compression_type, int filter_method,
    int interlace_type)
{
   png_byte buf[13]; /* Buffer to store the IHDR info */

   png_debug(1, "in png_write_IHDR");

   /* Check that we have valid input data from the application info */
   switch (color_type)
   {
      case PNG_COLOR_TYPE_GRAY:
         switch (bit_depth)
         {
            case 1:
            case 2:
            case 4:
            case 8:
#ifdef PNG_WRITE_16BIT_SUPPORTED
            case 16:
#endif
               break;

            default:
               png_error(png_ptr, "Invalid bit depth for grayscale image");
         }
         break;

      case PNG_COLOR_TYPE_RGB:
#ifdef PNG_WRITE_16BIT_SUPPORTED
         if (bit_depth != 8 && bit_depth != 16)
#else
         if (bit_depth != 8)
#endif
            png_error(png_ptr, "Invalid bit depth for RGB image");

         break;

      case PNG_COLOR_TYPE_PALETTE:
         switch (bit_depth)
         {
            case 1:
            case 2:
            case 4:
            case 8:
               break;

            default:
               png_error(png_ptr, "Invalid bit depth for paletted image");
         }
         break;

      case PNG_COLOR_TYPE_GRAY_ALPHA:
         if (bit_depth != 8 && bit_depth != 16)
            png_error(png_ptr, "Invalid bit depth for grayscale+alpha image");

         break;

      case PNG_COLOR_TYPE_RGB_ALPHA:
#ifdef PNG_WRITE_16BIT_SUPPORTED
         if (bit_depth != 8 && bit_depth != 16)
#else
         if (bit_depth != 8)
#endif
            png_error(png_ptr, "Invalid bit depth for RGBA image");

         break;

      default:
         png_error(png_ptr, "Invalid image color type specified");
   }

   if (compression_type != PNG_COMPRESSION_TYPE_BASE)
   {
      png_app_error(png_ptr, "Invalid compression type specified");
      compression_type = PNG_COMPRESSION_TYPE_BASE;
   }

   /* Write filter_method 64 (intrapixel differencing) only if
    * 1. Libpng was compiled with PNG_MNG_FEATURES_SUPPORTED and
    * 2. Libpng did not write a PNG signature (this filter_method is only
    *    used in PNG datastreams that are embedded in MNG datastreams) and
    * 3. The application called png_permit_mng_features with a mask that
    *    included PNG_FLAG_MNG_FILTER_64 and
    * 4. The filter_method is 64 and
    * 5. The color_type is RGB or RGBA
    */
   if (
#     ifdef PNG_MNG_FEATURES_SUPPORTED
         !((png_ptr->mng_features_permitted & PNG_FLAG_MNG_FILTER_64) != 0 &&
           ((png_ptr->mode & PNG_HAVE_PNG_SIGNATURE) == 0) &&
           (color_type == PNG_COLOR_TYPE_RGB ||
            color_type == PNG_COLOR_TYPE_RGB_ALPHA) &&
           (filter_method == PNG_INTRAPIXEL_DIFFERENCING)) &&
#     endif /* MNG_FEATURES */
       filter_method != PNG_FILTER_TYPE_BASE)
   {
      png_app_error(png_ptr, "Invalid filter type specified");
      filter_method = PNG_FILTER_TYPE_BASE;
   }

   if (interlace_type != PNG_INTERLACE_NONE &&
       interlace_type != PNG_INTERLACE_ADAM7)
   {
      png_app_error(png_ptr, "Invalid interlace type specified");
      interlace_type = PNG_INTERLACE_ADAM7;
   }

   /* Save the relevant information */
   png_ptr->bit_depth = png_check_byte(png_ptr, bit_depth);
   png_ptr->color_type = png_check_byte(png_ptr, color_type);
   png_ptr->interlaced = png_check_byte(png_ptr, interlace_type);
   png_ptr->filter_method = png_check_byte(png_ptr, filter_method);
   png_ptr->width = width;
   png_ptr->height = height;

   /* Pack the header information into the buffer */
   png_save_uint_32(buf, width);
   png_save_uint_32(buf + 4, height);
   buf[8] = png_check_byte(png_ptr, bit_depth);
   buf[9] = png_check_byte(png_ptr, color_type);
   buf[10] = png_check_byte(png_ptr, compression_type);
   buf[11] = png_check_byte(png_ptr, filter_method);
   buf[12] = png_check_byte(png_ptr, interlace_type);

   /* Write the chunk */
   png_write_complete_chunk(png_ptr, png_IHDR, buf, (png_size_t)13);
   png_ptr->mode |= PNG_HAVE_IHDR;
}

/* Write the palette.  We are careful not to trust png_color to be in the
 * correct order for PNG, so people can redefine it to any convenient
 * structure.
 */
void /* PRIVATE */
png_write_PLTE(png_structrp png_ptr, png_const_colorp palette,
    unsigned int num_pal)
{
   png_uint_32 max_palette_length, i;
   png_const_colorp pal_ptr;
   png_byte buf[3];

   png_debug(1, "in png_write_PLTE");

   max_palette_length = (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE) ?
      (1 << png_ptr->bit_depth) : PNG_MAX_PALETTE_LENGTH;

   if ((
#     ifdef PNG_MNG_FEATURES_SUPPORTED
         (png_ptr->mng_features_permitted & PNG_FLAG_MNG_EMPTY_PLTE) == 0 &&
#     endif /* MNG_FEATURES */
       num_pal == 0) || num_pal > max_palette_length)
   {
      if (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
      {
         png_error(png_ptr, "Invalid number of colors in palette");
      }

      else
      {
         png_warning(png_ptr, "Invalid number of colors in palette");
         return;
      }
   }

   if ((png_ptr->color_type & PNG_COLOR_MASK_COLOR) == 0)
   {
      png_warning(png_ptr,
          "Ignoring request to write a PLTE chunk in grayscale PNG");

      return;
   }

   png_ptr->num_palette = png_check_bits(png_ptr, num_pal, 9);
   png_debug1(3, "num_palette = %d", png_ptr->num_palette);

   png_write_chunk_header(png_ptr, png_PLTE, num_pal * 3U);

   for (i = 0, pal_ptr = palette; i < num_pal; i++, pal_ptr++)
   {
      buf[0] = pal_ptr->red;
      buf[1] = pal_ptr->green;
      buf[2] = pal_ptr->blue;
      png_write_chunk_data(png_ptr, buf, 3U);
   }

   png_write_chunk_end(png_ptr);
   png_ptr->mode |= PNG_HAVE_PLTE;
}

/* Write an IEND chunk */
void /* PRIVATE */
png_write_IEND(png_structrp png_ptr)
{
   png_debug(1, "in png_write_IEND");

   png_write_complete_chunk(png_ptr, png_IEND, NULL, (png_size_t)0);
   png_ptr->mode |= PNG_HAVE_IEND;
}

#if defined(PNG_WRITE_gAMA_SUPPORTED) || defined(PNG_WRITE_cHRM_SUPPORTED)
static int
png_save_int_31(png_structrp png_ptr, png_bytep buf, png_int_32 i)
   /* Save a signed value as a PNG unsigned value; the argument is required to
    * be in the range 0..0x7FFFFFFFU.  If not a *warning* is produced and false
    * is returned.  Because this is only called from png_write_cHRM_fixed and
    * png_write_gAMA_fixed below this is safe (we don't need either chunk,
    * particularly if the value is bogus.)
    *
    * The warning is png_app_error; it may return if the app tells it to but the
    * app can have it error out.  JB 20150821: I believe the checking in png.c
    * actually makes this error impossible, but this is safe.
    */
{
#ifndef __COVERITY__
   if (i >= 0 && i <= 0x7FFFFFFF)
#else
   /* Supress bogus Coverity complaint */
   if (i >= 0)
#endif
   {
      png_save_uint_32(buf, (png_uint_32)/*SAFE*/i);
      return 1;
   }

   else
   {
      png_chunk_report(png_ptr, "negative value in cHRM or gAMA",
         PNG_CHUNK_WRITE_ERROR);
      return 0;
   }
}
#endif /* WRITE_gAMA || WRITE_cHRM */

#ifdef PNG_WRITE_gAMA_SUPPORTED
/* Write a gAMA chunk */
void /* PRIVATE */
png_write_gAMA_fixed(png_structrp png_ptr, png_fixed_point file_gamma)
{
   png_byte buf[4];

   png_debug(1, "in png_write_gAMA");

   /* file_gamma is saved in 1/100,000ths */
   if (png_save_int_31(png_ptr, buf, file_gamma))
      png_write_complete_chunk(png_ptr, png_gAMA, buf, (png_size_t)4);
}
#endif

#ifdef PNG_WRITE_sRGB_SUPPORTED
/* Write a sRGB chunk */
void /* PRIVATE */
png_write_sRGB(png_structrp png_ptr, int srgb_intent)
{
   png_byte buf[1];

   png_debug(1, "in png_write_sRGB");

   if (srgb_intent >= PNG_sRGB_INTENT_LAST)
      png_chunk_report(png_ptr, "Invalid sRGB rendering intent specified",
            PNG_CHUNK_WRITE_ERROR);

   buf[0] = png_check_byte(png_ptr, srgb_intent);
   png_write_complete_chunk(png_ptr, png_sRGB, buf, (png_size_t)1);
}
#endif

#ifdef PNG_WRITE_iCCP_SUPPORTED
/* Write an iCCP chunk */
void /* PRIVATE */
png_write_iCCP(png_structrp png_ptr, png_const_charp name,
    png_const_voidp profile)
{
   png_uint_32 name_len;
   png_uint_32 profile_len;
   png_byte new_name[81]; /* 1 byte for the compression byte */

   png_debug(1, "in png_write_iCCP");

   affirm(profile != NULL);

   profile_len = png_get_uint_32(profile);
   name_len = png_check_keyword(png_ptr, name, new_name);

   if (name_len == 0)
   {
      png_chunk_report(png_ptr, "iCCP: invalid keyword", PNG_CHUNK_WRITE_ERROR);
      return;
   }

   ++name_len; /* trailing '\0' */
   new_name[name_len++] = PNG_COMPRESSION_TYPE_BASE;

   if (png_compress_chunk_data(png_ptr, png_iCCP, name_len, profile,
            profile_len))
   {
      png_write_chunk_header(png_ptr, png_iCCP,
            name_len+png_length_compressed_chunk_data(png_ptr, name_len));
      png_write_chunk_data(png_ptr, new_name, name_len);
      png_write_compressed_chunk_data(png_ptr);
      png_write_chunk_end(png_ptr);
   }
}
#endif

#ifdef PNG_WRITE_sPLT_SUPPORTED
/* Write a sPLT chunk */
void /* PRIVATE */
png_write_sPLT(png_structrp png_ptr, png_const_sPLT_tp spalette)
{
   png_uint_32 name_len;
   png_byte new_name[80];
   png_byte entrybuf[10];
   png_size_t entry_size = (spalette->depth == 8 ? 6 : 10);
   png_size_t palette_size = entry_size * spalette->nentries;
   png_sPLT_entryp ep;

   png_debug(1, "in png_write_sPLT");

   name_len = png_check_keyword(png_ptr, spalette->name, new_name);

   if (name_len == 0)
      png_error(png_ptr, "sPLT: invalid keyword");

   /* Make sure we include the NULL after the name */
   png_write_chunk_header(png_ptr, png_sPLT,
       (png_uint_32)(name_len + 2 + palette_size));

   png_write_chunk_data(png_ptr, new_name, name_len + 1);

   png_write_chunk_data(png_ptr, &spalette->depth, 1);

   /* Loop through each palette entry, writing appropriately */
   for (ep = spalette->entries; ep<spalette->entries + spalette->nentries; ep++)
   {
      if (spalette->depth == 8)
      {
         entrybuf[0] = png_check_byte(png_ptr, ep->red);
         entrybuf[1] = png_check_byte(png_ptr, ep->green);
         entrybuf[2] = png_check_byte(png_ptr, ep->blue);
         entrybuf[3] = png_check_byte(png_ptr, ep->alpha);
         png_save_uint_16(entrybuf + 4, ep->frequency);
      }

      else
      {
         png_save_uint_16(entrybuf + 0, ep->red);
         png_save_uint_16(entrybuf + 2, ep->green);
         png_save_uint_16(entrybuf + 4, ep->blue);
         png_save_uint_16(entrybuf + 6, ep->alpha);
         png_save_uint_16(entrybuf + 8, ep->frequency);
      }

      png_write_chunk_data(png_ptr, entrybuf, entry_size);
   }

   png_write_chunk_end(png_ptr);
}
#endif

#ifdef PNG_WRITE_sBIT_SUPPORTED
/* Write the sBIT chunk */
void /* PRIVATE */
png_write_sBIT(png_structrp png_ptr, png_const_color_8p sbit, int color_type)
{
   png_byte buf[4];
   png_size_t size;

   png_debug(1, "in png_write_sBIT");

   /* Make sure we don't depend upon the order of PNG_COLOR_8 */
   if ((color_type & PNG_COLOR_MASK_COLOR) != 0)
   {
      unsigned int maxbits;

      maxbits = color_type==PNG_COLOR_TYPE_PALETTE ? 8 : png_ptr->bit_depth;

      if (sbit->red == 0 || sbit->red > maxbits ||
          sbit->green == 0 || sbit->green > maxbits ||
          sbit->blue == 0 || sbit->blue > maxbits)
      {
         png_app_error(png_ptr, "Invalid sBIT depth specified");
         return;
      }

      buf[0] = sbit->red;
      buf[1] = sbit->green;
      buf[2] = sbit->blue;
      size = 3;
   }

   else
   {
      if (sbit->gray == 0 || sbit->gray > png_ptr->bit_depth)
      {
         png_app_error(png_ptr, "Invalid sBIT depth specified");
         return;
      }

      buf[0] = sbit->gray;
      size = 1;
   }

   if ((color_type & PNG_COLOR_MASK_ALPHA) != 0)
   {
      if (sbit->alpha == 0 || sbit->alpha > png_ptr->bit_depth)
      {
         png_app_error(png_ptr, "Invalid sBIT depth specified");
         return;
      }

      buf[size++] = sbit->alpha;
   }

   png_write_complete_chunk(png_ptr, png_sBIT, buf, size);
}
#endif

#ifdef PNG_WRITE_cHRM_SUPPORTED
/* Write the cHRM chunk */
void /* PRIVATE */
png_write_cHRM_fixed(png_structrp png_ptr, const png_xy *xy)
{
   png_byte buf[32];

   png_debug(1, "in png_write_cHRM");

   /* Each value is saved in 1/100,000ths */
   if (png_save_int_31(png_ptr, buf,      xy->whitex) &&
       png_save_int_31(png_ptr, buf +  4, xy->whitey) &&
       png_save_int_31(png_ptr, buf +  8, xy->redx) &&
       png_save_int_31(png_ptr, buf + 12, xy->redy) &&
       png_save_int_31(png_ptr, buf + 16, xy->greenx) &&
       png_save_int_31(png_ptr, buf + 20, xy->greeny) &&
       png_save_int_31(png_ptr, buf + 24, xy->bluex) &&
       png_save_int_31(png_ptr, buf + 28, xy->bluey))
      png_write_complete_chunk(png_ptr, png_cHRM, buf, 32);
}
#endif

#ifdef PNG_WRITE_tRNS_SUPPORTED
/* Write the tRNS chunk */
void /* PRIVATE */
png_write_tRNS(png_structrp png_ptr, png_const_bytep trans_alpha,
    png_const_color_16p tran, int num_trans, int color_type)
{
   png_byte buf[6];

   png_debug(1, "in png_write_tRNS");

   if (color_type == PNG_COLOR_TYPE_PALETTE)
   {
      affirm(num_trans > 0 && num_trans <= PNG_MAX_PALETTE_LENGTH);
      {
#        ifdef PNG_WRITE_INVERT_ALPHA_SUPPORTED
            union
            {
               png_uint_32 u32[1];
               png_byte    b8[PNG_MAX_PALETTE_LENGTH];
            }  inverted_alpha;

            /* Invert the alpha channel (in tRNS) if required */
            if (png_ptr->write_invert_alpha)
            {
               int i;

               memcpy(inverted_alpha.b8, trans_alpha, num_trans);

               for (i=0; 4*i<num_trans; ++i)
                  inverted_alpha.u32[i] = ~inverted_alpha.u32[i];

               trans_alpha = inverted_alpha.b8;
            }
#        endif /* WRITE_INVERT_ALPHA */

         png_write_complete_chunk(png_ptr, png_tRNS, trans_alpha, num_trans);
      }
   }

   else if (color_type == PNG_COLOR_TYPE_GRAY)
   {
      /* One 16 bit value */
      affirm(tran->gray < (1 << png_ptr->bit_depth));
      png_save_uint_16(buf, tran->gray);
      png_write_complete_chunk(png_ptr, png_tRNS, buf, (png_size_t)2);
   }

   else if (color_type == PNG_COLOR_TYPE_RGB)
   {
      /* Three 16 bit values */
      png_save_uint_16(buf, tran->red);
      png_save_uint_16(buf + 2, tran->green);
      png_save_uint_16(buf + 4, tran->blue);
      affirm(png_ptr->bit_depth == 8 || (buf[0] | buf[2] | buf[4]) == 0);
      png_write_complete_chunk(png_ptr, png_tRNS, buf, (png_size_t)6);
   }

   else /* Already checked in png_set_tRNS */
      impossible("invalid tRNS");
}
#endif

#ifdef PNG_WRITE_bKGD_SUPPORTED
/* Write the background chunk */
void /* PRIVATE */
png_write_bKGD(png_structrp png_ptr, png_const_color_16p back, int color_type)
{
   png_byte buf[6];

   png_debug(1, "in png_write_bKGD");

   if (color_type == PNG_COLOR_TYPE_PALETTE)
   {
      if (
#        ifdef PNG_MNG_FEATURES_SUPPORTED
            (png_ptr->num_palette != 0 ||
            (png_ptr->mng_features_permitted & PNG_FLAG_MNG_EMPTY_PLTE) == 0) &&
#        endif /* MNG_FEATURES */
         back->index >= png_ptr->num_palette)
      {
         png_app_error(png_ptr, "Invalid background palette index");
         return;
      }

      buf[0] = back->index;
      png_write_complete_chunk(png_ptr, png_bKGD, buf, (png_size_t)1);
   }

   else if ((color_type & PNG_COLOR_MASK_COLOR) != 0)
   {
      png_save_uint_16(buf, back->red);
      png_save_uint_16(buf + 2, back->green);
      png_save_uint_16(buf + 4, back->blue);
#ifdef PNG_WRITE_16BIT_SUPPORTED
      if (png_ptr->bit_depth == 8 && (buf[0] | buf[2] | buf[4]) != 0)
#else
      if ((buf[0] | buf[2] | buf[4]) != 0)
#endif
      {
         png_app_error(png_ptr,
             "Ignoring attempt to write 16-bit bKGD chunk when bit_depth is 8");

         return;
      }

      png_write_complete_chunk(png_ptr, png_bKGD, buf, (png_size_t)6);
   }

   else
   {
      if (back->gray >= (1 << png_ptr->bit_depth))
      {
         png_app_error(png_ptr,
             "Ignoring attempt to write bKGD chunk out-of-range for bit_depth");

         return;
      }

      png_save_uint_16(buf, back->gray);
      png_write_complete_chunk(png_ptr, png_bKGD, buf, (png_size_t)2);
   }
}
#endif

#ifdef PNG_WRITE_hIST_SUPPORTED
/* Write the histogram */
void /* PRIVATE */
png_write_hIST(png_structrp png_ptr, png_const_uint_16p hist, int num_hist)
{
   int i;
   png_byte buf[3];

   png_debug(1, "in png_write_hIST");

   if (num_hist > (int)png_ptr->num_palette)
   {
      png_debug2(3, "num_hist = %d, num_palette = %d", num_hist,
          png_ptr->num_palette);

      png_warning(png_ptr, "Invalid number of histogram entries specified");
      return;
   }

   png_write_chunk_header(png_ptr, png_hIST, (png_uint_32)(num_hist * 2));

   for (i = 0; i < num_hist; i++)
   {
      png_save_uint_16(buf, hist[i]);
      png_write_chunk_data(png_ptr, buf, (png_size_t)2);
   }

   png_write_chunk_end(png_ptr);
}
#endif

#ifdef PNG_WRITE_tEXt_SUPPORTED
/* Write a tEXt chunk */
void /* PRIVATE */
png_write_tEXt(png_structrp png_ptr, png_const_charp key, png_const_charp text,
    png_size_t text_len)
{
   unsigned int key_len;
   png_byte new_key[80];

   png_debug(1, "in png_write_tEXt");

   key_len = png_check_keyword(png_ptr, key, new_key);

   if (key_len == 0)
   {
      png_chunk_report(png_ptr, "tEXt: invalid keyword", PNG_CHUNK_WRITE_ERROR);
      return;
   }

   if (text == NULL || *text == '\0')
      text_len = 0;

   else
      text_len = strlen(text);

   if (text_len > PNG_UINT_31_MAX - (key_len+1))
   {
      png_chunk_report(png_ptr, "tEXt: text too long", PNG_CHUNK_WRITE_ERROR);
      return;
   }

   /* Make sure we include the 0 after the key */
   png_write_chunk_header(png_ptr, png_tEXt,
       (png_uint_32)/*checked above*/(key_len + text_len + 1));
   /*
    * We leave it to the application to meet PNG-1.0 requirements on the
    * contents of the text.  PNG-1.0 through PNG-1.2 discourage the use of
    * any non-Latin-1 characters except for NEWLINE.  ISO PNG will forbid them.
    * The NUL character is forbidden by PNG-1.0 through PNG-1.2 and ISO PNG.
    */
   png_write_chunk_data(png_ptr, new_key, key_len + 1);

   if (text_len != 0)
      png_write_chunk_data(png_ptr, (png_const_bytep)text, text_len);

   png_write_chunk_end(png_ptr);
}
#endif

#ifdef PNG_WRITE_zTXt_SUPPORTED
/* Write a compressed text chunk */
void /* PRIVATE */
png_write_zTXt(png_structrp png_ptr, png_const_charp key, png_const_charp text,
    int compression)
{
   unsigned int key_len;
   png_byte new_key[81];

   png_debug(1, "in png_write_zTXt");

   if (compression != PNG_TEXT_COMPRESSION_zTXt)
      png_app_warning(png_ptr, "zTXt: invalid compression type ignored");

   key_len = png_check_keyword(png_ptr, key, new_key);

   if (key_len == 0)
   {
      png_chunk_report(png_ptr, "zTXt: invalid keyword", PNG_CHUNK_WRITE_ERROR);
      return;
   }

   /* Add the compression method and 1 for the keyword separator. */
   ++key_len;
   new_key[key_len++] = PNG_COMPRESSION_TYPE_BASE;

   if (png_compress_chunk_data(png_ptr, png_zTXt, key_len, text, strlen(text)))
   {
      png_write_chunk_header(png_ptr, png_zTXt,
            key_len+png_length_compressed_chunk_data(png_ptr, key_len));
      png_write_chunk_data(png_ptr, new_key, key_len);
      png_write_compressed_chunk_data(png_ptr);
      png_write_chunk_end(png_ptr);
   }

   /* else chunk report already issued and ignored */
}
#endif

#ifdef PNG_WRITE_iTXt_SUPPORTED
/* Write an iTXt chunk */
void /* PRIVATE */
png_write_iTXt(png_structrp png_ptr, int compression, png_const_charp key,
    png_const_charp lang, png_const_charp lang_key, png_const_charp text)
{
   png_uint_32 key_len, prefix_len, data_len;
   png_size_t lang_len, lang_key_len, text_len;
   png_byte new_key[82]; /* 80 bytes for the key, 2 byte compression info */

   png_debug(1, "in png_write_iTXt");

   key_len = png_check_keyword(png_ptr, key, new_key);

   if (key_len == 0)
   {
      png_chunk_report(png_ptr, "iTXt: invalid keyword", PNG_CHUNK_WRITE_ERROR);
      return;
   }

   debug(new_key[key_len] == 0);
   ++key_len; /* terminating 0 added by png_check_keyword */

   /* Set the compression flag */
   switch (compression)
   {
      case PNG_ITXT_COMPRESSION_NONE:
      case PNG_TEXT_COMPRESSION_NONE:
         compression = new_key[key_len++] = 0; /* no compression */
         break;

      case PNG_TEXT_COMPRESSION_zTXt:
      case PNG_ITXT_COMPRESSION_zTXt:
         compression = new_key[key_len++] = 1; /* compressed */
         break;

      default:
         png_chunk_report(png_ptr, "iTXt: invalid compression",
               PNG_CHUNK_WRITE_ERROR);
         return;
   }

   new_key[key_len++] = PNG_COMPRESSION_TYPE_BASE;

   /* We leave it to the application to meet PNG-1.0 requirements on the
    * contents of the text.  PNG-1.0 through PNG-1.2 discourage the use of
    * any non-Latin-1 characters except for NEWLINE (yes, this is really weird
    * in an 'international' text string.  ISO PNG, however, specifies that the
    * text is UTF-8 and this *IS NOT YET CHECKED*, so invalid sequences may be
    * present.
    *
    * The NUL character is forbidden by PNG-1.0 through PNG-1.2 and ISO PNG.
    *
    * TODO: validate the language tag correctly (see the spec.)
    */
   if (lang == NULL) lang = ""; /* empty language is valid */
   lang_len = strlen(lang)+1U;
   if (lang_key == NULL) lang_key = ""; /* may be empty */
   lang_key_len = strlen(lang_key)+1U;
   if (text == NULL) text = ""; /* may be empty */

   if (lang_len > PNG_UINT_31_MAX-key_len ||
       lang_key_len > PNG_UINT_31_MAX-key_len-lang_len)
   {
      png_chunk_report(png_ptr, "iTXt: prefix too long", PNG_CHUNK_WRITE_ERROR);
      return;
   }

   prefix_len = (png_uint_32)/*SAFE*/(key_len+lang_len+lang_key_len);
   text_len = strlen(text); /* no trailing '\0' */

   if (compression != 0)
   {
      if (png_compress_chunk_data(png_ptr, png_iTXt, prefix_len, text,
               text_len))
         data_len = png_length_compressed_chunk_data(png_ptr, prefix_len);

      else
         return; /* chunk report already issued and ignored */
   }

   else
   {
      if (text_len > PNG_UINT_31_MAX-prefix_len)
      {
         png_chunk_report(png_ptr, "iTXt: text too long",
               PNG_CHUNK_WRITE_ERROR);
         return;
      }

      data_len = (png_uint_32)/*SAFE*/text_len;
   }

   png_write_chunk_header(png_ptr, png_iTXt, prefix_len+data_len);
   png_write_chunk_data(png_ptr, new_key, key_len);
   png_write_chunk_data(png_ptr, lang, lang_len);
   png_write_chunk_data(png_ptr, lang_key, lang_key_len);

   if (compression != 0)
      png_write_compressed_chunk_data(png_ptr);

   else
      png_write_chunk_data(png_ptr, text, data_len);

   png_write_chunk_end(png_ptr);
}
#endif /* WRITE_iTXt */

#if defined(PNG_WRITE_oFFs_SUPPORTED) ||\
    defined(PNG_WRITE_pCAL_SUPPORTED)
/* PNG signed integers are saved in 32-bit 2's complement format.  ANSI C-90
 * defines a cast of a signed integer to an unsigned integer either to preserve
 * the value, if it is positive, or to calculate:
 *
 *     (UNSIGNED_MAX+1) + integer
 *
 * Where UNSIGNED_MAX is the appropriate maximum unsigned value, so when the
 * negative integral value is added the result will be an unsigned value
 * correspnding to the 2's complement representation.
 */
static int
save_int_32(png_structrp png_ptr, png_bytep buf, png_int_32 j)
{
   png_uint_32 i = 0xFFFFFFFFU & (png_uint_32)/*SAFE & CORRECT*/j;

   if (i != 0x80000000U/*value not permitted*/)
   {
      png_save_uint_32(buf, i);
      return 1;
   }

   else
   {
      png_chunk_report(png_ptr, "invalid value in oFFS or pCAL",
         PNG_CHUNK_WRITE_ERROR);
      return 0;
   }
}
#endif /* WRITE_oFFs || WRITE_pCAL */

#ifdef PNG_WRITE_oFFs_SUPPORTED
/* Write the oFFs chunk */
void /* PRIVATE */
png_write_oFFs(png_structrp png_ptr, png_int_32 x_offset, png_int_32 y_offset,
    int unit_type)
{
   png_byte buf[9];

   png_debug(1, "in png_write_oFFs");

   if (unit_type >= PNG_OFFSET_LAST)
      png_warning(png_ptr, "Unrecognized unit type for oFFs chunk");

   if (save_int_32(png_ptr, buf, x_offset) &&
       save_int_32(png_ptr, buf + 4, y_offset))
   {
      /* unit type is 0 or 1, this has been checked already so the following
       * is safe:
       */
      buf[8] = unit_type != 0;
      png_write_complete_chunk(png_ptr, png_oFFs, buf, (png_size_t)9);
   }
}
#endif /* WRITE_oFFs */

#ifdef PNG_WRITE_pCAL_SUPPORTED
/* Write the pCAL chunk (described in the PNG extensions document) */
void /* PRIVATE */
png_write_pCAL(png_structrp png_ptr, png_charp purpose, png_int_32 X0,
    png_int_32 X1, int type, int nparams, png_const_charp units,
    png_charpp params)
{
   png_uint_32 purpose_len;
   size_t units_len;
   png_byte buf[10];
   png_byte new_purpose[80];

   png_debug1(1, "in png_write_pCAL (%d parameters)", nparams);

   if (type >= PNG_EQUATION_LAST)
      png_error(png_ptr, "Unrecognized equation type for pCAL chunk");

   purpose_len = png_check_keyword(png_ptr, purpose, new_purpose);

   if (purpose_len == 0)
      png_error(png_ptr, "pCAL: invalid keyword");

   ++purpose_len; /* terminator */

   png_debug1(3, "pCAL purpose length = %d", (int)purpose_len);
   units_len = strlen(units) + (nparams == 0 ? 0 : 1);
   png_debug1(3, "pCAL units length = %d", (int)units_len);

   if (save_int_32(png_ptr, buf, X0) &&
       save_int_32(png_ptr, buf + 4, X1))
   {
      png_size_tp params_len = png_voidcast(png_size_tp,
         png_malloc(png_ptr, nparams * sizeof (png_size_t)));
      int i;
      size_t total_len = purpose_len + units_len + 10;

      /* Find the length of each parameter, making sure we don't count the
       * null terminator for the last parameter.
       */
      for (i = 0; i < nparams; i++)
      {
         params_len[i] = strlen(params[i]) + (i == nparams - 1 ? 0 : 1);
         png_debug2(3, "pCAL parameter %d length = %lu", i,
             (unsigned long)params_len[i]);
         total_len += params_len[i];
      }

      png_debug1(3, "pCAL total length = %d", (int)total_len);
      png_write_chunk_header(png_ptr, png_pCAL, (png_uint_32)total_len);
      png_write_chunk_data(png_ptr, new_purpose, purpose_len);
      buf[8] = png_check_byte(png_ptr, type);
      buf[9] = png_check_byte(png_ptr, nparams);
      png_write_chunk_data(png_ptr, buf, (png_size_t)10);
      png_write_chunk_data(png_ptr, (png_const_bytep)units,
            (png_size_t)units_len);

      for (i = 0; i < nparams; i++)
         png_write_chunk_data(png_ptr, (png_const_bytep)params[i],
            params_len[i]);

      png_free(png_ptr, params_len);
      png_write_chunk_end(png_ptr);
   }
}
#endif /* WRITE_pCAL */

#ifdef PNG_WRITE_sCAL_SUPPORTED
/* Write the sCAL chunk */
void /* PRIVATE */
png_write_sCAL_s(png_structrp png_ptr, int unit, png_const_charp width,
    png_const_charp height)
{
   png_byte buf[64];
   png_size_t wlen, hlen, total_len;

   png_debug(1, "in png_write_sCAL_s");

   wlen = strlen(width);
   hlen = strlen(height);
   total_len = wlen + hlen + 2;

   if (total_len > 64)
   {
      png_warning(png_ptr, "Can't write sCAL (buffer too small)");
      return;
   }

   buf[0] = png_check_byte(png_ptr, unit);
   memcpy(buf + 1, width, wlen + 1);      /* Append the '\0' here */
   memcpy(buf + wlen + 2, height, hlen);  /* Do NOT append the '\0' here */

   png_debug1(3, "sCAL total length = %u", (unsigned int)total_len);
   png_write_complete_chunk(png_ptr, png_sCAL, buf, total_len);
}
#endif

#ifdef PNG_WRITE_pHYs_SUPPORTED
/* Write the pHYs chunk */
void /* PRIVATE */
png_write_pHYs(png_structrp png_ptr, png_uint_32 x_pixels_per_unit,
    png_uint_32 y_pixels_per_unit,
    int unit_type)
{
   png_byte buf[9];

   png_debug(1, "in png_write_pHYs");

   if (unit_type >= PNG_RESOLUTION_LAST)
      png_warning(png_ptr, "Unrecognized unit type for pHYs chunk");

   png_save_uint_32(buf, x_pixels_per_unit);
   png_save_uint_32(buf + 4, y_pixels_per_unit);
   buf[8] = png_check_byte(png_ptr, unit_type);

   png_write_complete_chunk(png_ptr, png_pHYs, buf, (png_size_t)9);
}
#endif

#ifdef PNG_WRITE_tIME_SUPPORTED
/* Write the tIME chunk.  Use either png_convert_from_struct_tm()
 * or png_convert_from_time_t(), or fill in the structure yourself.
 */
void /* PRIVATE */
png_write_tIME(png_structrp png_ptr, png_const_timep mod_time)
{
   png_byte buf[7];

   png_debug(1, "in png_write_tIME");

   if (mod_time->month  > 12 || mod_time->month  < 1 ||
       mod_time->day    > 31 || mod_time->day    < 1 ||
       mod_time->hour   > 23 || mod_time->second > 60)
   {
      png_warning(png_ptr, "Invalid time specified for tIME chunk");
      return;
   }

   png_save_uint_16(buf, mod_time->year);
   buf[2] = mod_time->month;
   buf[3] = mod_time->day;
   buf[4] = mod_time->hour;
   buf[5] = mod_time->minute;
   buf[6] = mod_time->second;

   png_write_complete_chunk(png_ptr, png_tIME, buf, (png_size_t)7);
}
#endif

static void
png_end_IDAT(png_structrp png_ptr)
{
   png_zlib_statep ps = png_ptr->zlib_state;

   png_ptr->zowner = 0U; /* release the stream */

   if (ps != NULL)
      png_deflate_release(png_ptr, ps, 1/*check*/);
}

static void
png_write_IDAT(png_structrp png_ptr, int flush)
{
   png_zlib_statep ps = png_ptr->zlib_state;
   png_uint_32 IDAT_size;

   /* Check for a correctly initialized list, the requirement that the end
    * pointer is NULL means that the end of the list can be easily detected.
    */
   affirm(ps != NULL && ps->s.end != NULL && *ps->s.end == NULL);
   png_zlib_compress_validate(&png_ptr->zlib_state->s, 0/*in_use*/);

   IDAT_size = png_ptr->IDAT_size;
   if (IDAT_size == 0U)
   {
      switch (pz_get(ps, IDAT, png_level, PNG_DEFAULT_COMPRESSION_LEVEL))
      {
         case PNG_COMPRESSION_COMPAT: /* Legacy */
            IDAT_size = 8192U;
            break;

         case PNG_COMPRESSION_LOW_MEMORY:
         case PNG_COMPRESSION_HIGH_SPEED:
         case PNG_COMPRESSION_LOW:
            /* png_compress uses PNG_ROW_BUFFER_SIZE buffers for the compressed
             * data.  Optimize to allocate only one of these:
             */
            IDAT_size = PNG_ROW_BUFFER_SIZE;
            break;

         default:
         case PNG_COMPRESSION_MEDIUM:
            IDAT_size = PNG_ZBUF_SIZE;
            break;

         case PNG_COMPRESSION_HIGH_READ_SPEED:
            /* Assume the reader reads partial IDAT chunks (pretty much a
             * requirement given that some PNG encoders produce just one IDAT)
             */
         case PNG_COMPRESSION_HIGH:
            /* This doesn't control the amount of memory allocated unless the
             * PNG IDAT data really is this big.
             *
             * TODO: review handling out-of-memory from png_compress() by
             * flushing an IDAT.
             */
            IDAT_size = PNG_UINT_31_MAX;
            break;
      }
   }

   /* Write IDAT chunks while either 'flush' is true or there are at
    * least png_ptr->IDAT_size bytes available to be written.
    */
   for (;;)
   {
      png_uint_32 len = IDAT_size;

      if (ps->s.overflow == 0U)
      {
         png_uint_32 avail = ps->s.len;

         if (avail < len)
         {
            /* When end_of_image is true everything gets written, otherwise
             * there must be at least IDAT_size bytes available.
             */
            if (!flush)
               return;

            if (avail == 0U)
               break;

            len = avail;
         }
      }

      png_write_chunk_header(png_ptr, png_IDAT, len);

      /* Write bytes from the buffer list, adjusting {overflow,len} as they are
       * written.
       */
      do
      {
         png_compression_bufferp next = ps->s.list;
         unsigned int avail = sizeof next->output;
         unsigned int start = ps->s.start;
         unsigned int written;

         affirm(next != NULL);

         if (next->next == NULL) /* end of list */
         {
            /* The z_stream should always be pointing into this output buffer,
             * the buffer may not be full:
             */
            debug(ps->s.zs.next_out + ps->s.zs.avail_out ==
                  next->output + sizeof next->output);
            avail -= ps->s.zs.avail_out;
         }

         else /* not end of list */
            debug((ps->s.zs.next_out < next->output ||
                   ps->s.zs.next_out > next->output + sizeof next->output) &&
                  (ps->s.overflow > 0 ||
                   ps->s.start + ps->s.len >= sizeof next->output));

         /* First, if this is the very first IDAT (PNG_HAVE_IDAT not set)
          * fix the Zlib CINFO field if required:
          */
         if ((png_ptr->mode & PNG_HAVE_IDAT) == 0U &&
             avail >= start+2U /* enough for the zlib header */)
         {
            debug(start == 0U);
            fix_cinfo(ps, next->output+start, png_image_size(png_ptr));
         }

         else /* always expect to see at least 2 bytes: */
            debug((png_ptr->mode & PNG_HAVE_IDAT) != 0U);

         /* Set this now to prevent the above happening again second time round
          * the loop:
          */
         png_ptr->mode |= PNG_HAVE_IDAT;

         if (avail <= start+len)
         {
            /* Write all of this buffer: */
            affirm(avail > start); /* else overflow on the subtract */
            written = avail-start;
            png_write_chunk_data(png_ptr, next->output+start, written);

            /* At the end there are no buffers in the list but the z_stream
             * still points into the old (just released) buffer.  This can
             * happen when the old buffer is not full if the compressed bytes
             * exactly match the IDAT length; it should always happen when
             * end_of_image is set.
             */
            ps->s.list = next->next;

            if (next->next == NULL)
            {
               debug(avail == start+len);
               ps->s.end = &ps->s.list;
               ps->s.zs.next_out = NULL;
               ps->s.zs.avail_out = 0U;
            }

            next->next = ps->stash;
            ps->stash = next;
            ps->s.start = 0U;
         }

         else /* write only part of this buffer */
         {
            written = len;
            png_write_chunk_data(png_ptr, next->output+start, written);
            ps->s.start = (unsigned int)/*SAFE*/(start + written);
         }

         /* 'written' bytes were written: */
         len -= written;

         if (written <= ps->s.len)
            ps->s.len -= written;

         else
         {
            affirm(ps->s.overflow > 0U);
            --ps->s.overflow;
            ps->s.len += 0x80000000U - written;
            UNTESTED
         }
      }
      while (len > 0U);

      png_write_chunk_end(png_ptr);
   }

   /* avail == 0 && flush */
   png_end_IDAT(png_ptr);
   png_ptr->mode |= PNG_AFTER_IDAT;
}

/* This is is a convenience wrapper to handle IDAT compression; it takes a
 * pointer to the input data and places no limit on the size of the output but
 * is otherwise the same as png_compress().  It also handles the use of the
 * stash (only used for IDAT compression.)
 */
static int
png_compress_IDAT_data(png_structrp png_ptr, png_zlib_statep ps,
    png_zlib_compressp pz, png_const_voidp input, uInt input_len, int flush)
{
   /* Delay initialize the z_stream. */
   if (png_ptr->zowner != png_IDAT)
      png_deflate_claim(png_ptr, png_IDAT, 0U);

   affirm(png_ptr->zowner == png_IDAT && pz->end != NULL && *pz->end == NULL);

   /* z_stream::{next,avail}_out are set by png_compress to point into the
    * buffer list.  next_in must be set here, avail_in comes from the input_len
    * parameter:
    */
   pz->zs.next_in = PNGZ_INPUT_CAST(png_voidcast(const Bytef*, input));
   *pz->end = ps->stash; /* May be NULL */
   ps->stash = NULL;

   /* zlib buffers the output, the maximum amount of compressed data that can be
    * produced here is governed by the amount of buffering.
    */
   {
      int ret = png_compress(pz, input_len, 0U/*unlimited*/, flush);

      affirm(pz->end != NULL && ps->stash == NULL);
      ps->stash = *pz->end; /* May be NULL */
      *pz->end = NULL;

      /* Z_FINISH should give Z_STREAM_END, everything else should give Z_OK, in
       * either case all the input should have been consumed:
       */
      implies(ret == Z_OK || ret == Z_FINISH, pz->zs.avail_in == 0U &&
            (ret == Z_STREAM_END) == (flush == Z_FINISH));
      pz->zs.next_in = NULL;
      pz->zs.avail_in = 0U; /* safety */
      png_zlib_compress_validate(pz, 0/*in_use*/);

      return ret;
   }
}

/* Compress some image data using the main png_zlib_compress.  Write the result
 * out if there is sufficient data.
 */
static void
png_compress_IDAT(png_structrp png_ptr, png_const_voidp input, uInt input_len,
    int flush)
{
   png_zlib_statep ps = png_ptr->zlib_state;
   int ret = png_compress_IDAT_data(png_ptr, ps, &ps->s, input, input_len,
         flush);

   /* Check the return code. */
   if (ret == Z_OK || ret == Z_STREAM_END)
      png_write_IDAT(png_ptr, flush == Z_FINISH);

   else /* ret != Z_OK && ret != Z_STREAM_END */
   {
      /* This is an error condition.  It is fatal. */
      png_end_IDAT(png_ptr);
      png_zstream_error(&ps->s.zs, ret);
      png_error(png_ptr, ps->s.zs.msg);
   }
}

/* This is called at the end of every row to handle the required callbacks and
 * advance png_struct::row_number and png_struct::pass.
 */
static void
png_write_end_row(png_structrp png_ptr, int flush)
{
   png_uint_32 row_number = png_ptr->row_number;
   unsigned int pass = png_ptr->pass;

   debug(pass < 7U);
   implies(flush == Z_FINISH, png_ptr->zowner == 0U);

   /* API NOTE: the write callback is made before any changes to the row number
    * or pass however, in 1.7.0, the zlib stream can be closed before the
    * callback is made (this is new).  The application flush function happens
    * afterward as was the case before.  In 1.7.0 this is solely determined by
    * the order of the code that follows.
    */
   if (png_ptr->write_row_fn != NULL)
      png_ptr->write_row_fn(png_ptr, row_number, pass);

#  ifdef PNG_WRITE_FLUSH_SUPPORTED
      if (flush == Z_SYNC_FLUSH)
      {
         if (png_ptr->output_flush_fn != NULL)
            png_ptr->output_flush_fn(png_ptr);
         png_ptr->zlib_state->flush_rows = 0U;
      }
#  else /* !WRITE_FLUSH */
      PNG_UNUSED(flush)
#  endif /* !WRITE_FLUSH */

   /* Finally advance to the next row/pass: */
   if (png_ptr->interlaced == PNG_INTERLACE_NONE)
   {
      debug(row_number < png_ptr->height);

      if (++row_number == png_ptr->height) /* last row */
      {
         row_number = 0U;
         debug(flush == Z_FINISH);
         png_ptr->pass = 7U;
      }
   }

#  ifdef PNG_WRITE_INTERLACING_SUPPORTED
      else /* interlaced */ if (png_ptr->do_interlace)
      {
         /* This gets called only for rows that are processed; i.e. rows that
          * are in the pass of a pass which is itself in the output.
          */
         debug(row_number < png_ptr->height &&
               PNG_PASS_IN_IMAGE(png_ptr->width, png_ptr->height, pass) &&
               pass <= PNG_LAST_PASS(png_ptr->width, png_ptr->height) &&
               PNG_ROW_IN_INTERLACE_PASS(row_number, pass));

         /* NOTE: the last row of the original image may not be in the pass, in
          * this case the code which skipped the row must do the increment
          * below!  See 'interlace_row' in pngwrite.c and the code in
          * png_write_png_rows below.
          *
          * In that case an earlier row will be the last one in the pass (if the
          * pass is in the output), check this here:
          */
         implies(pass == PNG_LAST_PASS(png_ptr->width, png_ptr->height) &&
                 PNG_LAST_PASS_ROW(row_number, pass, png_ptr->height),
                 flush == Z_FINISH);

         if (++row_number == png_ptr->height) /* last row */
         {
            row_number = 0U;
            png_ptr->pass = 0x7U & ++pass;
         }
      }
#  endif /* WRITE_INTERLACING */

   else /* application does interlace */
   {
      implies(png_ptr->height == 1U, pass != 6U);
      debug(PNG_PASS_IN_IMAGE(png_ptr->width, png_ptr->height, pass) &&
            row_number < PNG_PASS_ROWS(png_ptr->height, pass));

      if (++row_number == PNG_PASS_ROWS(png_ptr->height, pass))
      {
         /* last row in this pass, next one may be empty. */
         row_number = 0U;

         do
            ++pass;
         while (pass < 7U &&
                !PNG_PASS_IN_IMAGE(png_ptr->width, png_ptr->height, pass));

         implies(png_ptr->height == 1U, pass != 6U);
         implies(pass == 7U, flush == Z_FINISH);
         png_ptr->pass = 0x7U & pass;
      }
   }

   png_ptr->row_number = row_number;
}

#ifdef PNG_WRITE_FLUSH_SUPPORTED
/* Flush the current output buffers now */
void PNGAPI
png_write_flush(png_structrp png_ptr)
{
   png_debug(1, "in png_write_flush");

   /* Force a flush at the end of the current row by setting 'flush_rows' to the
    * maximum:
    */
   if (png_ptr != NULL && png_ptr->zlib_state != NULL)
      png_ptr->zlib_state->flush_rows = 0xEFFFFFFF;
}

/* Return the correct flush to use */
static int
row_flush(png_zlib_statep ps, unsigned int row_info_flags)
{
   if (PNG_IDAT_END(row_info_flags))
      return Z_FINISH;

   else if ((row_info_flags & png_row_end) != 0 &&
         ++ps->flush_rows >= ps->flush_dist)
      return Z_SYNC_FLUSH;

   else
      return Z_NO_FLUSH;
}
#else /* !WRITE_FLUSH */
#  define row_flush(ps, ri) (PNG_IDAT_END(ri) ? Z_FINISH : Z_NO_FLUSH)
#endif /* !WRITE_FLUSH */

static void
write_filtered_row(png_structrp png_ptr, png_const_voidp filtered_row,
   unsigned int row_bytes, unsigned int filter /*if at start of row*/,
   int flush)
{
   /* This handles writing a row that has been filtered, or did not need to be
    * filtered.  If the data row has a partial pixel it must have been handled
    * correctly in the caller; filters generate a full 8 bits even if the pixel
    * only has one significant bit!
    */
   debug(row_bytes > 0);
   affirm(row_bytes <= ZLIB_IO_MAX); /* I.e. it fits in a uInt */

   if (filter < PNG_FILTER_VALUE_LAST) /* start of row */
   {
      png_byte buffer[1];

      buffer[0] = PNG_BYTE(filter);
      png_compress_IDAT(png_ptr, buffer, 1U/*len*/, Z_NO_FLUSH);
   }

   png_compress_IDAT(png_ptr, filtered_row, row_bytes, flush);
}

static void
write_unfiltered_rowbits(png_structrp png_ptr, png_const_bytep filtered_row,
   unsigned int row_bits, png_byte filter /*if at start of row*/,
   int flush)
{
   /* Same as above, but it correctly clears the unused bits in a partial
    * byte.
    */
   const png_uint_32 row_bytes = row_bits >> 3;

   debug(filter == PNG_FILTER_VALUE_NONE || filter == PNG_FILTER_VALUE_LAST);

   if (row_bytes > 0U)
   {
      row_bits -= row_bytes << 3;
      write_filtered_row(png_ptr, filtered_row, row_bytes, filter,
            row_bits == 0U ? flush : Z_NO_FLUSH);
      filter = PNG_FILTER_VALUE_LAST; /* written */
   }

   /* Handle a partial byte. */
   if (row_bits > 0U)
   {
      png_byte buffer[1];

      buffer[0] = PNG_BYTE(filtered_row[row_bytes] & ~(0xFFU >> row_bits));
      write_filtered_row(png_ptr, buffer, 1U, filter, flush);
   }
}

#ifdef PNG_WRITE_FILTER_SUPPORTED
static void
filter_block_singlebyte(unsigned int row_bytes, png_bytep sub_row,
   png_bytep up_row, png_bytep avg_row, png_bytep paeth_row,
   png_const_bytep row, png_const_bytep prev_row, png_bytep prev_pixels)
{
   /* Calculate rows for all four filters where the input has one byte per pixel
    * (more accurately per filter-unit).
    */
   png_byte a = prev_pixels[0];
   png_byte c = prev_pixels[1];

   while (row_bytes-- > 0U)
   {
      const png_byte x = *row++;
      const png_byte b = prev_row == NULL ? 0U : *prev_row++;

      /* Calculate each filtered byte in turn: */
      if (sub_row != NULL) *sub_row++ = 0xFFU & (x - a);
      if (up_row != NULL) *up_row++ = 0xFFU & (x - b);
      if (avg_row != NULL) *avg_row++ = 0xFFU & (x - (a+b)/2U);

      /* Paeth is a little more difficult: */
      if (paeth_row != NULL)
      {
         int pa = b-c;   /* a+b-c - a */
         int pb = a-c;   /* a+b-c - b */
         int pc = pa+pb; /* a+b-c - c = b-c + a-c */
         png_byte p = a;

         pa = abs(pa);
         pb = abs(pb);
         if (pa > pb) pa = pb, p = b;
         if (pa > abs(pc)) p = c;

         *paeth_row++ = 0xFFU & (x - p);
      }

      /* And set a and c for the next pixel: */
      a = x;
      c = b;
   }

   /* Store a and c for the next block: */
   prev_pixels[0] = a;
   prev_pixels[1] = c;
}

static void
filter_block_multibyte(unsigned int row_bytes,
   const unsigned int bpp, png_bytep sub_row, png_bytep up_row,
   png_bytep avg_row, png_bytep paeth_row, png_const_bytep row,
   png_const_bytep prev_row, png_bytep prev_pixels)
{
   /* Calculate rows for all four filters, the input is a block of bytes such
    * that row_bytes is a multiple of bpp.  bpp can be 2, 3, 4, 6 or 8.
    * prev_pixels will be updated to the last pixels processed.
    */
   while (row_bytes >= bpp)
   {
      unsigned int i;

      for (i=0; i<bpp; ++i)
      {
         const png_byte a = prev_pixels[i];
         const png_byte c = prev_pixels[i+bpp];
         const png_byte b = prev_row == NULL ? 0U : *prev_row++;
         const png_byte x = *row++;

         /* Save for the next pixel: */
         prev_pixels[i] = x;
         prev_pixels[i+bpp] = b;

         /* Calculate each filtered byte in turn: */
         if (sub_row != NULL) *sub_row++ = 0xFFU & (x - a);
         if (up_row != NULL) *up_row++ = 0xFFU & (x - b);
         if (avg_row != NULL) *avg_row++ = 0xFFU & (x - (a+b)/2U);

         /* Paeth is a little more difficult: */
         if (paeth_row != NULL)
         {
            int pa = b-c;   /* a+b-c - a */
            int pb = a-c;   /* a+b-c - b */
            int pc = pa+pb; /* a+b-c - c = b-c + a-c */
            png_byte p = a;

            pa = abs(pa);
            pb = abs(pb);
            if (pa > pb) pa = pb, p = b;
            if (pa > abs(pc)) p = c;

            *paeth_row++ = 0xFFU & (x - p);
         }
      }

      row_bytes -= i;
   }
}

static void
filter_block(png_const_bytep prev_row, png_bytep prev_pixels,
      png_const_bytep unfiltered_row, unsigned int row_bits,
      const unsigned int bpp, png_bytep sub_row, png_bytep up_row,
      png_bytep avg_row, png_bytep paeth_row)
{
   const unsigned int row_bytes = row_bits >> 3; /* complete bytes */

   if (bpp <= 8U)
   {
      /* There may be a partial byte at the end. */
      if (row_bytes > 0)
         filter_block_singlebyte(row_bytes, sub_row, up_row, avg_row, paeth_row,
               unfiltered_row, prev_row, prev_pixels);

      /* The partial byte must be handled correctly here; both the previous row
       * value and the current value need to have non-present bits cleared.
       */
      if ((row_bits & 7U) != 0)
      {
         const png_byte mask = PNG_BYTE(~(0xFFU >> (row_bits & 7U)));
         png_byte buffer[2];

         buffer[0] = unfiltered_row[row_bytes] & mask;

         if (prev_row != NULL)
            buffer[1U] = prev_row[row_bytes] & mask;

         else
            buffer[1U] = 0U;

         filter_block_singlebyte(1U,
               sub_row == NULL ? NULL : sub_row+row_bytes,
               up_row == NULL ? NULL : up_row+row_bytes,
               avg_row == NULL ? NULL : avg_row+row_bytes,
               paeth_row == NULL ? NULL : paeth_row+row_bytes,
               buffer, buffer+1U, prev_pixels);
      }
   }

   else
      filter_block_multibyte(row_bytes, bpp >> 3,
            sub_row, up_row, avg_row, paeth_row,
            unfiltered_row, prev_row, prev_pixels);
}

static void
filter_row(png_structrp png_ptr, png_const_bytep prev_row,
      png_bytep prev_pixels, png_const_bytep unfiltered_row,
      unsigned int row_bits, unsigned int bpp, unsigned int filter,
      int start_of_row, int flush)
{
   /* filters_to_try identifies a single filter and it is not PNG_FILTER_NONE.
    */
   png_byte filtered_row[PNG_ROW_BUFFER_SIZE];

   affirm((row_bits+7U) >> 3 <= PNG_ROW_BUFFER_SIZE &&
          filter >= PNG_FILTER_VALUE_SUB && filter <= PNG_FILTER_VALUE_PAETH);
   debug((row_bits % bpp) == 0U);

   filter_block(prev_row, prev_pixels, unfiltered_row, row_bits, bpp,
         filter == PNG_FILTER_VALUE_SUB   ? filtered_row : NULL,
         filter == PNG_FILTER_VALUE_UP    ? filtered_row : NULL,
         filter == PNG_FILTER_VALUE_AVG   ? filtered_row : NULL,
         filter == PNG_FILTER_VALUE_PAETH ? filtered_row : NULL);

   write_filtered_row(png_ptr, filtered_row, (row_bits+7U)>>3,
         start_of_row ? filter : PNG_FILTER_VALUE_LAST, flush);
}

/* Allow the application to select one or more row filters to use. */
static png_int_32
set_filter(png_zlib_statep ps, unsigned int filtersIn)
{
   /* Notice that PNG_NO_FILTERS is 0 and passes this test; this is OK because
    * filters then gets set to PNG_FILTER_NONE, as is required.
    *
    * The argument to this routine is actually an (int), but conversion to
    * (unsigned int) is safe because it leaves the top bits set which results in
    * PNG_EDOM below.
    */
   if (filtersIn < PNG_FILTER_NONE)
      filtersIn = PNG_FILTER_MASK(filtersIn);

   /* PNG_ALL_FILTERS is a constant, unfortunately it is nominally signed, for
    * historical reasons, hence the PNG_BIC_MASK here.
    */
   if ((filtersIn & PNG_BIC_MASK(PNG_ALL_FILTERS)) == 0U)
   {
#     ifndef PNG_SELECT_FILTER_SUPPORTED
         filtersIn &= -filtersIn; /* Use lowest set bit */
#     endif /* !SELECT_FILTER */

      return ps->filter_mask = filtersIn & PNG_ALL_FILTERS;
   }

   else /* Out-of-range filtersIn: */
      return PNG_EDOM;
}
#endif /* WRITE_FILTER */

#ifdef PNG_WRITE_FILTER_SUPPORTED
void /* PRIVATE */
png_write_start_IDAT(png_structrp png_ptr)
{
   png_zlib_statep ps = get_zlib_state(png_ptr);

   /* Set up the IDAT compression state.  Expect the state to have been released
    * by the previous owner, but it doesn't much matter if there was an error.
    * Note that the stream is not claimed yet.
    */
   debug(png_ptr->zowner == 0U);

   /* This sets the buffer limits and write_row_size, which is used below. */
   png_zlib_state_set_buffer_limits(png_ptr, ps);

   if (ps->filter_mask == 0)
   {
#     ifdef PNG_SELECT_FILTER_SUPPORTED
         /* Now default the filter mask if it hasn't been set already: */
         int png_level =
            pz_get(ps, IDAT, png_level, PNG_DEFAULT_COMPRESSION_LEVEL);

         /* If the bit depth is less than 8, so pixels are not byte aligned, PNG
          * filtering hardly ever helps because there is no correlation between
          * the bytes on which the filter works and the actual pixel values.
          * Note that GIF is a whole lot better at this because it uses LZW to
          * compress a bit-stream, not a byte stream as in the deflate
          * implementation of LZ77.
          *
          * If the row size is less than 256 bytes filter selection algorithms
          * are flakey because the restricted range of codes in each row can
          * lead to poor selection of filters, particularly if the bytes in the
          * image are themselves limited.  (This happens when a low bit-depth
          * image is encoded with 8-bit channels.)
          *
          * By experiment with the test set of images the breakpoint between
          * not filtering and filtering based on which gives best compression by
          * row size is as follows:
          *
          *            NONE        FAST        ALL
          *    PAL   <=anything [even 8-bit palette images larger if filtered]
          *    G<8   <=anything [low bit depth gray images]
          *    G8      <=16        [+~1%]      >16
          *    G16     <=128       [+~1%]      >128
          *    GA8     <=64        [+~1%]      >64
          *    GA16  <=anything [always better without filtering!]
          *    RGB8    <=32        [+0-2%(1)]  >32
          *    RGB16  <=1024       [+~1%]      >1024
          *    RGBA8   <=64        [+~~1%]     >64
          *    RGBA16  <=128       {+~0.5%]    >128
          *
          * (1) The largest 24-bit RGB image (RGB8) faired better, by 1.3%,
          * with 'fast' filters.  This is assumed to be random.
          *
          * Aggregated across all color types and bit depths the breakpoint for
          * filtering is >16 bytes, but the size increase only exceeds 0.5% for
          * images with rows between 64 and 128 bytes, hence the choices below.
          *
          * Across all the test images that change (not including selecting just
          * the 'fast' filters by default) does not change the compressed size
          * significantly (+0.06% across the whole test set), however it does
          * substantially increase the number of images without filtering.
          *
          * Using just none and sub filters results in overall compressed sizes
          * somewhere around the geometric mean of no filtering and 'fast'.
          *
          * The image size also plays a part.  Filtering is not an advantage for
          * images of size <= 512 bytes.  This is also reflected below.
          *
          * NOTE: the libpng 1.6 (and earlier) algorithm seems to work
          * because it biases the byte codes in the output towards 0 and 255.
          * Zlib doesn't care what the codes are, but Huffman encoding always
          * benefits from a biased distribution and the filters themselves were
          * designed to produce values in this range.
          *
          * In a raw comparison with the legacy code selection of specific sets
          * of filters always increased the compressed size of the test set, as
          * follows:
          *
          *    PNG_ALL_FILTERS:  +0.26%
          *    PNG_FAST_FILTERS: +1.9%
          *    NONE+SUB:         +5.8%
          *    PNG_NO_FILTERS:   +14%
          *
          * This mainly proves that a static selection of filters (without
          * considering the PNG format) is always worse than the legacy
          * algorithm below.
          *
          * NOTE: ps->filter_mask must be set to a mask value, not a simple
          * PNG_FILTER_VALUE_ number.
          */
         if (ps->write_row_size == 0U /* row cannot be buffered */)
            ps->filter_mask = PNG_FILTER_NONE;

         else if (png_level == PNG_COMPRESSION_COMPAT/* Legacy */)
         {
            if (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE ||
                png_ptr->bit_depth < 8U)
               ps->filter_mask = PNG_FILTER_NONE;

            else
               ps->filter_mask = PNG_ALL_FILTERS;
         }

         /* NOTE: overall with the following size tests (row and image size) the
          * test set of images end up 0.06% larger, however some color types are
          * smaller and some larger; the differences are minute.  If the test is
          * <=128 (which means <=129 bytes per row with the filter byte) the
          * resultant inclusion of 32x32 RGBA images results in significantly
          * increased compressed size.
          *
          * The test on png_level captures the following settings:
          *
          *    PNG_COMPRESSION_LOW_MEMORY
          *    PNG_COMPRESSION_HIGH_SPEED
          *    PNG_COMPRESSION_HIGH_READ_SPEED
          *
          * NOTE: this relies on the exact values in png.h!
          */
         else if (png_level <= PNG_COMPRESSION_HIGH_READ_SPEED
               || png_ptr->color_type == PNG_COLOR_TYPE_PALETTE
               || png_ptr->bit_depth < 8U
               || ps->write_row_size/*does not include filter*/ < 128U
               || png_image_size(png_ptr) <= 512U)
            ps->filter_mask = PNG_FILTER_NONE;

         /* ELSE: there are at least 128 bytes in every row and the pixels
          * are multiples of a byte.
          */
         else switch (png_level)
         {
            default: /* For GCC */
            case PNG_COMPRESSION_LOW:
               ps->filter_mask = PNG_FILTER_NONE+PNG_FILTER_SUB;
               break;

            case PNG_COMPRESSION_MEDIUM:
               ps->filter_mask = PNG_FAST_FILTERS;
               break;

            case PNG_COMPRESSION_HIGH:
               ps->filter_mask = PNG_ALL_FILTERS;
               break;
         }
#     else /* !SELECT_FILTER */
         ps->filter_mask = PNG_FILTER_NONE;
#     endif /* !SELECT_FILTER */
   }
}

static png_byte
png_write_start_row(png_zlib_statep ps, int start_of_pass, int no_previous_row)
   /* Called at the start of a row to set up anything required for filter
    * handling in the row.  Sets png_zlib_state::filters to a single filter.
    */
{
   unsigned int mask = ps->filter_mask;

   /* If we see a previous-row filter in mask and png_zlib_state::save_row is
    * still unset set it.  This means that the first time a previous-row filter
    * is seen row-saving gets turned on.
    */
   if (ps->save_row == SAVE_ROW_UNSET && (mask & PREVIOUS_ROW_FILTERS) != 0U)
      ps->save_row = SAVE_ROW_DEFAULT;

   if ((no_previous_row /* row not stored */ && !start_of_pass) ||
       ps->save_row == SAVE_ROW_OFF /* disabled by app */ ||
       ps->write_row_size == 0U /* row too large to buffer */)
      mask &= PNG_BIC_MASK(PREVIOUS_ROW_FILTERS);

   /* On the first row of a pass Paeth is equivalent to sub and up is equivalent
    * to none, so try to simplify the mask in in this case.
    */
   else if (start_of_pass) {
#     define MATCH(flags) ((mask & (flags)) == (flags))
      if (MATCH(PNG_FILTER_NONE|PNG_FILTER_UP))
         mask &= PNG_BIC_MASK(PNG_FILTER_UP);

      if (MATCH(PNG_FILTER_SUB|PNG_FILTER_PAETH))
         mask &= PNG_BIC_MASK(PNG_FILTER_PAETH);
#     undef MATCH
   }

#  ifdef PNG_SELECT_FILTER_SUPPORTED
      if ((mask & (mask-1U)) == 0U /* single bit set */ ||
          ps->write_row_size == 0U /* row cannot be buffered */)
#  endif /* SELECT_FILTER */
   /* Convert the lowest set bit into the corresponding value.  If no bits
    * are set select NONE.  After this switch statement the value of
    * ps->filters is guaranteed to just be a single filter.
    */
   switch (mask & -mask)
   {
      default:               mask = PNG_FILTER_VALUE_NONE;  break;
      case PNG_FILTER_SUB:   mask = PNG_FILTER_VALUE_SUB;   break;
      case PNG_FILTER_UP:    mask = PNG_FILTER_VALUE_UP;    break;
      case PNG_FILTER_AVG:   mask = PNG_FILTER_VALUE_AVG;   break;
      case PNG_FILTER_PAETH: mask = PNG_FILTER_VALUE_PAETH; break;
   }

   return ps->filters = PNG_BYTE(mask);
}

static png_bytep
allocate_row(png_structrp png_ptr, png_const_bytep data, png_alloc_size_t size)
   /* Utility to allocate and save some row bytes.  If the result is NULL the
    * allocation failed and the png_zlib_struct will have been updated to
    * prevent further allocation attempts.
    */
{
   const png_zlib_statep ps = png_ptr->zlib_state;
   png_bytep buffer;

   debug(ps->write_row_size > 0U);

   /* OOM is handled silently, as is the case where the row is too large to
    * buffer.
    */
   buffer = png_voidcast(png_bytep,
         png_malloc_base(png_ptr, ps->write_row_size));

   /* Setting write_row_size to 0 switches on the code for handling a row that
    * is too large to buffer.  This will kick in next time round, i.e. on the
    * next row.
    */
   if (buffer == NULL)
      ps->write_row_size = 0U;

   else
      memcpy(buffer, data, size);

   return buffer;
}
#endif /* WRITE_FILTER */

#ifdef PNG_SELECT_FILTER_SUPPORTED
/* Bit set operations.  Not in ANSI C-90 but commonly available in highly
 * optimized versions, hence the ifndef.  These operations just work on bitsets
 * of size 256.  The second argument (the code index) may be evaluated multiple
 * times.
 */
#ifndef PNG_CODE_SET /* Can be set in pngpriv.h */
typedef png_uint_32 png_codeset[8];
#  define PNG_CODE_MASK(i) (((png_uint_32)1U) << ((i) & 0x1FU))
#  define PNG_CODE_IS_SET(c,i) (((c)[(i) >> 5] & PNG_CODE_MASK(i)))
#  define PNG_CODE_SET(c,i) (((c)[(i) >> 5] |= PNG_CODE_MASK(i)))
#  define PNG_CODE_CLEAR(c,i) (((c)[(i) >> 5] &= ~PNG_CODE_MASK(i)))
#endif /* !PNG_CODE_SET */

typedef struct filter_selector
{
   /* Persistent filter selection information (stored across row boundaries).
    * A code is not considered if it last occured more than 'window' bytes ago.
    * The deflate algorithm means that 'window' cannot exceed 8453377, however
    * practical versions may be far less.  When 'distance' reaches 'window' any
    * code where:
    *
    *    distance - code_distance[code] > window
    *
    * at the end of a row 'code' is removed from codeset.  Otherwise
    * (rearranging the above):
    *
    *    distance - window <= code_distance[code]
    *
    * and so the distances of the still active codes can be reduced:
    *
    *    code_distance[code] -= distance-window
    *    distance = window
    *
    * This prevents any wrap of 'distance' on a row which is shorter than
    * 2^32-window.
    *
    * However when then row is 2^32-window or more bytes long (the row can be up
    * to just under 2^34 bytes long) this algorithm doesn't work; 'distance'
    * will  overflow in the middle of the row and all codes are relevant.  This
    * is handled below simply by reseting the set of present codes at the start
    * of the row and ignoring the overflow.
    */
   unsigned int code_count;         /* Number of distinct codes seen */
   int          png_level;          /* Cached compression level */
   png_uint_32  filter_select_max_width;
      /* The maximum number of pixels which can be fitted in the window without
       * filling the entire window (i.e. the maximum number that can be fitted
       * in (window-1) bytes).
       */
   png_uint_32  sum_bias[PNG_FILTER_VALUE_LAST];
      /* For each filter a measure of its cost in the filter sum calculation.
       * This allows filter selection based on the sum-of-absolute-dfferences
       * method to be biased to favour particular filters.  There was no such
       * bias before 1.7 and the filter byte was ignored.
       */
   png_uint_32  distance;           /* Distance from beginning */
   png_codeset  codeset;            /* Set of seen codes */
   png_uint_32  code_distance[256]; /* Distance at last occurence */
}  filter_selector;

static const filter_selector *
png_start_filter_select(png_zlib_statep ps, unsigned int bpp)
{
#  define png_ptr ps_png_ptr(ps)
   filter_selector *fs = ps->selector;

   if (fs == NULL)
   {
      fs = png_voidcast(filter_selector*, png_malloc_base(png_ptr, sizeof *fs));

      if (fs != NULL)
      {
         png_uint_32 window = ps->filter_select_window;
         fs->png_level = pz_get(ps, IDAT, png_level,
               PNG_DEFAULT_COMPRESSION_LEVEL);

         /* Delay initialize this here: */
         if (window < 3U || window > PNG_FILTER_SELECT_WINDOW_MAX)
            ps->filter_select_window = window = PNG_FILTER_SELECT_WINDOW_MAX;

         fs->code_count = 0;

         switch (fs->png_level)
         {
            default:
               /* TODO: investigate other settings */
               {
                  unsigned int f;

                  for (f=0; f<PNG_FILTER_VALUE_LAST; ++f)
                     fs->sum_bias[f] = f;
               }
               ps->filter_select_threshold = 64U; /* 6bit RGB */
               ps->filter_select_threshold2 = 50U; /* TODO: experiment! */
               break;

            case PNG_COMPRESSION_COMPAT: /* Legacy */
               memset(fs->sum_bias, 0U, sizeof fs->sum_bias);
               ps->filter_select_threshold = 1U; /* disabled */
               ps->filter_select_threshold2 = 1U;
               break;
         }

         /* This is the maximum row width, in pixels, of a row which fits and
          * leaves 1 byte free in the window.  For any bigger row filter
          * selection ignores the previous rows.
          */
         fs->filter_select_max_width = ((window-2U/*filter+last byte*/)*8U)/bpp;
         fs->distance = 0U;
         memset(fs->codeset, 0U, sizeof fs->codeset);
         /* fs->code_distance is left uninitialized because fs->codeset says
          * whether or not each entry has been initialized.
          */
         ps->selector = fs;
      }

      else
         ps->write_row_size = 0U; /* OOM */
   }
#  undef png_ptr

   return fs;
}

typedef struct
{
   /* Per-filter data.  This remains separate from the above until the filter
    * selection has been made.  It reflects the above however the codeset only
    * records codes present in this row.
    *
    * The 'sum' fields are the sum of the absolute deviation of each code from
    * 0, the algorithm from 1.6 and earlier.  In other words:
    *
    *    if (code >= 128)
    *       sum += code;
    *    else
    *       sum += 256-code;
    */
   unsigned int code_count;         /* Number of distinct codes seen in row */
   unsigned int new_code_count;     /* Number of new codes seen in row */
   png_uint_32  sum_low;            /* Low 31 bits of code sum */
   png_uint_32  sum_high;           /* High 32 bits of code sum */
   png_codeset  codeset;            /* Set of codes seen in this row */
   png_uint_32  code_distance[256]; /* Distance at last occurence in this row */
}  filter_data;

static void
filter_data_init(filter_data *fd, png_uint_32 distance, unsigned int filter,
      unsigned int code_is_set, png_uint_32 bias)
{
   fd->code_count = 1U;
   fd->new_code_count = !code_is_set;
   fd->sum_low = bias;
   fd->sum_high = 0U;
   memset(&fd->codeset, 0U, sizeof fd->codeset);
   PNG_CODE_SET(fd->codeset, filter);
   fd->code_distance[filter] = distance;
}

static void
add_code(const filter_selector *fs, filter_data *fd, png_uint_32 distance,
      unsigned int code)
{
   if (!PNG_CODE_IS_SET(fd->codeset, code))
   {
      PNG_CODE_SET(fd->codeset, code);
      ++(fd->code_count);
      fd->code_distance[code] = distance;
      if (!PNG_CODE_IS_SET(fs->codeset, code))
         ++(fd->new_code_count);
   }

   {
      png_uint_32 low = fd->sum_low;

      if (code <= 128U)
         low += code;

      else
         low += 256U-code;

      /* Handle overflow into the top bit: */
      if (low & 0x80000000U)
         fd->sum_low = low & 0x7FFFFFFFU, ++fd->sum_high;

      else
         fd->sum_low = low;
   }
}

static png_byte
filter_data_select(png_zlib_statep ps, filter_data fd[PNG_FILTER_VALUE_LAST],
      unsigned int filter, png_uint_32 distance, png_uint_32 w)
{
#  define png_ptr ps_png_ptr(ps)
   /* Choose how to do this depending on the row and window size. */
   filter_selector *fs = ps->selector;
   png_uint_32 window = ps->filter_select_window;

   affirm(fs != NULL);

   /* Check the width against the maximum number of pixels that can fit in a
    * window without filling it:
    */
   if (w > fs->filter_select_max_width)
   {
      /* The cache is not used */
      fs->distance = 0U; /* for next row */
      fs->code_count = 0U;
      memset(fs->codeset, 0U, sizeof fs->codeset);
   }

   else
   {
      /* Merge the two code sets, discounting codes that last occurred before
       * the start of the window.
       */
      png_uint_32 adjust, code_count;
      unsigned int code;

      /* filter_selector::distance is the distance of the first byte in the row
       * (the filter byte), but 'distance' can wrap on long rows.  The above
       * test is meant to exclude the wrap case by excluding any case where the
       * row has as many bytes as the window, so:
       */
      affirm(distance > fs->distance && distance - fs->distance < window);

      /* Set 'adjust' to the current distance of the start of the window.  I.e:
       *
       *    +---------------+--------+
       *    | before window | window | future data
       *    +---------------+--------+
       *                    A        A
       *                    |        |
       *             adjust +        + distance
       *
       * If the window isn't full yet 'adjust' will be zero, otherwise all the
       * distances will be reduced by 'adjust' so that the first byte of the
       * window has distance 0.
       */
      if (distance > window)
         adjust = distance-window;

      else
         adjust = 0;


      /* This may be decreased below if some old codes only occured before the
       * start of the window.
       */
      code_count = fs->code_count + fd->new_code_count;

      for (code=0U; code<256U; ++code)
      {
         if (PNG_CODE_IS_SET(fd[filter].codeset, code))
         {
            PNG_CODE_SET(fs->codeset, code);
            debug(fd[filter].code_distance[code] >= adjust);
            fs->code_distance[code] = fd[filter].code_distance[code] - adjust;
         }

         else if (PNG_CODE_IS_SET(fs->codeset, code) && adjust > 0)
         {
            /* The code did not occur in this row, the old distance may now be
             * outside the window (because adjust is non-zero).
             */
            const png_uint_32 d = fs->code_distance[code];

            if (d >= adjust)
               fs->code_distance[code] = d-adjust;

            else
               PNG_CODE_CLEAR(fs->codeset, code), --code_count;
         }
      }

      fs->code_count = code_count;
      fs->distance = distance - adjust; /* I.e. either distance or window! */
   }

   return ps->filters = PNG_BYTE(filter);
#  undef png_ptr
}

static png_byte
select_filter(png_zlib_statep ps, png_const_bytep row,
   png_const_bytep prev, unsigned int bpp, png_uint_32 width, int start_of_pass)
   /* Select a filter from the list provided by png_write_start_row. */
{
   png_byte filters = png_write_start_row(ps, start_of_pass, prev == NULL);

#  define png_ptr ps_png_ptr(ps)
   if (filters >= PNG_FILTER_NONE) /* multiple filters to test */
   {
      const png_uint_32 max_pixels = ps->row_buffer_max_pixels;
      const png_uint_32 block_pixels = ps->row_buffer_max_aligned_pixels;
      const filter_selector *fs = ps->selector;
      png_uint_32 pixels_to_go = width;
      png_uint_32 distance;
      unsigned int bits_at_end = 0U;
      png_byte prev_pixels[4*2*2]; /* 2 pixels up to 4x2-bytes each */
      filter_data fd[PNG_FILTER_VALUE_LAST];

      debug((filters & (filters-1)) != 0U); /* Expect more than one bit! */

      if (fs == NULL)
      {
         /* Delay initialize with a quiet OOM handler */
         fs = png_start_filter_select(ps, bpp);
         if (fs == NULL)
         {
            ps->filters = PNG_FILTER_VALUE_NONE;
            return PNG_FILTER_VALUE_NONE;
         }
      }

      /* If PNG_FILTER_NONE is in the list check it first. */
      if (filters & PNG_FILTER_NONE)
      {
         png_const_bytep rp = row;
         png_uint_32 w = width;

         distance = fs->distance;
         filter_data_init(fd+PNG_FILTER_VALUE_NONE, distance++,
               PNG_FILTER_VALUE_NONE,
               PNG_CODE_IS_SET(fs->codeset, PNG_FILTER_VALUE_NONE),
               fs->sum_bias[PNG_FILTER_VALUE_NONE]);

         if (bpp >= 8) /* complete bytes */
         {
            const unsigned int bytes = bpp/8U;

            while (w > 0)
            {
               unsigned int b;
               for (b=0; b<bytes; ++b)
                  add_code(fs, fd+PNG_FILTER_VALUE_NONE, distance++, *rp++);
               --w;
            }
         }

         else /* multiple pixels per byte */
         {
            const unsigned int ppb = 8U/bpp;

            debug(ppb * bpp == 8U); /* Expect bpp to be a power of 2 */

            while (w >= ppb)
            {
               add_code(fs, fd+PNG_FILTER_VALUE_NONE, distance++, *rp++);
               w -= ppb;
            }

            if (w > 0) /* partial byte at end */
               add_code(fs, fd+PNG_FILTER_VALUE_NONE, distance++,
                     *rp & (0xFFU >> (w*bpp) /* zero unused bits */));
         }

         /* For PNG data with a small number of codes it is worth skipping the
          * filtering because it almost always increases the code count
          * significantly.  This is controlled by
          * png_zlib_state::filter_select_threshold and causes an early return
          * here.
          */
         if (fd[PNG_FILTER_VALUE_NONE].new_code_count +
                  fs->code_count < ps->filter_select_threshold)
            return filter_data_select(ps, fd, PNG_FILTER_VALUE_NONE, distance,
                  width);
      } /* PNG_FILTER_NONE */

      memset(prev_pixels, 0U, sizeof prev_pixels);
      distance = fs->distance;

      {
         unsigned int i;

         for (i=PNG_FILTER_VALUE_NONE+1U; i<PNG_FILTER_VALUE_LAST; ++i)
            if (PNG_FILTER_MASK(i) & filters)
               filter_data_init(fd+i, distance, i,
                     PNG_CODE_IS_SET(fs->codeset, i), fs->sum_bias[i]);
      }

      ++distance;

      while (pixels_to_go || bits_at_end)
      {
         unsigned int bits, i;
         union
         {
            PNG_ROW_BUFFER_ALIGN_TYPE force_buffer_alignment;
            png_byte row[4][PNG_ROW_BUFFER_SIZE];
         }  filtered;
         union
         {
            PNG_ROW_BUFFER_ALIGN_TYPE force_buffer_alignment;
            png_byte byte;
         }  last;

         if (pixels_to_go)
         {
            if (pixels_to_go > max_pixels)
            {
               /* Maintain alignment by consuming on block_pixels at once */
               bits = block_pixels * bpp;
               pixels_to_go -= block_pixels; /* May be 0 */
            }

            else
            {
               bits = pixels_to_go * bpp;
               bits_at_end = bits & 0x7U;
               bits -= bits_at_end;
               pixels_to_go = 0U; /* +bits_at_end */
            }
         }

         else /* incomplete byte at the end of the pixel */
         {
            /* Make sure the unused bits are cleared (to zero, although this is
             * an arbitrary choice):
             */
            last.byte = PNG_BYTE(*row & ~(0xFFU >> bits_at_end));
            row = &last.byte;
            bits = bits_at_end;
            bits_at_end = 0U;
         }

         filter_block(prev, prev_pixels, row, bits, bpp,
                  filtered.row[0/*sub*/], filtered.row[1/*up*/],
                  filtered.row[2/*avg*/], filtered.row[3/*Paeth*/]);

         /* A block of (bits+7)/8 bytes is now available to process. */
         for (i=0; 8U*i < bits; ++i, ++distance)
         {
            unsigned int f;

            for (f=PNG_FILTER_VALUE_NONE+1U; f<PNG_FILTER_VALUE_LAST; ++f)
               if (PNG_FILTER_MASK(f) & filters)
                  add_code(fs, fd+f, distance, filtered.row[f-1U][i]);
         }

         if (prev != NULL)
            prev += bits >> 3;

         row += bits >> 3;
      }

      /* Now look at the candidate filters, including 'none' and select the
       * best.  We know that 'none' increases the code count beyond the
       * threshold, so if the old code count is below the threshold and there is
       * a filter which does not increase the code count select it; doing so
       * should do no harm to the overall compression.
       */
      if (fs->code_count < ps->filter_select_threshold)
      {
         unsigned int f, min_new_count = 257U, min_f = PNG_FILTER_VALUE_NONE;

         for (f=PNG_FILTER_VALUE_NONE+1U; f<PNG_FILTER_VALUE_LAST; ++f)
            if ((PNG_FILTER_MASK(f) & filters) != 0)
            {
               unsigned int new_code_count = fd[f].new_code_count;

               if (new_code_count == 0U)
                  return filter_data_select(ps, fd, f, distance, width);

               else if (new_code_count < min_new_count)
                  min_new_count = new_code_count, min_f = f;
            }

         /* Use the second threshold to decide whether to select the best filter
          * on this basis alone:
          */
         if (min_f != PNG_FILTER_VALUE_NONE &&
             fs->code_count + min_new_count < ps->filter_select_threshold2)
            return filter_data_select(ps, fd, min_f, distance, width);
      }

      /* Now fall back to the libpng 1.6 and earlier algorithm.  This favours
       * the filter which produces least deviation in the codes from 0.  When
       * this works it does so by reducing the distribution of code values.  The
       * filters implicitly encode the difference between a predictor based on
       * adjacent values, the assumption is that this will result in values
       * close to 0.
       */
      {
         png_uint_32 high = -1;
         png_uint_32 low = -1;
         unsigned int min_f = 0 /*unset, but safe*/;
         unsigned int f;

         for (f=PNG_FILTER_VALUE_NONE; f<PNG_FILTER_VALUE_LAST; ++f)
            if ((PNG_FILTER_MASK(f) & filters) != 0 &&
                (fd[f].sum_high < high ||
                 (fd[f].sum_high == high && fd[f].sum_low < low)))
            {
               high = fd[f].sum_high;
               low = fd[f].sum_low;

               if (low & 0x80000000U)
               {
                  low &= 0x7FFFFFFFU, --high;
                  if (high & 0x80000000U)
                     low = high = 0U;
               }

               min_f = f;
            }

         return filter_data_select(ps, fd, min_f, distance, width);
      }
   }

   debug(filters < PNG_FILTER_VALUE_LAST);
   return ps->filters = filters;
#  undef png_ptr
}
#else /* !SELECT_FILTER */
   /* Filter selection not being done, just call png_write_start_row: */
#  define select_filter(ps, rp, pp, bpp, width, start_of_pass)\
      png_write_start_row((ps), (start_of_pass), (pp) == NULL)
#endif /* !SELECT_FILTER */

/* This is the common function to write multiple rows of PNG data.  The data is
 * in the relevant PNG format but has had no filtering done.
 */
void /* PRIVATE */
png_write_png_rows(png_structrp png_ptr, png_const_bytep *rows,
    png_uint_32 num_rows)
{
   const png_zlib_statep ps = png_ptr->zlib_state;
   const unsigned int bpp = png_ptr->row_output_pixel_depth;
#  ifdef PNG_WRITE_FILTER_SUPPORTED
      png_const_bytep previous_row = ps->previous_write_row;
#  else /* !WRITE_FILTER */
      /* These are constant in the no-filer case: */
      const png_byte filter = PNG_FILTER_VALUE_NONE;
      const png_uint_32 max_pixels = ps->zlib_max_pixels;
      const png_uint_32 block_pixels = ps->zlib_max_aligned_pixels;
#  endif /* !WRITE_FILTER */
   /* Write the given rows handling the png_compress_IDAT argument limitations
    * (uInt) and any valid row width.
    */
   png_uint_32 last_row_in_pass = 0U; /* Actual last, not last+1! */
   png_uint_32 pixels_in_pass = 0U;
   unsigned int first_row_in_pass = 0U; /* For do_interlace */
   unsigned int pixels_at_end = 0U; /* for a partial byte at the end */
   unsigned int base_info_flags = png_row_end;
   int pass = -1; /* Invalid: force calculation first time round */

   debug(png_ptr->row_output_pixel_depth == PNG_PIXEL_DEPTH(*png_ptr));

   while (num_rows-- > 0U)
   {
      if (png_ptr->pass != pass)
      {
         /* Recalcuate the row bytes and partial bits */
         pass = png_ptr->pass;
         pixels_in_pass = png_ptr->width;

         if (png_ptr->interlaced == PNG_INTERLACE_NONE)
         {
            debug(pass == 0);
            last_row_in_pass = png_ptr->height - 1U;
            base_info_flags |= png_pass_last; /* there is only one */
         }

         else
         {
            const png_uint_32 height = png_ptr->height;

            last_row_in_pass = PNG_PASS_ROWS(height, pass);
            debug(pass >= 0 && pass < 7);

#           ifdef PNG_WRITE_INTERLACING_SUPPORTED
               if (png_ptr->do_interlace)
               {
                  /* libpng is doing the interlace handling, the row number is
                   * actually the row in the image.
                   *
                   * This overflows when the PNG height is such that the are no
                   * rows in this pass.  This does not matter; because there are
                   * no rows the value doesn't get used.
                   */
                  last_row_in_pass =
                     PNG_ROW_FROM_PASS_ROW(last_row_in_pass-1U, pass);
                  first_row_in_pass = PNG_PASS_START_ROW(pass);
               }

               else /* Application handles the interlace */
#           endif /* WRITE_INTERLACING */
            {
               /* The row does exist, so this works without checking the column
                * count.
                */
               debug(last_row_in_pass > 0U);
               last_row_in_pass -= 1U;
            }

            if (pass == PNG_LAST_PASS(pixels_in_pass/*PNG width*/, height))
               base_info_flags |= png_pass_last;

            /* Finally, adjust pixels_in_pass for the interlacing (skip the
             * final pass; it is full width).
             */
            if (pass < 6)
               pixels_in_pass = PNG_PASS_COLS(pixels_in_pass, pass);
         }

         /* Mask out the bits in a partial byte. */
         pixels_at_end = pixels_in_pass & PNG_ADDOF(bpp);

#        ifdef PNG_WRITE_FILTER_SUPPORTED
            /* Reset the previous_row pointer correctly; NULL at the start of
             * the pass.  If row_number is not 0 then a previous write_rows was
             * interrupted in mid-pass and any required buffer should be in
             * previous_write_row (set in the initializer).
             */
            if (png_ptr->row_number == first_row_in_pass)
               previous_row = NULL;
#        endif /* WRITE_FILTER */
      }

#     ifdef PNG_WRITE_INTERLACING_SUPPORTED
         /* When libpng is handling the interlace we see rows that must be
          * skipped.
          */
         if (!png_ptr->do_interlace ||
             PNG_ROW_IN_INTERLACE_PASS(png_ptr->row_number, pass))
#     endif /* WRITE_INTERLACING */
      {
         const unsigned int row_info_flags = base_info_flags |
            (png_ptr->row_number ==
               first_row_in_pass ? png_pass_first_row : 0) |
            (png_ptr->row_number == last_row_in_pass ? png_pass_last_row : 0);
         const int flush = row_flush(ps, row_info_flags);
         png_const_bytep row = *rows;
         png_uint_32 pixels_to_go = pixels_in_pass;
#        ifdef PNG_WRITE_FILTER_SUPPORTED
            /* The filter can change each time round.  Call png_write_start_row
             * to resolve any changes.  Note that when this function is used to
             * do filter selection from png_write_png_data on the first row
             * png_write_start_row will get called twice.
             */
            const png_byte filter = select_filter(ps, row, previous_row, bpp,
                  pixels_in_pass, png_ptr->row_number == first_row_in_pass);
            const png_uint_32 max_pixels = filter == PNG_FILTER_VALUE_NONE ?
               ps->zlib_max_pixels : ps->row_buffer_max_pixels;
            const png_uint_32 block_pixels = filter == PNG_FILTER_VALUE_NONE ?
               ps->zlib_max_aligned_pixels : ps->row_buffer_max_aligned_pixels;

            /* The row handling uses png_compress_IDAT directly if there is no
             * filter to be applied, otherwise it uses filter_row.
             */
            if (filter != PNG_FILTER_VALUE_NONE)
            {
               int start_of_row = 1;
               png_byte prev_pixels[4*2*2]; /* 2 pixels up to 4x2-bytes each */

               memset(prev_pixels, 0U, sizeof prev_pixels);

               while (pixels_to_go > max_pixels)
               {
                  /* Write a block at once to maintain alignment */
                  filter_row(png_ptr, previous_row, prev_pixels, row,
                        bpp * block_pixels, bpp, filter, start_of_row,
                        Z_NO_FLUSH);

                  if (previous_row != NULL)
                     previous_row += (block_pixels * bpp) >> 3;

                  row += (block_pixels * bpp) >> 3;
                  pixels_to_go -= block_pixels;
                  start_of_row = 0;
               }

               /* The filter code handles the partial byte at the end correctly,
                * so this is all that is required:
                */
               if (pixels_to_go > 0)
                  filter_row(png_ptr, previous_row, prev_pixels, row,
                        bpp * pixels_to_go, bpp, filter, start_of_row, flush);
            }

            else
#        endif /* WRITE_FILTER */

         {
            /* The no-filter case. */
            const uInt block_bytes = (uInt)/*SAFE*/(
               bpp <= 8U ?
                  block_pixels >> PNG_SHIFTOF(bpp) :
                  block_pixels * (bpp >> 3));

            /* png_write_start_IDAT guarantees this, but double check for
             * overflow above in debug:
             */
            debug((block_bytes & (PNG_ROW_BUFFER_BYTE_ALIGN-1U)) == 0U);

            /* The filter has to be written here: */
            png_compress_IDAT(png_ptr, &filter, 1U/*len*/, Z_NO_FLUSH);

            /* Process blocks of pixels up to the limit. */
            while (pixels_to_go > max_pixels)
            {
               png_compress_IDAT(png_ptr, row, block_bytes, Z_NO_FLUSH);
               row += block_bytes;
               pixels_to_go -= block_pixels;
            }

            /* Now compress the remainder; pixels_to_go <= max_pixels so it will
             * fit in a uInt.
             */
            {
               const png_uint_32 remainder =
                   bpp <= 8U
                     ? (pixels_to_go-pixels_at_end) >> PNG_SHIFTOF(bpp)
                     : (pixels_to_go-pixels_at_end) * (bpp >> 3);

               if (remainder > 0U)
                  png_compress_IDAT(png_ptr, row, remainder,
                      pixels_at_end > 0U ? Z_NO_FLUSH : flush);

               else
                  debug(pixels_at_end > 0U);

               if (pixels_at_end > 0U)
               {
                  /* There is a final partial byte.  This is PNG format so the
                   * left-most bits are the most significant.
                   */
                  const png_byte last = PNG_BYTE(row[remainder] &
                        ~(0xFFU >> (pixels_at_end * bpp)));

                  png_compress_IDAT(png_ptr, &last, 1U, flush);
               }
            }
         }

         png_write_end_row(png_ptr, flush);

#        ifdef PNG_WRITE_FILTER_SUPPORTED
            previous_row = *rows;
#        endif /* WRITE_FILTER */
#        undef HANDLE
      } /* row in pass */

#     ifdef PNG_WRITE_INTERLACING_SUPPORTED
         else /* row not in pass; just skip it */
         {
            if (++png_ptr->row_number >= png_ptr->height)
            {
               debug(png_ptr->row_number == png_ptr->height);

               png_ptr->row_number = 0U;
               png_ptr->pass = 0x7U & (pass+1U);
            }
         }
#     endif /* WRITE_INTERLACING */

      ++rows;
   } /* while num_rows */

#  ifdef PNG_WRITE_FILTER_SUPPORTED
      /* previous_row must be copied back unless we don't need it because the
       * next row is the first one in the pass (this relies on png_write_end_row
       * setting row_number to 0 at the end!)
       */
      if (png_ptr->row_number != 0U && previous_row != NULL && SAVE_ROW(ps) &&
          ps->previous_write_row != previous_row/*all rows skipped*/)
      {
#        ifdef PNG_SELECT_FILTER_SUPPORTED
            /* We might be able to avoid any copy. */
            if (ps->current_write_row == previous_row)
            {
               png_bytep old = ps->previous_write_row;
               ps->previous_write_row = ps->current_write_row;
               ps->current_write_row = old; /* may be NULL */
            }

            else
#        endif /* SELECT_FILTER */

         if (ps->previous_write_row != NULL)
            memcpy(ps->previous_write_row, previous_row,
                   png_calc_rowbytes(png_ptr, bpp, pixels_in_pass));

         else
            ps->previous_write_row = allocate_row(png_ptr, previous_row,
                  png_calc_rowbytes(png_ptr, bpp, pixels_in_pass));
      }
#  endif /* WRITE_FILTER */
}

#ifdef PNG_WRITE_FILTER_SUPPORTED
/* This filters the row, chooses which filter to use, if it has not already
 * been specified by the application, and then writes the row out with the
 * chosen filter.
 */
static void
write_png_data(png_structrp png_ptr, png_const_bytep prev_row,
      png_bytep prev_pixels, png_const_bytep unfiltered_row, png_uint_32 x,
      unsigned int row_bits, unsigned int row_info_flags)
   /* This filters the row appropriately and returns an updated prev_row
    * (updated for 'x').
    */
{
   const png_zlib_statep ps = png_ptr->zlib_state;
   const unsigned int bpp = png_ptr->row_output_pixel_depth;
   const int flush = row_flush(ps, row_info_flags);
   const png_byte filter = ps->filters; /* just one */

   /* These invariants are expected from the caller: */
   affirm(row_bits <= 8U*PNG_ROW_BUFFER_SIZE);
   debug(filter < PNG_FILTER_VALUE_LAST/*sic: last+1*/);

   /* Now choose the correct filter implementation according to the number of
    * filters in the filters_to_try list.  The prev_row parameter is made
    * NULL on the first row because it is uninitialized at that point.
    */
   if (filter == PNG_FILTER_VALUE_NONE)
      write_unfiltered_rowbits(png_ptr, unfiltered_row, row_bits,
            x == 0 ? PNG_FILTER_VALUE_NONE : PNG_FILTER_VALUE_LAST, flush);

   else
      filter_row(png_ptr,
         (row_info_flags & png_pass_first_row) ? NULL : prev_row,
         prev_pixels, unfiltered_row, row_bits, bpp, filter, x == 0, flush);

   /* Handle end of row: */
   if ((row_info_flags & png_row_end) != 0)
      png_write_end_row(png_ptr, flush);
}

void /* PRIVATE */
png_write_png_data(png_structrp png_ptr, png_bytep prev_pixels,
    png_const_bytep unfiltered_row, png_uint_32 x,
    unsigned int width/*pixels*/, unsigned int row_info_flags)
{
   const png_zlib_statep ps = png_ptr->zlib_state;

   affirm(ps != NULL);

   {
      const unsigned int bpp = png_ptr->row_output_pixel_depth;
      const unsigned int row_bits = width * bpp;
      png_bytep prev_row = ps->previous_write_row;

      debug(bpp <= 64U && width <= 65535U &&
            width < 65535U/bpp); /* Expensive: only matters on 16-bit */

      /* This is called once before starting a new row here, but below it is
       * only called once between starting a new list of rows.
       */
      if (x == 0)
         png_write_start_row(ps, (row_info_flags & png_pass_first_row) != 0,
               prev_row == NULL);

      /* If filter selection is required the filter will have at least one mask
       * bit set.
       */
#     ifdef PNG_SELECT_FILTER_SUPPORTED
         if (ps->filters >= PNG_FILTER_NONE/*lowest mask bit*/)
         {
            /* If the entire row is passed in the input process it via
             * immediately, otherwise the row must be buffered for later
             * analysis.
             */
            png_const_bytep row;

            if (x > 0 || (row_info_flags & png_row_end) == 0)
            {
               /* The row must be saved for later. */
               png_bytep buffer = ps->current_write_row;

               /* png_write_start row should always check this: */
               debug(ps->write_row_size > 0U);

               if (buffer != NULL)
                  memcpy(buffer + png_calc_rowbytes(png_ptr, bpp, x),
                        unfiltered_row, (row_bits + 7U) >> 3);


               else if (x == 0U)
                  ps->current_write_row = buffer = allocate_row(png_ptr,
                        unfiltered_row, (row_bits + 7U) >> 3);

               row = buffer;
            }

            else
               row = unfiltered_row;

            if (row != NULL) /* else out of memory */
            {
               /* At row end, process the save buffer. */
               if ((row_info_flags & png_row_end) != 0)
                  png_write_png_rows(png_ptr, &row, 1U);

               /* Early return to skip the single-filter code */
               return;
            }

            /* Caching the row failed, so process the row using the lowest set
             * filter.  The allocation error should only ever happen at the
             * start of the row.  If this goes wrong the output will have been
             * damaged.
             */
            affirm(x == 0U);
         }
#     endif /* SELECT_FILTER */

      /* prev_row is either NULL or the position in the previous row buffer */
      if (prev_row != NULL && x > 0)
         prev_row += png_calc_rowbytes(png_ptr, bpp, x);

      /* This is the single filter case (no selection): */
      write_png_data(png_ptr, prev_row, prev_pixels, unfiltered_row, x,
            row_bits, row_info_flags);

      /* Copy the current row into the previous row buffer, if available, unless
       * this is the last row in the pass, when there is no point.  Note that
       * write_previous_row may have garbage in a partial byte at the end as a
       * result of this memcpy.
       */
      if (!(row_info_flags & png_pass_last_row) && SAVE_ROW(ps)) {
         if (prev_row != NULL)
            memcpy(prev_row, unfiltered_row, (row_bits + 7U) >> 3);

         /* NOTE: if the application sets png_zlib_state::save_row in a callback
          * it isn't possible to do the save until the next row.  allocate_row
          * handles OOM silently by turning off the save.
          */
         else if (x == 0) /* can allocate the save buffer */
            ps->previous_write_row =
               allocate_row(png_ptr, unfiltered_row, (row_bits + 7U) >> 3);
      }
   }
}
#else /* !WRITE_FILTER */
void /* PRIVATE */
png_write_start_IDAT(png_structrp png_ptr)
{
   png_zlib_statep ps = get_zlib_state(png_ptr);

   /* Set up the IDAT compression state.  Expect the state to have been released
    * by the previous owner, but it doesn't much matter if there was an error.
    * Note that the stream is not claimed yet.
    */
   debug(png_ptr->zowner == 0U);

   /* This sets the buffer limits and write_row_size, which is used below. */
   png_zlib_state_set_buffer_limits(png_ptr, ps);
}

void /* PRIVATE */
png_write_png_data(png_structrp png_ptr, png_bytep prev_pixels,
    png_const_bytep unfiltered_row, png_uint_32 x,
    unsigned int width/*pixels*/, unsigned int row_info_flags)
{
   const unsigned int bpp = png_ptr->row_output_pixel_depth;
   int flush;
   png_uint_32 row_bits;

   row_bits = width;
   row_bits *= bpp;
   /* These invariants are expected from the caller: */
   affirm(width < 65536U && bpp <= 64U && width < 65536U/bpp &&
         row_bits <= 8U*PNG_ROW_BUFFER_SIZE);

   affirm(png_ptr->zlib_state != NULL);
   flush = row_flush(png_ptr->zlib_state, row_info_flags);

   write_unfiltered_rowbits(png_ptr, unfiltered_row, row_bits,
         x == 0 ? PNG_FILTER_VALUE_NONE : PNG_FILTER_VALUE_LAST, flush);

   PNG_UNUSED(prev_pixels)

   /* Handle end of row: */
   if ((row_info_flags & png_row_end) != 0)
      png_write_end_row(png_ptr, flush);
}
#endif /* !WRITE_FILTER */

png_int_32 /* PRIVATE */
png_write_setting(png_structrp png_ptr, png_uint_32 setting,
    png_uint_32 parameter, png_int_32 value)
{
   /* Caller checks the arguments for basic validity */
   int only_get = (setting & PNG_SF_GET) != 0U;

   setting &= ~PNG_SF_GET;

   switch (setting)
   {
      /* Settings in png_struct: */
      case PNG_SW_IDAT_size:
         if (parameter > 0 && parameter <= PNG_UINT_31_MAX)
         {
            if (!only_get)
               png_ptr->IDAT_size = parameter;

            return 0; /* set ok */
         }

         else
            return PNG_EINVAL;

      /* Settings in zlib_state: */
         case PNG_SW_COMPRESS_png_level:
            return compression_setting(png_ptr, parameter, png_level, value,
                only_get);

#     ifdef PNG_WRITE_CUSTOMIZE_COMPRESSION_SUPPORTED
         case PNG_SW_COMPRESS_zlib_level:
            return compression_setting(png_ptr, parameter, level, value,
                only_get);

         case PNG_SW_COMPRESS_windowBits:
            return compression_setting(png_ptr, parameter, windowBits, value,
                only_get);

         case PNG_SW_COMPRESS_memLevel:
            return compression_setting(png_ptr, parameter, memLevel, value,
                only_get);

         case PNG_SW_COMPRESS_strategy:
            return compression_setting(png_ptr, parameter, strategy, value,
                only_get);

         case PNG_SW_COMPRESS_method:
            if (value != 8) /* Only supported method */
               return PNG_EINVAL;
            return 8; /* old method */
#     endif /* WRITE_CUSTOMIZE_COMPRESSION */

#     ifdef PNG_WRITE_FILTER_SUPPORTED
         case PNG_SW_COMPRESS_filters:
            /* The method must match that in the IHDR: */
            if (parameter == png_ptr->filter_method)
            {
               if (!only_get)
                  return set_filter(get_zlib_state(png_ptr), value);

               else if (png_ptr->zlib_state != NULL &&
                   png_ptr->zlib_state->filter_mask != 0U/*unset*/)
                  return png_ptr->zlib_state->filter_mask;

               else
                  return PNG_UNSET;
            }

            else /* Invalid filter method */
               return PNG_EINVAL;

         case PNG_SW_COMPRESS_row_buffers:
            /* New in 1.7.0: direct control of the buffering. */
            switch (parameter)
            {
               case 0:
                  if (!only_get)
                     get_zlib_state(png_ptr)->save_row = SAVE_ROW_OFF;
                  return 0;

               case 1:
                  if (!only_get)
                     get_zlib_state(png_ptr)->save_row = SAVE_ROW_ON;
                  return 1;

               default:
                  return PNG_ENOSYS; /* no support for bigger values */
            }
#     endif /* WRITE_FILTER */

#     ifdef PNG_WRITE_FLUSH_SUPPORTED
         case PNG_SW_FLUSH:
            /* Set the automatic flush interval or 0 to turn flushing off */
            if (!only_get)
               get_zlib_state(png_ptr)->flush_dist =
                  value <= 0 ? 0xEFFFFFFFU : (png_uint_32)/*SAFE*/value;

            return 0;
#     endif /* WRITE_FLUSH */

#     ifdef PNG_WRITE_CHECK_FOR_INVALID_INDEX_SUPPORTED
         case PNG_SRW_CHECK_FOR_INVALID_INDEX:
            /* The 'enabled' value is a FORTRAN style three-state: */
            if (value > 0)
               png_ptr->palette_index_check = PNG_PALETTE_CHECK_ON;

            else if (value < 0)
               png_ptr->palette_index_check = PNG_PALETTE_CHECK_OFF;

            else
               png_ptr->palette_index_check = PNG_PALETTE_CHECK_DEFAULT;

            return 0;
#     endif /* WRITE_CHECK_FOR_INVALID_INDEX */

#     ifdef PNG_BENIGN_WRITE_ERRORS_SUPPORTED
         case PNG_SRW_ERROR_HANDLING:
            /* The parameter is a bit mask of what to set, the value is what to
             * set it to.  PNG_IDAT_ERRORS is ignored on write.
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

               return 0;
            }

            return PNG_EINVAL;
#     endif /* BENIGN_WRITE_ERRORS */

      default:
         return PNG_ENOSYS; /* not supported (whatever it is) */
   }
}
#endif /* WRITE */
