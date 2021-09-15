#ifdef _MSC_VER
#pragma warning (disable:4146)
#endif

/* pngrtran.c - transforms the data in a row for PNG readers
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
 * This file contains functions optionally called by an application
 * in order to tell libpng how to handle data when reading a PNG.
 * Transformations that are used in both reading and writing are
 * in pngtrans.c.
 */

#include "pngpriv.h"
#define PNG_SRC_FILE PNG_SRC_FILE_pngrtran

#ifdef PNG_READ_QUANTIZE_SUPPORTED
typedef struct
{
   png_transform tr;
   png_byte      map[256U]; /* Map of palette values */
   png_byte      lut[1U <<  /* LUT for RGB values */
      (PNG_QUANTIZE_RED_BITS+PNG_QUANTIZE_GREEN_BITS+PNG_QUANTIZE_BLUE_BITS)];
}  png_transform_quantize;

#define PNG_QUANTIZE_MAP 1U /* map is present and not a 1:1 mapping */
#define PNG_QUANTIZE_LUT 2U /* lut has been built */

static void
do_quantize_rgb(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   png_transform_quantize *tr = png_transform_cast(png_transform_quantize,
      *transform);
   unsigned int channels = PNG_TC_CHANNELS(*tc);
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_const_bytep ep = sp + PNG_TC_ROWBYTES(*tc) - channels/*safety*/;
   png_bytep dp = png_voidcast(png_bytep, tc->dp);

   affirm(tc->bit_depth == 8 && (channels == 3 || channels == 4) &&
          !(tc->format & PNG_FORMAT_FLAG_SWAPPED) &&
          (tr->tr.args & PNG_QUANTIZE_LUT) != 0);

   tc->sp = dp;
   tc->format |= PNG_FORMAT_FLAG_COLORMAP;

   while (sp <= ep)
   {
      unsigned int r = sp[0];
      unsigned int g = sp[1];
      unsigned int b = sp[2];

      /* This looks real messy, but the compiler will reduce
       * it down to a reasonable formula.  For example, with
       * 5 bits per color, we get:
       * p = (((r >> 3) & 0x1f) << 10) |
       *    (((g >> 3) & 0x1f) << 5) |
       *    ((b >> 3) & 0x1f);
       */
      *dp++ = tr->lut[(((r >> (8 - PNG_QUANTIZE_RED_BITS)) &
          ((1 << PNG_QUANTIZE_RED_BITS) - 1)) <<
          (PNG_QUANTIZE_GREEN_BITS + PNG_QUANTIZE_BLUE_BITS)) |
          (((g >> (8 - PNG_QUANTIZE_GREEN_BITS)) &
          ((1 << PNG_QUANTIZE_GREEN_BITS) - 1)) <<
          (PNG_QUANTIZE_BLUE_BITS)) |
          ((b >> (8 - PNG_QUANTIZE_BLUE_BITS)) &
          ((1 << PNG_QUANTIZE_BLUE_BITS) - 1))];

      sp += channels;
   }

   affirm(sp == ep+channels);
   UNTESTED
#  undef png_ptr
}

static void
do_quantize_pal(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   png_transform_quantize *tr = png_transform_cast(png_transform_quantize,
      *transform);
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_const_bytep ep = sp + PNG_TC_ROWBYTES(*tc);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);

   affirm(tc->bit_depth == 8 && (tc->format & PNG_FORMAT_FLAG_COLORMAP) != 0 &&
          !(tc->format & PNG_FORMAT_FLAG_SWAPPED) &&
          (tr->tr.args & PNG_QUANTIZE_MAP) != 0);

   tc->sp = dp;

   while (sp < ep)
      *dp++ = tr->map[*sp++];

   UNTESTED
#  undef png_ptr
}

static void
png_init_quantize(png_transformp *transform, png_transform_controlp tc)
{
   if (tc->bit_depth == 8 && (tc->format & PNG_FORMAT_FLAG_COLOR) != 0)
   {
      /* Either colormapped input, RGB or RGBA: */
      if (!(tc->format & PNG_FORMAT_FLAG_COLORMAP)) /* RGB, RGBA */
      {
         /* This must be a 'palette' lookup */
         if (((*transform)->args & PNG_QUANTIZE_LUT) != 0)
         {
            /* This changes the format and invalidates pretty much everything in
             * the info struct:
             */
            tc->format |= PNG_FORMAT_FLAG_COLORMAP;

            if (tc->init == PNG_TC_INIT_FINAL)
            {
               (*transform)->fn = do_quantize_rgb;
               tc->invalid_info |= PNG_INFO_tRNS+PNG_INFO_hIST+PNG_INFO_pCAL+
                  PNG_INFO_sBIT+PNG_INFO_bKGD;
               tc->sBIT_R = tc->sBIT_G = tc->sBIT_B = tc->sBIT_A =
                  png_check_byte(tc->png_ptr, tc->bit_depth);
            }

            return;
         }
      }

      else /* colormapped */
      {
         /* This must be a 'quantize' lookup */
         if (((*transform)->args & PNG_QUANTIZE_MAP) != 0)
         {
            /* This doesn't change the format, just the values: */
            if (tc->init == PNG_TC_INIT_FINAL)
            {
               (*transform)->fn = do_quantize_pal;
               tc->invalid_info |= PNG_INFO_sBIT+PNG_INFO_pCAL;
               tc->sBIT_R = tc->sBIT_G = tc->sBIT_B = tc->sBIT_A =
                  png_check_byte(tc->png_ptr, tc->bit_depth);
            }

            return;
         }
      }
   }

   /* Else not applicable */
   (*transform)->fn = NULL;
}

/* Dither file to 8-bit.  Supply a palette, the current number
 * of elements in the palette, the maximum number of elements
 * allowed, and a histogram if possible.  If the current number
 * of colors is greater then the maximum number, the palette will be
 * modified to fit in the maximum number.  "full_quantize" indicates
 * whether we need a quantizing cube set up for RGB images, or if we
 * simply are reducing the number of colors in a paletted image.
 */
typedef struct png_dsort_struct
{
   struct png_dsort_struct * next;
   png_byte left;
   png_byte right;
} png_dsort;
typedef png_dsort *   png_dsortp;
typedef png_dsort * * png_dsortpp;

static void
init_map(png_bytep map)
   /* Initialize a mapping table to be 1:1 */
{
   png_byte b = 0U;

   do
      map[b] = b;
   while (b++ != 255U);
}

/* Save typing and make code easier to understand */
#define PNG_COLOR_DIST(c1, c2) (abs((int)((c1).red) - (int)((c2).red)) + \
   abs((int)((c1).green) - (int)((c2).green)) + \
   abs((int)((c1).blue) - (int)((c2).blue)))

void PNGAPI
png_set_quantize(png_structrp png_ptr, png_colorp palette,
    int num_palette, int maximum_colors, png_const_uint_16p histogram,
    int full_quantize)
{
   png_debug(1, "in png_set_quantize");

   if (png_ptr != NULL)
   {
      png_transform_quantize *tr = png_transform_cast(png_transform_quantize,
         png_add_transform(png_ptr, sizeof (png_transform_quantize),
            png_init_quantize, PNG_TR_QUANTIZE));

      /* This is weird (consider what happens to png_set_background on a palette
       * image with a tRNS chunk).
       */
      if (palette == png_ptr->palette)
         png_app_warning(png_ptr, "png_set_quantize: PLTE will be damaged");

      if (maximum_colors <= 0 || num_palette > 256)
      {
         /* The spuriously allocated transform will be removed by the init
          * code.
          */
         png_app_error(png_ptr, "png_set_quantize: invalid color count");
         return;
      }

      /* The app passed in a palette with too many colors, it's not clear why
       * libpng is providing this functionality, it's nothing to do with PNG and
       * can be done by the application without any PNG specific knowledge.
       */
      if (num_palette > maximum_colors)
      {
         int map_changed = 0;

         /* The map table must be preset to do no mapping initially: */
         init_map(tr->map);

         if (histogram != NULL)
         {
            /* This is easy enough, just throw out the least used colors.
             * Perhaps not the best solution, but good enough.
             */
            int i;
            png_byte quantize_sort[256U];

            /* Initialize an array to sort colors */
            init_map(quantize_sort);

            /* Find the least used palette entries by starting a
             * bubble sort, and running it until we have sorted
             * out enough colors.  Note that we don't care about
             * sorting all the colors, just finding which are
             * least used.
             */
            for (i = num_palette - 1; i >= maximum_colors; i--)
            {
               int done; /* To stop early if the list is pre-sorted */
               int j;

               done = 1;
               for (j = 0; j < i; j++)
               {
                  if (histogram[quantize_sort[j]] <
                      histogram[quantize_sort[j+1]])
                  {
                     png_byte t = quantize_sort[j];
                     quantize_sort[j] = quantize_sort[j+1];
                     quantize_sort[j+1] = t;
                     done = 0;
                  }
               }

               if (done != 0)
                  break;
            }

            /* Swap the palette around, and set up a table, if necessary */
            if (full_quantize)
            {
               int j = num_palette;

               /* Put all the useful colors within the max, but don't
                * move the others.
                *
                * NOTE: if the app passes in the result of png_get_PLTE it will
                * be overwritten at this point, what is the API?
                */
               for (i = 0; i < maximum_colors; i++)
               {
                  if (quantize_sort[i] >= maximum_colors)
                  {
                     do
                        j--;
                     while (quantize_sort[j] >= maximum_colors);

                     /* NOTE: NOT swapped, so the original palette[i] has been
                      * lost.
                      */
                     palette[i] = palette[j];
                  }
               }
            }

            else /* !full_quantize */
            {
               int j = num_palette;

               /* Move all the used colors inside the max limit, and
                * develop a translation table.
                */
               for (i = 0; i < maximum_colors; i++)
               {
                  /* Only move the colors we need to */
                  if (quantize_sort[i] >= maximum_colors)
                  {
                     png_color tmp_color;

                     do
                        j--;
                     while (quantize_sort[j] >= maximum_colors);

                     tmp_color = palette[j];
                     palette[j] = palette[i];
                     palette[i] = tmp_color;
                     /* Indicate where the color went */
                     tr->map[j] = png_check_byte(png_ptr, i);
                     tr->map[i] = png_check_byte(png_ptr, j);
                     map_changed = 1;
                  }
               }

               /* Find closest color for those colors we are not using */
               for (i = 0; i < num_palette; i++)
               {
                  if (tr->map[i] >= maximum_colors)
                  {
                     int min_d, k, min_k, d_index;

                     /* Find the closest color to one we threw out */
                     d_index = tr->map[i];
                     min_d = PNG_COLOR_DIST(palette[d_index], palette[0]);
                     for (k = 1, min_k = 0; k < maximum_colors; k++)
                     {
                        int d;

                        d = PNG_COLOR_DIST(palette[d_index], palette[k]);

                        if (d < min_d)
                        {
                           min_d = d;
                           min_k = k;
                        }
                     }

                     /* Point to closest color */
                     tr->map[i] = png_check_byte(png_ptr, min_k);
                     map_changed = 1;
                  }
               }
            } /* !full_quantize */
         } /* have a histogram */

         else /* no histogram */
         {
            /* This is much harder to do simply (and quickly).  Perhaps
             * we need to go through a median cut routine, but those
             * don't always behave themselves with only a few colors
             * as input.  So we will just find the closest two colors,
             * and throw out one of them (chosen somewhat randomly).
             * [We don't understand this at all, so if someone wants to
             *  work on improving it, be our guest - AED, GRP]
             */
            int max_d;
            int num_new_palette;
            png_byte index_to_palette[256U];
            png_byte palette_to_index[256U];
            png_dsortp hash[769];

            /* Initialize palette index sort arrays */
            init_map(index_to_palette);
            init_map(palette_to_index);
            memset(hash, 0, sizeof hash);
            num_new_palette = num_palette;

            /* Initial wild guess at how far apart the farthest pixel
             * pair we will be eliminating will be.  Larger
             * numbers mean more areas will be allocated, Smaller
             * numbers run the risk of not saving enough data, and
             * having to do this all over again.
             *
             * I have not done extensive checking on this number.
             */
            max_d = 96;

            while (num_new_palette > maximum_colors)
            {
               int i;
               png_dsortp t = NULL;

               for (i = 0; i < num_new_palette - 1; i++)
               {
                  int j;

                  for (j = i + 1; j < num_new_palette; j++)
                  {
                     int d = PNG_COLOR_DIST(palette[i], palette[j]);

                     if (d <= max_d)
                     {

                        t = png_voidcast(png_dsortp, png_malloc_warn(png_ptr,
                              sizeof (*t)));

                        if (t == NULL)
                            break;

                        t->next = hash[d];
                        t->left = png_check_byte(png_ptr, i);
                        t->right = png_check_byte(png_ptr, j);
                        hash[d] = t;
                     }
                  }
                  if (t == NULL)
                     break;
               }

               if (t != NULL) for (i = 0; i <= max_d; i++)
               {
                  if (hash[i] != NULL)
                  {
                     png_dsortp p;

                     for (p = hash[i]; p != NULL; p = p->next)
                     {
                        if (index_to_palette[p->left] < num_new_palette &&
                            index_to_palette[p->right] < num_new_palette)
                        {
                           int j, next_j;

                           if (num_new_palette & 0x01)
                           {
                              j = p->left;
                              next_j = p->right;
                           }
                           else
                           {
                              j = p->right;
                              next_j = p->left;
                           }

                           num_new_palette--;
                           /* NOTE: overwrites palette */
                           palette[index_to_palette[j]] =
                              palette[num_new_palette];

                           if (full_quantize == 0)
                           {
                              int k;

                              for (k = 0; k < num_palette; k++)
                              {
                                 if (tr->map[k] == index_to_palette[j])
                                 {
                                    tr->map[k] = index_to_palette[next_j];
                                    map_changed = 1;
                                 }

                                 if (tr->map[k] == num_new_palette)
                                 {
                                    tr->map[k] = index_to_palette[j];
                                    map_changed = 1;
                                 }
                              }
                           }

                           index_to_palette[palette_to_index[num_new_palette]] =
                              index_to_palette[j];

                           palette_to_index[index_to_palette[j]] =
                              palette_to_index[num_new_palette];

                           index_to_palette[j] =
                              png_check_byte(png_ptr, num_new_palette);

                           palette_to_index[num_new_palette] =
                              png_check_byte(png_ptr, j);
                        }

                        if (num_new_palette <= maximum_colors)
                           break;
                     }

                     if (num_new_palette <= maximum_colors)
                        break;
                  }
               }

               for (i = 0; i < 769; i++)
               {
                  if (hash[i] != NULL)
                  {
                     png_dsortp p = hash[i];

                     while (p)
                     {
                        t = p->next;
                        png_free(png_ptr, p);
                        p = t;
                     }

                     hash[i] = NULL;
                  }
               }

               max_d += 96;
            } /* while num_new_colors > maximum_colors */
         } /* no histogram */

         num_palette = maximum_colors;

         if (map_changed) /* else the map is 1:1 */
            tr->tr.args |= PNG_QUANTIZE_MAP;
      } /* num_palette > maximum_colors */

      /* The palette has been reduced to the requested number of colors if it
       * was over maximum colors before.
       */

      /* TODO: what is this?  Apparently the png_struct::palette member gets
       * updated if it didn't originally have a palette, but the update relies
       * on the app not freeing the passed in palette.
       */
      if (png_ptr->palette == NULL)
         png_ptr->palette = palette;

      png_ptr->num_palette = png_check_bits(png_ptr, num_palette, 9);

      if (full_quantize)
      {
         int i;
         png_byte distance[1U << (PNG_QUANTIZE_RED_BITS+PNG_QUANTIZE_GREEN_BITS+
            PNG_QUANTIZE_BLUE_BITS)];

         memset(distance, 0xff, sizeof distance);

         for (i = 0; i < num_palette; i++)
         {
            int ir;
            int r = (palette[i].red >> (8 - PNG_QUANTIZE_RED_BITS));
            int g = (palette[i].green >> (8 - PNG_QUANTIZE_GREEN_BITS));
            int b = (palette[i].blue >> (8 - PNG_QUANTIZE_BLUE_BITS));

            for (ir = 0; ir < (1<<PNG_QUANTIZE_RED_BITS); ir++)
            {
               /* int dr = abs(ir - r); */
               int ig;
               int dr = ((ir > r) ? ir - r : r - ir);
               int index_r = (ir << (PNG_QUANTIZE_BLUE_BITS +
                   PNG_QUANTIZE_GREEN_BITS));

               for (ig = 0; ig < (1<<PNG_QUANTIZE_GREEN_BITS); ig++)
               {
                  /* int dg = abs(ig - g); */
                  int ib;
                  int dg = ((ig > g) ? ig - g : g - ig);
                  int dt = dr + dg;
                  int dm = ((dr > dg) ? dr : dg);
                  int index_g = index_r | (ig << PNG_QUANTIZE_BLUE_BITS);

                  for (ib = 0; ib < (1<<PNG_QUANTIZE_BLUE_BITS); ib++)
                  {
                     int d_index = index_g | ib;
                     /* int db = abs(ib - b); */
                     int db = ((ib > b) ? ib - b : b - ib);
                     int dmax = ((dm > db) ? dm : db);
                     int d = dmax + dt + db;

                     if (d < distance[d_index])
                     {
                        distance[d_index] = png_check_byte(png_ptr, d);
                        tr->lut[d_index] = png_check_byte(png_ptr, i);
                     }
                  } /* for blue */
               } /* for green */
            } /* for red */
         } /* num_palette */
      } /* full_quantize */
   } /* png_ptr != NULL */
}
#endif /* READ_QUANTIZE */

#ifdef PNG_READ_PACK_SUPPORTED
/* Unpack pixels of 1, 2, or 4 bits per pixel into 1 byte per pixel,
 * without changing the actual values.  Thus, if you had a row with
 * a bit depth of 1, you would end up with bytes that only contained
 * the numbers 0 or 1.  If you would rather they contain 0 and 255, use
 * png_set_expand_gray_1_2_4_to_8 instead.
 */
static void
png_do_read_unpack(png_transformp *transform, png_transform_controlp tc)
{
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_const_bytep ep = png_voidcast(png_const_bytep, tc->dp);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);

   sp += PNG_TC_ROWBYTES(*tc) - 1; /* Start from end */
   dp += tc->width; /* output bit depth is 8 */

#  define png_ptr (tc->png_ptr)
   png_debug(1, "in png_do_unpack");

   switch (tc->bit_depth)
   {
      case 1:
      {
         /* Because we copy from the last pixel down the shift required
          * at the start is 8-pixels_in_last_byte, which is just:
          */
         unsigned int shift = 7U & -tc->width;

         while (dp > ep)
         {
            *--dp = (*sp >> shift) & 1U;
            shift = 7U & (shift+1U);
            if (shift == 0U)
               --sp;
         }

         debug(shift == 0U);
         break;
      }

      case 2:
      {
         unsigned int shift = 7U & -(tc->width << 1);

         while (dp > ep)
         {
            *--dp = (*sp >> shift) & 3U;
            shift = 7U & (shift+2U);
            if (shift == 0U)
               --sp;
         }

         debug(shift == 0U);
         break;
      }

      case 4:
      {
         unsigned int shift = 7U & -(tc->width << 2);

         while (dp > ep)
         {
            *--dp = (*sp >> shift) & 15U;
            shift = 7U & (shift+4U);
            if (shift == 0U)
               --sp;
         }

         debug(shift == 0U);
         break;
      }

      default:
         impossible("bit depth");
   }

   debug(dp == ep && sp == png_upcast(png_const_bytep, tc->sp)-1U);
   tc->sp = dp;

   if ((tc->format & PNG_FORMAT_FLAG_COLORMAP) == 0U)
   {
      tc->range++;
      tc->format |= PNG_FORMAT_FLAG_RANGE;
   }

   tc->bit_depth = 8U;
   PNG_UNUSED(transform)
#  undef png_ptr
}

/* Called from the curiously named png_set_packing API in pngtrans.c; the read
 * and write code is separated because read 'unpacks' (from PNG format) and
 * write 'packs' (to PNG format.)
 */
void /* PRIVATE */
png_init_read_pack(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr tc->png_ptr
   debug(tc->init);

   if (tc->bit_depth < 8) /* else no packing/unpacking */
   {
      /* For indexed images the pack operation does not invalidate the range; in
       * fact the corresponding shift operation would!
       */
      if ((tc->format & PNG_FORMAT_FLAG_COLORMAP) == 0U)
      {
         tc->range++;
         tc->format |= PNG_FORMAT_FLAG_RANGE;
      }

      tc->bit_depth = 8U;

      if (tc->init == PNG_TC_INIT_FINAL)
         (*transform)->fn = png_do_read_unpack/* sic: it unpacks */;
   }

   else /* the transform is not applicable */
      (*transform)->fn = NULL;

#  undef png_ptr
}
#endif /* READ_PACK */

#if defined(PNG_READ_EXPAND_SUPPORTED) || defined(PNG_READ_BACKGROUND_SUPPORTED)
#  ifdef PNG_READ_tRNS_SUPPORTED
static unsigned int
fill_transparent_pixel(png_const_structrp png_ptr, png_byte *trans)
   /* Fill a byte array according to the transparent pixel value and return a
    * count of the number of bytes.  Low bit depth gray values are replicated in
    * the first byte.  Writes from 1 to 6 bytes.
    */
{
   /* There must be a tRNS chunk and this must not be a palette image: */
   debug(png_ptr->num_trans == 1 &&
      !(png_ptr->color_type & (PNG_COLOR_MASK_ALPHA+PNG_COLOR_MASK_PALETTE)));

   if (!(png_ptr->color_type & PNG_COLOR_MASK_COLOR)) /* gray */
   {
      unsigned int t = png_ptr->trans_color.gray;
      unsigned int depth = png_ptr->bit_depth;

      if (depth < 16U)
      {
         /* ISO PNG 11.3.2.1 "tRNS Transparency": "If the image bit depth is
          * less than 16, the least significant bits are used and the others are
          * 0."  So mask out the upper bits.
          */
         t &= (1U<<depth)-1U;

         /* And replicate low bit-depth values across the byte: */
         while (depth < 8U)
         {
            t |= t << depth;
            depth <<= 1;
         }

         trans[0] = PNG_BYTE(t);
         return 1U;
      }

      /* Else a 16 bit value: */
      trans[0] = PNG_BYTE(t >> 8);
      trans[1] = PNG_BYTE(t);
      return 2U;
   }

   else /* color */ switch (png_ptr->bit_depth)
   {
      case 8: /* 8-bit RGB */
         trans[0] = PNG_BYTE(png_ptr->trans_color.red);
         trans[1] = PNG_BYTE(png_ptr->trans_color.green);
         trans[2] = PNG_BYTE(png_ptr->trans_color.blue);
         return 3U;

      case 16: /* 16-bit RGB */
         trans[0] = PNG_BYTE(png_ptr->trans_color.red >> 8);
         trans[1] = PNG_BYTE(png_ptr->trans_color.red);
         trans[2] = PNG_BYTE(png_ptr->trans_color.green >> 8);
         trans[3] = PNG_BYTE(png_ptr->trans_color.green);
         trans[4] = PNG_BYTE(png_ptr->trans_color.blue >> 8);
         trans[5] = PNG_BYTE(png_ptr->trans_color.blue);
         return 6U;

      default:
         NOT_REACHED;
         return 0U; /* safe */
   }
}
#  endif /* READ_tRNS */
#endif /* READ_EXPAND || READ_BACKGROUND */

#ifdef PNG_READ_EXPAND_SUPPORTED
/* Flags for png_init_expand */
#define PNG_EXPAND_PALETTE  1U /* palette images only, includes tRNS */
#define PNG_EXPAND_LBD_GRAY 2U /* grayscale low-bit depth only */
#define PNG_EXPAND_tRNS     4U /* non-palette images only */

/* This struct is only required for tRNS matching, but it is convenient to
 * allocated it anyway even if READ_tRNS is not supported.
 */
typedef struct
{
   png_transform tr;
   unsigned int  ntrans;               /* number of bytes below */
   png_byte      transparent_pixel[6]; /* the transparent pixel value */
}  png_expand;

#ifdef PNG_READ_tRNS_SUPPORTED
/* Look for colors matching the trans_color in png_ptr, low bit depth gray is
 * covered below so this only need handle 8 abd 16-bit channels.
 */
static void
png_do_expand_tRNS(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   png_expand *tr = png_transform_cast(png_expand, *transform);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   const png_const_bytep ep = sp;
   const unsigned int spixel_size = PNG_TC_PIXEL_DEPTH(*tc) >> 3;
   unsigned int alpha_size;

   /* We expect opaque and transparent pixels to be interleaved but with long
    * sequences of each.  Because we are adding an alpha channel we must copy
    * down.
    */
   debug(!(tc->format & PNG_FORMAT_FLAG_ALPHA));
   debug(spixel_size == tr->ntrans);
   sp += PNG_TC_ROWBYTES(*tc);
   tc->sp = dp;
   tc->format |= PNG_FORMAT_FLAG_ALPHA;
   tc->invalid_info |= PNG_INFO_tRNS;
   tc->transparent_alpha = 1U;
   alpha_size = (PNG_TC_PIXEL_DEPTH(*tc)>>3) - spixel_size;
   debug(alpha_size == 1 || alpha_size == 2);
   dp += PNG_TC_ROWBYTES(*tc);

   do
   {
      unsigned int i = spixel_size;
      png_byte alpha = 0U;

      dp -= alpha_size;
      alpha = 0U;

      /* Copy and check one source pixel (backwards, to avoid any
       * overwrite):
       */
      do if ((*--dp = *--sp) != tr->transparent_pixel[--i]) /* pixel != tRNS */
         alpha = 0xFFU;
      while (i != 0U);

      /* i == 0 */
      do
         dp[spixel_size + i] = alpha;
      while (++i < alpha_size);
   } while (sp > ep);

   debug(sp == ep && dp == tc->dp); /* else overwrite */
#  undef png_ptr
}
#endif /* READ_tRNS */

/* Expand grayscale images of less than 8-bit depth to 8 bits.
 * libpng 1.7.0: this no longer expands everything, it just expands the low bit
 * depth gray row.  It does *NOT* expand the tRNS into an alpha channel unless
 * it is told to do so.
 *
 * API CHANGE: the function now does what it was always meant to do.
 *
 * This is like do_unpack except that the packed data is expanded to the full
 * 8-bit range; scaled up.  This is not a good thing to do on an indexed image;
 * the indices will be invalid.
 *
 * The tRNS handling is included here too; speed is not important because the
 * result will always be cached unless the PNG is very small.
 */
