
/* pngstruct.h - header file for PNG reference library
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
#endif /* !ZLIB_CONST */

#include PNG_ZLIB_HEADER

#ifdef const
   /* zlib.h sometimes #defines const to nothing, undo this. */
#  undef const
#endif /* const */

/* zlib.h has mediocre z_const use before 1.2.6, this stuff is for compatibility
 * with older builds.
 */
#if ZLIB_VERNUM < 0x1260
#  define PNGZ_MSG_CAST(s) png_constcast(char*,s)
#  define PNGZ_INPUT_CAST(b) png_constcast(png_bytep,b)
#else /* ZLIB_VERNUM >= 0x1260 */
#  define PNGZ_MSG_CAST(s) (s)
#  define PNGZ_INPUT_CAST(b) (b)
#endif /* ZLIB_VERNUM >= 0x1260 */

/* zlib.h declares a magic type 'uInt' that limits the amount of data that zlib
 * can handle at once.  This type need be no larger than 16 bits (so maximum of
 * 65535), this define allows us to discover how big it is, but limited by the
 * maximum for size_t. The value can be overridden in a library build (pngusr.h,
 * or set it in CPPFLAGS) and it works to set it to a considerably lower value
 * (e.g. 255 works).  A lower value may help memory usage (slightly) and may
 * even improve performance on some systems (and degrade it on others.)
 */
#ifndef ZLIB_IO_MAX
#  ifdef __COVERITY__
#     define ZLIB_IO_MAX ((uInt)255U) /* else COVERITY whines */
#  else /* !COVERITY */
#     define ZLIB_IO_MAX ((uInt)-1)
#  endif /* !COVERITY */
#endif /* !ZLIB_IO_MAX */

#ifdef PNG_WRITE_SUPPORTED
/* The write compression control (allocated on demand).
 * TODO: use this for the read state too.
 */
typedef struct png_zlib_state *png_zlib_statep;
#endif /* WRITE */

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
/* A colorspace is all the above plus, potentially, profile information;
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
#endif /* GAMMA */

#ifdef PNG_COLORSPACE_SUPPORTED
   png_xy      end_points_xy;    /* End points as chromaticities */
   png_XYZ     end_points_XYZ;   /* End points as CIE XYZ colorant values */
   png_uint_16 rendering_intent; /* Rendering intent of a profile */
#endif /* COLORSPACE */

   /* Flags are always defined to simplify the code. */
   png_uint_16 flags;            /* As defined below */
} png_colorspace, * PNG_RESTRICT png_colorspacerp;

typedef const png_colorspace * PNG_RESTRICT png_const_colorspacerp;

/* General flags for the 'flags' field */
#define PNG_COLORSPACE_HAVE_GAMMA           0x0001U
#define PNG_COLORSPACE_HAVE_ENDPOINTS       0x0002U
#define PNG_COLORSPACE_HAVE_INTENT          0x0004U
#define PNG_COLORSPACE_FROM_gAMA            0x0008U
#define PNG_COLORSPACE_FROM_cHRM            0x0010U
#define PNG_COLORSPACE_FROM_sRGB            0x0020U
#define PNG_COLORSPACE_ENDPOINTS_MATCH_sRGB 0x0040U
#define PNG_COLORSPACE_MATCHES_sRGB         0x0080U /* exact match on profile */
#define PNG_COLORSPACE_RGB_TO_GRAY_SET      0x0100U /* user specified coeffs */
#define PNG_COLORSPACE_INVALID              0x8000U
#define PNG_COLORSPACE_CANCEL(flags)        (0xffffU - (flags))
#endif /* COLORSPACE || GAMMA */

#ifdef PNG_TRANSFORM_MECH_SUPPORTED
/***************************** READ and WRITE TRANSFORMS ***********************
 * These structures are used in pngrtran.c, pngwtran.c and pngtrans.c to hold
 * information about transforms in progress.  This mechanism was introduced in
 * libpng 1.7.0 to ensure reliable transform code and to fix multiple bugs in
 * the pre-1.7 transform handling.
 *
 * Prior to 1.7.0 the internal transform routines took a png_row_infop, like the
 * user transform function, but without the png_ptr because it was never used.
 * In 1.7.0 a separate internal structure is used in place of this to allow both
 * future development to change the structure.
 *
 * The values in this structure will normally be changed by transformation
 * implementations.
 ***************************** READ and WRITE TRANSFORMS **********************/
