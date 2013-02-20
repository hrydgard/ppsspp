#include "HLE.h"


const HLEFunction sce[] =
{ 
	{0xA1336091, 0, "sceNpDrmSetLicenseeKey"},
	{0x9B745542, 0, "sceNpDrmClearLicenseeKey"},
	{0x275987D1, 0, "sceNpDrmRenameCheck"},
	{0x08d98894, 0, "sceNpDrmEdataSetupKey"},
	{0x219EF5CC, 0, "sceNpDrmEdataGetDataSize"},
	{0x2BAA4294, 0, "sceNpDrmOpen"},
	{0xC618D0B1, 0, "sceKernelLoadModuleNpDrm"},
	{0xAA5FC85B, 0, "sceKernelLoadExecNpDrm"},
};
