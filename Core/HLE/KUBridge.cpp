// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

// KUBridge seems to be some utility module that comes with some custom firmware for PSP,
// providing the ability to call some kernel-only functions from user mode.
// A few homebrew applications use this. We only simulate a small subset of the functionality for now.

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/KUBridge.h"
#include "Core/HLE/sceKernelModule.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Config.h"

// kuKernelCall's arguments and results: struct KernelCallArg { u32 arg1..arg12, ret1, ret2; }.
static const u32 KERNEL_CALL_ARGS = 12;
static const u32 KERNEL_CALL_RET1 = 0x30;
static const u32 KERNEL_CALL_RET2 = 0x34;

static int actionKernelCallDone = -1;

// Copies the called function's v0 and v1 to ret1 and ret2.
class KernelCallDone : public PSPAction {
public:
	static PSPAction *Create() { return new KernelCallDone(); }
	void SetArgs(u32 argsAddr) { argsAddr_ = argsAddr; }
	void run(MipsCall &call) override {
		Memory::WriteUnchecked_U32(currentMIPS->r[MIPS_REG_V0], argsAddr_ + KERNEL_CALL_RET1);
		Memory::WriteUnchecked_U32(currentMIPS->r[MIPS_REG_V1], argsAddr_ + KERNEL_CALL_RET2);
	}
	void DoState(PointerWrap &p) override {
		auto s = p.Section("KernelCallDone", 1);
		if (!s)
			return;
		Do(p, argsAddr_);
	}

private:
	u32 argsAddr_ = 0;
};

void __KUBridgeInit() {
	actionKernelCallDone = __KernelRegisterActionType(KernelCallDone::Create);
}

void __KUBridgeDoState(PointerWrap &p) {
	auto s = p.Section("KUBridge", 0, 1);
	if (!s)
		return;
	Do(p, actionKernelCallDone);
	__KernelRestoreActionType(actionKernelCallDone, KernelCallDone::Create);
}

static int kuKernelLoadModule(MIPSState *mips, const char *path, uint32_t flags, uint32_t lmOptionAddr) {
	INFO_LOG(Log::sceKernel, "kuKernelLoadModule - forwarding to sceKernelLoadModule");
	// Simply forward the call, like JPSCP does.
	return sceKernelLoadModule(mips, path, flags, lmOptionAddr);
}

static int kuKernelGetModel() {
	INFO_LOG(Log::sceKernel, "kuKernelGetModel()");
	return g_Config.iPSPModel;
}

// Runs a function of the caller in kernel mode, which for us is any mode. The first eight arguments
// go in registers, which is all we pass.
static int kuKernelCall(u32 func, u32 argsAddr) {
	if (!Memory::IsValidRange(argsAddr, KERNEL_CALL_RET2 + 4) || !Memory::IsValid4AlignedAddress(func)) {
		return hleLogError(Log::sceKernel, -1, "bad function or arguments");
	}
	u32 args[8];
	for (int i = 0; i < 8; ++i)
		args[i] = Memory::ReadUnchecked_U32(argsAddr + i * 4);
	if (Memory::ReadUnchecked_U32(argsAddr + 8 * 4) || Memory::ReadUnchecked_U32(argsAddr + 9 * 4) || Memory::ReadUnchecked_U32(argsAddr + 10 * 4) || Memory::ReadUnchecked_U32(argsAddr + 11 * 4)) {
		WARN_LOG(Log::sceKernel, "kuKernelCall: arguments 9-%d not passed", KERNEL_CALL_ARGS);
	}
	KernelCallDone *done = (KernelCallDone *)__KernelCreateAction(actionKernelCallDone);
	done->SetArgs(argsAddr);
	hleEnqueueCall(func, 8, args, done);
	return hleLogDebug(Log::sceKernel, 0);
}

const HLEFunction KUBridge[] =
{
	{ 0x4C25EA72, &WrapI_CUU<kuKernelLoadModule>, "kuKernelLoadModule", 'i', "sxx" },
	{ 0x24331850, &WrapI_V<kuKernelGetModel>, "kuKernelGetModel", 'i', "" },
	{ 0x9060F69D, &WrapI_UU<kuKernelCall>, "kuKernelCall", 'i', "xx" },
};

void Register_KUBridge() {
	RegisterHLEModule("KUBridge", ARRAY_SIZE(KUBridge), KUBridge);
}