static void
png_do_expand_lbd_gray(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   const png_const_bytep ep = dp;
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   const unsigned int bit_depth = tc->bit_depth;
#  ifdef PNG_READ_sBIT_SUPPORTED
   unsigned int insignificant_bits = 0U;
#  endif /* READ_sBIT */
#  ifdef PNG_READ_tRNS_SUPPORTED
   unsigned int gray = 0xffffU; /* doesn't match anything */
   unsigned int do_alpha = 0U;
#  endif /* READ_tRNS */

   sp += PNG_TC_ROWBYTES(*tc); /* last byte +1 */
   tc->bit_depth = 8U;
   tc->invalid_info |= PNG_INFO_tRNS;
#  ifdef PNG_READ_sBIT_SUPPORTED
      if (bit_depth > 1U /* irrelevant for bit depth 1 */ &&
          !(tc->invalid_info & PNG_INFO_sBIT) &&
          tc->sBIT_G > 0U/*SAFETY*/ && tc->sBIT_G < bit_depth)
         insignificant_bits = bit_depth - tc->sBIT_G;
#  endif /* READ_sBIT */

#  ifdef PNG_READ_tRNS_SUPPORTED
      if (((*transform)->args & PNG_EXPAND_tRNS) != 0)
      {
         tc->format |= PNG_FORMAT_FLAG_ALPHA;
         tc->transparent_alpha = 1U;
         gray = png_ptr->trans_color.gray & ((1U << bit_depth)-1U);
         do_alpha = 1U;
      }

      /* This helps avoid cluttering the code up with #ifdefs: */
#     define check_tRNS if (do_alpha) *--dp = (pixel != gray) * 255U;
#  else /* !READ_tRNS */
#     define check_tRNS
#  endif /* READ_tRNS */

   dp += PNG_TC_ROWBYTES(*tc); /* pre-decremented below */

   switch (bit_depth)
   {
      case 1:
      {
         unsigned int shift = 7U & -tc->width;
         unsigned int s = *--sp;

         for(;;)
         {
            if (shift == 8U) s = *--sp, shift = 0;

            {
               const unsigned int pixel = (s >> shift) & 1U;

               check_tRNS
               *--dp = PNG_BYTE(pixel * 255U);
               if (dp <= ep) break;
            }
            ++shift;
         }

         debug(dp == ep && shift == 7U && sp == tc->sp);
         break;
      }

      case 2:
      {
         unsigned int shift = 7U & -(tc->width << 1)/*overflow ok*/;
         unsigned int s = *--sp;

         for (;;)
         {
            if (shift == 8U) s = *--sp, shift = 0;
            {
               const unsigned int pixel = (s >> shift) & 3U;

               check_tRNS

#  ifdef PNG_READ_sBIT_SUPPORTED
               /* 'sig_bits' must be 1 or 2 leaving insignificant_bits 0 or
                * 1.  This may look silly but it allows a compact representation
                * of 1 bit gray + 1 bit alpha (transparency):
                */
               if (insignificant_bits /* only 1 bit significant */)
                  *--dp = PNG_BYTE((pixel >> 1) * 255U);

               else
#  endif
                  *--dp = PNG_BYTE(pixel * 85U);

               if (dp <= ep) break;
            }
            shift += 2U;
         }

         debug(dp == ep && shift == 6U && sp == tc->sp);
         break;
      }

      case 4:
      {
         unsigned int shift = 7U & -(tc->width << 2)/*overflow ok*/;
         unsigned int s = *--sp;
#  ifdef PNG_READ_sBIT_SUPPORTED
         const unsigned int div = (1U << (4U-insignificant_bits)) - 1U;
#  endif

         for (;;)
         {
            if (shift == 8U) s = *--sp, shift = 0;
            {
               unsigned int pixel = (s >> shift) & 15U;

               check_tRNS

#  ifdef PNG_READ_sBIT_SUPPORTED
               /* insignificant_bits may be 0, 1, 2 or 3, requiring a multiply
                * by 17, 255/7, 85 or 255.  Since this operation is always
                * cached we don't much care about the time to do the divide
                * below.
                */
               if (insignificant_bits)
                  pixel = ((pixel>>insignificant_bits) * 255U + (div>>1)) / div;

               else
#  endif
                  pixel *= 17U;

               *--dp = PNG_BYTE(pixel);
               if (dp <= ep) break;
            }

            shift += 4U;
         }

         debug(dp == ep && shift == 4U && sp == tc->sp);
         break;
      }

      default:
         impossible("bit depth");
   }

   tc->sp = ep;

#  undef check_tRNS
#  undef png_ptr
}

static void
png_init_expand(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   /* The possible combinations are:
    *
    * 1) PALETTE: the 'palette' flag should be set on the transform control and
    *    all that need be done is cancel this to cause the cache code to do the
    *    expansion.
    *
    * 2) LBP_GRAY, LBP_GRAY+tRNS: use png_do_expand_lbd_gray to do the required
    *    expand.  Can be cached.
    *
    * 3) tRNS: scan the row for the relevant tRNS value.
    *
    * Note that expanding 8 to 16 bits is a byte op in pngtrans.c (it just
    * replicates bytes).
    */
   if (tc->palette)
   {
      debug(tc->caching && !(tc->format & PNG_FORMAT_FLAG_COLORMAP));

      if (((*transform)->args & PNG_EXPAND_PALETTE) != 0U)
      {
         tc->palette = 0U;
         tc->invalid_info |= PNG_INFO_PLTE + PNG_INFO_tRNS;
         tc->cost = PNG_CACHE_COST_LIMIT; /* the cache is required! */
      }

      /* Note that this needs to happen when the row is processed (!tc->init) as
       * well.
       */
   }

   else if (!(tc->format & PNG_FORMAT_FLAG_COLORMAP))
   {
      png_uint_32 args = (*transform)->args & PNG_BIC_MASK(PNG_EXPAND_PALETTE);
      unsigned int bit_depth = tc->bit_depth;

      debug(tc->init);

      if (bit_depth >= 8U)
         args &= PNG_BIC_MASK(PNG_EXPAND_LBD_GRAY);

#     ifdef PNG_READ_tRNS_SUPPORTED
         if (png_ptr->num_trans == 0U ||
             (tc->format & PNG_FORMAT_FLAG_ALPHA) != 0U ||
             (tc->invalid_info & PNG_INFO_tRNS) != 0U)
#     endif
         args &= PNG_BIC_MASK(PNG_EXPAND_tRNS);

      (*transform)->args = args;

      switch (args)
      {
         case PNG_EXPAND_LBD_GRAY:
            tc->bit_depth = 8U;
            tc->invalid_info |= PNG_INFO_tRNS;

            if (tc->init == PNG_TC_INIT_FINAL)
               (*transform)->fn = png_do_expand_lbd_gray;
            break;

#     ifdef PNG_READ_tRNS_SUPPORTED
         case PNG_EXPAND_LBD_GRAY + PNG_EXPAND_tRNS:
            tc->bit_depth = 8U;
            tc->format |= PNG_FORMAT_FLAG_ALPHA;
            tc->invalid_info |= PNG_INFO_tRNS;
            tc->transparent_alpha = 1U;

            /* In this case tRNS must be left unmodified for the expansion code
             * to handle.
             */
            if (tc->init == PNG_TC_INIT_FINAL)
               (*transform)->fn = png_do_expand_lbd_gray;
            break;

         case PNG_EXPAND_tRNS:
            if (tc->init == PNG_TC_INIT_FINAL)
            {
               png_expand *tr = png_transform_cast(png_expand, *transform);

               affirm((tc->bit_depth == 8U || tc->bit_depth == 16U) &&
                      (tc->format &
                       (PNG_FORMAT_FLAG_COLORMAP|PNG_FORMAT_FLAG_ALPHA)) == 0U);

               tr->ntrans = fill_transparent_pixel(png_ptr,
                  tr->transparent_pixel);
               tr->tr.fn = png_do_expand_tRNS;
            } /* TC_INIT_FINAL */

            tc->format |= PNG_FORMAT_FLAG_ALPHA;
            tc->invalid_info |= PNG_INFO_tRNS;
            tc->transparent_alpha = 1U;
            break;
#     endif /* READ_tRNS */

         default: /* transform not applicable */
            (*transform)->fn = NULL;
            break;
      }

      implies(tc->init == PNG_TC_INIT_FINAL,
              (*transform)->fn != png_init_expand);
   }

   else /* not applicable */
   {
      debug(tc->init);
      (*transform)->fn = NULL;
      NOT_REACHED;
   }
#  undef png_ptr
}

void PNGAPI
png_set_expand_gray_1_2_4_to_8(png_structrp png_ptr)
{
   if (png_ptr != NULL)
      png_add_transform(png_ptr, sizeof (png_expand), png_init_expand,
         PNG_TR_EXPAND)->args |= PNG_EXPAND_LBD_GRAY;
}

/* Expand paletted images to 8-bit RGB or, if there is a tRNS chunk, RGBA.
 * Note that this is effectively handled by the read code palette optimizations.
 *
 * API CHANGE: this used to have the completely unexpected side effect of
 * turning on the above two optimizations.
 */
void PNGAPI
png_set_palette_to_rgb(png_structrp png_ptr)
{
   if (png_ptr != NULL)
      png_add_transform(png_ptr, sizeof (png_expand), png_init_expand,
         PNG_TR_EXPAND)->args |= PNG_EXPAND_PALETTE;
}

/* Expand paletted images to RGB, expand grayscale images of less than 8-bit
 * depth to 8-bit depth, and expand tRNS chunks to alpha channels.  I.e. all the
 * above.
 */
void PNGAPI
png_set_expand(png_structrp png_ptr)
{
   if (png_ptr != NULL)
   {
      png_set_palette_to_rgb(png_ptr);
      png_set_expand_gray_1_2_4_to_8(png_ptr);
      png_set_tRNS_to_alpha(png_ptr);
   }
}
#endif /* READ_EXPAND */

#if defined(PNG_READ_EXPAND_SUPPORTED) ||\
    defined(PNG_READ_STRIP_ALPHA_SUPPORTED)

#define PNG_INIT_STRIP_ALPHA 1U
#define PNG_INIT_EXPAND_tRNS 2U
static void
png_init_alpha(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   int required = 0;

#  if defined(PNG_READ_EXPAND_SUPPORTED) && defined(PNG_READ_tRNS_SUPPORTED)
      if ((*transform)->args & PNG_INIT_EXPAND_tRNS)
      {
         /* Prior to 1.7 the alpha channel was stripped after expanding the tRNS
          * chunk, so this effectively cancelled out the expand.
          */
         if (png_ptr->num_trans > 0 && !tc->palette &&
             !((*transform)->args & PNG_INIT_STRIP_ALPHA))
         {
            debug((tc->format & PNG_FORMAT_FLAG_COLORMAP) == 0);

            required = 1;
            tc->expand_tRNS = 1U;

            /* This happens as a result of an explicit API call to
             * png_set_tRNS_to_alpha, so expand low-bit-depth gray too:
             */
            if (tc->init == PNG_TC_INIT_FORMAT)
               png_add_transform(png_ptr, sizeof (png_expand), png_init_expand,
                  PNG_TR_EXPAND)->args |= PNG_EXPAND_tRNS + PNG_EXPAND_LBD_GRAY;
         }

         else
            (*transform)->args &= PNG_BIC_MASK(PNG_INIT_EXPAND_tRNS);
      }
#  endif /* READ_EXPAND && READ_tRNS */

#  ifdef PNG_READ_STRIP_ALPHA_SUPPORTED
      if ((*transform)->args & PNG_INIT_STRIP_ALPHA)
      {
         /* When compose is being done tRNS will be expanded regardless of the
          * above test.  Rather that trying to work out if this will happen the
          * code just inserts a strip operation; it will be removed later if it
          * is not needed.
          */
         required = 1;
         tc->strip_alpha = 1U;

         if (tc->init == PNG_TC_INIT_FORMAT)
            png_add_strip_alpha_byte_ops(png_ptr);
      }
#  endif /* READ_STRIP_ALPHA */

   if (!required)
      (*transform)->fn = NULL;
#  undef png_ptr
}
#endif /* READ_EXPAND || READ_STRIP_ALPHA */

#ifdef PNG_READ_EXPAND_SUPPORTED
/* Expand tRNS chunks to alpha channels.  This only expands the tRNS chunk on
 * non-palette formats; call png_set_palette_to_rgb to get the corresponding
 * effect for a palette.
 *
 * Note that this will expand low bit depth gray if there is a tRNS chunk, but
 * if not nothing will happen.
 *
 * API CHANGE: this used to do all the expansions, it was rather pointless
 * calling it.
 */
void PNGAPI
png_set_tRNS_to_alpha(png_structrp png_ptr)
{
   if (png_ptr != NULL)
      png_add_transform(png_ptr, 0/*size*/, png_init_alpha, PNG_TR_INIT_ALPHA)->
         args |= PNG_INIT_EXPAND_tRNS;
}
#endif /* READ_EXPAND */

#ifdef PNG_READ_STRIP_ALPHA_SUPPORTED
void PNGAPI
png_set_strip_alpha(png_structrp png_ptr)
{
   if (png_ptr != NULL)
      png_add_transform(png_ptr, 0/*size*/, png_init_alpha, PNG_TR_INIT_ALPHA)->
         args |= PNG_INIT_STRIP_ALPHA;
}
#endif /* READ_STRIP_ALPHA */

#ifdef PNG_READ_SCALE_16_TO_8_SUPPORTED
static void
png_do_chop_16_to_8(png_transformp *transform, png_transform_controlp tc)
   /* This is actually a repeat of the byte transform, unnecessary code
    * replication.
    *
    * TODO: remove this
    */
{
#  define png_ptr (tc->png_ptr)
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp); /* source */
   png_const_bytep ep = sp + PNG_TC_ROWBYTES(*tc); /* end+1 */
   png_bytep dp = png_voidcast(png_bytep, tc->dp); /* destination */

   debug(tc->bit_depth == 16U);
   tc->sp = dp;
   tc->bit_depth = 8U;

   while (sp < ep)
      *dp++ = *sp, sp += 2;

   debug(sp == ep);
#  undef png_ptr

   PNG_UNUSED(transform)
}

/* A transform containing some useful scaling values... */
typedef struct
{
   png_transform   tr;
   png_uint_32     shifts; /* 4 4-bit values preceeded by a shibboleth (1) */
   png_uint_32     channel_scale[4];
} png_transform_scale_16_to_8;

/* Scale rows of bit depth 16 down to 8 accurately */
static void
png_do_scale_16_to_8(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp); /* source */
   png_const_bytep ep = sp + PNG_TC_ROWBYTES(*tc); /* end+1 */
   png_bytep dp = png_voidcast(png_bytep, tc->dp); /* destination */
   png_transform_scale_16_to_8 *tr =
      png_transform_cast(png_transform_scale_16_to_8, *transform);
   png_uint_32p scale = 0;
   png_uint_32 shift = 1U; /* set the shibboleth at the start */

   debug(tc->bit_depth == 16U);
   tc->sp = dp;
   tc->bit_depth = 8U;

   while (sp < ep)
   {
      /* The input is an array of 16 bit components, these must be scaled to
       * 8 bits each taking into account the sBIT setting.  The calculation
       * requires that the insignificant bits be stripped from the input value
       * via a shift then scaled back to 8 bits:
       *
       *      output = ((input >> shift) * scale + round) >> 24
       *
       * The shifts are packed into tr->shifts, with the end of the list marked
       * by a shibboleth, 1, which is preset above.
       */
      png_uint_32 v = png_get_uint_16(sp);

      sp += 2;

      if (shift == 1U)
      {
         shift = tr->shifts;
         scale = tr->channel_scale;
      }

      *dp++ = PNG_BYTE(((v >> (shift & 0xFU)) * *scale++ + 0x800000U) >> 24);
      shift >>= 4;
   }

   affirm(sp == ep);
#  undef png_ptr
}

static int
add_scale(png_transform_scale_16_to_8 *tr, unsigned int sBIT, unsigned int ch)
{
   /* This is the output max (255) scaled by 2^24 divided by the input max'
    * (which is variable) and rounded.  It gives the exact 8-bit answer for all
    * input sBIT depths when used in the calculation:
    *
    *    output = ((input >> shift) * scale + 0x800000U) >> 24
    */
   tr->channel_scale[ch] = (0xFF000000U + ((1U<<sBIT)>>1)) / ((1U<<sBIT)-1U);
   tr->shifts |= ((16U-sBIT) & 0xFU) << (4U*ch);

   /* The result says whether there are 8 or fewer significant bits in the
    * input value; if so we can just drop the low byte.
    */
   return sBIT <= 8U;
}

static void
png_init_scale_16_to_8(png_transformp *transform, png_transform_controlp tc)
{
   if (tc->bit_depth == 16U)
   {
#     define png_ptr (tc->png_ptr)
      tc->bit_depth = 8U;
      /* But this invalidates tRNS (a 16-bit tRNS cannot be updated to match
       * 8-bit data correctly).
       */
      tc->invalid_info |= PNG_INFO_tRNS+PNG_INFO_hIST+PNG_INFO_pCAL;
      /* TODO: These need further processing: PNG_INFO_bKGD */

      if (tc->init == PNG_TC_INIT_FINAL)
      {
         png_transform_scale_16_to_8 *tr =
            png_transform_cast(png_transform_scale_16_to_8, *transform);

         /* Set the scale factors for each channel (up to 4), the factors are
          * made so that:
          *
          *     ((channel >> shift) * factor + 0x800000U) >> 24
          *
          * Gives the required 8-bit value.  The 'shift' is stored in a single
          * png_uint_32 with a shibboleth at the end.
          */
         unsigned int channels = 0U;
         int chop_ok = 1;

         tr->shifts = 0U;

         /* This adds up to four scale factors, the remainder are left as 0
          * which is safe and leads to obvious errors in the output images in
          * the event of an (internal) error.
          */
         if (tc->format & PNG_FORMAT_FLAG_COLOR)
            chop_ok &= add_scale(tr, tc->sBIT_R, channels++);

         chop_ok &= add_scale(tr, tc->sBIT_G, channels++);

         if (tc->format & PNG_FORMAT_FLAG_COLOR)
            chop_ok &= add_scale(tr, tc->sBIT_B, channels++);

         if (tc->format & PNG_FORMAT_FLAG_ALPHA)
            chop_ok &= add_scale(tr, tc->sBIT_A, channels++);

         if (chop_ok)
            tr->tr.fn = png_do_chop_16_to_8;

         else
         {
            int handled = 1;

            /* Add the shibboleth at the end */
            tr->shifts |= 1U << (4U*channels);
            tr->tr.fn = png_do_scale_16_to_8;

            /* sBIT is a little tricky; it has to be processed in the scaling
             * operation.  The result will have the same number of bits unless
             * there were more than 8 before.  The sBIT flags in the transform
             * control are left unchanged here because the data is still valid,
             * unless all the values end up as 8 in which case there is no
             * remaining sBIT info.
             *
             * Note that fields, such as alpha, which are not set for this row
             * format will always have max values, so won't reset 'handled':
             */
            if (tc->sBIT_R >= 8U) tc->sBIT_R = 8U; else handled = 0;
            if (tc->sBIT_G >= 8U) tc->sBIT_G = 8U; else handled = 0;
            if (tc->sBIT_B >= 8U) tc->sBIT_B = 8U; else handled = 0;
            if (tc->sBIT_A >= 8U) tc->sBIT_A = 8U; else handled = 0;

            /* If all the sBIT values were >= 8U all the bits are now
             * significant:
             */
            if (handled)
               tc->invalid_info |= PNG_INFO_sBIT;
         }
      }

#     undef png_ptr
   }

   else /* not applicable */
      (*transform)->fn = NULL;
}

void PNGAPI
png_set_scale_16(png_structrp png_ptr)
{
   if (png_ptr != NULL)
      png_add_transform(png_ptr, sizeof (png_transform_scale_16_to_8),
         png_init_scale_16_to_8, PNG_TR_SCALE_16_TO_8);
}
#endif /* READ_SCALE_16_TO_8 */

#ifdef PNG_READ_GAMMA_SUPPORTED
   /* Code that depends on READ_GAMMA support; RGB to gray convertion and
    * background composition (including the various alpha-mode handling
    * operations which produce pre-multiplied alpha by composing on 0).
    */
/* Calculate a reciprocal, return 0 on div-by-zero or overflow. */
static png_fixed_point
png_reciprocal(png_fixed_point a)
{
#ifdef PNG_FLOATING_ARITHMETIC_SUPPORTED
   double r = floor(1E10/a+.5);

   if (r <= 2147483647. && r >= -2147483648.)
      return (png_fixed_point)r;
#else
   png_fixed_point res;

   if (png_muldiv(&res, PNG_FP_1, PNG_FP_1, a) != 0)
      return res;
#endif

   return 0; /* error/overflow */
}

/* This is the shared test on whether a gamma value is 'significant' - whether
 * it is worth doing gamma correction.  'significant_bits' is the number of bits
 * in the values to be corrected which are significant.
 */
static int
png_gamma_significant(png_const_structrp png_ptr, png_fixed_point gamma_val,
   unsigned int sbits)
{
#if 0
   /* This seems to be wrong.  The issue is that when the app asks for a higher
    * bit depth output than the input has significant bits it causes gamma
    * correction to be skipped (this was the intent) however there's no
    * particular guarantee that the app won't go on to do further gamma
    * processing - pngstest does this - and this messes up the results
    * completely.
    *
    * TODO: work out how to optimize this correctly.
    */
   /* The following table lists the threshold as a difference from PNG_FP_1 at
    * which the gamma correction will make a change to at least an 'sbits'
    * value.  There is no entry for 1 bit values; gamma correction is never
    * significant.
    */
   static const png_uint_16 gamma_threshold_by_sbit[15][2] =
   {
      { 36907, 63092 }, /*  2 bits */
      { 17812, 21518 }, /*  3 bits */
      {  8675,  9496 }, /*  4 bits */
      {  4290,  4484 }, /*  5 bits */
      {  2134,  2181 }, /*  6 bits */
      {  1064,  1075 }, /*  7 bits */
      {   531,   534 }, /*  8 bits */
      {   265,   266 }, /*  9 bits */
      {   132,   132 }, /* 10 bits */
      {    66,    66 }, /* 11 bits */
      {    33,    33 }, /* 12 bits */
      {    16,    16 }, /* 13 bits */
      {     8,     8 }, /* 14 bits */
      {     4,     4 }, /* 15 bits */
      {     2,     2 }, /* 16 bits */
   };

   /* Handle out of range values in release by doing the gamma correction: */
   debug_handled(sbits > 0U && sbits <= 16U);
   if (sbits == 0U || sbits > 16U)
      return 1;

   /* 1 bit input or zero gamma, no correction possible/required: */
   if (gamma_val == 0 || sbits < 2U)
      return 0;

   if (gamma_val < PNG_FP_1 - gamma_threshold_by_sbit[sbits-2U][0U])
      return gamma_val < PNG_FP_1 - png_ptr->gamma_threshold;

   else if (gamma_val > PNG_FP_1 + gamma_threshold_by_sbit[sbits-2U][1U])
      return gamma_val > PNG_FP_1 + png_ptr->gamma_threshold;
#else /* FIXUP */
   if (gamma_val < PNG_FP_1)
      return gamma_val < PNG_FP_1 - png_ptr->gamma_threshold;

   else if (gamma_val > PNG_FP_1)
      return gamma_val > PNG_FP_1 + png_ptr->gamma_threshold;

   PNG_UNUSED(sbits)
#endif /* FIXUP */

   return 0; /* not significant */
}

static int
png_gamma_equal(png_const_structrp png_ptr, png_fixed_point g1,
   png_fixed_point g2, png_fixed_point *c, unsigned int sbits)
   /* Gamma values are equal, or at least one is unknown; c is the correction
    * factor from g1 to g2, i.e. g2/g1.
    */
{
   return sbits == 1U || g1 == 0 || g2 == 0 || g1 == g2 ||
      (png_muldiv(c, g2, PNG_FP_1, g1) &&
       !png_gamma_significant(png_ptr, *c, sbits));
}

#ifdef PNG_SIMPLIFIED_READ_SUPPORTED
int
png_need_gamma_correction(png_const_structrp png_ptr, png_fixed_point gamma,
   int sRGB_output)
   /* This is a hook for the simplified code; it just decides whether or not the
    * given gamma (which defaults to that of the PNG data) is close enough to
    * linear or sRGB not to require gamma correction.
    */
{
   if (gamma == 0)
      gamma = png_ptr->colorspace.gamma;

   if (gamma != 0 &&
       (png_ptr->colorspace.flags &
         (PNG_COLORSPACE_INVALID|PNG_COLORSPACE_HAVE_GAMMA)) ==
            PNG_COLORSPACE_HAVE_GAMMA)
   {

      if (sRGB_output && !png_muldiv(&gamma, gamma, PNG_GAMMA_sRGB, PNG_FP_1))
         return 0; /* overflow, so no correction */

      return png_gamma_significant(png_ptr, gamma, (png_ptr->color_type &
               PNG_COLOR_MASK_PALETTE) ? 8U : png_ptr->bit_depth);
   }

   return 0; /* no info, no correction */
}
#endif /* SIMPLIFIED_READ */

#ifndef PNG_FLOATING_ARITHMETIC_SUPPORTED
/* Fixed point gamma.
 *
 * The code to calculate the tables used below can be found in the shell script
 * contrib/tools/intgamma.sh
 *
 * To calculate gamma this code implements fast log() and exp() calls using only
 * fixed point arithmetic.  This code has sufficient precision for either 8-bit
 * or 16-bit sample values.
 *
 * The tables used here were calculated using simple 'bc' programs, but C double
 * precision floating point arithmetic would work fine.
 *
 * 8-bit log table
 *   This is a table of -log(value/255)/log(2) for 'value' in the range 128 to
 *   255, so it's the base 2 logarithm of a normalized 8-bit floating point
 *   mantissa.  The numbers are 32-bit fractions.
 */
static const png_uint_32
png_8bit_l2[128] =
{
   4270715492U, 4222494797U, 4174646467U, 4127164793U, 4080044201U, 4033279239U,
   3986864580U, 3940795015U, 3895065449U, 3849670902U, 3804606499U, 3759867474U,
   3715449162U, 3671346997U, 3627556511U, 3584073329U, 3540893168U, 3498011834U,
   3455425220U, 3413129301U, 3371120137U, 3329393864U, 3287946700U, 3246774933U,
   3205874930U, 3165243125U, 3124876025U, 3084770202U, 3044922296U, 3005329011U,
   2965987113U, 2926893432U, 2888044853U, 2849438323U, 2811070844U, 2772939474U,
   2735041326U, 2697373562U, 2659933400U, 2622718104U, 2585724991U, 2548951424U,
   2512394810U, 2476052606U, 2439922311U, 2404001468U, 2368287663U, 2332778523U,
   2297471715U, 2262364947U, 2227455964U, 2192742551U, 2158222529U, 2123893754U,
   2089754119U, 2055801552U, 2022034013U, 1988449497U, 1955046031U, 1921821672U,
   1888774511U, 1855902668U, 1823204291U, 1790677560U, 1758320682U, 1726131893U,
   1694109454U, 1662251657U, 1630556815U, 1599023271U, 1567649391U, 1536433567U,
   1505374214U, 1474469770U, 1443718700U, 1413119487U, 1382670639U, 1352370686U,
   1322218179U, 1292211689U, 1262349810U, 1232631153U, 1203054352U, 1173618059U,
   1144320946U, 1115161701U, 1086139034U, 1057251672U, 1028498358U, 999877854U,
   971388940U, 943030410U, 914801076U, 886699767U, 858725327U, 830876614U,
   803152505U, 775551890U, 748073672U, 720716771U, 693480120U, 666362667U,
   639363374U, 612481215U, 585715177U, 559064263U, 532527486U, 506103872U,
   479792461U, 453592303U, 427502463U, 401522014U, 375650043U, 349885648U,
   324227938U, 298676034U, 273229066U, 247886176U, 222646516U, 197509248U,
   172473545U, 147538590U, 122703574U, 97967701U, 73330182U, 48790236U,
   24347096U, 0U

#if 0 /* NOT USED */
   /* The following are the values for 16-bit tables - these work fine for the
    * 8-bit conversions but produce very slightly larger errors in the 16-bit
    * log (about 1.2 as opposed to 0.7 absolute error in the final value).  To
    * use these all the shifts below must be adjusted appropriately.
    */
   65166, 64430, 63700, 62976, 62257, 61543, 60835, 60132, 59434, 58741, 58054,
   57371, 56693, 56020, 55352, 54689, 54030, 53375, 52726, 52080, 51439, 50803,
   50170, 49542, 48918, 48298, 47682, 47070, 46462, 45858, 45257, 44661, 44068,
   43479, 42894, 42312, 41733, 41159, 40587, 40020, 39455, 38894, 38336, 37782,
   37230, 36682, 36137, 35595, 35057, 34521, 33988, 33459, 32932, 32408, 31887,
   31369, 30854, 30341, 29832, 29325, 28820, 28319, 27820, 27324, 26830, 26339,
   25850, 25364, 24880, 24399, 23920, 23444, 22970, 22499, 22029, 21562, 21098,
   20636, 20175, 19718, 19262, 18808, 18357, 17908, 17461, 17016, 16573, 16132,
   15694, 15257, 14822, 14390, 13959, 13530, 13103, 12678, 12255, 11834, 11415,
   10997, 10582, 10168, 9756, 9346, 8937, 8531, 8126, 7723, 7321, 6921, 6523,
   6127, 5732, 5339, 4947, 4557, 4169, 3782, 3397, 3014, 2632, 2251, 1872, 1495,
   1119, 744, 372
#endif
};

#if 0 /* UNUSED */
static png_int_32
png_log8bit(unsigned int x)
{
   png_uint_32 lg2 = 0U;

   /* Each time 'x' is multiplied by 2, 1 must be subtracted off the final log,
    * because the log is actually negate that means adding 1.  The final
    * returned value thus has the range 0 (for 255 input) to 7.994 (for 1
    * input), return -1 for the overflow (log 0) case, - so the result is
    * always at most 19 bits.
    */
   if ((x &= 0xffU) == 0U) /* 0 input, -inf output */
      return -0xfffff;

   if ((x & 0xf0U) == 0U)
      lg2  = 4U, x <<= 4;

   if ((x & 0xc0U) == 0U)
      lg2 += 2U, x <<= 2;

   if ((x & 0x80U) == 0U)
      lg2 += 1U, x <<= 1;

   /* result is at most 19 bits, so this cast is safe: */
   return (png_int_32)((lg2 << 16) + ((png_8bit_l2[x-128U]+32768U)>>16));
}
#endif /* UNUSED */

