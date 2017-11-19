#include "stdafx.h"
#include "ThumbOpcodes.h"
#include "Core/Common.h"
#include "Arm.h"

/*	Placeholders:
	d	register
	s	register
	n	register
	o	register
	D	high register
	S	high register
	i	x bit immediate
	I	32 bit immediate
	p	load pc relative immediate
	P	literal pool value
	R	register list
	r	specific register
*/

const tThumbOpcode ThumbOpcodes[] = {
//	Name		Mask				Encod	Type		  Len	Flags
	{ "lsl",	"d,s,/#i\x05",		0x0000,	THUMB_TYPE1,	2,	THUMB_IMMEDIATE },
	{ "lsl",	"d,/#i\x05",		0x0000,	THUMB_TYPE1,	2,	THUMB_IMMEDIATE|THUMB_DS },
	{ "asl",	"d,s,/#i\x05",		0x0000,	THUMB_TYPE1,	2,	THUMB_IMMEDIATE },
	{ "asl",	"d,/#i\x05",		0x0000,	THUMB_TYPE1,	2,	THUMB_IMMEDIATE|THUMB_DS },
	{ "lsr",	"d,s,/#i\x05",		0x0800,	THUMB_TYPE1,	2,	THUMB_IMMEDIATE },
	{ "lsr",	"d,/#i\x05",		0x0800,	THUMB_TYPE1,	2,	THUMB_IMMEDIATE|THUMB_DS },
	{ "asr",	"d,s,/#i\x05",		0x1000,	THUMB_TYPE1,	2,	THUMB_IMMEDIATE },
	{ "asr",	"d,/#i\x05",		0x1000,	THUMB_TYPE1,	2,	THUMB_IMMEDIATE|THUMB_DS },

	{ "add",	"d,s,n",			0x1800,	THUMB_TYPE2,	2,	THUMB_REGISTER },
	{ "add",	"d,n",				0x1800,	THUMB_TYPE2,	2,	THUMB_REGISTER|THUMB_DS },
	{ "sub",	"d,s,n",			0x1A00,	THUMB_TYPE2,	2,	THUMB_REGISTER },
	{ "sub",	"d,n",				0x1A00,	THUMB_TYPE2,	2,	THUMB_REGISTER|THUMB_DS },
	{ "add",	"d,s,/#i\x03",		0x1C00,	THUMB_TYPE2,	2,	THUMB_IMMEDIATE },
	{ "sub",	"d,s,/#i\x03",		0x1E00,	THUMB_TYPE2,	2,	THUMB_IMMEDIATE },
	{ "mov",	"d,s",				0x1C00,	THUMB_TYPE2,	2,	0 },

	{ "mov",	"d,/#i\x08",		0x2000,	THUMB_TYPE3,	2,	THUMB_IMMEDIATE },
	{ "cmp",	"d,/#i\x08",		0x2800,	THUMB_TYPE3,	2,	THUMB_IMMEDIATE },
	{ "add",	"d,/#i\x08",		0x3000,	THUMB_TYPE3,	2,	THUMB_IMMEDIATE },
	{ "sub",	"d,/#i\x08",		0x3800,	THUMB_TYPE3,	2,	THUMB_IMMEDIATE },

	{ "and",	"d,s",				0x4000,	THUMB_TYPE4,	2,	THUMB_REGISTER },
	{ "eor",	"d,s",				0x4040,	THUMB_TYPE4,	2,	THUMB_REGISTER },
	{ "xor",	"d,s",				0x4040,	THUMB_TYPE4,	2,	THUMB_REGISTER },
	{ "lsl",	"d,s",				0x4080,	THUMB_TYPE4,	2,	THUMB_REGISTER },
	{ "lsr",	"d,s",				0x40C0,	THUMB_TYPE4,	2,	THUMB_REGISTER },
	{ "asr",	"d,s",				0x4100,	THUMB_TYPE4,	2,	THUMB_REGISTER },
	{ "adc",	"d,s",				0x4140,	THUMB_TYPE4,	2,	THUMB_REGISTER },
	{ "sbc",	"d,s",				0x4180,	THUMB_TYPE4,	2,	THUMB_REGISTER },
	{ "ror",	"d,s",				0x41C0,	THUMB_TYPE4,	2,	THUMB_REGISTER },
	{ "tst",	"d,s",				0x4200,	THUMB_TYPE4,	2,	THUMB_REGISTER },
	{ "neg",	"d,s",				0x4240,	THUMB_TYPE4,	2,	THUMB_REGISTER },
	{ "cmp",	"d,s",				0x4280,	THUMB_TYPE4,	2,	THUMB_REGISTER },
	{ "cmn",	"d,s",				0x42C0,	THUMB_TYPE4,	2,	THUMB_REGISTER },
	{ "orr",	"d,s",				0x4300,	THUMB_TYPE4,	2,	THUMB_REGISTER },
	{ "mul",	"d,s",				0x4340,	THUMB_TYPE4,	2,	THUMB_REGISTER },
	{ "bic",	"d,s",				0x4380,	THUMB_TYPE4,	2,	THUMB_REGISTER },
	{ "mvn",	"d,s",				0x43C0,	THUMB_TYPE4,	2,	THUMB_REGISTER },

	{ "add",	"D,S",				0x4400,	THUMB_TYPE5,	2,	THUMB_D|THUMB_S },
	{ "cmp",	"D,S",				0x4500,	THUMB_TYPE5,	2,	THUMB_D|THUMB_S },
	{ "mov",	"D,S",				0x4600,	THUMB_TYPE5,	2,	THUMB_D|THUMB_S },
	{ "nop",	"",					0x46C0,	THUMB_TYPE5,	2,	0 },
	{ "bx",		"S",				0x4700,	THUMB_TYPE5,	2,	THUMB_S },
	{ "blx",	"S",				0x4780,	THUMB_TYPE5,	2,	THUMB_S|THUMB_ARM9 },

	{ "ldr",	"d,[r\xF]",			0x4800,	THUMB_TYPE6,	2,	0 },
	{ "ldr",	"d,[r\xF,/#i\x08]",	0x4800,	THUMB_TYPE6,	2,	THUMB_IMMEDIATE|THUMB_WORD },
	{ "ldr",	"d,[/#I\x20]",		0x4800,	THUMB_TYPE6,	2,	THUMB_IMMEDIATE|THUMB_PCR },
	{ "ldr",	"d,=/#I\x20",		0x4800,	THUMB_TYPE6,	2,	THUMB_IMMEDIATE|THUMB_POOL },

	{ "str",	"d,[s,o]",			0x5000,	THUMB_TYPE7,	2,	THUMB_D|THUMB_S|THUMB_O },
	{ "strb",	"d,[s,o]",			0x5400,	THUMB_TYPE7,	2,	THUMB_D|THUMB_S|THUMB_O },
	{ "ldr",	"d,[s,o]",			0x5800,	THUMB_TYPE7,	2,	THUMB_D|THUMB_S|THUMB_O },
	{ "ldrb",	"d,[s,o]",			0x5C00,	THUMB_TYPE7,	2,	THUMB_D|THUMB_S|THUMB_O },

	{ "strh",	"d,[s,o]",			0x5200,	THUMB_TYPE8,	2,	THUMB_D|THUMB_S|THUMB_O },
	{ "ldsb",	"d,[s,o]",			0x5600,	THUMB_TYPE8,	2,	THUMB_D|THUMB_S|THUMB_O },
	{ "ldrsb",	"d,[s,o]",			0x5600,	THUMB_TYPE8,	2,	THUMB_D|THUMB_S|THUMB_O },
	{ "ldrh",	"d,[s,o]",			0x5A00,	THUMB_TYPE8,	2,	THUMB_D|THUMB_S|THUMB_O },
	{ "ldsh",	"d,[s,o]",			0x5E00,	THUMB_TYPE8,	2,	THUMB_D|THUMB_S|THUMB_O },
	{ "ldrsh",	"d,[s,o]",			0x5E00,	THUMB_TYPE8,	2,	THUMB_D|THUMB_S|THUMB_O },

	{ "str",	"d,[s,/#i\x05]",	0x6000,	THUMB_TYPE9,	2,	THUMB_D|THUMB_S|THUMB_IMMEDIATE|THUMB_WORD },
	{ "str",	"d,[s]",			0x6000,	THUMB_TYPE9,	2,	THUMB_D|THUMB_S },
	{ "ldr",	"d,[s,/#i\x05]",	0x6800,	THUMB_TYPE9,	2,	THUMB_D|THUMB_S|THUMB_IMMEDIATE|THUMB_WORD },
	{ "ldr",	"d,[s]",			0x6800,	THUMB_TYPE9,	2,	THUMB_D|THUMB_S },
	{ "strb",	"d,[s,/#i\x05]",	0x7000,	THUMB_TYPE9,	2,	THUMB_D|THUMB_S|THUMB_IMMEDIATE },
	{ "strb",	"d,[s]",			0x7000,	THUMB_TYPE9,	2,	THUMB_D|THUMB_S },
	{ "ldrb",	"d,[s,/#i\x05]",	0x7800,	THUMB_TYPE9,	2,	THUMB_D|THUMB_S|THUMB_IMMEDIATE },
	{ "ldrb",	"d,[s]",			0x7800,	THUMB_TYPE9,	2,	THUMB_D|THUMB_S },

	{ "strh",	"d,[s,/#i\x05]",	0x8000,	THUMB_TYPE10,	2,	THUMB_D|THUMB_S|THUMB_IMMEDIATE|THUMB_HALFWORD },
	{ "strh",	"d,[s]",			0x8000,	THUMB_TYPE10,	2,	THUMB_D|THUMB_S },
	{ "ldrh",	"d,[s,/#i\x05]",	0x8800,	THUMB_TYPE10,	2,	THUMB_D|THUMB_S|THUMB_IMMEDIATE|THUMB_HALFWORD },
	{ "ldrh",	"d,[s]",			0x8800,	THUMB_TYPE10,	2,	THUMB_D|THUMB_S },

	{ "str",	"d,[r\xD,/#i\x08]",	0x9000,	THUMB_TYPE11,	2,	THUMB_D|THUMB_IMMEDIATE|THUMB_WORD },
	{ "str",	"d,[r\xD]",			0x9000,	THUMB_TYPE11,	2,	THUMB_D },
	{ "ldr",	"d,[r\xD,/#i\x08]",	0x9800,	THUMB_TYPE11,	2,	THUMB_D|THUMB_IMMEDIATE|THUMB_WORD },
	{ "ldr",	"d,[r\xD]",			0x9800,	THUMB_TYPE11,	2,	THUMB_D },

	{ "add",	"d,r\xF,/#i\x08",	0xA000,	THUMB_TYPE12,	2,	THUMB_D|THUMB_IMMEDIATE|THUMB_WORD },
	{ "add",	"d,=/#i\x20",		0xA000,	THUMB_TYPE12,	2,	THUMB_D|THUMB_IMMEDIATE|THUMB_PCR },
	{ "add",	"d,r\xD,/#i\x08",	0xA800,	THUMB_TYPE12,	2,	THUMB_D|THUMB_IMMEDIATE|THUMB_WORD },

	{ "add",	"r\xD,/#i\x08",		0xB000,	THUMB_TYPE13,	2,	THUMB_IMMEDIATE|THUMB_WORD },
	{ "sub",	"r\xD,/#i\x08",		0xB000,	THUMB_TYPE13,	2,	THUMB_IMMEDIATE|THUMB_WORD|THUMB_NEGATIVE_IMMEDIATE },

	{ "add",	"r\xD,r\xD,/#i\x08",0xB000,	THUMB_TYPE13,	2,	THUMB_IMMEDIATE|THUMB_WORD },
	{ "sub",	"r\xD,r\xD,/#i\x08",0xB000,	THUMB_TYPE13,	2,	THUMB_IMMEDIATE|THUMB_WORD|THUMB_NEGATIVE_IMMEDIATE },

	{ "push",	"/{R\xFF\x40/}",	0xB400,	THUMB_TYPE14,	2,	THUMB_RLIST },
	{ "pop",	"/{R\xFF\x80/}",	0xBC00,	THUMB_TYPE14,	2,	THUMB_RLIST },

	{ "stmia",	"/[d/]!,/{R\xFF\x00/}",	0xC000,	THUMB_TYPE15,	2,	THUMB_D|THUMB_RLIST },
	{ "ldmia",	"/[d/]!,/{R\xFF\x00/}",	0xC800,	THUMB_TYPE15,	2,	THUMB_D|THUMB_RLIST },

	{ "beq",	"/#I\x08",			0xD000,	THUMB_TYPE16,	2,	THUMB_IMMEDIATE|THUMB_BRANCH },
	{ "bne",	"/#I\x08",			0xD100,	THUMB_TYPE16,	2,	THUMB_IMMEDIATE|THUMB_BRANCH  },
	{ "bcs",	"/#I\x08",			0xD200,	THUMB_TYPE16,	2,	THUMB_IMMEDIATE|THUMB_BRANCH  },
	{ "bhs",	"/#I\x08",			0xD200,	THUMB_TYPE16,	2,	THUMB_IMMEDIATE|THUMB_BRANCH  },
	{ "bcc",	"/#I\x08",			0xD300,	THUMB_TYPE16,	2,	THUMB_IMMEDIATE|THUMB_BRANCH  },
	{ "blo",	"/#I\x08",			0xD300,	THUMB_TYPE16,	2,	THUMB_IMMEDIATE|THUMB_BRANCH  },
	{ "bmi",	"/#I\x08",			0xD400,	THUMB_TYPE16,	2,	THUMB_IMMEDIATE|THUMB_BRANCH  },
	{ "bpl",	"/#I\x08",			0xD500,	THUMB_TYPE16,	2,	THUMB_IMMEDIATE|THUMB_BRANCH  },
	{ "bvs",	"/#I\x08",			0xD600,	THUMB_TYPE16,	2,	THUMB_IMMEDIATE|THUMB_BRANCH  },
	{ "bvc",	"/#I\x08",			0xD700,	THUMB_TYPE16,	2,	THUMB_IMMEDIATE|THUMB_BRANCH  },
	{ "bhi",	"/#I\x08",			0xD800,	THUMB_TYPE16,	2,	THUMB_IMMEDIATE|THUMB_BRANCH  },
	{ "bls",	"/#I\x08",			0xD900,	THUMB_TYPE16,	2,	THUMB_IMMEDIATE|THUMB_BRANCH  },
	{ "bge",	"/#I\x08",			0xDA00,	THUMB_TYPE16,	2,	THUMB_IMMEDIATE|THUMB_BRANCH  },
	{ "blt",	"/#I\x08",			0xDB00,	THUMB_TYPE16,	2,	THUMB_IMMEDIATE|THUMB_BRANCH  },
	{ "bgt",	"/#I\x08",			0xDC00,	THUMB_TYPE16,	2,	THUMB_IMMEDIATE|THUMB_BRANCH  },
	{ "ble",	"/#I\x08",			0xDD00,	THUMB_TYPE16,	2,	THUMB_IMMEDIATE|THUMB_BRANCH  },

	{ "swi",	"/#i\x08",			0xDF00,	THUMB_TYPE17,	2,	THUMB_IMMEDIATE },
	{ "bkpt",	"/#i\x08",			0xBE00,	THUMB_TYPE17,	2,	THUMB_IMMEDIATE|THUMB_ARM9 },

	{ "b",		"/#I\x0B",			0xE000,	THUMB_TYPE18,	2,	THUMB_IMMEDIATE|THUMB_BRANCH  },

	{ "bl",		"r\xE",				0xF800,	THUMB_TYPE19,	4,	0 },
	{ "bl",		"r\xE+/#I\x0B",		0xF800,	THUMB_TYPE19,	4,	THUMB_IMMEDIATE|THUMB_HALFWORD },
	{ "blh",	"/#I\x0B",			0xF800,	THUMB_TYPE19,	4,	THUMB_IMMEDIATE|THUMB_HALFWORD },
	{ "bl",		"/#I\x16",			0xF000,	THUMB_TYPE19,	4,	THUMB_IMMEDIATE|THUMB_BRANCH|THUMB_LONG  },
	{ "blx",	"/#I\x16",			0xF000,	THUMB_TYPE19,	4,	THUMB_IMMEDIATE|THUMB_ARM9|THUMB_EXCHANGE|THUMB_BRANCH|THUMB_LONG },
	{ "bl",		"/#I\x16",			0xF800,	THUMB_TYPE19,	4,	THUMB_IMMEDIATE|THUMB_BRANCH|THUMB_LONG },
	{ "blx",	"/#I\x16",			0xF800,	THUMB_TYPE19,	4,	THUMB_IMMEDIATE|THUMB_ARM9|THUMB_EXCHANGE|THUMB_BRANCH|THUMB_LONG },

	{ NULL, NULL, 0, 0, 0}
};

