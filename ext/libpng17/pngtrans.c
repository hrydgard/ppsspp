
/* pngtrans.c - transforms the data in a row (used by both readers and writers)
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
#define PNG_SRC_FILE PNG_SRC_FILE_pngtrans

#ifdef _XOPEN_SOURCE
#  include <unistd.h>
#endif /* for swab */

/* Memory format enquiries */
#ifdef PNG_GAMMA_SUPPORTED
static png_fixed_point
memory_gamma(png_const_structrp png_ptr)
{
#  ifdef PNG_READ_GAMMA_SUPPORTED
#     ifdef PNG_TRANSFORM_MECH_SUPPORTED
         if (png_ptr->read_struct)
            return png_ptr->row_gamma;
#     endif /* TRANSFORM_MECH */
#  endif /* READ_GAMMA */

   /* Else either no READ_GAMMA support or this is a write struct; in both
    * cases there are no gamma transforms.  In the write case the set of the
    * gamma in the info may not have been copied to the png_struct.
    */
#  if defined(PNG_GAMMA_SUPPORTED) && defined(PNG_READ_SUPPORTED)
      if ((png_ptr->colorspace.flags &
            (PNG_COLORSPACE_INVALID|PNG_COLORSPACE_HAVE_GAMMA)) ==
         PNG_COLORSPACE_HAVE_GAMMA)
         return png_ptr->colorspace.gamma;
#  else /* !(GAMMA && READ) */
      PNG_UNUSED(png_ptr)
#  endif /* !(GAMMA && READ) */

   /* '0' means the value is not know: */
   return 0;
}
#endif /* GAMMA */

unsigned int PNGAPI
png_memory_format(png_structrp png_ptr)
{
   /* The in-memory format as a bitmask of PNG_FORMAT_FLAG_ values.  All the
    * flags listed below are used.  If PNG_FORMAT_FLAG_INVALID is set the
    * following caveats apply to the interpretation of PNG_FORMAT_FLAG_LINEAR:
    *
    *    The gamma may differ from the sRGB (!LINEAR) or 1.0 (LINEAR).  Call
    *    png_memory_gamma to find the correct value.
    *
    *    The channel depth may differ from 8 (!LINEAR) or 16 (LINEAR).  Call
    *    png_memory_channel_depth to find the correct value.
    *
    * It is only valid to call these APIS *after* either png_read_update_info
    * or png_start_read_image on read or after the first row of an image has
    * been written on write.
    */
   if (png_ptr != NULL)
   {
#     ifdef PNG_TRANSFORM_MECH_SUPPORTED
         unsigned int format = png_ptr->row_format;
#     else /* !TRANSFORM_MECH */
         unsigned int format = PNG_FORMAT_FROM_COLOR_TYPE(png_ptr->color_type);
#     endif /* !TRANSFORM_MECH */

      if (png_ptr->read_struct) /* else no way to find the gamma! */
      {
#        ifdef PNG_GAMMA_SUPPORTED
#           ifdef PNG_TRANSFORM_MECH_SUPPORTED
               unsigned int bit_depth = png_ptr->row_bit_depth;
#           else /* !TRANSFORM_MECH */
               unsigned int bit_depth = png_ptr->bit_depth;
#           endif /* !TRANSFORM_MECH */

            /* Now work out whether this is a valid simplified API format. */
            switch (bit_depth)
            {
               case 8U:
                  {
                     png_fixed_point gamma = memory_gamma(png_ptr);

                     if (!PNG_GAMMA_IS_sRGB(gamma))
                        format |= PNG_FORMAT_FLAG_INVALID;
                  }
                  break;

               case 16:
                  if (memory_gamma(png_ptr) == PNG_GAMMA_LINEAR)
                  {
                     static const union
                     {
                        png_uint_16 u16;
                        png_byte    u8[2];
                     } sex = { 1U };

                     format |= PNG_FORMAT_FLAG_LINEAR;

                     /* But the memory layout of the 16-bit quantities must also
                      * match; we need swapped data on LSB platforms.
                      */
                     if (sex.u8[0] == ((format & PNG_FORMAT_FLAG_SWAPPED) != 0))
                        break; /* ok */
                  }

                  /* FALL THROUGH*/
               default: /* bit depth not supported for simplified API */
                  format |= PNG_FORMAT_FLAG_INVALID;
                  break;
            }
#        else /* !GAMMA */
            /* We have no way of knowing if the gamma value matches that
             * expected by the simplified API so mark the format as invalid:
             */
            format |= PNG_FORMAT_FLAG_INVALID;
#        endif
      } /* read_struct */

      return format;
   }

   return 0;
}

unsigned int PNGAPI png_memory_channel_depth(png_structrp png_ptr)
{
   /* The actual depth of each channel in the image. */
   if (png_ptr != NULL)
   {
#     ifdef PNG_TRANSFORM_MECH_SUPPORTED
         return png_ptr->row_bit_depth;
#     else
         return png_ptr->bit_depth;
#     endif
   }

   return 0;
}

#ifdef PNG_GAMMA_SUPPORTED
png_fixed_point PNGAPI
png_memory_gamma(png_structrp png_ptr)
{
   /* The actual gamma of the image data, scaled by 100,000.  This is the
    * encoding gamma, e.g. 1/2.2 for sRGB.  If the gamma is unknown this will
    * return 0.
    *
    * On write this invariably returns 0; libpng does not change the gamma of
    * the data on write.
    *
    * Note that this is not always the exact inverse of the 'screen gamma'
    * passed to png_set_gamma; internal optimizations remove attempts to make
    * small changes to the gamma value.  This function returns the actual
    * output value.
    */
   return (png_ptr != NULL) ? memory_gamma(png_ptr) : 0;
}
#endif /* GAMMA */

/* These are general purpose APIs that deal with the row buffer format in both
 * the read and write case.  The png_struct::row_* members describe the
 * in-memory format of the image data based on the transformations requested by
 * the application.
 */
#ifdef PNG_TRANSFORM_MECH_SUPPORTED
png_voidp /* PRIVATE */
png_transform_cast_check(png_const_structp png_ptr, unsigned int src_line,
   png_transformp tr, size_t size)
{
   /* Given a pointer to a transform, 'tr' validate that the underlying derived
    * class has size 'size' using the tr->size field and return the same
    * pointer.  If there is a size mismatch the function does an affirm using
    * the given line number.
    */
   if (tr->size != size)
      png_affirm(png_ptr, param_deb("transform upcast") src_line);

   return tr;
}

void /* PRIAVE */
png_transform_free(png_const_structrp png_ptr, png_transformp *list)
{
   if (*list != NULL)
   {
      png_transform_free(png_ptr, &(*list)-> next);
      if ((*list)->free != NULL)
         (*list)->free(png_ptr, *list);
      png_free(png_ptr, *list);
      *list = NULL;
   }
}

/* Utility to initialize a png_transform_control for read or write. */
void /* PRIVATE */
png_init_transform_control(png_transform_controlp tc, png_structp png_ptr)
{
   png_byte bd; /* bit depth of the row */
   png_byte cd; /* bit depth of color information */

   memset(tc, 0, sizeof *tc);
   tc->png_ptr = png_ptr; /* ALIAS */
   tc->sp = tc->dp = NULL;
   tc->width = 0;

#  ifdef PNG_READ_GAMMA_SUPPORTED
      /* The file gamma is set by png_set_gamma, as well as being read from the
       * input PNG gAMA chunk, if present, we don't have a png_info so can't get
       * the set_gAMA value but this doesn't matter because on read the gamma
       * value is in png_struct::colorspace and on write it isn't used.
       */
      if ((png_ptr->colorspace.flags &
            (PNG_COLORSPACE_INVALID|PNG_COLORSPACE_HAVE_GAMMA)) ==
         PNG_COLORSPACE_HAVE_GAMMA)
      {
         tc->gamma = png_ptr->colorspace.gamma;
         debug(tc->gamma > 0);
      }

      else
      {
         /* There is no input gamma, so there should be no overall gamma
          * correction going on.  This test works because the various things
          * that set an output gamma also default the input gamma.
          */
         debug(png_ptr->row_gamma == 0);
      }
#  endif

   /* Validate bit depth and color type here */
   cd = bd = png_ptr->bit_depth;

   switch (png_ptr->color_type)
   {
      case PNG_COLOR_TYPE_GRAY:
         affirm(bd == 1U || bd == 2U || bd == 4U || bd == 8U || bd == 16U);
         tc->format = 0U;
         break;

      case PNG_COLOR_TYPE_PALETTE:
         affirm(bd == 1U || bd == 2U || bd == 4U || bd == 8U);
         tc->format = PNG_FORMAT_FLAG_COLORMAP | PNG_FORMAT_FLAG_COLOR;
         cd = 8U;
         break;

      case PNG_COLOR_TYPE_GRAY_ALPHA:
         affirm(bd == 8U || bd == 16U);
         tc->format = PNG_FORMAT_FLAG_ALPHA;
         break;

      case PNG_COLOR_TYPE_RGB:
         affirm(bd == 8U || bd == 16U);
         tc->format = PNG_FORMAT_FLAG_COLOR;
         break;

      case PNG_COLOR_TYPE_RGB_ALPHA:
         affirm(bd == 8U || bd == 16U);
         tc->format = PNG_FORMAT_FLAG_COLOR | PNG_FORMAT_FLAG_ALPHA;
         break;

      default:
         impossible("PNG color type");
   }

   tc->bit_depth = bd;
   tc->range = 0;

   /* Preset the sBIT data to full precision/handled. */
   tc->sBIT_R = tc->sBIT_G = tc->sBIT_B = tc->sBIT_A = cd;
#  ifdef PNG_READ_sBIT_SUPPORTED
      {
         int handled = 1;

         if ((png_ptr->color_type & PNG_COLOR_MASK_COLOR) != 0)
         {
            png_byte c = png_ptr->sig_bit.red;
            if (c > 0 && c < cd)
            {
               tc->sBIT_R = c;
               handled = 0;
            }

            c = png_ptr->sig_bit.green;
            if (c > 0 && c < cd)
            {
               tc->sBIT_G = c;
               handled = 0;
            }

            c = png_ptr->sig_bit.blue;
            if (c > 0 && c < cd)
            {
               tc->sBIT_B = c;
               handled = 0;
            }
         }

         else /* grayscale */
         {
            png_byte c = png_ptr->sig_bit.gray;
            if (c > 0 && c < cd)
            {
               tc->sBIT_R = tc->sBIT_G = tc->sBIT_B = c;
               handled = 0;
            }
         }

         /* The palette-mapped format doesn't store alpha information, an
          * omission in the spec that is difficult to fix.  Notice that
          * 'handled' is not cleared below, this is because the alpha channel is
          * always linear, so the sBIT_A value can always be treated as a
          * precision value.
          */
         if ((png_ptr->color_type & PNG_COLOR_MASK_ALPHA) != 0)
         {
            png_byte c = png_ptr->sig_bit.alpha;
            if (c > 0 && c < cd)
               tc->sBIT_A = c;
         }

         /* If 'handled' did not get cleared there is no sBIT information. */
         if (handled)
            tc->invalid_info = PNG_INFO_sBIT;
      }
#  else /* !READ_sBIT */
      /* No sBIT information */
      tc->invalid_info = PNG_INFO_sBIT;
#  endif /* !READ_sBIT */
}

png_transformp /*PRIVATE*/
png_add_transform(png_structrp png_ptr, size_t size, png_transform_fn fn,
   unsigned int order)
{
   /* Add a transform.  This is a minimal implementation; the order is just
    * controlled by 'order', the result is a point to the new transform, or
    * to an existing one if one was already in the list.
    */
   png_transformp *p = &png_ptr->transform_list;

   while (*p != NULL && (*p)->order < order)
      p = &(*p)->next;

   if (size == 0)
      size = sizeof (png_transform);

   else
      affirm(size >= sizeof (png_transform));

   if (*p == NULL || (*p)->order > order)
   {
      png_transformp t;

      t = png_voidcast(png_transformp, png_malloc(png_ptr, size));
      memset(t, 0, size); /* zeros out the extra data too */
      /* *p comes after the new entry, t: */
      t->next = *p;
      t->fn = fn;
      t->free = NULL;
      t->order = order;
      t->size = 0xFFFFU & size;
      *p = t;
      return t;
   }

   else /* (*p)->order matches order, return *p */
   {
       affirm((*p)->fn == fn && (*p)->order == order && (*p)->size == size);
       return *p;
   }
}

png_transformp /* PRIVATE */
png_push_transform(png_structrp png_ptr, size_t size, png_transform_fn fn,
   png_transformp *transform, png_transform_controlp tc)
{
   png_transformp tr = *transform;
   unsigned int order = tr->order;

   /* Basic loop detection: */
   affirm(fn != NULL && tr->fn != fn);

   /* Move the following transforms up: */
   {
      unsigned int old_order = order;

      do
      {
         tr->order = ++old_order;
         tr = tr->next;
      }
      while (tr != NULL && tr->order == old_order);

      affirm(tr == NULL || tr->order > old_order);
   }

   *transform = png_add_transform(png_ptr, size, fn, order);

   if (tc != NULL)
      fn(transform, tc);

   return *transform;
}

#ifdef PNG_USER_TRANSFORM_PTR_SUPPORTED
static png_transformp
png_find_transform(png_const_structrp png_ptr, unsigned int order)
   /* Find a transform with the given order, or return NULL.  Currently only
    * used here.
    */
{
   png_transformp p = png_ptr->transform_list;

   for (;;)
   {
      if (p == NULL || p->order > order)
         return NULL;

      if (p->order == order)
         return p;

      p = p->next;
   }
}
#endif /* USER_TRANSFORM_PTR */

static void
remove_transform(png_const_structp png_ptr, png_transformp *transform)
   /* Remove a transform on a running list */
{
   png_transformp tp = *transform;
   png_transformp next = tp->next;

   *transform = next;
   tp->next = NULL;
   png_transform_free(png_ptr, &tp);
}

#ifdef PNG_READ_TRANSFORMS_SUPPORTED
void /* PRIVATE */
png_remove_transform(png_const_structp png_ptr, png_transformp *transform)
{
   remove_transform(png_ptr, transform);
}
#endif /* READ_TRANSFORMS */

static unsigned int
run_transform_list_forwards(png_transform_controlp tc, png_transformp *start,
   png_transformp end/*NULL for whole list*/)
   /* Called from the init code and below, the caller must initialize 'tc' */
{
   png_const_structp png_ptr = tc->png_ptr;
   unsigned int max_depth = PNG_TC_PIXEL_DEPTH(*tc);

   /* Caller guarantees that *start is non-NULL */
   debug(*start != NULL);

   do
   {
      if ((*start)->fn != NULL)
         (*start)->fn(start, tc);

      if ((*start)->fn == NULL) /* delete this transform */
         remove_transform(png_ptr, start);

      else
      {
         /* Handle the initialization of the maximum pixel depth. */
         unsigned int tc_depth = PNG_TC_PIXEL_DEPTH(*tc);

         if (tc_depth > max_depth)
            max_depth = tc_depth;

         /* Advance to the next transform. */
         start = &(*start)->next;
      }
   }
   while (*start != NULL && *start != end);

   /* This only goes wrong if 'end' was non-NULL and not in the list: */
   debug(*start == end);

   return max_depth;
}

