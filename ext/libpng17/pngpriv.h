
/* pngpriv.h - private declarations for use inside libpng
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

/* The symbols declared in this file (including the functions declared
 * as extern) are PRIVATE.  They are not part of the libpng public
 * interface, and are not recommended for use by regular applications.
 * Some of them may become public in the future; others may stay private,
 * change in an incompatible way, or even disappear.
 * Although the libpng users are not forbidden to include this header,
 * they should be well aware of the issues that may arise from doing so.
 */

#ifndef PNGPRIV_H
#define PNGPRIV_H

/* Feature Test Macros.  The following are defined here to ensure that correctly
 * implemented libraries reveal the APIs libpng needs to build and hide those
 * that are not needed and potentially damaging to the compilation.
 *
 * Feature Test Macros must be defined before any system header is included (see
 * POSIX 1003.1 2.8.2 "POSIX Symbols."
 *
 * These macros only have an effect if the operating system supports either
 * POSIX 1003.1 or C99, or both.  On other operating systems (particularly
 * Windows/Visual Studio) there is no effect; the OS specific tests below are
 * still required (as of 2011-05-02.)
 */
#define _POSIX_SOURCE 1 /* Just the POSIX 1003.1 and C89 APIs */

#ifndef PNG_VERSION_INFO_ONLY
/* Keep standard libraries at the top of this file */

/* Standard library headers not required by png.h: */
#  include <stdlib.h>
#  include <string.h>

/* For headers only required with some build configurations see the lines after
 * pnglibconf.h is included!
 */

#endif /* VERSION_INFO_ONLY */

#define PNGLIB_BUILD /*libpng is being built, not used*/

/* If HAVE_CONFIG_H is defined during the build then the build system must
 * provide an appropriate "config.h" file on the include path.  The header file
 * must provide definitions as required below (search for "HAVE_CONFIG_H");
 * see configure.ac for more details of the requirements.  The macro
 * "PNG_NO_CONFIG_H" is provided for maintainers to test for dependencies on
 * 'configure'; define this macro to prevent the configure build including the
 * configure generated config.h.  Libpng is expected to compile without *any*
 * special build system support on a reasonably ANSI-C compliant system.
 */
#if defined(HAVE_CONFIG_H) && !defined(PNG_NO_CONFIG_H)
#  include <config.h>

   /* Pick up the definition of 'restrict' from config.h if it was read: */
#  define PNG_RESTRICT restrict
#endif

/* To support symbol prefixing it is necessary to know *before* including png.h
 * whether the fixed point (and maybe other) APIs are exported, because if they
 * are not internal definitions may be required.  This is handled below just
 * before png.h is included, but load the configuration now if it is available.
 */
#ifndef PNGLCONF_H
#  include "pnglibconf.h"
#endif

/* Local renames may change non-exported API functions from png.h */
#if defined(PNG_PREFIX) && !defined(PNGPREFIX_H)
#  include "pngprefix.h"
#endif

#ifdef PNG_USER_CONFIG
#  include "pngusr.h"
   /* These should have been defined in pngusr.h */
#  ifndef PNG_USER_PRIVATEBUILD
#    define PNG_USER_PRIVATEBUILD "Custom libpng build"
#  endif
#  ifndef PNG_USER_DLLFNAME_POSTFIX
#    define PNG_USER_DLLFNAME_POSTFIX "Cb"
#  endif
#endif

#ifndef PNG_VERSION_INFO_ONLY
/* Additional standard libaries required in certain cases, put only standard
 * ANSI-C89 headers here.  If not available, or non-functional, the problem
 * should be fixed by writing a wrapper for the header and the file on your
 * include path.
 */
#if defined(PNG_sCAL_SUPPORTED) && defined(PNG_FLOATING_POINT_SUPPORTED)
   /* png.c requires the following ANSI-C constants if the conversion of
    * floating point to ASCII is implemented therein:
    *
    *  DBL_MIN_10_EXP Minimum negative integer such that 10^integer is a
    *                 normalized (double) value.
    *  DBL_DIG  Maximum number of decimal digits (can be set to any constant)
    *  DBL_MIN  Smallest normalized fp number (can be set to an arbitrary value)
    *  DBL_MAX  Maximum floating point number (can be set to an arbitrary value)
    */
#  include <float.h>
#endif /* sCAL && FLOATING_POINT */

#if defined(PNG_FLOATING_ARITHMETIC_SUPPORTED) ||\
   defined(PNG_FLOATING_POINT_SUPPORTED)
   /* ANSI-C90 math functions are required.  Full compliance with the standard
    * is probably not a requirement, but the functions must exist and be
    * declared in <math.h>
    */
#  include <math.h>
#endif /* FLOATING_ARITHMETIC || FLOATING_POINT */

#endif /* VERSION_INFO_ONLY */

/* Is this a build of a DLL where compilation of the object modules requires
 * different preprocessor settings to those required for a simple library?  If
 * so PNG_BUILD_DLL must be set.
 *
 * If libpng is used inside a DLL but that DLL does not export the libpng APIs
 * PNG_BUILD_DLL must not be set.  To avoid the code below kicking in build a
 * static library of libpng then link the DLL against that.
 */
#ifndef PNG_BUILD_DLL
#  ifdef DLL_EXPORT
      /* This is set by libtool when files are compiled for a DLL; libtool
       * always compiles twice, even on systems where it isn't necessary.  Set
       * PNG_BUILD_DLL in case it is necessary:
       */
#     define PNG_BUILD_DLL
#  else
#     ifdef _WINDLL
         /* This is set by the Microsoft Visual Studio IDE in projects that
          * build a DLL.  It can't easily be removed from those projects (it
          * isn't visible in the Visual Studio UI) so it is a fairly reliable
          * indication that PNG_IMPEXP needs to be set to the DLL export
          * attributes.
          */
#        define PNG_BUILD_DLL
#     else
#        ifdef __DLL__
            /* This is set by the Borland C system when compiling for a DLL
             * (as above.)
             */
#           define PNG_BUILD_DLL
#        else
            /* Add additional compiler cases here. */
#        endif
#     endif
#  endif
#endif /* Setting PNG_BUILD_DLL if required */

/* See pngconf.h for more details: the builder of the library may set this on
 * the command line to the right thing for the specific compilation system or it
 * may be automagically set above (at present we know of no system where it does
 * need to be set on the command line.)
 *
 * PNG_IMPEXP must be set here when building the library to prevent pngconf.h
 * setting it to the "import" setting for a DLL build.
 */
#ifndef PNG_IMPEXP
#  ifdef PNG_BUILD_DLL
#     define PNG_IMPEXP PNG_DLL_EXPORT
#  else
      /* Not building a DLL, or the DLL doesn't require specific export
       * definitions.
       */
#     define PNG_IMPEXP
#  endif
#endif

/* No warnings for private or deprecated functions in the build: */
#ifndef PNG_DEPRECATED
#  define PNG_DEPRECATED
#endif
#ifndef PNG_PRIVATE
#  define PNG_PRIVATE
#endif

/* Symbol preprocessing support.
 *
 * To enable listing global, but internal, symbols the following macros should
 * always be used to declare an extern data or function object in this file.
 */
#ifndef PNG_INTERNAL_DATA
#  define PNG_INTERNAL_DATA(type, name, array) extern type name array
#endif

#ifndef PNG_INTERNAL_FUNCTION
#  define PNG_INTERNAL_FUNCTION(type, name, args, attributes)\
      extern PNG_FUNCTION(type, name, args, PNG_EMPTY attributes)
#endif

#ifndef PNG_INTERNAL_CALLBACK
#  define PNG_INTERNAL_CALLBACK(type, name, args, attributes)\
      extern PNG_FUNCTION(type, (PNGCBAPI name), args, PNG_EMPTY attributes)
#endif

/* If floating or fixed point APIs are disabled they may still be compiled
 * internally.  To handle this make sure they are declared as the appropriate
 * internal extern function (otherwise the symbol prefixing stuff won't work and
 * the functions will be used without definitions.)
 *
 * NOTE: although all the API functions are declared here they are not all
 * actually built!  Because the declarations are still made it is necessary to
 * fake out types that they depend on.
 */
#ifndef PNG_FP_EXPORT
#  ifndef PNG_FLOATING_POINT_SUPPORTED
#     define PNG_FP_EXPORT(ordinal, type, name, args)\
         PNG_INTERNAL_FUNCTION(type, name, args, PNG_EMPTY);
#     ifndef PNG_VERSION_INFO_ONLY
         typedef struct png_incomplete png_double;
         typedef png_double*           png_doublep;
         typedef const png_double*     png_const_doublep;
         typedef png_double**          png_doublepp;
#     endif
#  endif
#endif
#ifndef PNG_FIXED_EXPORT
#  ifndef PNG_FIXED_POINT_SUPPORTED
#     define PNG_FIXED_EXPORT(ordinal, type, name, args)\
         PNG_INTERNAL_FUNCTION(type, name, args, PNG_EMPTY);
#  endif
#endif

/* Include png.h here to get the version info and other macros, pngstruct.h and
 * pnginfo.h are included later under the protection of !PNG_VERSION_INFO_ONLY
 */
#include "png.h"

/* pngconf.h does not set PNG_DLL_EXPORT unless it is required, so: */
#ifndef PNG_DLL_EXPORT
#  define PNG_DLL_EXPORT
#endif

/* This is a global switch to set the compilation for an installed system
 * (a release build).  It can be set for testing debug builds to ensure that
 * they will compile when the build type is switched to RC or STABLE, the
 * default is just to use PNG_LIBPNG_BUILD_BASE_TYPE.  Set this in CPPFLAGS
 * with either:
 *
 *   -DPNG_RELEASE_BUILD Turns on the release compile path
 *   -DPNG_RELEASE_BUILD=0 Turns it off
 * or in your pngusr.h with
 *   #define PNG_RELEASE_BUILD=1 Turns on the release compile path
 *   #define PNG_RELEASE_BUILD=0 Turns it off
 */
#ifndef PNG_RELEASE_BUILD
#  define PNG_RELEASE_BUILD (PNG_LIBPNG_BUILD_BASE_TYPE >= PNG_LIBPNG_BUILD_RC)
#endif

/* General purpose macros avoid the need to put #if PNG_RELEASE_BUILD
 * macro blocks around function declarations and definitions when the
 * parameter number varies.  Using these results in slightly cleaner code.
 */
#if PNG_RELEASE_BUILD
#  define only_rel(text) text
#  define only_deb(text)
#  define param_rel(param) param,
#  define param_deb(param)
#else
#  define only_rel(text)
#  define only_deb(text) text
#  define param_rel(param)
#  define param_deb(param) param,
#endif

/* The affirm mechanism results in a minimal png_error() in released versions
 * ('STABLE' versions) and a more descriptive PNG_ABORT in all other cases,
 * when the "condition" is false (zero).  If "condition" is true (nonzero),
 * then the affirm mechanism does nothing.
 *
 * The PNG_RELEASE_BUILD macro, defined above, controls the behavior of
 * 'affirm': if set to 1 affirm will call png_error (or png_err) rather than
 * abort.  The png_error text is the minimal (file location) text in this case,
 * if it is produced. This flag indicates a STABLE (or RC) build.
 *
 * The macros rely on the naming convention throughout this code - png_ptr
 * exists and is of type png_const_structrp or a compatible type - and the
 * presence in each file of a uniquely defined macro PNG_SRC_FILE; a number
 * indicating which file this is (this is to save space in released versions).
 *
 * 'affirm' is intended to look like the ANSI-C <assert.h> macro; note that
 * this macro can coexist with the assert macro if <assert.h> is
 * included.
 *
 * PNG_SRC_LINE is the position of the affirm macro.  There are currently 15
 * main source files (4 bits) and the biggest (pngrtran.c) has more than 4095
 * lines (12 bits).  However, to ensure the number will fit into 16-bits in the
 * future and to allow hardware files to use affirm, the encoding is a bit-wise
 * encoding based on the current number of lines.
 *
 * 'debug' is a version of 'affirm' that is completely removed from RELEASE
 * builds.  This is used when either an unexpected condition is completely
 * handled or when it can't be handled even by png_error, for example after a
 * memory overwrite.
 *
 * UNTESTED is used to mark code that has not been tested; it causes an assert
 * if the code is executed and (therefore) tested.  UNTESTED should not remain
 * in release candidate code.
 *
 * PNG_AFFIRM_TEXT is set to 1 if affirm text should be produced, either
 * the minimal text or, if PNG_RELEASE_BUILD is 0, the more verbose text
 * including the 'condition' string.  This value depends on whether the
 * build supports an appropriate way of outputting the message.
 *
 * Note that PNG_AFFIRM_TEXT is not configurable but is worked out here: this
 * is just the affirm code; there's no reason to allow configuration of this
 * option.
 */
#if PNG_RELEASE_BUILD ?\
      (defined PNG_ERROR_TEXT_SUPPORTED) :\
      (defined PNG_WARNINGS_SUPPORTED) || (defined PNG_CONSOLE_IO_SUPPORTED)
#  define PNG_AFFIRM_TEXT 1
#else
#  define PNG_AFFIRM_TEXT 0
#endif /* PNG_AFFIRM_TEXT definition */

#define PNG_SRC_LINE (PNG_SRC_FILE + __LINE__)

/* png_affirmpp and png_impossiblepp are macros to make the correct call to the
 * png_affirm function; these macros do not assume that the png_structp is
 * called png_ptr.
 */
