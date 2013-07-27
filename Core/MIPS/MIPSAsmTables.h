#pragma once

namespace MIPSAsm
{

typedef struct {
	char* name;
	char* encoding;
	int destencoding;
	int flags;
} tMipsOpcode;

typedef struct {
	char* name;
	short num;
	short len;
} tMipsRegister;

extern const tMipsRegister MipsRegister[];
extern const tMipsRegister MipsFloatRegister[];
extern const tMipsOpcode MipsOpcodes[];

}