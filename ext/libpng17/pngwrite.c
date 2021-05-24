
/* pngwrite.c - general routines to write a PNG file
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
#ifdef PNG_SIMPLIFIED_WRITE_STDIO_SUPPORTED
#  include <errno.h>
#endif /* SIMPLIFIED_WRITE_STDIO */

#define PNG_SRC_FILE PNG_SRC_FILE_pngwrite

#ifdef PNG_WRITE_SUPPORTED

#ifdef PNG_WRITE_UNKNOWN_CHUNKS_SUPPORTED
/* Write out all the unknown chunks for the current given location */
static void
write_unknown_chunks(png_structrp png_ptr, png_const_inforp info_ptr,
    unsigned int where)
{
   if (info_ptr->unknown_chunks_num != 0)
   {
      png_const_unknown_chunkp up;

      png_debug(5, "writing extra chunks");

      for (up = info_ptr->unknown_chunks;
           up < info_ptr->unknown_chunks + info_ptr->unknown_chunks_num;
           ++up)
         if ((up->location & where) != 0)
      {
         /* If per-chunk unknown chunk handling is enabled use it, otherwise
          * just write the chunks the application has set.
          */
#ifdef PNG_SET_UNKNOWN_CHUNKS_SUPPORTED
         int keep = png_handle_as_unknown(png_ptr, up->name);

         /* NOTE: this code is radically different from the read side in the
          * matter of handling an ancillary unknown chunk.  In the read side
          * the default behavior is to discard it, in the code below the default
          * behavior is to write it.  Critical chunks are, however, only
          * written if explicitly listed or if the default is set to write all
          * unknown chunks.
          *
          * The default handling is also slightly weird - it is not possible to
          * stop the writing of all unsafe-to-copy chunks!
          *
          * TODO: REVIEW: this would seem to be a bug.
          */
         if (keep != PNG_HANDLE_CHUNK_NEVER &&
             ((up->name[3] & 0x20) /* safe-to-copy overrides everything */ ||
              keep == PNG_HANDLE_CHUNK_ALWAYS ||
              (keep == PNG_HANDLE_CHUNK_AS_DEFAULT &&
               png_ptr->unknown_default == PNG_HANDLE_CHUNK_ALWAYS)))
#endif
            png_write_chunk(png_ptr, up->name, up->data, up->size);
      }
   }
}
#endif /* WRITE_UNKNOWN_CHUNKS */

#ifdef PNG_WRITE_TEXT_SUPPORTED
static void
png_write_text(png_structrp png_ptr, png_const_inforp info_ptr, png_byte where)
   /* Text chunk helper */
{
   int i;

   /* Check to see if we need to write text chunks */
   for (i = 0; i < info_ptr->num_text; i++)
   {
      png_debug2(2, "Writing text chunk %d, type %d", i,
            info_ptr->text[i].compression);

      /* Text chunks are written at info_ptr->text[i].location, skip the chunk
       * if we are not writing at that location:
       */
      if ((info_ptr->text[i].location & where) == 0U)
         continue;

      switch (info_ptr->text[i].compression)
      {
         case PNG_ITXT_COMPRESSION_NONE:
         case PNG_ITXT_COMPRESSION_zTXt:
#           ifdef PNG_WRITE_iTXt_SUPPORTED
               /* Write international chunk */
               png_write_iTXt(png_ptr, info_ptr->text[i].compression,
                     info_ptr->text[i].key, info_ptr->text[i].lang,
                     info_ptr->text[i].lang_key, info_ptr->text[i].text);
#           else /* !WRITE_iTXT */
               png_app_error(png_ptr, "Unable to write international text");
#           endif /* !WRITE_iTXT */
            break;

         case PNG_TEXT_COMPRESSION_zTXt:
#           ifdef PNG_WRITE_zTXt_SUPPORTED
               /* Write compressed chunk */
               png_write_zTXt(png_ptr, info_ptr->text[i].key,
                     info_ptr->text[i].text, info_ptr->text[i].compression);
#           else /* !WRITE_zTXT */
               png_app_error(png_ptr, "Unable to write compressed text");
#           endif /* !WRITE_zTXT */
            break;

         case PNG_TEXT_COMPRESSION_NONE:
#           ifdef PNG_WRITE_tEXt_SUPPORTED
               /* Write uncompressed chunk */
               png_write_tEXt(png_ptr, info_ptr->text[i].key,
                     info_ptr->text[i].text, 0);
#           else /* !WRITE_tEXt */
               /* Can't get here TODO: why not? */
               png_app_error(png_ptr, "Unable to write uncompressed text");
#           endif /* !WRITE_tEXt */
            break;

         default:
            /* This is an internal error because the libpng checking should
             * never manage to set any 'compression' except the above values.
             */
            impossible("invalid text compression");
      }

      /* The chunk was written, record where.  This allows the location to have
       * multiple bits set; the first successful write freezes the location.
       */
      info_ptr->text[i].location = where;
   }
}
#endif /* WRITE_TEXT */

/* Writes all the PNG information.  This is the suggested way to use the
 * library.  If you have a new chunk to add, make a function to write it,
 * and put it in the correct location here.  If you want the chunk written
 * after the image data, put it in png_write_end().  I strongly encourage
 * you to supply a PNG_INFO_ flag, and check info_ptr->valid before writing
 * the chunk, as that will keep the code from breaking if you want to just
 * write a plain PNG file.  If you have long comments, I suggest writing
 * them in png_write_end(), and compressing them.
 */
void PNGAPI
png_write_info_before_PLTE(png_structrp png_ptr, png_const_inforp info_ptr)
{
   png_debug(1, "in png_write_info_before_PLTE");

   if (png_ptr == NULL || info_ptr == NULL)
      return;

   if ((png_ptr->mode & PNG_HAVE_IHDR) == 0)
   {
      int color_type = PNG_COLOR_TYPE_FROM_FORMAT(info_ptr->format);

      /* Write PNG signature; doesn't set PNG_HAVE_PNG_SIGNATURE if it has
       * already been written (or rather, if at least 3 bytes have already been
       * written; undocumented wackiness, it means the 'PNG' at the start can be
       * replace by, e.g. "FOO" or "BAR" or "MNG").
       */
      png_write_sig(png_ptr);

#     ifdef PNG_MNG_FEATURES_SUPPORTED
         if ((png_ptr->mode & PNG_HAVE_PNG_SIGNATURE) != 0 &&
             png_ptr->mng_features_permitted != 0)
         {
            png_app_error(png_ptr,
                "MNG features are not allowed in a PNG datastream");
            /* Recovery: disable MNG features: */
            png_ptr->mng_features_permitted = 0;
         }
#     endif /* MNG_FEATURES */

      /* Write IHDR information. */
      png_write_IHDR(png_ptr, info_ptr->width, info_ptr->height,
          info_ptr->bit_depth, color_type, info_ptr->compression_type,
          info_ptr->filter_type, info_ptr->interlace_type);

#     ifdef PNG_WRITE_TRANSFORMS_SUPPORTED
         /* This are used for checking later on: */
         png_ptr->info_format = info_ptr->format;
#     endif /* WRITE_TRANSFORMS */

      /* This sets the flag that prevents re-entry to the 'before PLTE' case: */
      affirm((png_ptr->mode & PNG_HAVE_IHDR) != 0);

      /* The rest of these check to see if the valid field has the appropriate
       * flag set, and if it does, writes the chunk.
       *
       * 1.6.0: COLORSPACE support controls the writing of these chunks too, and
       * the chunks will be written if the WRITE routine is there and
       * information is available in the COLORSPACE.  (See
       * png_colorspace_sync_info in png.c for where the valid flags get set.)
       *
       * Under certain circumstances the colorspace can be invalidated without
       * syncing the info_struct 'valid' flags; this happens if libpng detects
       * an error and calls png_error while the color space is being set, yet
       * the application continues writing the PNG.  So check the 'invalid'
       * flag here too.
       */
#     ifdef PNG_WRITE_tIME_SUPPORTED
         if ((info_ptr->valid & PNG_INFO_tIME) != 0 &&
             (info_ptr->time_location & PNG_HAVE_IHDR) != 0)
            png_write_tIME(png_ptr, &(info_ptr->mod_time));
#     endif /* WRITE_tIME */

#     ifdef PNG_WRITE_gAMA_SUPPORTED /* enables GAMMA */
         if ((info_ptr->colorspace.flags & PNG_COLORSPACE_INVALID) == 0 &&
             (info_ptr->colorspace.flags & PNG_COLORSPACE_FROM_gAMA) != 0 &&
             (info_ptr->valid & PNG_INFO_gAMA) != 0)
         {
            /* This is the inverse of the test in png.c: */
            affirm(info_ptr->colorspace.gamma >= 16 &&
                   info_ptr->colorspace.gamma <= 625000000);
            png_write_gAMA_fixed(png_ptr, info_ptr->colorspace.gamma);
         }
#     endif /* WRITE_gAMA */

      /* Write only one of sRGB or an ICC profile.  If a profile was supplied
       * and it matches one of the known sRGB ones issue a warning.
       */
#     ifdef PNG_WRITE_iCCP_SUPPORTED /* enables COLORSPACE, GAMMA */
         if ((info_ptr->colorspace.flags & PNG_COLORSPACE_INVALID) == 0 &&
             (info_ptr->valid & PNG_INFO_iCCP) != 0)
         {
#           ifdef PNG_WRITE_sRGB_SUPPORTED
               /* The app must have supplied an sRGB iCCP profile (and one that
                * is recognized and therefore known to be correct) so we write
                * that profile, even though it increases the size of the PNG
                * significantly.  A warning is reasonable:
                */
               if ((info_ptr->valid & PNG_INFO_sRGB) != 0)
                  png_app_warning(png_ptr,
                      "profile matches sRGB but writing iCCP instead");
#           endif /* WRITE_sRGB */

            png_write_iCCP(png_ptr, info_ptr->iccp_name,
                info_ptr->iccp_profile);
         }
#        ifdef PNG_WRITE_sRGB_SUPPORTED
            else /* iCCP not written */
#        endif /* WRITE_sRGB */
#     endif /* WRITE_iCCP */

#     ifdef PNG_WRITE_sRGB_SUPPORTED /* enables COLORSPACE, GAMMA */
         if ((info_ptr->colorspace.flags & PNG_COLORSPACE_INVALID) == 0 &&
             (info_ptr->valid & PNG_INFO_sRGB) != 0)
            png_write_sRGB(png_ptr, info_ptr->colorspace.rendering_intent);
#     endif /* WRITE_sRGB */

#     ifdef PNG_WRITE_sBIT_SUPPORTED
         if ((info_ptr->valid & PNG_INFO_sBIT) != 0)
            png_write_sBIT(png_ptr, &(info_ptr->sig_bit), color_type);
#     endif /* WRITE_sBIT */

#     ifdef PNG_WRITE_cHRM_SUPPORTED /* enables COLORSPACE */
         if ((info_ptr->colorspace.flags & PNG_COLORSPACE_INVALID) == 0 &&
            (info_ptr->colorspace.flags & PNG_COLORSPACE_FROM_cHRM) != 0 &&
            (info_ptr->valid & PNG_INFO_cHRM) != 0)
            png_write_cHRM_fixed(png_ptr, &info_ptr->colorspace.end_points_xy);
#     endif /* WRITE_cHRM */

#     ifdef PNG_WRITE_TEXT_SUPPORTED
         if (info_ptr->num_text > 0)
            png_write_text(png_ptr, info_ptr, PNG_HAVE_IHDR);
#     endif /* WRITE_TEXT */

#     ifdef PNG_WRITE_UNKNOWN_CHUNKS_SUPPORTED
         /* The third arugment must encode only one bit, otherwise chunks will
          * be written twice because the test in write_unknown_chunks is
          * 'location & where'.
          */
         write_unknown_chunks(png_ptr, info_ptr, PNG_HAVE_IHDR);
#     endif
   }

   else /* 1.7.0: flag multiple calls; previously ignored */
      png_app_error(png_ptr,
          "png_write_info_before_PLTE called more than once");
}