/* The above gives exact (to 16 binary places) log2 values for 8-bit images,
 * for 16-bit images we use the most significant 8 bits of the 16-bit value to
 * get an approximation then multiply the approximation by a correction factor
 * determined by the remaining up to 8 bits.  This requires an additional step
 * in the 16-bit case.
 *
 * We want log2(value/65535), we have log2(v'/255), where:
 *
 *    value = v' * 256 + v''
 *          = v' * f
 *
 * So f is value/v', which is equal to (256+v''/v') since v' is in the range 128
 * to 255 and v'' is in the range 0 to 255 f will be in the range 256 to less
 * than 258.  The final factor also needs to correct for the fact that our 8-bit
 * value is scaled by 255, whereas the 16-bit values must be scaled by 65535.
 *
 * This gives a final formula using a calculated value 'x' which is value/v' and
 * scaling by 65536 to match the above table:
 *
 *   log2(x/257) * 65536
 *
 * Since these numbers are so close to '1' we can use simple linear
 * interpolation between the two end values 256/257 (result -368.61) and 258/257
 * (result 367.179).  The values used below are scaled by a further 64 to give
 * 16-bit precision in the interpolation:
 *
 * Start (256): -23591
 * Zero  (257):      0
 * End   (258):  23499
 *
 * In libpng 1.7.0 this is further generalized to return -log2(value/maxval) for
 * any maxval up to 65535.  This is done by evaluating -log2(value/65535) first
 * then adjusting for the required maxval:
 *
 *         ( value)        (value    65535)        (value)     ( 65535)
 *    -log2(------) = -log2(----- x ------) = -log2(-----)-log2(------)
 *         (maxval)        (65535   maxval)        (65535)     (maxval)
 *
 * The extra argument, 'factor', is (2^(16+12))*log2(65535/maxval) (a positive
 * value less than 2^32) and this is *subtracted* from the intermediate
 * calculation below.
 */
static png_int_32
png_log(unsigned int x, png_uint_32 factor)
   /* x: a value of up to 16 bits,
    * factor: a 4.28 number which is subtracted from the log below
    */
{
   png_uint_32 lg2 = 0U;

   /* As above, but now the input has 16 bits. */
   if ((x &= 0xffffU) == 0U)
      return -0xfffff;

   if ((x & 0xff00U) == 0U)
      lg2  = 8U, x <<= 8;

   if ((x & 0xf000U) == 0U)
      lg2 += 4U, x <<= 4;

   if ((x & 0xc000U) == 0U)
      lg2 += 2U, x <<= 2;

   if ((x & 0x8000U) == 0U)
      lg2 += 1U, x <<= 1;

   /* Calculate the base logarithm from the top 8 bits as a 28-bit fractional
    * value.
    */
   lg2 <<= 28;
   lg2 += (png_8bit_l2[(x>>8)-128U]+8U) >> 4;

   /* Now we need to interpolate the factor, this requires a division by the top
    * 8 bits.  Do this with maximum precision.
    */
   {
      png_uint_32 i = x;

      i = ((i << 16) + (i >> 9)) / (x>> 8);

      /* Since we divided by the top 8 bits of 'x' there will be a '1' at 1<<24,
       * the value at 1<<16 (ignoring this) will be 0 or 1; this gives us
       * exactly 16 bits to interpolate to get the low bits of the result.
       * Round the answer.  Note that the end point values are scaled by 64 to
       * retain overall precision and that 'lg2' is current scaled by an extra
       * 12 bits, so adjust the overall scaling by 6-12.  Round at every step.
       */
      i -= 1U << 24;

      if (i <= 65536U) /* <= '257' */
         lg2 += ((23591U * (65536U-i)) + (1U << (16+6-12-1))) >> (16+6-12);

      else
         lg2 -= ((23499U * (i-65536U)) + (1U << (16+6-12-1))) >> (16+6-12);
   }

   if (lg2 >= factor)
      return (png_int_32)/*SAFE*/((lg2 - factor + 2048U) >> 12);

   else /* the result will be greater than 1.0, so negative: */
      return -(png_int_32)/*SAFE*/((factor - lg2 + 2048U) >> 12);
}

#if 0 /* UNUSED */
static png_int_32
png_log16bit(unsigned int x)
{
   return png_log(x, 0U);
}
#endif /* UNUSED */

/* libpng 1.7.0: generalization of png_log{8,16}bit to accept an n-bit input
 * value.  We want to maintain 1% accuracy in linear light space.  This
 * corresponds to, approximately, (1*g)% in a gamma encoded space where the
 * gamma encoding is 'g' (in the PNG sense, e.g. 0.45455 for sRGB).  Apparently
 * this requires unbounded accuracy as the gamma encoding value goes down and
 * this is a problem for modern HDR data because it may require a high gamma to
 * accurately encode image data over a wide dynamic range; the dynamic range of
 * 16-bit linear data is only 655:1 if 1% accuracy is needed!
 *
 * However 16-bit gamma encoded data is still limited because PNG can only
 * express gamma encoding.  (A log-to-base-1.01 encoding is unlimited; a 12-bit
 * value, with 4094 steps, has a dynamic range of more than 1:10^17, which
 * exceeds the human eye's range of 1:10^14.)
 *
 * Notice that sRGB uses a 1/2.4 encoding and CIELab uses a 1/3 encoding.  It is
 * obvious that, if we assume a maximum D difference in the luminance of
 * adjacent pixel values the dynamic range is given by the lowest pixel value
 * which is D or less greater than its predecessor, so:
 *
 *   ( P ) (1)
 *   (---)^(-) = D
 *   (P-1) (g)
 *
 * and the maximum dynamic range that can be achieved using M+1 separate values,
 * where M+1 is 2^N-1 for an N bit value, reserving the first value for 0, is:
 *
 *              (M) (1)
 *   range(R) = (-)^(-)
 *              (P) (g)
 *
 * So we can eliminate 'P' from the two equations:
 *
 *   P = (P-1) x (D^g)
 *
 *        D^g
 *   P = -----
 *       D^g-1
 *
 *       (M x (D^g-1)) (1)
 *   R = (-----------)^(-)
 *       (    D^g    ) (g)
 *
 *       (M x (D^g-1)) ^ (1/g)
 *     = ---------------------
 *                D
 *
 * Which is a function in two variables (R and g) for a given D (maximum delta
 * between two adjacent pixel values) and M (number of pixel values, controlled
 * by the channel bit depth).
 *
 * See contrib/tools/dynamic-range.c for code exploring this function.  This
 * program will output the optimal gamma for a given number of bits and
 * precision.
 *
 * The range of sensitivity of human vision is roughly as follows (this comes
 * from the wikipedia article on scotopic vision):
 *
 *     scotopic: 10^-6 to 10^-3.5 cd/m^2
 *     mesopic:  10^-3 to 10^0.5 cd/m^2
 *     photopic: 10 to 10^8 cd/m^2
 *
 * Giving a total range of about 1:10^14.  The maximum precision at which this
 * range can be achieved using 16-bit channels is about .15% using a gamma of
 * 36, higher ranges are possible using higher gammas but precision is reduced.
 * The range with 1% precision and 16-bit channels is 1:10^104, using a gamma of
 * 240.
 *
 * In general the optimal gamma for n-bit channels (where 'n' is at least 7 and
 * precision is .01 or less) is:
 *
 *                  2^n * precision
 *          gamma = ---------------
 *                       2.736
 *
 * Or: (24000 * precision) for 16-bit data.
 *
 * The net effect is that we can't rely on the encoding gamma being limited to
 * values around 1/2.5!
 */
static png_int_32
png_log_nbit(unsigned int x, unsigned int nbits)
{
   static const png_uint_32 factors[16] =
   {
      4294961387U, /* 1 bit */
      3869501255U, /* 2 bit */
      3541367788U, /* 3 bit */
      3246213428U, /* 4 bit */
      2965079441U, /* 5 bit */
      2690447525U, /* 6 bit */
      2418950626U, /* 7 bit */
      2148993476U, /* 8 bit */
      1879799410U, /* 9 bit */
      1610985205U, /* 10 bit */
      1342360514U, /* 11 bit */
      1073830475U, /* 12 bit */
       805347736U, /* 13 bit */
       536888641U, /* 14 bit */
       268441365U, /* 15 bit */
               0U  /* 16 bit */
   };

   return png_log(x, factors[nbits-1]);
}


/* The 'exp()' case must invert the above, taking a 20-bit fixed point
 * logarithmic value and returning a 16 or 8-bit number as appropriate.  In
 * each case only the low 16 bits are relevant - the fraction - since the
 * integer bits (the top 4) simply determine a shift.
 *
 * The worst case is the 16-bit distinction between 65535 and 65534. This
 * requires perhaps spurious accuracy in the decoding of the logarithm to
 * distinguish log2(65535/65534.5) - 10^-5 or 17 bits.  There is little chance
 * of needing this accuracy in practice.
 *
 * To deal with this the following exp() function works out the exponent of the
 * frational part of the logarithm by using an accurate 32-bit value from the
 * top four fractional bits then multiplying in the remaining bits.
 */
static const png_uint_32
png_32bit_exp[16] =
{
   /* NOTE: the first entry is deliberately set to the maximum 32-bit value. */
   4294967295U, 4112874773U, 3938502376U, 3771522796U, 3611622603U, 3458501653U,
   3311872529U, 3171459999U, 3037000500U, 2908241642U, 2784941738U, 2666869345U,
   2553802834U, 2445529972U, 2341847524U, 2242560872U
};

/* Adjustment table; provided to explain the numbers in the code below. */
#if 0 /* BC CODE */
for (i=11;i>=0;--i){ print i, " ", (1 - e(-(2^i)/65536*l(2))) * 2^(32-i), "\n"}
   11 44937.64284865548751208448
   10 45180.98734845585101160448
    9 45303.31936980687359311872
    8 45364.65110595323018870784
    7 45395.35850361789624614912
    6 45410.72259715102037508096
    5 45418.40724413220722311168
    4 45422.25021786898173001728
    3 45424.17186732298419044352
    2 45425.13273269940811464704
    1 45425.61317555035558641664
    0 45425.85339951654943850496
#endif

static png_uint_32
png_exp(png_int_32 x)
   /* Utility, the value 'x' must be in the range 0..0x1fffff */
{
   /* Obtain a 4-bit approximation */
   png_uint_32 e = png_32bit_exp[(x >> 12) & 0xf];

   /* Incorporate the low 12 bits - these decrease the returned value by
    * multiplying by a number less than 1 if the bit is set.  The multiplier
    * is determined by the above table and the shift. Notice that the values
    * converge on 45426 and this is used to allow linear interpolation of the
    * low bits.
    */
   if (x & 0x800)
      e -= (((e >> 16) * 44938U) +  16U) >> 5;

   if (x & 0x400)
      e -= (((e >> 16) * 45181U) +  32U) >> 6;

   if (x & 0x200)
      e -= (((e >> 16) * 45303U) +  64U) >> 7;

   if (x & 0x100)
      e -= (((e >> 16) * 45365U) + 128U) >> 8;

   if (x & 0x080)
      e -= (((e >> 16) * 45395U) + 256U) >> 9;

   if (x & 0x040)
      e -= (((e >> 16) * 45410U) + 512U) >> 10;

   /* And handle the low 6 bits in a single block. */
   e -= (((e >> 16) * 355U * (x & 0x3fU)) + 256U) >> 9;

   /* Handle the upper bits of x, note that this works for x up to 0x1fffff but
    * fails for larger or negative x, where the shift (x >> 16) exceeds 31:
    */
   e >>= x >> 16;
   return e;
}

#if 0 /* UNUSED */
static png_byte
png_exp8bit(png_int_32 lg2)
{
   /* The input is a negative fixed point (16:16) logarithm with a useable range
    * of [0.0..8.0).  Clamp the value so that the output of png_exp is in the
    * range (254.5/255..0.5/255):
    */
   if (lg2 <= 185) /* -log2(254.5/255) */
      return 255U;

   else if (lg2 > 589453) /* -log2(0.5/255) */
      return 0U;

   else
   {
      /* Get a 32-bit value: */
      png_uint_32 x = png_exp(lg2);

      /* Convert the 32-bit value to 0..255 by multiplying by 256-1. Note that
       * the second, rounding, step can't overflow because of the first,
       * subtraction, step.
       */
      x -= x >> 8;
      return PNG_BYTE((x + 0x7fffffU) >> 24);
   }
}

static png_uint_16
png_exp16bit(png_int_32 lg2)
{
   if (lg2 <= 0) /* -log2(65534.5/65535) */
      return 65535U;

   else if (lg2 > 1114110) /* -log2(0.5/65535) */
      return 0U;

   else
   {
      /* Get a 32-bit value: */
      png_uint_32 x = png_exp(lg2);

      /* Convert the 32-bit value to 0..65535 by multiplying by 65536-1: */
      x -= x >> 16;
      return PNG_UINT_16((x + 32767U) >> 16);
   }
}
#endif /* UNUSED */

static png_uint_32
png_exp_nbit(png_int_32 lg2, unsigned int n)
{
   /* These pre-computed limits give the low value of lg2 at and below which
    * 2^(-lg2/65536) * (2^n-1) gives (2^n-1) and the high value of lg2 above
    * which 2(^-lg2/65536) * (2^n-1) gives 0:
    */
   static const png_int_32 limits[16][2] =
   {
      { 65535,   65535 }, /* bits =  1 */
      { 17238,  169408 }, /* bits =  2 */
      {  7006,  249518 }, /* bits =  3 */
      {  3205,  321577 }, /* bits =  4 */
      {  1537,  390214 }, /* bits =  5 */
      {   753,  457263 }, /* bits =  6 */
      {   372,  523546 }, /* bits =  7 */
      {   185,  589453 }, /* bits =  8 */
      {    92,  655175 }, /* bits =  9 */
      {    46,  720803 }, /* bits = 10 */
      {    23,  786385 }, /* bits = 11 */
      {    11,  851944 }, /* bits = 12 */
      {     5,  917492 }, /* bits = 13 */
      {     2,  983034 }, /* bits = 14 */
      {     1, 1048573 }, /* bits = 15 */
      {     0, 1114110 }  /* bits = 16 */
   };

   /* If 'max' is 2^n-1: */
   if (lg2 <= limits[n-1][0]) /* -log2((max-.5)/max) */
      return (1U << n)-1U;

   else if (lg2 > limits[n-1][1]) /* -log2(.5/max) */
      return 0U;

   else /* 'n' will be at least 2 */
   {
      /* Get a 32-bit value: */
      png_uint_32 x = png_exp(lg2);

      /* Convert the 32-bit value to 0..(2^n-1) by multiplying by 2^n-1: */
      x -= x >> n;
      return (x + ((1U<<(31U-n))-1U)) >> (32U-n);
   }
}
#endif /* !FLOATING_ARITHMETIC */

#if 0 /* UNUSED */
static png_byte
png_gamma_8bit_correct(unsigned int value, png_fixed_point gamma_val)
{
   if (value == 0U)
      return 0U;

   else if (value >= 255U)
      return 255U;

   else
   {
#     ifdef PNG_FLOATING_ARITHMETIC_SUPPORTED
         /* 'value' is unsigned, ANSI-C90 requires the compiler to correctly
          * convert this to a floating point value.  This includes values that
          * would overflow if 'value' were to be converted to 'int'.
          *
          * Apparently GCC, however, does an intermediate conversion to (int)
          * on some (ARM) but not all (x86) platforms, possibly because of
          * hardware FP limitations.  (E.g. if the hardware conversion always
          * assumes the integer register contains a signed value.)  This results
          * in ANSI-C undefined behavior for large values.
          *
          * Other implementations on the same machine might actually be ANSI-C90
          * conformant and therefore compile spurious extra code for the large
          * values.
          *
          * We can be reasonably sure that an unsigned to float conversion
          * won't be faster than an int to float one.  Therefore this code
          * assumes responsibility for the undefined behavior, which it knows
          * can't happen because of the check above.
          *
          * Note the argument to this routine is an (unsigned int) because, on
          * 16-bit platforms, it is assigned a value which might be out of
          * range for an (int); that would result in undefined behavior in the
          * caller if the *argument* ('value') were to be declared (int).
          */
         double r = 255*pow((int)/*SAFE*/value/255.,gamma_val*.00001);
         if (r < .5)
            return 0U;

         else if (r >= 254.5)
            return 255U;

         r = floor(r+.5);
         return (png_byte)/*SAFE*/r;
#     else
         png_int_32 lg2 = png_log8bit(value);
         png_int_32 res;

         /* Overflow in the muldiv means underflow in the calculation, this is
          * OK (it happens for ridiculously high gamma).
          */
         if (!png_muldiv(&res, gamma_val, lg2, PNG_FP_1))
            return 0U; /* underflow */

         return png_exp8bit(res);
#     endif
   }
}
#endif /* UNUSED */

/* libpng-1.7.0: this private function converts an n-bit input value to an
 * m-bit output value.
 */
unsigned int
png_gamma_nxmbit_correct(unsigned int value, png_fixed_point gamma_val,
   unsigned int n/*input bits*/, unsigned int m/*output bits */)
{
   if (value == 0U)
      return 0U;

   else
   {
      unsigned int min  = (1U<<n) - 1U;
      unsigned int mout = (1U<<m) - 1U;

      if (value >= min)
         return mout;

      else
      {
#        ifdef PNG_FLOATING_ARITHMETIC_SUPPORTED
            double r = value;
            r /= min;
            r = floor(mout * pow(r, gamma_val*.00001)+.5);
            if (r < 1)
               return 0U;

            else if (r >= mout)
               return mout;

            return (unsigned int)/*SAFE*/r;
#        else
            png_int_32 lg2 = png_log_nbit(value, n);
            png_int_32 res;

            if (!png_muldiv(&res, gamma_val, lg2, PNG_FP_1))
               return 0U; /* underflow */

            return png_exp_nbit(res, m);
#        endif
      }
   }
}

#if 0 /*UNUSED*/
static unsigned int
png_gamma_sbit_correct(unsigned int value, png_fixed_point gamma_val,
   unsigned int n/*input bits*/, unsigned int sbits,
   unsigned int m/*output bits */)
   /* As above but the number of significant bits in 'n' is passed in. */
{
   if (sbits < n)
   {
      value >>= (n-sbits);
      n = sbits;
   }

   return png_gamma_nxmbit_correct(value, gamma_val, n, m);
}
#endif /*UNUSED*/

static int
push_gamma_expand(png_transformp *transform, png_transform_controlp tc,
      int need_alpha)
   /* Utility to push a transform to expand low-bit-depth gray and, where
    * required, tRNS chunks.  The caller must return immediately if this
    * returns true because the init of the new transform has been run in place
    * of the caller's.
    */
{
#  define png_ptr (tc->png_ptr)
   unsigned int expand = 0;

   affirm(tc->init == PNG_TC_INIT_FINAL);

   if (tc->bit_depth < 8U) /* low bit gray: expand to 8 bits */
      expand = PNG_EXPAND_LBD_GRAY;

   /* Gamma correction invalidates tRNS, so if it is being expanded and
    * alpha is not being stripped expand it now.
    */
   if ((tc->format & PNG_FORMAT_FLAG_ALPHA) == 0 && !tc->palette &&
       png_ptr->num_trans == 1 && (tc->invalid_info & PNG_INFO_tRNS) == 0)
   {
      if (need_alpha || (tc->expand_tRNS && !tc->strip_alpha))
         expand |= PNG_EXPAND_tRNS;

      else
         tc->invalid_info |= PNG_INFO_tRNS;
   }

   if (expand == 0)
      return 0; /* nothing needs to be done */

   {
      png_transformp tr = png_push_transform(png_ptr, sizeof (png_expand),
         png_init_expand, transform, NULL/*don't run init*/);

      debug(tr == *transform);
      tr->args |= expand;

      /* This must be run immediately, because it just got inserted where this
       * transform is; this is safe, the caller must return immediately.
       */
      png_init_expand(transform, tc);
      affirm(tr->fn != NULL); /* because it should need to do something! */
   }

   return 1;
#  undef png_ptr
}

/* Low bit depth gray gamma correction.  The 1-bit case is a no-op because 0 and
 * 1 always map to 0 and 1.  The 2-bit case has the following possiblities:
 *
 *   bits/correction: g0 g1 g2 g3 g4 g5 g6
 *    00      ->      00 00 00 00 00 00 00
 *    01      ->      11 10 10 01 00 00 00
 *    10      ->      11 11 10 10 10 01 00
 *    11      ->      11 11 11 11 11 11 11
 *
 * Where the breakpoints are:
 *
 *    g0:          correction <=  16595 (1 - log(2.5/3))
 *    g1:  16595 < correction <=  44966 (log(2.5/3)/log(2/3))
 *    g2:  44966 < correction <=  63092 (1 - log(1.5/3))
 *    g3:  63092 < correction <= 163092 (1 - log(.5/3))
 *    g4: 163092 < correction <= 170951 (log(1.5/3)/log(2/3))
 *    g5: 170951 < correction <= 441902 (log(.5/3)/log(2/3)
 *    g6  441902 < correction
 *
 * This can be done by bit-hacking on the byte values (4 pixels), given that
 * the correction is fixed (indeed, it can be done on whole 32-bit values!)
 *
 *    g0: B |= B>>1; B &= 0x55U; B |= B<<1;  * either bit set
 *    g1: B ^= B>>1; B &= 0x55U; B += B;     * one bit set
 *    g2: B &= (~B)>>1; B &= 0x55U; B += B;  * low bit set, high bit unset
 *    g3: no-op
 *    g4: B &= (~B)>>1; B &= 0x55U; B -= B;  * low bit set, high bit unset
 *    g5: B ^= B>>1; B &= 0x55U; B -= B;     * one bit set
 *    g6: B &= B>>1; B &= 0x55U; B |= B<<1;  * both bits set
 */
typedef struct
{
   png_transform   tr;
   png_fixed_point correct;
   png_fixed_point to_gamma;
   png_uint_32     shifts;           /* 1 followed by up to 4 4-bit shifts */
   png_uint_32     channel_scale[4]; /* up to 4 channel scale factors */
   /* These factors are used:
    *
    *     (input >> (shifts & 0xFU) * channel_scale + SCALE_R) >> SCALE_S
    *
    * Where the rounding value, SCALE_R and the shift SCALE_S are dependent
    * on the bit depth:
    *
    *    SCALE_S = 32 - bit_depth     range 16..31
    *    SCALE_R = 1 << (SCALE_S-1)
    */
   unsigned int    to_bit_depth;
   unsigned int    encode_alpha :1;
   unsigned int    optimize_alpha :1;
} png_transform_gamma;

static unsigned int
init_gamma_sBIT(png_transform_gamma *tr, png_transform_controlp tc)
   /* Returns true if sBIT processing is required, otherwise all relevant sBIT
    * values match the from (tc) bit depth.
    */
{
   /* The to_bit_depth and to_gamma fields are already set, but updated values
    * are needed for sBIT and the shifts and channel_scale fields must be filled
    * in correctly.  The do_gamma setting says whether gamma correction will be
    * done, but the scale factors are filled in regardless.
    *
    * The general scaling equation is:
    *
    *    ((in >> shift) * factor + round) >> (32 - to_bit_depth)
    *
    * 'factor' is then the rounded value of:
    *
    *      out_max
    *      ------- . (1 << (32-to_bit_depth))
    *       in_max
    */
#  define png_ptr (tc->png_ptr)
   const unsigned int to_bit_depth = tr->to_bit_depth;
   const png_uint_32 numerator = ((1U<<to_bit_depth)-1U) << (32U-to_bit_depth);
   /* in_max depends on the number of significant bits */
   const unsigned int from_bit_depth = tc->bit_depth;

   /* The data in the gamma transform is stored in the order of the channels in
    * the input row, which is the PNG order.  It may be reversed below.
    */
   png_uint_32p channel_scale = tr->channel_scale;
   png_uint_32 shifts = 0U;
   unsigned int count = 0U;
   unsigned int need_sBIT = 0U;

   if (tc->format & PNG_FORMAT_FLAG_COLOR)
   {
      const unsigned int sBIT = tc->sBIT_R;

      if (sBIT < from_bit_depth)
         need_sBIT = 1U;

      debug(sBIT > 0U && sBIT <= from_bit_depth);
      shifts |= (from_bit_depth - sBIT) << count;
      count += 4U;
      /* round the scale: */
      *channel_scale++ = (numerator + (1U<<(sBIT-1U))) / ((1U << sBIT)-1U);
   }

   {
      const unsigned int sBIT = tc->sBIT_G;

      if (sBIT < from_bit_depth)
         need_sBIT = 1U;

      debug(sBIT > 0U && sBIT <= from_bit_depth);
      shifts |= (from_bit_depth - sBIT) << count;
      count += 4U;
      *channel_scale++ = (numerator + (1U<<(sBIT-1U))) / ((1U << sBIT)-1U);
   }

   if (tc->format & PNG_FORMAT_FLAG_COLOR)
   {
      const unsigned int sBIT = tc->sBIT_B;

      if (sBIT < from_bit_depth)
         need_sBIT = 1U;

      debug(sBIT > 0U && sBIT <= from_bit_depth);
      shifts |= (from_bit_depth - sBIT) << count;
      count += 4U;
      /* round the scale: */
      *channel_scale++ = (numerator + (1U<<(sBIT-1U))) / ((1U << sBIT)-1U);
   }

   if (tc->format & PNG_FORMAT_FLAG_ALPHA)
   {
      const unsigned int sBIT = tc->sBIT_A;

      if (sBIT < from_bit_depth)
         need_sBIT = 1U;

      debug(sBIT > 0U && sBIT <= from_bit_depth);
      shifts |= (from_bit_depth - sBIT) << count;
      count += 4U;
      /* round the scale: */
      *channel_scale++ = (numerator + (1U<<(sBIT-1U))) / ((1U << sBIT)-1U);
   }

   tr->shifts = shifts | (1U << count);

   return need_sBIT;
#  undef png_ptr
}

static void
reverse_gamma_sBIT(png_transform_gamma *tr)
{
   /* This is called for the 'down' gamma implementations, they read the shifts
    * and the channel scales in reverse, so:
    */
   png_uint_32 shifts = tr->shifts;
   png_uint_32 scales[4U];
   unsigned int count = 0U;

   tr->shifts = 1U;

   while (shifts != 1U)
   {
      scales[3U-count] = tr->channel_scale[count];
      ++count;
      tr->shifts <<= 4;
      tr->shifts |= shifts & 0xFU;
      shifts >>= 4;
   }

   memcpy(tr->channel_scale, scales+(4U-count), count * sizeof (png_uint_32));
}

static void
png_do_gamma8_up(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_const_bytep ep = sp + PNG_TC_ROWBYTES(*tc);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_transform_gamma *tr =
      png_transform_cast(png_transform_gamma, *transform);
   const png_fixed_point correct = tr->correct;
   const unsigned int bit_depth = tr->to_bit_depth;
   const png_uint_32 shifts = tr->shifts;

   affirm(tc->bit_depth == 8U);
   affirm(tr->shifts != 0U/*uninitialized*/);
   debug((shifts & 0x8888U) == 0U); /* all shifts 7 or less */
   debug(!tr->encode_alpha && !tr->optimize_alpha); /* only set for 16 bits */

   tc->sp = dp;
   tc->bit_depth = bit_depth;
   tc->gamma = tr->to_gamma;

   /* Handle the <8 bit output case differently because there can be no alpha
    * channel.
    */
   if (bit_depth < 8U)
   {
      const unsigned int shift = shifts & 0xFU;
      unsigned int bits = 8U;
      unsigned int ob = 0U;

      debug((shifts >> 4) == 1U && shift < 8U);
      affirm(PNG_TC_CHANNELS(*tc) == 1);

      do
      {
         const unsigned int inb = png_gamma_nxmbit_correct(
            *sp++ >> shift, correct, 8U-shift, bit_depth);
         bits -= bit_depth;
         ob = ob | (inb << bits);
         if (bits == 0U)
            bits = 8U, *dp++ = PNG_BYTE(ob), ob = 0U;
      }
      while (sp < ep);

      if (bits < 8U)
         *dp++ = PNG_BYTE(ob);
   }

   else /* 8-bit --> 8-bit */
   {
      png_uint_32 alpha_scale;
      const unsigned int channels = PNG_TC_CHANNELS(*tc);
      unsigned int channel, alpha;

      debug(bit_depth == 8U && (shifts >> (4*channels)) == 1U);

      /* The alpha channel is always last, so if present checking against the
       * top bits of 'channels' works because of the 1U shibboleth at the end.
       */
      if ((tc->format & PNG_FORMAT_FLAG_ALPHA) == 0)
         alpha_scale = alpha = 0U;

      else
      {
         alpha = shifts >> (4U*(channels-1U));
         alpha_scale = tr->channel_scale[channels-1U];
      }

      channel = 1U;

      do
      {
         unsigned int inb = *sp++, shift;

         if (channel == 1U)
            channel = shifts;

         shift = channel & 0xFU;
         inb >>= shift;

         /* The alpha channel is not gamma encoded but it may need some
          * appropriate scaling.
          */
         if (channel == alpha)
            inb = (inb * alpha_scale + 0x800000U) >> 24;

         else
            inb = png_gamma_nxmbit_correct(inb, correct, 8U-shift, 8U);

         channel >>= 4; /* for the next channel, or the shibboleth */
         *dp++ = PNG_BYTE(inb);
      }
      while (sp < ep);

      debug(channel == 1U);
   }
#  undef png_ptr
}