#if PNG_RELEASE_BUILD
#  define png_affirmpp(pp, cond)\
      do\
         if (!(cond)) png_affirm(pp, PNG_SRC_LINE);\
      while (0)
#  define png_affirmexp(pp, cond)\
      ((cond) ? (void)0 : png_affirm(pp, PNG_SRC_LINE))
#  define png_handled(pp, m) ((void)0)
#  define png_impossiblepp(pp, reason) png_affirm(pp, PNG_SRC_LINE)

#  define debug(cond) do {} while (0)
#  define debug_handled(cond) do {} while (0)
#  if PNG_LIBPNG_BUILD_BASE_TYPE >= PNG_LIBPNG_BUILD_RC
     /* Make sure there are no 'UNTESTED' macros in released code: */
#    define UNTESTED libpng untested code
#  endif
#  define NOT_REACHED do {} while (0)
#else
#  define png_affirmpp(pp, cond)\
      do\
         if (!(cond)) png_affirm(pp, #cond, PNG_SRC_LINE);\
      while (0)
#  define png_affirmexp(pp, cond)\
      ((cond) ? (void)0 : png_affirm(pp, #cond, PNG_SRC_LINE))
#  define png_handled(pp, m) (png_handled_affirm((pp), (m), PNG_SRC_LINE))
#  define png_impossiblepp(pp, reason) png_affirm(pp, reason, PNG_SRC_LINE)

#  define debug(cond) png_affirmpp(png_ptr, cond)
#  define debug_handled(cond)\
      do\
         if (!(cond)) png_handled(png_ptr, #cond);\
      while (0)
#  define UNTESTED png_affirm(png_ptr, "untested code", PNG_SRC_LINE);
#  define NOT_REACHED png_affirm(png_ptr, "NOT REACHED", PNG_SRC_LINE)
#endif

#define affirm(cond) png_affirmpp(png_ptr, cond)
#define affirmexp(cond) png_affirmexp(png_ptr, cond)
#define handled(m) png_handled(png_ptr, (m))
#define impossible(cond) png_impossiblepp(png_ptr, cond)
#define implies(a, b) debug(!(a) || (b))

/* The defines for PNG_SRC_FILE: */
#define PNG_SRC_FILE_(f,lines) PNG_SRC_FILE_ ## f + lines

#define PNG_SRC_FILE_png      0
#define PNG_SRC_FILE_pngerror (PNG_SRC_FILE_png      +8192)
#define PNG_SRC_FILE_pngget   (PNG_SRC_FILE_pngerror +2048)
#define PNG_SRC_FILE_pngmem   (PNG_SRC_FILE_pngget   +2048)
#define PNG_SRC_FILE_pngpread (PNG_SRC_FILE_pngmem   +1024)
#define PNG_SRC_FILE_pngread  (PNG_SRC_FILE_pngpread +2048)
#define PNG_SRC_FILE_pngrio   (PNG_SRC_FILE_pngread  +8192)
#define PNG_SRC_FILE_pngrtran (PNG_SRC_FILE_pngrio   +1024)
#define PNG_SRC_FILE_pngrutil (PNG_SRC_FILE_pngrtran +8192)
#define PNG_SRC_FILE_pngset   (PNG_SRC_FILE_pngrutil +8192)
#define PNG_SRC_FILE_pngtrans (PNG_SRC_FILE_pngset   +2048)
#define PNG_SRC_FILE_pngwio   (PNG_SRC_FILE_pngtrans +4096)
#define PNG_SRC_FILE_pngwrite (PNG_SRC_FILE_pngwio   +1024)
#define PNG_SRC_FILE_pngwtran (PNG_SRC_FILE_pngwrite +4096)
#define PNG_SRC_FILE_pngwutil (PNG_SRC_FILE_pngwtran +1024)

#define PNG_SRC_FILE_arm_arm_init (PNG_SRC_FILE_pngwutil +8192)
#define PNG_SRC_FILE_arm_filter_neon_intrinsics\
             (PNG_SRC_FILE_arm_arm_init +1024)

/* Add new files by changing the following line: */
#define PNG_SRC_FILE_LAST (PNG_SRC_FILE_arm_filter_neon_intrinsics +1024)

/* The following #define must list the files in exactly the same order as
 * the above.
 */
#define PNG_FILES\
   PNG_apply(png)\
   PNG_apply(pngerror)\
   PNG_apply(pngget)\
   PNG_apply(pngmem)\
   PNG_apply(pngpread)\
   PNG_apply(pngread)\
   PNG_apply(pngrio)\
   PNG_apply(pngrtran)\
   PNG_apply(pngrutil)\
   PNG_apply(pngset)\
   PNG_apply(pngtrans)\
   PNG_apply(pngwio)\
   PNG_apply(pngwrite)\
   PNG_apply(pngwtran)\
   PNG_apply(pngwutil)\
   PNG_apply(arm_arm_init)\
   PNG_apply(arm_filter_neon_intrinsics)\
   PNG_end

/* SECURITY and SAFETY:
 *
 * libpng is built with support for internal limits on image dimensions and
 * memory usage.  These are documented in scripts/pnglibconf.dfa of the
 * source and recorded in the machine generated header file pnglibconf.h.
 */

/* If you are running on a machine where you cannot allocate more
 * than 64K of memory at once, uncomment this.  While libpng will not
 * normally need that much memory in a chunk (unless you load up a very
 * large file), zlib needs to know how big of a chunk it can use, and
 * libpng thus makes sure to check any memory allocation to verify it
 * will fit into memory.
 *
 * zlib provides 'MAXSEG_64K' which, if defined, indicates the
 * same limit and pngconf.h (already included) sets the limit
 * if certain operating systems are detected.
 */
#if defined(MAXSEG_64K) && !defined(PNG_MAX_MALLOC_64K)
#  define PNG_MAX_MALLOC_64K
#endif

#ifndef PNG_UNUSED
/* Unused formal parameter warnings are silenced using the following macro
 * which is expected to have no bad effects on performance (optimizing
 * compilers will probably remove it entirely).  Note that if you replace
 * it with something other than whitespace, you must include the terminating
 * semicolon.
 */
#  define PNG_UNUSED(param) (void)param;
#endif

/* This is a convenience for parameters which are not used in release
 * builds.
 */
#define PNG_UNUSEDRC(param) only_rel(PNG_UNUSED(param))

/* Just a little check that someone hasn't tried to define something
 * contradictory.
 */
#if (PNG_ZBUF_SIZE > 32768) && defined(PNG_MAX_MALLOC_64K)
#  undef PNG_ZBUF_SIZE
#  define PNG_ZBUF_SIZE 32768
#endif

/* If warnings or errors are turned off the code is disabled or redirected here.
 * From 1.5.4 functions have been added to allow very limited formatting of
 * error and warning messages - this code will also be disabled here.
 */
#ifdef PNG_WARNINGS_SUPPORTED
#  define PNG_WARNING_PARAMETERS(p) png_warning_parameters p;
#else
#  define png_warning_parameter(p,number,string) ((void)0)
#  define png_warning_parameter_unsigned(p,number,format,value) ((void)0)
#  define png_warning_parameter_signed(p,number,format,value) ((void)0)
#  define png_formatted_warning(pp,p,message) ((void)(pp))
#  define PNG_WARNING_PARAMETERS(p)
#endif
#ifndef PNG_ERROR_TEXT_SUPPORTED
#  define png_fixed_error(s1,s2) png_err(s1)
#endif

/* C allows up-casts from (void*) to any pointer and (const void*) to any
 * pointer to a const object.  C++ regards this as a type error and requires an
 * explicit, static, cast and provides the static_cast<> rune to ensure that
 * const is not cast away.
 */
#ifdef __cplusplus
#  define png_voidcast(type, value) static_cast<type>(value)
#  define png_upcast(type, value) static_cast<type>(value)
#  define png_constcast(type, value) const_cast<type>(value)
#  define png_aligncast(type, value) \
   static_cast<type>(static_cast<void*>(value))
#  define png_aligncastconst(type, value) \
   static_cast<type>(static_cast<const void*>(value))
#else
#  define png_voidcast(type, value) (value)
#  define png_upcast(type, value) ((type)(value))
#  define png_constcast(type, value) ((type)(value))
#  define png_aligncast(type, value) ((void*)(value))
#  define png_aligncastconst(type, value) ((const void*)(value))
#endif /* __cplusplus */

/* Some fixed point APIs are still required even if not exported because
 * they get used by the corresponding floating point APIs.  This magic
 * deals with this:
 */
#ifdef PNG_FIXED_POINT_SUPPORTED
#  define PNGFAPI PNGAPI
#else
#  define PNGFAPI /* PRIVATE */
#endif

/* These macros may need to be architecture dependent. */
#define PNG_ALIGN_NONE   0 /* do not use data alignment */
#define PNG_ALIGN_ALWAYS 1 /* assume unaligned accesses are OK */
#ifdef offsetof
#  define PNG_ALIGN_OFFSET 2 /* use offsetof to determine alignment */
#else
#  define PNG_ALIGN_OFFSET -1 /* prevent the use of this */
#endif
#define PNG_ALIGN_SIZE   3 /* use sizeof to determine alignment */

#ifndef PNG_ALIGN_TYPE
   /* Default to using aligned access optimizations and requiring alignment to a
    * multiple of the data type size.  Override in a compiler specific fashion
    * if necessary by inserting tests here:
    */
#  define PNG_ALIGN_TYPE PNG_ALIGN_SIZE
#endif

#if PNG_ALIGN_TYPE == PNG_ALIGN_SIZE
   /* This is used because in some compiler implementations non-aligned
    * structure members are supported, so the offsetof approach below fails.
    * Set PNG_ALIGN_SIZE=0 for compiler combinations where unaligned access
    * is good for performance.  Do not do this unless you have tested the result
    * and understand it.
    */
#  define png_alignof(type) (sizeof (type))
#else
#  if PNG_ALIGN_TYPE == PNG_ALIGN_OFFSET
#     define png_alignof(type) offsetof(struct{char c; type t;}, t)
#  else
#     if PNG_ALIGN_TYPE == PNG_ALIGN_ALWAYS
#        define png_alignof(type) (1)
#     endif
      /* Else leave png_alignof undefined to prevent use thereof */
#  endif
#endif

/* This implicitly assumes alignment is always to a power of 2. */
#ifdef png_alignof
#  define png_isaligned(ptr, type)\
   ((((const char*)ptr-(const char*)0) & (png_alignof(type)-1)) == 0)
#else
#  define png_isaligned(ptr, type) 0
#endif

/* Buffer alignment control.  These #defines control how the buffers used during
 * read are aligned and how big they are.
 */
#ifndef PNG_ROW_BUFFER_ALIGN_TYPE
   /* The absolute minimum alignment for a row buffer is that required for
    * png_uint_32 direct access.  The #define is of a legal C type that can be
    * used as the type in the definition of the first member of a C union; give
    * a typedef name if in doubt.
    */
#  define PNG_ROW_BUFFER_ALIGN_TYPE png_uint_32
#endif /* !ROW_BUFFER_ALIGN_TYPE */
#ifndef PNG_ROW_BUFFER_BYTE_ALIGN
   /* This is the minimum size in bytes of the buffer used while processing
    * parts of row.  Except at the end of the row pixels will always be
    * processed in blocks such that the block size is a multiple of this number
    */
#  define PNG_ROW_BUFFER_BYTE_ALIGN\
   ((unsigned int)/*SAFE*/(sizeof (PNG_ROW_BUFFER_ALIGN_TYPE)))
#endif /* !ROW_BUFFER_BYTE_ALIGN */
#ifdef PNG_READ_USER_TRANSFORM_SUPPORTED
#  define PNG_MAX_PIXEL_BYTES 16U /* 4x32-bit channels */
#else /* !READ_USER_TRANSFORM */
#  define PNG_MAX_PIXEL_BYTES  8U /* 4x16-bit channels */
#endif /* !READ_USER_TRANSFORM */
/* PNG_ROW_BUFFER_SIZE is a compile time constant for the size of the row
 * buffer.  The minimum size of 2048 bytes is intended to allow the buffer to
 * hold a complete 256 entry color map of 64-bit (8-byte) pixels.  This is a
 * requirement at some points of the colormap handling code.
 *
 * The maximum size is intended to allow (unsigned int) indexing of the buffer,
 * it only affects systems with a 16-bit unsigned value where it limits the
 * maximum to 4096 bytes.
 */
#define PNG_MIN_ROW_BUFFER_SIZE\
   (PNG_MAX_PIXEL_BYTES * PNG_ROW_BUFFER_BYTE_ALIGN * 8U)
#define PNG_MAX_ROW_BUFFER_SIZE ((UINT_MAX / 16U) + 1U)
#ifndef PNG_ROW_BUFFER_SIZE
#  define PNG_ROW_BUFFER_SIZE\
   (PNG_MIN_ROW_BUFFER_SIZE < 2048U ? 2048U : PNG_MIN_ROW_BUFFER_SIZE)
#endif /* ROW_BUFFER_SIZE */

/* End of memory model/platform independent support */
/* End of 1.5.0beta36 move from pngconf.h */

/* CONSTANTS and UTILITY MACROS
 * These are used internally by libpng and not exposed in the API
 */

/* Various modes of operation.  Note that after an init, mode is set to
 * zero automatically when the structure is created.  Three of these
 * are defined in png.h because they need to be visible to applications
 * that call png_set_unknown_chunk().
 */
/* #define PNG_HAVE_IHDR            0x01 (defined as (int) in png.h) */
/* #define PNG_HAVE_PLTE            0x02 (defined as (int) in png.h) */
#define PNG_HAVE_IDAT               0x04U
/* #define PNG_AFTER_IDAT           0x08 (defined as (int) in png.h) */
#define PNG_HAVE_IEND               0x10U
#define PNG_HAVE_PNG_SIGNATURE      0x20U

#if defined(PNG_SIMPLIFIED_READ_SUPPORTED) ||\
   defined(PNG_SIMPLIFIED_WRITE_SUPPORTED)
/* See below for the definitions of the tables used in these macros */
#define PNG_sRGB_FROM_LINEAR(pp, linear) png_check_byte(pp,\
   (png_sRGB_base[(linear)>>15] +\
    ((((linear)&0x7fffU)*png_sRGB_delta[(linear)>>15])>>12)) >> 8)
   /* Given a value 'linear' in the range 0..255*65535 calculate the 8-bit sRGB
    * encoded value with maximum error 0.646365.  Note that the input is not a
    * 16-bit value; it has been multiplied by 255! */
#endif /* PNG_SIMPLIFIED_READ/WRITE */

/* Added to libpng-1.6.0: scale a 16-bit value in the range 0..65535 to 0..255
 * by dividing by 257 *with rounding*.  This macro is exact for the given range.
 * See the discourse in pngrtran.c png_do_scale_16_to_8.  The values in the
 * macro were established by experiment (modifying the added value).  The macro
 * has a second variant that takes a value already scaled by 255 and divides by
 * 65535 - this has a maximum error of .502.  Over the range 0..65535*65535 it
 * only gives off-by-one errors and only for 0.5% (1 in 200) of the values.
 */
#define PNG_DIV65535(v24) (((v24) + 32895U) >> 16)
#define PNG_DIV257(v16) PNG_DIV65535((png_uint_32)(v16) * 255U)

/* Added to libpng-1.2.6 JB
 * Modified in libpng-1.7.0 to avoid the intermediate calculation overflow
 * when:
 *
 *  pixel_bits == 4: any width over 0x3FFFFFFEU overflows
 *  pixel_bits == 2: any width over 0x7FFFFFFCU overflows
 *
 * In both these cases any width results in a rowbytes that fits in 32 bits.
 * The problem arose in previous versions because the calculation used was
 * simply ((width x pixel-bit-depth)+7)/8.  At the cost of more calculations
 * on pixel_depth this avoids the problem.
 */
#define PNG_SHIFTOF(pixel_bits/*<8*/) \
   ( (pixel_bits) == 1 ? 3 : \
   ( (pixel_bits) == 2 ? 2 : \
   ( (pixel_bits) == 4 ? 1 : \
                         0/*force bytes*/ ) ) )
#define PNG_ADDOF(pixel_bits/*<8*/) ((1U<<PNG_SHIFTOF(pixel_bits))-1)
#define PNG_ROWBYTES(pixel_bits, width) \
   ((pixel_bits) >= 8 ? \
   ((png_alloc_size_t)(width) * ((pixel_bits) >> 3)) : \
   (((png_alloc_size_t)(width) + PNG_ADDOF(pixel_bits)) >> \
      PNG_SHIFTOF(pixel_bits)) )

/* This macros, added in 1.7.0, makes it easy to deduce the number of channels
 * and therefore the pixel depth from the color type.  The PNG specification
 * numbers are used in preference to the png.h constants to make it more clear
 * why the macro works.
 */
#define PNG_COLOR_TYPE_CHANNELS(ct)\
   (((ct) & PNG_COLOR_MASK_PALETTE) ?\
      1U : 1U+((ct) & 2U/*COLOR*/)+(((ct)>>2)&1U/*ALPHA*/))
#define PNG_CHANNELS(ps) PNG_COLOR_TYPE_CHANNELS((ps).color_type)
#define PNG_PIXEL_DEPTH(ps) (PNG_CHANNELS(ps) * (ps).bit_depth)

/* PNG_OUT_OF_RANGE returns true if value is outside the range
 * ideal-delta..ideal+delta.  Each argument is evaluated twice.
 * "ideal" and "delta" should be constants, normally simple
 * integers, "value" a variable. Added to libpng-1.2.6 JB
 */
#define PNG_OUT_OF_RANGE(value, ideal, delta) \
   ( (value) < (ideal)-(delta) || (value) > (ideal)+(delta) )

/* Handling of bit-field masks.  Because the expression:
 *
 *     bit_field & ~mask
 *
 * has implementation defined behavior in ANSI C-90 for many (int) values of
 * 'mask' and because some of these are defined in png.h and passed in (int)
 * parameters use of '~' has been expunged in libpng 1.7 and replaced by this
 * macro, which is well defined in ANSI C-90 (there is a similar, 16-bit,
 * version in pngstruct.h for the colorspace flags.)
 */
#define PNG_BIC_MASK(flags) (0xFFFFFFFFU - (flags))

/* Conversions between fixed and floating point, only defined if
 * required (to make sure the code doesn't accidentally use float
 * when it is supposedly disabled.)
 */
#ifdef PNG_FLOATING_POINT_SUPPORTED
/* The floating point conversion can't overflow, though it can and
 * does lose accuracy relative to the original fixed point value.
 * In practice this doesn't matter because png_fixed_point only
 * stores numbers with very low precision.  The png_ptr and s
 * arguments are unused by default but are there in case error
 * checking becomes a requirement.
 */
#define png_float(png_ptr, fixed, s) (.00001 * (fixed))

/* The fixed point conversion performs range checking and evaluates
 * its argument multiple times, so must be used with care.  The
 * range checking uses the PNG specification values for a signed
 * 32 bit fixed point value except that the values are deliberately
 * rounded-to-zero to an integral value - 21474 (21474.83 is roughly
 * (2^31-1) * 100000). 's' is a string that describes the value being
 * converted.
 *
 * NOTE: this macro will raise a png_error if the range check fails,
 * therefore it is normally only appropriate to use this on values
 * that come from API calls or other sources where an out of range
 * error indicates a programming error, not a data error!
 *
 * NOTE: by default this is off - the macro is not used - because the
 * function call saves a lot of code.
 */
#ifdef PNG_FIXED_POINT_MACRO_SUPPORTED
#define png_fixed(png_ptr, fp, s) ((fp) <= 21474 && (fp) >= -21474 ?\
    ((png_fixed_point)(100000 * (fp))) : (png_fixed_error(png_ptr, s),0))
#endif
/* else the corresponding function is defined below, inside the scope of the
 * cplusplus test.
 */
#endif

/* Gamma values (new at libpng-1.5.4): */
#define PNG_GAMMA_MAC_OLD 151724  /* Assume '1.8' is really 2.2/1.45! */
#define PNG_GAMMA_MAC_INVERSE 65909
#define PNG_GAMMA_sRGB_INVERSE 45455

/* Almost everything below is C specific; the #defines above can be used in
 * non-C code (so long as it is C-preprocessed) the rest of this stuff cannot.
 */
#ifndef PNG_VERSION_INFO_ONLY

#include "pngstruct.h"
#include "pnginfo.h"

/* Validate the include paths - the include path used to generate pnglibconf.h
 * must match that used in the build, or we must be using pnglibconf.h.prebuilt:
 */
#if PNG_ZLIB_VERNUM != 0 && PNG_ZLIB_VERNUM != ZLIB_VERNUM
#  error ZLIB_VERNUM != PNG_ZLIB_VERNUM \
      "-I (include path) error: see the notes in pngpriv.h"
   /* This means that when pnglibconf.h was built the copy of zlib.h that it
    * used is not the same as the one being used here.  Because the build of
    * libpng makes decisions to use inflateInit2 and inflateReset2 based on the
    * zlib version number and because this affects handling of certain broken
    * PNG files the -I directives must match.
    *
    * The most likely explanation is that you passed a -I in CFLAGS. This will
    * not work; all the preprocessor directories and in particular all the -I
    * directives must be in CPPFLAGS.
    */
#endif

/* This is used for 16 bit gamma tables -- only the top level pointers are
 * const; this could be changed:
 */
typedef const png_uint_16p * png_const_uint_16pp;

/* Added to libpng-1.5.7: sRGB conversion tables */
#if defined(PNG_SIMPLIFIED_READ_SUPPORTED) ||\
   defined(PNG_SIMPLIFIED_WRITE_SUPPORTED)
#ifdef PNG_SIMPLIFIED_READ_SUPPORTED
PNG_INTERNAL_DATA(const png_uint_16, png_sRGB_table, [256]);
   /* Convert from an sRGB encoded value 0..255 to a 16-bit linear value,
    * 0..65535.  This table gives the closest 16-bit answers (no errors).
    */
#endif

PNG_INTERNAL_DATA(const png_uint_16, png_sRGB_base, [512]);
PNG_INTERNAL_DATA(const png_byte, png_sRGB_delta, [512]);
#endif /* PNG_SIMPLIFIED_READ/WRITE */


/* Inhibit C++ name-mangling for libpng functions but not for system calls. */
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Internal functions; these are not exported from a DLL however because they
 * are used within several of the C source files they have to be C extern.
 *
 * All of these functions must be declared with PNG_INTERNAL_FUNCTION.
 */
/* Affirm handling */
PNG_INTERNAL_FUNCTION(void, png_affirm,(png_const_structrp png_ptr,
    param_deb(png_const_charp condition) unsigned int position), PNG_NORETURN);

#if !PNG_RELEASE_BUILD
PNG_INTERNAL_FUNCTION(void, png_handled_affirm,(png_const_structrp png_ptr,
      png_const_charp message, unsigned int position), PNG_EMPTY);
   /* This is not marked PNG_NORETURN because in PNG_RELEASE_BUILD it will
    * disappear and control will pass through it.
    */
#endif /* !RELEASE_BUILD */

/* Character/byte range checking. */
/* GCC complains about assignments of an (int) expression to a (char) even when
 * it can readily determine that the value is in range.  This makes arithmetic
 * on (char) or (png_byte) values tedious.  The warning is not issued by
 * default, but libpng coding rules require no warnings leading to excessive,
 * ridiculous and dangerous expressions of the form:
 *
 *     <char> = (char)(expression & 0xff)
 *
 * They are dangerous because they hide the warning, which might actually be
 * valid, and therefore merely enable introduction of undetected overflows when
 * code is modified.
 *
 * The following macros exist to reliably detect any overflow in non-release
 * builds.  The theory here is that we really want to know about overflows, not
 * merely hide a basically flawed compiler warning by throwing unnecessary casts
 * into the code.  The warnings disappear in RC builds so that the released
 * (STABLE) version just assigns the value (with, possibly, a warning if someone
 * turns on the -Wconversion GCC warning.)
 *
 * Doing it this way ensures that the code meets two very important aims:
 *
 * 1) Overflows are detected in pre-release tests; previously versions of libpng
 *    have been released that really did have overflows in the RGB calculations.
 * 2) In release builds GCC specific operations, which may reduce the ability
 *    of other compilers and even GCC to optimize the code, are avoided.
 *
 * There is one important extra consequence for pre-release code; it is
 * performing a lot of checks in pixel arithmetic that the release code won't
 * perform.  As a consequence a build time option, RANGE_CHECK, is provided
 * to allow the checks to be turned off in pre-release when building for
 * performance testing.  This is a standard "_SUPPORTED" option except that it
 * cannot be set in the system configuration (pnglibconf.h, pnglibconf.dfa).
 *
 * A separate macro PNG_BYTE() is provided to safely convert an unsigned value
 * to the PNG byte range 0..255.  This handles the fact that, technically,
 * an ANSI-C (unsigned char), hence a (png_byte), may be able to store values
 * outside this range.  Note that if you are building on a system where this is
 * true libpng is almost certainly going to produce errors; it has never been
 * tested on such a system.  For the moment pngconf.h ensures that this will
 * not happen.
 *
 * PNG_UINT_16 does the same thing for a 16-bit value passed in an (int) or
 * (png_uint_32) (where checking is not expected.)
 */
#if !PNG_RELEASE_BUILD
#  ifndef PNG_NO_RANGE_CHECK /* Turn off even in pre-release */
#     define PNG_RANGE_CHECK_SUPPORTED
#  endif
#endif

#ifdef PNG_RANGE_CHECK_SUPPORTED
PNG_INTERNAL_FUNCTION(unsigned int, png_bit_affirm,(png_const_structrp png_ptr,
      unsigned int position, unsigned int u, unsigned int bits), PNG_EMPTY);

PNG_INTERNAL_FUNCTION(char, png_char_affirm,(png_const_structrp png_ptr,
      unsigned int position, int c), PNG_EMPTY);

PNG_INTERNAL_FUNCTION(png_byte, png_byte_affirm,(png_const_structrp png_ptr,
      unsigned int position, int b), PNG_EMPTY);

#if INT_MAX >= 65535
PNG_INTERNAL_FUNCTION(png_uint_16, png_u16_affirm,(png_const_structrp png_ptr,
      unsigned int position, int u), PNG_EMPTY);
#  define png_check_u16(pp, u) (png_u16_affirm((pp), PNG_SRC_LINE, (u)))
#else
   /* (int) cannot hold a (png_uint_16) so the above function just won't
    * compile correctly, for the moment just do this:
    */
#  define png_check_u16(pp, u) (u)
#endif

#  define png_check_bits(pp, u, bits)\
   (((1U<<(bits))-1) & png_bit_affirm((pp), PNG_SRC_LINE, (u), (bits)))
#  define png_check_char(pp, c) (png_char_affirm((pp), PNG_SRC_LINE, (c)))
#  define png_check_byte(pp, b) (png_byte_affirm((pp), PNG_SRC_LINE, (b)))
#  define PNG_BYTE(b)           ((png_byte)((b) & 0xFFU))
#  define PNG_UINT_16(u)        ((png_uint_16)((u) & 0xFFFFU))
#elif !(defined PNG_REMOVE_CASTS) /* && !RANGE_CHECK */
#  define png_check_bits(pp, u, bits) (((1U<<(bits))-1U) & (u))
#  define png_check_char(pp, c) ((char)(c))
#  define png_check_byte(pp, b) ((png_byte)(b))
#  define png_check_u16(pp, u)  ((png_uint_16)(u))
#  define PNG_BYTE(b)           ((png_byte)((b) & 0xFFU))
#  define PNG_UINT_16(u)        ((png_uint_16)((u) & 0xFFFFU))
#else /* !RANGE_CHECK */
   /* This is somewhat trust-me-it-works: if PNG_REMOVE_CASTS is defined then
    * the casts, which might otherwise change the values, are completely
    * removed.  Use this to test your compiler to see if it makes *any*
    * difference (code size or speed.)  Currently NOT SUPPORTED.
    *
    * It also makes the PNG_BYTE and PNG_UINT_16 macros do nothing either
    * NOTE: this seems safe at present but might lead to unexpected results
    * if someone writes code to depend on the truncation.
    */
#  define png_check_bits(pp, u, bits) (u)
#  define png_check_char(pp, c) (c)
#  define png_check_byte(pp, b) (b)
#  define png_check_u16(pp, u)  (u)
#  define PNG_BYTE(b)           (b)
#  define PNG_UINT_16(u)        (u)
#endif /* RANGE_CHECK */

/* Safe calculation of a rowbytes value; does a png_error if the system limits
 * are exceeded.
 */
PNG_INTERNAL_FUNCTION(png_alloc_size_t,png_calc_rowbytes,
   (png_const_structrp png_ptr, unsigned int pixel_depth,
    png_uint_32 row_width),PNG_EMPTY);

/* Common code to calculate the maximum number of pixels to transform or filter
 * at one time; controlled by PNG_ROW_BUFFER_SIZE above:
 */
PNG_INTERNAL_FUNCTION(unsigned int,png_max_pixel_block,
      (png_const_structrp png_ptr),PNG_EMPTY);

/* Copy the row in row_buffer; this is the non-interlaced copy used in both the
 * read and write code.  'x_in_dest' specifies whether the 'x' applies to
 * the destination (sp->dp[x], x_in_dest tru) or the source (sp[x]->dp,
 * x_in_dest false).
 */
PNG_INTERNAL_FUNCTION(void, png_copy_row,(png_const_structrp png_ptr,
   png_bytep dp, png_const_bytep sp, png_uint_32 x/*in INPUT*/,
   png_uint_32 width/*of INPUT*/, unsigned int pixel_depth,
   int clear/*clear the final byte*/, int x_in_dest),PNG_EMPTY);

/* Zlib support */
#define PNG_UNEXPECTED_ZLIB_RETURN (-7)
PNG_INTERNAL_FUNCTION(void, png_zstream_error,(z_stream *zstream, int ret),
   PNG_EMPTY);
   /* Used by the zlib handling functions to ensure that z_stream::msg is always
    * set before they return.
    */

#if defined(PNG_FLOATING_POINT_SUPPORTED) && \
   !defined(PNG_FIXED_POINT_MACRO_SUPPORTED) && \
   (defined(PNG_gAMA_SUPPORTED) || defined(PNG_cHRM_SUPPORTED) || \
   defined(PNG_sCAL_SUPPORTED) || defined(PNG_READ_BACKGROUND_SUPPORTED) || \
   defined(PNG_READ_RGB_TO_GRAY_SUPPORTED)) || \
   (defined(PNG_sCAL_SUPPORTED) && \
   defined(PNG_FLOATING_ARITHMETIC_SUPPORTED))
PNG_INTERNAL_FUNCTION(png_fixed_point,png_fixed,(png_const_structrp png_ptr,
   double fp, png_const_charp text),PNG_EMPTY);
#endif

/* Internal base allocator - no messages, NULL on failure to allocate.  This
 * does, however, call the application provided allocator and that could call
 * png_error (although that would be a bug in the application implementation.)
 */
PNG_INTERNAL_FUNCTION(png_voidp,png_malloc_base,(png_const_structrp png_ptr,
   png_alloc_size_t size),PNG_ALLOCATED);

#if defined(PNG_TEXT_SUPPORTED) || defined(PNG_sPLT_SUPPORTED) ||\
   defined(PNG_STORE_UNKNOWN_CHUNKS_SUPPORTED)
/* Internal array allocator, outputs no error or warning messages on failure,
 * just returns NULL.
 */
PNG_INTERNAL_FUNCTION(png_voidp,png_malloc_array,(png_const_structrp png_ptr,
   int nelements, size_t element_size),PNG_ALLOCATED);

/* The same but an existing array is extended by add_elements.  This function
 * also memsets the new elements to 0 and copies the old elements.  The old
 * array is not freed or altered.
 */
PNG_INTERNAL_FUNCTION(png_voidp,png_realloc_array,(png_structrp png_ptr,
   png_const_voidp array, int old_elements, int add_elements,
   size_t element_size),PNG_ALLOCATED);
#endif /* text, sPLT or unknown chunks */

/* Magic to create a struct when there is no struct to call the user supplied
 * memory allocators.  Because error handling has not been set up the memory
 * handlers can't safely call png_error, but this is an obscure and undocumented
 * restriction so libpng has to assume that the 'free' handler, at least, might
 * call png_error.
 */
PNG_INTERNAL_FUNCTION(png_structp,png_create_png_struct,
   (png_const_charp user_png_ver, png_voidp error_ptr, png_error_ptr error_fn,
    png_error_ptr warn_fn, png_voidp mem_ptr, png_malloc_ptr malloc_fn,
    png_free_ptr free_fn),PNG_ALLOCATED);

/* Free memory from internal libpng struct */
PNG_INTERNAL_FUNCTION(void,png_destroy_png_struct,(png_structrp png_ptr),
   PNG_EMPTY);

/* Free an allocated jmp_buf (always succeeds) */
PNG_INTERNAL_FUNCTION(void,png_free_jmpbuf,(png_structrp png_ptr),PNG_EMPTY);

/* Function to allocate memory for zlib.  PNGAPI is disallowed. */
PNG_INTERNAL_FUNCTION(voidpf,png_zalloc,(voidpf png_ptr, uInt items, uInt size),
   PNG_ALLOCATED);

/* Function to free memory for zlib.  PNGAPI is disallowed. */
PNG_INTERNAL_FUNCTION(void,png_zfree,(voidpf png_ptr, voidpf ptr),PNG_EMPTY);

/* The next three functions are used by png_init_io to set the default
 * implementations for reading or writing to a stdio (png_FILE_p) stream.
 * They can't be static because in 1.7 png_init_io needs to reference them.
 */
#ifdef PNG_STDIO_SUPPORTED
#  ifdef PNG_READ_SUPPORTED
PNG_INTERNAL_FUNCTION(void PNGCBAPI,png_default_read_data,(png_structp png_ptr,
    png_bytep data, png_size_t length),PNG_EMPTY);
#  endif /* READ */

#  ifdef PNG_WRITE_SUPPORTED
PNG_INTERNAL_FUNCTION(void PNGCBAPI,png_default_write_data,(png_structp png_ptr,
    png_bytep data, png_size_t length),PNG_EMPTY);

#     ifdef PNG_WRITE_FLUSH_SUPPORTED
PNG_INTERNAL_FUNCTION(void PNGCBAPI,png_default_flush,(png_structp png_ptr),
   PNG_EMPTY);
#     endif /* WRITE_FLUSH */
#  endif /* WRITE */
#endif /* STDIO */

/* Reset the CRC variable.  The CRC is initialized with the chunk tag (4 bytes).
 * NOTE: at present png_struct::chunk_name MUST be set before this as well so
 * that png_struct::current_crc is initialized correctly!
 */
PNG_INTERNAL_FUNCTION(void,png_reset_crc,(png_structrp png_ptr,
         png_const_bytep chunk_tag), PNG_EMPTY);

/* Write the "data" buffer to whatever output you are using */
PNG_INTERNAL_FUNCTION(void,png_write_data,(png_structrp png_ptr,
    png_const_voidp data, png_size_t length),PNG_EMPTY);

/* Read and check the PNG file signature */
PNG_INTERNAL_FUNCTION(void,png_read_sig,(png_structrp png_ptr,
   png_inforp info_ptr),PNG_EMPTY);

/* Read data from whatever input you are using into the "data" buffer */
PNG_INTERNAL_FUNCTION(void,png_read_data,(png_structrp png_ptr, png_voidp data,
    png_size_t length),PNG_EMPTY);

/* Read bytes into buf, and update png_ptr->crc */
PNG_INTERNAL_FUNCTION(void,png_crc_read,(png_structrp png_ptr, png_voidp buf,
    png_uint_32 length),PNG_EMPTY);

/* Read "skip" bytes, read the file crc, and (optionally) verify png_ptr->crc */
PNG_INTERNAL_FUNCTION(int,png_crc_finish,(png_structrp png_ptr,
   png_uint_32 skip),PNG_EMPTY);

/* Calculate the CRC over a section of data.  Note that we are only
 * passing a maximum of 64K on systems that have this as a memory limit,
 * since this is the maximum buffer size we can specify.
 */
PNG_INTERNAL_FUNCTION(void,png_calculate_crc,(png_structrp png_ptr,
   png_const_voidp ptr, png_size_t length),PNG_EMPTY);

/* Write various chunks */

/* Write the IHDR chunk, and update the png_struct with the necessary
 * information.
 */
PNG_INTERNAL_FUNCTION(void,png_write_IHDR,(png_structrp png_ptr,
   png_uint_32 width, png_uint_32 height, int bit_depth, int color_type,
   int compression_method, int filter_method, int interlace_method),PNG_EMPTY);

PNG_INTERNAL_FUNCTION(void,png_write_PLTE,(png_structrp png_ptr,
   png_const_colorp palette, unsigned int num_pal),PNG_EMPTY);

PNG_INTERNAL_FUNCTION(void,png_write_IEND,(png_structrp png_ptr),PNG_EMPTY);

#ifdef PNG_WRITE_gAMA_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_gAMA_fixed,(png_structrp png_ptr,
    png_fixed_point file_gamma),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_sBIT_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_sBIT,(png_structrp png_ptr,
    png_const_color_8p sbit, int color_type),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_cHRM_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_cHRM_fixed,(png_structrp png_ptr,
    const png_xy *xy), PNG_EMPTY);
   /* The xy value must have been previously validated */
