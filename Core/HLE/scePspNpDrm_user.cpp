#include "scePspNpDrm_user.h"

#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceIo.h"

static int sceNpDrmSetLicenseeKey(u32 npDrmKeyAddr)
{
	INFO_LOG(Log::HLE, "call sceNpDrmSetLicenseeKey(%08x)", npDrmKeyAddr);
	return 0;
}

static int sceNpDrmClearLicenseeKey()
{
	INFO_LOG(Log::HLE, "call sceNpDrmClearLicenseeKey()");
	return 0;
}

static int sceNpDrmRenameCheck(const char *filename)
{
	INFO_LOG(Log::HLE, "call sceNpDrmRenameCheck(%s)", filename);
	return 0;
}

static int sceNpDrmEdataSetupKey(u32 edataFd)
{
	INFO_LOG(Log::HLE, "call sceNpDrmEdataSetupKey %x", edataFd);
	int usec = 0;
	// set PGD offset
	int retval = __IoIoctl(edataFd, 0x04100002, 0x90, 0, 0, 0, usec);
	if (retval != 0) {
		return hleDelayResult(retval, "io ctrl command", usec);
	}
	// call PGD open
	// Note that usec accumulates.
	retval = __IoIoctl(edataFd, 0x04100001, 0, 0, 0, 0, usec);
	return hleDelayResult(retval, "io ctrl command", usec);
}

static int sceNpDrmEdataGetDataSize(u32 edataFd)
{
	INFO_LOG(Log::HLE, "call sceNpDrmEdataGetDataSize %x", edataFd);
	return sceIoIoctl(edataFd, 0x04100010, 0, 0, 0, 0);
}

static int sceNpDrmOpen()
{
	ERROR_LOG(Log::HLE, "UNIMPL sceNpDrmOpen");
	return 0;
}

const HLEFunction sceNpDrm[] =
{ 
	{0XA1336091, &WrapI_U<sceNpDrmSetLicenseeKey>,   "sceNpDrmSetLicenseeKey",   'i', "x"},
	{0X9B745542, &WrapI_V<sceNpDrmClearLicenseeKey>, "sceNpDrmClearLicenseeKey", 'i', "" },
	{0X275987D1, &WrapI_C<sceNpDrmRenameCheck>,      "sceNpDrmRenameCheck",      'i', "s"},
	{0X08D98894, &WrapI_U<sceNpDrmEdataSetupKey>,    "sceNpDrmEdataSetupKey",    'i', "x"},
	{0X219EF5CC, &WrapI_U<sceNpDrmEdataGetDataSize>, "sceNpDrmEdataGetDataSize", 'i', "x"},
	{0X2BAA4294, &WrapI_V<sceNpDrmOpen>,             "sceNpDrmOpen",             'i', "" },
};

void Register_sceNpDrm()
{
	RegisterModule("sceNpDrm", ARRAY_SIZE(sceNpDrm), sceNpDrm);
	RegisterModule("scePspNpDrm_user", ARRAY_SIZE(sceNpDrm), sceNpDrm);
}
 
