#include "scePspNpDrm_user.h"

#include "HLE.h"

int sceNpDrmSetLicenseeKey(u32 npDrmKeyAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceNpDrmSetLicenseeKey(%08x)", npDrmKeyAddr);
	return 0;
}

int sceNpDrmClearLicenseeKey()
{
	ERROR_LOG(HLE, "UNIMPL sceNpDrmClearLicenseeKey()");
	return 0;
}

int sceNpDrmRenameCheck(const char *filename)
{
	ERROR_LOG(HLE, "UNIMPL sceNpDrmRenameCheck(%s)", filename);
	return 0;
}

int sceNpDrmEdataSetupKey(u32 edataFd)
{
	ERROR_LOG(HLE, "UNIMPL sceNpDrmEdataSetupKey %x", edataFd);
	return 0;
}

int sceNpDrmEdataGetDataSize(u32 edataFd)
{
	ERROR_LOG(HLE, "UNIMPL sceNpDrmEdataGetDataSize %x", edataFd);
	return 0;
}

int sceNpDrmOpen()
{
	ERROR_LOG(HLE, "UNIMPL sceNpDrmOpen");
	return 0;
}

int sceKernelLoadModuleNpDrm()
{
	ERROR_LOG(HLE, "UNIMPL sceKernelLoadModuleNpDrm");
	return 0;
}

int sceKernelLoadExecNpDrm()
{
	ERROR_LOG(HLE, "UNIMPL sceKernelLoadExecNpDrm");
	return 0;
}

const HLEFunction sceNpDrm[] =
{ 
	{0xA1336091, WrapI_U<sceNpDrmSetLicenseeKey>, "sceNpDrmSetLicenseeKey"},
	{0x9B745542, WrapI_V<sceNpDrmClearLicenseeKey>, "sceNpDrmClearLicenseeKey"},
	{0x275987D1, WrapI_C<sceNpDrmRenameCheck>, "sceNpDrmRenameCheck"},
	{0x08d98894, WrapI_U<sceNpDrmEdataSetupKey>, "sceNpDrmEdataSetupKey"},
	{0x219EF5CC, WrapI_U<sceNpDrmEdataGetDataSize>, "sceNpDrmEdataGetDataSize"},
	{0x2BAA4294, 0, "sceNpDrmOpen"},
	{0xC618D0B1, 0, "sceKernelLoadModuleNpDrm"},
	{0xAA5FC85B, 0, "sceKernelLoadExecNpDrm"},
};

void Register_sceNpDrm()
{
	RegisterModule("sceNpDrm", ARRAY_SIZE(sceNpDrm), sceNpDrm);
	RegisterModule("scePspNpDrm_user", ARRAY_SIZE(sceNpDrm), sceNpDrm);
}
 