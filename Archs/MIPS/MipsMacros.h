#pragma once
#include "Mips.h"
#include "CMipsInstruction.h"

#define MIPSM_B						0x00000001
#define MIPSM_BU					0x00000002
#define MIPSM_HW					0x00000003
#define MIPSM_HWU					0x00000004
#define MIPSM_W						0x00000005
#define MIPSM_WU					0x00000006
#define MIPSM_DW					0x00000007
#define MIPSM_LLSCW					0x00000008
#define MIPSM_LLSCDW				0x00000009
#define MIPSM_COP1					0x0000000a
#define MIPSM_COP2					0x0000000b
#define MIPSM_DCOP1					0x0000000c
#define MIPSM_DCOP2					0x0000000d
#define MIPSM_ACCESSMASK			0x0000000f

#define MIPSM_NE					0x00000001
#define MIPSM_LT					0x00000002
#define MIPSM_LTU					0x00000003
#define MIPSM_GE					0x00000004
#define MIPSM_GEU					0x00000005
#define MIPSM_EQ					0x00000006
#define MIPSM_CONDITIONMASK			0x00000007

#define MIPSM_IMM					0x00000200
#define MIPSM_LEFT					0x00000400
#define MIPSM_RIGHT					0x00000800
#define MIPSM_UNALIGNED				0x00001000
#define MIPSM_DONTWARNDELAYSLOT		0x00002000
#define MIPSM_UPPER					0x00004000
#define MIPSM_LOWER					0x00008000
#define MIPSM_LOAD					0x00010000
#define MIPSM_STORE					0x00020000
#define MIPSM_LIKELY				0x00040000
#define MIPSM_REVCMP				0x00080000

class Parser;

typedef CAssemblerCommand* (*MipsMacroFunc)(Parser&,MipsRegisterData&,MipsImmediateData&,int);

struct MipsMacroDefinition {
	const wchar_t* name;
	const wchar_t* args;
	MipsMacroFunc function;
	int flags;
};

extern const MipsMacroDefinition mipsMacros[];

class MipsMacroCommand: public CAssemblerCommand
{
public:
	MipsMacroCommand(CAssemblerCommand* content, int macroFlags);
	~MipsMacroCommand();
	virtual bool Validate();
	virtual void Encode() const;
	virtual void writeTempData(TempData& tempData) const;
private:
	CAssemblerCommand* content;
	int macroFlags;
	bool IgnoreLoadDelay;
};