static void
png_do_gamma16_up(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_const_bytep ep = sp + PNG_TC_ROWBYTES(*tc) - 1U/*safety*/;
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_transform_gamma *tr =
      png_transform_cast(png_transform_gamma, *transform);
   const png_fixed_point correct = tr->correct;
   const unsigned int bit_depth = tr->to_bit_depth;
   const png_uint_32 shifts = tr->shifts;

   affirm(tc->bit_depth == 16U);
   affirm(tr->shifts != 0U/*uninitialized*/);
   debug(!tr->optimize_alpha);

   /* This is exactly the same as above but the input has 16 bits per component,
    * not 8.
    */
   tc->sp = dp;
   tc->bit_depth = bit_depth;
   tc->gamma = tr->to_gamma;

   /* Handle the <8 bit output case differently, the input cannot be color (at
    * present) and, if there is an alpha channel, then it is for the
    * low-bit-depth gray input case and we expect the alpha to be transparent.
    */
   if (bit_depth < 8U)
   {
      const unsigned int shift = shifts & 0xFU;
      unsigned int bits = 8U;
      unsigned int ob = 0U;

      affirm((tc->format & PNG_FORMAT_FLAG_COLOR) == 0U);

      if ((tc->format & PNG_FORMAT_FLAG_ALPHA) == 0U)
      {
         debug((shifts >> 4) == 1U && shift < 16U);
         debug(!tr->encode_alpha && !tr->optimize_alpha);

         do
         {
            unsigned int inb = *sp++ << 8; /* high bits first */
            inb = png_gamma_nxmbit_correct(
               (inb + *sp++) >> shift, correct, 16U-shift, bit_depth);

            bits -= bit_depth;
            ob = ob | (inb << bits);
            if (bits == 0U)
               bits = 8U, *dp++ = PNG_BYTE(ob), ob = 0U;
         }
         while (sp < ep);

         UNTESTED
      }

      else /* low bit GA intermediate format */
      {
         debug((shifts >> 8) == 1U && shift < 16U);
         debug(!tr->encode_alpha && !tr->optimize_alpha);
         debug(tc->transparent_alpha);

         /* Gray is first then the alpha component, the alpha component is just
          * mapped to 0 or 1.
          */
         do
         {
            unsigned int gray = *sp++ << 8; /* high bits first */
            unsigned int alpha;
            gray += *sp++;

            alpha = (*sp++ << 8);
            alpha += *sp++;

            if (alpha == 0U)
               gray = 0U; /* will be replaced later */

            else
            {
               gray = png_gamma_nxmbit_correct(gray >> shift, correct,
                     16U-shift, bit_depth);
               debug(alpha == 65535U);
               alpha = (1U << bit_depth)-1U;
            }

            bits -= bit_depth;
            ob = ob | (gray << bits);
            bits -= bit_depth;
            ob = ob | (alpha << bits);

            if (bits == 0U)
               bits = 8U, *dp++ = PNG_BYTE(ob), ob = 0U;
         }
         while (sp < ep-2U);
      }

      if (bits < 8U)
         *dp++ = PNG_BYTE(ob);

      debug(sp == ep+1U);
   }

   else
   {
      png_uint_32 alpha_scale;
      const unsigned int channels = PNG_TC_CHANNELS(*tc);
      unsigned int channel, alpha;

      debug((bit_depth == 8U || bit_depth == 16U) &&
            (shifts >> (4*channels)) == 1U);

      /* Note that 'encode_alpha' turns on gamma encoding of the alpha
       * channel (and this is a really weird operation!)
       */
      if ((tc->format & PNG_FORMAT_FLAG_ALPHA) == 0 || tr->encode_alpha)
         alpha_scale = alpha = 0U;

      else
      {
         alpha = shifts >> (4U*(channels-1U));
         alpha_scale = tr->channel_scale[channels-1U];
      }

      channel = 1U;

      if (bit_depth == 16U)
      {
         do
         {
            unsigned int inb = *sp++ << 8, shift;
            inb += *sp++;

            if (channel == 1U)
               channel = shifts;

            shift = channel & 0xFU;
            inb >>= shift;

            /* The 16-16bit scaling factor equation may be off-by-1 but this
             * hardly matters for alpha or for gamma operations.
             */
            if (channel == alpha)
               inb = (inb * alpha_scale + 0x8000U) >> 16;

            else
               inb = png_gamma_nxmbit_correct(inb, correct, 16U-shift, 16U);

            channel >>= 4; /* for the next channel, or the shibboleth */
            *dp++ = PNG_BYTE(inb >> 8);
            *dp++ = PNG_BYTE(inb);
         }
         while (sp < ep);

         debug(channel == 1U && sp == ep+1U);
      }

      else /* bit_depth == 8U */
      {
         do
         {
            unsigned int inb = *sp++ << 8, shift;
            inb += *sp++;

            if (channel == 1U)
               channel = shifts;

            shift = channel & 0xFU;
            inb >>= shift;

            if (channel == alpha)
               inb = (inb * alpha_scale + 0x800000U) >> 24;

            else
               inb = png_gamma_nxmbit_correct(inb, correct, 16U-shift, 8U);

            channel >>= 4; /* for the next channel, or the shibboleth */
            *dp++ = PNG_BYTE(inb);
         }
         while (sp < ep);

         debug(channel == 1U && sp == ep+1U);
      }
   }
#  undef png_ptr
}

#ifdef PNG_READ_ALPHA_MODE_SUPPORTED
static void
png_do_gamma16_up_optimize(png_transformp *transform, png_transform_controlp tc)
   /* As above, but the alpha channel is 'optimized' */
{
#  define png_ptr (tc->png_ptr)
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_const_bytep ep = sp + PNG_TC_ROWBYTES(*tc);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_transform_gamma *tr =
      png_transform_cast(png_transform_gamma, *transform);
   const png_fixed_point correct = tr->correct;

   /* The input always as 16 bits, the output 8 or 16.  There is always an alpha
    * channel and it is converted to the 'optimized' form, where pixels with
    * alpha not 0.0 or 1.0 are left in linear form (not gamma corrected.)  Where
    * bit depth convertion is required it is from 16-bits to 8-bits and the
    * DIV257 macro can be used.
    *
    * The following affirms and NOT_REACHED cases are consequences of the way
    * the background (compose) code works:
    */
   affirm(tr->optimize_alpha && !tr->encode_alpha && tc->bit_depth == 16U);

   /* TODO: split this into separate functions */
   switch (tr->to_bit_depth)
   {
      case 8U: /* 16-bit --> 8-bit */
         tc->sp = dp;
         tc->bit_depth = 8U;
         tc->gamma = tr->to_gamma;

         switch (PNG_TC_CHANNELS(*tc))
         {
            case 2:/* GA */
               debug(tr->shifts == 0x100U);
               ep -= 3U; /*SAFETY*/

               do
               {
                  png_uint_32 alpha = PNG_DIV257((sp[2] << 8) + sp[3]);

                  switch (alpha)
                  {
                     case 0U:
                        dp[1] = dp[0] = 0U;
                        break;

                     default: /* optimized case: linear color data */
                        dp[0] = png_check_byte(png_ptr,
                              PNG_DIV257((sp[0] << 8) + sp[1]));
                        dp[1] = PNG_BYTE(alpha);
                        break;

                     case 255U: /* opaque pixels are encoded */
                        dp[0] = PNG_BYTE(png_gamma_nxmbit_correct(
                           (sp[0] << 8) + sp[1], correct, 16U, 8U));
                        dp[1] = 255U;
                        break;
                  }

                  sp += 4U;
                  dp += 2U;
               }
               while (sp < ep);

               debug(sp == ep+3U);
               break;

            case 4:/* RGBA */
               debug(tr->shifts == 0x10000U);
               ep -= 7U; /*SAFETY*/

               do
               {
                  png_uint_32 alpha = PNG_DIV257((sp[6] << 8) + sp[7]);

                  switch (alpha)
                  {
                     case 0U:
                        memset(dp, 0U, 4U);
                        break;

                     default: /* optimized case: linear color data */
                        dp[0] = PNG_BYTE(PNG_DIV257((sp[0] << 8) + sp[1]));
                        dp[1] = PNG_BYTE(PNG_DIV257((sp[2] << 8) + sp[3]));
                        dp[2] = PNG_BYTE(PNG_DIV257((sp[4] << 8) + sp[5]));
                        dp[3] = PNG_BYTE(alpha);
                        break;

                     case 255U: /* opaque pixels are encoded */
                        dp[0] = PNG_BYTE(png_gamma_nxmbit_correct(
                           (sp[0] << 8) + sp[1], correct, 16U, 8U));
                        dp[1] = PNG_BYTE(png_gamma_nxmbit_correct(
                           (sp[2] << 8) + sp[3], correct, 16U, 8U));
                        dp[2] = PNG_BYTE(png_gamma_nxmbit_correct(
                           (sp[4] << 8) + sp[5], correct, 16U, 8U));
                        dp[3] = 255U;
                        break;
                  }

                  sp += 8U;
                  dp += 4U;
               }
               while (sp < ep);

               debug(sp == ep+7U);
               break;

            default:
               NOT_REACHED;
               break;
         }
         break;

      case 16: /* 16-bit to 16-bit */
         tc->sp = dp;
         tc->bit_depth = 16U;
         tc->gamma = tr->to_gamma;

         switch (PNG_TC_CHANNELS(*tc))
         {
            case 2:/* GA */
               debug(tr->shifts == 0x100U);
               ep -= 3U; /*SAFETY*/

               do
               {
                  unsigned int alpha = (sp[2] << 8) + sp[3];

                  switch (alpha)
                  {
                     case 0U:
                        memset(dp, 0U, 4U);
                        break;

                     default: /* optimized case: linear color data */
                        if (dp != sp)
                        {
                           memcpy(dp, sp, 4U);
                           UNTESTED
                        }
                        break;

                     case 65535U: /* opaque pixels are encoded */
                        {
                           unsigned int gray = png_gamma_nxmbit_correct(
                              (sp[0] << 8) + sp[1], correct, 16U, 16U);
                           dp[0] = PNG_BYTE(gray >> 8);
                           dp[1] = PNG_BYTE(gray);
                        }
                        dp[3] = dp[2] = 255U;
                        break;
                  }

                  sp += 4U;
                  dp += 4U;
               }
               while (sp < ep);

               debug(sp == ep+3U);
               break;

            case 4:/* RGBA */
               debug(tr->shifts == 0x10000U);
               ep -= 7U; /*SAFETY*/

               do
               {
                  unsigned int alpha = (sp[6] << 8) + sp[7];

                  switch (alpha)
                  {
                     case 0U:
                        memset(dp, 0U, 8U);
                        break;

                     default: /* optimized case: linear color data */
                        if (dp != sp)
                        {
                           memcpy(dp, sp, 8U);
                           UNTESTED
                        }
                        break;

                     case 65535U: /* opaque pixels are encoded */
                        {
                           unsigned int c = png_gamma_nxmbit_correct(
                              (sp[0] << 8) + sp[1], correct, 16U, 16U);
                           dp[0] = PNG_BYTE(c >> 8);
                           dp[1] = PNG_BYTE(c);

                           c = png_gamma_nxmbit_correct(
                              (sp[2] << 8) + sp[3], correct, 16U, 16U);
                           dp[2] = PNG_BYTE(c >> 8);
                           dp[3] = PNG_BYTE(c);

                           c = png_gamma_nxmbit_correct(
                              (sp[4] << 8) + sp[5], correct, 16U, 16U);
                           dp[4] = PNG_BYTE(c >> 8);
                           dp[5] = PNG_BYTE(c);
                        }
                        dp[7] = dp[6] = 255U;
                        break;
                  }

                  sp += 8U;
                  dp += 8U;
               }
               while (sp < ep);

               debug(sp == ep+7U);
               break;

            default:
               NOT_REACHED;
               break;
         }
         break;

      default:
         NOT_REACHED;
         break;
   }
#  undef png_ptr
}
#endif /* READ_ALPHA_MODE */

static void
png_do_scale16_up(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_const_bytep ep = sp + PNG_TC_ROWBYTES(*tc);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_transform_gamma *tr =
      png_transform_cast(png_transform_gamma, *transform);
   const unsigned int bit_depth = tr->to_bit_depth;

   affirm(tc->bit_depth == 16U && bit_depth < 8U);
   affirm(tr->shifts != 0U/*uninitialized*/);

   /* This is exactly the same as above but without the gamma correction and
    * without the 8-bit target support.  The code handles one or two channels,
    * but the result is not a PNG format unless the number of channels is just
    * 1 (grayscale).
    *
    * For multi-channel low bit depth the channels are packed into bytes using
    * the standard PNG big-endian packing.
    */
   affirm((tc->format & PNG_FORMAT_FLAG_COLOR) == 0);
   /* The alpha shift is actually ignored; at present we only get here with an
    * alpha channel if it is to be removed for transparent alpha processing.
    */
   debug(tc->format & PNG_FORMAT_FLAG_ALPHA ?
         (tr->shifts >> 8) == 1U : (tr->shifts >> 4) == 1U);
   debug(tc->transparent_alpha);

   tc->sp = dp;
   /* This is a pure scaling operation so sBIT is not invalidated or altered. */
   tc->bit_depth = bit_depth;

   /* TODO: maybe do this properly and use the alpha shift, but only the top bit
    * matters.
    */
   {
      const unsigned int shift = tr->shifts & 0xFU;
      const png_uint_32 factor = tr->channel_scale[0];
      const png_uint_32 round = 1U << (31U-bit_depth);
      unsigned int bits = 8U;
      unsigned int ob = 0U;

      do
      {
         png_uint_32 inb = *sp++ << 8; /* high bits first */
         inb += *sp++;

         inb = ((inb >> shift) * factor + round) >> (32U-bit_depth);
         bits -= bit_depth;
         ob = ob | (inb << bits);
         if (bits == 0U)
            bits = 8U, *dp++ = PNG_BYTE(ob), ob = 0U;
      }
      while (sp < ep);

      if (bits < 8U)
         *dp++ = PNG_BYTE(ob);
   }
#  undef png_ptr
}

static void
png_do_gamma8_down(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_const_bytep ep = dp + 1U/*safety*/;
   png_transform_gamma *tr =
      png_transform_cast(png_transform_gamma, *transform);
   const png_fixed_point correct = tr->correct;
   const png_uint_32 shifts = tr->shifts;

   affirm(tc->bit_depth == 8U && tr->to_bit_depth == 16U);
   affirm(tr->shifts != 0U/*uninitialized*/);
   debug((shifts & 0x8888U) == 0U); /* all shifts 7 or less */
   debug(!tr->encode_alpha && !tr->optimize_alpha); /* only set for 16 bits */

   sp += PNG_TC_ROWBYTES(*tc);
   tc->sp = dp;
   tc->bit_depth = tr->to_bit_depth;
   tc->gamma = tr->to_gamma;
   dp += PNG_TC_ROWBYTES(*tc);

   {
      png_uint_32 alpha_scale;
      unsigned int channel, alpha;

      debug((shifts >> (4*PNG_TC_CHANNELS(*tc))) == 1U);

      /* We are going down so alpha, if present, is first.  Notice that the init
       * routine has to reverse both 'shifts' and 'channel_scale' for the _down
       * cases.
       */
      if ((tc->format & PNG_FORMAT_FLAG_ALPHA) == 0)
         alpha_scale = alpha = 0U;

      else
      {
         alpha = shifts;
         alpha_scale = tr->channel_scale[0U];
      }

      channel = 1U;

      do /* 8-bit --> 16-bit */
      {
         unsigned int inb = *--sp, shift;

         if (channel == 1U)
            channel = shifts;

         shift = channel & 0xFU;
         inb >>= shift;

         if (channel == alpha) /* unencoded alpha, must scale */
            inb = (inb * alpha_scale + 0x8000U) >> 16;

         else
            inb = png_gamma_nxmbit_correct(inb, correct, 8U-shift, 16U);

         channel >>= 4;

         *--dp = PNG_BYTE(inb);
         *--dp = PNG_BYTE(inb >> 8);
      }
      while (dp > ep);

      debug(channel == 1U && dp == ep-1U);
   }
#  undef png_ptr
}

static void
png_do_expand8_down(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_const_bytep ep = dp + 1U/*safety*/;
   png_transform_gamma *tr =
      png_transform_cast(png_transform_gamma, *transform);
   const png_uint_32 shifts = tr->shifts;

   affirm(tc->bit_depth == 8U && tr->to_bit_depth == 16U);
   affirm(tr->shifts != 0U/*uninitialized*/);

   sp += PNG_TC_ROWBYTES(*tc);
   tc->sp = dp;
   tc->bit_depth = 16U;
   dp += PNG_TC_ROWBYTES(*tc);

   {
      png_uint_32 channel = 1U;
      png_const_uint_32p scale = 0U;

      do /* 8-bit -> 16-bit */
      {
         unsigned int inb = *--sp, shift;

         if (channel == 1U)
            channel = shifts, scale = tr->channel_scale;

         shift = channel & 0xFU;
         channel >>= 4;
         inb >>= shift;
         inb = (inb * *scale++ + 0x8000U) >> 16;
         /* dp starts beyond the end: */
         *--dp = PNG_BYTE(inb);
         *--dp = PNG_BYTE(inb >> 8);
      }
      while (dp > ep);

      debug(channel == 1U && dp == ep-1U);
   }
#  undef png_ptr
}

static void
png_do_expand8_down_fast(png_transformp *transform, png_transform_controlp tc)
   /* Optimized version of the above for when the sBIT settings are all a full 8
    * bits (the normal case).
    */
{
#  define png_ptr (tc->png_ptr)
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_const_bytep ep = dp + 1U/*safety*/;
   png_transform_gamma *tr =
      png_transform_cast(png_transform_gamma, *transform);

   affirm(tc->bit_depth == 8U && tr->to_bit_depth == 16U);
   affirm(tr->shifts != 0U/*uninitialized*/);

   sp += PNG_TC_ROWBYTES(*tc);
   tc->sp = dp;
   tc->bit_depth = 16U;
   dp += PNG_TC_ROWBYTES(*tc);

   do
      dp -= 2, dp[0] = dp[1] = *--sp;
   while (dp > ep);

   debug(dp == ep-1U);
#  undef png_ptr
}

static void
png_init_gamma_uncached(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   png_transform_gamma *tr =
      png_transform_cast(png_transform_gamma, *transform);

   debug(tc->init == PNG_TC_INIT_FINAL);

   /* Set this first; the result says if the sBIT data is significant, but it is
    * ignored here.
    */
   (void)init_gamma_sBIT(tr, tc);

   /* If png_set_alpha_mode is called but no background processing needs to be
    * done (because there is no alpha channel or tRNS) we get to here with
    * potentially spurious alpha mode flags.
    */
   if (!(tc->format & PNG_FORMAT_FLAG_ALPHA))
      tr->encode_alpha = tr->optimize_alpha = 0U;

   /* Use separate functions for the two input depths but not for the five
    * possible output depths and four channel counts.
    */
   if (tc->bit_depth == 8U)
   {
      if (tr->to_bit_depth <= 8U)
         tr->tr.fn = png_do_gamma8_up;

      else
      {
         debug(tr->to_bit_depth == 16U);
         reverse_gamma_sBIT(tr);
         tr->tr.fn = png_do_gamma8_down;
      }
   }

   else
   {
      affirm(tc->bit_depth == 16U);
#     ifdef PNG_READ_ALPHA_MODE_SUPPORTED
         if (!tr->optimize_alpha)
            tr->tr.fn = png_do_gamma16_up;
         else
            tr->tr.fn = png_do_gamma16_up_optimize;
#     else /* !READ_ALPHA_MODE */
         tr->tr.fn = png_do_gamma16_up;
#     endif /* !READ_ALPHA_MODE */
   }

   /* Since the 'do' routines always perform gamma correction they will always
    * expand the significant bits to the full output bit depth.
    */
   tc->invalid_info |= PNG_INFO_sBIT;
   tc->bit_depth = tr->to_bit_depth;
   tc->sBIT_R = tc->sBIT_G = tc->sBIT_B =
      png_check_byte(png_ptr, tc->bit_depth);
   if (tr->encode_alpha)
      tc->sBIT_A = tc->sBIT_G;
   tc->gamma = tr->to_gamma;
#  undef png_ptr
}

#ifdef PNG_READ_sBIT_SUPPORTED
static unsigned int
tc_sBIT(png_const_transform_controlp tc)
   /* Determine the maximum number of significant bits in the row at this point.
    * This uses the png_struct::sig_bit field if it has not been invalidated,
    * otherwise it just returns the current bit depth.
    */
{
   const png_structrp png_ptr = tc->png_ptr;
   unsigned int bit_depth = tc->bit_depth;

   if ((tc->invalid_info & PNG_INFO_sBIT) == 0U)
   {
      /* Normally the bit depth will not have been changed from the original PNG
       * depth, but it currently is changed by the grayscale expand to 8 bits,
       * an operation which doesn't invalidate sBIT.
       */
      unsigned int sBIT;

      if ((png_ptr->color_type & PNG_COLOR_MASK_COLOR) != 0U)
      {
         /* Must use the largest of the sBIT depths, except that unset values
          * take priority.
          */
         sBIT = png_ptr->sig_bit.red && png_ptr->sig_bit.green &&
            png_ptr->sig_bit.blue;

         if (sBIT != 0U)
         {
            sBIT = png_ptr->sig_bit.red;

            if (png_ptr->sig_bit.green > sBIT)
               sBIT = png_ptr->sig_bit.green;
            if (png_ptr->sig_bit.blue > sBIT)
               sBIT = png_ptr->sig_bit.blue;
         }
      }

      else
         sBIT = png_ptr->sig_bit.gray;

      if (sBIT > 0U && sBIT < bit_depth)
         bit_depth = sBIT;
   }

   return bit_depth;
}
#else /* !READ_sBIT */
#  define tc_sBIT(tc) ((tc)->bit_depth)
#endif /* READ_sBIT */

static void
png_init_gamma(png_transformp *transform, png_transform_controlp tc)
{
   const png_structrp png_ptr = tc->png_ptr;
   png_transform_gamma *tr =
      png_transform_cast(png_transform_gamma, *transform);

   if (tc->init == PNG_TC_INIT_FORMAT)
   {
      /* This should only happen for the final encode gamma transform, which
       * never initializes the target bit depth (see png_set_gamma and
       * png_set_alpha_mode).  The affirm is required here; in we can't continue
       * safely if the bit depth has been set somehow.
       */
      debug(tr->tr.order == PNG_TR_GAMMA_ENCODE);
      affirm(tr->to_gamma > 0 && tr->to_bit_depth == 0U);

      /* At this point the output gamma should not have been set yet: */
      debug(png_ptr->row_gamma == 0);

      /* The following must be true; png_set_gamma and png_set_alpha_mode set
       * (or default) the PNG gamma and other routines that insert a gamma
       * transform must only do in PNG_TC_INIT_FINAL:
       */
      debug(tc->gamma > 0);

      /* At this point the data gamma must be updated so that we get the correct
       * png_struct::row_gamma at the end of the init:
       */
      tc->gamma = tr->to_gamma;

      /* For safety invalidate the sBIT information too; we don't know yet
       * whether a gamma transform will be required but if it is the sBIT
       * information becomes invalid.
       */
      tc->invalid_info |= PNG_INFO_sBIT;
   }

   else /* PNG_TC_INIT_FINAL */
   {
      /* It is very bad if we get here when processing a row: */
      affirm(tc->init == PNG_TC_INIT_FINAL && png_ptr->row_bit_depth > 0);

      /* There are three cases:
       *
       * 1) Gamma correction is required, output bit depth may need to be
       *    defaulted.
       * 2) Gamma correction is not required but a bit depth change is
       *    necessary.
       * 3) Neither is required; the transform can be eliminated.
       *
       * First default the bit depth if it is not already set.  Note that if the
       * output is a palette then 'row_bit_depth' refers to the palette size and
       * 8U must be used here.  tc->palette is irrelevant; it only tells us that
       * the data came from a palette.
       */
      if (tr->to_bit_depth == 0)
      {
         if ((png_ptr->row_format & PNG_FORMAT_FLAG_COLORMAP) != 0U)
            tr->to_bit_depth = 8U;

         else
            tr->to_bit_depth = png_ptr->row_bit_depth;
      }

      /* (1); is gamma correction required?  If tc->gamma is 0 at this point it
       * is not, but then the png_struct::row_gamma should be 0 too.
       */
      implies(tc->gamma == 0, png_ptr->row_gamma == 0);
      implies(tr->to_gamma == 0, tc->gamma == 0);

      if (!png_gamma_equal(png_ptr, tc->gamma, tr->to_gamma, &tr->correct,
                           tc_sBIT(tc)))
      {
         /* First make sure the input doesn't have a tRNS chunk which needs to
          * be expanded now; if it does push_gamma_expand will push an
          * appropriate transform *before* this one and we need to return
          * immediately (the caller will call back to this function).
          */
         if (push_gamma_expand(transform, tc, 0/*need alpha*/))
         {
            affirm(tc->bit_depth >= 8U &&
                   (tc->invalid_info & PNG_INFO_tRNS) != 0U &&
                   *transform != &tr->tr);
            return;
         }

         debug(*transform == &tr->tr && tc->bit_depth >= 8U);

         /* The format is now 8 or 16-bit G, GA, RGB or RGBA and gamma
          * correction is required.
          */
         png_init_gamma_uncached(transform, tc);
         /* TODO: implement caching for the !tc->caching cases! */
         return;
      }

      /* The cases where the two gamma values are close enough to be considered
       * equal.  The code lies about the gamma; this prevents apps and the
       * simplified API getting into loops or bad conditions because the gamma
       * was not set to the expected value.
       *
       * Note that png_transform_control::gamma is only set here if both the
       * input and output gamma values are known, otherwise the transform
       * introduces a spurious know gamma value.
       */
      if (tr->to_gamma > 0 && tc->gamma > 0)
         tc->gamma = tr->to_gamma;

      if (tr->to_bit_depth > tc->bit_depth)
      {
         /* This is either the to-linear operation, in which case the expected
          * bit depth is 16U, or it is the final encode in the case where an
          * 'expand' operation was also specified.
          *
          * We don't care about the PNG_TR_GAMMA_ENCODE case because we know
          * that there has to be an expand operation further down the pipeline.
          */
         if (tr->tr.order < PNG_TR_GAMMA_ENCODE)
         {
            affirm(tr->to_bit_depth == 16U);

            if (push_gamma_expand(transform, tc, 0/*need alpha*/))
            {
               affirm(tc->bit_depth == 8U &&
                      (tc->invalid_info & PNG_INFO_tRNS) != 0U &&
                      *transform != &tr->tr);
               return;
            }

            debug(*transform == &tr->tr);
            affirm(tc->bit_depth == 8U); /* if 16U we would not be here! */

            /* not using byte_ops here, but if there is no sBIT required
             * (normally the case) the fast code can be used:
             */
            if (init_gamma_sBIT(tr, tc))
               tr->tr.fn = png_do_expand8_down;

            else
               tr->tr.fn = png_do_expand8_down_fast;

            tc->bit_depth = 16U;
         }

         else /* PNG_TR_GAMMA_ENCODE: nothing need be done */
            tr->tr.fn = NULL;
      }

      else if (tr->to_bit_depth < tc->bit_depth)
      {
         /* No gamma correction but bit depth *reduction* is required.  Expect
          * the 'from' bit depth to always be 16, otherwise this transform
          * should not have been pushed.  Also expect this to be the gamma
          * 'encode' operation at the end of the arithmetic.
          */
         affirm(tc->bit_depth == 16U && tr->tr.order == PNG_TR_GAMMA_ENCODE);

         /* If the target bit depth is 8-bit delay the operation and use the
          * standard 16-8-bit scale code.  For low bit depth do it now.
          */
         if (tr->to_bit_depth == 8U)
         {
            png_set_scale_16(png_ptr);
            tr->tr.fn = NULL;
         }

         else /* low bit depth */
         {
            (void)init_gamma_sBIT(tr, tc);
            tr->tr.fn = png_do_scale16_up;
            tc->bit_depth = tr->to_bit_depth;
         }
      }

      else /* gamma !significant and nothing to do */
         tr->tr.fn = NULL;
   }
}

