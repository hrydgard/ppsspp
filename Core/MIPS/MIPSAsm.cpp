#include <cstdarg>
#include <cstring>
#include <memory>
#include <string_view>
#include <vector>

#include "Common/CommonTypes.h"
#include "ext/armips/Core/Assembler.h"
#include "ext/armips/Core/FileManager.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/MemMapHelpers.h"
#include "Core/MIPS/MIPSAsm.h"

class PspAssemblerFile : public AssemblerFile {
public:
	PspAssemblerFile() {
		address = 0;
	}

	bool open(bool onlyCheck) override{ return true; };
	void close() override { };
	bool isOpen() override { return true; };
	bool write(void *data, size_t length) override {
		if (!Memory::IsValidAddress((u32)(address+length-1)))
			return false;

		Memory::Memcpy((u32)address, data, (u32)length, "Debugger");
		
		// In case this is a delay slot or combined instruction, clear cache above it too.
		mipsr4k.InvalidateICache((u32)(address - 4), (int)length + 4);

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
	const fs::path &getFileName() override { return dummyFilename_; }
private:
	u64 address;
	fs::path dummyFilename_;
};

bool MipsAssembleOpcode(std::string_view line, DebugInterface *cpu, u32 address, std::string *error) {
	std::vector<std::string> errors;

	char str[64];
	snprintf(str, 64, ".psp\n.org 0x%08X\n", address);

	ArmipsArguments args;
	args.mode = ArmipsMode::MEMORY;
	args.content = str + std::string(line);
	args.silent = true;
	args.memoryFile.reset(new PspAssemblerFile());
	args.errorsResult = &errors;

	if (g_symbolMap) {
		g_symbolMap->GetLabels(args.labels);
	}

	error->clear();
	if (!runArmips(args)) {
		for (size_t i = 0; i < errors.size(); i++) {
			(*error) += errors[i];
			if (i != errors.size() - 1)
				error->push_back('\n');
		}

		return false;
	}

	return true;
}