#endif

#ifdef PNG_WRITE_sRGB_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_sRGB,(png_structrp png_ptr,
    int intent),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_iCCP_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_iCCP,(png_structrp png_ptr,
   png_const_charp name, png_const_voidp profile), PNG_EMPTY);
   /* The profile must have been previously validated for correctness, the
    * length comes from the first four bytes.  Only the base, deflate,
    * compression is supported.
    */
#endif

#ifdef PNG_WRITE_sPLT_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_sPLT,(png_structrp png_ptr,
    png_const_sPLT_tp palette),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_tRNS_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_tRNS,(png_structrp png_ptr,
    png_const_bytep trans, png_const_color_16p values, int number,
    int color_type),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_bKGD_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_bKGD,(png_structrp png_ptr,
    png_const_color_16p values, int color_type),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_hIST_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_hIST,(png_structrp png_ptr,
    png_const_uint_16p hist, int num_hist),PNG_EMPTY);
#endif

/* Chunks that have keywords */
#ifdef PNG_WRITE_tEXt_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_tEXt,(png_structrp png_ptr,
    png_const_charp key, png_const_charp text, png_size_t text_len),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_zTXt_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_zTXt,(png_structrp png_ptr,
    png_const_charp key, png_const_charp text, int compression),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_iTXt_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_iTXt,(png_structrp png_ptr,
    int compression, png_const_charp key, png_const_charp lang,
    png_const_charp lang_key, png_const_charp text),PNG_EMPTY);