#if !PNG_RELEASE_BUILD
int /* PRIVATE(debug only) */
png_gamma_check(png_const_structrp png_ptr, png_const_transform_controlp tc)
   /* Debugging only routine to repeat the test used above to determine if the
    * gamma was insignificant.
    *
    * NOTE: JB20160723: This may still be incorrect in a complicated transform
    * pipeline because it uses 'tc_sBIT' for the end of the pipeline whereas the
    * init above happens earlier.  I don't think this matters because the test
    * is only invoked if the gamma transform is eliminated or if there is a bug
    * and in the former case the sBIT values should remain unchanged.
    */
{
   png_fixed_point dummy;

   return png_gamma_equal(png_ptr, png_ptr->row_gamma, tc->gamma, &dummy,
                          tc_sBIT(tc));
}
#endif /* !RELEASE_BUILD */

static png_fixed_point
translate_gamma_flags(png_const_structrp png_ptr, png_fixed_point gamma,
    int is_screen)
   /* If 'is_screen' is set this returns the inverse of the supplied value; i.e.
    * this routine always returns an encoding value.
    */
{
   /* Check for flag values.  The main reason for having the old Mac value as a
    * flag is that it is pretty near impossible to work out what the correct
    * value is from Apple documentation - a working Mac system is needed to
    * discover the value!
    */
   switch (gamma)
   {
      case PNG_DEFAULT_sRGB:
      case PNG_GAMMA_sRGB:
      case PNG_FP_1/PNG_GAMMA_sRGB: /* stupid case: -100000 */
         gamma = PNG_GAMMA_sRGB_INVERSE;
         break;

      case PNG_GAMMA_MAC_18:
      case PNG_FP_1/PNG_GAMMA_MAC_18: /* stupid case: -50000 */
         gamma = PNG_GAMMA_MAC_INVERSE;
         break;

      default:
         if (is_screen)
         {
            /* Check for a ridiculously low value; this will result in an
             * overflow
             * in the reciprocal calculation.
             */
            if (gamma < 5)
            {
               png_app_error(png_ptr, "invalid screen gamma (too low)");
               gamma = 0;
            }

            else if (gamma != PNG_FP_1) /* optimize linear */
               gamma = png_reciprocal(gamma);
         }

         else if (gamma <= 0)
         {
            png_app_error(png_ptr, "invalid file gamma (too low)");
            gamma = 0;
         }
         break;
   }

   return gamma;
}

static png_transform_gamma *
add_gamma_transform(png_structrp png_ptr, unsigned int order,
   png_fixed_point gamma, unsigned int bit_depth, int force)
{
   /* Add a png_transform_gamma transform at the given position; this is a
    * utility which just adds the transform and (unconditionally) overwrites the
    * to_gamma field.  gamma must be valid.  If 'force' is true the gamma value
    * in an existing transform will be overwritten, otherwise this is just a
    * default value.
    */
   png_transform_gamma *tr = png_transform_cast(png_transform_gamma,
      png_add_transform(png_ptr, sizeof (png_transform_gamma), png_init_gamma,
         order));

   if (force || tr->to_gamma == 0)
      tr->to_gamma = gamma;

   tr->to_bit_depth = bit_depth;

   return tr;
}

void PNGFAPI
png_set_gamma_fixed(png_structrp png_ptr, png_fixed_point scrn_gamma,
    png_fixed_point file_gamma)
{
   png_debug(1, "in png_set_gamma_fixed");

   /* Validate the passed in file gamma value: */
   file_gamma = translate_gamma_flags(png_ptr, file_gamma, 0/*file*/);

   /* The returned value may be 0, this results in a png_app_error above which
    * may be ignored; if that happens simply ignore the setting.
    */
   if (file_gamma > 0)
   {
      /* Set the colorspace gamma value unconditionally - this overrides the
       * value in the PNG file if a gAMA chunk was present.  png_set_alpha_mode
       * provides a different, easier, way to default the file gamma.
       */
      png_ptr->colorspace.gamma = file_gamma;
      if (png_ptr->colorspace.flags & PNG_COLORSPACE_INVALID)
         png_ptr->colorspace.flags = PNG_COLORSPACE_HAVE_GAMMA;
      else
         png_ptr->colorspace.flags |= PNG_COLORSPACE_HAVE_GAMMA;
   }

   /* Do the same thing with the screen gamma; check it and handle it if valid.
    * This adds/sets the encoding of the final gamma transform in the chain.
    * png_set_alpha_mode does the same thing.
    */
   scrn_gamma = translate_gamma_flags(png_ptr, scrn_gamma, 1/*screen*/);

   if (scrn_gamma > 0)
      (void)add_gamma_transform(png_ptr, PNG_TR_GAMMA_ENCODE, scrn_gamma,
            0/*bit depth*/, 1/*force to_gamma to scrn_gamma*/);
}

#ifdef PNG_FLOATING_POINT_SUPPORTED
static png_fixed_point
convert_gamma_value(png_structrp png_ptr, double output_gamma)
{
   /* The following silently ignores cases where fixed point (times 100,000)
    * gamma values are passed to the floating point API.  This is safe and it
    * means the fixed point constants work just fine with the floating point
    * API.  The alternative would just lead to undetected errors and spurious
    * bug reports.  Negative values fail inside the _fixed API unless they
    * correspond to the flag values.
    */
   if (output_gamma < 0 || output_gamma > 128)
      output_gamma *= .00001;

   return png_fixed(png_ptr, output_gamma, "gamma value");
}

void PNGAPI
png_set_gamma(png_structrp png_ptr, double scrn_gamma, double file_gamma)
{
   png_set_gamma_fixed(png_ptr, convert_gamma_value(png_ptr, scrn_gamma),
       convert_gamma_value(png_ptr, file_gamma));
}
#endif /* FLOATING_POINT */
#endif /* READ_GAMMA */

#ifdef PNG_READ_RGB_TO_GRAY_SUPPORTED
static void
png_do_rtog_48(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   const png_uint_32 r = (*transform)->args >> 16;
   const png_uint_32 g = (*transform)->args & 0xFFFFU;
   const png_uint_32 b = 65536U - r - g;

   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   const png_const_bytep ep = sp + PNG_TC_ROWBYTES(*tc) - 6U;
   png_bytep dp = png_voidcast(png_bytep, tc->dp);

   debug(tc->bit_depth == 16U && tc->format == PNG_FORMAT_FLAG_COLOR &&
         (tc->gamma == 0U || !png_gamma_significant(png_ptr, tc->gamma, 16U)));

   tc->sp = dp;
   tc->format &= PNG_BIC_MASK(PNG_FORMAT_FLAG_COLOR);

   while (sp <= ep)
   {
      png_uint_32 gray = (((sp[0] << 8) + sp[1]) * r +
         ((sp[2] << 8) + sp[3]) * g +
         ((sp[4] << 8) + sp[5]) * b + 32767U) >> 16;

      debug(gray < 65536U);
      *dp++ = PNG_BYTE(gray >> 8);
      *dp++ = PNG_BYTE(gray);
      sp += 6U;
   }
#  undef png_ptr
}

static void
png_do_rtog_64(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   const png_uint_32 r = (*transform)->args >> 16;
   const png_uint_32 g = (*transform)->args & 0xFFFFU;
   const png_uint_32 b = 65536U - r - g;

   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   const png_const_bytep ep = sp + PNG_TC_ROWBYTES(*tc) - 8U;
   png_bytep dp = png_voidcast(png_bytep, tc->dp);

   debug(tc->bit_depth == 16U &&
         tc->format == PNG_FORMAT_FLAG_COLOR+PNG_FORMAT_FLAG_ALPHA &&
         (tc->gamma == 0U || !png_gamma_significant(png_ptr, tc->gamma, 16U)));

   tc->sp = dp;
   tc->format &= PNG_BIC_MASK(PNG_FORMAT_FLAG_COLOR);

   while (sp <= ep)
   {
      png_uint_32 gray = (((sp[0] << 8) + sp[1]) * r +
         ((sp[2] << 8) + sp[3]) * g +
         ((sp[4] << 8) + sp[5]) * b + 32767U) >> 16;

      debug(gray < 65536U);
      *dp++ = PNG_BYTE(gray >> 8);
      *dp++ = PNG_BYTE(gray);
      sp += 6U;
      *dp++ = *sp++; /* alpha */
      *dp++ = *sp++;
   }
#  undef png_ptr
}

static void
png_init_rgb_to_gray_arithmetic(png_transformp *transform,
   png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   /* This only gets used in the final init stage: */
   debug(tc->init == PNG_TC_INIT_FINAL && tc->bit_depth == 16U &&
         (tc->format & PNG_BIC_MASK(PNG_FORMAT_FLAG_ALPHA)) ==
            PNG_FORMAT_FLAG_COLOR);

   (*transform)->fn = (tc->format & PNG_FORMAT_FLAG_ALPHA) ? png_do_rtog_64 :
      png_do_rtog_48;

   tc->format &= PNG_BIC_MASK(PNG_FORMAT_FLAG_COLOR);
   tc->invalid_info |= PNG_INFO_sBIT;
   tc->sBIT_R = tc->sBIT_G = tc->sBIT_B = tc->sBIT_A =
      png_check_byte(png_ptr, tc->bit_depth);
#  undef png_ptr
}

typedef struct
{
   png_transform tr;
   png_fixed_point red_coefficient;
   png_fixed_point green_coefficient;
   unsigned int    coefficients_set   :1;
   unsigned int    error_action       :2;
}  png_transform_rgb_to_gray;

static void
png_update_rgb_status(png_structrp png_ptr, png_transformp *transform)
{
   png_transform_rgb_to_gray *tr = png_transform_cast(png_transform_rgb_to_gray,
      *transform);

   png_ptr->rgb_to_gray_status = 1U;
   tr->tr.fn = NULL; /* one warning/error only */

   switch (tr->error_action)
   {
      case PNG_ERROR_ACTION_WARN:
         png_warning(png_ptr, "RGB to gray found nongray pixel");
         break;

      case PNG_ERROR_ACTION_ERROR:
         png_error(png_ptr, "RGB to gray found nongray pixel");
         break;

      default:
         break;
   }
}

static void
png_do_rgb_check24(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   /* Sets 'rgb_to_gray' status if a pixel is found where the red green and blue
    * channels are not equal.
    */
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   const png_const_bytep ep = sp + PNG_TC_ROWBYTES(*tc) - 3U;

   debug(tc->bit_depth == 8U && tc->format == PNG_FORMAT_FLAG_COLOR);

   while (sp <= ep)
   {
      if ((sp[0] ^ sp[1]) | (sp[2] ^ sp[1]))
      {
         png_update_rgb_status(png_ptr, transform);
         break;
      }

      sp += 3U;
   }
#  undef png_ptr
}

static void
png_do_rgb_check32(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   /* Sets 'rgb_to_gray' status if a pixel is found where the red green and blue
    * channels are not equal and alpha is not zero.
    */
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   const png_const_bytep ep = sp + PNG_TC_ROWBYTES(*tc) - 4U;

   debug(tc->bit_depth == 8U &&
         tc->format == PNG_FORMAT_FLAG_COLOR+PNG_FORMAT_FLAG_ALPHA);

   while (sp <= ep)
   {
      if (((sp[0] ^ sp[1]) | (sp[2] ^ sp[1])) && sp[3] != 0)
      {
         png_update_rgb_status(png_ptr, transform);
         break;
      }

      sp += 4U;
   }
#  undef png_ptr
}

static void
png_do_rgb_check48(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   /* Sets 'rgb_to_gray' status if a pixel is found where the red green and blue
    * channels are not equal.
    */
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   const png_const_bytep ep = sp + PNG_TC_ROWBYTES(*tc) - 6U;

   debug(tc->bit_depth == 16U && tc->format == PNG_FORMAT_FLAG_COLOR);

   while (sp <= ep)
   {
      if ((sp[0] ^ sp[2]) | (sp[4] ^ sp[2]) |
          (sp[1] ^ sp[3]) | (sp[5] ^ sp[3]))
      {
         png_update_rgb_status(png_ptr, transform);
         break;
      }

      sp += 6U;
   }
#  undef png_ptr
}

static void
png_do_rgb_check64(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   /* Sets 'rgb_to_gray' status if a pixel is found where the red green and blue
    * channels are not equal and alpha is not zero.
    */
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   const png_const_bytep ep = sp + PNG_TC_ROWBYTES(*tc) - 8U;

   debug(tc->bit_depth == 16U &&
         tc->format == PNG_FORMAT_FLAG_COLOR+PNG_FORMAT_FLAG_ALPHA);

   while (sp <= ep)
   {
      if (((sp[0] ^ sp[2]) | (sp[4] ^ sp[2]) |
          (sp[1] ^ sp[3]) | (sp[5] ^ sp[3])) &&
          (sp[6] | sp[7]) != 0)
      {
         png_update_rgb_status(png_ptr, transform);
         break;
      }

      sp += 8U;
   }
#  undef png_ptr
}

static void
png_init_rgb_to_gray(png_transformp *transform, png_transform_controlp tc)
{
   png_structrp png_ptr = tc->png_ptr;

   /* Basic checks: if there is no color in the format this transform is not
    * applicable.
    */
   if ((tc->format & PNG_FORMAT_FLAG_COLOR) != 0)
   {
      png_transform_rgb_to_gray *tr = png_transform_cast(
         png_transform_rgb_to_gray, *transform);

      /* no colormap allowed: */
      affirm(tc->init && !(tc->format & PNG_FORMAT_FLAG_COLORMAP));
      /* no extra flags yet: */
      debug(!(tc->format &
               PNG_BIC_MASK(PNG_FORMAT_FLAG_COLOR+PNG_FORMAT_FLAG_ALPHA)));
      /* at present no non-palette caching: */
      implies(tc->caching, tc->palette);

      if (tc->init == PNG_TC_INIT_FORMAT)
      {
         /* The convertion should just remove the 'COLOR' flag and do nothing
          * else, but if a tRNS chunk is present this would invalidate it.
          * Handle this by expanding it now.
          */
         if ((tc->format & PNG_FORMAT_FLAG_ALPHA) == 0 && !tc->palette &&
             png_ptr->num_trans == 1 && !(tc->invalid_info & PNG_INFO_tRNS))
         {
            /* Only if expand was requested and not cancelled: */
            if (tc->expand_tRNS && !tc->strip_alpha)
               tc->format |= PNG_FORMAT_FLAG_ALPHA;

            tc->invalid_info |= PNG_INFO_tRNS; /* prevent expansion later */
         }

         tc->format &= PNG_BIC_MASK(PNG_FORMAT_FLAG_COLOR);
      }

      else /* PNG_TC_INIT_FINAL */
      {
         unsigned int index;   /* channel to select (invalid) */
         png_byte sBIT_color;  /* sBIT of that channel if valid */
         png_fixed_point r, g; /* Coefficients in range 0..65536 */

         /* Push a tRNS transform if required.  Because this is a push the
          * transform the init needs to be run now.  This needs to go in
          * before the check on r==g==b because a color key might be used.
          */
         if ((tc->format & PNG_FORMAT_FLAG_ALPHA) == 0 && !tc->palette &&
             png_ptr->num_trans == 1 && !(tc->invalid_info & PNG_INFO_tRNS))
         {
            if (tc->expand_tRNS && !tc->strip_alpha)
            {
               png_transformp tr_expand = png_push_transform(png_ptr,
                  sizeof (png_expand), png_init_expand, transform, NULL);

               debug(*transform == tr_expand);
               tr_expand->args |= PNG_EXPAND_tRNS;
               png_init_expand(transform, tc);
               /* Check for the infinite loop possibility: */
               affirm((tc->invalid_info & PNG_INFO_tRNS) != 0);
               return;
            }

            else
               tc->invalid_info |= PNG_INFO_tRNS;
         }

         {
            png_fixed_point red, green;

            if (tr->coefficients_set)
            {
               red = tr->red_coefficient;
               green = tr->green_coefficient;
            }

#           ifdef PNG_COLORSPACE_SUPPORTED
               else if ((png_ptr->colorspace.flags &
                         (PNG_COLORSPACE_HAVE_ENDPOINTS+PNG_COLORSPACE_INVALID))
                         == PNG_COLORSPACE_HAVE_ENDPOINTS)
               {
                  red = png_ptr->colorspace.end_points_XYZ.red_Y;
                  green = png_ptr->colorspace.end_points_XYZ.green_Y;
               }
#           endif

            else /* no colorspace support, assume sRGB */
            {
               /* From IEC 61966-2-1:1999, the reverse transformation from sRGB
                * RGB values to XYZ D65 values (not CIEXYZ!).  These are not
                * exact inverses of the forward transformation; they only have
                * four (decimal) digits of precision.
                *
                * API CHANGE: in 1.7.0 the sRGB values from the official IEC
                * specification are used, previously libpng used values from
                * Charles Poynton's ColorFAQ of 1998-01-04.  The original page
                * is gone, however up to date information can be found below:
                *
                *    http://www.poynton.com/ColorFAQ.html
                *
                * At the time of reading (20150628) this web site quotes the
                * same values as below and cites ITU Rec 709 as the source.
                */
               red = 21260;
               green = 71520;
            }

            /* Prior to 1.7 this calculation was done with 15-bit precision,
             * this is because the code was written pre-muldiv and tried to
             * work round the problems caused by the signs in integer
             * calculations.
             */
            (void)png_muldiv(&r, red, 65536, PNG_FP_1);
            (void)png_muldiv(&g, green, 65536, PNG_FP_1);
         }

         /* If the convertion can be deduced to select a single channel do so.
          * If the error action is set to error just copy the red channel, if
          * the coefficients select just one channel use that.
          */
         if (tr->error_action == PNG_ERROR_ACTION_ERROR || r >= 65536)
            index = 0U, sBIT_color = tc->sBIT_R; /* select red */

         else if (g >= 65536)
            index = 1U, sBIT_color = tc->sBIT_G; /* select green */

         else if (r + g == 0)
            index = 2U, sBIT_color = tc->sBIT_B; /* select blue */

         else
            index = 3U, sBIT_color = 0U/*UNUSED*/;

         if (index == 3U)
         {
            /* Arithmetic will have to be done.  For this we need linear 16-bit
             * data which must then be converted back to the required bit depth,
             * png_init_gamma handles this.  It may push other expand operations
             * (it shouldn't but it can), so give it some space.
             *
             * The gamma must be restored to the original value, 0U for the bit
             * depth means use the output bit depth.
             */
            (void)add_gamma_transform(png_ptr, PNG_TR_GAMMA_ENCODE, tc->gamma,
               0U/*bit depth*/, 0/*default*/);

            /* If png_init_gamma is called with tc->gamma 0 it does the right
             * thing in PNG_TC_INIT_FINAL; it just does any required bit depth
             * adjustment.
             */
            (void)add_gamma_transform(png_ptr, tr->tr.order + 0x10U, PNG_FP_1,
               16U, 1/*force: doesn't matter*/);

            {
               /* This init routine will update the sBIT information
                * appropriately.
                */
               png_transformp tr_rtog = png_add_transform(png_ptr, 0/*size*/,
                  png_init_rgb_to_gray_arithmetic, tr->tr.order + 0x20U);

               /* r and g are known to be in the range 0..65535, so pack them
                * into the 'args' argument of a new transform.
                */
               tr_rtog->args = (((png_uint_32)r) << 16) + g;
            }
         }

         else /* index < 3 */
         {
            /* TODO: does this need to select the correct sBIT value too? */
            png_add_rgb_to_gray_byte_ops(png_ptr, tc, index,
               tr->tr.order + 0x10U);
            tc->sBIT_G = sBIT_color;
         }

         /* Prior to 1.7 libpng would always check for r!=g!=b.  In 1.7 an extra
          * error_action setting is added to prevent this overhead.
          */
         if (tr->error_action)
            tr->tr.fn = tc->bit_depth == 8 ?
               ((tc->format & PNG_FORMAT_FLAG_ALPHA) ?
                png_do_rgb_check32 : png_do_rgb_check24) :
               ((tc->format & PNG_FORMAT_FLAG_ALPHA) ?
                png_do_rgb_check64 : png_do_rgb_check48);

         else
            tr->tr.fn = NULL; /* PNG_ERROR_ACTION_NO_CHECK */
      }
   }

   else /* not color: transform not applicable */
      (*transform)->fn = NULL;
}

void PNGFAPI
png_set_rgb_to_gray_fixed(png_structrp png_ptr, int error_action,
    png_fixed_point red, png_fixed_point green)
   /* API CHANGE: in 1.7 calling this on a palette PNG no longer causes the
    * palette to be expanded (unless explicitly requested), rather it converts
    * the palette to grayscale.
    */
{
   /* The coefficients must be reasonable, the error handling is to warn (pre
    * 1.7) or app error (1.7) and drop back to the cHRM definition of Y.  The
    * drop back is done in the init routine if relevant flag is unset.  Passing
    * negative values causes this default to be used without a warning.
    */
   int pset = 0;

   if (red >= 0 && green >= 0)
   {
      if (red <= PNG_FP_1 && green <= PNG_FP_1 && red + green <= PNG_FP_1)
         pset = 1;

      else /* overflow */
         png_app_error(png_ptr, "rgb_to_gray coefficients too large (ignored)");
   }

   {
      png_transform_rgb_to_gray *tr =
         png_transform_cast(png_transform_rgb_to_gray,
            png_add_transform(png_ptr, sizeof (png_transform_rgb_to_gray),
               png_init_rgb_to_gray, PNG_TR_RGB_TO_GRAY));

      tr->error_action = 0x3U & error_action;

      if (red < 0 || green < 0) /* use cHRM default */
         tr->coefficients_set = 0U;

      else if (pset) /* else bad coefficients which get ignored */
      {
         tr->coefficients_set = 1U;
         tr->red_coefficient = red;
         tr->green_coefficient = green;
      }
   }
}

#ifdef PNG_FLOATING_POINT_SUPPORTED
/* Convert a RGB image to a grayscale of the same width.  This allows us,
 * for example, to convert a 24 bpp RGB image into an 8 bpp grayscale image.
 */

void PNGAPI
png_set_rgb_to_gray(png_structrp png_ptr, int error_action, double red,
    double green)
{
   png_set_rgb_to_gray_fixed(png_ptr, error_action,
      png_fixed(png_ptr, red, "rgb to gray red coefficient"),
      png_fixed(png_ptr, green, "rgb to gray green coefficient"));
}
#endif /* FLOATING POINT */
#endif /* RGB_TO_GRAY */

#ifdef PNG_READ_BACKGROUND_SUPPORTED
typedef struct
{
   png_transform     tr;
   struct
   {
      png_color_16      background;
      unsigned int      need_expand :1; /* Background matches format of PNG */
      unsigned int      rgb_to_gray :1; /* RGB-to-gray transform found */
      unsigned int      compose_background   :1; /* png_set_background */
      unsigned int      associate_alpha      :1;
      unsigned int      encode_alpha         :1;
      unsigned int      optimize_alpha       :1;
      unsigned int      background_is_gray   :1; /* Background color is gray */
      unsigned int      background_bit_depth :5; /* bit depth, 1..16 */
      unsigned int      ntrans               :3; /* 1..6 bytes */
      png_byte          transparent_pixel[6];
      png_byte          background_pixel[6];
      png_fixed_point   background_gamma;
   }  st; /* to allow the whole state to be copied reliably */
}  png_transform_background;

static void
resolve_background_color(png_transform_background *tr,
   png_transform_controlp tc)
{
   png_const_structp png_ptr = tc->png_ptr;

   /* Deduce the bit depth and color information for the background, the
    * special case is when need_expand is set and the PNG has palette format,
    * then (and only then) the background value is a palette index.
    */
   if (tr->st.need_expand && tc->palette)
   {
      unsigned int i = tr->st.background.index;
      png_byte r, g, b;

      if (i >= png_ptr->num_palette)
      {
         png_app_error(png_ptr, "background index out of range");
         tr->tr.fn = NULL;
         return;
      }

      tr->st.background_bit_depth = 8U;
      r = png_ptr->palette[i].red;
      g = png_ptr->palette[i].green;
      b = png_ptr->palette[i].blue;

      if (r == g && g == b)
      {
         tr->st.background_is_gray = 1U;
         tr->st.background.gray = g;
         UNTESTED
      }

      else
      {
         tr->st.background_is_gray = 0U;
         tr->st.background.red = r;
         tr->st.background.green = g;
         tr->st.background.blue = b;
         UNTESTED
      }
   }

   else /* background is not a palette index */
   {
      int use_rgb;
      png_uint_16 mask;

      /* First work out the bit depth and whether or not to use the RGB
       * fields of the background.
       */
      if (tr->st.need_expand)
      {
         affirm(!(tc->format & PNG_FORMAT_FLAG_COLORMAP));
         tr->st.background_bit_depth =
            png_check_bits(png_ptr, png_ptr->bit_depth, 5U);
         use_rgb = (png_ptr->color_type & PNG_COLOR_MASK_COLOR) != 0;
      }

      else /* screen format background */
      {
         /* If the final output is in palette format assume the background
          * is in a matching format.  This covers two cases, an original
          * COLORMAP PNG  and png_set_quantize.
          */
         if ((png_ptr->row_format & PNG_FORMAT_FLAG_COLORMAP) != 0)
            tr->st.background_bit_depth = 8U;

         else
            tr->st.background_bit_depth =
               png_check_bits(png_ptr, png_ptr->row_bit_depth, 5U);

         use_rgb = (png_ptr->row_format & PNG_FORMAT_FLAG_COLOR) != 0;
      }

      /* The PNG spec says to use the low bits of the values, so we mask out
       * the high bits here (at present no warning is produced if they are
       * set.)
       */
      mask = png_check_u16(png_ptr, (1U << tr->st.background_bit_depth)-1U);

      if (use_rgb)
      {
         png_uint_16 r, g, b;

         r = tr->st.background.red & mask;
         g = tr->st.background.green & mask;
         b = tr->st.background.blue & mask;

         if (r == g && g == b)
         {
            tr->st.background_is_gray = 1U;
            tr->st.background.gray = g;
         }

         else
         {
            tr->st.background_is_gray = 0U;
            tr->st.background.red = r;
            tr->st.background.green = g;
            tr->st.background.blue = b;
         }
      }

      else /* gray */
      {
         tr->st.background_is_gray = 1U;
         tr->st.background.gray = tr->st.background.gray & mask;
      }
   }
}

static void
gamma_correct_background_component(png_const_structrp png_ptr, png_uint_16p cp,
   unsigned int bdc, png_fixed_point correction, unsigned int bdout)
   /* Utility function for gamma_correct_background. */
{
   unsigned int c = *cp;

   /* 0.0 and 1.0 are unchanged (and common): */
   if (c > 0U && c < (1U<<bdc)-1U)
   {
      if (correction != 0)
         c = png_check_bits(png_ptr,
            png_gamma_nxmbit_correct(c, correction, bdc, bdout), bdout);

      else if (bdc != bdout)
      {
         /* Scale the value from bdc to bdout bits. */
         png_int_32 i;
         affirm(png_muldiv(&i, c, (1U<<bdout)-1U, (1U<<bdc)-1U));
         c = png_check_bits(png_ptr, i, bdout);
      }
   }

   else if (c != 0U)
      c = (1U << bdout) - 1U;

   *cp = PNG_UINT_16(c);
   PNG_UNUSED(png_ptr) /* if checking disabled */
}

