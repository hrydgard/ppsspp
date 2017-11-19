#pragma once


// opcode types
#define ARM_TYPE3 0
#define ARM_TYPE4 1
#define ARM_TYPE5 2
#define ARM_TYPE6 3
#define ARM_TYPE7 4
#define ARM_TYPE8 5
#define ARM_TYPE9 6
#define ARM_TYPE10 7
#define ARM_TYPE11 8
#define ARM_TYPE12 9
#define ARM_TYPE13 10
#define ARM_TYPE14 11
#define ARM_TYPE15 12
#define ARM_TYPE16 13
#define ARM_TYPE17 14
#define ARM_MISC 15

// opcode flags
#define ARM_ARM9			0x00000001
#define ARM_S				0x00000002
#define ARM_D				0x00000004
#define ARM_N				0x00000008
#define ARM_M				0x00000010
#define ARM_IMMEDIATE		0x00000020
#define ARM_WORD			0x00000040
#define ARM_HALFWORD		0x00000080
#define ARM_EXCHANGE		0x00000100	
#define ARM_UNCOND			0x00000200
#define ARM_REGISTER		0x00000400
#define ARM_LOAD			0x00000800
#define ARM_STORE			0x00001000
#define ARM_X				0x00002000
#define ARM_Y				0x00004000
#define ARM_SHIFT			0x00008000
#define ARM_POOL			0x00010000
#define ARM_ABS				0x00020000	// absolute immediate value for range check
#define ARM_BRANCH			0x00040000
#define ARM_ABSIMM			0x00080000	// ldr r0,[200h]
#define ARM_MRS				0x00100000	// differentiate between msr und mrs
#define ARM_SWI				0x00200000	// software interrupt
#define ARM_COPOP			0x00400000	// cp opcode number
#define ARM_COPINF			0x00800000	// cp information number
#define ARM_DN				0x01000000	// rn = rd
#define ARM_DM				0x02000000	// rm = rd
#define ARM_SIGN			0x04000000	// sign
#define ARM_RDEVEN			0x08000000	// rd has to be even
#define ARM_OPTIMIZE		0x10000000	// optimization
#define ARM_OPMOVMVN		0x20000000	// ... of mov/mvn
#define ARM_OPANDBIC		0x40000000	// ... of and/bic
#define ARM_OPCMPCMN		0x80000000	// ... of cmp/cmn
#define ARM_PCR			   0x100000000	// pc relative

typedef struct {
	const char* name;
	const char* mask;
	unsigned int encoding;
	unsigned int type:4;
	int64_t flags;
} tArmOpcode;

#define ARM_AMODE_IB 0
#define ARM_AMODE_IA 1
#define ARM_AMODE_DB 2
#define ARM_AMODE_DA 3
#define ARM_AMODE_ED 4
#define ARM_AMODE_FD 5
#define ARM_AMODE_EA 6
#define ARM_AMODE_FA 7

typedef struct {
	unsigned char p:1;
	unsigned char u:1;
} tArmAddressingMode;

extern const tArmOpcode ArmOpcodes[];
extern const unsigned char LdmModes[8];
extern const unsigned char StmModes[8];