#ifdef PNG_READ_TRANSFORMS_SUPPORTED
unsigned int /* PRIVATE */
png_run_this_transform_list_forwards(png_transform_controlp tc,
   png_transformp *start, png_transformp end)
{
   return run_transform_list_forwards(tc, start, end);
}
#endif /* READ_TRANSFORMS */

#ifdef PNG_READ_SUPPORTED
unsigned int /* PRIVATE */
png_run_transform_list_forwards(png_structp png_ptr, png_transform_controlp tc)
{
   if (png_ptr->transform_list != NULL)
      return run_transform_list_forwards(tc, &png_ptr->transform_list, NULL);

   else
      return PNG_PIXEL_DEPTH(*png_ptr);
}
#endif /* READ */

#ifdef PNG_WRITE_SUPPORTED /* only used from pngwrite.c */
static unsigned int
run_transform_list_backwards(png_transform_controlp tc, png_transformp *list)
{
   png_const_structp png_ptr = tc->png_ptr;
   unsigned int max_depth = 0;

   if ((*list)->next != NULL)
      max_depth = run_transform_list_backwards(tc, &(*list)->next);

   /* Note that the above might change (*list)->next, but it can't change
    * *list itself.
    */
   if ((*list)->fn != NULL)
      (*list)->fn(list, tc);

   /* If that set 'fn' to NULL this transform must be removed; this is how
    * (*list)->next gets changed in our caller:
    */
   if ((*list)->fn == NULL)
      remove_transform(png_ptr, list);

   else
   {
      unsigned int depth = PNG_TC_PIXEL_DEPTH(*tc);

      if (depth > max_depth)
         max_depth = depth;
   }

   return max_depth;
}

void /* PRIVATE */
png_run_transform_list_backwards(png_structp png_ptr, png_transform_controlp tc)
{
   if (png_ptr->transform_list != NULL)
   {
      /* This doesn't take account of the base PNG depth, but that shouldn't
       * matter, it's just a check:
       */
      unsigned int max_depth =
         run_transform_list_backwards(tc, &png_ptr->transform_list);

      /* Better late than never (if this fires a memory overwrite has happened):
       */
      affirm(max_depth <= png_ptr->row_max_pixel_depth);
   }
}
#endif /* WRITE */

static unsigned int
init_transform_mech(png_structrp png_ptr, png_transform_control *tc, int start)
   /* Called each time to run the transform list once during initialization. */
{
   png_init_transform_control(tc, png_ptr);
   tc->init = start ? PNG_TC_INIT_FORMAT : PNG_TC_INIT_FINAL;
#  ifdef PNG_READ_TRANSFORMS_SUPPORTED
      if (png_ptr->read_struct)
         return png_read_init_transform_mech(png_ptr, tc);
      else
#  endif
   return run_transform_list_forwards(tc, &png_ptr->transform_list, NULL);
}
#endif /* TRANSFORM_MECH */

#ifdef PNG_PALETTE_MAX_SUPPORTED
static int
set_palette_max(png_structrp png_ptr, png_transformp tr, unsigned int max,
      unsigned int format_max)
   /* Called whenever a new maximum pixel value is found */
{
   /* One of these must be true: */
#  ifdef PNG_CHECK_FOR_INVALID_INDEX_SUPPORTED
      if (max >= (tr->args & 0x1FFU) && !png_ptr->palette_index_check_issued)
      {
         /* In 1.7 only issue the error/warning by default; the 'check' API is
          * used to enable/disable the check.  Assume that if the app enabled it
          * then the app will be checking the result with get_palette_max in
          * read.  In write an error results unless the check is disabled.
          */
         if (png_ptr->palette_index_check == PNG_PALETTE_CHECK_DEFAULT
#           ifdef PNG_WRITE_SUPPORTED
               || (!png_ptr->read_struct &&
                   png_ptr->palette_index_check != PNG_PALETTE_CHECK_OFF)
#           endif /* WRITE */
            )
         {
#           ifdef PNG_READ_SUPPORTED
#              ifdef PNG_WRITE_SUPPORTED
                  if (png_ptr->read_struct)
#              endif /* WRITE */
                  png_chunk_benign_error(png_ptr, "palette index too large");
#              ifdef PNG_WRITE_SUPPORTED
                  else
#              endif
#           endif /* READ */
#           ifdef PNG_WRITE_SUPPORTED
               png_error(png_ptr, "palette index too large");
#           endif /* WRITE */
         }

         png_ptr->palette_index_check_issued = 1;
      }
#  endif /* CHECK_FOR_INVALID_INDEX */
#  ifdef PNG_GET_PALETTE_MAX_SUPPORTED
      png_ptr->palette_index_max = png_check_byte(png_ptr, max);
#  endif

   if (max == format_max)
   {
      tr->fn = NULL; /* no point continuing once the max has been seen */
      return 1; /* stop */
   }

   return 0; /* keep going */
}

static void
palette_max_1bpp(png_transformp *tr, png_transform_controlp tc)
{
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_uint_32 width = tc->width;

   while (width >= 8)
   {
      if (*sp++) break;
      width -= 8;
   }

   if (width < 8)
   {
      if (width == 0 ||
          (*sp & (((1U<<width)-1U) << (8-width))) == 0)
         return; /* no '1' pixels */
   }

   /* If the code reaches this point there is a set pixel */
   (void)set_palette_max(tc->png_ptr, *tr, 1U, 1U);
}

static void
palette_max_2bpp(png_transformp *tr, png_transform_controlp tc)
{
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_uint_32 width = tc->width;
   const png_uint_32 args = (*tr)->args;
   unsigned int max = args >> 24; /* saved maximum */

   while (width > 0)
   {
      png_uint_32 input = 0U, test;
      unsigned int new_max;

      /* This just skips 0 bytes: */
      while (width > 0)
      {
         unsigned int next = *sp++;

         /* There may be partial pixels at the end, just remove the absent
          * pixels with a right shift:
          */
         if (width >= 4)
            width -= 4;
         else
            next >>= (4U-width) * 2U, width = 0;

         if (next)
         {
            input = (input << 8) | next;
            if ((input & 0xFF000000U) != 0)
               break;
         }
      }

      test = input & 0xAAAAAAAAU;

      if (test != 0)
      {
         if ((input & (test >> 1)) != 0)
            new_max = 3U; /* both bits set in at least one pixel */

         else if (max < 2U)
            new_max = 2U;

         else
            continue; /* no change to max */
      }

      else /* test is 0 */ if (input != 0 && max == 0)
         new_max = 1U;

      else /* input is 0, or max is at least 1 */
         continue;

      /* new_max is greater than max: */
      if (set_palette_max(tc->png_ptr, *tr, new_max, 3U))
         return;

      /* Record new_max: */
      max = new_max;
   }

   /* End of input, check the next line. */
   (*tr)->args = (max << 24) + (args & 0xFFFFFFU);
}

static void
palette_max_4bpp(png_transformp *tr, png_transform_controlp tc)
{
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_uint_32 width = tc->width;
   const png_uint_32 args = (*tr)->args;
   unsigned int max = args >> 24; /* saved maximum */

   while (width > 0)
   {
      unsigned int input = *sp++;

      if (width >= 2)
         width -= 2;
      else
         input >>= 1, width = 0;

      if ((input & 0xFU) > max)
         max = input & 0xFU;

      if (((input >> 4) & 0xFU) > max)
         max = (input >> 4) & 0xFU;
   }

   if (max > (args >> 24))
   {
      if (set_palette_max(tc->png_ptr, *tr, max, 15U))
         return;

      (*tr)->args = (max << 24) + (args & 0xFFFFFFU);
   }
}

static void
palette_max_8bpp(png_transformp *tr, png_transform_controlp tc)
{
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_uint_32 width = tc->width;
   const png_uint_32 args = (*tr)->args;
   unsigned int max = args >> 24; /* saved maximum */

   while (width > 0)
   {
      unsigned int input = *sp++;

      if (input > max)
         max = input;

      --width;
   }

   if (max > (args >> 24))
   {
      if (set_palette_max(tc->png_ptr, *tr, max, 255U))
         return;

      (*tr)->args = (max << 24) + (args & 0xFFFFFFU);
   }
}

static void
palette_max_init(png_transformp *tr, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   affirm((tc->format & PNG_FORMAT_FLAG_COLORMAP) != 0);
   debug(tc->init);

   if (tc->init == PNG_TC_INIT_FINAL)
   {
      /* Record the palette depth to check here along with the running total
       * in the top 8 bits (initially 0, which is always valid).
       */
      (*tr)->args = png_ptr->num_palette;

      switch (tc->bit_depth)
      {
         case 1: (*tr)->fn = palette_max_1bpp; break;
         case 2: (*tr)->fn = palette_max_2bpp; break;
         case 4: (*tr)->fn = palette_max_4bpp; break;
         case 8: (*tr)->fn = palette_max_8bpp; break;
         default:impossible("palette bit depth");
      }

      png_ptr->palette_index_have_max = 1U;
   }
#  undef png_ptr
}
#endif /* PALETTE_MAX */

#ifdef PNG_GET_PALETTE_MAX_SUPPORTED
int PNGAPI
png_get_palette_max(png_const_structrp png_ptr, png_const_inforp info_ptr)
{
   if (png_ptr != NULL && png_ptr->palette_index_have_max)
      return png_ptr->palette_index_max;

   /* This indicates to the caller that the information is not available: */
   return -1;
   PNG_UNUSED(info_ptr)
}
#endif /* GET_PALETTE_MAX */

void /* PRIVATE */
png_init_row_info(png_structrp png_ptr)
{
   /* PNG pixels never exceed 64 bits in depth: */
   const png_byte png_depth =
      png_check_bits(png_ptr, PNG_PIXEL_DEPTH(*png_ptr), 7U);

#  ifdef PNG_TRANSFORM_MECH_SUPPORTED
      /* The palette index check stuff is *on* automatically.  To handle this
       * add it here, if it is supported.
       */
#     ifdef PNG_PALETTE_MAX_SUPPORTED
         /* The logic here is a little complex because of the plethora of
          * #defines controlling this stuff.
          */
         if (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE/* fast escape */ && (
#           if defined (PNG_READ_GET_PALETTE_MAX_SUPPORTED) ||\
               defined (PNG_READ_CHECK_FOR_INVALID_INDEX_SUPPORTED)
               (png_ptr->read_struct
#              ifdef PNG_READ_CHECK_FOR_INVALID_INDEX_SUPPORTED
                  && (png_ptr->palette_index_check == PNG_PALETTE_CHECK_ON ||
                      (png_ptr->palette_index_check == PNG_PALETTE_CHECK_DEFAULT
                       && png_ptr->num_palette < (1U << png_ptr->bit_depth)))
#              endif /* READ_CHECK_FOR_INVALID_INDEX */
               )
#           else /* no READ support */
               0
#           endif /* READ checks */
            ||
#           if defined (PNG_WRITE_GET_PALETTE_MAX_SUPPORTED) ||\
               defined (PNG_WRITE_CHECK_FOR_INVALID_INDEX_SUPPORTED)
               (!png_ptr->read_struct
#              ifdef PNG_WRITE_CHECK_FOR_INVALID_INDEX_SUPPORTED
                  && (png_ptr->palette_index_check == PNG_PALETTE_CHECK_ON ||
                      (png_ptr->palette_index_check == PNG_PALETTE_CHECK_DEFAULT
                       && png_ptr->num_palette < (1U << png_ptr->bit_depth)))
#              endif /* WRITE_CHECK_FOR_INVALID_INDEX */
               )
#           else /* no WRITE support */
               0
#           endif /* WRITE checks */
            ))
            png_add_transform(png_ptr, 0/*size*/, palette_max_init,
               PNG_TR_CHECK_PALETTE);
#     endif

      /* Application transforms may change the format of the data or, when
       * producing interlaced images, the number of pixels in a line.  This code
       * determines the maximum pixel depth required and allows transformations
       * a chance to initialize themselves.
       */
      if (png_ptr->transform_list != NULL)
      {
         png_transform_control tc;

         (void)init_transform_mech(png_ptr, &tc, 1/*start*/);

         png_ptr->row_format = png_check_bits(png_ptr, tc.format, PNG_RF_BITS);
         affirm(tc.bit_depth <= 32);
         png_ptr->row_bit_depth = png_check_bits(png_ptr, tc.bit_depth, 6);
         png_ptr->row_range = png_check_bits(png_ptr, tc.range, 3);
#        ifdef PNG_READ_GAMMA_SUPPORTED
            png_ptr->row_gamma = tc.gamma;
#        endif /* READ_GAMMA */

         /* The above may have cancelled all the transforms in the list. */
         if (png_ptr->transform_list != NULL)
         {
            /* Run the transform list again, also forward, and accumulate the
             * maximum pixel depth.  At this point the transforms can swap
             * out their initialization code.
             */
            unsigned int max_depth =
               init_transform_mech(png_ptr, &tc, 0/*final*/);

            /* init_transform_mech is expected to take the input depth into
             * account:
             */
            debug(max_depth >= png_depth);
            if (max_depth < png_depth)
                max_depth = png_depth;
            affirm(max_depth <= (png_ptr->read_struct ? 128U : 64U));

#           ifdef PNG_READ_TRANSFORMS_SUPPORTED
               /* Set this now because it only gets resolved finally at this
                * point.
                */
               png_ptr->invalid_info = tc.invalid_info;
#           endif /* READ_TRANSFORMS */

            /* And check the transform fields: */
            affirm(png_ptr->row_format == tc.format &&
               png_ptr->row_range == tc.range &&
               png_ptr->row_bit_depth == tc.bit_depth);
#           ifdef PNG_READ_GAMMA_SUPPORTED
               affirm(png_ptr->row_gamma == tc.gamma);
#           endif /* READ_GAMMA */

            png_ptr->row_max_pixel_depth =
               png_check_bits(png_ptr, max_depth, 8U);

            /* On 'read' input_depth is the PNG pixel depth and output_depth is
             * the depth of the pixels passed to the application, but on 'write'
             * the transform list is reversed so output_depth is the PNG depth
             * and input_depth the application depth.
             */
            {
               const png_byte app_depth =
                  png_check_bits(png_ptr, PNG_TC_PIXEL_DEPTH(tc), 8U);

               affirm(app_depth <= max_depth);

               if (png_ptr->read_struct)
               {
                  png_ptr->row_input_pixel_depth = png_depth;
                  png_ptr->row_output_pixel_depth = app_depth;
               }

               else
               {
                  png_ptr->row_input_pixel_depth = app_depth;
                  png_ptr->row_output_pixel_depth = png_depth;
               }

               return; /* to skip the default settings below */
            }
         }
      }

      else /* png_ptr->transform_list == NULL */
      {
         png_ptr->row_format = png_check_bits(png_ptr,
            PNG_FORMAT_FROM_COLOR_TYPE(png_ptr->color_type), PNG_RF_BITS);
         png_ptr->row_bit_depth = png_check_bits(png_ptr, png_ptr->bit_depth,
            6);
         png_ptr->row_range = 0;
#        ifdef PNG_READ_GAMMA_SUPPORTED
            if ((png_ptr->colorspace.flags &
                  (PNG_COLORSPACE_INVALID|PNG_COLORSPACE_HAVE_GAMMA)) ==
                 PNG_COLORSPACE_HAVE_GAMMA)
               png_ptr->row_gamma = png_ptr->colorspace.gamma;
#        endif /* READ_GAMMA */
#        ifdef PNG_READ_TRANSFORMS_SUPPORTED
            png_ptr->invalid_info = 0U;
#        endif /* READ_TRANSFORMS */
      }
#  endif /* TRANSFORM_MECH */

   /* We get here if there are no transforms therefore no change to the pixel
    * bit depths.
    */
   png_ptr->row_output_pixel_depth = png_ptr->row_max_pixel_depth =
      png_ptr->row_input_pixel_depth = png_depth;
}

