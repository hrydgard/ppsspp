//https://www.programmersought.com/article/17665094415/

#include <stdio.h>   
#include <stdlib.h>   
#include <string.h>   
#include <errno.h>   
#include <unistd.h>   
#include <fcntl.h>   
#include <sys/stat.h>

int calc_img_crc(const char *in_file, unsigned int *img_crc);
void init_crc32_tab();
