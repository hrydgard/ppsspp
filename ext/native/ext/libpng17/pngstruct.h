
/* pngstruct.h - header file for PNG reference library
 *
 * Copyright (c) 1998-2014 Glenn Randers-Pehrson
 * (Version 0.96 Copyright (c) 1996, 1997 Andreas Dilger)
 * (Version 0.88 Copyright (c) 1995, 1996 Guy Eric Schalnat, Group 42, Inc.)
 *
 * Last changed in libpng 1.7.0 [(PENDING RELEASE)]
 *
 * This code is released under the libpng license.
 * For conditions of distribution and use, see the disclaimer
 * and license in png.h
 */

/* The structure that holds the information to read and write PNG files.
 * The only people who need to care about what is inside of this are the
 * people who will be modifying the library for their own special needs.
 * It should NOT be accessed directly by an application.
 */

#ifndef PNGSTRUCT_H
#define PNGSTRUCT_H
/* zlib.h defines the structure z_stream, an instance of which is included
 * in this structure and is required for decompressing the LZ compressed
 * data in PNG files.
 */
#ifndef ZLIB_CONST
   /* We must ensure that zlib uses 'const' in declarations. */
#  define ZLIB_CONST
#endif

#include PNG_ZLIB_HEADER

#ifdef const
   /* zlib.h sometimes #defines const to nothing, undo this. */
#  undef const
#endif

/* zlib.h has mediocre z_const use before 1.2.6, this stuff is for compatibility
 * with older builds.
 */
#if ZLIB_VERNUM < 0x1260
#  define PNGZ_MSG_CAST(s) png_constcast(char*,s)
#  define PNGZ_INPUT_CAST(b) png_constcast(png_bytep,b)
#else
#  define PNGZ_MSG_CAST(s) (s)
#  define PNGZ_INPUT_CAST(b) (b)
#endif

/* zlib.h declares a magic type 'uInt' that limits the amount of data that zlib
 * can handle at once.  This type need be no larger than 16 bits (so maximum of
 * 65535), this define allows us to discover how big it is, but limited by the
 * maximum for size_t. The value can be overridden in a library build (pngusr.h,
 * or set it in CPPFLAGS) and it works to set it to a considerably lower value
 * (e.g. 255 works).  A lower value may help memory usage (slightly) and may
 * even improve performance on some systems (and degrade it on others.)
 */
#ifndef ZLIB_IO_MAX
#  define ZLIB_IO_MAX ((uInt)-1)
#endif

#ifdef PNG_WRITE_SUPPORTED
/* The type of a compression buffer list used by the write code. */
typedef struct png_compression_buffer
{
   struct png_compression_buffer *next;
   png_byte                       output[1]; /* actually zbuf_size */
} png_compression_buffer, *png_compression_bufferp;

#define PNG_COMPRESSION_BUFFER_SIZE(pp)\
   (offsetof(png_compression_buffer, output) + (pp)->zbuffer_size)
#endif

/* Colorspace support; structures used in png_struct, png_info and in internal
 * functions to hold and communicate information about the color space.
 *
 * PNG_COLORSPACE_SUPPORTED is only required if the application will perform
 * colorspace corrections, otherwise all the colorspace information can be
 * skipped and the size of libpng can be reduced (significantly) by compiling
 * out the colorspace support.
 */
#ifdef PNG_COLORSPACE_SUPPORTED
/* The chromaticities of the red, green and blue colorants and the chromaticity
 * of the corresponding white point (i.e. of rgb(1.0,1.0,1.0)).
 */
typedef struct png_xy
{
   png_fixed_point redx, redy;
   png_fixed_point greenx, greeny;
   png_fixed_point bluex, bluey;
   png_fixed_point whitex, whitey;
} png_xy;

/* The same data as above but encoded as CIE XYZ values.  When this data comes
 * from chromaticities the sum of the Y values is assumed to be 1.0
 */
typedef struct png_XYZ
{
   png_fixed_point red_X, red_Y, red_Z;
   png_fixed_point green_X, green_Y, green_Z;
   png_fixed_point blue_X, blue_Y, blue_Z;
} png_XYZ;
#endif /* COLORSPACE */