typedef struct
{
   png_structp        png_ptr;      /* png_struct for error handling and some
                                     * transform parameters.  May be aliased.
                                     */
   png_const_voidp    sp;           /* Source; the input row. */
   png_voidp          dp;           /* Output buffer for the transformed row,
                                     * this may be the same as sp.
                                     */
      /* If the row is changed the tranform routine must write the result to
       * dp[] and set sp to dp, otherwise it must not write to dp and must leave
       * sp unchanged.  dp[] and sp[] are both 'malloc' aligned; i.e. they have
       * the system alignment, so the data can be read as any valid ANSI-C
       * type.
       */
   png_uint_32        width;        /* width of row */
#  ifdef PNG_READ_GAMMA_SUPPORTED
      png_fixed_point gamma;        /* Current gamma of the row data */
      /* When a row is being transformed this contains the current gamma of the
       * data if known.  During initialization the value is used to accumulate
       * information for png_struct::row_gamma in the first step,
       * PNG_TC_INIT_FORMAT, then used to insert the correct gamma transforms
       * during PNG_TC_INIT_FINAL.  The field is only used on read; write
       * transforms do not modify the gamma of the data.
       */
#  endif /* READ_GAMMA */
   unsigned int       format;       /* As pngstruct::row_format below */
   unsigned int       range;        /* Count of range transforms */
#  define PNG_TC_CHANNELS(tc) PNG_FORMAT_CHANNELS((tc).format)
   unsigned int       bit_depth;    /* bit depth of row */
   png_byte           sBIT_R;
   png_byte           sBIT_G;
   png_byte           sBIT_B;
   png_byte           sBIT_A;       /* Signnificant bits in the row channels. */
   /* The above four values are initially set to the number of bits significant
    * in the input PNG data, R/G/B are set to the same (gray) value for
    * grayscale input.  All values are set to the bit depth if there is no sBIT
    * chunk, if there is no alpha channel sBIT_A is set to the bit depth.
    *
    * When any potentially spurious bits have been cleared PNG_INFO_sBIT will be
    * set in invalid_info.  From this point on the above values record the
    * approximate number of bits of accuracy in the channels and the lower bits
    * should be preserved; they potentially contain useful information.
    */
#  define PNG_TC_PIXEL_DEPTH(tc) (PNG_TC_CHANNELS(tc) * (tc).bit_depth)
#  define PNG_TC_ROWBYTES(tc) PNG_ROWBYTES(PNG_TC_PIXEL_DEPTH(tc), (tc).width)
   unsigned int       invalid_info; /* PNG_INFO_* for invalidated chunks */
   unsigned int       cost;         /* Cache cost */
#  define PNG_CACHE_COST_LIMIT 0x100U
   /* This is a runtime structure, so size doesn't matter much, and it helps
    * code reliability to use real member names here.  Feel free to experiment
    * with integer values rather than bitfields.
    */
   unsigned int       init :2;      /* 0 for processing, non zero for init: */
#  define PNG_TC_INIT_FORMAT  0x01U /* Initialization step 1: just set 'format',
                                     * 'bit_depth' and 'gamma' to the output
                                     * values iff the transform corresponds to
                                     * a user requested change to those values.
                                     */
#  define PNG_TC_INIT_FINAL   0x03U /* Initialization step 2; set the 'format'
                                     * 'bit_depth' and 'gamma' to the values the
                                     * transform will actually produce (which
                                     * need not be the same as the above).
                                     */
   /* During initialization 'init' must be set and sp and dp may be NULL.  If
    * neither flag is set sp and dp must be non-NULL.
    *
    * When the transform runs it must update 'format', 'bit_depth' and 'gamma'
    * to the values previously reported during PNG_TC_INIT_FINAL; not doing so
    * may result in an affirm from a later transform.
    */
   unsigned int       caching     :1; /* The color values are being used to
                                       * generate a cache of the transforms.
                                       */
   unsigned int       palette     :1; /* The values come from a PNG palette and
                                       * the palette will not be expanded.  The
                                       * CACHE flag must be set too.  A
                                       * transform which causes the palette to
                                       * be expanded must clear this flag.
                                       */
#if 0 /* NYI */
   unsigned int       interchannel:1; /* Set by a transform that combines two or
                                       * more channels together; for example
                                       * alpha composition or RGB to gray.
                                       */
#endif /* NYI */
   unsigned int       channel_add :1; /* A channel (alpha/filler) was added */
   unsigned int       strip_alpha :1; /* Set if the alpha channel will be
                                       * stripped on read, this also prevents
                                       * the tRNS chunk being expanded.  Only
                                       * some transforms check this, depending
                                       * on the handling order and checks in
                                       * pre-1.7 versions.
                                       */
   unsigned int       expand_tRNS :1; /* Set if the tRNS chunk should be
                                       * expanded (ignored if read_strip_alpha
                                       * is set).  If this is *not* set
                                       * transforms which do not use alpha/tRNS
                                       * but would invalidate it (such as
                                       * simple gamma correction) will simply
                                       * mark the tRNS info as invalid.
                                       */
   unsigned int       transparent_alpha :1; /* Indicates that the alpha channel
                                       * consists entirely of opaque (1.0 alpha)
                                       * or completely transparent (0.0 alpha)
                                       * pixels.  Set when tRNS is expanded to
                                       * alpha.
                                       */
   unsigned int       optimized_alpha :1; /* Meaningful only when bit_depth is
                                       * 16 and gamma is 1 or unknown (0).
                                       * Indicates that pixels which are opaque
                                       * (alpha 1.0) have not been expanded to
                                       * 16-bit linear; instead these pixels
                                       * are encoded in the final format in
                                       * png_struct::row_bit_depth and
                                       * png_struct::row_gamma.  This will
                                       * invariably match the file format.
                                       */
} png_transform_control, *png_transform_controlp;