static void
gamma_correct_background(png_transform_background *tr,
   png_const_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   png_fixed_point correction = tc->gamma;
   const unsigned int bdback = tr->st.background_bit_depth;
   const unsigned int bdrow = tc->bit_depth;

   /* This is harmless if it fails but it will damage the output pixels - they
    * won't have the requested color depth accuracy where the background is
    * used.
    */
   debug(bdback <= bdrow);
   debug(tr->st.background_is_gray || (bdrow >= 8U && bdback >= 8U));

   /* The background is assumed to be full precision; there is no sBIT
    * information for it.  The convertion converts from the current depth and
    * gamma of the background to that in the transform control.  It uses the
    * full 16-bit precision when considering the gamma values even though this
    * is probably spurious.
    */
   if (correction != 0 && (tr->st.background_gamma == 0 ||
       png_gamma_equal(png_ptr, tr->st.background_gamma, correction,
          &correction, 16U)))
      correction = 0; /* no correction! */

   if (tr->st.background_is_gray)
      gamma_correct_background_component(png_ptr, &tr->st.background.gray,
            bdback, correction, bdrow);

   else
   {
      gamma_correct_background_component(png_ptr, &tr->st.background.red,
            bdback, correction, bdrow);
      gamma_correct_background_component(png_ptr, &tr->st.background.green,
            bdback, correction, bdrow);
      gamma_correct_background_component(png_ptr, &tr->st.background.blue,
            bdback, correction, bdrow);
   }

   /* Regardless of whether there was a correction set the background gamma: */
   tr->st.background_gamma = tc->gamma;
   tr->st.background_bit_depth = png_check_bits(png_ptr, bdrow, 5U);
#  undef png_ptr
}

static void
fill_background_pixel(png_transform_background *tr, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   /* Fill in 'background_pixel' if the appropriate sequence of bytes for the
    * format given in the transform control.
    */
   unsigned int bdtc = tc->bit_depth;

   /* If necessary adjust the background pixel to the current row format (it is
    * important to do this as late as possible to avoid spurious
    * interconvertions).
    */
   gamma_correct_background(tr, tc);

   if (tr->st.background_is_gray)
   {
      unsigned int g = tr->st.background.gray;

      /* 'g' now has enough bits for the destination, note that in the case of
       * low bit depth gray this causes the pixel to be replicated through the
       * written byte.  Fill all six bytes with the replicated background:
       */
      while (bdtc < 8U)
      {
         g &= (1U << bdtc) - 1U; /* use only the low bits */
         g |= g << bdtc;
         bdtc <<= 1;
      }

      memset(tr->st.background_pixel, PNG_BYTE(g), 6U);
      if (bdtc == 16U)
         tr->st.background_pixel[0] = tr->st.background_pixel[2] =
            tr->st.background_pixel[4] = PNG_BYTE(g >> 8);
      /* Must not include the alpha channel here: */
      tr->st.ntrans = png_check_bits(png_ptr,
         ((tc->format & PNG_FORMAT_FLAG_COLOR)+1U) << (bdtc == 16U), 3U);
   }

   else
   {
      unsigned int r = tr->st.background.red;
      unsigned int g = tr->st.background.green;
      unsigned int b = tr->st.background.blue;

      debug((tc->format & PNG_FORMAT_FLAG_COLOR) != 0);

      switch (bdtc)
      {
         case 8U:
            tr->st.background_pixel[0] = PNG_BYTE(r);
            tr->st.background_pixel[1] = PNG_BYTE(g);
            tr->st.background_pixel[2] = PNG_BYTE(b);
            tr->st.ntrans = 3U;
            break;

         case 16U:
            tr->st.background_pixel[0] = PNG_BYTE(r>>8);
            tr->st.background_pixel[1] = PNG_BYTE(r);
            tr->st.background_pixel[2] = PNG_BYTE(g>>8);
            tr->st.background_pixel[3] = PNG_BYTE(g);
            tr->st.background_pixel[4] = PNG_BYTE(b>>8);
            tr->st.background_pixel[5] = PNG_BYTE(b);
            tr->st.ntrans = 6U;
            break;

         default:
            NOT_REACHED;
      }
   }
#  undef png_ptr
}

/* Look for colors matching the trans_color in png_ptr and replace them.  This
 * must handle all the non-alpha formats.
 */
static void
png_do_replace_tRNS_multi(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   png_transform_background *tr =
      png_transform_cast(png_transform_background, *transform);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   const unsigned int cbytes = tr->st.ntrans;
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   const png_const_bytep ep = sp + PNG_TC_ROWBYTES(*tc) - cbytes/*safety*/;
   const int copy = dp != sp;

   /* We expect opaque and transparent pixels to be interleaved but with long
    * sequences of each.
    */
   debug(!(tc->format & PNG_FORMAT_FLAG_ALPHA) &&
         PNG_TC_PIXEL_DEPTH(*tc) == cbytes << 3);
   tc->invalid_info |= PNG_INFO_tRNS;
   tc->sp = dp;

   /* Look for pixels that match the transparent value, copying opaque ones as
    * required.
    */
   do
   {
      const png_const_bytep opaque_start = sp;
      size_t cb;

      /* Find a transparent pixel, or the end: */
      do
      {
         if (memcmp(sp, tr->st.transparent_pixel, cbytes) == 0) /*transparent*/
            break;
         sp += cbytes;
      }
      while (sp <= ep);

      cb = sp - opaque_start;

      /* Copy any opaque pixels: */
      if (cb > 0)
      {
         if (copy)
            memcpy(dp, opaque_start, cb);
         dp += cb;
      }

      /* Set transparent pixels to the background (this has to be done one-by
       * one; the case where all the bytes in the background are equal is not
       * optimized.)
       */
      if (sp <= ep) do
      {
         memcpy(dp, tr->st.background_pixel, cbytes);
         sp += cbytes;
         dp += cbytes;
      }
      while (sp <= ep && memcmp(sp, tr->st.transparent_pixel, cbytes) == 0);
   } while (sp <= ep);

   debug(sp == ep+cbytes);
#  undef png_ptr
}

static void
png_do_replace_tRNS_8(png_transformp *transform, png_transform_controlp tc)
   /* The single byte version: 8-bit gray */
{
#  define png_ptr (tc->png_ptr)
   png_transform_background *tr =
      png_transform_cast(png_transform_background, *transform);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_alloc_size_t row_bytes = tc->width;
   const int copy = dp != sp;
   const int transparent_pixel = tr->st.transparent_pixel[0];
   const int background_pixel = tr->st.background_pixel[0];

   /* We expect opaque and transparent pixels to be interleaved but with long
    * sequences of each.
    */
   debug(!(tc->format & PNG_FORMAT_FLAG_ALPHA) &&
         PNG_TC_PIXEL_DEPTH(*tc) == 8 && tr->st.ntrans == 1);
   tc->invalid_info |= PNG_INFO_tRNS;
   tc->sp = dp;

   /* Now search for a byte that matches the transparent pixel. */
   do
   {
      const png_const_bytep tp = png_voidcast(png_const_bytep,
         memchr(sp, transparent_pixel, row_bytes));
      png_alloc_size_t cb;

      if (tp == NULL) /* all remaining pixels are opaque */
      {
         if (copy)
            memcpy(dp, sp, row_bytes);
         return;
      }

      cb = tp - sp;
      if (cb > 0) /* some opaque pixels found */
      {
         if (copy)
            memcpy(dp, sp, cb);
         sp = tp;
         dp += cb;
         debug(row_bytes > cb);
         row_bytes -= cb;
      }

      /* Now count the transparent pixels, this could use strspn but for the
       * moment does not.
       */
      debug(row_bytes > 0);
      ++sp; /* next to check, may be beyond the last */
      while (--row_bytes > 0 && *sp == transparent_pixel) ++sp;

      cb = sp - tp;
      memset(dp, background_pixel, cb);
      dp += cb;
   } while (row_bytes > 0);
   UNTESTED
#  undef png_ptr
}

static void
png_do_set_row(png_transformp *transform, png_transform_controlp tc)
   /* This is a no-op transform that both invalidates INFO from args and sets
    * the entire row to the byte given in the top bits.
    */
{
   png_bytep dp = png_voidcast(png_bytep, tc->dp);

   tc->sp = dp;
   memset(dp, (*transform)->args >> 24, PNG_TC_ROWBYTES(*tc));
}

static void
png_do_replace_tRNS_lbd(png_transformp *transform, png_transform_controlp tc)
{
   /* This is the 2 or 4 bit depth grayscale case; the 1 bit case is handled by
    * the two routines above and the 8-bit and 16-bit cases by the two before
    * that.
    *
    * The transform contains pixel values that have been expanded to one byte,
    * the code needs to match the tRNS pixel and substitute the background one
    * in each byte.
    */
#  define png_ptr (tc->png_ptr)
   png_transform_background *tr =
      png_transform_cast(png_transform_background, *transform);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   png_const_bytep ep = sp + PNG_TC_ROWBYTES(*tc);
   const unsigned int copy = sp != dp;
   const png_byte transparent_pixel = tr->st.transparent_pixel[0];
   const png_byte background_pixel = tr->st.background_pixel[0];

   /* We expect opaque and transparent pixels to be interleaved but with long
    * sequences of each.
    */
   debug(!(tc->format & PNG_FORMAT_FLAG_ALPHA) &&
         PNG_TC_PIXEL_DEPTH(*tc) < 8 && tr->st.ntrans == 1);
   tc->sp = dp;

   /* Now search for a byte that contains the transparent pixel
    *
    * NOTE: this is the "strlen" algorithm, I first saw a variant implemented in
    * Acorn RISC iX (strlen) around 1991, almost certainly derived from a
    * suggestion by Alan Mycroft dating from April 27, 1987 (Mycroft was one of
    * the authors of the 'Norcroft' compiler used for RISC iX, and well known to
    * the RISC iX implementors.) See, e.g.:
    *
    *    http://bits.stephan-brumme.com/null.html.
    *
    * The exact form used here is the one reported by Brumme; I haven't been
    * able to find the original Mycroft posting, it was probably on comp.arch.
    *
    * The 4-bit and 2-bit versions (probably slower in the 4-bit case than the
    * do-it-by-pixel version, but definately faster once 32-bit handling is
    * implemented):
    *
    *    4 bit: (byte - 0x11) & ~byte & 0x88
    *    2 bit: (byte - 0x55) & ~byte & 0xcc
    *
    * The generalizations to 32 bits (8 and 16 pixels per step) should be
    * obvious.
    *
    * This algorithm reads pixels within a byte beyond the end of the row and,
    * potentially, changes the non-existent pixels.  This is harmless and not
    * a security risk.
    */
   if (tc->bit_depth == 4U)
   {
      /* For the moment the algorithm isn't used; there are only two pixels in
       * each byte so it is likely to be quicker to check as below:
       */
      do
      {
         const png_byte b = *sp++;
         const unsigned int m = b ^ transparent_pixel;

         if (m == 0U) /* both transparent */
            *dp = background_pixel;

         else if ((m & 0xF0U) == 0U) /* first transparent */
            *dp = PNG_BYTE((background_pixel & 0xF0U) | (b & 0x0FU));

         else if ((m & 0x0FU) == 0U) /* second transparent */
            *dp = PNG_BYTE((background_pixel & 0x0FU) | (b & 0xF0U));

         else if (copy) /* neither transparent */
            *dp = b;

         ++dp;
      } while (sp < ep);
   }

   else
   {
      affirm(tc->bit_depth == 2U);

      do
      {
         const png_byte b = *sp++;
         const unsigned int m = b ^ transparent_pixel;

         if (m == 0U) /* transparent */
            *dp = background_pixel;

         else if (0xAAU & ((m - 0x55U) & ~m))
         {
            /* One or more pixels transparent */
            const unsigned int mask =
               (m & 0xC0U ? 0xC0U : 0U) |
               (m & 0x30U ? 0x30U : 0U) |
               (m & 0x0CU ? 0x0CU : 0U) |
               (m & 0x03U ? 0x03U : 0U);

            *dp = PNG_BYTE((b & mask) | (background_pixel & ~mask));
         }

         else if (copy) /* no transparent pixels */
            *dp = b;

         ++dp;
      } while (sp < ep);
   }

#  undef png_ptr
}

static void
png_do_background_with_transparent_GA8(png_transformp *transform,
   png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   png_transform_background *tr =
      png_transform_cast(png_transform_background, *transform);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   const png_const_bytep ep = sp + PNG_TC_ROWBYTES(*tc) - 1U/*safety*/;
   const png_byte background_pixel = tr->st.background_pixel[0];

   /* Because this is an alpha format and we are removing the alpha channel we
    * can copy up.
    */
   debug(tc->bit_depth == 8U && tc->format == PNG_FORMAT_GA &&
         tr->st.ntrans == 1U);
   tc->format &= PNG_BIC_MASK(PNG_FORMAT_FLAG_ALPHA);
   tc->sp = dp;

   /* Look for pixels that have alpha 0; all others should have alpha 1.0,
    * however they are simply treated as opaque regardless.
    */
   do
   {
      *dp++ = (sp[1] == 0U) ? background_pixel : sp[0];
      sp += 2U;
   } while (sp < ep);

   debug(sp == ep+1U);
#  undef png_ptr
}

static void
png_do_background_with_transparent_GA16(png_transformp *transform,
   png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   png_transform_background *tr =
      png_transform_cast(png_transform_background, *transform);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   const png_const_bytep ep = sp + PNG_TC_ROWBYTES(*tc) - 3U/*safety*/;

   debug(tc->bit_depth == 16U && tc->format == PNG_FORMAT_GA &&
         tr->st.ntrans == 2U);
   tc->format &= PNG_BIC_MASK(PNG_FORMAT_FLAG_ALPHA);
   tc->sp = dp;

   do
   {
      if (sp[2] == 0U && sp[3] == 0U) /* transparent */
         dp[0] = tr->st.background_pixel[0], dp[1] = tr->st.background_pixel[1];

      else
         dp[0] = sp[0], dp[1] = sp[1];

      dp += 2U;
      sp += 4U;
   } while (sp < ep);

   debug(sp == ep+3U);
#  undef png_ptr
}

static void
png_do_background_with_transparent_GAlbd(png_transformp *transform,
   png_transform_controlp tc)
   /* This is the low-bit-depth gray case, the input is 1, 2 or 4-bit per
    * channel gray-alpha.
    */
{
#  define png_ptr (tc->png_ptr)
   png_transform_background *tr =
      png_transform_cast(png_transform_background, *transform);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   const png_const_bytep ep = sp + PNG_TC_ROWBYTES(*tc);
   const unsigned int bit_depth = tc->bit_depth;
   const unsigned int mask = (1U << bit_depth) - 1U;
   const unsigned int back = tr->st.background_pixel[0] & mask;
   unsigned int opos, ob, inb;

   debug(bit_depth < 8U && tc->format == PNG_FORMAT_GA && tr->st.ntrans == 1U);
   tc->format &= PNG_BIC_MASK(PNG_FORMAT_FLAG_ALPHA);
   tc->sp = dp;

   ob = 0U;   /* output byte */
   opos = 0U; /* bit index of previous output pixel (counts down) */
   inb = 0U;  /* quiet a GCC 4.8.5 warning */

   for (;;)
   {
      /* The output is half the size of the input, so we need a new input byte
       * for every 4 bits of output:
       */
      if (opos == 0U || opos == 4U)
      {
         if (sp >= ep)
            break;

         inb = *sp++;
      }

      /* Move to the next *output* pixel, this wraps when bits is 0U: */
      opos = (opos - bit_depth) & 0x7U;

      /* Extract the whole input pixel to the low bits of a temporary: */
      {
         unsigned int pixel = inb >> ((opos*2U) & 0x7U);

         /* The alpha channel is second, check for a value of 0: */
         if ((pixel & mask)/* A component*/ == 0U)
            pixel = back;

         else
         {
            debug((pixel & mask) == mask);
            pixel = (pixel >> bit_depth) & mask; /* G component */
         }

         ob |= pixel << opos;
      }

      if (opos == 0U)
         *dp++ = PNG_BYTE(ob), ob = 0U;
   }

   if (opos != 0U)
      *dp++ = PNG_BYTE(ob);

   debug(sp == ep);
#  undef png_ptr
}

static void
png_do_background_with_transparent_RGBA8(png_transformp *transform,
   png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   png_transform_background *tr =
      png_transform_cast(png_transform_background, *transform);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   const png_const_bytep ep = sp + PNG_TC_ROWBYTES(*tc) - 3U/*safety*/;

   debug(tc->bit_depth == 8U && tc->format == PNG_FORMAT_RGBA &&
         tr->st.ntrans == 3U);
   tc->format &= PNG_BIC_MASK(PNG_FORMAT_FLAG_ALPHA);
   tc->sp = dp;

   do
   {
      if (sp[3] == 0U) /* transparent */
         memcpy(dp, tr->st.background_pixel, 3U);

      else
         memmove(dp, sp, 3U);

      dp += 3U;
      sp += 4U;
   } while (sp < ep);

   debug(sp == ep+3U);
#  undef png_ptr
}

static void
png_do_background_with_transparent_RGBA16(png_transformp *transform,
   png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   png_transform_background *tr =
      png_transform_cast(png_transform_background, *transform);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   const png_const_bytep ep = sp + PNG_TC_ROWBYTES(*tc) - 7U/*safety*/;

   debug(tc->bit_depth == 16U && tc->format == PNG_FORMAT_RGBA &&
         tr->st.ntrans == 6U);
   tc->format &= PNG_BIC_MASK(PNG_FORMAT_FLAG_ALPHA);
   tc->sp = dp;

   do
   {
      if (sp[6] == 0U && sp[7] == 0U) /* transparent */
         memcpy(dp, tr->st.background_pixel, 6U);

      else
         memmove(dp, sp, 6U);

      dp += 6U;
      sp += 8U;
   } while (sp < ep);

   debug(sp == ep+7U);
#  undef png_ptr
}

static void
png_init_background_transparent(png_transformp *transform,
   png_transform_controlp tc)
   /* Select the correct version of the above routines. */
{
#  define png_ptr (tc->png_ptr)
   png_transform_background *tr =
      png_transform_cast(png_transform_background, *transform);

   debug(tc->init == PNG_TC_INIT_FINAL /* never called in 'FORMAT' */ &&
         (tc->format & PNG_FORMAT_FLAG_ALPHA) != 0);

   /* Now we know the format on which processing will happen so it is possible
    * to generate the correct fill pixel value to use.
    */
   fill_background_pixel(tr, tc);
   tc->format &= PNG_BIC_MASK(PNG_FORMAT_FLAG_ALPHA);
   tc->invalid_info |= PNG_INFO_sBIT;
   tc->sBIT_R = tc->sBIT_G = tc->sBIT_B = tc->sBIT_A =
      png_check_byte(png_ptr, tc->bit_depth);

   if (!(tc->format & PNG_FORMAT_FLAG_COLOR))
   {
      if (tc->bit_depth == 8U)
         tr->tr.fn = png_do_background_with_transparent_GA8;

      else if (tc->bit_depth == 16U)
         tr->tr.fn = png_do_background_with_transparent_GA16;

      else /* low-bit-depth gray with alpha (not a PNG format!) */
         tr->tr.fn = png_do_background_with_transparent_GAlbd;
   }

   else /* color */
   {
      if (tc->bit_depth == 8U)
         tr->tr.fn = png_do_background_with_transparent_RGBA8;

      else
      {
         debug(tc->bit_depth == 16U);
         tr->tr.fn = png_do_background_with_transparent_RGBA16;
      }
   }
#  undef png_ptr
}

/* The calculated values below have the range 0..65535*65535, the output has the
 * range 0..65535, so divide by 65535.  Two approaches are given here, one
 * modifies the value in place, the other uses a more complex expression.  With
 * gcc on an AMD64 system the in-place approach is very slightly faster.
 *
 * The two expressions are slightly different in what they calculate but both
 * give the exact answer (verified by exhaustive testing.)
 *
 * The macro must be given a png_uint_32 variable (lvalue), normally an auto
 * variable.
 */
#ifndef PNG_COMPOSE_DIV_65535
#  ifdef PNG_COMPOSE_DIV_EXPRESSION_SUPPORTED
#     define PNG_COMPOSE_DIV_65535(v)\
         (v = ((v + (v>>16) + (v>>31) + 32768U) >> 16))
#  else
#     define PNG_COMPOSE_DIV_65535(v)\
         (v += v >> 16, v += v >> 31, v += 32768U, v >>= 16)
#  endif
#endif

static void
png_do_background_alpha_GA(png_transformp *transform, png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   png_transform_background *tr =
      png_transform_cast(png_transform_background, *transform);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   const png_const_bytep ep = sp + PNG_TC_ROWBYTES(*tc) - 3U/*safety*/;
   const unsigned int background = tr->st.background.gray;
   const int copy = (sp != dp);
   const int compose = tr->st.compose_background;

   affirm(tc->bit_depth == 16U && tc->format == PNG_FORMAT_GA &&
         tr->st.background_bit_depth == 16U);

   /* If gamma transforms are eliminated this might fail: */
   debug(tr->st.background_gamma == tc->gamma ||
         tr->st.background_gamma == 0 ||
         tc->sBIT_G == 1);

   tc->sp = tc->dp; /* nothing else changes */

   do
   {
      const png_uint_32 alpha = (sp[2] << 8) + sp[3];

      switch (alpha)
      {
         case 0U: /* transparent */
            memset(dp, 0U, 4U);
            break;

         default:
            {
               png_uint_32 v = ((sp[0] << 8) + sp[1]) * alpha +
                  background * (65535U - alpha);

               PNG_COMPOSE_DIV_65535(v);
               debug(v <= 65535U);
               dp[0] = PNG_BYTE(v >> 8);
               dp[1] = PNG_BYTE(v);
            }

            if (compose)
               dp[3] = dp[2] = 0xFFU; /* alpha; set to 1.0 */

            else if (copy)
            {
               dp[2] = PNG_BYTE(alpha >> 8);
               dp[3] = PNG_BYTE(alpha);
            }
            break;

         case 65535U: /* opaque */
            if (copy)
               memcpy(dp, sp, 4U);
            break;
      }

      sp += 4U;
      dp += 4U;
   }
   while (sp < ep);

   debug(sp == ep+3U);
#  undef png_ptr
}

static void
png_do_background_alpha_RGBA(png_transformp *transform,
   png_transform_controlp tc)
{
#  define png_ptr (tc->png_ptr)
   png_transform_background *tr =
      png_transform_cast(png_transform_background, *transform);
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   const png_const_bytep ep = sp + PNG_TC_ROWBYTES(*tc) - 7U/*safety*/;
   const unsigned int bred = tr->st.background.red;
   const unsigned int bgreen = tr->st.background.green;
   const unsigned int bblue = tr->st.background.blue;
   const int copy = (sp != dp);
   const int compose = tr->st.compose_background;

   affirm(tc->bit_depth == 16U && tc->format == PNG_FORMAT_RGBA &&
         tr->st.background_bit_depth == 16U);

   debug(tr->st.background_gamma == tc->gamma ||
         tr->st.background_gamma == 0 ||
         (tc->sBIT_R == 1 && tc->sBIT_G == 1 && tc->sBIT_B == 1));

   tc->sp = tc->dp; /* nothing else changes */

   do
   {
      const png_uint_32 alpha = (sp[6] << 8) + sp[7];

      switch (alpha)
      {
         case 0U: /* transparent */
            memset(dp, 0U, 8U);
            break;

         default:
            {
               const png_uint_32 balpha = (65535U - alpha);
               png_uint_32 r = ((sp[0] << 8) + sp[1]) * alpha + bred * balpha;
               png_uint_32 g = ((sp[2] << 8) + sp[3]) * alpha + bgreen * balpha;
               png_uint_32 b = ((sp[4] << 8) + sp[5]) * alpha + bblue * balpha;

               PNG_COMPOSE_DIV_65535(r);
               PNG_COMPOSE_DIV_65535(g);
               PNG_COMPOSE_DIV_65535(b);
               debug(r <= 65535U && g <= 65535U && b <= 65535U);
               dp[0] = PNG_BYTE(r >> 8);
               dp[1] = PNG_BYTE(r);
               dp[2] = PNG_BYTE(g >> 8);
               dp[3] = PNG_BYTE(g);
               dp[4] = PNG_BYTE(b >> 8);
               dp[5] = PNG_BYTE(b);
            }

            if (compose)
               dp[7] = dp[6] = 0xFFU;

            else if (copy)
            {
               dp[6] = PNG_BYTE(alpha >> 8);
               dp[7] = PNG_BYTE(alpha);
            }
            break;

         case 65535U: /* opaque */
            if (copy)
               memcpy(dp, sp, 8U);
            break;
      }

      sp += 8U;
      dp += 8U;
   }
   while (sp < ep);

   debug(sp == ep+7U);
#  undef png_ptr
}

static void
png_init_background_alpha_end(png_transformp *transform,
   png_transform_controlp tc)
   /* This is just the last part of png_init_background_alpha (below) */
{
#  define png_ptr (tc->png_ptr)
   png_transform_background *tr =
      png_transform_cast(png_transform_background, *transform);

   debug(tc->init == PNG_TC_INIT_FINAL);

   /* Repeat the tests at the end of png_init_background_alpha: */
   affirm(tc->bit_depth == 16U && (tc->format & PNG_FORMAT_FLAG_ALPHA) != 0);
   debug(tc->gamma == 0 ||
         !png_gamma_significant(png_ptr, tc->gamma, tc_sBIT(tc)));

   /* tr->st.background_is_gray was filled in by resolve_background_color and
    * records if either the background was a gray value or it was a color
    * value with all the channels equal.
    */
   if (!tr->st.background_is_gray && !(tc->format & PNG_FORMAT_FLAG_COLOR))
   {
#     ifdef PNG_READ_GRAY_TO_RGB_SUPPORTED
         /* Color background with gray data: this happens when there is a
          * gray to RGB transform in the pipeline but it hasn't happened
          * yet.  Unfortunately it has to happen now to be able to do the
          * compose against the colored background.
          */
         png_push_gray_to_rgb_byte_ops(transform, tc);
         affirm((tc->format & PNG_FORMAT_FLAG_COLOR) != 0);
         return;
#     else /* !GRAY_TO_RGB */
         impossible("gray to RGB"); /* how can this happen? */
#     endif /* !GRAY_TO_RGB */
   }

   /* The transform happens in two parts, a part to do the arithmetic on
    * pixels where it is required followed by a part to replace transparent
    * pixels.  These two parts require different versions of the background
    * pixel.  Set up the second part first.
    *
    * This only happens with background composition, otherwise the
    * transparent pixels are already 0 and nothing needs to be done.
    */
   if (tr->st.compose_background)
   {
      /* The transparent pixel handling happens *after* the data has been
       * re-encoded to the output gamma:
       */
      png_transform_background *tr_alpha =
         png_transform_cast(png_transform_background,
            png_add_transform(png_ptr, sizeof (png_transform_background),
               png_init_background_transparent, PNG_TR_GAMMA_ENCODE+0xF0U));

      /* Copy the current state into the new png_transform_background: */
      tr_alpha->st = tr->st;
      tr_alpha->tr.args = tr->tr.args;
   }

   /* Now it is possible to overwrite tr->st.background with the linear version.
    */
   gamma_correct_background(tr, tc);

   /* sBIT informationmust also be invalidated here, because a gamma
    * transform may run before the transparent pixel handling.
    */
   tc->invalid_info |= PNG_INFO_sBIT;
   tc->sBIT_R = tc->sBIT_G = tc->sBIT_B = tc->sBIT_A =
      png_check_byte(png_ptr, tc->bit_depth);

   /* And select an appropriate function; there are only two choices: */
   switch (tc->format)
   {
      case PNG_FORMAT_GA:
         /* If the background format is color this indicates that there is a
          * gray to RGB transform missing and we need it to happen before
          * this point!
          */
         affirm(tr->st.background_is_gray);
         tr->tr.fn = png_do_background_alpha_GA;
         break;

      case PNG_FORMAT_RGBA:
         if (tr->st.background_is_gray)
            tr->st.background.blue = tr->st.background.green =
               tr->st.background.red = tr->st.background.gray;
         tr->tr.fn = png_do_background_alpha_RGBA;
         break;

      default:
         NOT_REACHED;
   }
#  undef png_ptr
}

