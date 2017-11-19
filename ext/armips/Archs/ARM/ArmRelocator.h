#pragma once
#include "Core/ELF/ElfRelocator.h"
#include "Parser/Parser.h"

enum {
	R_ARM_ABS32 = 2,
	R_ARM_THM_CALL = 10,
	R_ARM_CALL = 28,
	R_ARM_JUMP24 = 29,
	R_ARM_TARGET1 = 38,
};


class ArmElfRelocator: public IElfRelocator
{
public:
	ArmElfRelocator(bool arm9): arm9(arm9) { };
	virtual bool relocateOpcode(int type, RelocationData& data);
	virtual void setSymbolAddress(RelocationData& data, int64_t symbolAddress, int symbolType);
	virtual CAssemblerCommand* generateCtorStub(std::vector<ElfRelocatorCtor>& ctors);
private:
	bool arm9;
};