typedef const png_transform_control *png_const_transform_controlp;
typedef const png_row_info *png_const_row_infop;

typedef struct png_transform *png_transformp; /* Forward declaration */
typedef void (*png_transform_free_fn)(/* Function to free a transform */
   png_const_structrp png_ptr,
   png_transformp     transform); /* pointer to this transform */
   /* This function need not exist in a transform, it must free all the data
    * allocated within the transform but not the transform itself.  It is called
    * from png_transform_free.
    */
typedef void (*png_transform_fn)(/* Function to implement a transform */
   png_transformp        *transform, /* pointer to this transform */
   png_transform_controlp control);  /* row information */
   /* The transform function has two modes of operation:
    *
    * 1) Initialization.  The list of transforms is processed from the start to
    *    the end and each function is called with one of hte PNG_TC_INIT_ flags
    *    set in control->flags, control->dp and control->sp may be NULL.
    *
    *    For read the control structure contains the input row format and bit
    *    depth, the transform function changes this to represent what the
    *    transform will produce when it runs.
    *
    *    For write the control structure contains the *required* output format
    *    and bit depth.  The transform function changes this to the values that
    *    it needs to produce the required values.
    *
    *    In both cases the transform function may update the 'fn' function to a
    *    new function to perform the desired transform; this allows considerable
    *    optimization on multi-row images.
    *
    *    In both cases the caller considers the pixel bit depth changes and
    *    records the maximum required so that it can allocate a suitably sized
    *    buffer.
    *
    * 2) Execution.
    *
    *    In the read case the transforms are processed in the stored order and
    *    must transform the row data appropriately *and* update the bit depth
    *    and format as before.
    *
    *    In the write case the transforms are called in the reverse order and
    *    the input bit depth and format should match the required values.
    *
    * It is valid during initialization for the transform function to push
    * another transform into the list in either the read or the write case if
    * the transform cannot handle (read) or produce (write) the required format.    * The transform pushes another transform into the list ahead of itself (at
    * *transform) and runs that initialization; when control is returned to the
    * caller the caller will re-run the transform initialization.
    *
    * It is also valid (during initialization) to push new transforms onto the
    * list, just so long as the order of the transform is greater than the
    * current transform (so that the caller will still call the new transform
    * initialization.)
    *
    * In the write case the user transform callback might still end up producing
    * an unexpected format, but at present this is unavoidable; the libpng API
    * is extremely inconsistent in how a user transform reports the changes it
    * made.
    *
    * TODO: fix this, probably with an API change in 1.7.0
    */