static void
png_init_background_alpha(png_transformp *transform, png_transform_controlp tc)
   /* This is used when alpha composition is required because the alpha channel
    * may contain values that are between 0 and 1.  Because doing alpha
    * composition requires linear arithmetic the data is converted to 16-bit
    * linear, however this means that the background pixel gets converted too
    * and, for 16-bit output, this tends to smash the value.  Consequently the
    * algorithm used here is to skip those pixels and use the 'transparent
    * alpha' routines to replace them after the gamma correction step.
    */
{
#  define png_ptr (tc->png_ptr)
   png_transform_background *tr =
      png_transform_cast(png_transform_background, *transform);

   debug(tc->init == PNG_TC_INIT_FINAL);
   /* png_init_background ensures this is true: */
   debug((tc->format & PNG_FORMAT_FLAG_ALPHA) != 0);

   /* Always push gamma transforms; don't try to optimize the case when they
    * aren't needed because that would be an attempt to duplicate the tests in
    * png_init_gamma and it might now work reliably.
    *
    * Need to push the to-linear transform *before* this transform and add gamma
    * correction afterward to get back to the screen format.  Do the afterward
    * bit first to avoid complexity over *transform:
    */
   {
      png_transform_gamma *tr_end = add_gamma_transform(png_ptr,
         PNG_TR_GAMMA_ENCODE, tc->gamma, 0U/*bit depth*/, 0/*default*/);

      /* Encoding the alpha channel happens in the last step, so this needs to
       * be set here.  Notice that in C++ terms we are very friendly with
       * png_transform_gamma.
       */
      tr_end->encode_alpha = tr->st.encode_alpha;
      tr_end->optimize_alpha = tr->st.optimize_alpha;
   }

   {
      /* Now add tr_gamma before this transform, expect it to go in at
       * *transform or the whole thing won't work:
       */
      png_transform_gamma *tr_gamma = png_transform_cast(png_transform_gamma,
         png_push_transform(png_ptr, sizeof (png_transform_gamma),
            png_init_gamma, transform, NULL/*don't run init*/));

      /* This must happen before we run png_gamma_init: */
      tr_gamma->to_gamma = PNG_FP_1;
      tr_gamma->to_bit_depth = 16U;

      /* Now run the this transform; it was pushed before this one, so it gets
       * to do its init first and this function must return as the caller will
       * immediately call here again.
       */
      debug(*transform == &tr_gamma->tr);
      png_init_gamma(transform, tc);
      affirm(tc->bit_depth == 16U &&
             (tc->format & PNG_FORMAT_FLAG_ALPHA) != 0);
      /* This is only a 'debug' because it needs to replicate the test in
       * png_init_gamma and that is easy to get wrong (a harmless mistake).
       */
      debug(tc->gamma == 0 ||
            !png_gamma_significant(png_ptr, tc->gamma, tc_sBIT(tc)));
   }

   /* A transform was pushed, so this transform init will be run again: */
   tr->tr.fn = png_init_background_alpha_end;
#  undef png_ptr
}

/* Handle alpha and tRNS via a background color */
static void
png_init_background(png_transformp *transform, png_transform_controlp tc)
{
   /* This init function is called right at the start, this means it can get at
    * the tRNS values if appropriate.  If not the RGB to gray transform comes
    * next followed by PNG_TR_COMPOSE_ALPHA, which actually does the non-tRNS
    * work.
    */
   png_structp png_ptr = tc->png_ptr;
   png_transform_background *tr =
      png_transform_cast(png_transform_background, *transform);

   if (tc->init == PNG_TC_INIT_FORMAT)
   {
      /* Background composition removes the alpha channel, so the other
       * operations become irrelevant:
       */
      if (tr->st.compose_background)
         tr->st.associate_alpha = tr->st.encode_alpha = tr->st.optimize_alpha =
            0U;

      else if (!tr->st.associate_alpha)
      {
         /* There is nothing to do, delete the whole transform. */
         tr->tr.fn = NULL;
         return;
      }

      /* Else alpha association ('pre-multiplication') which is achieved by
       * composing on a 0 background.  The background color will be black (all
       * zeros) and the background gamma will be zero.
       */

      /* Because we are in PNG_TC_INIT_FORMAT no other transforms will have been
       * inserted between this one and an rgb-to-gray transform, so we can find
       * out if rgb-to-gray has been requested:
       */
      tr->st.rgb_to_gray = tr->tr.next != NULL &&
         tr->tr.next->order == PNG_TR_RGB_TO_GRAY;

      if ((tc->format & PNG_FORMAT_FLAG_ALPHA) != 0)
      {
         /* Associated alpha does not strip the alpha channel! */
         if (tr->st.compose_background)
            tc->format &= PNG_BIC_MASK(PNG_FORMAT_FLAG_ALPHA);
      }

      else if (!tc->palette &&
         png_ptr->num_trans == 1 && !(tc->invalid_info & PNG_INFO_tRNS))
      {
         /* tRNS will be expanded, or handled */
         tc->invalid_info |= PNG_INFO_tRNS;
         if (!tr->st.compose_background)
         {
            tc->format |= PNG_FORMAT_FLAG_ALPHA;
            /* And in this case, only, because we are adding an alpha channel we
             * need to have a channel depth of at least 8:
             */
            if (tc->bit_depth < 8U)
               tc->bit_depth = 8U;
         }
      }

      else /* no transparent pixels to change */
         tr->tr.fn = NULL;
   }

   else /* PNG_TC_INIT_FINAL */
   {
      png_fixed_point correction;

      debug(tc->init == PNG_TC_INIT_FINAL &&
            ((tc->format & PNG_FORMAT_FLAG_ALPHA) != 0 ||
             (!tc->palette && png_ptr->num_trans == 1 &&
              !(tc->invalid_info & PNG_INFO_tRNS))));

      /* The screen gamma is known, so the background gamma can be found, note
       * that both the gamma values used below will be 0 if no gamma information
       * was in the PNG and no gamma information has been provided by
       * png_set_gamma or png_set_alpha_mode.
       */
      switch (tr->st.background_gamma)
      {
         case PNG_BACKGROUND_GAMMA_FILE:
            /* png_init_transform_control has already found the file gamma,
             * and because this is the first arithmetic transformation
             * nothing has changed it.
             */
            tr->st.background_gamma = tc->gamma;
            break;

         case PNG_BACKGROUND_GAMMA_SCREEN:
            tr->st.background_gamma = png_ptr->row_gamma;
            break;

         default:
            /* already set */
            break;
      }

      /* Work out what the background color is, this only depends on 'tc' for
       * palette information, so it can be done now before we know the actual
       * bit_depth/format that will be required:
       */
      resolve_background_color(tr, tc);

      /* Is this format compatible with the current row data?  If it is then it
       * is possible to avoid the arithmetic if no alpha processing is required.
       * This is a useful optimization because PNG files with just transparent
       * pixels and no alpha are common.
       *
       * NOTE: if an RGB-to-gray transform is present this is fine so long as
       * the background is gray, otherwise (non-gray background) there is a
       * following gray-to-RGB transform and the now gray image must be
       * composited on a color background.
       */
      if (tr->st.compose_background /* alpha channel stripped */ &&
          (tr->st.background_is_gray ||
           ((tc->format & PNG_FORMAT_FLAG_COLOR) != 0 && !tr->st.rgb_to_gray))
            /* color compatible */ &&
          tc->bit_depth >= tr->st.background_bit_depth
            /* bit depth compatible */ &&
          (tc->transparent_alpha ||
           (!tc->palette && png_ptr->num_trans == 1 &&
            !(tc->invalid_info & PNG_INFO_tRNS)))
            /* no alpha processing */ &&
          png_gamma_equal(png_ptr, tc->gamma, png_ptr->row_gamma, &correction,
             tc->bit_depth) /* gamma compatible (so no gamma processing) */)
      {
         /* How the operation gets performed depends on whether the current data
          * has an alpha channel or not.
          */
         if ((tc->format & PNG_FORMAT_FLAG_ALPHA) != 0)
         {
            affirm(tc->transparent_alpha);
            /* This init routine does the sBIT handling: */
            png_init_background_transparent(transform, tc);
         }

         else if (!tc->palette && png_ptr->num_trans == 1 &&
            !(tc->invalid_info & PNG_INFO_tRNS))
         {
            /* The background pixel needs to be filled in now; no more init
             * routines are called in this case.  It is important to delay this
             * as late as possible because it needs to know the actual tc format
             * that must be used.
             */
            fill_background_pixel(tr, tc);

            debug(!(png_ptr->color_type & PNG_COLOR_MASK_PALETTE));

            /* The pixel depth should not have been changed yet: */
            debug(PNG_PIXEL_DEPTH(*png_ptr) == PNG_TC_PIXEL_DEPTH(*tc));

            /* The transparent_pixel value needs to be filled in. */
            affirm(tr->st.ntrans ==
               fill_transparent_pixel(png_ptr, tr->st.transparent_pixel));

            /* The whole operation is a no-op if the transparent pixel and the
             * background pixel match, even in the associated alpha case where
             * both will be 0 throughout.
             *
             * NOTE: for palette images this test happens in the caching
             * operation, so the answer is still correct.
             *
             * NOTE: for low bit depth gray both 'transparent_pixel' and
             * 'background_pixel' have been expanded to fill a byte, so this
             * works.
             */
            if (memcmp(tr->st.transparent_pixel, tr->st.background_pixel,
                     tr->st.ntrans) == 0)
               tr->tr.fn = NULL;

            /* Then the processing function depends on the pixel size: */
            else if (tr->st.ntrans > 1U)
               tr->tr.fn = png_do_replace_tRNS_multi;

            else if (tc->bit_depth == 8U)
               tr->tr.fn = png_do_replace_tRNS_8;

            else if (tc->bit_depth == 1U)
            {
               /* This is the silly case: the replacement pixel does not match
                * the transparent pixel (handled above) so either all the '0'
                * bits are replaced by '1' or all the '1' bits are replaced by
                * '0':
                */
               png_uint_32 args = tr->st.background_pixel[0];

               args <<= 24;
               args |= PNG_INFO_tRNS | PNG_INFO_sRGB;
               tr->tr.args = args;
               tr->tr.fn = png_do_set_row;
            }

            else
               tr->tr.fn = png_do_replace_tRNS_lbd;

            tc->invalid_info |= PNG_INFO_tRNS | PNG_INFO_sBIT;
            tc->sBIT_R = tc->sBIT_G = tc->sBIT_B = tc->sBIT_A =
               png_check_byte(png_ptr, tc->bit_depth);
         }

         else
         {
            /* Nothing to do; should have been eliminated before! */
            tr->tr.fn = NULL;
            NOT_REACHED;
         }
      }

      else /* alpha, or maybe gamma, processing required */
      {
         /* Alpha case, add an appropriate transform; this has to be done
          * *after* the RGB-to-gray case so move the transform info there:
          */
         png_transform_background *tr_alpha =
            png_transform_cast(png_transform_background,
               png_add_transform(png_ptr, sizeof (png_transform_background),
                  png_init_background_alpha, PNG_TR_COMPOSE_ALPHA));

         /* Copy the current state into the new png_transform_background: */
         tr_alpha->st = tr->st;
         tr_alpha->tr.args = tr->tr.args;

         /* The rest of the init occurs later; this transform is no longer
          * needed.
          */
         tr->tr.fn = NULL;

         /* Ensure that png_init_background_alpha gets an alpha channel, this
          * needs to happen here because otherwise intervening transforms can
          * invalidate tRNS.
          */
         tc->expand_tRNS = 1U;
         if (tr->st.compose_background)
            tc->strip_alpha = 0U;

         /* And push the expand: */
         (void)push_gamma_expand(transform, tc, 1/*need alpha*/);

         /* Regardless of whether anything got pushed the following should now
          * be true:
          */
         affirm((tc->format & PNG_FORMAT_FLAG_ALPHA) != 0 &&
                tc->bit_depth >= 8U);
      }
   }
}

void PNGFAPI
png_set_background_fixed(png_structrp png_ptr,
    png_const_color_16p background_color, int background_gamma_code,
    int need_expand, png_fixed_point background_gamma)
{
   if (png_ptr != NULL)
   {
      if (background_color != NULL)
      {
         png_transform_background *tr =
            png_transform_cast(png_transform_background,
               png_add_transform(png_ptr, sizeof (png_transform_background),
                  png_init_background, PNG_TR_COMPOSE));

         /* This silently overwrites the information if png_set_background is
          * called more than once.
          */
         tr->st.background = *background_color;
         tr->st.need_expand = need_expand != 0;
         tr->st.compose_background = 1U; /* png_set_background called */
         switch (background_gamma_code)
         {
            case PNG_BACKGROUND_GAMMA_SCREEN:
            case PNG_BACKGROUND_GAMMA_FILE:
               tr->st.background_gamma = background_gamma_code;
               break;

            case PNG_BACKGROUND_GAMMA_UNIQUE:
               if (background_gamma >= 16 && background_gamma <= 625000000)
               {
                  tr->st.background_gamma = background_gamma;
                  break;
               }

               png_app_error(png_ptr, "gamma value out of range");
               /* FALL THROUGH */
            default:
               png_app_error(png_ptr, "invalid gamma information");
               tr->st.background_gamma = (need_expand ?
                  PNG_BACKGROUND_GAMMA_FILE : PNG_BACKGROUND_GAMMA_SCREEN);
               break;
         }
      }

      else
         png_app_error(png_ptr, "missing background color");
   }
}

#  ifdef PNG_FLOATING_POINT_SUPPORTED
void PNGAPI
png_set_background(png_structrp png_ptr,
    png_const_color_16p background_color, int background_gamma_code,
    int need_expand, double background_gamma)
{
   png_set_background_fixed(png_ptr, background_color, background_gamma_code,
      need_expand, png_fixed(png_ptr, background_gamma, "png_set_background"));
}
#  endif  /* FLOATING_POINT */
#endif /* READ_BACKGROUND */

#ifdef PNG_READ_ALPHA_MODE_SUPPORTED
void PNGFAPI
png_set_alpha_mode_fixed(png_structrp png_ptr, int mode,
    png_fixed_point output_gamma)
{
   if (png_ptr != NULL)
   {
      /* Check the passed in output_gamma value; it must be valid and it must be
       * converted to the reciprocal for use below:
       */
      output_gamma = translate_gamma_flags(png_ptr, output_gamma, 1/*screen*/);

      if (output_gamma > 0) /* Else an app_error has been signalled. */
      {
         /* Only set the colorspace gamma if it has not already been set (this
          * has the side effect that the gamma in a second call to
          * png_set_alpha_mode will be ignored.)
          */
         if ((png_ptr->colorspace.flags &
              (PNG_COLORSPACE_INVALID | PNG_COLORSPACE_HAVE_GAMMA)) !=
              PNG_COLORSPACE_HAVE_GAMMA)
         {
            /* The default file gamma is the output gamma encoding: */
            png_ptr->colorspace.gamma = output_gamma;
            if (png_ptr->colorspace.flags & PNG_COLORSPACE_INVALID)
               png_ptr->colorspace.flags = PNG_COLORSPACE_HAVE_GAMMA;
            else
               png_ptr->colorspace.flags |= PNG_COLORSPACE_HAVE_GAMMA;
         }

         /* Always set the output gamma, note that it may be changed to PNG_FP_1
          * for the associated alpha support.  This means that the last call to
          * png_set_gamma[_fixed] or png_set_alpha_mode sets the output gamma,
          * which is probably what is expected.
          */
         {
            png_transform_gamma *tr_gamma = add_gamma_transform(png_ptr,
               PNG_TR_GAMMA_ENCODE,
               mode == PNG_ALPHA_ASSOCIATED ? PNG_FP_1 : output_gamma, 0U,
               1/*force*/);

            /* Get a background transform and set the appropriate fields.
             *
             * png_set_background removes the alpha channel so it effectively
             * disbles png_set_alpha_mode however png_set_alpha_mode is still
             * useful to set a default gamma value.
             */
            png_transform_background *tr =
               png_transform_cast(png_transform_background,
                  png_add_transform(png_ptr, sizeof (png_transform_background),
                     png_init_background, PNG_TR_COMPOSE));

            /* There are really 8 possibilities here, composed of any
             * combination of:
             *
             *    premultiply the color channels
             *    do not encode non-opaque pixels (leave as linear)
             *    encode the alpha as well as the color channels
             *
             * The differences disappear if the input/output ('screen') gamma is
             * 1.0,  because then the encoding is a no-op and there is only the
             * choice of premultiplying the color channels or not.
             */
            switch (mode)
            {
               case PNG_ALPHA_PNG:        /* default: png standard */
                  /* No compose, but it may be set by png_set_background!  This
                   * is the only mode that doesn't interfere with what
                   * png_set_background does.
                   */
                  tr->st.associate_alpha = 0U;
                  tr_gamma->encode_alpha = tr->st.encode_alpha = 0U;
                  tr_gamma->optimize_alpha = tr->st.optimize_alpha = 0U;
                  break;

               case PNG_ALPHA_ASSOCIATED: /* color channels premultiplied */
                  tr->st.associate_alpha = 1U;
                  tr_gamma->encode_alpha = tr->st.encode_alpha = 0U;
                  tr_gamma->optimize_alpha = tr->st.optimize_alpha = 0U;
                  break;

               case PNG_ALPHA_OPTIMIZED:
                  /* associated with opaque pixels having the given gamma and
                   * non-opaque pixels being linear.
                   */
                  tr->st.associate_alpha = 1U;
                  tr_gamma->encode_alpha = tr->st.encode_alpha = 0U;
                  tr_gamma->optimize_alpha = tr->st.optimize_alpha = 1U;
                  /* output_gamma records the encoding of opaque pixels! */
                  break;

               case PNG_ALPHA_BROKEN:
                  /* associated+non-linear+alpha encoded */
                  tr->st.associate_alpha = 1U;
                  tr_gamma->encode_alpha = tr->st.encode_alpha = 1U;
                  tr_gamma->optimize_alpha = tr->st.optimize_alpha = 0U;
                  break;

               default:
                  png_app_error(png_ptr, "invalid alpha mode");
                  /* A return at this point is safe; if a background transform
                   * was created the init routine will remove it because
                   * nothing is set.
                   */
                  break;
            } /* alpha mode switch */
         } /* add gamma and background transforms */
      } /* valid output gamma */
   } /* png_ptr != NULL */
}

#ifdef PNG_FLOATING_POINT_SUPPORTED
void PNGAPI
png_set_alpha_mode(png_structrp png_ptr, int mode, double output_gamma)
{
   png_set_alpha_mode_fixed(png_ptr, mode, convert_gamma_value(png_ptr,
       output_gamma));
}
#endif /* FLOATING_POINT */
#endif /* READ_ALPHA_MODE */

#ifdef PNG_READ_TRANSFORMS_SUPPORTED
typedef struct
{
   png_transform         tr;
   png_transform_control tc;
   union
   {
      png_uint_32        u32[1]; /* ensure alignment */
      png_uint_16        u16[1];
      png_byte           b8[1];
   }  cache;
}  png_transform_cache;

#define png_transform_cache_size(size)\
   (offsetof(png_transform_cache, cache)+(size))
#define png_transform_cache_cast(pointer,size)\
   png_voidcast(png_transform_cache*,\
      png_transform_cast_check(png_ptr, PNG_SRC_LINE, (pointer),\
         png_transform_cache_size(size)))
   /* This is like png_transform_cast except that 'size' is the size of the
    * cache part in the above structure and the type returned is always
    * 'png_transform_cache*'.
    */

/* Functions to handle the cache operation.  These don't do any initialization;
 * that happens below when PNG_TC_INIT_FINAL is being run on the whole list.
 * These functions are only implemented for read so the transform control
 * source and destination are always aligned.
 *
 * First some utility functions:
 */
static void
png_transform_control_cp(png_transform_controlp tcDest,
   png_const_transform_controlp tcSrc)
{
   /* Copy tcSrc over tcDest without overwriting the information specific to the
    * row being transformed.
    */
   png_structp     png_ptr = tcDest->png_ptr;
   png_const_voidp sp      = tcDest->sp;
   png_voidp       dp      = tcDest->dp;
   png_uint_32     width   = tcDest->width;
   unsigned int    init    = tcDest->init;

   *tcDest = *tcSrc;

   tcDest->png_ptr = png_ptr;
   tcDest->sp      = sp;
   tcDest->dp      = dp;
   tcDest->width   = width;
   tcDest->init    = png_check_bits(tcDest->png_ptr, init, 2);
}

#if !PNG_RELEASE_BUILD
static int
png_transform_control_eq(png_const_transform_controlp tc1,
   png_const_transform_controlp tc2)
{
   /* Say if *tc1 == *tc2, ignoring differences in uncopied fields and 'cost':
    */
   return
#     ifdef PNG_READ_GAMMA_SUPPORTED
         tc1->gamma == tc2->gamma &&
#     endif
      tc1->format == tc2->format &&
      tc1->range == tc2->range &&
      tc1->bit_depth == tc2->bit_depth &&
      tc1->caching == tc2->caching &&
      tc1->palette == tc2->palette;
      /* invalid_info, cost, interchannel and channel_add are only set during
       * init, so don't do the compare.
       */
}
#endif /* !RELEASE_BUILD */

/* Now the routines that actually perform the transform.  There are two basic
 * cases:
 *
 * 1) A cached transform that does not change the pixel size and where the pixel
 *    size 8 bits or less.  This can be done by a 256-entry single byte lookup
 *    table, regardless of the bit depth.  Two versions of the code exist, one
 *    which just transforms the row, the other which transforms and records the
 *    maximum pixel depth.
 *
 * 2) A cached transform that increases pixel depth.  The destination pixel
 *    depth will always be a multiple of 8 bits, the source pixel will be less
 *    than or equal to 8 bits and will be in the PNG native (big endian) layout.
 */
#define png_ptr (tc->png_ptr) /* Used in all functions below */
/* (1): single-byte cached transforms: */
static void
do_transform_cache_byte(png_transformp *trIn, png_transform_controlp tc)
{
   png_transform_cache *tr = png_transform_cache_cast(*trIn, 256U);

   /* Copy the bytes through the 256-byte LUT: */
   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_const_bytep ep = dp + PNG_TC_ROWBYTES(*tc);
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);

   tc->sp = dp;

   do
      *dp++ = tr->cache.b8[*sp++];
   while (dp < ep);

   png_transform_control_cp(tc, &tr->tc);
}

/* (2) A cached transform that increases pixel depth.
 *
 * There are six output depth possibilites, all a whole number of bytes:
 *
 *    1 byte,   8 bits: palette or grayscale
 *    2 bytes, 16 bits: 16-bit grayscale or 8-bit gray+alpa
 *    3 bytes, 24 bits: 8-bit RGB
 *    4 bytes, 32 bits: 16-bit gray+alpha or 8-bit RGBA
 *    6 bytes, 48 bits: 16-bit RGB
 *    8 bytes, 64 bits: 16-bit RGBA
 *
 * The input must be 1, 2, 4 or 8-bit gray or palette.  The first 1-byte case is
 * handled for 8-bit gray/palette above, so there are 22 possibilities.  The
 * function names below are:
 *
 *    do_transform_cache_<input-bits>_<output-bits>
 */
#define transform_cache_size(ipd,opd) ((((1U << (ipd)) * (opd))+7U) >> 3)
static void
do_transform_cache_(png_transformp *trIn, png_transform_controlp tc,
   unsigned int ipd, unsigned int opd)
   /* This is the implementation for unknown ipd, opd, below it is called with
    * fixed values.  The purpose of this is to allow the compiler/system builder
    * to decide how to optimize for size vs space vs speed.  Note that this
    * implementation, while it would work for 8 bit ipd, is not used in that
    * case.
    */
{
   png_transform_cache *tr =
      png_transform_cache_cast(*trIn, transform_cache_size(ipd, opd));

   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_const_bytep ep = dp;
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);
   unsigned int s, shift, mask;

   sp += PNG_TC_ROWBYTES(*tc); /* One byte beyond the end */

   png_transform_control_cp(tc, &tr->tc);
   dp += PNG_TC_ROWBYTES(*tc);

   shift = 7U & -(tc->width * ipd);
      /* MSB: shift right required to get last pixel */
   mask = (1U << ipd) - 1U;
      /* Mask to extract a single pixel from the low bits of a byte */
   opd >>= 3;
      /* Output pixel size in bytes */
   s = *--sp;
      /* The first byte; the last byte of the input row */

   for (;;)
   {
      png_const_bytep opixel = (((s >> shift) & mask)+1U) * opd + tr->cache.b8;
         /* Points to the byte after last byte of the output value */
      unsigned int i;

      for (i=0; i<opd; ++i)
         *--dp = *--opixel;

      if (dp <= ep)
         break;

      shift += ipd; /* To find shift for *previous* pixel */

      if (shift == 8U)
         s = *--sp, shift = 0U/*right-most pixel*/;
   }

   debug(dp == ep && shift == 8U-ipd && sp == tc->sp);
   tc->sp = ep; /* start of row, safe even if the above fails */
}

#define do_transform_cache(ipd,opd)\
static void \
do_transform_cache_##ipd##_##opd(png_transformp *tr, png_transform_controlp tc)\
{\
   do_transform_cache_(tr, tc, ipd, opd);\
}

#define TCLOW(opd)\
do_transform_cache(1,opd)\
do_transform_cache(2,opd)\
do_transform_cache(4,opd)

TCLOW(8)
TCLOW(16)
TCLOW(24)
TCLOW(32)
TCLOW(48)
TCLOW(64)

#undef TCLOW
#undef do_transform_cache

static void
do_transform_cache_8_(png_transformp *trIn, png_transform_controlp tc,
   unsigned int opd)
   /* This is the 8-bit input implementation. */
{
   png_transform_cache *tr =
      png_transform_cache_cast(*trIn, transform_cache_size(8, opd));

   png_bytep dp = png_voidcast(png_bytep, tc->dp);
   png_const_bytep ep = dp;
   png_const_bytep sp = png_voidcast(png_const_bytep, tc->sp);

   sp += PNG_TC_ROWBYTES(*tc); /* One byte beyond the end */

   png_transform_control_cp(tc, &tr->tc);
   dp += PNG_TC_ROWBYTES(*tc);

   opd >>= 3; /* Output pixel size in bytes */
   do
   {
      png_const_bytep opixel = (*--sp + 1U) * opd + tr->cache.b8;
         /* Points to the byte after last byte of the output value */
      unsigned int i;

      for (i=0; i<opd; ++i)
         *--dp = *--opixel;
   }
   while (dp > ep);

   debug(dp == ep && sp == tc->sp);
   tc->sp = ep; /* start of row, safe even if the above fails */
}

#define do_transform_cache(opd)\
static void \
do_transform_cache_8_##opd(png_transformp *tr, png_transform_controlp tc)\
{\
   do_transform_cache_8_(tr, tc, opd);\
}

/* The 8-bit to 8-bit case uses the byte transform code */
do_transform_cache(16)
do_transform_cache(24)
do_transform_cache(32)
do_transform_cache(48)
do_transform_cache(64)

#undef do_transform_cache

#define do_transform_cache(ipd,opd) do_transform_cache_##ipd##_##opd

#undef png_ptr

typedef struct
{
   png_transformp *start;
      /* This is a pointer to the pointer to the start of the list being cached,
       * i.e. *start is the first transform in the list.
       */
   png_transform_control tstart;
      /* This is the transform control at the start; i.e. before (*start)->fn is
       * called.  Note that for palette data it will contain the original
       * palette format/bit-depth, not that passed to (*start)->fn which will
       * represent the palette.
       */
   png_transformp *end;
   png_transform_control tend;
      /* The same data from the end of the run to be cached, i.e. after the
       * function of the transform which *contains* '*end' (end points to
       * tr->next).
       */
}  png_cache_params, *png_cache_paramsp;

static void
init_caching(png_structp png_ptr, png_transform_controlp tend)
   /* Given an already initialized tend turn on caching if appropriate. */
{
   /* Handle the colormap case, where a cache is always required: */
   if (tend->format & PNG_FORMAT_FLAG_COLORMAP)
   {
      /* This turns starts the palette caching with the next transform: */
      tend->palette = tend->caching = 1U;
#     ifdef PNG_READ_tRNS_SUPPORTED
         tend->transparent_alpha = png_ptr->transparent_palette;
#     else /* !READ_tRNS */
         tend->transparent_alpha = 0;
         PNG_UNUSED(png_ptr)
#     endif /* !READ_tRNS */
      tend->format = PNG_FORMAT_FLAG_COLOR;
#     ifdef PNG_READ_tRNS_SUPPORTED
         if (png_ptr->num_trans > 0 && !(tend->invalid_info & PNG_INFO_tRNS))
         {
            tend->format |= PNG_FORMAT_FLAG_ALPHA;
         }
#     endif /* READ_tRNS */
      tend->bit_depth = 8U;
   }

   else if (PNG_TC_PIXEL_DEPTH(*tend) <= 8)
   {
      /* Cacheable pixel transforms; the pixel is less than 8 bits in size so
       * the cache makes sense.
       *
       * TODO: check the cost estimate and the image size to avoid expensive
       * caches of very small images.
       */
      tend->caching = 1U;
   }

   /* TODO: handle handle 8-bit GA/RGB/RGBA */
}