#endif

#ifdef PNG_TEXT_SUPPORTED  /* Added at version 1.0.14 and 1.2.4 */
PNG_INTERNAL_FUNCTION(int,png_set_text_2,(png_structrp png_ptr,
    png_inforp info_ptr, png_const_textp text_ptr, int num_text),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_oFFs_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_oFFs,(png_structrp png_ptr,
    png_int_32 x_offset, png_int_32 y_offset, int unit_type),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_pCAL_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_pCAL,(png_structrp png_ptr,
    png_charp purpose, png_int_32 X0, png_int_32 X1, int type, int nparams,
    png_const_charp units, png_charpp params),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_pHYs_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_pHYs,(png_structrp png_ptr,
    png_uint_32 x_pixels_per_unit, png_uint_32 y_pixels_per_unit,
    int unit_type),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_tIME_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_tIME,(png_structrp png_ptr,
    png_const_timep mod_time),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_sCAL_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_sCAL_s,(png_structrp png_ptr,
    int unit, png_const_charp width, png_const_charp height),PNG_EMPTY);
#endif

#ifdef PNG_WRITE_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_write_start_IDAT,(png_structrp png_ptr),
      PNG_EMPTY);
   /* Do any required initialization before IDAT or row processing starts. */

/* Choose the best filter to use and filter the row data then write it out.  If
 * WRITE_FILTERING is not supported this just writes the data out with a zero
 * (NONE) filter byte.
 *
 * This may be called multiple times per row, but calls must be in 'x' order;
 * first a call with x 0 to mark the start of the row and, at the end, one with
 * PNG_ROW_END set (this can be done in the same function call if the whole row
 * is passed.)  The following flags are used internally to control pass
 * filtering and deflate:
 */