#if defined(PNG_COLORSPACE_SUPPORTED) || defined(PNG_GAMMA_SUPPORTED)
/* A colorspace is all the above plus, potentially, profile information,
 * however at present libpng does not use the profile internally so it is only
 * stored in the png_info struct (if iCCP is supported.)  The rendering intent
 * is retained here and is checked.
 *
 * The file gamma encoding information is also stored here and gamma correction
 * is done by libpng, whereas color correction must currently be done by the
 * application.
 */
typedef struct png_colorspace
{
#ifdef PNG_GAMMA_SUPPORTED
   png_fixed_point gamma;        /* File gamma */
#endif

#ifdef PNG_COLORSPACE_SUPPORTED
   png_xy      end_points_xy;    /* End points as chromaticities */
   png_XYZ     end_points_XYZ;   /* End points as CIE XYZ colorant values */
   png_uint_16 rendering_intent; /* Rendering intent of a profile */
#endif

   /* Flags are always defined to simplify the code. */
   png_uint_16 flags;            /* As defined below */
} png_colorspace, * PNG_RESTRICT png_colorspacerp;

typedef const png_colorspace * PNG_RESTRICT png_const_colorspacerp;

/* General flags for the 'flags' field */
#define PNG_COLORSPACE_HAVE_GAMMA           0x0001
#define PNG_COLORSPACE_HAVE_ENDPOINTS       0x0002
#define PNG_COLORSPACE_HAVE_INTENT          0x0004
#define PNG_COLORSPACE_FROM_gAMA            0x0008
#define PNG_COLORSPACE_FROM_cHRM            0x0010
#define PNG_COLORSPACE_FROM_sRGB            0x0020
#define PNG_COLORSPACE_ENDPOINTS_MATCH_sRGB 0x0040
#define PNG_COLORSPACE_MATCHES_sRGB         0x0080 /* exact match on profile */
#define PNG_COLORSPACE_RGB_TO_GRAY_SET      0x0100 /* user specified coeffs */
#define PNG_COLORSPACE_INVALID              0x8000
#define PNG_COLORSPACE_CANCEL(flags)        (0xffff ^ (flags))
#endif /* COLORSPACE || GAMMA */

struct png_struct_def
{
   /* Rearranged in libpng 1.7 to attempt to lessen padding; in general
    * (char), (short), (int) and pointer types are kept separate, however
    * associated members under the control of the same #define are still
    * together.
    */
#ifdef PNG_SETJMP_SUPPORTED
   /* jmp_buf can have very high alignment requirements on some systems, so put
    * it first (the other setjmp members are later as they are infrequently
    * accesed.)
    */
   jmp_buf jmp_buf_local;
#endif

   /* Next the frequently accessed fields.  Many processors perform arithmetic
    * in the address pipeline, but frequently the amount of addition or
    * subtraction is limited.  By putting these fields at the head of png_struct
    * the hope is that such processors will generate code that is both smaller
    * and faster.
    */
   png_colorp  palette;        /* palette from the input file */
   size_t      rowbytes;       /* size of row in bytes */
   size_t      info_rowbytes;  /* cache of updated row bytes */
   png_uint_32 width;          /* width of image in pixels */
   png_uint_32 height;         /* height of image in pixels */
   png_uint_32 num_rows;       /* number of rows in current pass */
   png_uint_32 usr_width;      /* width of row at start of write */
   png_uint_32 iwidth;         /* width of current interlaced row in pixels */
   png_uint_32 row_number;     /* current row in interlace pass */
   png_uint_32 chunk_name;     /* PNG_CHUNK() id of current chunk */
   png_uint_32 crc;            /* current chunk CRC value */
   png_uint_32 mode;           /* tells us where we are in the PNG file */
   png_uint_32 flags;          /* flags indicating various things to libpng */
   png_uint_32 transformations;/* which transformations to perform */
   png_uint_32 zowner;         /* ID (chunk type) of zstream owner, 0 if none */
   png_uint_32 free_me;        /* items libpng is responsible for freeing */

   int         maximum_pixel_depth; /* pixel depth used for the row buffers */
#ifdef PNG_CHECK_FOR_INVALID_INDEX_SUPPORTED
   int         num_palette_max;     /* maximum palette index found in IDAT */
#endif

   png_uint_16 num_palette;   /* number of color entries in palette */
   png_uint_16 num_trans;     /* number of transparency values */