typedef struct png_transform /* Linked list of transform functions */
{
   png_transformp        next;   /* Next transform in the list */
   png_transform_fn      fn;     /* Function to implement the transform */
   png_transform_free_fn free;   /* Free allocated data, normally NULL */
   unsigned int          order;  /* Order of the transform in the list. */
   unsigned int          size;   /* Size of this structure (max 65535) */
   png_uint_32           args;   /* Optional transform arguments. */
}  png_transform;
#endif /* TRANSFORM_MECH */

/* Action to take on CRC errors (four values) */
typedef enum
{
   crc_error_quit = PNG_CRC_ERROR_QUIT-1,
   crc_warn_discard = PNG_CRC_WARN_DISCARD-1,
   crc_warn_use = PNG_CRC_WARN_USE-1,
   crc_quiet_use = PNG_CRC_QUIET_USE-1
}  png_crc_action;

struct png_struct_def
{
   /* Rearranged in libpng 1.7 to attempt to lessen padding; in general
    * (char), (short), (int) and pointer types are kept separate; however
    * associated members under the control of the same #define are still
    * together.
    */
#ifdef PNG_SETJMP_SUPPORTED
   /* jmp_buf can have very high alignment requirements on some systems, so put
    * it first (the other setjmp members are later as they are infrequently
    * accessed.)
    */
   jmp_buf jmp_buf_local;
#endif /* SETJMP */

   /* Next the frequently accessed fields.  Many processors perform arithmetic
    * in the address pipeline, but frequently the amount of addition or
    * subtraction is limited.  By putting these fields at the head of png_struct
    * the hope is that such processors will generate code that is both smaller
    * and faster.
    */
#ifdef PNG_READ_SUPPORTED
   png_colorp  palette;        /* palette from the input file */
#endif /* READ */
#ifdef PNG_READ_tRNS_SUPPORTED
   png_bytep   trans_alpha;    /* alpha values for paletted files */
#endif /* READ_tRNS */

   png_uint_32 width;          /* width of image in pixels */
   png_uint_32 height;         /* height of image in pixels */
   png_uint_32 chunk_name;     /* PNG_CHUNK() id of current chunk */
#ifdef PNG_READ_SUPPORTED
   png_uint_32 chunk_length;   /* Length (possibly remaining) in said chunk. */
#endif /* READ */
   png_uint_32 crc;            /* current chunk CRC value */

   unsigned int mode                :6; /* where we are in the PNG file */
   unsigned int read_struct         :1; /* this is a read (not write) struct */
   unsigned int num_palette         :9; /* number of color entries in palette */
#ifdef PNG_READ_tRNS_SUPPORTED
   unsigned int num_trans           :9; /* number of transparency values */
   unsigned int transparent_palette :1; /* if they are all 0 or 255 */
#endif /* READ_tRNS */
#ifdef PNG_PALETTE_MAX_SUPPORTED
   unsigned int palette_index_max   :8; /* maximum palette index in IDAT */
   unsigned int palette_index_check :2; /* one of the following: */
#     define PNG_PALETTE_CHECK_DEFAULT 0U
#     define PNG_PALETTE_CHECK_OFF     1U
#     define PNG_PALETTE_CHECK_ON      2U
   unsigned int palette_index_have_max     :1; /* max is being set */
   unsigned int palette_index_check_issued :1; /* error message output */
#endif /* PALETTE_MAX */
#ifdef PNG_READ_tRNS_SUPPORTED
   png_color_16 trans_color;   /* transparent color for non-paletted files */
#endif /* READ_tRNS */
#ifdef PNG_READ_sBIT_SUPPORTED
   png_color_8 sig_bit;        /* significant bits in each channel */
#endif /* READ_sBIT */

