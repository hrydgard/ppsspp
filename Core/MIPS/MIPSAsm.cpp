#ifdef _WIN32
#include "stdafx.h"
#endif
#include <cstdarg>
#include <cstring>
#include <vector>

#include "Common/CommonTypes.h"

// This has to be before basictypes to avoid a define conflict.
#include "ext/armips/Core/Assembler.h"

#include "util/text/utf8.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/MemMapHelpers.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/MIPSAsm.h"

namespace MIPSAsm
{	
	static std::wstring errorText;

std::wstring GetAssembleError()
{
	return errorText;
}

class PspAssemblerFile: public AssemblerFile
{
public:
	PspAssemblerFile() {
		address = 0;
	}

	bool open(bool onlyCheck) override{ return true; };
	void close() override { };
	bool isOpen() override { return true; };
	bool write(void* data, size_t length) override {
		if (!Memory::IsValidAddress((u32)(address+length-1)))
			return false;

		Memory::Memcpy((u32)address,data,(u32)length);
		
		// In case this is a delay slot or combined instruction, clear cache above it too.
		if (MIPSComp::jit)
			MIPSComp::jit->InvalidateCacheAt((u32)(address - 4),(int)length+4);

		address += length;
		return true;
	}
	int64_t getVirtualAddress() override { return address; };
	int64_t getPhysicalAddress() override { return getVirtualAddress(); };
	int64_t getHeaderSize() override { return 0; }
	bool seekVirtual(int64_t virtualAddress) override {
		if (!Memory::IsValidAddress(virtualAddress))
			return false;
		address = virtualAddress;
		return true;
	}
	bool seekPhysical(int64_t physicalAddress) override { return seekVirtual(physicalAddress); }
	const std::wstring &getFileName() override { return dummyWFilename_; }
private:
	u64 address;
	std::wstring dummyWFilename_;
};

bool MipsAssembleOpcode(const char* line, DebugInterface* cpu, u32 address)
{
	PspAssemblerFile file;
	StringList errors;

	wchar_t str[64];
	swprintf(str,64,L".psp\n.org 0x%08X\n",address);

	ArmipsArguments args;
	args.mode = ArmipsMode::MEMORY;
	args.content = str + ConvertUTF8ToWString(line);
	args.silent = true;
	args.memoryFile = &file;
	args.errorsResult = &errors;

	if (g_symbolMap) {
		g_symbolMap->GetLabels(args.labels);
	}

	errorText = L"";
	if (!runArmips(args))
	{
		for (size_t i = 0; i < errors.size(); i++)
		{
			errorText += errors[i];
			if (i != errors.size()-1)
				errorText += L"\n";
		}

		return false;
	}

	return true;
}

}  // namespace