#if defined(PNG_READ_INTERLACING_SUPPORTED) || \
    defined(PNG_WRITE_INTERLACING_SUPPORTED)
int PNGAPI
png_set_interlace_handling(png_structrp png_ptr)
{
   png_debug(1, "in png_set_interlace handling");

   if (png_ptr != 0)
   {
      if (png_ptr->read_struct)
      {
#        ifdef PNG_READ_INTERLACING_SUPPORTED
            if (png_ptr->interlaced)
            {
               png_ptr->do_interlace = 1;
               return PNG_INTERLACE_ADAM7_PASSES;
            }

            return 1;
#        else /* !READ_INTERLACING */
            png_app_error(png_ptr, "no de-interlace support");
            /* return 0 below */
#        endif /* !READ_INTERLACING */
      }

      else /* write */
      {
#        ifdef PNG_WRITE_INTERLACING_SUPPORTED
            if (png_ptr->interlaced)
            {
               png_ptr->do_interlace = 1;
               return PNG_INTERLACE_ADAM7_PASSES;
            }

            return 1;
#        else /* !WRITE_INTERLACING */
            png_app_error(png_ptr, "no interlace support");
            /* return 0 below */
#        endif /* !WRITE_INTERLACING */
      }
   }

   /* API CHANGE: 1.7.0: returns 0 if called with a NULL png_ptr */
   return 0;
}
#endif /* READ_INTERLACING || WRITE_INTERLACING */

#ifdef PNG_MNG_READ_FEATURES_SUPPORTED
/* Undoes intrapixel differencing, this is called immediately after the PNG
 * filter has been undone.
 */
static void
png_do_read_intrapixel_RGB8(png_transformp *tr, png_transform_controlp tc)
{
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_uint_32 width = tc->width;

   tc->sp = dp;

   /* TAKE CARE: dp and sp may be the same, in which case the assignments to *dp
    * are overwriting sp[]
    */
   do
   {
      *dp++ = PNG_BYTE(sp[0] + sp[1]); /* red+green */
      *dp++ = *++sp; /* green */
      *dp++ = PNG_BYTE(sp[0] + sp[1]); /* green+blue */
      sp += 2;
   }
   while (--width > 0);

#  define png_ptr (tc->png_ptr)
   UNTESTED
#  undef png_ptr
   PNG_UNUSED(tr)
}

static void
png_do_read_intrapixel_RGBA8(png_transformp *tr, png_transform_controlp tc)
{
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_uint_32 width = tc->width;

   tc->sp = dp;

   do
   {
      *dp++ = PNG_BYTE(sp[0] + sp[1]); /* red+green */
      *dp++ = *++sp; /* green */
      *dp++ = PNG_BYTE(sp[0] + sp[1]); /* green+blue */
      sp += 2;
      *dp++ = *sp++; /* alpha */
   }
   while (--width > 0);

#  define png_ptr (tc->png_ptr)
   UNTESTED
#  undef png_ptr
   PNG_UNUSED(tr)
}

static void
png_do_read_intrapixel_RGB16(png_transformp *tr, png_transform_controlp tc)
{
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_uint_32 width = tc->width;

   tc->sp = dp;

   /* The input consists of 16-bit values and, by examination of the code
    * (please, someone, check; I didn't read the spec) the differencing is done
    * against the 16-bit green value.
    */
   do
   {
      unsigned int red   = png_get_uint_16(sp + 0);
      unsigned int green = png_get_uint_16(sp + 2);
      unsigned int blue  = png_get_uint_16(sp + 4);
      sp += 6;

      red  += green;
      blue += green;

      *dp++ = PNG_BYTE(red >> 8);
      *dp++ = PNG_BYTE(red);
      *dp++ = PNG_BYTE(green >> 8);
      *dp++ = PNG_BYTE(green);
      *dp++ = PNG_BYTE(blue >> 8);
      *dp++ = PNG_BYTE(blue);
   }
   while (--width > 0);

#  define png_ptr (tc->png_ptr)
   UNTESTED
#  undef png_ptr
   PNG_UNUSED(tr)
}

static void
png_do_read_intrapixel_RGBA16(png_transformp *tr, png_transform_controlp tc)
{
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_uint_32 width = tc->width;

   tc->sp = dp;

   /* As above but copy the alpha over too. */
   do
   {
      unsigned int red   = png_get_uint_16(sp + 0);
      unsigned int green = png_get_uint_16(sp + 2);
      unsigned int blue  = png_get_uint_16(sp + 4);
      sp += 6;

      red  += green;
      blue += green;

      *dp++ = PNG_BYTE(red >> 8);
      *dp++ = PNG_BYTE(red);
      *dp++ = PNG_BYTE(green >> 8);
      *dp++ = PNG_BYTE(green);
      *dp++ = PNG_BYTE(blue >> 8);
      *dp++ = PNG_BYTE(blue);
      *dp++ = *sp++;
      *dp++ = *sp++; /* alpha */
   }
   while (--width > 0);

#  define png_ptr (tc->png_ptr)
   UNTESTED
#  undef png_ptr
   PNG_UNUSED(tr)
}

static void
png_init_read_intrapixel(png_transformp *tr, png_transform_controlp tc)
{
   /* Double check the permitted MNG features in case the app turned the feature
    * on then off again.  Also make sure the color type is acceptable; it must
    * be RGB or RGBA.
    */
   png_const_structp png_ptr = tc->png_ptr;

   if ((png_ptr->mng_features_permitted & PNG_FLAG_MNG_FILTER_64) != 0 &&
       (png_ptr->filter_method == PNG_INTRAPIXEL_DIFFERENCING) &&
       (tc->format & (PNG_FORMAT_FLAG_COLOR+PNG_FORMAT_FLAG_COLORMAP)) ==
         PNG_FORMAT_FLAG_COLOR)
   {
      if (tc->init == PNG_TC_INIT_FINAL) switch (PNG_TC_PIXEL_DEPTH(*tc))
      {
         case 24: (*tr)->fn = png_do_read_intrapixel_RGB8;   break;
         case 32: (*tr)->fn = png_do_read_intrapixel_RGBA8;  break;
         case 48: (*tr)->fn = png_do_read_intrapixel_RGB16;  break;
         case 64: (*tr)->fn = png_do_read_intrapixel_RGBA16; break;
         default: impossible("bit depth");
      }
   }

   else
      (*tr)->fn = NULL;
}
#endif /* MNG_READ_FEATURES_SUPPORTED */

#ifdef PNG_MNG_WRITE_FEATURES_SUPPORTED
/* This is just the forward direction of the above:
 *
 *    red := red - green
 *    blue:= blue- green
 *
 * Alpha is not changed.
 */
static void
png_do_write_intrapixel_RGB8(png_transformp *tr, png_transform_controlp tc)
{
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_uint_32 width = tc->width;

   tc->sp = dp;

   /* TAKE CARE: dp and sp may be the same, in which case the assignments to *dp
    * are overwriting sp[]
    */
   do
   {
      *dp++ = PNG_BYTE(sp[0] - sp[1]); /* red-green */
      *dp++ = *++sp; /* green */
      *dp++ = PNG_BYTE(sp[0] - sp[1]); /* green-blue */
      sp += 2;
   }
   while (--width > 0);

#  define png_ptr (tc->png_ptr)
   UNTESTED
#  undef png_ptr
   PNG_UNUSED(tr)
}

static void
png_do_write_intrapixel_RGBA8(png_transformp *tr, png_transform_controlp tc)
{
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_uint_32 width = tc->width;

   tc->sp = dp;

   do
   {
      *dp++ = PNG_BYTE(sp[0] - sp[1]); /* red-green */
      *dp++ = *++sp; /* green */
      *dp++ = PNG_BYTE(sp[0] - sp[1]); /* green-blue */
      sp += 2;
      *dp++ = *sp++; /* alpha */
   }
   while (--width > 0);

#  define png_ptr (tc->png_ptr)
   UNTESTED
#  undef png_ptr
   PNG_UNUSED(tr)
}

static void
png_do_write_intrapixel_RGB16(png_transformp *tr, png_transform_controlp tc)
{
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_uint_32 width = tc->width;

   tc->sp = dp;

   do
   {
      unsigned int red   = png_get_uint_16(sp + 0);
      unsigned int green = png_get_uint_16(sp + 2);
      unsigned int blue  = png_get_uint_16(sp + 4);
      sp += 6;

      red  -= green;
      blue -= green;

      *dp++ = PNG_BYTE(red >> 8);
      *dp++ = PNG_BYTE(red);
      *dp++ = PNG_BYTE(green >> 8);
      *dp++ = PNG_BYTE(green);
      *dp++ = PNG_BYTE(blue >> 8);
      *dp++ = PNG_BYTE(blue);
   }
   while (--width > 0);

#  define png_ptr (tc->png_ptr)
   UNTESTED
#  undef png_ptr
   PNG_UNUSED(tr)
}

static void
png_do_write_intrapixel_RGBA16(png_transformp *tr, png_transform_controlp tc)
{
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_uint_32 width = tc->width;

   tc->sp = dp;

   /* As above but copy the alpha over too. */
   do
   {
      unsigned int red   = png_get_uint_16(sp + 0);
      unsigned int green = png_get_uint_16(sp + 2);
      unsigned int blue  = png_get_uint_16(sp + 4);
      sp += 6;

      red  -= green;
      blue -= green;

      *dp++ = PNG_BYTE(red >> 8);
      *dp++ = PNG_BYTE(red);
      *dp++ = PNG_BYTE(green >> 8);
      *dp++ = PNG_BYTE(green);
      *dp++ = PNG_BYTE(blue >> 8);
      *dp++ = PNG_BYTE(blue);
      *dp++ = *sp++;
      *dp++ = *sp++; /* alpha */
   }
   while (--width > 0);

#  define png_ptr (tc->png_ptr)
   UNTESTED
#  undef png_ptr
   PNG_UNUSED(tr)
}

static void
png_init_write_intrapixel(png_transformp *tr, png_transform_controlp tc)
{
   /* Write filter_method 64 (intrapixel differencing) only if:
    *
    * 1. Libpng was compiled with PNG_MNG_FEATURES_SUPPORTED, and;
    * 2. Libpng did not write a PNG signature (this filter_method is only
    *    used in PNG datastreams that are embedded in MNG datastreams),
    *    and;
    * 3. The application called png_permit_mng_features with a mask that
    *    included PNG_FLAG_MNG_FILTER_64, and;
    * 4. The filter_method is 64, and;
    * 5. The color_type is RGB or RGBA
    */
   png_const_structp png_ptr = tc->png_ptr;

   if ((png_ptr->mng_features_permitted & PNG_FLAG_MNG_FILTER_64) != 0 &&
       (png_ptr->filter_method == PNG_INTRAPIXEL_DIFFERENCING) &&
       (tc->format & (PNG_FORMAT_FLAG_COLOR+PNG_FORMAT_FLAG_COLORMAP)) ==
         PNG_FORMAT_FLAG_COLOR)
   {
      if (tc->init == PNG_TC_INIT_FINAL) switch (PNG_TC_PIXEL_DEPTH(*tc))
      {
         case 24: (*tr)->fn = png_do_write_intrapixel_RGB8;   break;
         case 32: (*tr)->fn = png_do_write_intrapixel_RGBA8;  break;
         case 48: (*tr)->fn = png_do_write_intrapixel_RGB16;  break;
         case 64: (*tr)->fn = png_do_write_intrapixel_RGBA16; break;
         default: impossible("bit depth");
      }
   }

   else
      (*tr)->fn = NULL;
}
#endif /* MNG_WRITE_FEATURES */

#ifdef PNG_MNG_FEATURES_SUPPORTED
png_uint_32 PNGAPI
png_permit_mng_features(png_structrp png_ptr, png_uint_32 mng_features)
{
   if (png_ptr != NULL)
   {
#     ifdef PNG_MNG_READ_FEATURES_SUPPORTED
         if ((mng_features & PNG_FLAG_MNG_FILTER_64) != 0)
            png_add_transform(png_ptr, 0/*size*/, png_init_read_intrapixel,
               PNG_TR_MNG_INTRAPIXEL);
#     else /* !MNG_READ_FEATURES */
         if (png_ptr->read_struct)
         {
            png_app_error(png_ptr, "MNG not supported on read");
            return;
         }
#     endif /* !MNG_READ_FEATURES */

#     ifdef PNG_MNG_WRITE_FEATURES_SUPPORTED
         if ((mng_features & PNG_FLAG_MNG_FILTER_64) != 0)
            png_add_transform(png_ptr, 0/*size*/, png_init_write_intrapixel,
               PNG_TR_MNG_INTRAPIXEL);
#     else /* !MNG_WRITE_FEATURES */
         if (!png_ptr->read_struct)
         {
            png_app_error(png_ptr, "MNG not supported on write");
            return;
         }
#     endif /* !MNG_WRITE_FEATURES */

      return png_ptr->mng_features_permitted =
         mng_features & PNG_ALL_MNG_FEATURES;
   }

   return 0;
}
#endif /* MNG_FEATURES */

#if defined(PNG_READ_SWAP_SUPPORTED) || defined(PNG_WRITE_SWAP_SUPPORTED) ||\
    defined(PNG_READ_BGR_SUPPORTED) || defined(PNG_WRITE_BGR_SUPPORTED) ||\
    defined(PNG_READ_SWAP_ALPHA_SUPPORTED) ||\
    defined(PNG_WRITE_SWAP_ALPHA_SUPPORTED) ||\
    defined(PNG_READ_FILLER_SUPPORTED) ||\
    defined(PNG_WRITE_FILLER_SUPPORTED) ||\
    defined(PNG_READ_STRIP_ALPHA_SUPPORTED) ||\
    defined(PNG_READ_STRIP_16_TO_8_SUPPORTED) ||\
    defined(PNG_READ_GRAY_TO_RGB_SUPPORTED) ||\
    defined(PNG_READ_EXPAND_16_SUPPORTED) ||\
    defined(PNG_READ_RGB_TO_GRAY_SUPPORTED)
/* This is a generic transform which manipulates the bytes in an input row.  The
 * manipulations supported are:
 *
 *    Channel addition (alpha or filler)
 *    Channel removal (alpha or filler)
 *    Channel swaps - RGB to BGR, alpha/filler from last to first and vice versa
 *
 * The output is described in blocks of output pixel size 4-bit codes encoded
 * as follows:
 *
 *    0        Advance the source pointer by the source pixel size, start the
 *             code list again.  This code doesn't actually exist; it is simply
 *             the result of emptying the code list.
 *    1..3     An error (ignored; treated like 0)
 *    4..7     Put filler[code-4] into the output
 *    8..15    Put source byte[code-8] in the output
 *
 * The codes are held in a png_uint_32 parameter.  transform->args is used by
 * the init routine to work out the required codes.  The format change is a mask
 * which is XORed with the tc format.  Note that the init routine works out
 * whether to work from the beginning or end of the row and the codes are always
 * stored LSB first in the order needed.
 */
typedef struct
{
   png_transform tr;
   png_uint_32   codes;      /* As above */
   unsigned int  format;     /* format after transform */
   unsigned int  bit_depth;  /* bit depth after transform */
   png_byte      filler[4];  /* Filler or alpha bytes, LSB first (see below) */
}  png_transform_byte_op;