void PNGAPI
png_write_info(png_structrp png_ptr, png_const_inforp info_ptr)
{
   png_debug(1, "in png_write_info");

   if (png_ptr == NULL || info_ptr == NULL)
      return;

   if ((png_ptr->mode & (PNG_HAVE_PLTE+PNG_HAVE_IDAT)) != 0)
   {
      png_app_error(png_ptr, "late call to png_write_info");
      return;
   }

   /* The app may do this for us, and in 1.7.0 multiple calls are flagged as an
    * application error, so this code must check:
    */
   if ((png_ptr->mode & PNG_HAVE_IHDR) == 0)
      png_write_info_before_PLTE(png_ptr, info_ptr);

   if ((info_ptr->valid & PNG_INFO_PLTE) != 0)
      png_write_PLTE(png_ptr, info_ptr->palette, info_ptr->num_palette);

   /* Validate the consistency of the PNG being produced; a palette must have
    * been written if a palette mapped PNG is to be valid:
    */
   if ((png_ptr->mode & PNG_HAVE_PLTE) == 0 &&
       png_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
      png_error(png_ptr, "Valid palette required for paletted images");

   /* But always set the mode flag because without this we don't know when to
    * write the post-palette text or unknown chunks.
    */
   png_ptr->mode |= PNG_HAVE_PLTE;

#  ifdef PNG_WRITE_tRNS_SUPPORTED
      if ((info_ptr->valid & PNG_INFO_tRNS) !=0)
      {
         png_write_tRNS(png_ptr, info_ptr->trans_alpha,
             &(info_ptr->trans_color), info_ptr->num_trans,
             PNG_COLOR_TYPE_FROM_FORMAT(info_ptr->format));
      }
#  endif /* WRITE_tRNS */

#  ifdef PNG_WRITE_bKGD_SUPPORTED
      if ((info_ptr->valid & PNG_INFO_bKGD) != 0)
         png_write_bKGD(png_ptr, &(info_ptr->background),
             PNG_COLOR_TYPE_FROM_FORMAT(info_ptr->format));
#  endif /* WRITE_bKGD */

#  ifdef PNG_WRITE_hIST_SUPPORTED
      if ((info_ptr->valid & PNG_INFO_hIST) != 0)
         png_write_hIST(png_ptr, info_ptr->hist, info_ptr->num_palette);
#  endif /* WRITE_hIST */

#  ifdef PNG_WRITE_oFFs_SUPPORTED
      if ((info_ptr->valid & PNG_INFO_oFFs) != 0)
         png_write_oFFs(png_ptr, info_ptr->x_offset, info_ptr->y_offset,
             info_ptr->offset_unit_type);
#  endif /* WRITE_oFFs */

#  ifdef PNG_WRITE_pCAL_SUPPORTED
      if ((info_ptr->valid & PNG_INFO_pCAL) != 0)
         png_write_pCAL(png_ptr, info_ptr->pcal_purpose, info_ptr->pcal_X0,
             info_ptr->pcal_X1, info_ptr->pcal_type, info_ptr->pcal_nparams,
             info_ptr->pcal_units, info_ptr->pcal_params);
#  endif /* WRITE_pCAL */

#  ifdef PNG_WRITE_sCAL_SUPPORTED
      if ((info_ptr->valid & PNG_INFO_sCAL) != 0)
         png_write_sCAL_s(png_ptr, info_ptr->scal_unit, info_ptr->scal_s_width,
             info_ptr->scal_s_height);
#  endif /* WRITE_sCAL */

#  ifdef PNG_WRITE_pHYs_SUPPORTED
      if ((info_ptr->valid & PNG_INFO_pHYs) != 0)
         png_write_pHYs(png_ptr, info_ptr->x_pixels_per_unit,
             info_ptr->y_pixels_per_unit, info_ptr->phys_unit_type);
#  endif /* WRITE_pHYs */

#  ifdef PNG_WRITE_tIME_SUPPORTED
      if ((info_ptr->valid & PNG_INFO_tIME) != 0 &&
          (info_ptr->time_location & PNG_HAVE_PLTE) != 0)
         png_write_tIME(png_ptr, &(info_ptr->mod_time));
#  endif /* WRITE_tIME */

#  ifdef PNG_WRITE_sPLT_SUPPORTED
      if ((info_ptr->valid & PNG_INFO_sPLT) != 0)
      {
         int i;

         for (i = 0; i < info_ptr->splt_palettes_num; i++)
            png_write_sPLT(png_ptr, info_ptr->splt_palettes + i);
      }
#  endif /* WRITE_sPLT */

#  ifdef PNG_WRITE_TEXT_SUPPORTED
      if (info_ptr->num_text > 0)
         png_write_text(png_ptr, info_ptr, PNG_HAVE_PLTE);
#  endif /* WRITE_TEXT */

#  ifdef PNG_WRITE_UNKNOWN_CHUNKS_SUPPORTED
      write_unknown_chunks(png_ptr, info_ptr, PNG_HAVE_PLTE);
#  endif /* WRITE_UNKNOWN_CHUNKS */
}

/* Writes the end of the PNG file.  If you don't want to write comments or
 * time information, you can pass NULL for info.  If you already wrote these
 * in png_write_info(), do not write them again here.  If you have long
 * comments, I suggest writing them here, and compressing them.
 */
void PNGAPI
png_write_end(png_structrp png_ptr, png_inforp info_ptr)
{
   png_debug(1, "in png_write_end");

   if (png_ptr == NULL)
      return;

   if ((png_ptr->mode &
         (PNG_HAVE_IHDR+PNG_HAVE_IDAT+PNG_AFTER_IDAT+PNG_HAVE_IEND)) !=
         (PNG_HAVE_IHDR+PNG_HAVE_IDAT+PNG_AFTER_IDAT))
   {
      /* Out of place png_write_end: */
      if ((png_ptr->mode & PNG_HAVE_IHDR) == 0)
         png_error(png_ptr, "Missing call to png_write_info");

      else if ((png_ptr->mode & PNG_HAVE_IDAT) == 0 && png_ptr->zowner == 0)
      {
         /* TODO: write unknown IDAT here, for the moment allow the app to write
          * IDAT then call write_end:
          */
         png_app_error(png_ptr, "No IDATs written into file");
         png_ptr->mode |= PNG_HAVE_IDAT+PNG_AFTER_IDAT;
      }

      else if ((png_ptr->mode & PNG_AFTER_IDAT) == 0)
      {
         affirm(png_ptr->zowner == png_IDAT);
         png_error(png_ptr, "incomplete PNG image"); /* unrecoverable */
      }

      else if ((png_ptr->mode & PNG_HAVE_IEND) != 0)
      {
         png_app_error(png_ptr, "multiple calls to png_write_end");
         return;
      }

      else
         impossible("not reached");
   }

   /* And double check that the image rows were all written; this is actually
    * a harmless error on an interlaced image because the image rows with
    * data were all passed in or the above check would not work.
    *
    * Don't do this if the IDAT came from unknowns (TBD) or the app, above.
    *
    * The check depends on the precise logic in png_write_row.
    */
   else if (png_ptr->pass != 7U)
      png_app_error(png_ptr, "png_write_row not called to last row");

   else
      debug(png_ptr->row_number == 0U);

   /* See if user wants us to write information chunks */
   if (info_ptr != NULL)
   {
#     ifdef PNG_WRITE_tIME_SUPPORTED
         /* Check to see if user has supplied a time chunk */
         if ((info_ptr->valid & PNG_INFO_tIME) != 0 &&
             (info_ptr->time_location & PNG_AFTER_IDAT) != 0)
            png_write_tIME(png_ptr, &(info_ptr->mod_time));
#     endif

#     ifdef PNG_WRITE_TEXT_SUPPORTED
         if (info_ptr->num_text > 0)
            png_write_text(png_ptr, info_ptr, PNG_AFTER_IDAT);
#     endif /* WRITE_TEXT */

#     ifdef PNG_WRITE_UNKNOWN_CHUNKS_SUPPORTED
         write_unknown_chunks(png_ptr, info_ptr, PNG_AFTER_IDAT);
#     endif
   }

   /* Write end of PNG file */
   png_write_IEND(png_ptr);

   /* This flush, added in libpng-1.0.8, removed from libpng-1.0.9beta03,
    * and restored again in libpng-1.2.30, may cause some applications that
    * do not set png_ptr->output_flush_fn to crash.  If your application
    * experiences a problem, please try building libpng with
    * PNG_WRITE_FLUSH_AFTER_IEND_SUPPORTED defined, and report the event to
    * png-mng-implement at lists.sf.net .
    */
#  ifdef PNG_WRITE_FLUSH_SUPPORTED
#     ifdef PNG_WRITE_FLUSH_AFTER_IEND_SUPPORTED
         if (png_ptr->output_flush_fn != NULL)
            png_ptr->output_flush_fn(png_ptr);
#     endif
#  endif
}

#ifdef PNG_CONVERT_tIME_SUPPORTED
void PNGAPI
png_convert_from_struct_tm(png_timep ptime, PNG_CONST struct tm * ttime)
{
   png_debug(1, "in png_convert_from_struct_tm");

   ptime->year = png_check_u16(0/*TODO: fixme*/, 1900 + ttime->tm_year);
   ptime->month = png_check_byte(0/*TODO: fixme*/, ttime->tm_mon + 1);
   ptime->day = png_check_byte(0/*TODO: fixme*/, ttime->tm_mday);
   ptime->hour = png_check_byte(0/*TODO: fixme*/, ttime->tm_hour);
   ptime->minute = png_check_byte(0/*TODO: fixme*/, ttime->tm_min);
   ptime->second = png_check_byte(0/*TODO: fixme*/, ttime->tm_sec);
}

void PNGAPI
png_convert_from_time_t(png_timep ptime, time_t ttime)
{
   struct tm *tbuf;

   png_debug(1, "in png_convert_from_time_t");

   tbuf = gmtime(&ttime);
   png_convert_from_struct_tm(ptime, tbuf);
}
#endif

/* Initialize png_ptr structure, and allocate any memory needed */
PNG_FUNCTION(png_structp,PNGAPI
png_create_write_struct,(png_const_charp user_png_ver, png_voidp error_ptr,
    png_error_ptr error_fn, png_error_ptr warn_fn),PNG_ALLOCATED)
{
#ifndef PNG_USER_MEM_SUPPORTED
   png_structrp png_ptr = png_create_png_struct(user_png_ver, error_ptr,
       error_fn, warn_fn, NULL, NULL, NULL);
#else
   return png_create_write_struct_2(user_png_ver, error_ptr, error_fn,
       warn_fn, NULL, NULL, NULL);
}

/* Alternate initialize png_ptr structure, and allocate any memory needed */
PNG_FUNCTION(png_structp,PNGAPI
png_create_write_struct_2,(png_const_charp user_png_ver, png_voidp error_ptr,
    png_error_ptr error_fn, png_error_ptr warn_fn, png_voidp mem_ptr,
    png_malloc_ptr malloc_fn, png_free_ptr free_fn),PNG_ALLOCATED)
{
   png_structrp png_ptr = png_create_png_struct(user_png_ver, error_ptr,
       error_fn, warn_fn, mem_ptr, malloc_fn, free_fn);
#endif /* USER_MEM */

   if (png_ptr != NULL)
   {
#     ifdef PNG_BENIGN_ERRORS_SUPPORTED
#        if !PNG_RELEASE_BUILD
            /* Always quit on error prior to release */
            png_ptr->benign_error_action = PNG_ERROR;
            png_ptr->app_warning_action = PNG_WARN;
            png_ptr->app_error_action = PNG_ERROR;
#        else /* RELEASE_BUILD */
            /* Allow benign errors on write, subject to app control. */
#           ifdef PNG_BENIGN_WRITE_ERRORS_SUPPORTED
               png_ptr->benign_error_action = PNG_WARN;
               png_ptr->app_error_action = PNG_WARN;
               png_ptr->app_warning_action = PNG_WARN;
#           else /* !BENIGN_WRITE_ERRORS */
               /* libpng build without benign error support; the application
                * author has to be assumed to be correct, so:
                */
               png_ptr->benign_error_action = PNG_ERROR;
               png_ptr->app_warning_action = PNG_WARN;
               png_ptr->app_error_action = PNG_ERROR;
#           endif /* !BENIGN_WRITE_ERRORS */
#        endif /* RELEASE_BUILD */
#     endif /* BENIGN_ERRORS */
   }

   return png_ptr;
}


#if defined(PNG_WRITE_INTERLACING_SUPPORTED) ||\
    defined(PNG_WRITE_TRANSFORMS_SUPPORTED)
static void
write_row_buffered(png_structrp png_ptr,
    png_const_bytep row, unsigned int row_info_flags,
    void (*copy_fn)(png_const_structrp png_ptr, png_bytep row_buffer,
        png_const_bytep row, png_uint_32 x, unsigned int count, unsigned int p),
    unsigned int copy_parameter)
{
   unsigned int max_pixels = png_max_pixel_block(png_ptr);
   const unsigned int pass = png_ptr->pass;
   const png_uint_32 width = png_ptr->interlaced == PNG_INTERLACE_NONE ?
      png_ptr->width : PNG_PASS_COLS(png_ptr->width, pass);
   png_uint_32 x;
   png_byte prev_pixels[4*2*2]; /* 2 pixels up to 4 2-byte channels each */

   memset(prev_pixels, 0U, sizeof prev_pixels);

   for (x = 0U; x < width; x += max_pixels)
   {
      union
      {
         PNG_ROW_BUFFER_ALIGN_TYPE force_buffer_alignment;
         png_byte buffer[PNG_ROW_BUFFER_SIZE];
      }  pixel_buffer;

      if (max_pixels > width - x)
         max_pixels = (unsigned int)/*SAFE*/(width - x);

      debug((row_info_flags & png_row_end) == 0U); /* must be set here at end */
      if (x + max_pixels >= width)
         row_info_flags |= png_row_end;

      /* Copy a block of input pixels into the buffer, effecting the interlace
       * on the way if required.  The argument is the number of pixels in the
       * buffer, not the number handled from the input which will be larger in
       * the interlaced case.
       */
      copy_fn(png_ptr, pixel_buffer.buffer, row, x, max_pixels, copy_parameter);

      /* Now pixel_buffer[0..max_pixels-1] contains max_pixels pixels which may
       * need to be transformed (the interlace has already been handled).
       */
#     ifdef PNG_WRITE_TRANSFORMS_SUPPORTED
         if (png_ptr->transform_list != NULL)
         {
            png_transform_control tc;

            /* The initial values are the memory format; this was worked out in
             * png_init_row_info below.
             */
            memset(&tc, 0, sizeof tc);
            tc.png_ptr = png_ptr;
            tc.sp = tc.dp = pixel_buffer.buffer;

            tc.width = max_pixels; /* width of block that we have */
            tc.format = png_ptr->row_format;
            tc.range = png_ptr->row_range;
            tc.bit_depth = png_ptr->row_bit_depth;
            /* tc.init == 0 */
            /* tc.caching: not used */
            /* tc.palette: not used */
            debug(PNG_TC_PIXEL_DEPTH(tc) == png_ptr->row_input_pixel_depth);

            /* Run the list. */
            png_run_transform_list_backwards(png_ptr, &tc);

            /* Make sure the format that resulted is compatible with PNG: */
            affirm((tc.format & PNG_BIC_MASK(PNG_FORMAT_FLAG_ALPHA +
               PNG_FORMAT_FLAG_COLOR + PNG_FORMAT_FLAG_LINEAR +
               PNG_FORMAT_FLAG_COLORMAP)) == 0);

            /* Now we must have the PNG format from the IHDR: */
            affirm(png_ptr->bit_depth == tc.bit_depth &&
               png_ptr->color_type == PNG_COLOR_TYPE_FROM_FORMAT(tc.format));
         }
#     endif /* WRITE_TRANSFORMS */

      /* Call png_write_png_data to write this block of data, the test on
       * maxpixels says if this is the final block in the row.
       */
      png_write_png_data(png_ptr, prev_pixels, pixel_buffer.buffer, x,
          max_pixels, row_info_flags);
   }
}
#endif /* WRITE { INTERLACING || TRANSFORMS } */

#ifdef PNG_WRITE_TRANSFORMS_SUPPORTED
static void
copy_row(png_const_structrp png_ptr, png_bytep row_buffer,
    png_const_bytep row, png_uint_32 x, unsigned int count,
    unsigned int pixel_depth)
{
   /* Copy row[x..x+count] pixels to row_buffer. */
   png_copy_row(png_ptr, row_buffer, row, x, count, pixel_depth, 1/*clear*/,
       0/* x_in_dest; row[x]->row_buffer */);
}
#endif /* WRITE_TRANSFORMS */

#ifdef PNG_WRITE_INTERLACING_SUPPORTED
static void
interlace_row_lbd(png_const_structrp png_ptr, png_bytep dp, png_const_bytep sp,
    png_uint_32 x, unsigned int count, const unsigned int B)
{
   /* Pick out the correct pixels for the interlace pass.  The basic idea here
    * is to go through the row with a source pointer and a destination pointer
    * (sp and dp), and copy the correct pixels for the pass.  As the row gets
    * compacted, sp will always be >= dp, so we should never overwrite anything.
    * See the default: case for the easiest code to understand.
    */
   const unsigned int pass = png_ptr->pass;
   png_uint_32 i = PNG_COL_FROM_PASS_COL(x, pass);
   const unsigned int inc = PNG_PASS_COL_OFFSET(pass);

   /* For pixels less than one byte wide the correct pixels have to be
    * extracted from the input bytes.  Because we are reading data in
    * the application memory format we cannot rely on the PNG big
    * endian order.  Notice that this was apparently broken before
    * 1.7.0.
    *
    * In libpng 1.7.0 libpng uses a classic bit-pump to optimize the
    * extraction.  In all passes before the last (6/7) no two pixels
    * are adjacent in the input, so we are always extracting 1 bit.
    * At present the code uses an 8-bit buffer to avoid coding for
    * different byte sexes, but this could easily be changed.
    *
    * 'i' is the bit-index of bit in the input (sp[]), so,
    * considering the 1-bit per pixel case, sp[i>>3] is the byte
    * and the bit is bit (i&7) (0 lowest) on swapped (little endian)
    * data or 7-(i&7) on PNG default (big-endian) data.
    *
    * Define these macros, where:
    *
    *    B: the log2 bit depth (0, 1, 2 for 1bpp, 2bpp or 4bpp) of
    *       the data; this should be a constant.
    *   sp: the source pointer (sp) (a png_const_bytep)
    *    i: the pixel index in the input (png_uint_32)
    *    j: the bit index in the output (unsigned int)
    *
    * Unlike 'i', 'j' is interpreted directly; for LSB bytes it counts
    * up, for MSB it counts down.
    *
    * NOTE: this could all be expanded to eliminate the code below by
    * the time honoured copy'n'paste into three separate functions.  This
    * might be worth doing in the future.
    */
#     define PIXEL_MASK     ((1U << (1<<B))-1U)
#     define BIT_MASK       ((1U << (3-(B)))-1U) /* within a byte */
#     define SP_BYTE        (sp[i>>(3-(B))]) /* byte to use */
#     define SP_OFFSET_LSB  ((BIT_MASK &  i) << (B))
#     define SP_OFFSET_MSB  ((BIT_MASK & ~i) << (B))
#     define SP_PIXEL(sex)  ((SP_BYTE >> SP_OFFSET_ ## sex) & PIXEL_MASK)
   {
      unsigned int j;
      unsigned int d;

      /* The data is always in the PNG, big-endian, format: */
      for (j = 8U, d = 0U; count > 0U; --count, i += inc)
      {  /* big-endian */
         j -= 1U<<B;
         d |= SP_PIXEL(MSB) << j;
         if (j == 0U) *dp++ = png_check_byte(png_ptr, d), j = 8U, d = 0U;
      }

      /* The end condition: if j is not 0 the last byte was not
       * written:
       */
      if (j != 0U) *dp = png_check_byte(png_ptr, d);
   }
#     undef PIXEL_MASK
#     undef BIT_MASK
#     undef SP_BYTE
#     undef SP_OFFSET_MSB
#     undef SP_OFFSET_LSB
#     undef SP_PIXEL
}

static void
interlace_row_byte(png_const_structrp png_ptr, png_bytep dp, png_const_bytep sp,
    png_uint_32 x, unsigned int count, unsigned int cbytes)
{
   const unsigned int pass = png_ptr->pass;
   const unsigned int inc = PNG_PASS_COL_OFFSET(pass);

   /* Loop through the input copying each pixel to the correct place
    * in the output.  Note that the loop may be executed 0 times if
    * this is called on a narrow image that does not contain this
    * pass.
    */
   for (sp += PNG_COL_FROM_PASS_COL(x, pass) * cbytes; count > 0;
        --count, sp += inc * cbytes, dp += cbytes)
      memcpy(dp, sp, cbytes);
}
#endif /* WRITE_INTERLACING */

#ifdef PNG_WRITE_TRANSFORMS_SUPPORTED
static void
write_row_core(png_structrp png_ptr, png_const_bytep row,
    unsigned int row_info_flags)
{
#  ifdef PNG_WRITE_TRANSFORMS_SUPPORTED
      if (png_ptr->transform_list != NULL)
         write_row_buffered(png_ptr, row, row_info_flags,
             copy_row, png_ptr->row_input_pixel_depth);

      else
#  endif /* WRITE_TRANSFORMS */

   /* If control reaches this point the intermediate buffer is not required and
    * the input data can be used unmodified.
    */
   png_write_png_rows(png_ptr, &row, 1U);
   PNG_UNUSED(row_info_flags)
}

/* Write a single non-interlaced row. */
static void
write_row_non_interlaced(png_structrp png_ptr, png_const_bytep row)
{
   const png_uint_32 row_number = png_ptr->row_number+1U;
   /* There is only one pass, so this is the last pass: */
   const unsigned int row_info_flags =
      (row_number == 1U ? png_pass_first_row : 0) |
      (row_number >= png_ptr->height ? png_pass_last_row : 0) |
      png_pass_last;

   debug(png_ptr->interlaced == PNG_INTERLACE_NONE);

   write_row_core(png_ptr, row, row_info_flags);
}

/* Write a single interlaced row. */
static void
write_row_interlaced(png_structrp png_ptr, png_const_bytep row)
{
   const png_uint_32 row_number = png_ptr->row_number+1U;
   const png_uint_32 height = png_ptr->height;
   const unsigned int pass = png_ptr->pass;
   const unsigned int row_info_flags =
      (row_number == 1U ? png_pass_first_row : 0) |
      (row_number == PNG_PASS_ROWS(height, pass) ? png_pass_last_row : 0) |
      (pass == PNG_LAST_PASS(png_ptr->width, height) ? png_pass_last : 0);

#  ifdef PNG_WRITE_INTERLACING_SUPPORTED
      /* Check that libpng is not doing the interlace: */
      debug(png_ptr->interlaced != PNG_INTERLACE_NONE &&
            !png_ptr->do_interlace);
#  endif /* WRITE_INTERLACING */

   write_row_core(png_ptr, row, row_info_flags);
}
#endif /* WRITE_TRANSFORMS */

#ifdef PNG_WRITE_INTERLACING_SUPPORTED
/* Interlace a row then write it out. */
static void
interlace_row(png_structrp png_ptr, png_const_bytep row)
{
   /* The row may not exist in the image (for this pass). */
   const png_uint_32 row_number = png_ptr->row_number; /* in image */
   const unsigned int pass = png_ptr->pass;

   if (png_ptr->width > PNG_PASS_START_COL(pass) &&
       PNG_ROW_IN_INTERLACE_PASS(row_number, pass))
   {
      const unsigned int row_info_flags =
         (row_number == PNG_PASS_START_ROW(pass) ? png_pass_first_row : 0) |
         (PNG_LAST_PASS_ROW(row_number, pass, png_ptr->height) ?
            png_pass_last_row : 0) |
         (pass == PNG_LAST_PASS(png_ptr->width, png_ptr->height) ?
            png_pass_last : 0);

      if (pass < 6)
      {
         /* Libpng is doing the interlacing and pixels need to be selected
          * from the input row for this pass.
          */
         /* row interlacing uses either the log bit depth for low bit
          * depth input or the byte count for 8bpp or bigger pixels.
          */
         const unsigned int input_depth = png_ptr->row_input_pixel_depth;
         unsigned int B = 0; /* log2(input_depth) */

         switch (input_depth)
         {
            case 4U: /* B will be 2 */
               ++B;
               /*FALL THROUGH*/
            case 2U: /* B will be 1 */
               ++B;
               /*FALL THROUGH*/
            case 1U: /* B will be 0 */
               write_row_buffered(png_ptr, row, row_info_flags,
                   interlace_row_lbd, B);
               break;

            default: /* Parameter is the pixel size in bytes */
               write_row_buffered(png_ptr, row, row_info_flags,
                   interlace_row_byte, input_depth >> 3);
               break;
         }
      } /* pass < 6 */

      else /* pass 6: no interlacing required */
         write_row_core(png_ptr, row, row_info_flags);
   }

   else
   {
      /* This code must advance row_number/pass itself; the row has been
       * skipped.
       */
      if (row_number+1U < png_ptr->height)
         png_ptr->row_number = row_number+1U;

      else
      {
         png_ptr->row_number = 0U;
         png_ptr->pass = 0x7U & (pass+1U);
      }
   }
}
#endif /* WRITE_INTERLACING */

/* Bottleneck API to actually write a number of rows, only exists because the
 * rows parameter to png_write_rows is wrong.
 */
static void
png_write_rows_internal(png_structrp png_ptr, png_const_bytep *rows,
    png_uint_32 num_rows)
{
   if (png_ptr != NULL && num_rows > 0U && rows != NULL)
   {
      /* Unlike the read code initialization happens automatically: */
      if (png_ptr->row_number == 0U && png_ptr->pass == 0U)
      {
         png_init_row_info(png_ptr);

#        ifdef PNG_WRITE_TRANSFORMS_SUPPORTED
         /* If the app takes a png_info from a read operation and if the app has
          * performed transforms on the data the png_info can contain IHDR
          * information that cannot be represented in PNG.  The code that writes
          * the IHDR takes the color type from the png_info::format.  The app
          * adds transforms, before or after writing the IHDR, then the IHDR
          * color_type stored in png_struct::color_type is used in
          * png_init_row_info above to work out the actual row format.
          *
          * Prior to 1.7.0 this was not verified (there was no easy way to do
          * so).  Now we can check it here, however this is an:
          *
          * API CHANGE: in 1.7.0 an error may be flagged against bogus
          * info_struct formats even though the app had removed them itself.
          * It's just a warning at present.
          *
          * The test is that either the row_format produced by the write
          * transforms exactly matches that in the original info_struct::format
          * or that the info_struct::format was a simple mapping of the
          * color_type that ended up in the IHDR:
          */
         if (png_ptr->row_format != png_ptr->info_format &&
             PNG_FORMAT_FROM_COLOR_TYPE(png_ptr->color_type) !=
               png_ptr->info_format)
            png_app_warning(png_ptr, "info_struct format does not match IHDR");
#        endif /* WRITE_TRANSFORMS */

         /* Perform initialization required before IDATs are written. */
         png_write_start_IDAT(png_ptr);
      }

      else if (png_ptr->pass >= 7U) /* too many calls; write already ended */
      {
         debug(png_ptr->row_number == 0U);
         png_app_error(png_ptr, "Too many calls to png_write_row");
         return;
      }

      /* The remainder of these tests detect internal errors in libpng */
      else if (png_ptr->interlaced == PNG_INTERLACE_NONE)
         affirm(png_ptr->row_number < png_ptr->height && png_ptr->pass == 0U);

#     ifdef PNG_WRITE_INTERLACING_SUPPORTED
         else if (png_ptr->do_interlace)
            affirm(png_ptr->row_number < png_ptr->height);
#     endif /* WRITE_INTERLACING */

      else /* app does interlace */
         affirm(
            PNG_PASS_IN_IMAGE(png_ptr->width, png_ptr->height, png_ptr->pass) &&
            png_ptr->row_number < PNG_PASS_ROWS(png_ptr->height, png_ptr->pass)
         );

      /* First handle rows that require buffering because of the need to
       * interlace them or the need to perform write transforms.
       */
#     ifdef PNG_WRITE_INTERLACING_SUPPORTED
         /* libpng is doing the interlacing, but this only makes a difference to
          * the first six passes (numbered, in libpng, 0..5); the seventh pass
          * (numbered 6 by libpng) consists of complete image rows.
          */
         if (png_ptr->do_interlace) while (num_rows > 0U && png_ptr->pass < 6)
            interlace_row(png_ptr, *rows++), --num_rows;
#     endif /* WRITE_INTERLACING */

#     ifdef PNG_WRITE_TRANSFORMS_SUPPORTED
         /* Transforms required however the row interlacing has already been
          * handled and we have a complete (PNG) row.
          */
         if (png_ptr->transform_list != NULL)
         {
            if (png_ptr->interlaced == PNG_INTERLACE_NONE)
               while (num_rows > 0U)
                  write_row_non_interlaced(png_ptr, *rows++), --num_rows;

#           ifdef PNG_WRITE_INTERLACING_SUPPORTED
               else if (png_ptr->do_interlace)
                  while (num_rows > 0U)
                     interlace_row(png_ptr, *rows++), --num_rows;
#           endif /* WRITE_INTERLACING */

            else /* app does the interlacing */
               while (num_rows > 0U)
                  write_row_interlaced(png_ptr, *rows++), --num_rows;
         }
#     endif /* WRITE_TRANSFORMS */

      /* Finally handle any remaining rows that require no (libpng) interlace
       * and no transforms.
       */
      if (num_rows > 0U)
         png_write_png_rows(png_ptr, rows, num_rows);

      /* Repeat the checks above, but allow for end-of-image. */
      if (png_ptr->pass < 7U)
      {
         if (png_ptr->interlaced == PNG_INTERLACE_NONE)
            affirm(png_ptr->row_number < png_ptr->height &&
                   png_ptr->pass == 0U);

#        ifdef PNG_WRITE_INTERLACING_SUPPORTED
            else if (png_ptr->do_interlace)
               affirm(png_ptr->row_number < png_ptr->height);
#        endif /* WRITE_INTERLACING */

         else /* app does interlace */
            affirm(PNG_PASS_IN_IMAGE(png_ptr->width, png_ptr->height,
                png_ptr->pass) &&
                png_ptr->row_number <
                PNG_PASS_ROWS(png_ptr->height, png_ptr->pass));
      }
   } /* png_ptr, rows, num_rows all valid */

   else if (png_ptr != NULL)
      png_app_warning(png_ptr, "Missing rows to row write API");
}

/* ROW WRITE APIs */
/* Called by user to write a single row of image data */
void PNGAPI
png_write_row(png_structrp png_ptr, png_const_bytep row)
{
   png_debug(1, "in png_write_row");
   png_write_rows_internal(png_ptr, &row, 1U);
}

/* Write a few rows of image data.  If the image is interlaced,
 * either you will have to write the 7 sub images, or, if you
 * have called png_set_interlace_handling(), you will have to
 * "write" the image seven times.
 */
void PNGAPI
png_write_rows(png_structrp png_ptr, png_bytepp rows, png_uint_32 num_rows)
{
   png_debug(1, "in png_write_rows");

   if (png_ptr != NULL)
      png_write_rows_internal(png_ptr, png_constcast(png_const_bytep*,rows),
          num_rows);
}

/* Write the image.  You only need to call this function once, even
 * if you are writing an interlaced image.
 */
void PNGAPI
png_write_image(png_structrp png_ptr, png_bytepp image)
{
   png_debug(1, "in png_write_image");

   if (png_ptr != NULL)
   {
      int num_pass = 1;

      /* The image is always an non-interlaced image.  To write it as interlaced
       * interlace handling must be present:
       */
      if (png_ptr->interlaced)
      {
#        ifdef PNG_WRITE_INTERLACING_SUPPORTED
            num_pass = png_set_interlace_handling(png_ptr);
#        else /* !WRITE_INTERLACING */
            /* There is no recovery because the IHDR has already been written.
             */
            png_error(png_ptr, "No interlace support");
#        endif /* !WRITE_INTERLACING */
      }

      /* And write the whole thing, 7 times if interlacing it: */
      for (; num_pass > 0; --num_pass)
         png_write_rows(png_ptr, image, png_ptr->height);
   }
}

/* Free any memory used in png_ptr struct without freeing the struct itself. */
static void
png_write_destroy(png_structrp png_ptr)
{
   png_debug(1, "in png_write_destroy");

   png_deflate_destroy(png_ptr);

#ifdef PNG_TRANSFORM_MECH_SUPPORTED
   png_transform_free(png_ptr, &png_ptr->transform_list);
#endif

#ifdef PNG_SET_UNKNOWN_CHUNKS_SUPPORTED
   png_free(png_ptr, png_ptr->chunk_list);
   png_ptr->chunk_list = NULL;
#endif

   /* The error handling and memory handling information is left intact at this
    * point: the jmp_buf may still have to be freed.  See png_destroy_png_struct
    * for how this happens.
    */
}

/* Free all memory used by the write.
 * In libpng 1.6.0 this API changed quietly to no longer accept a NULL value for
 * *png_ptr_ptr.  Prior to 1.6.0 it would accept such a value and it would free
 * the passed in info_structs but it would quietly fail to free any of the data
 * inside them.  In 1.6.0 it quietly does nothing (it has to be quiet because it
 * has no png_ptr.)
 */
void PNGAPI
png_destroy_write_struct(png_structpp png_ptr_ptr, png_infopp info_ptr_ptr)
{
   png_debug(1, "in png_destroy_write_struct");

   if (png_ptr_ptr != NULL)
   {
      png_structrp png_ptr = *png_ptr_ptr;

      if (png_ptr != NULL) /* added in libpng 1.6.0 */
      {
         png_destroy_info_struct(png_ptr, info_ptr_ptr);

         *png_ptr_ptr = NULL;
         png_write_destroy(png_ptr);
         png_destroy_png_struct(png_ptr);
      }
   }
}

void PNGAPI
png_set_write_status_fn(png_structrp png_ptr, png_write_status_ptr write_row_fn)
{
   if (png_ptr == NULL)
      return;

   png_ptr->write_row_fn = write_row_fn;
}

#ifdef PNG_WRITE_PNG_SUPPORTED
void PNGAPI
png_write_png(png_structrp png_ptr, png_inforp info_ptr,
    int transforms, voidp params)
{
   if (png_ptr == NULL || info_ptr == NULL)
      return;

   if ((info_ptr->valid & PNG_INFO_IDAT) == 0)
   {
      png_app_error(png_ptr, "no rows for png_write_image to write");
      return;
   }

   /* Write the file header information. */
   png_write_info(png_ptr, info_ptr);

   /* ------ these transformations don't touch the info structure ------- */

   /* Invert monochrome pixels */
   if ((transforms & PNG_TRANSFORM_INVERT_MONO) != 0)
#ifdef PNG_WRITE_INVERT_SUPPORTED
      png_set_invert_mono(png_ptr);
#else
      png_app_error(png_ptr, "PNG_TRANSFORM_INVERT_MONO not supported");
#endif

   /* Shift the pixels up to a legal bit depth and fill in
    * as appropriate to correctly scale the image.
    */
   if ((transforms & PNG_TRANSFORM_SHIFT) != 0)
#ifdef PNG_WRITE_SHIFT_SUPPORTED
      if ((info_ptr->valid & PNG_INFO_sBIT) != 0)
         png_set_shift(png_ptr, &info_ptr->sig_bit);
#else
      png_app_error(png_ptr, "PNG_TRANSFORM_SHIFT not supported");
#endif

   /* Pack pixels into bytes */
   if ((transforms & PNG_TRANSFORM_PACKING) != 0)
#ifdef PNG_WRITE_PACK_SUPPORTED
      png_set_packing(png_ptr);
#else
      png_app_error(png_ptr, "PNG_TRANSFORM_PACKING not supported");
#endif

   /* Swap location of alpha bytes from ARGB to RGBA */
   if ((transforms & PNG_TRANSFORM_SWAP_ALPHA) != 0)
#ifdef PNG_WRITE_SWAP_ALPHA_SUPPORTED
      png_set_swap_alpha(png_ptr);
#else
      png_app_error(png_ptr, "PNG_TRANSFORM_SWAP_ALPHA not supported");
#endif

   /* Remove a filler (X) from XRGB/RGBX/AG/GA into to convert it into
    * RGB, note that the code expects the input color type to be G or RGB; no
    * alpha channel.
    */
   if ((transforms & (PNG_TRANSFORM_STRIP_FILLER_AFTER|
      PNG_TRANSFORM_STRIP_FILLER_BEFORE)) != 0)
   {
#ifdef PNG_WRITE_FILLER_SUPPORTED
      if ((transforms & PNG_TRANSFORM_STRIP_FILLER_AFTER) != 0)
      {
         if ((transforms & PNG_TRANSFORM_STRIP_FILLER_BEFORE) != 0)
            png_app_error(png_ptr,
                "PNG_TRANSFORM_STRIP_FILLER: BEFORE+AFTER not supported");

         /* Continue if ignored - this is the pre-1.6.10 behavior */
         png_set_filler(png_ptr, 0, PNG_FILLER_AFTER);
      }

      else if ((transforms & PNG_TRANSFORM_STRIP_FILLER_BEFORE) != 0)
         png_set_filler(png_ptr, 0, PNG_FILLER_BEFORE);
#else
      png_app_error(png_ptr, "PNG_TRANSFORM_STRIP_FILLER not supported");
#endif
   }

   /* Flip BGR pixels to RGB */
   if ((transforms & PNG_TRANSFORM_BGR) != 0)
#ifdef PNG_WRITE_BGR_SUPPORTED
      png_set_bgr(png_ptr);
#else
      png_app_error(png_ptr, "PNG_TRANSFORM_BGR not supported");
#endif

   /* Swap bytes of 16-bit files to most significant byte first */
   if ((transforms & PNG_TRANSFORM_SWAP_ENDIAN) != 0)
#ifdef PNG_WRITE_SWAP_SUPPORTED
      png_set_swap(png_ptr);
#else
      png_app_error(png_ptr, "PNG_TRANSFORM_SWAP_ENDIAN not supported");
#endif

   /* Swap bits of 1, 2, 4 bit packed pixel formats */
   if ((transforms & PNG_TRANSFORM_PACKSWAP) != 0)
#ifdef PNG_WRITE_PACKSWAP_SUPPORTED
      png_set_packswap(png_ptr);
#else
      png_app_error(png_ptr, "PNG_TRANSFORM_PACKSWAP not supported");
#endif

   /* Invert the alpha channel from opacity to transparency */
   if ((transforms & PNG_TRANSFORM_INVERT_ALPHA) != 0)
#ifdef PNG_WRITE_INVERT_ALPHA_SUPPORTED
      png_set_invert_alpha(png_ptr);
#else
      png_app_error(png_ptr, "PNG_TRANSFORM_INVERT_ALPHA not supported");
#endif

   /* ----------------------- end of transformations ------------------- */

   /* Write the bits */
   png_write_image(png_ptr, info_ptr->row_pointers);

   /* It is REQUIRED to call this to finish writing the rest of the file */
   png_write_end(png_ptr, info_ptr);

   PNG_UNUSED(params)
}
#endif /* WRITE_PNG */


#ifdef PNG_SIMPLIFIED_WRITE_SUPPORTED
/* Initialize the write structure - general purpose utility. */
static int
png_image_write_init(png_imagep image)
{
   png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, image,
       png_safe_error, png_safe_warning);

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
            control->for_write = 1;

            image->opaque = control;
            return 1;
         }

         /* Error clean up */
         png_destroy_info_struct(png_ptr, &info_ptr);
      }

      png_destroy_write_struct(&png_ptr, NULL);
   }

   return png_image_error(image, "png_image_write_: out of memory");
}

