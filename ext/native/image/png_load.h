#ifndef _PNG_LOAD_H
#define _PNG_LOAD_H

// *image_data_ptr should be deleted with free()
// return value of 1 == success.
int pngLoad(const char *file, int *pwidth, 
            int *pheight, unsigned char **image_data_ptr, bool flip);

int pngLoadPtr(const unsigned  char *input_ptr, size_t input_len, int *pwidth,
            int *pheight, unsigned char **image_data_ptr, bool flip);

#endif  // _PNG_LOAD_H