   /* Single byte values, typically used either to save space or to hold 1-byte
    * values from the PNG chunk specifications.
    */
   png_byte filter_method;    /* file filter type (only non-0 with MNG) */
   png_byte interlaced;       /* PNG_INTERLACE_NONE, PNG_INTERLACE_ADAM7 */
   png_byte color_type;       /* color type of file */
   png_byte bit_depth;        /* bit depth of file */
   png_byte sig_bytes;        /* magic bytes read/written at start of file */

#ifdef PNG_READ_SUPPORTED
#if defined(PNG_COLORSPACE_SUPPORTED) || defined(PNG_GAMMA_SUPPORTED)
   /* The png_struct colorspace structure is only required on read - on write it
    * is in (just) the info_struct.
    */
   png_colorspace   colorspace;
#endif /* COLORSPACE || GAMMA */
#endif /* READ */

   /* Transform handling */
#ifdef PNG_TRANSFORM_MECH_SUPPORTED
   png_transformp transform_list; /* List of transformation to perform. */
#endif /* TRANSFORM_MECH */

   /* ROW BUFFERS and CONTROL
    *
    * Members used for image row compression (write) or decompression (read).
    * filter byte (which is in next_filter.)  All fields are only used during
    * IDAT processing and start of 0.
    */
#ifdef PNG_READ_SUPPORTED
   png_bytep        row_buffer;          /* primary row buffer */
#if (defined(PNG_PROGRESSIVE_READ_SUPPORTED) ||\
     defined(PNG_READ_INTERLACING_SUPPORTED)) &&\
    defined(PNG_TRANSFORM_MECH_SUPPORTED)
   png_bytep        transformed_row;     /* pointer to the transformed row, if
                                          * required.  May point to row_buffer.
                                          */
#endif /* (PROGRESSIVE_READ || READ_INTERLACING) && TRANSFORM_MECH */

   png_alloc_size_t row_bytes_read;   /* Total read in row */
#endif /* READ */

   png_uint_32      row_number;       /* current row in pass */
#ifdef PNG_READ_SUPPORTED
#ifdef PNG_READ_GAMMA_SUPPORTED
   png_fixed_point  row_gamma;        /* Gamma of final output */
#if 0 /* NYI */
   unsigned int     gamma_accuracy;
      /* LINEAR gamma cache table size (in bits) times 100; for non-linear
       * tables the value used is gamma_accuracy/gamma where 'gamma' is the
       * encoding value of the data (typically less than 1).
       *
       * default: PNG_DEFAULT_GAMMA_ACCURACY (665)
       */
#endif /* NYI */
   png_uint_16  gamma_threshold;
      /* Gamma threshold value as a fixed-point value in the range 0..1; the
       * threshold at or below which gamma correction is skipped.  '0' forces
       * gamma correction even when there is none because the input and output
       * gammas are equal.
       *
       * default: PNG_GAMMA_THRESHOLD_FIXED (153)
       */
#endif /* READ_GAMMA */
#ifdef PNG_READ_TRANSFORMS_SUPPORTED
   unsigned int invalid_info;      /* PNG_INFO_* for invalidated chunks */
   unsigned int palette_updated:1; /* png_struct::palette changed */
#endif /* READ_TRANSFORMS */
#ifdef PNG_SEQUENTIAL_READ_SUPPORTED
   unsigned int read_started   :1; /* at least one call to png_read_row */
#endif /* SEQUENTIAL_READ */
   /* The next field is just used by the read IDAT process functions to store
    * the state of IDAT processing; they should not be altered or used by other
    * functions.
    */
   unsigned int row_state      :2; /* state of row parsing (internal) */
#endif /* READ */

#if defined (PNG_READ_INTERLACING_SUPPORTED) ||\
    defined (PNG_WRITE_INTERLACING_SUPPORTED)
   unsigned int do_interlace   :1; /* libpng handles the interlace */
