
/* pngwtran.c - transforms the data in a row for PNG writers
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
#define PNG_SRC_FILE PNG_SRC_FILE_pngwtran

#ifdef PNG_WRITE_PACK_SUPPORTED
/* Pack pixels into bytes. */
static void
png_do_write_pack(png_transformp *transform, png_transform_controlp tc)
{
   png_alloc_size_t rowbytes = PNG_TC_ROWBYTES(*tc);
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_const_bytep ep = png_upcast(png_const_bytep, tc->sp) + rowbytes;
   png_bytep dp = png_voidcast(png_bytep, tc->dp);

   png_debug(1, "in png_do_pack");

#  define png_ptr tc->png_ptr

   switch ((*transform)->args)
   {
      case 1:
      {
         unsigned int mask = 0x80, v = 0;

         while (sp < ep)
         {
            if (*sp++ != 0)
               v |= mask;

            mask >>= 1;

            if (mask == 0)
            {
               mask = 0x80;
               *dp++ = PNG_BYTE(v);
               v = 0;
            }
         }

         if (mask != 0x80)
            *dp++ = PNG_BYTE(v);
         break;
      }

      case 2:
      {
         unsigned int shift = 8, v = 0;

         while (sp < ep)
         {
            shift -= 2;
            v |= (*sp++ & 0x3) << shift;

            if (shift == 0)
            {
               shift = 8;
               *dp++ = PNG_BYTE(v);
               v = 0;
            }
         }

         if (shift != 8)
            *dp++ = PNG_BYTE(v);
         break;
      }

      case 4:
      {
         unsigned int shift = 8, v = 0;

         while (sp < ep)
         {
            shift -= 4;
            v |= ((*sp++ & 0xf) << shift);

            if (shift == 0)
            {
               shift = 8;
               *dp++ = PNG_BYTE(v);
               v = 0;
            }
         }

         if (shift != 8)
            *dp++ = PNG_BYTE(v);
         break;
      }

      default:
         impossible("bit depth");
   }

   if ((tc->format & PNG_FORMAT_FLAG_COLORMAP) == 0 &&
       --(tc->range) == 0)
      tc->format &= PNG_BIC_MASK(PNG_FORMAT_FLAG_RANGE);

   tc->bit_depth = (*transform)->args;
   tc->sp = tc->dp;
#  undef png_ptr
}

void /* PRIVATE */
png_init_write_pack(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr tc->png_ptr
   debug(tc->init);
#  undef png_ptr

   /* The init routine is called *forward* so the transform control we get has
    * the required bit depth and the transform routine will increase it to 8
    * bits per channel.  The code doesn't really care how many channels there
    * are, but the only way to get a channel depth of less than 8 is to have
    * just one channel.
    */
   if (tc->bit_depth < 8) /* else no packing/unpacking */
   {
      if (tc->init == PNG_TC_INIT_FINAL)
      {
         (*transform)->fn = png_do_write_pack;
         /* Record this for the backwards run: */
         (*transform)->args = tc->bit_depth & 0xf;
      }

      if ((tc->format & PNG_FORMAT_FLAG_COLORMAP) == 0)
      {
         tc->range++;
         tc->format |= PNG_FORMAT_FLAG_RANGE; /* forwards: backwards cancels */
      }

      tc->bit_depth = 8;
   }

   else /* the transform is not applicable */
      (*transform)->fn = NULL;
}
#endif /* WRITE_PACK */
