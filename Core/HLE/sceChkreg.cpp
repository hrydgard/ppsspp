#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceChkreg.h"

static int sceChkregCheckRegion(int unknown1, int unknown2) {
	// region is correct.
	return hleLogWarning(Log::sceMisc, 1);
}

int sceChkreg_driver_6894A027(u32 unknown1, u32 unknown2) {
	if (Memory::IsValid4AlignedAddress(unknown1)) {
		Memory::WriteUnchecked_U32(1, unknown1);
	}
	return hleLogWarning(Log::sceMisc, 0);
}

int sceChkregGetPspModel() {
	// Has no parameters
	return hleLogWarning(Log::sceMisc, 1);
}

int sceChkregGetPsCode(u32 ptr) {
	static const u8 values[8] = {1, 0, 5, 0, 1, 0, 1, 0};
	if (Memory::IsValidRange(ptr, 8)) {
		Memory::MemcpyUnchecked(ptr, values, 8);
	}

	return hleLogWarning(Log::sceMisc, 0);
}

static const HLEFunction sceChkreg[] = {
	{0x54495B19, &WrapI_II<sceChkregCheckRegion>,      "sceChkregCheckRegion",      'i', "ii" },
	{0x59F8491D, &WrapI_U< sceChkregGetPsCode>,        "sceChkregGetPsCode",        'i', "x"},
	{0x6894A027, &WrapI_UU<sceChkreg_driver_6894A027>, "sceChkreg_driver_6894A027", 'i', "xx" },
	{0x7939C851, &WrapI_V<sceChkregGetPspModel>,       "sceChkregGetPspModel",      'i', "" },
};

void Register_sceChkreg() {
	RegisterHLEModule("sceChkreg_driver", ARRAY_SIZE(sceChkreg), sceChkreg);
}