enum
{
   png_pass_last      =0x1U, /* This is the last pass in the image */
   png_pass_last_row  =0x2U, /* This is the last row in a pass */
   png_pass_first_row =0x4U, /* This is the first row in a pass */
   png_row_end        =0x8U, /* This is the last block in the row */
   png_no_row_info    =0x0U  /* Placeholder */

   /* A useful macro; return true if this is the last block of the last row in
    * the image.
    */
#  define PNG_IDAT_END(f) (((f) & ~png_pass_first_row) == \
      (png_row_end+png_pass_last_row+png_pass_last))
};
PNG_INTERNAL_FUNCTION(void,png_write_png_data,(png_structrp png_ptr,
    png_bytep prev_pixels, png_const_bytep unfiltered_row, png_uint_32 x,
    unsigned int width/*pixels*/, unsigned int row_info_flags),
   PNG_EMPTY);

PNG_INTERNAL_FUNCTION(void,png_write_png_rows,(png_structrp png_ptr,
    png_const_bytep *rows, png_uint_32 num_rows), PNG_EMPTY);
   /* As above but rows[num_rows] of correctly (PNG) formated but unfiltered
    * data are passed in.  For an interlaced image the rows will be interlaced
    * rows and therefore may be narrower than the image width.
    *
    * This function advances png_structp::pass and png_structp::row_number as
    * required.
    */

/* Release memory used by the deflate mechanism */
PNG_INTERNAL_FUNCTION(void, png_deflate_destroy, (png_structp png_ptr),
   PNG_EMPTY);
#endif /* WRITE */

#ifdef PNG_TRANSFORM_MECH_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_transform_free,(png_const_structrp png_ptr,
         png_transformp *list),PNG_EMPTY);
   /* Free the entire transform list, from the given point on. the argument is
    * set to NULL.
    */

PNG_INTERNAL_FUNCTION(void,png_init_transform_control,(
   png_transform_controlp out, png_structp png_ptr),PNG_EMPTY);
   /* Initialize a transform control for running the transform list forward (the
    * read case, and write initialization, but the write case is called within
    * pngtrans.c by the above function.)
    */

#ifdef PNG_READ_TRANSFORMS_SUPPORTED
PNG_INTERNAL_FUNCTION(unsigned int,png_run_this_transform_list_forwards,
   (png_transform_controlp tc, png_transformp *start, png_transformp end),
   PNG_EMPTY);
   /* Used by the transform cache code to run a sub-list, from *start to the
    * transform end.
    */
#endif /* READ_TRANSFORMS */

#ifdef PNG_READ_SUPPORTED
PNG_INTERNAL_FUNCTION(unsigned int,png_run_transform_list_forwards,
   (png_structp png_ptr, png_transform_controlp tc),PNG_EMPTY);
   /* Run the transform list in the forwards direction (from PNG format to
    * memory format).  The transform control must be initialized completely by
    * the caller.  This function takes account of transforms which delete
    * themselves during the run; it must be used.
    */
#endif /* READ */

#ifdef PNG_WRITE_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_run_transform_list_backwards,
   (png_structp png_ptr, png_transform_controlp tc),PNG_EMPTY);
   /* Run the transform list in the backwards direction (from memory format to
    * PNG format).  The transform control must be initialized completely by
    * the caller.  This function takes account of transforms which delete
    * themselves during the run; it must be used.
    */
#endif /* WRITE */

PNG_INTERNAL_FUNCTION(png_transformp,png_add_transform,(png_structrp png_ptr,
   size_t size, png_transform_fn fn, unsigned int order),PNG_EMPTY);
   /* Add a transform, using the information in 'order' to control the position
    * of the transform in the list, returning a pointer to the transform.  The
    * top 8 bits of 'order' control the position in the list.  If a transform
    * does not already exist in the list with the given value a new transform
    * will be created and 'fn' and 'order' set.  If there is a transform with
    * that value 'fn' must match and 'order' will be updated by combining the
    * new value in with a bitwise or (|).  It is up to the function (fn) or the
    * caller of png_add_transform to determine whether the combination is valid.
    *
    * 'size' is used when creating a new transform, it may be larger than
    * (sizeof png_transform) if required to accomodate extra data.
    *
    * Prior to 1.7.0 transforms were executed in an order hard-wired into the
    * code that executed the transform functions.  This was summarized in the
    * read case by the following comment from pngrtran.c
    * (png_init_read_transformations), note that this has been marked up to
    * indicate which PNG formats the transforms in the list apply to:
    *
    * *: applies to most formats
    * A: only formats with alpha
    * L: only low-bit-depth (less than 8 bits per component/pixel)
    * H: only high-bit-depth (16-bits per component)
    *
    > From the code of png_do_read_transformations the order is:
    *
    * GGRR   For example column: . no action
    *  AGG                       r acts on read
    *   BB                       w acts on write
    *    A                       B acts on both read and write
    >
    > r.r.  1) PNG_EXPAND (including PNG_EXPAND_tRNS)
    > .r.r  2) PNG_STRIP_ALPHA (if no compose)
    > ..rr  3) PNG_RGB_TO_GRAY
    > rr..  4) PNG_GRAY_TO_RGB iff !PNG_FLAG_BACKGROUND_IS_GRAY
    > rrrr  5) PNG_COMPOSE
    > rrrr  6) PNG_GAMMA
    > .r.r  7) PNG_STRIP_ALPHA (if compose)
    > .r.r  8) PNG_ENCODE_ALPHA
    > rrrr  9) PNG_SCALE_16_TO_8
    > rrrr 10) PNG_16_TO_8
    > ..rr 11) PNG_QUANTIZE (converts to palette)
    > rrrr 12) PNG_EXPAND_16
    > rr.. 13) PNG_GRAY_TO_RGB iff PNG_FLAG_BACKGROUND_IS_GRAY
    > BB.. 14) PNG_INVERT_MONO
    > .B.B 15) PNG_INVERT_ALPHA
    > BBBB 16) PNG_SHIFT
    *
    * Note that transforms from this point on are used in 1.7.0 on palette
    * indices as well; a png_set_pack request (for example) packs the palette
    * index values if the output will be palettized and the grayscale values
    * if it will not be (if the output is low-bit-grayscale, not palette.)
    *
    > B... 17) PNG_PACK
    > ..BB 18) PNG_BGR
    > B... 19) PNG_PACKSWAP
    > rwrw 20) PNG_FILLER (includes PNG_ADD_ALPHA)
    > .B.B 21) PNG_SWAP_ALPHA
    > BBBB 22) PNG_SWAP_BYTES
    > BBBB 23) PNG_USER_TRANSFORM [must be last]
    *
    * Finally, outside the set of transforms prior to 1.7.0, the libpng
    * interlace handling required the pixels to be replicated to match the pixel
    * spacing in the image row; the first part the pre-1.7.0 interlace support,
    * this is still the case when reading, but for writing the interlace is now
    * a transform:
    *
    > BBBB 24) png_do_{read,write}_interlace (interlaced images only).
    *
    * First transforms are grouped according to basic function using the top 3
    * bits of the order code:
    */
#  define PNG_TR_START         0x0000U /* initial ops on the PNG data */
#  define PNG_TR_ARITHMETIC    0x2000U /* arithmetic linear operations */
#  define PNG_TR_CHANNEL       0x4000U /* PNG conformant format changes */
#  define PNG_TR_QUANTIZE      0x6000U /* quantize and following operations */
#  define PNG_TR_ENCODING      0x8000U /* Row encoding transforms */
#  define PNG_TR_INTERLACE     0xA000U /* write interlace transform */
   /*
    * In libpng 1.7.0 the check on palette index values is moved to the start
    * (of read, end of write, which is where it was before) immediately after
    * the MNG filter handling
    */
#  define PNG_TR_MNG_INTRAPIXEL (PNG_TR_START + 0x0100U)
   /* Perform intra-pixel differencing (write) or un-differencing on read. */
#  define PNG_TR_CHECK_PALETTE  (PNG_TR_START + 0x0200U)
   /* Done before at the start on read, at the end on write to give a
    * consistent postion:
    *
    *    PNG_RWTR_CHECK_PALETTE PI W11: happens in pngwrite.c last
    */
#  define PNG_TR_START_CACHE    (PNG_TR_START + 0x0300U)
   /* Not used on a transform; this is just a marker for the point at which
    * palette or low-bit-depth caching can start on read.  (The previous
    * operations cannot be cached).
    */
#  define PNG_TR_INIT_ALPHA     (PNG_TR_START + 0x0400U)
   /* This just handles alpha/tRNS initialization issues to resolve the
    * inter-dependencies with tRNS expansion and background composition; it
    * doesn't do anything itself, just sets flags and pushes transforms.
    */
   /*
    * Prior to 1.7 the arithmetic operations interleaved with the RGB-to-gray
    * and alpha strip byte level ops.  This was done to reduce the amount of
    * data processed, i.e. it was an optimization not a requirement.  These
    * operations were preceded by the 'expand' operations, which is the
    * opposite; it was done to simplify the code and actually slows things down
    * in the low bit depth gray case.  The full list of operations after expand,
    * in the 1.6 order, is:
    *
    *    PNG_TR_STRIP_ALPHA     png_do_strip_channel (sometimes)
    *    PNG_TR_RGB_TO_GRAY     png_do_rgb_to_gray
    *    PNG_TR_GRAY_TO_RGB     png_do_gray_to_rgb (sometimes)
    *    PNG_TR_COMPOSE         png_do_compose
    *    PNG_TR_GAMMA           png_do_gamma (if no RGB_TO_GRAY)
    *    PNG_TR_STRIP_ALPHA     png_do_strip_channel (other times)
    *    PNG_TR_ENCODE_ALPHA    png_do_encode_alpha
    *
    * In 1.7 the operations are moved round somewhat, including moving alpha and
    * 16-to-8 bit reduction later.  This leaves the following operations:
    *
    *    PNG_TR_RGB_TO_GRAY     png_do_rgb_to_gray
    *    PNG_TR_COMPOSE         png_do_compose
    *    PNG_TR_GAMMA           png_do_gamma (if no RGB_TO_GRAY)
    *    PNG_TR_ENCODE_ALPHA    png_do_encode_alpha
    *
    * Prior to 1.7 some combinations of transforms would do gamma correction
    * twice, the actual implementation in 1.7 is to use the following order and
    * rely on the cache code to optimize gray 1,2,4,8 and (of course) palette.
    */
#  define PNG_TR_COMPOSE          (PNG_TR_ARITHMETIC + 0x0100U)
      /* Handle background composition.  This may need to push a gray-to-rgb
       * transform if the background is RGB for gray input.  This precedes RGB
       * to gray convertion so that it can handle tRNS appropriately when the
       * background is in the PNG encoding however, typically, the processing
       * happens at PNG_TR_COMPOSE_ALPHA below.
       *
       * NOTE: this must be the first arithmetic transform because the code in
       * png_init_background relies on png_transform_control::gamma being the
       * original PNG gamma.
       */
#  define PNG_TR_RGB_TO_GRAY      (PNG_TR_ARITHMETIC + 0x0200U) /* to gray */
      /* Convert any RGB input (8/16 bit depth, RGB, RGBA) to linear gray
       * 16-bit.  This happens first because it cannot be cached; the input data
       * has 24 or 48 bits of uncorrelated data so the transform has to happen
       * pixel-by-pixel.  Internally the transform may maintain an 8 or 16-bit
       * gamma correction table (to 16-bit linear) to speed things up.
       *
       * NOTE: this transform must follow PNG_TR_COMPOSE with no intervening
       * transforms; see the code in png_init_background (pngrtran.c) which
       * relies on this during PNG_TC_INIT_FORMAT.
       */
#  define PNG_TR_COMPOSE_ALPHA    (PNG_TR_ARITHMETIC + 0x0300U)
       /* Compose alpha composition and tRNS handling when the background is a
        * screen color.  Pushed by PNG_TR_COMPOSE as required.
        */
#  define PNG_TR_GAMMA_ENCODE     (PNG_TR_ARITHMETIC + 0x1F00U) /* last */
      /* Gamma encode the input.  This encodes the gray or RGB channels to the
       * required bit depth and either scales the alpha channel or encodes it as
       * well, depending on the requested alpha encoding.
       */
   /*
    * The 'expand' operations come after the arithmetic ones in libpng 1.7, this
    * forces the arithmetic stuff to do the expand, but since arithmetic is (in
    * 1.7) normally done in 16-bit linear this avoids spurious expands.
    */
#  define PNG_TR_EXPAND        (PNG_TR_CHANNEL    + 0x0100U)
   /* Includes:
    *
    *    PNG_TR_EXPAND_PALETTE  palette images only, includes tRNS
    *    PNG_TR_EXPAND_LBP_GRAY grayscale low-bit depth only
    *    PNG_TR_EXPAND_tRNS     non-palette images only
    */
