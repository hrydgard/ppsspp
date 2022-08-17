/* Compile time options.
 * =====================
 * In a multi-arch build the compiler may compile the code several times for the
 * same object module, producing different binaries for different architectures.
 * When this happens configure-time setting of the target host options cannot be
 * done.  An example is iOS vs Android compilation of ARM NEON support.
 *
 * NOTE: symbol prefixing does not pass $(CFLAGS) to the preprocessor, because
 * this is not possible with certain compilers (Oracle SUN OS CC), as a result
 * it is necessary to ensure that all extern functions that *might* be used
 * regardless of $(CFLAGS) get declared in this file.  The test on __ARM_NEON
 * below is one example of this behavior because it is controlled by the
 * presence or not of -mfpu=neon on the GCC command line, it is possible to do
 * this in $(CC), e.g. "CC=gcc -mfpu=neon", but people who build libpng rarely
 * do this.
 */
#ifndef PNG_ARM_NEON_OPT
   /* ARM NEON optimizations are being controlled by the compiler settings,
    * typically the target FPU.  If the FPU supports NEON instructions then the
    * compiler will define __ARM_NEON and we can rely unconditionally on NEON
    * instructions not crashing, otherwise we must disable use of NEON
    * instructions.
    *
    * NOTE: at present these optimizations depend on 'ALIGNED_MEMORY', so they
    * can only be turned on automatically if that is supported too.  If
    * PNG_ARM_NEON_OPT is set in CPPFLAGS (to >0) then arm/arm_init.c will fail
    * to compile with an appropriate #error if ALIGNED_MEMORY has been turned
    * off.
    *
    * Note that older versions of GCC defined __ARM_NEON__; this is no longer
    * supported.  Also 32-bit ARM versions of GCC required the NEON FPU mode to
    * be turned on explicitly on the command line.  If this is not done (on
    * 32-bit ARM) NEON code will not be included.
    *
    * To disable ARM_NEON optimizations entirely, and skip compiling the
    * associated assembler code, pass --enable-arm-neon=no to configure
    * or put -DPNG_ARM_NEON_OPT=0 in CPPFLAGS.
    */
#  if defined(__ARM_NEON) && defined(PNG_ALIGNED_MEMORY_SUPPORTED)
#     define PNG_ARM_NEON_OPT 2
#  else
#     define PNG_ARM_NEON_OPT 0
#  endif
#endif

#if PNG_ARM_NEON_OPT > 0
   /* NEON optimizations are to be at least considered by libpng, so enable the
    * callbacks to do this.
    */
#  define PNG_FILTER_OPTIMIZATIONS png_init_filter_functions_neon

   /* By default the 'intrinsics' code in arm/filter_neon_intrinsics.c is used
    * if possible - if __ARM_NEON is set and the compiler version is not known
    * to be broken.  This is controlled by PNG_ARM_NEON_IMPLEMENTATION which can
    * be:
    *
    *    1  The intrinsics code (the default with __ARM_NEON)
    *    2  The hand coded assembler (the default without __ARM_NEON)
    *
    * It is possible to set PNG_ARM_NEON_IMPLEMENTATION in CPPFLAGS, however
    * this is *NOT* supported and may cease to work even after a minor revision
    * to libpng.  It *is* valid to do this for testing purposes, e.g. speed
    * testing or a new compiler, but the results should be communicated to the
    * libpng implementation list for incorporation in the next minor release.
    */
#  ifndef PNG_ARM_NEON_IMPLEMENTATION
#     ifdef __ARM_NEON
#        if defined(__clang__)
            /* At present it is unknown by the libpng developers which versions
             * of clang support the intrinsics, however some or perhaps all
             * versions do not work with the assembler so this may be
             * irrelevant, so just use the default (do nothing here.)
             */
#        elif defined(__GNUC__)
            /* GCC 4.5.4 NEON support is known to be broken.  4.6.3 is known to
             * work, so if this *is* GCC, or G++, look for a version >4.5
             */
#           if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 6)
#              define PNG_ARM_NEON_IMPLEMENTATION 2
#           endif /* no GNUC support */
#        endif /* __GNUC__ */
#     else /* !defined __ARM_NEON */
         /* The 'intrinsics' code simply won't compile without compiler support
          * and that support switches on __ARM_NEON, so use the assembler:
          */
#        define PNG_ARM_NEON_IMPLEMENTATION 2
#     endif /* __ARM_NEON */
#  endif /* !defined PNG_ARM_NEON_IMPLEMENTATION */

#  ifndef PNG_ARM_NEON_IMPLEMENTATION
      /* Use the intrinsics code by default. */
#     define PNG_ARM_NEON_IMPLEMENTATION 1
#  endif
#endif /* PNG_ARM_NEON_OPT > 0 */

#ifdef PNG_SET_OPTION_SUPPORTED
#ifdef PNG_ARM_NEON_API_SUPPORTED
#  define PNG_ARM_NEON   0 /* HARDWARE: ARM Neon SIMD instructions supported */
#endif
#endif /* SET_OPTION */

#if PNG_ARM_NEON_OPT > 0
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_up_neon,
   (png_alloc_size_t row_bytes, unsigned int bpp, png_bytep row,
    png_const_bytep prev_row, png_const_bytep prev_pixels),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_sub3_neon,
   (png_alloc_size_t row_bytes, unsigned int bpp, png_bytep row,
    png_const_bytep prev_row, png_const_bytep prev_pixels),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_sub4_neon,
   (png_alloc_size_t row_bytes, unsigned int bpp, png_bytep row,
    png_const_bytep prev_row, png_const_bytep prev_pixels),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_avg3_neon,
   (png_alloc_size_t row_bytes, unsigned int bpp, png_bytep row,
    png_const_bytep prev_row, png_const_bytep prev_pixels),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_avg4_neon,
   (png_alloc_size_t row_bytes, unsigned int bpp, png_bytep row,
    png_const_bytep prev_row, png_const_bytep prev_pixels),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_paeth3_neon,
   (png_alloc_size_t row_bytes, unsigned int bpp, png_bytep row,
    png_const_bytep prev_row, png_const_bytep prev_pixels),PNG_EMPTY);
PNG_INTERNAL_FUNCTION(void,png_read_filter_row_paeth4_neon,
   (png_alloc_size_t row_bytes, unsigned int bpp, png_bytep row,
    png_const_bytep prev_row, png_const_bytep prev_pixels),PNG_EMPTY);
#endif
