// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include "Common.h"
#include "MemoryUtil.h"

// Everything that needs to generate code should inherit from this.
// You get memory management for free, plus, you can use all emitter functions without
// having to prefix them with gen-> or something similar.
// Example implementation:
// class JIT : public CodeBlock<ARMXEmitter>, public JitInterface {}

class CodeBlockCommon {
public:
	CodeBlockCommon() : region(nullptr), region_size(0) {}
	virtual ~CodeBlockCommon() {}

	bool IsInSpace(const u8 *ptr) {
		return (ptr >= region) && (ptr < (region + region_size));
	}

	virtual void SetCodePtr(u8 *ptr) = 0;
	virtual const u8 *GetCodePtr() const = 0;

	u8 *GetBasePtr() {
		return region;
	}

	size_t GetOffset(const u8 *ptr) const {
		return ptr - region;
	}

protected:
	u8 *region;
	size_t region_size;
};

template<class T> class CodeBlock : public CodeBlockCommon, public T, NonCopyable {
private:
	// A privately used function to set the executable RAM space to something invalid.
	// For debugging usefulness it should be used to set the RAM to a host specific breakpoint instruction
	virtual void PoisonMemory() = 0;

public:
	CodeBlock() {}
	virtual ~CodeBlock() { if (region) FreeCodeSpace(); }

	// Call this before you generate any code.
	void AllocCodeSpace(int size) {
		region_size = size;
		region = (u8*)AllocateExecutableMemory(region_size);
		T::SetCodePointer(region);
	}

	// Always clear code space with breakpoints, so that if someone accidentally executes
	// uninitialized, it just breaks into the debugger.
	void ClearCodeSpace() {
		PoisonMemory();
		ResetCodePtr();
	}

	// Call this when shutting down. Don't rely on the destructor, even though it'll do the job.
	void FreeCodeSpace() {
#ifdef __SYMBIAN32__
		ResetExecutableMemory(region);
#else
		FreeMemoryPages(region, region_size);
#endif
		region = nullptr;
		region_size = 0;
	}

	// Cannot currently be undone. Will write protect the entire code region.
	// Start over if you need to change the code (call FreeCodeSpace(), AllocCodeSpace()).
	void WriteProtect() {
		ProtectMemoryPages(region, region_size, MEM_PROT_READ | MEM_PROT_EXEC);
	}

	void SetCodePtr(u8 *ptr) override {
		T::SetCodePointer(ptr);
	}

	const u8 *GetCodePtr() const override {
		return T::GetCodePointer();
	}

	void ResetCodePtr()
	{
		T::SetCodePointer(region);
	}

	size_t GetSpaceLeft() const {
		return region_size - (T::GetCodePointer() - region);
	}
};