static void
png_do_byte_ops_up(png_transformp *transform, png_transform_controlp tc)
   /* Row width is unchanged or decreasing */
{
#  define png_ptr (tc->png_ptr)
   png_transform_byte_op *tr =
      png_transform_cast(png_transform_byte_op, *transform);
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   const unsigned int sp_advance = PNG_TC_PIXEL_DEPTH(*tc) >> 3;
   const png_const_bytep ep = sp + PNG_TC_ROWBYTES(*tc);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);

   debug(tc->bit_depth == 8 || tc->bit_depth == 16);
   debug((tc->format & PNG_FORMAT_FLAG_COLORMAP) == 0);

   tc->sp = tc->dp;
   tc->format = tr->format;
   tc->bit_depth = tr->bit_depth;

   /* 'output' is a 32-byte buffer that is used to delay writes for 16 bytes,
    * avoiding overwrite when source and destination buffers are the same.
    * 'hwm' is either 32 or 16, initially '32', when the byte counter 'i'
    * reaches 'hwm' the last-but-one 16 bytes are written; the bytes
    * [hwm..hwm+15] modulo 32.  hwm is then swapped to hwm+16 mod 32 and i
    * continues to advance.  i is always below hwm.
    *
    * At the end the whole remaining buffer from hwm to i is written.
    */
   {
      const png_uint_32 codes = tr->codes;
      png_uint_32 code = codes;
      unsigned int i, hwm; /* buffer index and high-water-mark */
      png_byte output[32];

      hwm = 32;

      for (i=0;;)
      {
         unsigned int next_code = code & 0xf;

         if (next_code >= 8)
            output[i++] = sp[next_code-8];

         else if (next_code >= 4)
            output[i++] = tr->filler[next_code - 4];

         else /* end code */
         {
            sp += sp_advance;

            if (sp >= ep)
               break; /* i may be == hwm at this point. */

            code = codes;
            continue; /* no ouput produced, skip the check */
         }

         code >>= 4; /* find the next code */

         if (i == hwm)
         {
            hwm &= 0x10U; /* 0 or 16 */
            memcpy(dp, output + hwm, 16);
            dp += 16;
            i = hwm; /* reset i if hwm was 32 */
            /* hwm is only ever 16 or 32: */
            hwm += 16;
         }
      }

      /* Write from hwm to (i-1), the delay means there is always something to
       * write.
       */
      hwm &= 0x10U;
      if (hwm == 16)
      {
         debug(i <= 16);
         memcpy(dp, output + hwm, 16);
         dp += 16;
      }

      if (i > 0)
         memcpy(dp, output, i);

#     ifndef PNG_RELEASE_BUILD
         dp += i;
         /* The macro expansion exceeded the limit on ANSI strings, so split it:
          */
         dp -= PNG_TC_ROWBYTES(*tc);
         debug(dp == tc->dp);
#     endif
   }

   debug(sp == ep);
#  undef png_ptr
}

#ifdef PNG_READ_TRANSFORMS_SUPPORTED
static void
png_do_byte_ops_down(png_transformp *transform, png_transform_controlp tc)
   /* Row width is increasing */
{
#  define png_ptr (tc->png_ptr)
   png_transform_byte_op *tr =
      png_transform_cast(png_transform_byte_op, *transform);
   const png_const_bytep ep = png_voidcast(png_const_bytep, tc->sp);
   const unsigned int sp_advance = PNG_TC_PIXEL_DEPTH(*tc) >> 3;
   png_const_bytep sp = ep + PNG_TC_ROWBYTES(*tc);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_alloc_size_t dest_rowbytes;

   debug(tc->bit_depth == 8U || tc->bit_depth == 16U);
   debug((tc->format & PNG_FORMAT_FLAG_COLORMAP) == 0U);

   tc->sp = tc->dp;
   tc->format = tr->format;
   tc->bit_depth = tr->bit_depth;
   dest_rowbytes = PNG_TC_ROWBYTES(*tc);
   dp += dest_rowbytes;

   /* In this case the 32-byte buffer is written downwards with a writes delayed
    * by 16 bytes as before.  'hwm' is lower than i; 0 or 16.
    */
   {
      const png_uint_32 codes = tr->codes;
      png_uint_32 code = codes;
      unsigned int size, hwm, i;
      png_byte output[32] = { 0 };

      /* This catches an empty codes array, which would cause all the input to
       * be skipped and, potentially, a garbage output[] to be written (once) to
       * *dp.
       */
      affirm((codes & 0xFU) >= 4U);

      /* Align the writes to a 16-byte multiple from the start of the
       * destination buffer:
       */
      size = dest_rowbytes & 0xFU;
      if (size == 0U) size = 16U;
      i = size+16U;
      sp -= sp_advance; /* Move 1 pixel back */
      hwm = 0U;

      for (;;)
      {
         unsigned int next_code = code & 0xFU;

         if (next_code >= 8U)
            output[--i] = sp[next_code-8U];

         else if (next_code >= 4U)
            output[--i] = tr->filler[next_code - 4U];

         else /* end code */
         {
            sp -= sp_advance;

            if (sp < ep)
               break;

            code = codes;
            continue; /* no ouput produced, skip the check */
         }

         code >>= 4; /* find the next code */

         if (i == hwm)
         {
            /* A partial copy comes at the beginning to align the copies to a
             * 16-byte boundary.  The bytes to be written are the bytes
             * i+16..(hwm-1) except that the partial buffer may reduce this.
             */
            dp -= size;
            hwm ^= 0x10U; /* == i+16 mod 32 */
            memcpy(dp, output + hwm, size);
            size = 16U;
            if (i == 0U) i = 32U;
         }
      }

      /* The loop above only exits with an exit code, so 'i' has been checked
       * against 'hwm' before and, because of the alignment, i will always be
       * either 16 or 32:
       */
      debug((i == 16U || i == 32U) & (((i & 0x10U)^0x10U) == hwm));
      debug(sp+sp_advance == ep);

      /* At the end the bytes i..(hwm-1) need to be written, with the proviso
       * that 'size' will be less than 16 for short rows.  If 'size' is still a
       * short value then the range to be written is output[i..16+(size-1)],
       * otherwise (size == 16) either this is the first write and a full 32
       * bytes will be written (hwm == 0, i == 32) or 16 bytes need to be
       * written.
       */
      if (size < 16U)
      {
         debug(i == 16U);
         dp -= size;
         memcpy(dp, output + i, size);
      }

      else /* size == 16 */
      {
         debug(size == 16U);

         /* Write i..(hwm-1); 16 or 32 bytes, however if 32 bytes are written
          * they are contiguous and i==0.
          *
          * hwm is 0 or 16, i is 16 or 32, swap 0 and 32:
          */
         if (hwm == 0U) hwm = 32U;
         if (i == 32U) i = 0U;
         affirm(i < hwm);
         debug(hwm == i+16U || (i == 0U && hwm == 32U));

         hwm -= i;
         dp -= hwm;
         memcpy(dp, output+i, hwm);
      }
   }

   debug(dp == png_upcast(png_bytep, tc->dp));

#  undef png_ptr
}
#endif /* READ_TRANSFORMS */

/* 16 bit byte swapping */
static void
png_do_bswap(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   png_transform_byte_op *tr =
      png_transform_cast(png_transform_byte_op, *transform);
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   const png_alloc_size_t rowbytes = PNG_TC_ROWBYTES(*tc);

   tc->format = tr->format;
   tc->bit_depth = tr->bit_depth;
   tc->sp = dp;

#  ifdef _XOPEN_SOURCE
      /* byte swapping often has incredibly fast implementations because of the
       * importance in handling ethernet traffic.  X/Open defines swab() for
       * this purpose and it is widely supported and normally incredibly fast:
       */
      debug((rowbytes & 1) == 0);
      swab(sp, dp, rowbytes);
#  else /* !_XOPEN_SOURCE */
      {
         const png_const_bytep ep = sp + rowbytes - 1;

         while (sp < ep)
         {
            png_byte b0 = *sp++;
            *dp++ = *sp++;
            *dp++ = b0;
         }

         debug(sp == ep+1); /* even number of bytes */
      }
#  endif

   PNG_UNUSED(transform)
#  undef png_ptr
}

/* The following flags, store in tr->args, are set by the relevant PNGAPI
 * png_set calls then resolved below.
 */
#define PNG_BO_STRIP_ALPHA  0x0001U /* Remove an alpha channel (read only) */
#define PNG_BO_CHOP_16_TO_8 0x0002U /* Chop 16-bit to 8-bit channels */
#define PNG_BO_GRAY_TO_RGB  0x0004U /* G <-> RGB; replicate channels */
/* QUANTIZE happens here */
#define PNG_BO_EXPAND_16    0x0008U /* Expand 8-bit channels to 16-bit */
#define PNG_BO_BGR          0x0010U /* RGB <-> BGR */
#define PNG_BO_FILLER       0x0020U /* Add a filler/alpha */
#define PNG_BO_SWAP_ALPHA   0x0040U /* xA <-> Ax; alpha swap */
#define PNG_BO_SWAP_16      0x0080U /* 16-bit channel byte swapping */

/* The following are additional flags to qualify the transforms: */
#define PNG_BO_FILLER_ALPHA 0x4000U /* The filler is an alpha value */
#define PNG_BO_FILLER_FIRST 0x8000U /* The filler comes first */