/* Arguments to png_image_write_main: */
typedef struct
{
   /* Arguments: */
   png_imagep      image;
   png_const_voidp buffer;
   ptrdiff_t       row_stride;
   png_const_voidp colormap;
   int             convert_to_8bit;
   /* Local variables: */
   png_const_voidp first_row;
   ptrdiff_t       row_bytes;
   png_voidp       local_row;
   /* Byte count for memory writing */
   png_bytep        memory;
   png_alloc_size_t memory_bytes; /* not used for STDIO */
   png_alloc_size_t output_bytes; /* running total */
} png_image_write_control;

/* Write png_uint_16 input to a 16-bit PNG; the png_ptr has already been set to
 * do any necessary byte swapping.  The component order is defined by the
 * png_image format value.
 */
static int
png_write_image_16bit(png_voidp argument)
{
   png_image_write_control *display = png_voidcast(png_image_write_control*,
       argument);
   png_imagep image = display->image;
   png_structrp png_ptr = image->opaque->png_ptr;

   png_const_uint_16p input_row = png_voidcast(png_const_uint_16p,
       display->first_row);
   png_uint_16p output_row = png_voidcast(png_uint_16p, display->local_row);
   png_uint_16p row_end;
   const int channels = (image->format & PNG_FORMAT_FLAG_COLOR) != 0 ? 3 : 1;
   int aindex = 0;
   png_uint_32 y = image->height;

   if ((image->format & PNG_FORMAT_FLAG_ALPHA) != 0)
   {
#     ifdef PNG_SIMPLIFIED_WRITE_AFIRST_SUPPORTED
         if ((image->format & PNG_FORMAT_FLAG_AFIRST) != 0)
         {
            aindex = -1;
            ++input_row; /* To point to the first component */
            ++output_row;
         }

         else
#     endif
         aindex = channels;
   }

   else
      png_error(png_ptr, "png_write_image: internal call error");

   /* Work out the output row end and count over this, note that the increment
    * above to 'row' means that row_end can actually be beyond the end of the
    * row; this is correct.
    */
   row_end = output_row + image->width * (channels+1);

   while (y-- > 0)
   {
      png_const_uint_16p in_ptr = input_row;
      png_uint_16p out_ptr = output_row;

      while (out_ptr < row_end)
      {
         const png_uint_16 alpha = in_ptr[aindex];
         png_uint_32 reciprocal = 0;
         int c;

         out_ptr[aindex] = alpha;

         /* Calculate a reciprocal.  The correct calculation is simply
          * component/alpha*65535 << 15. (I.e. 15 bits of precision); this
          * allows correct rounding by adding .5 before the shift.  'reciprocal'
          * is only initialized when required.
          */
         if (alpha > 0 && alpha < 65535)
            reciprocal = ((0xffff<<15)+(alpha>>1))/alpha;

         c = channels;
         do /* always at least one channel */
         {
            png_uint_16 component = *in_ptr++;

            /* The following gives 65535 for an alpha of 0, which is fine,
             * otherwise if 0/0 is represented as some other value there is more
             * likely to be a discontinuity which will probably damage
             * compression when moving from a fully transparent area to a
             * nearly transparent one.  (The assumption here is that opaque
             * areas tend not to be 0 intensity.)
             */
            if (component >= alpha)
               component = 65535;

            /* component<alpha, so component/alpha is less than one and
             * component*reciprocal is less than 2^31.
             */
            else if (component > 0 && alpha < 65535)
            {
               png_uint_32 calc = component * reciprocal;
               calc += 16384; /* round to nearest */
               component = png_check_u16(png_ptr, calc >> 15);
            }

            *out_ptr++ = component;
         }
         while (--c > 0);

         /* Skip to next component (skip the intervening alpha channel) */
         ++in_ptr;
         ++out_ptr;
      }

      png_write_row(png_ptr, png_voidcast(png_const_bytep, display->local_row));
      input_row += display->row_bytes/(sizeof (png_uint_16));
   }

   return 1;
}