#  define PNG_TR_SCALE_16_TO_8 (PNG_TR_CHANNEL    + 0x0200U)
   /* Comes after the expand and before the chop version; note that it works on
    * the pixel values directly, so it is a linear transform on a non-linear
    * value.
    */
   /*
    * To handle transforms that affect the palette entries, not the palette
    * indices in the row data, libpng 1.7 reorders some of the post-quantize
    * transformations to put all the "PC" transforms ahead of all the "PI"
    * transforms.  The "PC" transforms that came after png_do_quantize in libpng
    * 1.6 cannot be ordered to be before so they are included in the
    * PNG_TR_QUANTIZE section.  The PI transforms are all in PNG_TR_ENCODING,
    * PNG_GRAY_TO_RGB is moved before PNG_TR_QUANTIZE to avoid the unpredictable
    * behavior of png_set_quantize that otherwise arises.
    *
    * The transforms in the PNG_TR_QUANTIZE section are:
    *
    *    PNG_TR_EXPAND_16       !P !W
    *    PNG_RWTR_INVERT_MONO   !P W10: invert the gray channel
    *    PNG_RWTR_INVERT_ALPHA  PC W8: invert the alpha channel
    *    PNG_RWTR_SHIFT         PC W6: read: down, write: scale up
    *    PNG_RWTR_BGR           !P W9
    *    PNG_RWTR_FILLER        !P W2: add on R, remove on W
    *    PNG_RWTR_SWAP_ALPHA    !P W7
    *    PNG_RWTR_SWAP_16       !P W5
    *
    * The ones in PNG_TR_ENCODING are:
    *
    *    PNG_RWTR_PACK          PI W4: R: unpack bytes, W: pack
    *    PNG_RWTR_PIXEL_SWAP    PI W3: Swap pixels in a byte
    *    PNG_RWTR_USER          PI W1
    */

#  define PNG_TR_CHANNEL_PREQ  (PNG_TR_CHANNEL    + 0x1F00U)
   /* The channel swap transforms that must happen before PNG_TR_QUANTIZE:
    *
    *    PNG_TR_STRIP_ALPHA
    *    PNG_TR_CHOP_16_TO_8
    *    PNG_TR_GRAY_TO_RGB
    */
#  define PNG_TR_CHANNEL_POSTQ (PNG_TR_QUANTIZE   + 0x0100U)
   /* The post-quantize channel swap transforms:
    *
    *    PNG_TR_EXPAND_16       !P !W
    *    PNG_RWTR_BGR           !P W9
    *    PNG_RWTR_FILLER        !P W2: (filler) add on R, remove on W
    *    PNG_RWTR_SWAP_ALPHA    !P W7
    *    PNG_RWTR_SWAP_16       !P W5
    *
    * The 'CHANNEL' operation sets the transform_control channel_add flag for
    * use below.
    */
#  define PNG_TR_INVERT        (PNG_TR_QUANTIZE   + 0x0200U)
   /* Invert MONO and ALPHA.  If the channel_add flag is set in the transform
    * control INVERT_ALPHA will not be done; the png_add_alpha/filler APIs
    * happened after png_set_invert_alpha in earlier versions so the filler
    * value had to include the invert.
    *
    *    PNG_RWTR_INVERT_MONO   !P W10: invert the gray channel
    *    PNG_RWTR_INVERT_ALPHA  PC W8: invert the alpha channel
    */
#  define PNG_TR_SHIFT         (PNG_TR_QUANTIZE   + 0x0300U)
   /* The channel shift, except that if the channel_add flag has been set the
    * alpha channel is not shifted.
    *
    *    PNG_RWTR_SHIFT         PC W6: read: down, write: scale up
    */
#  define PNG_TR_PACK          (PNG_TR_ENCODING   + 0x0200U)
   /*    PNG_RWTR_PACK          PI W4: R: unpack bytes, W: pack */
#  define PNG_TR_PIXEL_SWAP    (PNG_TR_ENCODING   + 0x0300U)
   /*    PNG_RWTR_PIXEL_SWAP    PI W3: Swap pixels in a byte */
#  define PNG_TR_USER          (PNG_TR_ENCODING   + 0x1F00U)
   /* The user transform; must be last before the interlace handling because it
    * does unpredictable things to the format.
    *
    *    PNG_RWTR_USER          PI W1
    */

PNG_INTERNAL_FUNCTION(png_transformp,png_push_transform,(png_structrp png_ptr,
   size_t size, png_transform_fn fn, png_transformp *transform,
   png_transform_controlp tc),PNG_EMPTY);
   /* As png_add_transform except that the new transform is inserted ahead of
    * the given transform (*transform).  The new transform is returned, but it
    * will also invariably be in *transform.  If 'tc' is not NULL the transform
    * callback will also be called; it needs to be called if this function is
    * called while transforms are being run.
    *
    * 'fn' must not be NULL.
    *
    * The transform is inserted with the same 'order' as the passed in
    * *transform, that transform and following transforms are moved up ('order'
    * is incremented) as required to make space.  Consequently, unlike with
    * png_add_transform, the transform will always be new.  To detect loops
    * (*transform)->fn must not be the same as the passed in 'fn'.
    */

PNG_INTERNAL_FUNCTION(png_voidp,png_transform_cast_check,
   (png_const_structp png_ptr, unsigned int src_line, png_transformp tr,
    size_t size),PNG_EMPTY);
   /* Given a pointer to a transform, 'tr' validate that the underlying derived
    * class has size 'size' using the tr->size field and return the same
    * pointer.  If there is a size mismatch the function does an affirm using
    * the given line number.
    */
#define png_transform_cast(type, pointer) png_voidcast(type*,\
   png_transform_cast_check(png_ptr, PNG_SRC_LINE, (pointer), sizeof (type)))
   /* This takes a pointer to a transform and safely returns a pointer to a
    * derived transform class (type); type must not have the pointer.  It
    * validates the 'size' field.  Derived classes start with a png_transform
    * as the first member called 'tr'.
    */
#endif /* TRANSFORM_MECH_SUPPORTED */

#ifdef PNG_READ_TRANSFORMS_SUPPORTED
/* Remove a transform from a list, moving the next transform down into
 * *transform.
 */
PNG_INTERNAL_FUNCTION(void,png_remove_transform,(png_const_structp png_ptr,
   png_transformp *transform),PNG_EMPTY);

/* Initializer for read transforms that handles caching, palette update and
 * palette expansion.
 */
PNG_INTERNAL_FUNCTION(unsigned int,png_read_init_transform_mech,
   (png_structp png_ptr, png_transform_control *tc),PNG_EMPTY);

/* Optional call to update the users info structure */
PNG_INTERNAL_FUNCTION(void,png_read_transform_info,(png_structrp png_ptr,
   png_inforp info_ptr),PNG_EMPTY);
#endif

/* APIs which do a tranform on both read and write but where the implementation
 * is separate for each; the read and write init functions are in pngrtran.c or
 * pngwtran.c, the API is in pngtrans.c
 */
#if defined(PNG_READ_PACK_SUPPORTED) || defined(PNG_READ_EXPAND_SUPPORTED)
PNG_INTERNAL_FUNCTION(void,png_init_read_pack,(png_transformp *transform,
   png_transform_controlp tc),PNG_EMPTY);
#endif /* READ_PACK || READ_EXPAND */
#ifdef PNG_WRITE_PACK_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_init_write_pack,(png_transformp *transform,
   png_transform_controlp tc),PNG_EMPTY);
#endif /* WRITE_PACK */

/* Shared transform functions, defined in pngtran.c */
#if defined(PNG_WRITE_FILLER_SUPPORTED) || \
    defined(PNG_READ_STRIP_ALPHA_SUPPORTED)
PNG_INTERNAL_FUNCTION(void,png_do_strip_channel,(
    png_transform_controlp row_info, png_bytep row, int at_start),PNG_EMPTY);
#endif /* FILLER */

#if defined(PNG_READ_INVERT_SUPPORTED) || defined(PNG_WRITE_INVERT_SUPPORTED)
PNG_INTERNAL_FUNCTION(void,png_do_invert,(png_transform_controlp row_info,
    png_bytep row),PNG_EMPTY);
#endif /* INVERT */

#if defined(PNG_READ_INVERT_ALPHA_SUPPORTED) ||\
    defined(PNG_WRITE_INVERT_ALPHA_SUPPORTED)
PNG_INTERNAL_FUNCTION(void,png_do_invert_alpha,(png_transform_controlp row_info,
    png_bytep row),PNG_EMPTY);
#endif /* INVERT_ALPHA */

#ifdef PNG_READ_RGB_TO_GRAY_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_add_rgb_to_gray_byte_ops,(png_structrp png_ptr,
   png_transform_controlp tc, unsigned int index, unsigned int order),
   PNG_EMPTY);
   /* This is an init-time utility to add appropriate byte ops to select a given
    * channel from R/G/B.
    */
#endif /* READ_RGB_TO_GRAY */

#if defined(PNG_READ_GRAY_TO_RGB_SUPPORTED) &&\
    defined(PNG_READ_BACKGROUND_SUPPORTED)
PNG_INTERNAL_FUNCTION(void,png_push_gray_to_rgb_byte_ops,(png_transformp *tr,
   png_transform_controlp tc), PNG_EMPTY);
   /* This is an init-time utility to push appropriate byte ops to expand a
    * grayscale PNG data set to RGB.  It calls the function callback so 'tc'
    * must be non-NULL.
    */
#endif /* GRAY_TO_RGB && READ_BACKGROUND */

#ifdef PNG_READ_STRIP_ALPHA_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_add_strip_alpha_byte_ops,(png_structrp png_ptr),
   PNG_EMPTY);
   /* Called from pngrtran.c to add the relevant byte op. */
#endif /* READ_STRIP_ALPHA */

/* The following decodes the appropriate chunks, and does error correction,
 * then calls the appropriate callback for the chunk if it is valid.
 */

#ifdef PNG_READ_SUPPORTED
PNG_INTERNAL_FUNCTION(png_bytep,png_read_buffer,(png_structrp png_ptr,
   png_alloc_size_t new_size, int warn),PNG_EMPTY);
   /* Manage the dynamically allocated read buffer */

/* Shared READ IDAT handling: */
PNG_INTERNAL_FUNCTION(void,png_read_start_IDAT,(png_structrp png_ptr),
   PNG_EMPTY);
   /* Initialize the row buffers, etc. */

typedef enum
{
   png_row_incomplete,
      /* more IDAT data needed for row */
   png_row_process,
      /* png_struct::row_buffer contains a complete row */
   png_row_repeat,
      /* row not in this pass, but the existing row may be used */
   png_row_skip
      /* row not in pass and no appropriate data; skip this row */
}  png_row_op;
PNG_INTERNAL_FUNCTION(png_row_op,png_read_process_IDAT,(png_structrp png_ptr,
      png_bytep transformed_row, png_bytep display_row, int save_row),
      PNG_EMPTY);
   /* Process a block of IDAT data; the routine returns early if it has
    * obtained a row.  It is valid to call this routine with no input data;
    * it will return png_row_incomplete if it needs input.
    *
    * transformed_row: The transformed pixels of the input are written here.
    *                  For interlaced images only the pixels in the pass will
    *                  be written, the other pixels will not be touched.
    *
    * display_row:     The transformed pixels but replicated to that the entire
    *                  buffer will have been initialized.  For passes after the
    *                  first the pixels written are determined by the 'block'
    *                  algorithm; only those *following* pixels which are
    *                  written by *later* passes are written (with a copy of the
    *                  pixel from the pass.)
    *
    * save_row:        A boolean which indicates that the row (unexpanded)
    *                  should be saved in png_struct::transformed_row.  This can
    *                  be used in a later call to png_combine_row.
    *
    * During reading the row is built up until png_row_process is returned.  At
    * this point png_struct::row_buffer contains the original PNG row from the
    * file and, if save_row was set, png_struct::transformed_row contains the
    * row after the selected row transforms have been performed.  For interlaced
    * images both are the width of the interlace pass.
    *
    * When png_row_repeat is returned the same is true, except that the buffers
    * still contain the contents of the preceding row (the one where this
    * funciton returned png_row_pricess).
    *
    * The row buffers should not be accessed if png_row_skip is returned; this
    * row is not modified in the current pass.
    */

PNG_INTERNAL_FUNCTION(void,png_read_free_row_buffers,(png_structrp png_ptr),
    PNG_EMPTY);
   /* Free allocated row buffers; done as soon as possible to avoid carrying
    * around all the memory for longer than necessary.
    */

PNG_INTERNAL_FUNCTION(int,png_read_finish_IDAT,(png_structrp png_ptr),
    PNG_EMPTY);
   /* Complete reading of the IDAT chunks.  This returns 0 if more data is to
    * be read, 1 if the zlib stream has terminated.  Call this routine with
    * zstream.avail_in greater than zero unless there is no more input data.
    * When zstream_avail_in is 0 on entry and the stream does not terminate
    * an "IDAT truncated" error will be output.
    *
    *    ENTRY: png_ptr->zstream.{next,avail}_in points to more IDAT data, if
    *           available, otherwise avail_in should be 0.
    *    RET 0: the LZ stream is still active, more IDAT date is required, if
    *           available, the routine *must* be called again.
    *    RET 1: the LZ stream has been closed and an error may have been output;
    *           png_ptr->zstream_error says whether it has.  If not and there
    *           is more IDAT data available the caller should output an
    *           appropriate (too much IDAT) error message.
    */