#  endif /* R/W INTERLACING */

   unsigned int pass           :3; /* current (interlace) pass (0 - 6) */

   /* The following fields are set by png_row_init to the pixel depths of the
    * pixels at various states.  If transforms are not supported they will
    * always be the same value:
    *
    *              READ               WRITE
    * input:        PNG          From application
    * output:  To application         PNG
    * max:           Largest in transform
    */
   unsigned int row_input_pixel_depth  :8;
   unsigned int row_output_pixel_depth :8;
   unsigned int row_max_pixel_depth    :8;

#  define PNG_RF_BITS 9 /* Number of bits required for the row format (below) */
#ifdef PNG_TRANSFORM_MECH_SUPPORTED
   /* The following fields describe the format of the user row; the output on
    * read or the input on write, and give the maximum pixel depth, which
    * controls the row buffer allocation size (row_allocated_bytes) above.
    */
   unsigned int row_range       :3;    /* range error count */
   unsigned int row_bit_depth   :6;    /* bits per channel (up to 32) */
   unsigned int row_format:PNG_RF_BITS;/* format of output(R)/input(W) row: */
   /*     PNG_FORMAT_FLAG_ALPHA    0x01U    format with an alpha channel
    *     PNG_FORMAT_FLAG_COLOR    0x02U    color format: otherwise grayscale
    *     PNG_FORMAT_FLAG_LINEAR   0x04U    NOT used (informational)
    *     PNG_FORMAT_FLAG_COLORMAP 0x08U    image data is color-mapped
    *     PNG_FORMAT_FLAG_BGR      0x10U    BGR colors, else order is RGB
    *     PNG_FORMAT_FLAG_AFIRST   0x20U    alpha channel comes first *
    *     PNG_FORAMT_FLAG_AFILLER  0x40U    The 'alpha' channel is a filler:
    *       PNG_FORMAT_FLAG_ALPHA is set however the value in the alpha channel
    *       is not an alpha value and (therefore) cannot be used for alpha
    *       computations, it is just a filler value.  PNG_COLOR_TYPE_FROM_FORMAT
    *       will return a color type *without* PNG_COLOR_MASK_ALPHA, however
    *       PNG_FORMAT_CHANNELS will return the correct number, including the
    *       filler channel.
    *     PNG_FORMAT_FLAG_SWAPPED  0x80U    bytes or bits swapped:
    *       When the bit depth is 16 this means that the bytes within the
    *       components have been swapped, when the bit depth is less than 8
    *       it means the pixels within the bytes have been swapped.  It should
    *       not be set for 8-bit compononents (it is meaningless).
    *     PNG_FORMAT_FLAG_RANGE   0x100U    component range not 0..bit-depth:
    *       Low-bit-depth grayscale components have been unpacked into bytes
    *       without scaling, or RGB[A] pixels have been shifted back to the
    *       significant-bit range from the sBIT chunk or channels (currently
    *       alpha or gray) have been inverted.
    *     PNG_FORMAT_FLAG_INVALID           NOT STORED HERE
    */
#ifdef PNG_WRITE_TRANSFORMS_SUPPORTED
   unsigned int info_format:PNG_RF_BITS;
      /* This field is used to validate the png_info used to write the
       * IHDR.  This is a new check in 1.7.0; previously it was possible to pass
       * a png_info from a png_read with the read tranform information in the
       * format having manually removed the required transforms from the rows
       * passed to png_write_row.
       */
#endif /* WRITE_TRANSFORMS */
#ifdef PNG_WRITE_INVERT_ALPHA_SUPPORTED
   unsigned int write_invert_alpha :1;
      /* This indicates the png_set_invert_alpha was called, it is used by the
       * write code to implement the transform without needing to run the whole
       * transform mechanism on the PNG palette data.
       */
#endif /* WRITE_INVERT_ALPHA */
#ifdef PNG_READ_RGB_TO_GRAY_SUPPORTED
   unsigned int rgb_to_gray_status :1;
      /* If set an RGB pixel was encountered by the RGB to gray transform
       * wherein !(r==g==b).
       */
#endif /* RGB_TO_GRAY */
#endif /* TRANSFORM_MECH */

#ifdef PNG_READ_SUPPORTED
   /* These, and IDAT_size below, control how much input and output (at most) is
    * available to zlib during read decompression.
    */
   png_alloc_size_t read_buffer_size; /* current size of the buffer */
