#pragma once
#include "Commands/CAssemblerCommand.h"
#include "ArmOpcodes.h"
#include "Arm.h"
#include "Core/Expression.h"

struct ArmOpcodeVariables {
	struct {
		unsigned char c,a;
		bool s,x,y;
		unsigned int NewEncoding;
		char NewType;
		bool UseNewEncoding;
		bool UseNewType;
	} Opcode;

	struct {
		unsigned char Type;
		bool ShiftByRegister;
		bool UseShift;
		ArmRegisterValue reg;
		Expression ShiftExpression;
		int ShiftAmount;
		unsigned char FinalType;
		int FinalShiftAmount;
		bool UseFinal;
	} Shift;

	struct {
		bool spsr;
		int field;
	} PsrData;

	struct {
		ArmRegisterValue cd;	// cop register d
		ArmRegisterValue cn;	// cop register n
		ArmRegisterValue cm;	// cop register m
		ArmRegisterValue pn;	// cop number
		Expression CpopExpression;	// cp opc number
		Expression CpinfExpression;	// cp information
		int Cpop;
		int Cpinf;
	} CopData;

	ArmRegisterValue rs;
	ArmRegisterValue rm;
	ArmRegisterValue rd;
	ArmRegisterValue rn;
	bool psr;
	bool writeback;
	bool SignPlus;
	bool negative;
	Expression ImmediateExpression;
	int Immediate;
	int ImmediateBitLen;
	int OriginalImmediate;
	int rlist;
	char RlistStr[64];
};

class CArmInstruction: public ArmOpcodeCommand
{
public:
	CArmInstruction(const tArmOpcode& sourceOpcode, ArmOpcodeVariables& vars);
//	~CArmInstruction();
	bool Load(char* Name, char* Params);
	virtual bool Validate();
	virtual void Encode() const;
	virtual void writeTempData(TempData& tempData) const;
	virtual void setPoolAddress(int64_t address);
private:
	void FormatOpcode(char* Dest, const char* Source) const;
	void FormatInstruction(const char* encoding, char* dest) const;
	int getShiftedImmediate(unsigned int num, int& ShiftAmount);

	ArmOpcodeVariables Vars;
	tArmOpcode Opcode;
	int64_t RamPos;
	ArmArchType arch;
};
