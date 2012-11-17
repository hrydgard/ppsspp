#pragma once

#if defined(ANDROID) || defined(BLACKBERRY)
#error DO NOT COMPILE THIS INTO ANDROID BUILDS
#endif

char *disasmx86(unsigned char *opcode1,int codeoff1,int *len);
