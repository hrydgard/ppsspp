#define PNG_AVAILABLE
#ifdef PNG_AVAILABLE
#include <png.h>
#include <stdlib.h>
#include <stdio.h>

int pngLoad(const char *file, int *pwidth, 
            int *pheight, unsigned char **image_data_ptr,
            bool flip) {
  FILE *infile = fopen(file, "rb");
  if (!infile) {
    printf("No such file: %s\n", file);
    return 0;
  }
  /* Check for the 8-byte signature */
  char      sig[8];        /* PNG signature array */
  int len = fread(sig, 1, 8, infile);
  if (len != 8 || !png_check_sig((unsigned char *) sig, 8)) {
    fclose(infile);
    return 0;
  }
  png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png_ptr) {
    fclose(infile);
    return 4;   /* out of memory */
  }
  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr) {
    png_destroy_read_struct(&png_ptr, (png_infopp) NULL, (png_infopp) NULL);
    fclose(infile);
    return 4;   /* out of memory */
  }
  if (setjmp(png_jmpbuf(png_ptr))) {
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(infile);
    return 0;
  }

  png_init_io(png_ptr, infile);
  png_set_sig_bytes(png_ptr, 8);  // we already checked the sig bytes
  png_read_info(png_ptr, info_ptr);
  int        bit_depth=0;
  int        color_type=0;
  png_uint_32 width=0, height=0;
  png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, NULL, NULL, NULL);
  *pwidth = (int)width;
  *pheight = (int)height;
  // Set up some transforms. Always load RGBA.
  if (color_type & PNG_COLOR_MASK_ALPHA) {
    // png_set_strip_alpha(png_ptr);
  }
  if (bit_depth > 8) {
    png_set_strip_16(png_ptr);
  }
  if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
    png_set_gray_to_rgb(png_ptr);
  }
  if (color_type == PNG_COLOR_TYPE_PALETTE) {
    png_set_palette_to_rgb(png_ptr);
  }
  if (color_type == PNG_COLOR_TYPE_RGB) {
    png_set_filler(png_ptr, 255, PNG_FILLER_AFTER);
  }

  // Update the png info struct.
  png_read_update_info(png_ptr, info_ptr);
  unsigned long rowbytes = png_get_rowbytes(png_ptr, info_ptr);
  unsigned char *image_data = NULL;    /* raw png image data */
  if ((image_data = (unsigned char *) malloc(rowbytes * height))==NULL) {
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    return 4;
  }
  png_bytepp row_pointers = NULL;
  if ((row_pointers = (png_bytepp)malloc(height*sizeof(png_bytep))) == NULL) {
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    free(image_data);
    image_data = NULL;
    return 4;
  }
  if (flip) {
    for (unsigned long i = 0; i < height; ++i)
      row_pointers[height - 1 - i] = (png_byte *)(image_data + i*rowbytes);
  } else {
    for (unsigned long i = 0; i < height; ++i)
      row_pointers[i] = (png_byte *)(image_data + i*rowbytes);
  }
  png_read_image(png_ptr, row_pointers);
  free(row_pointers);
  png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
  fclose(infile);
  *image_data_ptr = image_data;
  return 1;
}

#endif