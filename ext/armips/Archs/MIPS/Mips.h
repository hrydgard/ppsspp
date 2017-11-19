#pragma once
#include "Archs/Architecture.h"
#include "Core/Expression.h"
#include "Core/ELF/ElfRelocator.h"

enum MipsArchType { MARCH_PSX = 0, MARCH_N64, MARCH_PS2, MARCH_PSP, MARCH_RSP, MARCH_INVALID };

enum {
	R_MIPS_NONE,
	R_MIPS_16,
	R_MIPS_32,
	R_MIPS_REL32,
	R_MIPS_26,
	R_MIPS_HI16,
	R_MIPS_LO16,
	R_MIPS_GPREL16,
	R_MIPS_LITERAL,
	R_MIPS_GOT16,
	R_MIPS_PC16,
	R_MIPS_CALL16,
	R_MIPS_GPREL32
};

class CMipsArchitecture: public CArchitecture
{
public:
	CMipsArchitecture();
	virtual CAssemblerCommand* parseDirective(Parser& parser);
	virtual CAssemblerCommand* parseOpcode(Parser& parser);
	virtual void NextSection();
	virtual void Pass2() { return; };
	virtual void Revalidate();
	virtual IElfRelocator* getElfRelocator();
	virtual Endianness getEndianness()
	{
		return Version == MARCH_N64 || Version == MARCH_RSP ? Endianness::Big : Endianness::Little;
	};
	void SetLoadDelay(bool Delay, int Register);
	bool GetLoadDelay() { return LoadDelay; };
	int GetLoadDelayRegister() { return LoadDelayRegister; };
	bool GetIgnoreDelay() { return IgnoreLoadDelay; };
	void SetIgnoreDelay(bool b) { IgnoreLoadDelay = b; };
	void SetFixLoadDelay(bool b) { FixLoadDelay = b; };
	bool GetFixLoadDelay() { return FixLoadDelay; };
	void SetVersion(MipsArchType v) { Version = v; };
	MipsArchType GetVersion() { return Version; };
	bool GetDelaySlot() { return DelaySlot; };
	void SetDelaySlot(bool b) {DelaySlot = b; };
	bool hasLoadDelay() { return Version == MARCH_PSX; };
private:
	bool FixLoadDelay;
	bool IgnoreLoadDelay;
	bool LoadDelay;
	int LoadDelayRegister;
	bool DelaySlot;
	MipsArchType Version;
};

typedef struct {
	const char* name;
	short num;
	short len;
} tMipsRegister;

typedef struct {
	char name[5];
	short num;
} MipsRegisterInfo;

enum MipsVfpuType { MIPSVFPU_VECTOR, MIPSVFPU_MATRIX };

struct MipsVFPURegister
{
	MipsVfpuType type;
	int num;
	char name[8];
};

extern const tMipsRegister MipsRegister[];
extern CMipsArchitecture Mips;

bool MipsGetRegister(const char* source, int& RetLen, MipsRegisterInfo& Result);
int MipsGetRegister(const char* source, int& RetLen);
bool MipsGetFloatRegister(const char* source, int& RetLen, MipsRegisterInfo& Result);
bool MipsGetPs2VectorRegister(const char* source, int& RetLen, MipsRegisterInfo& Result);
int MipsGetFloatRegister(const char* source, int& RetLen);
bool MipsCheckImmediate(const char* Source, Expression& Dest, int& RetLen);

class MipsElfRelocator: public IElfRelocator
{
public:
	virtual bool relocateOpcode(int type, RelocationData& data);
	virtual void setSymbolAddress(RelocationData& data, int64_t symbolAddress, int symbolType);
	virtual CAssemblerCommand* generateCtorStub(std::vector<ElfRelocatorCtor>& ctors);
};