   /* Single byte values, typically used either to save space or to hold 1-byte
    * values from the PNG chunk specifications.
    */
   png_byte compression_type; /* file compression type (always 0) */
   png_byte filter;           /* file filter type (always 0) */
   png_byte interlaced;       /* PNG_INTERLACE_NONE, PNG_INTERLACE_ADAM7 */
   png_byte pass;             /* current interlace pass (0 - 6) */
   png_byte do_filter;        /* row filter flags (see PNG_FILTER_ below ) */
   png_byte color_type;       /* color type of file */
   png_byte bit_depth;        /* bit depth of file */
   png_byte pixel_depth;      /* number of bits per pixel */
   png_byte channels;         /* number of channels in file */
   png_byte sig_bytes;        /* magic bytes read/written from start of file */
   png_byte transformed_pixel_depth;
                              /* pixel depth after read/write transforms */

   /* ERROR HANDLING */
#ifdef PNG_SETJMP_SUPPORTED
   jmp_buf        *jmp_buf_ptr;   /* passed to longjmp_fn */
   png_longjmp_ptr longjmp_fn;    /* setjmp non-local goto function. */
   size_t          jmp_buf_size;  /* size of *jmp_buf_ptr, if allocated */
#endif

   /* Error/warning callbacks */
   png_error_ptr error_fn;        /* print an error message and abort */
#ifdef PNG_WARNINGS_SUPPORTED
   png_error_ptr warning_fn;      /* print a warning and continue */
#endif
   png_voidp     error_ptr;       /* user supplied data for the above */

   /* MEMORY ALLOCATION */
#ifdef PNG_USER_MEM_SUPPORTED
   png_malloc_ptr malloc_fn; /* allocate memory */
   png_free_ptr   free_fn;   /* free memory */
   png_voidp      mem_ptr;   /* user supplied data for the above */
#endif

   /* IO and BASIC READ/WRITE SUPPORT */
   png_voidp            io_ptr;       /* user supplied data for IO callbacks */

#ifdef PNG_READ_SUPPORTED
   png_rw_ptr          read_data_fn;  /* read some bytes (must succeed) */
   png_read_status_ptr read_row_fn;   /* called after each row is decoded */
   png_bytep           read_buffer;   /* buffer for reading chunk data */

   /* During read the following array is set up to point to the appropriate
    * un-filter function, this allows per-image and per-processor optimization.
    */
   void (*read_filter[PNG_FILTER_VALUE_LAST-1])(png_row_infop row_info,
      png_bytep row, png_const_bytep prev_row);

#if defined(PNG_COLORSPACE_SUPPORTED) || defined(PNG_GAMMA_SUPPORTED)
   /* The png_struct colorspace structure is only required on read - on write it
    * is in (just) the info_struct.
    */
   png_colorspace   colorspace;
#endif
#endif /* PNG_READ_SUPPORTED */

#ifdef PNG_SET_USER_LIMITS_SUPPORTED
   png_uint_32 user_width_max;        /* Maximum width on read */
   png_uint_32 user_height_max;       /* Maximum height on read */
   /* Total memory that a single zTXt, sPLT, iTXt, iCCP, or unknown chunk
    * can occupy when decompressed.  0 means unlimited.  This field is a counter
    * - it is decremented as memory is allocated.
    */
   png_alloc_size_t user_chunk_malloc_max;
#endif
#ifdef PNG_USER_LIMITS_SUPPORTED
   /* limit on total *number* of sPLT, text and unknown chunks that can be
    * stored.  0 means unlimited.  This field is a counter - it is decremented
    * as chunks are encountered.
    */
   png_uint_32 user_chunk_cache_max;
#endif

   /* The progressive reader gets passed data and calls application handling
    * functions when appropriate.
    */
#ifdef PNG_PROGRESSIVE_READ_SUPPORTED
   png_progressive_info_ptr info_fn; /* called after header data fully read */
   png_progressive_row_ptr  row_fn;  /* called after a row is decoded */
   png_progressive_end_ptr  end_fn;  /* called after image is complete */

   /* Progressive read control data */
   png_bytep   save_buffer_ptr;      /* current location in save_buffer */
   png_bytep   save_buffer;          /* buffer for previously read data */
   png_bytep   current_buffer_ptr;   /* current location in current_buffer */
   png_bytep   current_buffer;       /* buffer for recently used data */