static void
png_init_byte_ops(png_transformp *transform, png_transform_controlp tc)
{
   /* In the absence of png_set_quantize none of the above operations apply to a
    * palette row except indirectly; they may apply if the palette was expanded,
    * but this happens earlier in the pipeline.
    *
    * In the presence of png_set_quantize the rules are considerably more
    * complex.  In libpng 1.6.0 the following operations occur before
    * png_do_quantize:
    *
    *    PNG_BO_GRAY_TO_RGB (png_do_gray_to_rgb, but only sometimes)
    *    PNG_BO_STRIP_ALPHA (png_do_strip_channel; removes alpha)
    *    encode_alpha
    *    scale_16_to_8
    *    PNG_BO_CHOP_16_TO_8 (png_do_chop)
    *
    * The following occur afterward:
    *
    *    PNG_BO_EXPAND_16 (png_do_expand_16)
    *    PNG_BO_GRAY_TO_RGB (png_do_gray_to_rgb, normally)
    *    PNG_BO_BGR (png_do_bgr)
    *    PNG_BO_FILLER (png_do_read_filler)
    *    PNG_BO_SWAP_ALPHA (png_do_read_swap_alpha)
    *    PNG_BO_SWAP_16 (png_do_swap; 16-bit byte swap)
    *
    * The gray to RGB operation needs to occur early for GA or gray+tRNS images
    * where the pixels are being composed on a non-gray value.  For the moment
    * we assume that if this is necessary the following 'init' code will see RGB
    * at this point.
    *
    * The quantize operation operates only if:
    *
    *    1) tc->bit_depth is 8
    *    2) The color type exactly matches that required by the parameters to
    *       png_set_quantize; it can be RGB, RGBA or palette, but
    *       png_set_quantize (not the init routine) determines this.
    *
    * To avoid needing to know this here the two stage initialization is used
    * with two transforms, one pre-quantization the other post.  In the first
    * stage the correct row format and depth is set up.  In the second stage the
    * pre-quantization transform looks for a post-quantization transform
    * immediately following and, if it exists, transfers its flags to that.
    */
   png_structp png_ptr = tc->png_ptr;
   png_transform_byte_op *tr =
      png_transform_cast(png_transform_byte_op, *transform);
   png_uint_32 args = tr->tr.args;
   const unsigned int png_format = tc->format;
   unsigned int format = png_format; /* memory format */
   const unsigned int png_bit_depth = tc->bit_depth;
   unsigned int bit_depth = png_bit_depth; /* memory bit depth */

   debug(tc->init);

   /* Channel swaps do not occur on COLORMAP format data at present because the
    * COLORMAP is limited to 1 byte per pixel (so there is nothing to
    * manipulate). Likewise for low bit depth gray, however the code below may
    * widen 8-bit gray to RGB.
    */
   if ((png_format & PNG_FORMAT_FLAG_COLORMAP) != 0U || png_bit_depth < 8U)
   {
      tr->tr.fn = NULL;
      return;
   }

   /* This will normally happen in TC_INIT_FORMAT, but if there is a
    * png_do_quantize operation which doesn't apply (this is unlikely) it will
    * happen in TC_INIT_FINAL.
    */
   if (tr->tr.next != NULL && tr->tr.next->order == PNG_TR_CHANNEL_POSTQ)
   {
      debug(tr->tr.order == PNG_TR_CHANNEL_PREQ);

      /* So we can merge this transform into the next one, note that because the
       * PNG_BO_FILLER operation is POSTQ we don't need to copy anything other
       * than the flags.
       */
      debug((args & tr->tr.next->args) == 0U);
      tr->tr.next->args |= args;
      tr->tr.fn = NULL;
      return;
   }

   /* Else compact the flags for this transform - this is done in both
    * TC_INIT_FORMAT and TC_INIT_FINAL because it is safer that way; the copy
    * above shouldn't actually affect the results but might result in TO8 and
    * TO16 cancelling each other because they are in separate transforms before
    * the merge above.
    *
    * QUIET API CHANGE:
    * For compatiblity with earlier versions of libpng these tests need to
    * occur in the same order as the earlier transforms; 'TO8' combined with
    * 'TO16' did actually do something to 16-bit data, however now it just
    * preserves the original bit depth.
    */
   if ((args & PNG_BO_STRIP_ALPHA) != 0U)
   {
      if ((format & PNG_FORMAT_FLAG_ALPHA) != 0U)
         format &= PNG_BIC_MASK(PNG_FORMAT_FLAG_ALPHA);

      else
         args &= PNG_BIC_MASK(PNG_BO_STRIP_ALPHA);
   }

   if ((args & PNG_BO_CHOP_16_TO_8) != 0U)
   {
      /* This is the quiet API CHANGE; in fact it isn't necessary, but it
       * seems likely that requesting both operations is a mistake:
       */
      if ((args & PNG_BO_EXPAND_16) != 0U)
         args &= PNG_BIC_MASK(PNG_BO_CHOP_16_TO_8|PNG_BO_EXPAND_16);

      else if (bit_depth == 16U)
      {
         bit_depth = 8U;

         /* This also makes the tRNS chunk unusable: */
         tc->invalid_info |= PNG_INFO_tRNS+PNG_INFO_hIST+PNG_INFO_pCAL;
         /* These need further processing: PNG_INFO_sBIT, PNG_INFO_bKGD */
      }

      else
         args &= PNG_BIC_MASK(PNG_BO_CHOP_16_TO_8);
   }

   /* QUANTIZE happens here */

   if ((args & PNG_BO_EXPAND_16) != 0U)
   {
      /* This only does the 8 to 16-bit part of the expansion by multiply by
       * 65535/255 (257) using byte replication.  The cases of low bit depth
       * gray being expanded to 16-bit have to be handled separately.
       */
      if (bit_depth == 8U)
         bit_depth = 16U;

      else
         args &= PNG_BIC_MASK(PNG_BO_EXPAND_16);
   }

   if ((args & PNG_BO_GRAY_TO_RGB) != 0U)
   {
      if ((format & PNG_FORMAT_FLAG_COLOR) == 0U)
         format |= PNG_FORMAT_FLAG_COLOR;

      else
         args &= PNG_BIC_MASK(PNG_BO_GRAY_TO_RGB);
   }

   if ((args & PNG_BO_BGR) != 0U)
   {
      /* This does not happen on colormaps: */
      if ((format & PNG_FORMAT_FLAG_COLOR) != 0U && !tc->palette)
         format |= PNG_FORMAT_FLAG_BGR;

      else
         args &= PNG_BIC_MASK(PNG_BO_BGR);
   }

   if ((args & PNG_BO_FILLER) != 0U)
   {
      if ((format & PNG_FORMAT_FLAG_ALPHA) == 0U)
      {
         format |= PNG_FORMAT_FLAG_ALPHA;
         tc->channel_add = 1U;
         /* And SWAP_ALPHA did not occur, because prior to 1.7.0 the filler op
          * did not set ALPHA in the color type, so use SWAP_ALPHA to handle the
          * before/after filler location.
          *
          * NOTE: this occurs twice, once in TC_START and once in TC_FINAL, but
          * that is ok, the operations are idempotent.
          *
          * For colormaps (tc->palette set) the filler will just end up setting
          * all the tRNS entries and PNG_BO_SWAP_ALPHA will be cancelled below.
          */
         if ((args & PNG_BO_FILLER_FIRST) != 0U)
            args |= PNG_BO_SWAP_ALPHA;

         else
            args &= PNG_BIC_MASK(PNG_BO_SWAP_ALPHA);

         if (!(args & PNG_BO_FILLER_ALPHA)) /* filler is not alpha */
            format |= PNG_FORMAT_FLAG_AFILLER;
      }

      else
         args &= PNG_BIC_MASK(PNG_BO_FILLER);
   }

   if ((args & PNG_BO_SWAP_ALPHA) != 0U)
   {
      /* This does not happen on color maps: */
      if ((format & PNG_FORMAT_FLAG_ALPHA) != 0U && !tc->palette)
         format |= PNG_FORMAT_FLAG_AFIRST;

      else
         args &= PNG_BIC_MASK(PNG_BO_SWAP_ALPHA);
   }

   if ((args & PNG_BO_SWAP_16) != 0U)
   {
      if (bit_depth == 16U)
         format |= PNG_FORMAT_FLAG_SWAPPED;

      else
         args &= PNG_BIC_MASK(PNG_BO_SWAP_16);
   }

   if (args != 0U)
   {
      /* At the end (TC_INIT_FINAL) work out the mapping array using the codes
       * defined above and store the format and bit depth changes (as changes,
       * so they will work either forward or backward).  The filler array must
       * be set up by the png_set API.
       */
      if (tc->init == PNG_TC_INIT_FINAL)
      {
         const unsigned int png_pixel_size = PNG_TC_PIXEL_DEPTH(*tc) >> 3;

         tc->format = format;
         tc->bit_depth = bit_depth;

         {
            const unsigned int memory_pixel_size = PNG_TC_PIXEL_DEPTH(*tc) >> 3;
            unsigned int code_size, src_size;
            int go_down;
            png_byte codes[8];

            /* The codes array maps the PNG format into the memory format
             * assuming the mapping works upwards in the address space.
             * Initially ignore the bit depth and just work on the first four
             * bytes.
             */
            codes[0] = 8U;
            codes[1] = 9U;
            codes[2] = 10U;
            codes[3] = 11U;
            codes[7] = codes[6] = codes[5] = codes[4] = 0U/*error*/;

            /* PNG_BO_STRIP_ALPHA: handled by memory_pixel_size */
            /* PNG_BO_CHOP_16_TO_8: handled below */
            /* PNG_BO_EXPAND_16: handled below */

            if ((args & PNG_BO_GRAY_TO_RGB) != 0U)
            {
               codes[3] = 9U; /* alpha, if present */
               codes[2] = codes[1] = 8U;

#              ifdef PNG_READ_tRNS_SUPPORTED
                  /* Gray to RGB, so copy the tRNS G value into r,g,b: */
                  if (png_ptr->num_trans == 1U)
                     png_ptr->trans_color.blue =
                        png_ptr->trans_color.green =
                        png_ptr->trans_color.red =
                        png_ptr->trans_color.gray;
#              endif /* READ_tRNS */
            }

            /* 'BGR' and gray-to-RGB are mutually exclusive; with gray-to-RGB
             * codes[0] == codes[2] == 8
             */
            else if ((args & PNG_BO_BGR) != 0U)
            {
               codes[0] = 10U;
               codes[2] = 8U;
            }

            if ((args & PNG_BO_FILLER) != 0U)
            {
               /* The filler alway goes after; for a 'before' filler the code
                * above turns on SWAP_ALPHA too.  The gray-to-RGB transform has
                * happened already, so the location of the filler channel is
                * given by 'format':
                */
               if ((format & PNG_FORMAT_FLAG_COLOR) != 0U)
                  codes[3] = 4U; /* low byte of filler */

               else
                  codes[1] = 4U;
            }

            if ((args & PNG_BO_SWAP_ALPHA) != 0U)
            {
               if ((format & PNG_FORMAT_FLAG_COLOR) != 0U)
               {
                  /* BGR may have swapped the early codes. gray-to-RGB may have
                   * set them all to '8':
                   */
                  png_byte acode = codes[3];
                  codes[3] = codes[2];
                  codes[2] = codes[1];
                  codes[1] = codes[0];
                  codes[0] = acode;
               }

               else /* GA format */
                  codes[0] = codes[1], codes[1] = 8U;
            }

            /* PNG_BO_SWAP_16: 16-bit only, handled below */

            /* Now the 16-bit dependent stuff. */
            if ((args & PNG_BO_CHOP_16_TO_8) != 0U)
            {
               /* 16-bit input, 8-bit output, happens before FILLER so the
                * filler must be an 8-bit value.  Apart from a filler code (4 in
                * this case) the code must be adjusted from byte 'x' to byte
                * '2x' to select the MSB of each 16-bit channel.
                *
                * We must use PNG_FORMAT_CHANNELS here because the memory pixel
                * size might (in the future) include a TO16 operation.
                */
               unsigned int i = PNG_FORMAT_CHANNELS(format);

               while (i > 0U)
               {
                  unsigned int code = codes[--i];

                  if (code > 8U) /* 8, 4 need not change */
                     codes[i] = PNG_BYTE(8U+2U*(code-8U));
               }
            }

            if ((args & PNG_BO_EXPAND_16) != 0U)
            {
               /* Don't expect this with CHOP, but it will work, setting the low
                * 8-bits of each 16-bit value to the high bits.
                */
               unsigned int i = PNG_FORMAT_CHANNELS(format);

               while (i > 0U)
               {
                  png_byte code = codes[--i];

                  /* BSWAP is after FILLER, however the data passed in is a
                   * machine native png_uint_16.  We don't know until this init
                   * routine whether the data is an 8 or 16-bit value because we
                   * don't know the full set of transforms the app will apply
                   * when the png_set_filler API is called.
                   *
                   * This means that the data in tr->filler[] needs to have the
                   * low bits in a known place, so the code here puts the low 8
                   * bits in filler[0], code 4.  Hence the following:
                   */
                  if (code == 4U)
                     codes[2U*i/*MSB*/] = 5U, codes[2U*i+1U/*LSB*/] = 4U;

                  else
                     codes[2U*i] = codes[2U*i+1U] = code;
               }

#              ifdef PNG_READ_tRNS_SUPPORTED
                  /* We're just duplicating bytes, so the tRNS chunk can be
                   * maintained if present.  If the tRNS is for a colormap this
                   * produces garbage in trans_color, but it isn't used.
                   */
                  if (png_ptr->num_trans == 1U)
                  {
#                    define TO16(x) x = PNG_UINT_16((x & 0xFFU) * 0x101U)
                     TO16(png_ptr->trans_color.gray);
                     TO16(png_ptr->trans_color.red);
                     TO16(png_ptr->trans_color.green);
                     TO16(png_ptr->trans_color.blue);
#                    undef TO16
                  }
#              endif /* READ_tRNS */
            }

            else if (bit_depth == 16U)
            {
               /* 16-bit input and output. */
               unsigned int i = PNG_FORMAT_CHANNELS(format);

               while (i > 0U)
               {
                  unsigned int code = codes[--i];

                  if (code == 4U) /* as above */
                     codes[2U*i/*MSB*/] = 5U, codes[2U*i+1U/*LSB*/] = 4U;

                  else
                  {
                     codes[2U*i] = PNG_BYTE(8U+2U*(code-8U));
                     codes[2U*i+1U] = PNG_BYTE(8U+2U*(code-8U)+1U);
                  }
               }
            }

            if ((args & PNG_BO_SWAP_16) != 0U)
            {
               /* bswap the memory bytes. */
               unsigned int i;
               png_byte bswap_codes[sizeof codes];

               debug((memory_pixel_size & 1U) == 0U);

               for (i=0U; i<sizeof codes; ++i)
                  bswap_codes[i] = codes[i ^ 1U];

               memcpy(codes, bswap_codes, sizeof codes);
            }

#           ifdef PNG_WRITE_TRANSFORMS_SUPPORTED
               /* Handle the 'write' case; the codes[] array must be inverted,
                * it lists the PNG pixel for each memory pixel, we need it to
                * list the memory pixel for each PNG pixel.
                */
               if (!png_ptr->read_struct)
               {
                  /* There are no write transforms that add data to the PNG
                   * file; the 'filler' transform removes a channel, but that is
                   * the limit of the changes.
                   */
                  unsigned int i = 0U;
                  png_byte write_codes[8U];

                  memset(write_codes, 0, sizeof write_codes);

                  while (i<memory_pixel_size)
                  {
                     unsigned int code = codes[i];

                     if (code >= 8U) /* 8+index of PNG byte */
                        write_codes[code-8U] = PNG_BYTE(8U+i);
                     /* else this is a filler byte to be removed */
                     else
                        debug(code == 4U || code == 5U);

                     ++i;
                  }

                  code_size = png_pixel_size;
                  src_size = memory_pixel_size;
                  tr->format = png_format;
                  tr->bit_depth = png_bit_depth;

                  /* The PNG size should always be <= to the memory size, the
                   * source pointer will be the memory, the destination the PNG
                   * format, so it should always be possible to do the upwards
                   * copy.
                   */
                  go_down = png_pixel_size > memory_pixel_size;
                  affirm(!go_down);
                  memcpy(codes, write_codes, sizeof codes);
               }

               else
#           endif /* WRITE_TRANSFORMS */
            {
               code_size = memory_pixel_size;
               src_size = png_pixel_size;
               tr->format = format;
               tr->bit_depth = bit_depth;
               go_down = png_pixel_size < memory_pixel_size;
            }

            /* Record this for debugging: */
            tr->tr.args = args;

            /* For the same-pixel-size case check for a bswap; this is available
             * in heavily optimized forms and is a common operation (50% of the
             * time) with 16-bit PNG data, particularly given the handling in
             * the simplified API.
             */
            if (!go_down)
            {
               if (memory_pixel_size == png_pixel_size)
               {
                  int the_same = 1;
                  int swapped = (memory_pixel_size & 1) == 0; /* even count */
                  unsigned int i;

                  for (i=0U; i<memory_pixel_size; ++i)
                  {
                     if (codes[i] != 8U+i)
                     {
                        the_same = 0;
                        if (codes[i] != 8U+(i^1U))
                           swapped = 0;
                        if (!swapped)
                           break;
                     }

                     else /* byte is copied, so it can't be swapped! */
                     {
                        swapped = 0;
                        if (!the_same)
                           break;
                     }
                  }

                  /* Use the 'bswap' routine if possible. */
                  if (swapped)
                  {
                     tr->tr.fn = png_do_bswap;
                     return;
                  }

                  else if (the_same)
                     impossible("not reached");
               }

               tr->tr.fn = png_do_byte_ops_up;

               /* Construct the code, forwards: */
               {
                  unsigned int i = code_size;
                  png_uint_32 code = 0U;

                  while (i > 0U)
                  {
                     unsigned int next = codes[--i];

                     code <<= 4U;

                     if ((next >= 8U && next < 8U+src_size) ||
                           next == 4U || next == 5U)
                        code += next;

                     else
                        impossible("invalid code (up)");
                  }

                  tr->codes = code;
               }
            }

            else /* go_down */
#           ifdef PNG_READ_TRANSFORMS_SUPPORTED
               {
                  tr->tr.fn = png_do_byte_ops_down;

                  /* Construct the code, backwards: */
                  {
                     unsigned int i = 0U;
                     png_uint_32 code = 0U;

                     while (i < code_size)
                     {
                        unsigned int next = codes[i++];

                        code <<= 4;

                        if ((next >= 8U && next < 8U+src_size) ||
                              next == 4U || next == 5U)
                           code += next;

                        else
                           impossible("invalid code (down)");
                     }

                     tr->codes = code;
                  }
               }
#           else /* !READ_TRANSFORMS */
               impossible("not reached"); /* because of the affirm above */
#           endif /* !READ_TRANSFORMS */
         }
      }

      else /* TC_INIT_FORMAT: just store modified 'args' */
      {
         tc->format = format;
         tc->bit_depth = bit_depth;
         tr->tr.args = args;
      }
   }

   else /* the transform is not applicable */
      tr->tr.fn = NULL;
}
#endif /* SWAP poo */

#ifdef PNG_READ_RGB_TO_GRAY_SUPPORTED
static void
png_init_rgb_to_gray_byte_ops(png_transformp *transform,
   png_transform_controlp tc)
{
   /* This just delay initializes the function; all the transform initialization
    * has been done below.
    */
   (*transform)->fn = png_do_byte_ops_up;

   /*  If this happens on a row do the transform immediately: */
   if (!tc->init)
      png_do_byte_ops_up(transform, tc);

   else
   {
      /* This doing the init - update the row information here */
#     define png_ptr (tc->png_ptr)
      png_transform_byte_op *tr =
         png_transform_cast(png_transform_byte_op, *transform);

      debug(tc->bit_depth == 8U || tc->bit_depth == 16U);
      debug((tc->format & PNG_FORMAT_FLAG_COLORMAP) == 0U &&
            (tc->format & PNG_FORMAT_FLAG_COLOR) != 0U);

      tc->format = tr->format;
      tc->bit_depth = tr->bit_depth;
#     undef png_ptr
   }
}

void /* PRIVATE */
png_add_rgb_to_gray_byte_ops(png_structrp png_ptr, png_transform_controlp tc,
   unsigned int index, unsigned int order)
   /* Add a byte_ops transform to convert RGB or RGBA data to 'gray' by
    * selecting just the given change [index]  The transform added is added at
    * 'order'.
    */
{
   png_transform_byte_op *tr = png_transform_cast(png_transform_byte_op,
      png_add_transform(png_ptr, sizeof (png_transform_byte_op),
         png_init_rgb_to_gray_byte_ops, order));

   affirm((tc->format & (PNG_FORMAT_FLAG_COLOR+PNG_FORMAT_FLAG_COLORMAP)) ==
            PNG_FORMAT_FLAG_COLOR &&
          index <= 2 && tc->init == PNG_TC_INIT_FINAL);

   tr->format = tc->format & PNG_BIC_MASK(PNG_FORMAT_FLAG_COLOR);
   tr->bit_depth = tc->bit_depth;

   /* For 1 byte channel [index] plus, maybe, alpha: */
   if (tc->bit_depth == 8)
      tr->codes = 8U + index +
         ((tc->format & PNG_FORMAT_FLAG_ALPHA) != 0 ? (8U+3U) << 4 : 0U);

   else
   {
      affirm(tc->bit_depth == 16);

      /* As above, but two bytes; [2*index] and [2*index+1] */
      index *= 2U;
      tr->codes = (8U + index) + ((9U + index) << 4) +
         ((tc->format & PNG_FORMAT_FLAG_ALPHA) != 0 ?
            ((8U+6U) + ((9U+6U) << 4)) << 8 : 0U);
   }
}
#endif /* READ_RGB_TO_GRAY */

