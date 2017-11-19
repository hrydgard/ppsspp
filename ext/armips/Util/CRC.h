#pragma once

unsigned short getCrc16(unsigned char* Source, size_t len);
unsigned int getCrc32(unsigned char* Source, size_t len);
unsigned int getChecksum(unsigned char* Source, size_t len);