   size_t      save_buffer_size;     /* amount of data now in save_buffer */
   size_t      save_buffer_max;      /* total size of save_buffer */
   size_t      buffer_size;          /* total amount of available input data */
   size_t      current_buffer_size;  /* amount of data now in current_buffer */

   png_uint_32 push_length;          /* size of current input chunk */
   png_uint_32 skip_length;          /* bytes to skip in input data */

   int         process_mode;         /* what push library is currently doing */
   int         cur_palette;          /* current push library palette index */
#endif

#ifdef PNG_WRITE_SUPPORTED
   png_rw_ptr              write_data_fn;/* write some bytes (must succeed) */
   png_write_status_ptr    write_row_fn; /* called after each row is encoded */
#endif

#ifdef PNG_WRITE_FLUSH_SUPPORTED
   png_flush_ptr output_flush_fn; /* Function for flushing output */
   png_uint_32   flush_dist; /* how many rows apart to flush, 0 - no flush */
   png_uint_32   flush_rows; /* number of rows written since last flush */
#endif

#ifdef PNG_WRITE_WEIGHTED_FILTER_SUPPORTED
   png_bytep    prev_filters;        /* filter type(s) of previous row(s) */
   png_uint_16p filter_weights;      /* weight(s) for previous line(s) */
   png_uint_16p inv_filter_weights;  /* 1/weight(s) for previous line(s) */
   png_uint_16p filter_costs;        /* relative filter calculation cost */
   png_uint_16p inv_filter_costs;    /* 1/relative filter calculation cost */
   png_byte     heuristic_method;    /* heuristic for row filter selection */
   png_byte     num_prev_filters;    /* number of weights for previous rows */
#endif

#ifdef PNG_WRITE_SUPPORTED
   png_byte     usr_bit_depth;       /* bit depth of users row */
   png_byte     usr_channels;        /* channels at start of write */
#endif

#ifdef PNG_IO_STATE_SUPPORTED
   png_uint_32          io_state;     /* tells the app read/write progress */
#endif

   /* ROW BUFFERS
    *
    * Members that hold pointers to the decompressed image rows.
    */
   png_bytep row_buf;  /* buffer for the current (unfiltered) row */
#if defined(PNG_WRITE_FILTER_SUPPORTED) || defined(PNG_READ_SUPPORTED)
   png_bytep prev_row; /* buffer to save the previous (unfiltered) row */
#endif

#ifdef PNG_READ_SUPPORTED
   /* The row_buf and prev_row pointers are misaligned so that the start of the
    * row - after the filter byte - is aligned, the 'big_' pointers record the
    * original allocated pointer.
    */
   png_bytep big_row_buf;
   png_bytep big_prev_row;
   size_t    big_row_buf_size; /* Actual size of both */
#endif

#ifdef PNG_WRITE_SUPPORTED
   /* This is somewhat excessive, there is no obvious reason on write to
    * allocate a buffer for each possible filtered row, only for the one being
    * tested and the current best.
    *
    * TODO: fix this
    */
   png_bytep sub_row;         /* buffer to save "sub" row when filtering */
   png_bytep up_row;          /* buffer to save "up" row when filtering */
   png_bytep avg_row;         /* buffer to save "avg" row when filtering */
   png_bytep paeth_row;       /* buffer to save "Paeth" row when filtering */
#endif

   /* UNKNOWN CHUNK HANDLING */
   /* TODO: this is excessively complicated, there are multiple ways of doing
    * the same thing.  It should be cleaned up, possibly by finding out which
    * APIs applications really use.
    */
#ifdef PNG_USER_CHUNKS_SUPPORTED
   /* General purpose pointer for all user/unknown chunk handling; points to
    * application supplied data for use in the read_user_chunk_fn callback
    * (currently there is no write side support - the write side must use the
    * set_unknown_chunks interface.)
    */
   png_voidp user_chunk_ptr;
#endif

#ifdef PNG_READ_USER_CHUNKS_SUPPORTED
   /* This is called back from the unknown chunk handling */
   png_user_chunk_ptr     read_user_chunk_fn; /* user read chunk handler */
#endif
#ifdef PNG_READ_UNKNOWN_CHUNKS_SUPPORTED
   /* Temporary storage for unknown chunk that the library doesn't recognize,
    * used while reading the chunk.
    */
   png_unknown_chunk unknown_chunk;
#endif

#ifdef PNG_SET_UNKNOWN_CHUNKS_SUPPORTED
   png_bytep    chunk_list;      /* List of png_byte[5]; the textual chunk name
                                  * followed by a PNG_HANDLE_* byte */
   int          unknown_default; /* As PNG_HANDLE_* */
   unsigned int num_chunk_list;  /* Number of entries in the list */
#endif

