#pragma once

namespace MIPSAsm
{

typedef struct {
	const char *name;
	const char *encoding;
	unsigned int destencoding;
	unsigned int flags;
} tMipsOpcode;

typedef struct {
	const char *name;
	short num;
	short len;
} tMipsRegister;

#define O_RS          0x00000001 // source reg
#define O_RT          0x00000002 // target reg
#define O_RD          0x00000004 // dest reg
#define O_I16         0x00000008 // 16 bit immediate
#define O_I20         0x00000010 // 16 bit immediate
#define O_IPCA        0x00000020 // pc >> 2
#define O_IPCR        0x00000080 // PC, -> difference >> 2
#define O_I26         0x00000200 // 26 bit immediate
#define O_I5          0x00000400 // 5 bit immediate
#define O_RSD         0x00000800 // rs = rd
#define O_RST         0x00001000 // rs = rt
#define O_RDT         0x00002000 // rd = rt
#define MO_DELAY      0x00004000 // delay slot follows
#define MO_NODELAY    0x00008000 // can't be in a delay slot
#define MO_DELAYRT    0x00010000 // rt won't be available for one instruction
#define MO_IGNORERTD  0x00020000 // don't care for rt delay

#define MO_FRS        0x00040000 // float rs
#define MO_FRD        0x00080000 // float rd
#define MO_FRT        0x00100000 // float rt
#define MO_FRSD       0x00200000 // float rs + rd
#define MO_FRST       0x00400000 // float rs + rt
#define MO_FRDT       0x00800000 // float rt + rd

extern const tMipsRegister MipsRegister[];
extern const tMipsRegister MipsFloatRegister[];
extern const tMipsOpcode MipsOpcodes[];

}