#if defined(PNG_READ_GRAY_TO_RGB_SUPPORTED) &&\
    defined(PNG_READ_BACKGROUND_SUPPORTED)
void /* PRIVATE */
png_push_gray_to_rgb_byte_ops(png_transformp *transform,
   png_transform_controlp tc)
   /* This is an init-time utility to add appropriate byte ops to expand a
    * grayscale PNG data set to RGB.
    */
{
#  define png_ptr (tc->png_ptr)
   png_transformp tr = png_push_transform(png_ptr,
      sizeof (png_transform_byte_op), png_init_byte_ops, transform, NULL);

   tr->args = PNG_BO_GRAY_TO_RGB;
   debug(tr == *transform);
   png_init_byte_ops(transform, tc);
#  undef png_ptr
}
#endif /* GRAY_TO_RGB && READ_BACKGROUND */

#ifdef PNG_READ_STRIP_ALPHA_SUPPORTED
void /* PRIVATE */
png_add_strip_alpha_byte_ops(png_structrp png_ptr)
{
   png_add_transform(png_ptr, sizeof (png_transform_byte_op), png_init_byte_ops,
      PNG_TR_CHANNEL_PREQ)->args |= PNG_BO_STRIP_ALPHA;
}
#endif /* READ_STRIP_ALPHA */

#ifdef PNG_READ_STRIP_16_TO_8_SUPPORTED
/* Chop 16-bit depth files to 8-bit depth */
void PNGAPI
png_set_strip_16(png_structrp png_ptr)
{
   if (png_ptr != NULL)
      png_add_transform(png_ptr, sizeof (png_transform_byte_op),
         png_init_byte_ops, PNG_TR_CHANNEL_PREQ)->args |=
         PNG_BO_CHOP_16_TO_8;
}
#endif /* READ_STRIP_16_TO_8 */

#ifdef PNG_READ_GRAY_TO_RGB_SUPPORTED
void PNGAPI
png_set_gray_to_rgb(png_structrp png_ptr)
{
   if (png_ptr != NULL)
   {
      png_set_expand_gray_1_2_4_to_8(png_ptr);
      png_add_transform(png_ptr, sizeof (png_transform_byte_op),
         png_init_byte_ops, PNG_TR_CHANNEL_PREQ)->args |=
         PNG_BO_GRAY_TO_RGB;
   }
}
#endif /* READ_GRAY_TO_RGB */

/* QUANTIZE */

#ifdef PNG_READ_EXPAND_16_SUPPORTED
/* Expand to 16-bit channels.  PNG_BO_EXPAND_16 also expands the tRNS chunk if
 * it is present, but it requires low bit depth grayscale expanded first.  This
 * must also force palette to RGB.
 */
void PNGAPI
png_set_expand_16(png_structrp png_ptr)
{
   if (png_ptr != NULL)
   {
      png_set_expand_gray_1_2_4_to_8(png_ptr);
      png_set_palette_to_rgb(png_ptr);
      png_add_transform(png_ptr, sizeof (png_transform_byte_op),
         png_init_byte_ops, PNG_TR_CHANNEL_POSTQ)->args |=
         PNG_BO_EXPAND_16;
   }
}
#endif /* READ_EXPAND_16 */

#if defined(PNG_READ_BGR_SUPPORTED) || defined(PNG_WRITE_BGR_SUPPORTED)
void PNGAPI
png_set_bgr(png_structrp png_ptr)
{
   if (png_ptr != NULL)
   {
#     ifndef PNG_READ_BGR_SUPPORTED
         if (png_ptr->read_struct)
         {
            png_app_error(png_ptr, "png_set_bgr not supported on read");
            return;
         }
#     endif
#     ifndef PNG_WRITE_BGR_SUPPORTED
         if (!png_ptr->read_struct)
         {
            png_app_error(png_ptr, "png_set_bgr not supported on write");
            return;
         }
#     endif

      png_add_transform(png_ptr, sizeof (png_transform_byte_op),
         png_init_byte_ops, PNG_TR_CHANNEL_POSTQ)->args |=
         PNG_BO_BGR;
   }
}
#endif /* BGR */

#if defined(PNG_READ_FILLER_SUPPORTED) || defined(PNG_WRITE_FILLER_SUPPORTED)
/* This includes png_set_filler and png_set_add_alpha.  The only difference
 * between the two is that the latter resulted in PNG_COLOR_MASK_ALPHA being
 * added to the info_ptr color type, if png_read_update_info was called whereas
 * the former did not.
 *
 * Regardless of whether the added channel resulted in the change to the
 * png_info color type, the SWAP_ALPHA transform was not performed, even though
 * it apparently occured after the add, because PNG_COLOR_MASK_ALPHA was never
 * set in the 1.6 'row_info'.
 *
 * Consequently 'SWAP_ALPHA' and 'FILLER' were independent; one or the other
 * would occur depending on the color type (not the number of channels) prior to
 * the two transforms.
 *
 * Prior to 1.7.0 the app could obtain information about the memory format by
 * calling png_read_update_info followed by png_get_color_type and
 * png_get_channels.  The first would return PNG_COLOR_TYPE..._ALPHA if
 * png_set_add_alpha was performed and the base type if png_set_filler was
 * performed, however in both cases png_get_channels would return the extra
 * channel; 2 or 4.
 *
 * The app could also insert a user transform callback and view the color type
 * in the old "row_info" structure, however this resulted in an inconsistent
 * color type because png_set_alpha did not add COLOR_MASK_ALPHA to the color
 * type.
 *
 * Hence API CHANGE: 1.7.0, row transform callbacks now see the same color type
 * as reported by png_get_color_type after png_read_update_info.
 */
/* Add a filler byte on read, or remove a filler or alpha byte on write.
 * The filler type has changed in v0.95 to allow future 2-byte fillers
 * for 48-bit input data, as well as to avoid problems with some compilers
 * that don't like bytes as parameters.
 */
static void
set_filler(png_structrp png_ptr, png_uint_32 filler, int filler_loc, int alpha)
{
   if (png_ptr != NULL)
   {
      if (filler_loc != PNG_FILLER_BEFORE && filler_loc != PNG_FILLER_AFTER)
      {
         png_app_error(png_ptr, "png_set_filler: invalid filler location");
         return;
      }

#     ifndef PNG_READ_SWAP_SUPPORTED
         if (png_ptr->read_struct)
         {
            png_app_error(png_ptr, "png_set_filler not supported on read");
            return;
         }
#     endif
#     ifndef PNG_WRITE_SWAP_SUPPORTED
         if (!png_ptr->read_struct)
         {
            png_app_error(png_ptr, "png_set_filler not supported on write");
            return;
         }
#     endif

      {
         png_transform_byte_op *tr =
            png_transform_cast(png_transform_byte_op,
               png_add_transform(png_ptr, sizeof (png_transform_byte_op),
                  png_init_byte_ops, PNG_TR_CHANNEL_POSTQ));
         png_uint_32 args = PNG_BO_FILLER;

         if (filler_loc == PNG_FILLER_BEFORE)
            args |= PNG_BO_FILLER_FIRST;

         if (alpha)
            args |= PNG_BO_FILLER_ALPHA;

         tr->tr.args |= args;

         /* The filler must be stored LSByte first: */
         tr->filler[0] = PNG_BYTE(filler >>  0);
         tr->filler[1] = PNG_BYTE(filler >>  8);
         tr->filler[2] = PNG_BYTE(filler >> 16);
         tr->filler[3] = PNG_BYTE(filler >> 24);
      }
   }
}

void PNGAPI
png_set_filler(png_structrp png_ptr, png_uint_32 filler, int filler_loc)
{
   set_filler(png_ptr, filler, filler_loc, 0/*!alpha*/);
}

/* Added to libpng-1.2.7 */
void PNGAPI
png_set_add_alpha(png_structrp png_ptr, png_uint_32 filler, int filler_loc)
{
   set_filler(png_ptr, filler, filler_loc, 1/*alpha*/);
}
#endif /* FILLER */

#if defined(PNG_READ_SWAP_ALPHA_SUPPORTED) || \
    defined(PNG_WRITE_SWAP_ALPHA_SUPPORTED)
void PNGAPI
png_set_swap_alpha(png_structrp png_ptr)
{
   if (png_ptr != NULL)
   {
#     ifndef PNG_READ_SWAP_SUPPORTED
         if (png_ptr->read_struct)
         {
            png_app_error(png_ptr, "png_set_swap_alpha not supported on read");
            return;
         }
#     endif
#     ifndef PNG_WRITE_SWAP_SUPPORTED
         if (!png_ptr->read_struct)
         {
            png_app_error(png_ptr, "png_set_swap_alpha not supported on write");
            return;
         }
#     endif

      png_add_transform(png_ptr, sizeof (png_transform_byte_op),
         png_init_byte_ops, PNG_TR_CHANNEL_POSTQ)->args |=
         PNG_BO_SWAP_ALPHA;
   }
}
#endif /* SWAP_ALPHA */

#if defined(PNG_READ_SWAP_SUPPORTED) || defined(PNG_WRITE_SWAP_SUPPORTED)
void PNGAPI
png_set_swap(png_structrp png_ptr)
{
   if (png_ptr != NULL)
   {
#     ifndef PNG_READ_SWAP_SUPPORTED
         if (png_ptr->read_struct)
         {
            png_app_error(png_ptr, "png_set_swap not supported on read");
            return;
         }
#     endif
#     ifndef PNG_WRITE_SWAP_SUPPORTED
         if (!png_ptr->read_struct)
         {
            png_app_error(png_ptr, "png_set_swap not supported on write");
            return;
         }
#     endif

      png_add_transform(png_ptr, sizeof (png_transform_byte_op),
         png_init_byte_ops, PNG_TR_CHANNEL_POSTQ)->args |=
         PNG_BO_SWAP_16;
   }
}
#endif /* SWAP */

#if defined(PNG_READ_PACKSWAP_SUPPORTED) ||\
    defined(PNG_WRITE_PACKSWAP_SUPPORTED) ||\
    defined(PNG_READ_INVERT_ALPHA_SUPPORTED) ||\
    defined(PNG_WRITE_INVERT_ALPHA_SUPPORTED) ||\
    defined(PNG_READ_INVERT_SUPPORTED) || defined(PNG_WRITE_INVERT_SUPPORTED)
static png_alloc_size_t
row_align(png_transform_controlp tc)
   /* Utiltity to align the source row (sp) in a transform control; it does this
    * by simply copying it to dp if it is not already aligned.  As a convenience
    * the utility returns the number of bytes in the row.
    */
{
   png_const_structp png_ptr = tc->png_ptr;
   png_const_voidp sp = tc->sp;
   png_voidp dp = tc->dp;
   png_alloc_size_t rowbytes = PNG_TC_ROWBYTES(*tc);

   /* For alignment; if png_alignof is not supported by the compiler this will
    * always do an initial memcpy if the source and destination are not the
    * same.  We can only get here for write; the read case always uses locally
    * allocated buffers, only write reads from the application data directly.
    */
#  ifdef png_alignof
      debug(png_isaligned(dp, png_uint_32));
#  endif
   if (sp != dp && !png_ptr->read_struct && !png_isaligned(sp, png_uint_32))
   {
      UNTESTED
      memcpy(dp, sp, rowbytes);
      tc->sp = dp;
   }

   return rowbytes;
}
#endif /* Stuff that needs row_align */

#if defined(PNG_READ_INVERT_ALPHA_SUPPORTED) ||\
    defined(PNG_WRITE_INVERT_ALPHA_SUPPORTED) ||\
    defined(PNG_READ_INVERT_SUPPORTED) || defined(PNG_WRITE_INVERT_SUPPORTED)
/* Bit-ops; invert bytes.  This works for mono inverts too because even the low
 * bit depths can be handled as bytes (since there can be no intervening
 * channels).
 */
#define PNG_B_INVERT_MONO  1U
#define PNG_B_INVERT_RGB   2U /* not set, used below */
#define PNG_B_INVERT_ALPHA 4U

typedef struct
{
   png_transform tr;
   unsigned int  step0; /* initial advance on sp and dp */
   unsigned int  step;  /* advance after start */
   png_uint_32   mask;  /* XOR mask */
}  png_transform_bit_op;

static void
png_do_invert_all(png_transformp *transform, png_transform_controlp tc)
{
   const png_const_structp png_ptr = tc->png_ptr;
   /* Invert the whole row, quickly */
   const png_const_voidp dp_end = png_upcast(png_bytep, tc->dp) + row_align(tc);
   png_uint_32p dp = png_voidcast(png_uint_32p, tc->dp);
   png_const_uint_32p sp = png_voidcast(png_const_uint_32p, tc->sp);

   tc->sp = dp;

   if (png_ptr->read_struct)
   {
      tc->format |= PNG_FORMAT_FLAG_RANGE;
      tc->range++;
   }

   else if (--(tc->range) == 0)
      tc->format &= PNG_BIC_MASK(PNG_FORMAT_FLAG_RANGE);

   while (png_upcast(void*,dp) < dp_end)
      *dp++ = ~*sp++;

   PNG_UNUSED(transform)
}

static void
png_do_invert_channel(png_transformp *transform, png_transform_controlp tc)
{
   const png_const_structp png_ptr = tc->png_ptr;
   /* Invert just one channel in the row. */
   const png_transform_bit_op * const tr =
      png_transform_cast(png_transform_bit_op, *transform);
   const png_uint_32 mask = tr->mask;
   const unsigned int step = tr->step;
   const unsigned int step0 = tr->step0;
   const png_const_voidp dp_end = png_upcast(png_bytep, tc->dp) + row_align(tc);
   png_uint_32p dp = png_voidcast(png_uint_32p, tc->dp);
   png_const_uint_32p sp = png_voidcast(png_const_uint_32p, tc->sp);

   tc->sp = dp;

   if (png_ptr->read_struct)
   {
      tc->format |= PNG_FORMAT_FLAG_RANGE;
      tc->range++;
   }

   else if (--(tc->range) == 0)
      tc->format &= PNG_BIC_MASK(PNG_FORMAT_FLAG_RANGE);

   if (sp == dp || step == 1)
   {
      sp += step0;
      dp += step0;

      while (png_upcast(void*,dp) < dp_end)
         *dp = *sp ^ mask, dp += step, sp += step;
   }

   else /* step == 2, copy required */
   {
      if (step0) /* must be 1 */
         *dp++ = *sp++;

      while (png_upcast(void*,dp) < dp_end)
      {
         *dp++ = *sp++ ^ mask;
         if (!(png_upcast(void*,dp) < dp_end))
            break;
         *dp++ = *sp++;
      }
   }

   PNG_UNUSED(transform)
}