/* Given 16-bit input (1 to 4 channels) write 8-bit output.  If an alpha channel
 * is present it must be removed from the components, the components are then
 * written in sRGB encoding.  No components are added or removed.
 *
 * Calculate an alpha reciprocal to reverse pre-multiplication.  As above the
 * calculation can be done to 15 bits of accuracy; however, the output needs to
 * be scaled in the range 0..255*65535, so include that scaling here.
 */
#define UNP_RECIPROCAL(alpha) ((((0xffff*0xff)<<7)+(alpha>>1))/alpha)

static png_byte
png_unpremultiply(png_const_structrp png_ptr, png_uint_32 component,
    png_uint_32 alpha, png_uint_32 reciprocal/*from the above macro*/)
{
   /* The following gives 1.0 for an alpha of 0, which is fine, otherwise if 0/0
    * is represented as some other value there is more likely to be a
    * discontinuity which will probably damage compression when moving from a
    * fully transparent area to a nearly transparent one.  (The assumption here
    * is that opaque areas tend not to be 0 intensity.)
    *
    * There is a rounding problem here; if alpha is less than 128 it will end up
    * as 0 when scaled to 8 bits.  To avoid introducing spurious colors into the
    * output change for this too.
    */
   if (component >= alpha || alpha < 128)
      return 255;

   /* component<alpha, so component/alpha is less than one and
    * component*reciprocal is less than 2^31.
    */
   else if (component > 0)
   {
      /* The test is that alpha/257 (rounded) is less than 255, the first value
       * that becomes 255 is 65407.
       * NOTE: this must agree with the PNG_DIV257 macro (which must, therefore,
       * be exact!)  [Could also test reciprocal != 0]
       */
      if (alpha < 65407)
      {
         component *= reciprocal;
         component += 64; /* round to nearest */
         component >>= 7;
      }

      else
         component *= 255;

      /* Convert the component to sRGB. */
      return PNG_sRGB_FROM_LINEAR(png_ptr, component);
   }

   else
      return 0;

   PNG_UNUSEDRC(png_ptr)
}

