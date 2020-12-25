#ifndef __CORETYPES_H__
#define __CORETYPES_H__

#include <stdint.h>
#include <stdio.h>

#define ARRAY_LENGTH(x) (sizeof(x)/sizeof(x[0]))

typedef uint64_t UINT64;
typedef uint32_t UINT32;
typedef uint16_t UINT16;
typedef uint8_t UINT8;

typedef int64_t INT64;
typedef int32_t INT32;
typedef int16_t INT16;
typedef int8_t INT8;

#define core_file FILE
#define core_fopen(file) fopen(file, "rb")
#define core_fseek fseek
#define core_fread(fc, buff, len) fread(buff, 1, len, fc)
#define core_fclose fclose
#define core_ftell ftell
static size_t core_fsize(core_file *f)
{
    long p = ftell(f);
    fseek(f, 0, SEEK_END);
    long rv = ftell(f);
    fseek(f, p, SEEK_SET);
    return rv;
}

#endif
