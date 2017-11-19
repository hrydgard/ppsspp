#pragma once
#include "Archs/Architecture.h"
#include "Pool.h"
#include "Core/Expression.h"
#include "Parser/Tokenizer.h"

#define ARM_SHIFT_LSL		0x00
#define ARM_SHIFT_LSR		0x01
#define ARM_SHIFT_ASR		0x02
#define ARM_SHIFT_ROR		0x03
#define ARM_SHIFT_RRX		0x04

enum ArmArchType { AARCH_GBA = 0, AARCH_NDS, AARCH_3DS, AARCH_LITTLE, AARCH_BIG, AARCH_INVALID };

typedef struct {
	const char* name;
	short num;
	short len;
} tArmRegister;

typedef struct {
	char Name[4];
	int Number;
} tArmRegisterInfo;

struct ArmRegisterValue
{
	std::wstring name;
	int num;
};

extern const tArmRegister ArmRegister[];

class CArmArchitecture: public CArchitecture
{
public:
	CArmArchitecture();
	~CArmArchitecture();
	void clear();

	virtual CAssemblerCommand* parseDirective(Parser& parser);
	virtual CAssemblerCommand* parseOpcode(Parser& parser);
	virtual void NextSection();
	virtual void Pass2();
	virtual void Revalidate();
	virtual IElfRelocator* getElfRelocator();
	virtual Endianness getEndianness() { return version == AARCH_BIG ? Endianness::Big : Endianness::Little; };
	void SetThumbMode(bool b) { thumb = b; };
	bool GetThumbMode() { return thumb; };
	void setVersion(ArmArchType type) { version = type; }
	ArmArchType getVersion() { return version; }

	std::vector<ArmPoolEntry> getPoolContent() { return currentPoolContent; }
	void clearPoolContent() { currentPoolContent.clear(); }
	void addPoolValue(ArmOpcodeCommand* command, int32_t value);
private:
	bool thumb;
	ArmArchType version;

	std::vector<ArmPoolEntry> currentPoolContent;
};

class ArmOpcodeCommand: public CAssemblerCommand
{
public:
	virtual void setPoolAddress(int64_t address) = 0;
};

extern CArmArchitecture Arm;