static int
png_write_image_8bit(png_voidp argument)
{
   png_image_write_control *display = png_voidcast(png_image_write_control*,
       argument);
   png_imagep image = display->image;
   png_structrp png_ptr = image->opaque->png_ptr;

   png_const_uint_16p input_row = png_voidcast(png_const_uint_16p,
       display->first_row);
   png_bytep output_row = png_voidcast(png_bytep, display->local_row);
   png_uint_32 y = image->height;
   const int channels = (image->format & PNG_FORMAT_FLAG_COLOR) != 0 ? 3 : 1;

   if ((image->format & PNG_FORMAT_FLAG_ALPHA) != 0)
   {
      png_bytep row_end;
      int aindex;

#     ifdef PNG_SIMPLIFIED_WRITE_AFIRST_SUPPORTED
         if ((image->format & PNG_FORMAT_FLAG_AFIRST) != 0)
         {
            aindex = -1;
            ++input_row; /* To point to the first component */
            ++output_row;
         }

         else
#     endif
         aindex = channels;

      /* Use row_end in place of a loop counter: */
      row_end = output_row + image->width * (channels+1);

      while (y-- > 0)
      {
         png_const_uint_16p in_ptr = input_row;
         png_bytep out_ptr = output_row;

         while (out_ptr < row_end)
         {
            png_uint_16 alpha = in_ptr[aindex];
            png_byte alphabyte = png_check_byte(png_ptr, PNG_DIV257(alpha));
            png_uint_32 reciprocal = 0;
            int c;

            /* Scale and write the alpha channel. */
            out_ptr[aindex] = alphabyte;

            if (alphabyte > 0 && alphabyte < 255)
               reciprocal = UNP_RECIPROCAL(alpha);

            c = channels;
            do /* always at least one channel */
               *out_ptr++ = png_unpremultiply(png_ptr, *in_ptr++, alpha,
                   reciprocal);
            while (--c > 0);

            /* Skip to next component (skip the intervening alpha channel) */
            ++in_ptr;
            ++out_ptr;
         } /* while out_ptr < row_end */

         png_write_row(png_ptr, png_voidcast(png_const_bytep,
             display->local_row));
         input_row += display->row_bytes/(sizeof (png_uint_16));
      } /* while y */
   }

   else
   {
      /* No alpha channel, so the row_end really is the end of the row and it
       * is sufficient to loop over the components one by one.
       */
      png_bytep row_end = output_row + image->width * channels;

      while (y-- > 0)
      {
         png_const_uint_16p in_ptr = input_row;
         png_bytep out_ptr = output_row;

         while (out_ptr < row_end)
         {
            png_uint_32 component = *in_ptr++;

            component *= 255;
            *out_ptr++ = PNG_sRGB_FROM_LINEAR(png_ptr, component);
         }

         png_write_row(png_ptr, output_row);
         input_row += display->row_bytes/(sizeof (png_uint_16));
      }
   }

   return 1;
}