   /* USER TRANSFORM SUPPORT */
#ifdef PNG_READ_USER_TRANSFORM_SUPPORTED
   png_user_transform_ptr read_user_transform_fn; /* user read transform */
#endif
#ifdef PNG_WRITE_USER_TRANSFORM_SUPPORTED
   png_user_transform_ptr write_user_transform_fn; /* user write transform */
#endif
#ifdef PNG_USER_TRANSFORM_PTR_SUPPORTED
   png_voidp user_transform_ptr;      /* user supplied data for the above */
   png_byte  user_transform_depth;    /* bit depth of user transformed pixels */
   png_byte  user_transform_channels; /* channels in user transformed pixels */
#endif

   /* READ TRANSFORM SUPPORT
    *
    * Quite a lot of things can be done to the original image data on read, and
    * most of these are configurable.  The data required by the configurable
    * read transforms should be stored here.  The png_color_16 and png_color_8
    * structures have low alignment requirements and odd sizes, so may cause
    * misalignment when present.  Member alignment is as follows:
    *
    *    png_color_16   png_uint_16
    *    png_color_8    png_byte
    */
   /* GAMMA/BACKGROUND/ALPHA-MODE/RGB-TO-GRAY/tRNS/sBIT
    *
    * These things are all interrelated because they need some or all of the
    * gamma tables.  Some attempt has been made below to order these members by
    * size, so that as little padding as possible is required.
    */
#ifdef PNG_READ_GAMMA_SUPPORTED
   png_bytep gamma_table;        /* gamma table for 8-bit depth files */
   png_uint_16p gamma_16_table;  /* gamma table for 16-bit depth files */

#if defined(PNG_READ_BACKGROUND_SUPPORTED) ||\
   defined(PNG_READ_ALPHA_MODE_SUPPORTED) ||\
   defined(PNG_READ_RGB_TO_GRAY_SUPPORTED)
   png_bytep gamma_from_1;       /* converts from 1.0 to screen */
   png_uint_16p gamma_to_1;      /* converts from file to 1.0 */
   png_uint_16p gamma_16_from_1; /* converts from 1.0 to screen */
   png_uint_16p gamma_16_to_1;   /* converts from file to 1.0 */
#endif /* READ_BACKGROUND || READ_ALPHA_MODE || RGB_TO_GRAY */
#endif /* PNG_READ_GAMMA_SUPPORTED */

#if defined(PNG_READ_tRNS_SUPPORTED) || \
   defined(PNG_READ_BACKGROUND_SUPPORTED) || \
   defined(PNG_READ_EXPAND_SUPPORTED)
   png_bytep trans_alpha;           /* alpha values for paletted files */
#endif

   /* Integer values */
#if defined(PNG_READ_BACKGROUND_SUPPORTED) ||\
   defined(PNG_READ_ALPHA_MODE_SUPPORTED)
   png_fixed_point background_gamma;
#endif
#ifdef PNG_READ_GAMMA_SUPPORTED
   png_fixed_point screen_gamma; /* screen gamma value (display_exponent) */
   int gamma_shift; /* number of "insignificant" bits in 16-bit gamma */
#endif

   /* png_color_16 */
#if defined(PNG_READ_BACKGROUND_SUPPORTED) ||\
   defined(PNG_READ_ALPHA_MODE_SUPPORTED)
   png_color_16 background;   /* background color in screen gamma space */
   png_color_16 background_1; /* background normalized to gamma 1.0 */
#endif
#if defined(PNG_READ_tRNS_SUPPORTED) || \
   defined(PNG_READ_BACKGROUND_SUPPORTED) || \
   defined(PNG_READ_EXPAND_SUPPORTED)
   png_color_16 trans_color;  /* transparent color for non-paletted files */
#endif