static void
png_init_invert(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   png_transform_bit_op *tr =
      png_transform_cast(png_transform_bit_op, *transform);
   png_uint_32 invert = tr->tr.args;
   png_uint_32 present; /* channels present */

   if ((tc->format & PNG_FORMAT_FLAG_COLORMAP) != 0)
      present = 0;

   else /* not color mapped */
   {
      if ((tc->format & PNG_FORMAT_FLAG_COLOR) != 0)
         present = PNG_B_INVERT_RGB;
      else
         present = PNG_B_INVERT_MONO;

      if ((tc->format & PNG_FORMAT_FLAG_ALPHA) != 0)
         present |= PNG_B_INVERT_ALPHA;
   }

   /* Cannot invert things that aren't there: */
   invert &= present;

   /* If nothing can be inverted is present the transform is not applicable: */
   if (invert == 0)
      (*transform)->fn = NULL;

   else
   {
      tc->format |= PNG_FORMAT_FLAG_RANGE;
      tc->range++;

      if (tc->init == PNG_TC_INIT_FINAL)
      {
         /* If everything that is present is to be inverted just invert the
          * whole row:
          */
         if (invert == present)
            (*transform)->fn = png_do_invert_all;

         else
         {
            /* One thing is to be inverted, G or A: */
            unsigned int channels = PNG_TC_CHANNELS(*tc);
            unsigned int channel =
               (tc->format & PNG_FORMAT_FLAG_AFIRST) != 0 ? 0 : channels-1;

            affirm(channels == 2 || channels == 4);

            if (invert != PNG_B_INVERT_ALPHA)
            {
               debug(invert == PNG_B_INVERT_MONO && channels == 2 &&
                     present == PNG_B_INVERT_MONO+PNG_B_INVERT_ALPHA);
               channel = (channels-1) - channel;
            }

            affirm(tc->bit_depth == 8 || tc->bit_depth == 16);

            /* So channels[channel] is to be inverted, make a mask: */
            {
               union
               {
                  png_byte bytes[8];
                  png_uint_32 words[2];
               } masks;

               memset(&masks, 0, sizeof masks);

               if (tc->bit_depth == 8)
               {
                  /* channels is 2 or 4, channel < 4. */
                  masks.bytes[channel+channels] = masks.bytes[channel] = 0xff;
                  tr->step = 1;
                  tr->mask = masks.words[0];
                  tr->step0 = 0;
               }

               else /* tc->bit_depth == 16 */
               {
                  channel <<= 1; /* in bytes */
                  masks.bytes[channel+1] = masks.bytes[channel] = 0xff;

                  if (channels == 2)
                  {
                     tr->step = 1;
                     tr->mask = masks.words[0];
                     tr->step0 = 0;
                  }

                  else /* channels == 4 */
                  {
                     tr->step = 2;

                     if (masks.words[0] == 0)
                     {
                        tr->mask = masks.words[1];
                        tr->step0 = 1;
                     }

                     else
                     {
                        tr->mask = masks.words[0];
                        tr->step0 = 0;
                     }
                  }
               }
            }

            (*transform)->fn = png_do_invert_channel;
         }
      }
   }
#  undef png_ptr
}

#if defined(PNG_READ_INVERT_ALPHA_SUPPORTED) ||\
    defined(PNG_WRITE_INVERT_ALPHA_SUPPORTED)
void PNGAPI
png_set_invert_alpha(png_structrp png_ptr)
{
   if (png_ptr != NULL)
   {
      png_add_transform(png_ptr, sizeof (png_transform_bit_op),
         png_init_invert, PNG_TR_INVERT)->args |= PNG_B_INVERT_ALPHA;
#     ifdef PNG_WRITE_INVERT_ALPHA_SUPPORTED
         /* This is necessary to avoid palette processing on write; the only
          * transform that applies to colormapped images is the tRNS chunk
          * invert.
          */
         png_ptr->write_invert_alpha = 1U;
#     endif
   }
}
#endif /* INVERT_ALPHA */

#if defined(PNG_READ_INVERT_SUPPORTED) || defined(PNG_WRITE_INVERT_SUPPORTED)
void PNGAPI
png_set_invert_mono(png_structrp png_ptr)
{
   if (png_ptr != NULL)
      png_add_transform(png_ptr, sizeof (png_transform_bit_op),
         png_init_invert, PNG_TR_INVERT)->args |= PNG_B_INVERT_MONO;
}
#endif /* INVERT */
#endif /* INVERT_ALPHA || INVERT */

/*
 *   WARNING
 *     WARNING
 *       WARNING
 *         WARNING
 *           WARNING  The transforms below are temporary; they can and will be
 *         WARNING    heavily optimized before release.
 *       WARNING
 *     WARNING
 *   WARNING
 */
#if defined(PNG_READ_SHIFT_SUPPORTED) || defined(PNG_WRITE_SHIFT_SUPPORTED)
typedef struct
{
   png_transform  tr;
   png_color_8    true_bits;
}  png_transform_shift;

/* Shift pixel values to take advantage of whole range.  Pass the
 * true number of bits in bit_depth.  The row should be packed
 * according to tc->bit_depth.  Thus, if you had a row of
 * bit depth 4, but the pixels only had values from 0 to 7, you
 * would pass 3 as bit_depth, and this routine would translate the
 * data to 0 to 15.
 *
 * NOTE: this is horrible complexity for no value.  Once people suggested they
 * were selling 16-bit displays with 5:6:5 bits spread R:G:B but so far as I
 * could determine these displays produced intermediate grey (uncolored) colors,
 * which is impossible with a true 5:6:5, so most likely 5:6:5 was marketing.
 */
static unsigned int
set_shifts(unsigned int format, unsigned int bit_depth,
   png_const_color_8p true_bits, int *shift_start, int *shift_dec)
{
   unsigned int channels = 0;

   if ((format & (PNG_FORMAT_FLAG_ALPHA+PNG_FORMAT_FLAG_AFIRST)) ==
       (PNG_FORMAT_FLAG_ALPHA+PNG_FORMAT_FLAG_AFIRST))
      ++channels; /* filled in below */

   if ((format & PNG_FORMAT_FLAG_COLOR) != 0)
   {
      unsigned int offset = /* 0 or 2 as appropriate for red */
         ((format & PNG_FORMAT_FLAG_BGR) != 0) << 1;

      shift_start[channels+offset] = bit_depth - true_bits->red;
      if (shift_dec != NULL) shift_dec[channels+offset] = true_bits->red;

      shift_start[channels+1] = bit_depth - true_bits->green;
      if (shift_dec != NULL) shift_dec[channels+1] = true_bits->green;

      offset ^= 2; /* for blue */
      shift_start[channels+offset] = bit_depth - true_bits->blue;
      if (shift_dec != NULL) shift_dec[channels+offset] = true_bits->blue;

      channels += 3;
   }

   else /* no color: gray */
   {
      shift_start[channels] = bit_depth - true_bits->gray;
      if (shift_dec != NULL) shift_dec[channels] = true_bits->gray;
      ++channels;
   }

   if ((format & PNG_FORMAT_FLAG_ALPHA) != 0)
   {
      const unsigned int offset =
         (format & PNG_FORMAT_FLAG_AFIRST) != 0 ? 0 : channels++;

      shift_start[offset] = bit_depth - true_bits->alpha;
      if (shift_dec != NULL) shift_dec[offset] = true_bits->alpha;
   }

   return channels;
}

#ifdef PNG_WRITE_SHIFT_SUPPORTED
static void
png_do_shift(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   png_transform_shift *tr =
      png_transform_cast(png_transform_shift, *transform);
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_const_bytep dp_end = dp + PNG_TC_ROWBYTES(*tc);

   png_debug(1, "in png_do_shift");

   if (--(tc->range) == 0)
      tc->format &= PNG_BIC_MASK(PNG_FORMAT_FLAG_RANGE);

   tc->sp = dp;

   {
      int shift_start[4], shift_dec[4];
      unsigned int channels = set_shifts(tc->format, tc->bit_depth,
         &tr->true_bits, shift_start, shift_dec);

      debug(PNG_TC_CHANNELS(*tc) == channels);

      /* With low res depths, could only be grayscale, so one channel */
      if (tc->bit_depth < 8)
      {
         unsigned int mask;

         UNTESTED
         affirm(channels == 1);
         /* This doesn't matter but we expect to run before packswap: */
         debug(!(tc->format & PNG_FORMAT_FLAG_SWAPPED));

         if (tr->true_bits.gray == 1 && tc->bit_depth == 2)
            mask = 0x55;

         else if (tc->bit_depth == 4 && tr->true_bits.gray == 3)
            mask = 0x11;

         else
            mask = 0xff;

         while (dp < dp_end)
         {
            int j;
            unsigned int v, out;

            v = *sp++;
            out = 0;

            for (j = shift_start[0]; j > -shift_dec[0]; j -= shift_dec[0])
            {
               if (j > 0)
                  out |= v << j;

               else
                  out |= (v >> (-j)) & mask;
            }

            *dp++ = png_check_byte(png_ptr, out);
         }
      }

      else if (tc->bit_depth == 8)
      {
         unsigned int c = 0;

         UNTESTED
         while (dp < dp_end)
         {

            int j;
            unsigned int v, out;

            v = *sp++;
            out = 0;

            for (j = shift_start[c]; j > -shift_dec[c]; j -= shift_dec[c])
            {
               if (j > 0)
                  out |= v << j;

               else
                  out |= v >> (-j);
            }

            *dp++ = png_check_byte(png_ptr, out);
            if (++c == channels) c = 0;
         }
      }

      else /* tc->bit_depth == 16 */
      {
         unsigned int c = 0, s0, s1;

         UNTESTED
         if ((tc->format & PNG_FORMAT_FLAG_SWAPPED) != 0)
            s0 = 0, s1 = 8; /* LSB */

         else
            s0 = 8, s1 = 0; /* MSB */

         while (dp < dp_end)
         {
            int j;
            unsigned int value, v;

            v = *sp++ << s0;
            v += *sp++ << s1;
            value = 0;

            for (j = shift_start[c]; j > -shift_dec[c]; j -= shift_dec[c])
            {
               if (j > 0)
                  value |= v << j;

               else
                  value |= v >> (-j);
            }

            *dp++ = PNG_BYTE(value >> s0);
            *dp++ = PNG_BYTE(value >> s1);
         }
      }
   }
#  undef png_ptr
}
#endif /* WRITE_SHIFT */

#ifdef PNG_READ_SHIFT_SUPPORTED
/* Reverse the effects of png_do_shift.  This routine merely shifts the
 * pixels back to their significant bits values.  Thus, if you have
 * a row of bit depth 8, but only 5 are significant, this will shift
 * the values back to 0 through 31.
 */
static void
png_do_unshift(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   png_transform_shift *tr =
      png_transform_cast(png_transform_shift, *transform);
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_const_bytep dp_end = dp + PNG_TC_ROWBYTES(*tc);

   png_debug(1, "in png_do_unshift");

   tc->range++;
   tc->format |= PNG_FORMAT_FLAG_RANGE;

   {
      int shift[4];
      unsigned int channels = set_shifts(tc->format, tc->bit_depth,
         &tr->true_bits, shift, NULL);

      debug(PNG_TC_CHANNELS(*tc) == channels);

      {
         unsigned int c, have_shift;

         for (c = have_shift = 0; c < channels; ++c)
         {
            /* A shift of more than the bit depth is an error condition but it
             * gets ignored here.
             */
            if (shift[c] <= 0 || (unsigned)/*SAFE*/shift[c] >= tc->bit_depth)
               shift[c] = 0;

            else
               have_shift = 1;
         }

         if (have_shift == 0)
            return;
      }

      /* The code below will copy sp to dp, so: */
      tc->sp = dp;

      switch (tc->bit_depth)
      {
         default:
            /* Must be 1bpp gray: should not be here! */
            impossible("unshift bit depth");
            /* NOTREACHED */
            break;

         case 2:
            /* Must be 2bpp gray */
            debug(channels == 1 && shift[0] == 1);
            debug(!(tc->format & PNG_FORMAT_FLAG_SWAPPED));

            while (dp < dp_end)
               *dp++ = (*sp++ >> 1) & 0x55;
            break;

         case 4:
            /* Must be 4bpp gray */
            debug(channels == 1);
            debug(!(tc->format & PNG_FORMAT_FLAG_SWAPPED));
            {
               unsigned int gray_shift = shift[0];
               unsigned int mask =  0xf >> gray_shift; /* <= 4 bits */

               mask |= mask << 4; /* <= 8 bits */

               while (dp < dp_end)
                  *dp++ = (png_byte)/*SAFE*/((*sp++ >> gray_shift) & mask);
            }
            break;

         case 8:
            /* Single byte components, G, GA, RGB, RGBA */
            {
               unsigned int channel = 0;

               while (dp < dp_end)
               {
                  *dp++ = (png_byte)/*SAFE*/(*sp++ >> shift[channel]);
                  if (++channel >= channels)
                     channel = 0;
               }
            }
            break;

         case 16:
            /* Double byte components, G, GA, RGB, RGBA */
            {
               unsigned int channel = 0;
               unsigned int s0, s1;

               if ((tc->format & PNG_FORMAT_FLAG_SWAPPED) != 0)
                  s0 = 0, s1 = 8; /* LSB */

               else
                  s0 = 8, s1 = 0; /* MSB */

               while (dp < dp_end)
               {
                  unsigned int value = *sp++ << s0;

                  value += *sp++ << s1; /* <= 16 bits */

                  value >>= shift[channel];
                  if (++channel >= channels) channel = 0;
                  *dp++ = PNG_BYTE(value >> s0);
                  *dp++ = PNG_BYTE(value >> s1);
               }
            }
            break;
      }
   }

#  undef png_ptr
}
#endif /* READ_SHIFT */

static void
init_shift(png_transformp *transform, png_transform_controlp tc)
{
   png_const_structp png_ptr = tc->png_ptr;

   /* These shifts apply to the component value, not the pixel index, so skip
    * palette data.  In addition there is no *write* shift for palette entries;
    * only a read one, so skip the write/palette case too.
    */
   if ((tc->format & PNG_FORMAT_FLAG_COLORMAP) == 0 &&
       (png_ptr->read_struct || !tc->palette))
   {
      /* The only change to the format is to mark the data as having a non-PNG
       * range.
       */
      tc->range++;
      tc->format |= PNG_FORMAT_FLAG_RANGE;

      if (tc->init == PNG_TC_INIT_FINAL)
      {
#     ifdef PNG_READ_SHIFT_SUPPORTED
         if (png_ptr->read_struct)
         {
            (*transform)->fn = png_do_unshift;
            return;
         }
#     endif
#     ifdef PNG_WRITE_SHIFT_SUPPORTED
         if (png_ptr->read_struct)
         {
            (*transform)->fn = png_do_shift;
            return;
         }
#     endif
      }
   }

   else /* transform not applicable */
      (*transform)->fn = NULL;
}

void PNGAPI
png_set_shift(png_structrp png_ptr, png_const_color_8p true_bits)
{
   if (png_ptr != NULL && true_bits != NULL)
   {
#     ifndef PNG_READ_SHIFT_SUPPORTED
         if (png_ptr->read_struct)
         {
            png_app_error(png_ptr, "png_set_shift not supported on read");
            return;
         }
#     endif
#     ifndef PNG_WRITE_SHIFT_SUPPORTED
         if (!png_ptr->read_struct)
         {
            png_app_error(png_ptr, "png_set_shift not supported on write");
            return;
         }
#     endif

      {
         png_transform_shift *trs = png_transform_cast(png_transform_shift,
            png_add_transform(png_ptr, sizeof (png_transform_shift),
               init_shift, PNG_TR_SHIFT));
         trs->true_bits = *true_bits;
      }
   }
}
#endif /* SHIFT */