static void
png_image_set_PLTE(png_image_write_control *display)
{
   const png_imagep image = display->image;
   const void *cmap = display->colormap;
   const int entries = image->colormap_entries > 256 ? 256 :
      (int)image->colormap_entries;

   /* NOTE: the caller must check for cmap != NULL and entries != 0 */
   const png_uint_32 format = image->format;
   const int channels = PNG_IMAGE_SAMPLE_CHANNELS(format);

#  if defined(PNG_FORMAT_BGR_SUPPORTED) &&\
      defined(PNG_SIMPLIFIED_WRITE_AFIRST_SUPPORTED)
      const int afirst = (format & PNG_FORMAT_FLAG_AFIRST) != 0 &&
         (format & PNG_FORMAT_FLAG_ALPHA) != 0;
#  else
#     define afirst 0
#  endif

#  ifdef PNG_FORMAT_BGR_SUPPORTED
      const int bgr = (format & PNG_FORMAT_FLAG_BGR) != 0 ? 2 : 0;
#  else
#     define bgr 0
#  endif

   int i, num_trans;
   png_color palette[256];
   png_byte tRNS[256];

   memset(tRNS, 255, (sizeof tRNS));
   memset(palette, 0, (sizeof palette));

   for (i=num_trans=0; i<entries; ++i)
   {
      /* This gets automatically converted to sRGB with reversal of the
       * pre-multiplication if the color-map has an alpha channel.
       */
      if ((format & PNG_FORMAT_FLAG_LINEAR) != 0)
      {
         png_const_uint_16p entry = png_voidcast(png_const_uint_16p, cmap);

         entry += i * channels;

         if ((channels & 1) != 0) /* no alpha */
         {
            if (channels >= 3) /* RGB */
            {
               palette[i].blue = PNG_sRGB_FROM_LINEAR(
                  display->image->opaque->png_ptr, 255 * entry[(2 ^ bgr)]);
               palette[i].green = PNG_sRGB_FROM_LINEAR(
                  display->image->opaque->png_ptr, 255 * entry[1]);
               palette[i].red = PNG_sRGB_FROM_LINEAR(
                  display->image->opaque->png_ptr, 255 * entry[bgr]);
            }

            else /* Gray */
               palette[i].blue = palette[i].red = palette[i].green =
                  PNG_sRGB_FROM_LINEAR(display->image->opaque->png_ptr,
                      255 * *entry);
         }

         else /* alpha */
         {
            png_uint_16 alpha = entry[afirst ? 0 : channels-1];
            png_byte alphabyte = png_check_byte(
               display->image->opaque->png_ptr, PNG_DIV257(alpha));
            png_uint_32 reciprocal = 0;

            /* Calculate a reciprocal, as in the png_write_image_8bit code above
             * this is designed to produce a value scaled to 255*65535 when
             * divided by 128 (i.e. asr 7).
             */
            if (alphabyte > 0 && alphabyte < 255)
               reciprocal = (((0xffff*0xff)<<7)+(alpha>>1))/alpha;

            tRNS[i] = alphabyte;
            if (alphabyte < 255)
               num_trans = i+1;

            if (channels >= 3) /* RGB */
            {
               palette[i].blue = png_unpremultiply(
                   display->image->opaque->png_ptr, entry[afirst + (2 ^ bgr)],
                   alpha, reciprocal);
               palette[i].green = png_unpremultiply(
                   display->image->opaque->png_ptr, entry[afirst + 1], alpha,
                   reciprocal);
               palette[i].red = png_unpremultiply(
                   display->image->opaque->png_ptr, entry[afirst + bgr], alpha,
                   reciprocal);
            }

            else /* gray */
               palette[i].blue = palette[i].red = palette[i].green =
                   png_unpremultiply(display->image->opaque->png_ptr,
                       entry[afirst], alpha, reciprocal);
         }
      }

      else /* Color-map has sRGB values */
      {
         png_const_bytep entry = png_voidcast(png_const_bytep, cmap);

         entry += i * channels;

         switch (channels)
         {
            case 4:
               tRNS[i] = entry[afirst ? 0 : 3];
               if (tRNS[i] < 255)
                  num_trans = i+1;
               /* FALL THROUGH */
            case 3:
               palette[i].blue = entry[afirst + (2 ^ bgr)];
               palette[i].green = entry[afirst + 1];
               palette[i].red = entry[afirst + bgr];
               break;

            case 2:
               tRNS[i] = entry[1 ^ afirst];
               if (tRNS[i] < 255)
                  num_trans = i+1;
               /* FALL THROUGH */
            case 1:
               palette[i].blue = palette[i].red = palette[i].green =
                  entry[afirst];
               break;

            default:
               break;
         }
      }
   }

#  ifdef afirst
#     undef afirst
#  endif
#  ifdef bgr
#     undef bgr
#  endif

   png_set_PLTE(image->opaque->png_ptr, image->opaque->info_ptr, palette,
       entries);

   if (num_trans > 0)
      png_set_tRNS(image->opaque->png_ptr, image->opaque->info_ptr, tRNS,
          num_trans, NULL);

   image->colormap_entries = entries;
}

