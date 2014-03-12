#pragma once

#include "Core/Debugger/DebugInterface.h"
#include "Core/MIPS/MIPSAsmTables.h"

namespace MIPSAsm {

bool MipsAssembleOpcode(const char* line, DebugInterface* cpu, u32 address, u32& dest);

enum eMipsImmediateType { MIPS_NOIMMEDIATE, MIPS_IMMEDIATE5,
	MIPS_IMMEDIATE16, MIPS_IMMEDIATE20, MIPS_IMMEDIATE26 };

typedef struct {
	int rs;			// source reg
	int rt;			// target reg
	int rd;			// dest reg
	eMipsImmediateType ImmediateType;
	int Immediate;
	int OriginalImmediate;
} tMipsOpcodeVariables;

class CMipsInstruction
{
public:
	CMipsInstruction(DebugInterface* cpu):Loaded(false), cpu(cpu) { };
	bool Load(char* Name, char* Params, int RamPos);
	bool Validate();
	void Encode();
	u32 getEncoding() { return encoding; };
private:
	bool LoadEncoding(const tMipsOpcode& SourceOpcode, char* Line);
	tMipsOpcode Opcode;
	bool NoCheckError;
	bool Loaded;
	tMipsOpcodeVariables Vars;
	int RamPos;
	DebugInterface* cpu;
	u32 encoding;
};

char* GetAssembleError();

}