#ifdef PNG_HANDLE_AS_UNKNOWN_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_cache_known_unknown,(png_structrp png_ptr,
   png_const_bytep add, int keep),PNG_EMPTY);
   /* Update the png_struct::known_unknown bit cache which stores whether each
    * known chunk should be treated as unknown.
    */
#endif

typedef enum
{
   png_chunk_skip = 0,    /* Skip this chunk */
   png_chunk_unknown,     /* Pass the chunk to png_handle_unknown */
   png_chunk_process_all, /* Process the chunk all at once */
   png_chunk_process_part /* Process the chunk in parts (for IDAT) */
}  png_chunk_op;

PNG_INTERNAL_FUNCTION(png_chunk_op,png_find_chunk_op,(png_structrp png_ptr),
      PNG_EMPTY);
   /* Given a chunk in png_struct::{chunk_name,chunk_length} validate the name
    * and work out how it should be handled.  This function checks the chunk
    * location using png_struct::mode and will set the mode appropriately for
    * the known critical chunks but otherwise makes no changes to the stream
    * read state.
    */

PNG_INTERNAL_FUNCTION(void,png_check_chunk_name,(png_const_structrp png_ptr,
    const png_uint_32 chunk_name),PNG_EMPTY);

PNG_INTERNAL_FUNCTION(void,png_check_chunk_length,(png_const_structrp png_ptr,
    const png_uint_32 chunk_length),PNG_EMPTY);

#ifdef PNG_READ_UNKNOWN_CHUNKS_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_handle_unknown,(png_structrp png_ptr,
   png_inforp info_ptr, png_bytep chunk_data),PNG_EMPTY);
   /* Handle an unknown chunk that needs to be processed.  It is only valid
    * to call this after png_find_chunk_op returns png_chunk_unknown.  The
    * data argument points to the png_struct::chunk_length bytes of the chunk
    * data.
    */
#endif /* READ_UNKNOWN_CHUNKS */

PNG_INTERNAL_FUNCTION(void,png_handle_chunk,(png_structrp png_ptr,
      png_inforp info_ptr),PNG_EMPTY);
   /* The chunk to handle is in png_struct::chunk_name,chunk_length.
    *
    * NOTE: at present it is only valid to call this after png_find_chunk_op
    * has returned png_chunk_process_all and all the data is available for
    * png_handle_chunk (via the libpng read callback.)
    */
#endif /* READ */

PNG_INTERNAL_FUNCTION(void,png_init_row_info,(png_structrp png_ptr),PNG_EMPTY);
   /* Set the png_struct::row_ members from the PNG file information, running
    * transforms if required.
    */

/* Added at libpng version 1.6.0 */
#ifdef PNG_GAMMA_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_colorspace_set_gamma,(png_const_structrp png_ptr,
    png_colorspacerp colorspace, png_fixed_point gAMA), PNG_EMPTY);
   /* Set the colorspace gamma with a value provided by the application or by
    * the gAMA chunk on read.  The value will override anything set by an ICC
    * profile.
    */

PNG_INTERNAL_FUNCTION(void,png_colorspace_sync_info,(png_const_structrp png_ptr,
    png_inforp info_ptr), PNG_EMPTY);
   /* Synchronize the info 'valid' flags with the colorspace */

PNG_INTERNAL_FUNCTION(void,png_colorspace_sync,(png_const_structrp png_ptr,
    png_inforp info_ptr), PNG_EMPTY);
   /* Copy the png_struct colorspace to the info_struct and call the above to
    * synchronize the flags.  Checks for NULL info_ptr and does nothing.
    */
#endif

/* Added at libpng version 1.4.0 */
#ifdef PNG_COLORSPACE_SUPPORTED
/* These internal functions are for maintaining the colorspace structure within
 * a png_info or png_struct (or, indeed, both).
 */
PNG_INTERNAL_FUNCTION(int,png_colorspace_set_chromaticities,
   (png_const_structrp png_ptr, png_colorspacerp colorspace, const png_xy *xy,
    int preferred), PNG_EMPTY);

PNG_INTERNAL_FUNCTION(int,png_colorspace_set_endpoints,
   (png_const_structrp png_ptr, png_colorspacerp colorspace, const png_XYZ *XYZ,
    int preferred), PNG_EMPTY);

#ifdef PNG_sRGB_SUPPORTED
PNG_INTERNAL_FUNCTION(int,png_colorspace_set_sRGB,(png_const_structrp png_ptr,
   png_colorspacerp colorspace, int intent), PNG_EMPTY);
   /* This does set the colorspace gAMA and cHRM values too, but doesn't set the
    * flags to write them, if it returns false there was a problem and an error
    * message has already been output (but the colorspace may still need to be
    * synced to record the invalid flag).
    */
#endif /* sRGB */

#ifdef PNG_iCCP_SUPPORTED
PNG_INTERNAL_FUNCTION(int,png_colorspace_set_ICC,(png_const_structrp png_ptr,
   png_colorspacerp colorspace, png_const_charp name,
   png_uint_32 profile_length, png_const_bytep profile, int is_color),
   PNG_EMPTY);
   /* The 'name' is used for information only */

/* Routines for checking parts of an ICC profile. */
PNG_INTERNAL_FUNCTION(int,png_icc_check_length,(png_const_structrp png_ptr,
   png_colorspacerp colorspace, png_const_charp name,
   png_uint_32 profile_length), PNG_EMPTY);
PNG_INTERNAL_FUNCTION(int,png_icc_check_header,(png_const_structrp png_ptr,
   png_colorspacerp colorspace, png_const_charp name,
   png_uint_32 profile_length,
   png_const_bytep profile /* first 132 bytes only */, int is_color),
   PNG_EMPTY);
PNG_INTERNAL_FUNCTION(int,png_icc_check_tag_table,(png_const_structrp png_ptr,
   png_colorspacerp colorspace, png_const_charp name,
   png_uint_32 profile_length,
   png_const_bytep profile /* header plus whole tag table */), PNG_EMPTY);
#ifdef PNG_sRGB_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_icc_set_sRGB,(
   png_const_structrp png_ptr, png_colorspacerp colorspace,
   png_const_bytep profile, uLong adler), PNG_EMPTY);
   /* 'adler' is the Adler32 checksum of the uncompressed profile data. It may
    * be zero to indicate that it is not available.  It is used, if provided,
    * as a fast check on the profile when checking to see if it is sRGB.
    */
#endif /* sRGB */
#endif /* iCCP */
#endif /* COLORSPACE */

/* Added at libpng version 1.4.0 */
PNG_INTERNAL_FUNCTION(void,png_check_IHDR,(png_const_structrp png_ptr,
    png_uint_32 width, png_uint_32 height, int bit_depth,
    int color_type, int interlace_type, int compression_type,
    int filter_type),PNG_EMPTY);

#if defined(PNG_FLOATING_POINT_SUPPORTED) && defined(PNG_ERROR_TEXT_SUPPORTED)
PNG_INTERNAL_FUNCTION(void,png_fixed_error,(png_const_structrp png_ptr,
   png_const_charp name),PNG_NORETURN);
#endif

/* Puts 'string' into 'buffer' at buffer[pos], taking care never to overwrite
 * the end.  Always leaves the buffer nul terminated.  Never errors out (and
 * there is no error code.)
 */
PNG_INTERNAL_FUNCTION(size_t,png_safecat,(png_charp buffer, size_t bufsize,
   size_t pos, png_const_charp string),PNG_EMPTY);

/* Various internal functions to handle formatted warning messages, currently
 * only implemented for warnings.
 */
#if defined(PNG_WARNINGS_SUPPORTED) || defined(PNG_TIME_RFC1123_SUPPORTED)
/* Utility to dump an unsigned value into a buffer, given a start pointer and
 * and end pointer (which should point just *beyond* the end of the buffer!)
 * Returns the pointer to the start of the formatted string.  This utility only
 * does unsigned values.
 */
PNG_INTERNAL_FUNCTION(png_charp,png_format_number,(png_const_charp start,
   png_charp end, int format, png_alloc_size_t number),PNG_EMPTY);

/* Convenience macro that takes an array: */
#define PNG_FORMAT_NUMBER(buffer,format,number) \
   png_format_number(buffer, buffer + (sizeof buffer), format, number)

/* Suggested size for a number buffer (enough for 64 bits and a sign!) */
#define PNG_NUMBER_BUFFER_SIZE 24

/* These are the integer formats currently supported, the name is formed from
 * the standard printf(3) format string.
 */
#define PNG_NUMBER_FORMAT_u     1 /* chose unsigned API! */
#define PNG_NUMBER_FORMAT_02u   2
#define PNG_NUMBER_FORMAT_d     1 /* chose signed API! */
#define PNG_NUMBER_FORMAT_02d   2
#define PNG_NUMBER_FORMAT_x     3
#define PNG_NUMBER_FORMAT_02x   4
#define PNG_NUMBER_FORMAT_fixed 5 /* choose the signed API */
#endif

#ifdef PNG_WARNINGS_SUPPORTED
/* New defines and members adding in libpng-1.5.4 */
#  define PNG_WARNING_PARAMETER_SIZE 32
#  define PNG_WARNING_PARAMETER_COUNT 8 /* Maximum 9; see pngerror.c */

/* An l-value of this type has to be passed to the APIs below to cache the
 * values of the parameters to a formatted warning message.
 */
typedef char png_warning_parameters[PNG_WARNING_PARAMETER_COUNT][
   PNG_WARNING_PARAMETER_SIZE];

PNG_INTERNAL_FUNCTION(void,png_warning_parameter,(png_warning_parameters p,
   int number, png_const_charp string),PNG_EMPTY);
   /* Parameters are limited in size to PNG_WARNING_PARAMETER_SIZE characters,
    * including the trailing '\0'.
    */
PNG_INTERNAL_FUNCTION(void,png_warning_parameter_unsigned,
   (png_warning_parameters p, int number, int format, png_alloc_size_t value),
   PNG_EMPTY);
   /* Use png_alloc_size_t because it is an unsigned type as big as any we
    * need to output.  Use the following for a signed value.
    */
PNG_INTERNAL_FUNCTION(void,png_warning_parameter_signed,
   (png_warning_parameters p, int number, int format, png_int_32 value),
   PNG_EMPTY);

PNG_INTERNAL_FUNCTION(void,png_formatted_warning,(png_const_structrp png_ptr,
   png_warning_parameters p, png_const_charp message),PNG_EMPTY);
   /* 'message' follows the X/Open approach of using @1, @2 to insert
    * parameters previously supplied using the above functions.  Errors in
    * specifying the parameters will simply result in garbage substitutions.
    */
#endif

#ifdef PNG_BENIGN_ERRORS_SUPPORTED
/* Application errors (new in 1.6); use these functions (declared below) for
 * errors in the parameters or order of API function calls on read.  The
 * 'warning' should be used for an error that can be handled completely; the
 * 'error' for one which can be handled safely but which may lose application
 * information or settings.
 *
 * By default these both result in a png_error call prior to release, while in a
 * released version the 'warning' is just a warning.  However if the application
 * explicitly disables benign errors (explicitly permitting the code to lose
 * information) they both turn into warnings.
 *
 * If benign errors aren't supported they end up as the corresponding base call
 * (png_warning or png_error.)
 */
PNG_INTERNAL_FUNCTION(void,png_app_warning,(png_const_structrp png_ptr,
   png_const_charp message),PNG_EMPTY);
   /* The application provided invalid parameters to an API function or called
    * an API function at the wrong time, libpng can completely recover.
    */

PNG_INTERNAL_FUNCTION(void,png_app_error,(png_const_structrp png_ptr,
   png_const_charp message),PNG_EMPTY);
   /* As above but libpng will ignore the call, or attempt some other partial
    * recovery from the error.
    */
#else
#  define png_app_warning(pp,s) png_warning(pp,s)
#  define png_app_error(pp,s) png_error(pp,s)
#endif

PNG_INTERNAL_FUNCTION(void,png_chunk_report,(png_const_structrp png_ptr,
   png_const_charp message, int error),PNG_EMPTY);
   /* Report a recoverable issue in chunk data.  On read this is used to report
    * a problem found while reading a particular chunk and the
    * png_chunk_benign_error or png_chunk_warning function is used as
    * appropriate.  On write this is used to report an error that comes from
    * data set via an application call to a png_set_ API and png_app_error or
    * png_app_warning is used as appropriate.
    *
    * With PNG_CHUNK_FATAL an error can be marked as unrecoverable, and the
    * function will not return.
    *
    * The 'error' parameter must have one of the following values:
    */
#define PNG_CHUNK_WARNING     0 /* never an error */
#define PNG_CHUNK_WRITE_ERROR 1 /* an error only on write */
#define PNG_CHUNK_ERROR       2 /* always an error */
#define PNG_CHUNK_FATAL       3 /* an unrecoverable error */

#ifndef PNG_ERROR_TEXT_SUPPORTED
#  define png_chunk_report(pp,e,v) png_chunk_report(pp,NULL,v)
#endif

/* ASCII to FP interfaces, currently only implemented if sCAL
 * support is required.
 */
#if defined(PNG_sCAL_SUPPORTED)
/* MAX_DIGITS is actually the maximum number of characters in an sCAL
 * width or height, derived from the precision (number of significant
 * digits - a build time settable option) and assumptions about the
 * maximum ridiculous exponent.
 */
