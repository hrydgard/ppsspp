#pragma once

#define THUMB_TYPE1		0x00
#define THUMB_TYPE2		0x01
#define THUMB_TYPE3		0x02
#define THUMB_TYPE4		0x03
#define THUMB_TYPE5		0x04
#define THUMB_TYPE6		0x05
#define THUMB_TYPE7		0x06
#define THUMB_TYPE8		0x07
#define THUMB_TYPE9		0x08
#define THUMB_TYPE10	0x09
#define THUMB_TYPE11	0x0A
#define THUMB_TYPE12	0x0B
#define THUMB_TYPE13	0x0C
#define THUMB_TYPE14	0x0D
#define THUMB_TYPE15	0x0E
#define THUMB_TYPE16	0x0F
#define THUMB_TYPE17	0x10
#define THUMB_TYPE18	0x11
#define THUMB_TYPE19	0x12

#define THUMB_ARM9					0x00000001
#define THUMB_IMMEDIATE				0x00000002
#define THUMB_REGISTER				0x00000004
#define THUMB_D						0x00000008
#define THUMB_S						0x00000010
#define THUMB_POOL					0x00000020
#define THUMB_O						0x00000040
#define THUMB_WORD					0x00000080
#define THUMB_HALFWORD				0x00000100
#define THUMB_RLIST					0x00000200
#define THUMB_EXCHANGE				0x00000400
#define THUMB_BRANCH				0x00000800
#define THUMB_LONG					0x00001000
#define THUMB_PCR					0x00002000
#define THUMB_DS					0x00004000	// rs = rd
#define THUMB_PCADD					0x00008000
#define THUMB_NEGATIVE_IMMEDIATE	0x00010000

typedef struct {
	const char* name;
	const char* mask;
	unsigned short encoding;
	unsigned char type:5;
	unsigned char length:3;
	int flags;
} tThumbOpcode;

extern const tThumbOpcode ThumbOpcodes[];