   /* png_uint_16 */
#ifdef PNG_READ_RGB_TO_GRAY_SUPPORTED
   png_uint_16 rgb_to_gray_red_coeff;
   png_uint_16 rgb_to_gray_green_coeff;
   /* The blue coefficient is calculated from the above */
#endif

   /* png_color_8 */
#if defined(PNG_READ_GAMMA_SUPPORTED) || defined(PNG_READ_sBIT_SUPPORTED)
   png_color_8 sig_bit;       /* significant bits in each available channel */
#endif

   /* png_byte */
#if defined(PNG_READ_BACKGROUND_SUPPORTED) ||\
   defined(PNG_READ_ALPHA_MODE_SUPPORTED)
   png_byte background_gamma_type;
#endif
#ifdef PNG_READ_RGB_TO_GRAY_SUPPORTED
   png_byte rgb_to_gray_status;
#endif

   /* SHIFT - both READ_SHIFT and WRITE_SHIFT */
#if defined(PNG_READ_SHIFT_SUPPORTED) || defined(PNG_WRITE_SHIFT_SUPPORTED)
   png_color_8 shift;         /* shift for significant bit tranformation */
#endif

   /* FILLER SUPPORT (pixel expansion or read, contraction on write) */
#if defined(PNG_READ_FILLER_SUPPORTED) || defined(PNG_WRITE_FILLER_SUPPORTED)
   png_uint_16 filler;              /* filler bytes for pixel expansion */
#endif

   /* QUANTIZE (convert to color-mapped) */
#ifdef PNG_READ_QUANTIZE_SUPPORTED
   png_bytep palette_lookup;   /* lookup table for quantizing */
   png_bytep quantize_index;   /* index translation for palette files */
   png_bytep quantize_sort;    /* working sort array */
   png_bytep index_to_palette; /* where the original index currently is in the
                                * palette
                                */
   png_bytep palette_to_index; /* which original index points to this palette
                                * color
                                */
#endif

   /* MNG SUPPORT */
#ifdef PNG_MNG_FEATURES_SUPPORTED
   png_uint_32 mng_features_permitted;
   png_byte filter_type;
#endif

   /* Options */
#ifdef PNG_SET_OPTION_SUPPORTED
   png_byte options;           /* On/off state (up to 4 options) */
#endif

   /* COMPRESSION AND DECOMPRESSION SUPPORT.
    *
    * zlib expects a 'zstream' as the fundamental control structure, it allows
    * all the parameters to be passed as one pointer.
    */
   z_stream    zstream;       /* decompression structure */

#ifdef PNG_READ_SUPPORTED
   /* These, and IDAT_read_size below, control how much input and output (at
    * most) is available to zlib.
    */
   png_uint_32      idat_size;        /* current IDAT size for read */
   png_alloc_size_t read_buffer_size; /* current size of the buffer */
#endif

#ifdef PNG_WRITE_CUSTOMIZE_ZTXT_COMPRESSION_SUPPORTED
   int zlib_text_level;       /* holds zlib compression level */
   int zlib_text_method;      /* holds zlib compression method */
   int zlib_text_window_bits; /* holds zlib compression window bits */
   int zlib_text_mem_level;   /* holds zlib compression memory level */
   int zlib_text_strategy;    /* holds zlib compression strategy */
#endif

#ifdef PNG_WRITE_SUPPORTED
   int zlib_level;            /* holds zlib compression level */
   int zlib_method;           /* holds zlib compression method */
   int zlib_window_bits;      /* holds zlib compression window bits */
   int zlib_mem_level;        /* holds zlib compression memory level */
   int zlib_strategy;         /* holds zlib compression strategy */

   int zlib_set_level;        /* Actual values set into the zstream on write */
   int zlib_set_method;
   int zlib_set_window_bits;
   int zlib_set_mem_level;
   int zlib_set_strategy;

   png_compression_bufferp zbuffer_list; /* Created on demand during write */
   uInt                    zbuffer_size; /* size of the actual zlib buffer */
#endif

#ifdef PNG_SEQUENTIAL_READ_SUPPORTED
   uInt IDAT_read_size;       /* limit on read buffer size for IDAT */
#endif
};
#endif /* PNGSTRUCT_H */