#define PNG_sCAL_MAX_DIGITS (PNG_sCAL_PRECISION+1/*.*/+1/*E*/+10/*exponent*/)

#ifdef PNG_FLOATING_POINT_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_ascii_from_fp,(png_const_structrp png_ptr,
   png_charp ascii, png_size_t size, double fp, unsigned int precision),
   PNG_EMPTY);
#endif /* FLOATING_POINT */

#ifdef PNG_FIXED_POINT_SUPPORTED
PNG_INTERNAL_FUNCTION(void,png_ascii_from_fixed,(png_const_structrp png_ptr,
   png_charp ascii, png_size_t size, png_fixed_point fp),PNG_EMPTY);
#endif /* FIXED_POINT */
#endif /* sCAL */

#if defined(PNG_sCAL_SUPPORTED) || defined(PNG_pCAL_SUPPORTED)
/* An internal API to validate the format of a floating point number.
 * The result is the index of the next character.  If the number is
 * not valid it will be the index of a character in the supposed number.
 *
 * The format of a number is defined in the PNG extensions specification
 * and this API is strictly conformant to that spec, not anyone elses!
 *
 * The format as a regular expression is:
 *
 * [+-]?[0-9]+.?([Ee][+-]?[0-9]+)?
 *
 * or:
 *
 * [+-]?.[0-9]+(.[0-9]+)?([Ee][+-]?[0-9]+)?
 *
 * The complexity is that either integer or fraction must be present and the
 * fraction is permitted to have no digits only if the integer is present.
 *
 * NOTE: The dangling E problem.
 *   There is a PNG valid floating point number in the following:
 *
 *       PNG floating point numbers are not greedy.
 *
 *   Working this out requires *TWO* character lookahead (because of the
 *   sign), the parser does not do this - it will fail at the 'r' - this
 *   doesn't matter for PNG sCAL chunk values, but it requires more care
 *   if the value were ever to be embedded in something more complex.  Use
 *   ANSI-C strtod if you need the lookahead.
 */
/* State table for the parser. */
#define PNG_FP_INTEGER    0  /* before or in integer */
#define PNG_FP_FRACTION   1  /* before or in fraction */
#define PNG_FP_EXPONENT   2  /* before or in exponent */
#define PNG_FP_STATE      3  /* mask for the above */
#define PNG_FP_SAW_SIGN   4  /* Saw +/- in current state */
#define PNG_FP_SAW_DIGIT  8  /* Saw a digit in current state */
#define PNG_FP_SAW_DOT   16  /* Saw a dot in current state */
#define PNG_FP_SAW_E     32  /* Saw an E (or e) in current state */
#define PNG_FP_SAW_ANY   60  /* Saw any of the above 4 */

/* These three values don't affect the parser.  They are set but not used.
 */
#define PNG_FP_WAS_VALID 64  /* Preceding substring is a valid fp number */
#define PNG_FP_NEGATIVE 128  /* A negative number, including "-0" */
#define PNG_FP_NONZERO  256  /* A non-zero value */
#define PNG_FP_STICKY   448  /* The above three flags */

/* This is available for the caller to store in 'state' if required.  Do not
 * call the parser after setting it (the parser sometimes clears it.)
 */
#define PNG_FP_INVALID  512  /* Available for callers as a distinct value */

/* Result codes for the parser (boolean - true meants ok, false means
 * not ok yet.)
 */
#define PNG_FP_MAYBE      0  /* The number may be valid in the future */
#define PNG_FP_OK         1  /* The number is valid */

/* Tests on the sticky non-zero and negative flags.  To pass these checks
 * the state must also indicate that the whole number is valid - this is
 * achieved by testing PNG_FP_SAW_DIGIT (see the implementation for why this
 * is equivalent to PNG_FP_OK above.)
 */
#define PNG_FP_NZ_MASK (PNG_FP_SAW_DIGIT | PNG_FP_NEGATIVE | PNG_FP_NONZERO)
   /* NZ_MASK: the string is valid and a non-zero negative value */
#define PNG_FP_Z_MASK (PNG_FP_SAW_DIGIT | PNG_FP_NONZERO)
   /* Z MASK: the string is valid and a non-zero value. */
   /* PNG_FP_SAW_DIGIT: the string is valid. */
#define PNG_FP_IS_ZERO(state) (((state) & PNG_FP_Z_MASK) == PNG_FP_SAW_DIGIT)
#define PNG_FP_IS_POSITIVE(state) (((state) & PNG_FP_NZ_MASK) == PNG_FP_Z_MASK)
#define PNG_FP_IS_NEGATIVE(state) (((state) & PNG_FP_NZ_MASK) == PNG_FP_NZ_MASK)

/* The actual parser.  This can be called repeatedly. It updates
 * the index into the string and the state variable (which must
 * be initialized to 0).  It returns a result code, as above.  There
 * is no point calling the parser any more if it fails to advance to
 * the end of the string - it is stuck on an invalid character (or
 * terminated by '\0').
 *
 * Note that the pointer will consume an E or even an E+ and then leave
 * a 'maybe' state even though a preceding integer.fraction is valid.
 * The PNG_FP_WAS_VALID flag indicates that a preceding substring was
 * a valid number.  It's possible to recover from this by calling
 * the parser again (from the start, with state 0) but with a string
 * that omits the last character (i.e. set the size to the index of
 * the problem character.)  This has not been tested within libpng.
 */
PNG_INTERNAL_FUNCTION(int,png_check_fp_number,(png_const_charp string,
   png_size_t size, int *statep, png_size_tp whereami),PNG_EMPTY);

/* This is the same but it checks a complete string and returns true
 * only if it just contains a floating point number.  As of 1.5.4 this
 * function also returns the state at the end of parsing the number if
 * it was valid (otherwise it returns 0.)  This can be used for testing
 * for negative or zero values using the sticky flag.
 */
PNG_INTERNAL_FUNCTION(int,png_check_fp_string,(png_const_charp string,
   png_size_t size),PNG_EMPTY);
#endif /* pCAL || sCAL */

#if defined(PNG_GAMMA_SUPPORTED) ||\
    defined(PNG_INCH_CONVERSIONS_SUPPORTED) || defined(PNG_READ_pHYs_SUPPORTED)
/* Added at libpng version 1.5.0 */
/* This is a utility to provide a*times/div (rounded) and indicate
 * if there is an overflow.  The result is a boolean - false (0)
 * for overflow, true (1) if no overflow, in which case *res
 * holds the result.
 */
PNG_INTERNAL_FUNCTION(int,png_muldiv,(png_fixed_point_p res, png_fixed_point a,
   png_int_32 multiplied_by, png_int_32 divided_by),PNG_EMPTY);
#endif /* GAMMA || INCH_CONVERSIONS || READ_pHYs */

#ifdef PNG_READ_GAMMA_SUPPORTED
/* Internal fixed point gamma correction.  These APIs are called as
 * required to convert single values - they don't need to be fast,
 * they are not used when processing image pixel values.
 */
PNG_INTERNAL_FUNCTION(unsigned int,png_gamma_nxmbit_correct,
   (unsigned int value, png_fixed_point gamma_val, unsigned int n/*input bits*/,
    unsigned int m/*output bits */),PNG_EMPTY);
   /* In this case the value must have 'n' bits and the output will have 'm'
    * bits.
    */

#if !PNG_RELEASE_BUILD
PNG_INTERNAL_FUNCTION(int,png_gamma_check,(png_const_structrp png_ptr,
   png_const_transform_controlp tc),PNG_EMPTY);
   /* Debugging only routine to repeat the test used above to determine if the
    * gamma was insignificant.
    */
#endif /* !RELEASE_BUILD */
#endif /* READ_GAMMA */

#ifdef PNG_SIMPLIFIED_READ_SUPPORTED
/* Internal check function to saw if the gamma of the PNG data is far enough
 * from the given screen gamma to require gamma correction (only needed for a
 * bug work-round in the simplified API).
 * TODO: it should be possible to remove the bug work-round in 1.7
 */
PNG_INTERNAL_FUNCTION(int,png_need_gamma_correction,(png_const_structrp png_ptr,
   png_fixed_point gamma, int sRGB_output),PNG_EMPTY);
#endif /* SIMPLIFIED_READ */

/* This is a utility macro to say whether a gamma value is close enough to sRGB.
 * The test is now hardwired:
 *
 * API CHANGE: prior to 1.7 this would depend on the build-time
 * PNG_GAMMA_THRESHOLD_FIXED setting, which would cause inconsistent results
 * when the setting was changed.  Since this setting can now be changed at
 * run-time it seems more sensible to have a single fixed definition of 'sRGB'.
 *
 * The test is approximately +/- 1%, it allows any decimal value from 0.45 (the
 * two digit rounded version of 1/2.2) to just under 0.46).
 */
#define PNG_GAMMA_IS_sRGB(g) ((g) >= 45000 && (g) < 46000)

/* SIMPLIFIED READ/WRITE SUPPORT */
#if defined(PNG_SIMPLIFIED_READ_SUPPORTED) ||\
   defined(PNG_SIMPLIFIED_WRITE_SUPPORTED)
/* The internal structure that png_image::opaque points to. */
typedef struct png_control
{
   png_structp png_ptr;
   png_infop   info_ptr;
   png_voidp   error_buf;           /* Always a jmp_buf at present. */

   png_const_bytep memory;          /* Memory buffer. */
   png_size_t      size;            /* Size of the memory buffer. */

   unsigned int for_write       :1; /* Otherwise it is a read structure */
   unsigned int owned_file      :1; /* We own the file in io_ptr */
} png_control;

/* Return the pointer to the jmp_buf from a png_control: necessary because C
 * does not reveal the type of the elements of jmp_buf.
 */
#ifdef __cplusplus
#  define png_control_jmp_buf(pc) (((jmp_buf*)((pc)->error_buf))[0])
#else
#  define png_control_jmp_buf(pc) ((pc)->error_buf)
#endif

/* Utility to safely execute a piece of libpng code catching and logging any
 * errors that might occur.  Returns true on success, false on failure (either
 * of the function or as a result of a png_error.)
 */
PNG_INTERNAL_CALLBACK(void,png_safe_error,(png_structp png_ptr,
   png_const_charp error_message),PNG_NORETURN);

#ifdef PNG_WARNINGS_SUPPORTED
PNG_INTERNAL_CALLBACK(void,png_safe_warning,(png_structp png_ptr,
   png_const_charp warning_message),PNG_EMPTY);
#else
#  define png_safe_warning 0/*dummy argument*/
#endif

PNG_INTERNAL_FUNCTION(int,png_safe_execute,(png_imagep image,
   int (*function)(png_voidp), png_voidp arg),PNG_EMPTY);

/* Utility to log an error; this also cleans up the png_image; the function
 * always returns 0 (false).
 */
PNG_INTERNAL_FUNCTION(int,png_image_error,(png_imagep image,
   png_const_charp error_message),PNG_EMPTY);

/* Safely initialize a stdio pointer - used by both the read and the write
 * code.
 */
#ifdef PNG_STDIO_SUPPORTED
PNG_INTERNAL_FUNCTION(int,png_image_init_io,(png_imagep image, png_FILE_p file),
   PNG_EMPTY);
#endif /* STDIO */

#ifndef PNG_SIMPLIFIED_READ_SUPPORTED
/* png_image_free is used by the write code but not exported */
PNG_INTERNAL_FUNCTION(void, png_image_free, (png_imagep image), PNG_EMPTY);
#endif /* !SIMPLIFIED_READ */

#endif /* SIMPLIFIED READ/WRITE */

#ifdef PNG_READ_SUPPORTED
PNG_INTERNAL_FUNCTION(png_int_32, png_read_setting, (png_structrp png_ptr,
   png_uint_32 setting, png_uint_32 parameter, png_int_32 value), PNG_EMPTY);
#endif /* READ */
#ifdef PNG_WRITE_SUPPORTED
PNG_INTERNAL_FUNCTION(png_int_32,  png_write_setting, (png_structrp png_ptr,
   png_uint_32 setting, png_uint_32 parameter, png_int_32 value), PNG_EMPTY);
   /* Implementations of read and write settings, in pngrutil.c and pngwutil.c
    * respectively.
    */
#endif /* WRITE */

/* Maintainer: Put new private prototypes here ^ */

/* These are initialization functions for hardware specific PNG filter
 * optimizations; list these here then select the appropriate one at compile
 * time using the macro PNG_FILTER_OPTIMIZATIONS.  If the macro is not defined
 * the generic code is used.
 */
#ifdef PNG_FILTER_OPTIMIZATIONS
PNG_INTERNAL_FUNCTION(void, PNG_FILTER_OPTIMIZATIONS, (png_structrp png_ptr,
   unsigned int bpp), PNG_EMPTY);
   /* Just declare the optimization that will be used */
#else
   /* List *all* the possible optimizations here - this branch is required if
    * the builder of libpng passes the definition of PNG_FILTER_OPTIMIZATIONS in
    * CFLAGS in place of CPPFLAGS *and* uses symbol prefixing.
    */
PNG_INTERNAL_FUNCTION(void, png_init_filter_functions_neon,
   (png_structp png_ptr, unsigned int bpp), PNG_EMPTY);
#endif

#include "pngdebug.h"

/* EXTENSION SPECIFIC FUNCTIONS */
#include "arm/neon.h"

#ifdef __cplusplus
}
#endif

#endif /* PNG_VERSION_INFO_ONLY */
#endif /* PNGPRIV_H */