static int
png_image_write_main(png_voidp argument)
{
   png_image_write_control *display = png_voidcast(png_image_write_control*,
       argument);
   png_imagep image = display->image;
   png_structrp png_ptr = image->opaque->png_ptr;
   png_inforp info_ptr = image->opaque->info_ptr;
   png_uint_32 format = image->format;

   /* The following four ints are actually booleans */
   int colormap = (format & PNG_FORMAT_FLAG_COLORMAP);
   int linear = !colormap && (format & PNG_FORMAT_FLAG_LINEAR); /* input */
   int alpha = !colormap && (format & PNG_FORMAT_FLAG_ALPHA);
   int write_16bit = linear && !colormap && (display->convert_to_8bit == 0);

#  ifdef PNG_BENIGN_ERRORS_SUPPORTED
      /* Make sure we error out on any bad situation */
      png_set_benign_errors(png_ptr, 0/*error*/);
#  endif

   /* Default the 'row_stride' parameter if required, also check the row stride
    * and total image size to ensure that they are within the system limits.
    */
   {
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

         if (display->row_stride == 0)
            display->row_stride = (ptrdiff_t)png_row_stride;

         if (display->row_stride < 0)
            check = -display->row_stride;

         else
            check = display->row_stride;

         if (check >= png_row_stride)
         {
            /* Now check for overflow of the image buffer calculation; check for
             * (size_t) overflow here.  This detects issues with the
             * PNG_IMAGE_BUFFER_SIZE macro.
             */
            if (image->height > PNG_SIZE_MAX/png_row_stride)
               png_error(image->opaque->png_ptr, "memory image too large");
         }

         else
            png_error(image->opaque->png_ptr, "supplied row stride too small");
      }

      else
         png_error(image->opaque->png_ptr, "image row stride too large");
   }

   /* Set the required transforms then write the rows in the correct order. */
   if ((format & PNG_FORMAT_FLAG_COLORMAP) != 0)
   {
      if (display->colormap != NULL && image->colormap_entries > 0)
      {
         png_uint_32 entries = image->colormap_entries;

         png_set_IHDR(png_ptr, info_ptr, image->width, image->height,
             entries > 16 ? 8 : (entries > 4 ? 4 : (entries > 2 ? 2 : 1)),
             PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE,
             PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

         png_image_set_PLTE(display);
      }

      else
         png_error(image->opaque->png_ptr,
             "no color-map for color-mapped image");
   }

   else
      png_set_IHDR(png_ptr, info_ptr, image->width, image->height,
          write_16bit ? 16 : 8,
          ((format & PNG_FORMAT_FLAG_COLOR) ? PNG_COLOR_MASK_COLOR : 0) +
          ((format & PNG_FORMAT_FLAG_ALPHA) ? PNG_COLOR_MASK_ALPHA : 0),
          PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

   /* Counter-intuitively the data transformations must be called *after*
    * png_write_info, not before as in the read code, but the 'set' functions
    * must still be called before.  Just set the color space information, never
    * write an interlaced image.
    */

   if (write_16bit != 0)
   {
      /* The gamma here is 1.0 (linear) and the cHRM chunk matches sRGB. */
      png_set_gAMA_fixed(png_ptr, info_ptr, PNG_GAMMA_LINEAR);

      if ((image->flags & PNG_IMAGE_FLAG_COLORSPACE_NOT_sRGB) == 0)
         png_set_cHRM_fixed(png_ptr, info_ptr,
             /* color      x       y */
             /* white */ 31270, 32900,
             /* red   */ 64000, 33000,
             /* green */ 30000, 60000,
             /* blue  */ 15000,  6000
         );
   }

   else if ((image->flags & PNG_IMAGE_FLAG_COLORSPACE_NOT_sRGB) == 0)
      png_set_sRGB(png_ptr, info_ptr, PNG_sRGB_INTENT_PERCEPTUAL);

   /* Else writing an 8-bit file and the *colors* aren't sRGB, but the 8-bit
    * space must still be gamma encoded.
    */
   else
      png_set_gAMA_fixed(png_ptr, info_ptr, PNG_GAMMA_sRGB_INVERSE);

   /* Write the file header. */
   png_write_info(png_ptr, info_ptr);

   /* Now set up the data transformations (*after* the header is written),
    * remove the handled transformations from the 'format' flags for checking.
    *
    * First check for a little endian system if writing 16 bit files.
    */
   if (write_16bit != 0)
   {
      PNG_CONST png_uint_16 le = 0x0001;

      if ((*(png_const_bytep) & le) != 0)
         png_set_swap(png_ptr);
   }

#  ifdef PNG_SIMPLIFIED_WRITE_BGR_SUPPORTED
      if ((format & PNG_FORMAT_FLAG_BGR) != 0)
      {
         if (colormap == 0 && (format & PNG_FORMAT_FLAG_COLOR) != 0)
            png_set_bgr(png_ptr);
         format &= PNG_BIC_MASK(PNG_FORMAT_FLAG_BGR);
      }
#  endif

#  ifdef PNG_SIMPLIFIED_WRITE_AFIRST_SUPPORTED
      if ((format & PNG_FORMAT_FLAG_AFIRST) != 0)
      {
         if (colormap == 0 && (format & PNG_FORMAT_FLAG_ALPHA) != 0)
            png_set_swap_alpha(png_ptr);
         format &= PNG_BIC_MASK(PNG_FORMAT_FLAG_AFIRST);
      }
#  endif

   /* If there are 16 or fewer color-map entries we wrote a lower bit depth
    * above, but the application data is still byte packed.
    */
   if (colormap != 0 && image->colormap_entries <= 16)
      png_set_packing(png_ptr);

   /* That should have handled all (both) the transforms. */
   if ((format & PNG_BIC_MASK(PNG_FORMAT_FLAG_COLOR | PNG_FORMAT_FLAG_LINEAR |
         PNG_FORMAT_FLAG_ALPHA | PNG_FORMAT_FLAG_COLORMAP)) != 0)
      png_error(png_ptr, "png_write_image: unsupported transformation");

   {
      png_const_bytep row = png_voidcast(png_const_bytep, display->buffer);
      ptrdiff_t row_bytes = display->row_stride;

      if (linear != 0)
         row_bytes *= (sizeof (png_uint_16));

      if (row_bytes < 0)
         row += (image->height-1) * (-row_bytes);

      display->first_row = row;
      display->row_bytes = row_bytes;
   }

   /* Select the right compression mode based on the presence or absence of the
    * 'fast' flag. This will use whatever options are available in the libpng
    * build.  It is always supported.
    */
   png_set_compression(png_ptr, (image->flags & PNG_IMAGE_FLAG_FAST) != 0 ?
         PNG_COMPRESSION_HIGH_SPEED : PNG_COMPRESSION_HIGH);

   /* Check for the cases that currently require a pre-transform on the row
    * before it is written.  This only applies when the input is 16-bit and
    * either there is an alpha channel or it is converted to 8-bit.
    */
   if ((linear != 0 && alpha != 0 ) ||
       (colormap == 0 && display->convert_to_8bit != 0))
   {
      png_bytep row = png_voidcast(png_bytep, png_malloc(png_ptr,
          png_get_rowbytes(png_ptr, info_ptr)));
      int result;

      display->local_row = row;
      if (write_16bit != 0)
         result = png_safe_execute(image, png_write_image_16bit, display);
      else
         result = png_safe_execute(image, png_write_image_8bit, display);
      display->local_row = NULL;

      png_free(png_ptr, row);

      /* Skip the 'write_end' on error: */
      if (result == 0)
         return 0;
   }

   /* Otherwise this is the case where the input is in a format currently
    * supported by the rest of the libpng write code; call it directly.
    */
   else
   {
      png_const_bytep row = png_voidcast(png_const_bytep, display->first_row);
      ptrdiff_t row_bytes = display->row_bytes;
      png_uint_32 y = image->height;

      while (y-- > 0)
      {
         png_write_row(png_ptr, row);
         row += row_bytes;
      }
   }

   png_write_end(png_ptr, info_ptr);
   return 1;
}


static void (PNGCBAPI
image_memory_write)(png_structp png_ptr, png_bytep/*const*/ data,
    png_size_t size)
{
   png_image_write_control *display = png_voidcast(png_image_write_control*,
       png_ptr->io_ptr/*backdoor: png_get_io_ptr(png_ptr)*/);
   const png_alloc_size_t ob = display->output_bytes;

   /* Check for overflow; this should never happen: */
   if (size <= ((png_alloc_size_t)-1) - ob)
   {
      /* I don't think libpng ever does this, but just in case: */
      if (size > 0)
      {
         if (display->memory_bytes >= ob+size) /* writing */
            memcpy(display->memory+ob, data, size);

         /* Always update the size: */
         display->output_bytes = ob+size;
      }
   }

   else
      png_error(png_ptr, "png_image_write_to_memory: PNG too big");
}

static void (PNGCBAPI
image_memory_flush)(png_structp png_ptr)
{
   PNG_UNUSED(png_ptr)
}

static int
png_image_write_memory(png_voidp argument)
{
   png_image_write_control *display = png_voidcast(png_image_write_control*,
          argument);

   /* The rest of the memory-specific init and write_main in an error protected
    * environment.  This case needs to use callbacks for the write operations
    * since libpng has no built in support for writing to memory.
    */
   png_set_write_fn(display->image->opaque->png_ptr, display/*io_ptr*/,
       image_memory_write, image_memory_flush);

   return png_image_write_main(display);
}

int PNGAPI
png_image_write_to_memory(png_imagep image, void *memory,
    png_alloc_size_t * PNG_RESTRICT memory_bytes, int convert_to_8bit,
    const void *buffer, ptrdiff_t row_stride, const void *colormap)
{
   /* Write the image to the given buffer, or count the bytes if it is NULL */
   if (image != NULL && image->version == PNG_IMAGE_VERSION)
   {
      if (memory_bytes != NULL && buffer != NULL)
      {
         /* This is to give the caller an easier error detection in the NULL
          * case and guard against uninitialized variable problems:
          */
         if (memory == NULL)
            *memory_bytes = 0;

         if (png_image_write_init(image) != 0)
         {
            png_image_write_control display;
            int result;

            memset(&display, 0, (sizeof display));
            display.image = image;
            display.buffer = buffer;
            display.row_stride = row_stride;
            display.colormap = colormap;
            display.convert_to_8bit = convert_to_8bit;
            display.memory = png_voidcast(png_bytep, memory);
            display.memory_bytes = *memory_bytes;
            display.output_bytes = 0;

            result = png_safe_execute(image, png_image_write_memory, &display);
            png_image_free(image);

            /* write_memory returns true even if we ran out of buffer. */
            if (result)
            {
               /* On out-of-buffer this function returns '0' but still updates
                * memory_bytes:
                */
               if (memory != NULL && display.output_bytes > *memory_bytes)
                  result = 0;

               *memory_bytes = display.output_bytes;
            }

            return result;
         }

         else
            return 0;
      }

      else
         return png_image_error(image,
             "png_image_write_to_memory: invalid argument");
   }

   else if (image != NULL)
      return png_image_error(image,
          "png_image_write_to_memory: incorrect PNG_IMAGE_VERSION");

   else
      return 0;
}

#ifdef PNG_SIMPLIFIED_WRITE_STDIO_SUPPORTED
int PNGAPI
png_image_write_to_stdio(png_imagep image, FILE *file, int convert_to_8bit,
    const void *buffer, ptrdiff_t row_stride, const void *colormap)
{
   /* Write the image to the given (FILE*). */
   if (image != NULL && image->version == PNG_IMAGE_VERSION)
   {
      if (file != NULL && buffer != NULL)
      {
         if (png_image_write_init(image) != 0 &&
             png_image_init_io(image, file) != 0)
         {
            png_image_write_control display;
            int result;

            memset(&display, 0, (sizeof display));
            display.image = image;
            display.buffer = buffer;
            display.row_stride = row_stride;
            display.colormap = colormap;
            display.convert_to_8bit = convert_to_8bit;

            result = png_safe_execute(image, png_image_write_main, &display);
            png_image_free(image);
            return result;
         }

         else
            return 0;
      }

      else
         return png_image_error(image,
             "png_image_write_to_stdio: invalid argument");
   }

   else if (image != NULL)
      return png_image_error(image,
          "png_image_write_to_stdio: incorrect PNG_IMAGE_VERSION");

   else
      return 0;
}

int PNGAPI
png_image_write_to_file(png_imagep image, const char *file_name,
    int convert_to_8bit, const void *buffer, ptrdiff_t row_stride,
    const void *colormap)
{
   /* Write the image to the named file. */
   if (image != NULL && image->version == PNG_IMAGE_VERSION)
   {
      if (file_name != NULL && buffer != NULL)
      {
         FILE *fp = fopen(file_name, "wb");

         if (fp != NULL)
         {
            if (png_image_write_to_stdio(image, fp, convert_to_8bit, buffer,
                row_stride, colormap) != 0)
            {
               int error; /* from fflush/fclose */

               /* Make sure the file is flushed correctly. */
               if (fflush(fp) == 0 && ferror(fp) == 0)
               {
                  if (fclose(fp) == 0)
                     return 1;

                  error = errno; /* from fclose */
               }

               else
               {
                  error = errno; /* from fflush or ferror */
                  (void)fclose(fp);
               }

               (void)remove(file_name);
               /* The image has already been cleaned up; this is just used to
                * set the error (because the original write succeeded).
                */
               return png_image_error(image, strerror(error));
            }

            else
            {
               /* Clean up: just the opened file. */
               (void)fclose(fp);
               (void)remove(file_name);
               return 0;
            }
         }

         else
            return png_image_error(image, strerror(errno));
      }

      else
         return png_image_error(image,
             "png_image_write_to_file: invalid argument");
   }

   else if (image != NULL)
      return png_image_error(image,
          "png_image_write_to_file: incorrect PNG_IMAGE_VERSION");

   else
      return 0;
}
#endif /* SIMPLIFIED_WRITE_STDIO */
#endif /* SIMPLIFIED_WRITE */
#endif /* WRITE */