static void
add_cache_transform(png_structp png_ptr, unsigned int order,
   png_transform_fn fn, png_cache_paramsp cp,
   png_const_bytep cache, unsigned int size)
   /* Add a transform from the input format cp->tstart to the output format
    * stored in cp->tend.
    */
{
   affirm(size <= 2048U); /* 256 8-byte pixels at most */
   {
      png_transform_cache *tr = png_transform_cache_cast(
         png_add_transform(png_ptr, png_transform_cache_size(size), fn, order),
            size);

      /* This must have replaced the transform in *cp->start: */
      affirm(&tr->tr == *cp->start);

      /* Fill in the respective members: */
      tr->tc = cp->tend;
      memcpy(tr->cache.b8, cache, size);

      /* Skip this transform, because the calling routine has already executed
       * the cache (it could be executed again, just to verify that it works;
       * cp->tstart should be correct.)
       */
      cp->start = &tr->tr.next;
   }
}

static unsigned int
setup_palette_cache(png_structp png_ptr, png_byte cache[8*256])
   /* This returns the number of entries in the cache; the width */
{
   const unsigned int num_palette = png_ptr->num_palette;
#  ifdef PNG_READ_tRNS_SUPPORTED
      unsigned int num_trans = png_ptr->num_trans;
#  endif /* READ_tRNS */
   const png_colorp  palette = png_ptr->palette;
   png_bytep p;
   unsigned int i;
#  ifdef PNG_READ_tRNS_SUPPORTED
      const png_bytep trans_alpha = png_ptr->trans_alpha;
#  endif /* READ_tRNS */

   for (i=0, p=cache; i<num_palette; ++i)
   {
      *p++ = palette[i].red;
      *p++ = palette[i].green;
      *p++ = palette[i].blue;
#     ifdef PNG_READ_tRNS_SUPPORTED
         if (num_trans > 0)
         {
            if (i < num_trans)
               *p++ = trans_alpha[i];

            else
               *p++ = 0xFFU;
         }
#     endif /* READ_tRNS */
   }

   return num_palette;
}

static void
png_remove_PLTE_and_tRNS(png_structrp png_ptr)
{
   if (png_ptr->palette != NULL)
      png_free(png_ptr, png_ptr->palette);

   png_ptr->palette = NULL;
   png_ptr->num_palette = 0;

#  ifdef PNG_READ_tRNS_SUPPORTED
      if (png_ptr->trans_alpha != NULL)
         png_free(png_ptr, png_ptr->trans_alpha);

      png_ptr->trans_alpha = NULL;
      png_ptr->num_trans = 0;
#  endif /* READ_tRNS */

   png_ptr->palette_updated = 1U;
}

static void
update_palette(png_structp png_ptr, png_cache_paramsp cp,
   unsigned int max_depth)
{
   union
   {
      png_uint_32 u32[1];
      png_uint_16 u16[1];    /* For alignment */
      png_byte    b8[8*256]; /* For 16-bit RGBA intermediate */
   }  cache;

   /* The caller only calls this function if the initial transform control had
    * the palette flag set, implying that the original 'format' was a COLORMAP
    * one.  Also this can only happen (at present) when starting the transform
    * list, so:
    */
   affirm((cp->tstart.format & PNG_FORMAT_FLAG_COLORMAP) != 0); /* required */

   /* Run the whole of the given list on the palette data.  PNG_TC_INIT_FINAL
    * has already been run; this is a full run (with init == 0).
    */
   {
      unsigned int check_depth;
      only_deb(png_transform_control orig = cp->tend;)

      cp->tend = cp->tstart;
      init_caching(png_ptr, &cp->tend);
      /* And set up tend to actually work out the palette: */
      cp->tend.init = 0U;
      cp->tend.width = setup_palette_cache(png_ptr, cache.b8);
      cp->tend.sp = cache.b8;
      cp->tend.dp = cache.b8;

      check_depth =
         png_run_this_transform_list_forwards(&cp->tend, cp->start, *cp->end);

      /* If we get here these two things must be true or there are been some
       * buggy difference of opinion between the INIT code and the actual run:
       */
      affirm(check_depth == max_depth && cp->tend.palette);

      /* This should match the passed in final format obtained before, this
       * debug statement detects discrepancies between the init code and the
       * run code:
       */
      debug(png_transform_control_eq(&cp->tend, &orig));

      /* Also, expect the palette to still be valid: */
      debug((cp->tend.invalid_info & PNG_INFO_PLTE) == 0);
   }

   /* The result must be compatible with a PNG palette with respect to bit
    * depth; specifically the expand-16 transform has no effect on palette data.
    *
    * The colormap setting must not have been re-introduced here either; there
    * may be some quantize interactions here, neither can unexpected flags be
    * handled; just COLOR and ALPHA.
    */
   affirm(cp->tend.bit_depth == 8 &&
          (cp->tend.format & PNG_FORMAT_FLAG_COLORMAP) == 0);

   /* Remove all the transforms between start(inclusive) and end(exclusive);
    * they have been processed.  The effect they had on the transform control
    * is irrelevant because the caller re-instates the settings from tstart.
    */
   {
      png_transformp list = *cp->start; /* list to free */

      *cp->start = *cp->end; /* part of list not to be freed */
      *cp->end = NULL; /* terminate the list to be freed */
      cp->end = cp->start; /* else cp->end points to the end of the list! */

      png_transform_free(png_ptr, &list);
   }

   /* Adjust the PNG palette and, if required, the tRNS entries.  Note that
    * if the transforms stripped the alpha channel from the palette num_trans
    * will get set to 0 here.
    *
    * This is the point where the gamma gets frozen too.  The alternative
    * design is to pass palette, tRNS and gamma up the transform chain, but
    * that doesn't work because the palette change would, apparently, have to
    * be repeated on each row.  This seems simpler at the cost of a little
    * obscurity; the answer to the question, "Where does the palette get
    * updated?", is "Here!"
    *
    * API CHANGE: (fix): previously the init code would silently overwrite
    * the palette information shared with png_info, breaking the API for
    * png_read_update_info, which doesn't update the info if it isn't called,
    * by changing the palette and maybe tRNS when the first row was read!
    *
    * NOTE: PNG_FORMAT_FLAG_RANGE is lost at this point, even if the palette
    * entries were shifted or inverted.  This could be fixed, but it would
    * complicate the libpng API to expose the information.
    */
   /* Write the transformed palette: */
   {
      png_colorp palette = png_voidcast(png_colorp, png_calloc(png_ptr,
               sizeof (png_color[PNG_MAX_PALETTE_LENGTH])));
      png_const_bytep p;
      const int is_color = (cp->tend.format & PNG_FORMAT_FLAG_COLOR) != 0;
      unsigned int i;
#     ifdef PNG_READ_tRNS_SUPPORTED
         unsigned int num_trans = 0;
         const int do_trans = (cp->tend.format & PNG_FORMAT_FLAG_ALPHA) != 0;
         png_byte trans_alpha[PNG_MAX_PALETTE_LENGTH];
#     endif /* READ_tRNS */

      memset(palette, 0xFFU, sizeof (png_color[PNG_MAX_PALETTE_LENGTH]));
      png_free(png_ptr, png_ptr->palette);
      png_ptr->palette = palette;

      for (i=0, p=cache.b8; i<cp->tend.width; ++i)
      {
         if (is_color)
         {
            palette[i].red = *p++;
            palette[i].green = *p++;
            palette[i].blue = *p++;
         }

         else
            palette[i].blue = palette[i].green = palette[i].red = *p++;

#        ifdef PNG_READ_tRNS_SUPPORTED
            if (do_trans)
            {
               png_byte a = *p++;
               trans_alpha[i] = a;

               /* Strip opaque entries from the end: */
               if (a < 0xFFU)
                  num_trans = i+1;
            }
#        endif /* READ_tRNS */
      }

      png_ptr->num_palette = png_check_bits(png_ptr, cp->tend.width, 9);

#     ifdef PNG_READ_tRNS_SUPPORTED
         if (num_trans > 0)
         {
            png_bytep tRNS = png_voidcast(png_bytep, png_malloc(png_ptr,
                     PNG_MAX_PALETTE_LENGTH));

            memset(tRNS, 0xFFU, PNG_MAX_PALETTE_LENGTH);

            if (png_ptr->trans_alpha != NULL)
               png_free(png_ptr, png_ptr->trans_alpha);

            png_ptr->trans_alpha = tRNS;

            memcpy(tRNS, trans_alpha, num_trans);
            png_ptr->num_trans = png_check_bits(png_ptr, num_trans, 9);
         }
#     endif /* READ_tRNS */
   }

   /* NOTE: the caller sets cp->start to cp->end and cp->tend to cp->tstart,
    * this causes processing to continue with the palette format and the
    * first unprocessed transform.  The reset of the transform control loses the
    * gamma information as well, of course, as any information about the palette
    * and tRNS changes (such as the RANGE flags).
    *
    * The following ensures that png_read_update_info knows to update the
    * palette in png_info (which is no longer shared).
    */
   png_ptr->palette_updated = 1U;
}

/* These structure and the save/restore routines that follow it exist to save
 * data from a png_transform_control that is specific to the sample encoding of
 * the PNG data, rather than the row format itself.
 */
typedef struct
{
#  ifdef PNG_READ_GAMMA_SUPPORTED
      png_fixed_point gamma;
#  endif
   png_byte           sBIT_R;
   png_byte           sBIT_G;
   png_byte           sBIT_B;
   png_byte           sBIT_A;       /* Signnificant bits in the row channels. */
   unsigned int       invalid_info; /* PNG_INFO_* for invalidated chunks */
} png_tc_channel_data;

static void
save_cp_channel_data(png_tc_channel_data *save, png_const_transform_controlp tc)
{
#  ifdef PNG_READ_GAMMA_SUPPORTED
      save->gamma = tc->gamma;
#  endif /* READ_GAMMA */

   /* The sBIT information and the list of invalidated chunks must also be
    * preserved:
    */
   save->sBIT_R = tc->sBIT_R;
   save->sBIT_G = tc->sBIT_G;
   save->sBIT_B = tc->sBIT_B;
   save->sBIT_A = tc->sBIT_A;
   save->invalid_info = tc->invalid_info;
}

static void
restore_cp_channel_data(png_transform_controlp tc,
      const png_tc_channel_data *save)
   /* Reverse the above */
{
#  ifdef PNG_READ_GAMMA_SUPPORTED
      tc->gamma = save->gamma;
#  endif /* READ_GAMMA */

   tc->sBIT_R = save->sBIT_R;
   tc->sBIT_G = save->sBIT_G;
   tc->sBIT_B = save->sBIT_B;
   tc->sBIT_A = save->sBIT_A;
   tc->invalid_info = save->invalid_info;
}

static void
make_cache(png_structp png_ptr, png_cache_paramsp cp, unsigned int max_depth)
{
   /* At present the cache is just a byte lookup table.  We need the original
    * pixel depth to work out how big the working buffer needs to be.
    */
   const unsigned int ipd = PNG_TC_PIXEL_DEPTH(cp->tstart);
   const unsigned int opd = PNG_TC_PIXEL_DEPTH(cp->tend);
   unsigned int order; /* records position of start transform */
   unsigned int width; /* width of cache in pixels */
   png_tc_channel_data save; /* Record of the final channel info */
   union
   {
      png_uint_32 u32[1];
      png_uint_16 u16[1];    /* For alignment */
      png_byte    b8[8*256]; /* For 16-bit RGBA */
   }  cache;

   debug(cp->tend.init == PNG_TC_INIT_FINAL);
   affirm(opd <= 64 && max_depth <= 64); /* or the cache is not big enough */
   affirm(ipd == opd || (opd & 0x7U) == 0);

   if ((cp->tstart.format & PNG_FORMAT_FLAG_COLORMAP) != 0)
      width = setup_palette_cache(png_ptr, cache.b8);

   else switch (ipd)
   {
      /* The input to the cache is the full range of possible pixel values: */
      case 1:
         /* 2 1-bit pixels, MSB first */
         cache.b8[0] = 0x40U;
         width = 2;
         break;

      case 2:
         /* 4 2-bit pixels, MSB first */
         cache.b8[0] = 0x1BU;
         width = 4;
         break;

      case 4:
         /* 16 4-bit pixels, MSB first */
         cache.b8[0] = 0x01U;
         cache.b8[1] = 0x23U;
         cache.b8[2] = 0x45U;
         cache.b8[3] = 0x67U;
         cache.b8[4] = 0x89U;
         cache.b8[5] = 0xABU;
         cache.b8[6] = 0xCDU;
         cache.b8[7] = 0xEFU;
         width = 16;
         break;

      case 8:
         /* 256 8-bit pixels */
         {
            unsigned int i;

            for (i=0; i<256; ++i)
               cache.b8[i] = PNG_BYTE(i);
         }
         width = 256;
         break;

      default:
         impossible("cache input bit depth");
   }

   /* Reset the transform control to run the transforms on this data, but save
    * the channel info because the row processing functions do not always
    * write it.
    */
   save_cp_channel_data(&save, &cp->tend);
   cp->tend = cp->tstart;
   init_caching(png_ptr, &cp->tend);
   /* And set tend to work out the result of transforming each possible pixel
    * value:
    */
   cp->tend.init = 0U;
   cp->tend.width = width;
   cp->tend.sp = cache.b8;
   cp->tend.dp = cache.b8;

   {
      unsigned int check_depth =
         png_run_this_transform_list_forwards(&cp->tend, cp->start, *cp->end);

      /* This must not change: */
      affirm(PNG_TC_PIXEL_DEPTH(cp->tend) == opd && check_depth == max_depth);
   }

   /* Restore the potentially lost channel data. */
   restore_cp_channel_data(&cp->tend, &save);

   /* This is all the information required to cache the set of transforms
    * between 'start' and 'end'.  We take the transformed pixels and make a
    * cache transform of them.  The cache transform skips the work, transforms
    * the row, and sets the tranform_control to (a copy of) cp->tend.
    *
    * Remove all the transforms between start(inclusive) and end(exclusive);
    * they have been processed.  The effect they had on the transform control
    * is irrelevant because the caller re-instates the settings from tstart.
    */
   {
      png_transformp list = *cp->start; /* list to free */

      *cp->start = *cp->end; /* part of list not to be freed */
      *cp->end = NULL; /* terminate the list to be freed */
      cp->end = NULL; /* reset below */

      order = list->order; /* used below when adding the cache transform */
      png_transform_free(png_ptr, &list);
   }

   /* Make the required cache, as enumerated above there are 22 possibilities,
    * this selects between them, fixes up the cache for the 'byte' cases (where
    * multiple pixels can be handled byte-by-byte) and selects the correct
    * transform function.
    */
   if (ipd == opd)
   {
      /* We already know that ipd is <= 8 bits, so we can expand this case to
       * the byte transform.  The complexity is that for ipd < 8 bits we only
       * have information for individual pixel values and these may be
       * pixel-swapped within the byte.
       */
      if (ipd < 8)
      {
         const int lsb = (cp->tend.format & PNG_FORMAT_FLAG_SWAPPED) != 0;
         unsigned int ishift, b;
         png_byte bcache[256];

         switch (ipd)
         {
            case 1: ishift = 3U; break;
            case 2: ishift = 2U; break;
            case 4: ishift = 1U; break;
            default: impossible("ipd");
         }

         /* Work out the right answer for each byte of pixels: */
         for (b=0U; b<256U; ++b)
         {
            unsigned int o = 0U; /* output byte */
            unsigned int p = 8U; /* right shift to find input pixel */

            do
            {
               unsigned int q = ((1U<<ipd)-1U) & (b >> (p-=ipd));
                  /* The input pixel.  For a palette this value might be outside
                   * the range of palette indices, in which case simply insert
                   * '0':
                   */
               if (q < width)
               {
                  unsigned int r = cache.b8[q >> ishift];
                  r >>= ((lsb ? q : ~q) & ((1U<<ishift)-1U)) << (3U-ishift);
                  r &= ((1U<<ipd)-1U);
                  o |= r << (lsb ? (8U-ipd)-p : p);
               }

               else
               {
                  UNTESTED
               }
            }
            while (p != 0U);

            bcache[b] = png_check_byte(png_ptr, o);
         }

         /* This is a byte transform, with the optional check-for-invalid-index
          * functionality.
          */
         add_cache_transform(png_ptr, order, do_transform_cache_byte, cp,
            bcache, 256U);
      }

      else /* ipd == 8 */
         add_cache_transform(png_ptr, order, do_transform_cache_byte, cp,
            cache.b8, 256U);
   }

   else
   {
      /* opd is a whole number of bytes, ipd is 1, 2, 4 or 8 and not equal to
       * opd.
       */
      png_transform_fn fn;

#     define C(ipd,opd) ((ipd) + 8*(opd))
      switch (C(ipd,opd))
      {
#        define CASE(ipd,opd)\
            case C(ipd,opd): fn = do_transform_cache(ipd,opd); break

            CASE(1,8);
            CASE(2,8);
            CASE(4,8);
            /* No 8,8 */

#        define CASES(opd)\
            CASE(1,opd);\
            CASE(2,opd);\
            CASE(4,opd);\
            CASE(8,opd)

            CASES(16);
            CASES(24);
            CASES(32);
            CASES(48);
            CASES(64);
#        undef CASES
#        undef CASE

         default:
            impossible("cache bit depths");
      }
#     undef C

      /* In the event that the cache is not the full width implied by ipd zero
       * the remaining bytes for security; otherwise they get copied into the
       * cache transform and might get used.  (Specifically if there is an
       * out-of-range palette index they do get used!)
       */
      {
         unsigned int size = transform_cache_size(ipd, opd);
         png_alloc_size_t cachebytes = PNG_TC_ROWBYTES(cp->tend);

         affirm(cachebytes <= sizeof cache.b8);

         if (cachebytes < size)
            memset(cache.b8+cachebytes, 0, size - cachebytes);

         add_cache_transform(png_ptr, order, fn, cp, cache.b8, size);
      }
   }

   /* Because a transform was inserted cp->end needs to be set to the new
    * pointer to the original end.  add_cache_transform sets cp->start to this,
    * so:
    */
   cp->end = cp->start;

   /* This invalidates the palette if that is what was cached because the
    * palette and, if present, tRNS chunk did not get updated above.
    */
   if (cp->tstart.palette)
      png_remove_PLTE_and_tRNS(png_ptr);
}

static void restore_cp(png_cache_paramsp cp)
{
   /* A utility to restore cp->tstart by copying it into cp->tend.  This is used
    * both in the palette case when restoring the transform control for the
    * indexed data and in the case where no transforms were cached.  It
    * preserves the color-channel-specific data from cp->tend because in either
    * case it is possible for this data to be modified without preserving any
    * transforms, e.g. if only the gamma is changed but no gamma transform is
    * retained because the change was not significant.
    */
   png_tc_channel_data save;

   save_cp_channel_data(&save, &cp->tend);
   cp->tend = cp->tstart;
   restore_cp_channel_data(&cp->tend, &save);
}

static void
handle_cache(png_structp png_ptr, png_cache_paramsp cp, unsigned int max_depth)
{
   /* There is nothing to do if there are no transforms between 'start' and
    * 'end':
    */
   if (cp->start != cp->end)
   {
      only_deb(png_transformp tr_check = *cp->end;)

      /* libpng doesn't currently implement any pixel size of more than 64 bits
       * so:
       */
      affirm(max_depth <= 64);

      if (cp->tend.palette)
      {
         /* The transforms being cached apply to the palette, the following
          * transforms will apply to the original index data and the transformed
          * data must be used to update the palette:
          */
         if (cp->tend.init == PNG_TC_INIT_FINAL)
            update_palette(png_ptr, cp, max_depth);

         cp->start = cp->end;
         restore_cp(cp); /* reset to palette data */
      }

      else
      {
         /* Continue with the transform control in cp.tend; even if there was
          * palette data in cp.tstart it has been expanded.
          */
         if (cp->tend.init == PNG_TC_INIT_FINAL)
            make_cache(png_ptr, cp, max_depth);

         cp->tstart = cp->tend; /* keep current context */
      }

      debug(tr_check == *cp->end);
   }

   else /* no transforms cached */
      restore_cp(cp); /* removes any palette caching info */
}

#ifdef PNG_READ_tRNS_SUPPORTED
static void
check_tRNS_for_alpha(png_structrp png_ptr)
{
   unsigned int num_trans = png_ptr->num_trans;

   debug(png_ptr->color_type == PNG_COLOR_TYPE_PALETTE);

   while (num_trans > 0)
   {
      {
         const png_byte trans = png_ptr->trans_alpha[--num_trans];

         if (trans == 0xFFU)
            continue;

         if (trans > 0U)
            return; /* Palette has at least one entry >0, <0xff */
      }

      /* There is some point to the tRNS chunk; it has a non-opaque entry, this
       * code could truncate it but there is no obvious performance advantage to
       * doing this.
       */
      while (num_trans > 0)
      {
         const png_byte trans = png_ptr->trans_alpha[--num_trans];

         if (trans > 0U && trans < 0xFFU)
            return;
      }

      /* Here if the above did not find an entry >0 && <0xFFU but did find a
       * transparent entry (0u).  Record this.
       */
      png_ptr->transparent_palette = 1U;
      return;
   }

   /* All entries opaque; remove the tRNS data: */
   // TODO: This optimization doesn't handle adding it back if RGBA is requested.
   // See PPSSPP issue #14628.
   //png_ptr->num_trans = 0U;
}
#endif /* READ_tRNS */

unsigned int /* PRIVATE */
png_read_init_transform_mech(png_structp png_ptr, png_transform_controlp tc)
   /* This is called once for each init stage (PNG_TC_INIT_FORMAT and
    * PNG_TC_INIT_FINAL) to run the transform list forwards, returning the
    * maximum depth required to process the row.  It handles caching of the
    * transforms and the processing of the palette for color-mapped PNG data.
    */
{
   png_transformp *list = &png_ptr->transform_list;
   unsigned int max_depth, cache_start_depth;
   png_cache_params cp;

   /* PNG color-mapped data must be handled here so that the palette is updated
    * correctly.  png_set_palette_to_rgb causes the palette flag to be removed
    * from the transform control but does no other change.  png_set_quantize
    * causes 8-bit RGB, RGBA or palette data to be converted into palette
    * indices, setting the palette flag.
    */
#  ifdef PNG_READ_tRNS_SUPPORTED
      /* This happens once at the start to find out if the tRNS chunk consisted
       * entirely of opaque (255) and/or transparent (0) entries.
       */
      if (tc->init == PNG_TC_INIT_FORMAT &&
          png_ptr->color_type == PNG_COLOR_TYPE_PALETTE)
         check_tRNS_for_alpha(png_ptr);
#  endif /* READ_tRNS */
   cp.end = cp.start = list;
   cp.tend = cp.tstart = *tc;
   max_depth = cache_start_depth = PNG_TC_PIXEL_DEPTH(cp.tend);

   while (*cp.end != NULL)
   {
      png_transformp tr = *cp.end;

      /* The user transform cannot be cached. */
      if (tr->order >= PNG_TR_USER)
         break;

      /* If caching is not on and this transform is after PNG_TR_START_CACHE
       * try to turn it on.
       */
      if (tr->order > PNG_TR_START_CACHE && !cp.tend.caching)
      {
         cp.start = cp.end;
         cp.tstart = cp.tend;
         init_caching(png_ptr, &cp.tend);

         if (cp.tend.caching)
         {
            cache_start_depth = max_depth;
            max_depth = PNG_TC_PIXEL_DEPTH(cp.tend);
         }
      }

      /* If the 'palette' flag is set and the next transform has order
       * PNG_TR_ENCODING or later cache the results so far and continue with the
       * original palette data (cp.tstart).
       */
      if (cp.tend.palette && tr->order >= PNG_TR_ENCODING)
      {
         handle_cache(png_ptr, &cp, max_depth);

         /* The cache handling function must maintain cp.end; */
         affirm(tr == *cp.end);
         max_depth = PNG_TC_PIXEL_DEPTH(cp.tend);
         if (max_depth < cache_start_depth)
            max_depth = cache_start_depth;
      }

      /* Now run the transform list entry: */
      if (tr->fn != NULL)
      {
         tr->fn(cp.end, &cp.tend);
         tr = *cp.end; /* in case something was inserted */
      }

      if (tr->fn == NULL) /* delete this transform */
         png_remove_transform(png_ptr, cp.end);

      else
      {
         /* Handle the initialization of the maximum pixel depth. */
         unsigned int tc_depth = PNG_TC_PIXEL_DEPTH(cp.tend);

         if (tc_depth > max_depth)
            max_depth = tc_depth;

         /* Advance to the next transform. */
         cp.end = &tr->next;
      }
   }

   /* At the end if still caching record the cache information (this is common;
    * this is generally the case for an expanded palette.)
    */
   if (cp.tend.caching)
   {
      png_transformp tr = *cp.end;
      handle_cache(png_ptr, &cp, max_depth);
      affirm(tr == *cp.end);
      max_depth = PNG_TC_PIXEL_DEPTH(cp.tend);
      if (max_depth < cache_start_depth)
         max_depth = cache_start_depth;
   }

   /* At the end run the init on the user transform: */
   if (*cp.end != NULL)
   {
      png_transformp tr = *cp.end;
      affirm(tr->order == PNG_TR_USER);
      if (tr->fn != NULL)
         tr->fn(cp.end, &cp.tend);
      /* This cannot insert anything, so: */
      affirm(tr == *cp.end && tr->next == NULL);

      if (tr->fn == NULL) /* delete this transform */
         png_remove_transform(png_ptr, cp.end);

      else
      {
         unsigned int tc_depth = PNG_TC_PIXEL_DEPTH(cp.tend);

         if (tc_depth > max_depth)
            max_depth = tc_depth;
      }
   }

   /* And write the input transform control: */
   *tc = cp.tend;

   return max_depth;
}

/* Modify the info structure to reflect the transformations.  The
 * info should be updated so a PNG file could be written with it,
 * assuming the transformations result in valid PNG data.
 */
void /* PRIVATE */
png_read_transform_info(png_structrp png_ptr, png_inforp info_ptr)
{
   png_debug(1, "in png_read_transform_info");

   /* WARNING: this is very basic at present.  It just updates the format
    * information.  It should update the palette (and will eventually) as well
    * as invalidating chunks that the transforms break.
    */
#  ifdef PNG_TRANSFORM_MECH_SUPPORTED
      info_ptr->format = png_ptr->row_format;
      info_ptr->bit_depth = png_ptr->row_bit_depth;
#     ifdef PNG_READ_GAMMA_SUPPORTED
         /* If an info struct is used with a different png_ptr in a call to
          * png_set_gAMA then the png_struct information won't be updated, this
          * doesn't matter on write, but don't zap the value in the info on read
          * unless it is known:
          *
          * TODO: review this whole mess.
          */
         if (png_ptr->row_gamma > 0)
            info_ptr->colorspace.gamma = png_ptr->row_gamma;
#     endif

      /* Invalidate chunks marked as invalid: */
#     ifdef PNG_READ_TRANSFORMS_SUPPORTED
         info_ptr->valid &= ~png_ptr->invalid_info;

         /* If the palette or tRNS chunk was changed copy them over to the info
          * structure; this may actually re-validate the PLTE or tRNS chunks,
          * but only if png_ptr has a new version, otherwise the invalid_info
          * settings from above can still invalidate the chunk.
          */
         if (png_ptr->palette_updated)
         {
            if (png_ptr->num_palette > 0)
               png_set_PLTE(png_ptr, info_ptr, png_ptr->palette,
                     png_ptr->num_palette);

            else
            {
               png_free_data(png_ptr, info_ptr, PNG_FREE_PLTE, 0);
               info_ptr->valid &= PNG_BIC_MASK(PNG_INFO_PLTE);
            }

#           ifdef PNG_READ_tRNS
               /* If the output format is not a palette format the tRNS
                * information was a single color which is now invalid
                * (probably), otherwise the array of tRNS values must be
                * updated.
                */
               if ((info_ptr->format & PNG_FORMAT_FLAG_COLORMAP) != 0)
               {
                  if (png_ptr->num_trans > 0)
                     png_set_tRNS(png_ptr, info_ptr, png_ptr->trans_alpha,
                           png_ptr->num_trans, NULL/*trans color*/);

                  else
                  {
                     png_free_data(png_ptr, info_ptr, PNG_FREE_tRNS, 0);
                     info_ptr->valid &= PNG_BIC_MASK(PNG_INFO_tRNS);
                  }
               }

               else
                  info_ptr->valid &= PNG_BIC_MASK(PNG_INFO_tRNS);
#           endif /* READ_tRNS */
         }
#     endif /* READ_TRANSFORMS */
#  else /* !TRANSFORM_MECH */
      PNG_UNUSED(png_ptr)
      PNG_UNUSED(info_ptr)
#  endif /* !TRANSFORM_MECH */
}
#endif /* READ_TRANSFORMS */