#endif /* READ */

#if defined(PNG_SEQUENTIAL_READ_SUPPORTED) || defined(PNG_WRITE_SUPPORTED)
   png_uint_32 IDAT_size;         /* limit on IDAT read and write IDAT size */
#endif /* SEQUENTIAL_READ || WRITE */

   /* ERROR HANDLING */
#ifdef PNG_SETJMP_SUPPORTED
   jmp_buf        *jmp_buf_ptr;   /* passed to longjmp_fn */
   png_longjmp_ptr longjmp_fn;    /* setjmp non-local goto function. */
   size_t          jmp_buf_size;  /* size of *jmp_buf_ptr, if allocated */
#endif /* SETJMP */

   /* Error/warning callbacks */
   png_error_ptr error_fn;        /* print an error message and abort */
#ifdef PNG_WARNINGS_SUPPORTED
   png_error_ptr warning_fn;      /* print a warning and continue */
#endif /* WARNINGS */
   png_voidp     error_ptr;       /* user supplied data for the above */

   /* MEMORY ALLOCATION */
#ifdef PNG_USER_MEM_SUPPORTED
   png_malloc_ptr malloc_fn; /* allocate memory */
   png_free_ptr   free_fn;   /* free memory */
   png_voidp      mem_ptr;   /* user supplied data for the above */
#endif /* USER_MEM */

   /* IO and BASIC READ/WRITE SUPPORT */
   png_voidp            io_ptr;       /* user supplied data for IO callbacks */
   png_rw_ptr           rw_data_fn;   /* read/write some bytes (must succeed) */

#ifdef PNG_READ_SUPPORTED
   png_read_status_ptr read_row_fn;   /* called after each row is decoded */
   png_bytep           read_buffer;   /* buffer for reading chunk data */

   /* During read the following array is set up to point to the appropriate
    * un-filter function, this allows per-image and per-processor optimization.
    */
   void (*read_filter[PNG_FILTER_VALUE_LAST-1])(png_alloc_size_t row_bytes,
      unsigned int bpp, png_bytep row, png_const_bytep prev_row,
      png_const_bytep prev_pixels);
#endif /* READ */

#ifdef PNG_WRITE_SUPPORTED
   png_write_status_ptr    write_row_fn; /* called after each row is encoded */

#ifdef PNG_WRITE_FLUSH_SUPPORTED
   png_flush_ptr output_flush_fn; /* Function for flushing output */
#endif /* WRITE_FLUSH */
#endif /* WRITE */

#ifdef PNG_SET_USER_LIMITS_SUPPORTED
   png_uint_32 user_width_max;        /* Maximum width on read */
   png_uint_32 user_height_max;       /* Maximum height on read */
   /* Total memory that a single zTXt, sPLT, iTXt, iCCP, or unknown chunk
    * can occupy when decompressed.  0 means unlimited.  This field is a counter
    * - it is decremented as memory is allocated.
    */
   png_alloc_size_t user_chunk_malloc_max;
#endif /* SET_USER_LIMITS */
#ifdef PNG_USER_LIMITS_SUPPORTED
   /* limit on total *number* of sPLT, text and unknown chunks that can be
    * stored.  0 means unlimited.  This field is a counter - it is decremented
    * as chunks are encountered.
    */
   png_uint_32 user_chunk_cache_max;
#endif /* USER_LIMITS */

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

   size_t      save_buffer_size;     /* amount of data now in save_buffer */
   size_t      save_buffer_max;      /* total size of save_buffer */
   size_t      current_buffer_size;  /* amount of data now in current_buffer */
   size_t      buffer_size;          /* total amount of available input data */

   unsigned int process_mode :8;
      /* This is one or two four bit codes describing the current state of the
       * 'push' reader.  Normally the low four bits are a state code, however in
       * some cases this may be pushed to the top four bits and replaced by a
       * different temporary state code.  The value is, in effect, a two entry
       * stack.
       */
#endif /* PROGRESSIVE_READ */

#ifdef PNG_IO_STATE_SUPPORTED
   png_uint_32          io_state;     /* tells the app read/write progress */