#if defined(PNG_READ_PACK_SUPPORTED) || defined(PNG_WRITE_PACK_SUPPORTED)
/* Turn on pixel packing */
void PNGAPI
png_set_packing(png_structrp png_ptr)
{
   /* The transforms aren't symmetric, so even though there is one API there are
    * two internal init functions, one for read, the other write:
    */
   if (png_ptr != NULL)
   {
      if (png_ptr->read_struct)
      {
#        ifdef PNG_READ_PACK_SUPPORTED
            png_add_transform(png_ptr, 0/*size*/, png_init_read_pack,
               PNG_TR_PACK);
#        else
            png_app_error(png_ptr, "png_set_packing not supported on read");
#        endif
      }

      else
      {
#        ifdef PNG_WRITE_PACK_SUPPORTED
            png_add_transform(png_ptr, 0/*size*/, png_init_write_pack,
               PNG_TR_PACK);
#        else
            png_app_error(png_ptr, "png_set_packing not supported on write");
#        endif
      }
   }
}
#endif /* PACK */

#if defined(PNG_READ_PACKSWAP_SUPPORTED) ||\
    defined(PNG_WRITE_PACKSWAP_SUPPORTED)
/* Turn on pixel-swapping within a byte, this is symmetric - doing the swap
 * twice produces the original value, so only one implementation is required for
 * either read or write.
 *
 * Used to be refered to as "packswap", but pixel-swap seems more
 * self-documenting.
 */
static void
png_do_swap_1bit(png_transformp *transform, png_transform_controlp tc)
{
   png_alloc_size_t rowbytes = row_align(tc); /* may change tc->sp */
   png_const_uint_32p sp = png_voidcast(png_const_uint_32p, tc->sp);
   png_uint_32p dp = png_voidcast(png_uint_32p, tc->dp);

   tc->sp = dp;
   tc->format ^= PNG_FORMAT_FLAG_SWAPPED;

   for (;;)
   {
      png_uint_32 s = *sp++;
      s = ((s >> 1) & 0x55555555) | ((s & 0x55555555) << 1);
      s = ((s >> 2) & 0x33333333) | ((s & 0x33333333) << 2);
      s = ((s >> 4) & 0x0f0f0f0f) | ((s & 0x0f0f0f0f) << 4);
      *dp++ = s;
      if (rowbytes <= 4) break;
      rowbytes -= 4;
   }

   PNG_UNUSED(transform)
}

static void
png_do_swap_2bit(png_transformp *transform, png_transform_controlp tc)
{
   png_alloc_size_t rowbytes = row_align(tc); /* may change tc->sp */
   png_const_uint_32p sp = png_voidcast(png_const_uint_32p, tc->sp);
   png_uint_32p dp = png_voidcast(png_uint_32p, tc->dp);

   tc->sp = dp;
   tc->format ^= PNG_FORMAT_FLAG_SWAPPED;

   for (;;)
   {
      png_uint_32 s = *sp++;
      s = ((s >> 2) & 0x33333333) | ((s & 0x33333333) << 2);
      s = ((s >> 4) & 0x0f0f0f0f) | ((s & 0x0f0f0f0f) << 4);
      *dp++ = s;
      if (rowbytes <= 4) break;
      rowbytes -= 4;
   }

   PNG_UNUSED(transform)
}

static void
png_do_swap_4bit(png_transformp *transform, png_transform_controlp tc)
{
   png_alloc_size_t rowbytes = row_align(tc); /* may change tc->sp */
   png_const_uint_32p sp = png_voidcast(png_const_uint_32p, tc->sp);
   png_uint_32p dp = png_voidcast(png_uint_32p, tc->dp);

   tc->sp = dp;
   tc->format ^= PNG_FORMAT_FLAG_SWAPPED;

   for (;;)
   {
      png_uint_32 s = *sp++;
      s = ((s >> 4) & 0x0f0f0f0f) | ((s & 0x0f0f0f0f) << 4);
      *dp++ = s;
      if (rowbytes <= 4) break;
      rowbytes -= 4;
   }

   PNG_UNUSED(transform)
}

static void
init_packswap(png_transformp *transform, png_transform_controlp tc)
{
   png_transform_fn fn;

#  define png_ptr tc->png_ptr
   debug(tc->init);
#  undef png_ptr

   switch (tc->bit_depth)
   {
      case 1: fn = png_do_swap_1bit; break;
      case 2: fn = png_do_swap_2bit; break;
      case 4: fn = png_do_swap_4bit; break;

      default: /* transform not applicable */
         (*transform)->fn = NULL;
         return;
   }

   tc->format ^= PNG_FORMAT_FLAG_SWAPPED;
   if (tc->init == PNG_TC_INIT_FINAL)
      (*transform)->fn = fn;
}

void PNGAPI
png_set_packswap(png_structrp png_ptr)
{
   if (png_ptr != NULL)
   {
#     ifndef PNG_READ_PACKSWAP_SUPPORTED
         if (png_ptr->read_struct)
         {
            png_app_error(png_ptr, "png_set_packswap not supported on read");
            return;
         }
#     endif
#     ifndef PNG_WRITE_PACKSWAP_SUPPORTED
         if (!png_ptr->read_struct)
         {
            png_app_error(png_ptr, "png_set_packswap not supported on write");
            return;
         }
#     endif

      png_add_transform(png_ptr, 0/*size*/, init_packswap, PNG_TR_PIXEL_SWAP);
   }
}
#endif /* PACKSWAP */

/* User transform handling */
#ifdef PNG_USER_TRANSFORM_INFO_SUPPORTED
png_uint_32 PNGAPI
png_get_current_row_number(png_const_structrp png_ptr)
{
   /* See the comments in png.h - this is the sub-image row when reading an
    * interlaced image.
    */
   if (png_ptr != NULL)
   {
      /* In the read case png_struct::row_number is the row in the final image,
       * not the pass, this will return the previous row number if the row isn't
       * in the pass:
       */
      if (png_ptr->read_struct)
         return PNG_PASS_ROWS(png_ptr->row_number+1, png_ptr->pass)-1U;

      else
         return png_ptr->row_number;
   }

   return PNG_UINT_32_MAX; /* help the app not to fail silently */
}

png_byte PNGAPI
png_get_current_pass_number(png_const_structrp png_ptr)
{
   if (png_ptr != NULL)
      return png_ptr->pass;
   return 8; /* invalid */
}
#endif /* USER_TRANSFORM_INFO */

#if defined(PNG_READ_USER_TRANSFORM_SUPPORTED) ||\
    defined(PNG_WRITE_USER_TRANSFORM_SUPPORTED)
typedef struct
{
   png_transform          tr;
   png_user_transform_ptr user_fn;
#ifdef PNG_USER_TRANSFORM_PTR_SUPPORTED
   png_voidp              user_ptr;
#ifdef PNG_READ_USER_TRANSFORM_SUPPORTED
   unsigned int           user_depth;
   unsigned int           user_channels;
#endif
#endif
}  png_user_transform, *png_user_transformp;

typedef const png_user_transform *png_const_user_transformp;

static png_user_transformp
get_user_transform(png_structrp png_ptr)
{
   /* Note that in an added transform the whole transform is memset to 0, so we
    * don't need to initialize anything.
    */
   return png_transform_cast(png_user_transform, png_add_transform(png_ptr,
      sizeof (png_user_transform), NULL/*function*/, PNG_TR_USER));
}
#endif /* READ_USER_TRANSFORM || WRITE_USER_TRANSFORM */

#ifdef PNG_USER_TRANSFORM_PTR_SUPPORTED
png_voidp PNGAPI
png_get_user_transform_ptr(png_const_structrp png_ptr)
{
   if (png_ptr != NULL)
   {
      png_transformp tr = png_find_transform(png_ptr, PNG_TR_USER);

      if (tr != NULL)
      {
         png_user_transformp tru = png_transform_cast(png_user_transform, tr);
         return tru->user_ptr;
      }
   }

   return NULL;
}
#endif /* USER_TRANSFORM_PTR */

#ifdef PNG_USER_TRANSFORM_PTR_SUPPORTED
void PNGAPI
png_set_user_transform_info(png_structrp png_ptr, png_voidp ptr, int depth,
   int channels)
{
   if (png_ptr != NULL)
   {
      /* NOTE: this function only sets the user transform pointer on write, i.e.
       * the depth and channels arguments are ignored.
       */
      png_user_transformp tr = get_user_transform(png_ptr);

      tr->user_ptr = ptr;

#     ifdef PNG_READ_USER_TRANSFORM_SUPPORTED
         if (png_ptr->read_struct)
         {
            if (png_ptr->row_bit_depth == 0)
            {
               if (depth > 0 && depth <= 32 && channels > 0 && channels <= 4 &&
                   (-depth & depth) == depth /* power of 2 */)
               {
                  tr->user_depth = png_check_bits(png_ptr, depth, 6);
                  tr->user_channels = png_check_bits(png_ptr, channels, 3);
               }

               else
                  png_app_error(png_ptr, "unsupported bit-depth or channels");
            }
            else
               png_app_error(png_ptr,
                   "cannot change user info after image start");
         }
#     else /* !READ_USER_TRANSFORM */
         PNG_UNUSED(depth)
         PNG_UNUSED(channels)
#     endif /* !READ_USER_TRANSFORM */
   }
}
#endif /* USER_TRANSFORM_PTR */

#ifdef PNG_READ_USER_TRANSFORM_SUPPORTED
static void
png_do_read_user_transform(png_transformp *trIn, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   png_user_transformp tr = png_transform_cast(png_user_transform, *trIn);

   if (!tc->init && tr->user_fn != NULL)
   {
      png_row_info row_info;

      row_info.width = tc->width;
      row_info.rowbytes = PNG_TC_ROWBYTES(*tc);
      row_info.color_type = png_check_byte(png_ptr,
         PNG_COLOR_TYPE_FROM_FORMAT(tc->format));
      row_info.bit_depth = png_check_byte(png_ptr, tc->bit_depth);
      row_info.channels = png_check_byte(png_ptr,
         PNG_FORMAT_CHANNELS(tc->format));
      row_info.bit_depth = png_check_byte(png_ptr,
         PNG_TC_PIXEL_DEPTH(*tc));

      /* TODO: fix this API, but for the moment we have to copy the row data to
       * the working buffer.  This is an unnecessary perf overhead when a user
       * transform is used to read information without a transform or when it is
       * used on write.
       */
      if (tc->sp != tc->dp)
      {
         memcpy(tc->dp, tc->sp, PNG_TC_ROWBYTES(*tc));
         tc->sp = tc->dp;
      }

      tr->user_fn(png_ptr, &row_info, png_voidcast(png_bytep, tc->dp));
   }

#  ifdef PNG_USER_TRANSFORM_PTR_SUPPORTED
      if (tr->user_depth > 0)
      {
         /* The read transform can modify the bit depth and number of
          * channels; the interface doesn't permit anything else to be
          * changed.  If the information isn't set the user callback has to
          * produce pixels with the correct pixel depth (otherwise the
          * de-interlace won't work) but there really is no other constraint.
          */
         tc->bit_depth = tr->user_depth;

         /* The API is very restricted in functionality; the user_channels
          * can be changed, but the color_type can't, so the format is simply
          * fixed up to match the channels.
          */
         if (tr->user_channels != PNG_FORMAT_CHANNELS(tc->format))
            switch (tr->user_channels)
         {
            case 1:
               tc->format &=
                  PNG_BIC_MASK(PNG_FORMAT_FLAG_COLOR+PNG_FORMAT_FLAG_ALPHA);
               break;

            case 2: /* has to be GA */
               tc->format &=
                  PNG_BIC_MASK(PNG_FORMAT_FLAG_COLORMAP+PNG_FORMAT_FLAG_COLOR);
               tc->format |= PNG_FORMAT_FLAG_ALPHA;
               break;

            case 3: /* has to be RGB */
               tc->format &=
                  PNG_BIC_MASK(PNG_FORMAT_FLAG_COLORMAP|PNG_FORMAT_FLAG_ALPHA);
               tc->format |= PNG_FORMAT_FLAG_COLOR;
               break;

            case 4: /* has to be RGBA */
               tc->format &= PNG_BIC_MASK(PNG_FORMAT_FLAG_COLORMAP);
               tc->format |= (PNG_FORMAT_FLAG_COLOR+PNG_FORMAT_FLAG_ALPHA);
               break;

            default: /* checked before */
               impossible("user channels");
         }

         debug(PNG_FORMAT_CHANNELS(tc->format) == tr->user_channels);
      }
#  endif /* USER_TRANSFORM_PTR */
#  undef png_ptr
}

void PNGAPI
png_set_read_user_transform_fn(png_structrp png_ptr, png_user_transform_ptr
    read_user_transform_fn)
{
   /* There is no 'init' function, just the callback above to handle the
    * transform.
    */
   if (png_ptr != NULL)
   {
      if (png_ptr->read_struct)
      {
         png_user_transformp tr = get_user_transform(png_ptr);

         tr->user_fn = read_user_transform_fn;
         tr->tr.fn = png_do_read_user_transform;
      }

      else
         png_app_error(png_ptr, "cannot set a read transform on write");
   }
}
#endif /* READ_USER_TRANSFORM */

#ifdef PNG_WRITE_USER_TRANSFORM_SUPPORTED
static void
png_do_write_user_transform(png_transformp *trIn, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   /* The write side is pretty simple; call the call-back, it will make the row
    * data right.
    *
    * BUG: we need to copy the input row (it would be a non-quiet API change
    * otherwise) and we don't know how big it is because the information passed
    * to png_set_user_transform_info never was used on write, but that's fine
    * because the write code never did get this right, so presumably all the
    * apps have worked round it...
    */
   if (!tc->init)
   {
      png_user_transformp tr = png_transform_cast(png_user_transform, *trIn);
      png_row_info row_info;

      if (tc->sp != tc->dp) /* no interlace */
      {
         memcpy(tc->dp, tc->sp, PNG_TC_ROWBYTES(*tc));
         tc->sp = tc->dp;
      }

      row_info.width = tc->width;
      row_info.rowbytes = PNG_TC_ROWBYTES(*tc);
      row_info.color_type = png_check_byte(png_ptr,
         PNG_COLOR_TYPE_FROM_FORMAT(tc->format));
      row_info.bit_depth = png_check_byte(png_ptr, tc->bit_depth);
      row_info.channels = png_check_byte(png_ptr,
         PNG_FORMAT_CHANNELS(tc->format));
      row_info.bit_depth = png_check_byte(png_ptr,
         PNG_TC_PIXEL_DEPTH(*tc));

      /* The user function promises to give us this format: */
      tr->user_fn(png_ptr, &row_info, png_voidcast(png_bytep, tc->dp));
   }
#  undef png_ptr
}

void PNGAPI
png_set_write_user_transform_fn(png_structrp png_ptr, png_user_transform_ptr
    write_user_transform_fn)
{

   if (png_ptr != NULL)
   {
      if (!png_ptr->read_struct)
      {
         png_user_transformp tr = get_user_transform(png_ptr);

         tr->user_fn = write_user_transform_fn;
         tr->tr.fn = png_do_write_user_transform;
      }

      else
         png_app_error(png_ptr, "cannot set a write transform on read");
   }
}
#endif /* WRITE_USER_TRANSFORM */
