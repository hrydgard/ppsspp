//https://www.programmersought.com/article/17665094415/

#include "UI/linux_crc32.h"
 
#define BUFSIZE   16 * 1024  
 
static unsigned int crc_table[256];  
 
void init_crc_table()
{  
    unsigned int c;  
    unsigned int i, j;  
 
    for (i = 0; i < 256; i++) {  
        c = (unsigned int)i;  
        for (j = 0; j < 8; j++) {  
            if (c & 1)  
                c = 0xedb88320 ^ (c >> 1);  
            else  
                c = c >> 1;  
        }  
        crc_table[i] = c;  
    }  
}  
 
unsigned int crc32(unsigned int crc,unsigned char *buffer, unsigned int size)  
{  
    unsigned int i = crc ^ 0xffffffff;  
    while (size--)
    {
		i = (i >> 8) ^ crc_table[(i & 0xff) ^ *buffer++];
    }
    return i ^ 0xffffffff;
}  
 
int calc_img_crc(const char *in_file, unsigned int *img_crc)  
{  
    int fd;  
    int nread;  
    int ret;  
    unsigned char buf[BUFSIZE];
    unsigned int crc = 0;   
 
    fd = open(in_file, O_RDONLY);  
    if (fd < 0) {  
        printf("%d:open %s.\n", __LINE__, strerror(errno));  
        return -1;  
    }  
 
    while ((nread = read(fd, buf, BUFSIZE)) > 0) {  
        crc = crc32(crc, buf, nread);  
    }  
    *img_crc = crc;  
 
    close(fd);  
 
    if (nread < 0) {  
        printf("%d:read %s.\n", __LINE__, strerror(errno));  
        return -1;  
    }  
 
    return 0;  
}  