#endif /* IO_STATE */

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
#endif /* USER_CHUNKS */

#ifdef PNG_READ_USER_CHUNKS_SUPPORTED
   /* This is called back from the unknown chunk handling */
   png_user_chunk_ptr     read_user_chunk_fn; /* user read chunk handler */
#endif /* READ_USER_CHUNKS */

#ifdef PNG_HANDLE_AS_UNKNOWN_SUPPORTED
   png_uint_32  known_unknown;   /* Bit mask of known chunks to be treated as
                                  * unknown in the read code.
                                  */
#ifdef PNG_SAVE_UNKNOWN_CHUNKS_SUPPORTED
   png_uint_32  save_unknown;    /* Whether to save or skip these chunks:
                                  * 'save' is 'known & save', 'skip' is
                                  * 'known & ~save'.
                                  */
#  define png_IDATs_skipped(pp) (((pp)->known_unknown & ~(pp)->save_unknown)&1U)
#else /* !SAVE_UNKNOWN_CHUNKS */
#  define png_IDATs_skipped(pp) ((pp)->known_unknown & 1U)
#endif /* !SAVE_UNKNOWN_CHUNKS */
#endif /* HANDLE_AS_UNKNOWN */

#ifdef PNG_SET_UNKNOWN_CHUNKS_SUPPORTED
   png_bytep    chunk_list;      /* List of png_byte[5]; the textual chunk name
                                  * followed by a PNG_HANDLE_* byte */
   unsigned int unknown_default :2; /* As PNG_HANDLE_* */
   unsigned int num_chunk_list;  /* Number of entries in the list */
#endif /* SET_UNKNOWN_CHUNKS */

   /* COMPRESSION AND DECOMPRESSION SUPPORT.
    *
    * zlib expects a 'zstream' as the fundamental control structure, it allows
    * all the parameters to be passed as one pointer.
    */
   png_uint_32     zowner;       /* ID (chunk type) of zlib owner, 0 if none */
#  ifdef PNG_WRITE_SUPPORTED
   png_zlib_statep zlib_state;   /* State of zlib compression */
#  endif /* WRITE */
#  ifdef PNG_READ_SUPPORTED
   z_stream     zstream;         /* decompression structure */
   unsigned int zstream_ended:1; /* no more zlib output available [read] */
   unsigned int zstream_error:1; /* zlib error message has been output [read] */
#  endif /* READ */
#  ifdef PNG_PROGRESSIVE_READ_SUPPORTED
   unsigned int zstream_eod  :1; /* all the required uncompressed data has been
                                  * received; set by the zstream using code for
                                  * its own purposes. [progressive read] */
#  endif /* PROGRESSIVE_READ */
#  ifdef PNG_BENIGN_ERRORS_SUPPORTED
      unsigned int benign_error_action :2;
      unsigned int app_warning_action  :2;
      unsigned int app_error_action    :2;
#  ifdef PNG_READ_SUPPORTED
      unsigned int IDAT_error_action   :2;
#  endif /* READ */
#  endif /* BENIGN_ERRORS */
#  ifdef PNG_READ_SUPPORTED
      /* CRC checking actions, one for critical chunks, one for ancillary
       * chunks.
       */
      unsigned int critical_crc  :2;
      unsigned int ancillary_crc :2;
      unsigned int current_crc   :2; /* Cache of one or other of the above */
#  endif
#  ifdef PNG_SET_OPTION_SUPPORTED
#  ifdef PNG_READ_SUPPORTED
         unsigned int maximum_inflate_window  :1U;
#  endif /* READ */
         unsigned int skip_sRGB_profile_check :1U;
#  endif /* SET_OPTION */

   /* MNG SUPPORT */
#ifdef PNG_MNG_FEATURES_SUPPORTED
   unsigned int mng_features_permitted :3;
#endif /* MNG_FEATURES */

   /* SCRATCH buffers, used when control returns to the application or a read
    * loop.
    */
#  ifdef PNG_READ_SUPPORTED
   png_byte scratch[PNG_ROW_BUFFER_SIZE+16U];
#  endif /* READ */
};
#endif /* PNGSTRUCT_H */
