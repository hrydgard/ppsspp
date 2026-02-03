#include "Core/HLE/scePspNpDrm_user.h"
#include "Core/MemMapHelpers.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceIo.h"
#include "Core/FileSystems/MetaFileSystem.h"

extern MetaFileSystem pspFileSystem;

// PSP NpDrm licensee key tracking
static const int PSP_NPDRM_LICENSEE_KEY_LENGTH = 0x10;
static u8 licenseeKey[PSP_NPDRM_LICENSEE_KEY_LENGTH];
static bool isLicenseeKeySet = false;

// Check if the file is an encrypted EDAT file by reading the magic number
static bool isEncrypted(u32 edataFd) {
	// Check for "\0PSPEDAT" magic number
	// The magic bytes are: 0x00 0x50 0x53 0x50 0x45 0x44 0x41 0x54
	// Which translates to: 0x50535000 and 0x54414445 as uint32_t (little endian)	
	
	u32 error;
	// Get the file handle from the file descriptor
	u32 fileHandle = __IoGetFileHandleFromId(edataFd, error);
	if (fileHandle == (u32)-1) {
		// Invalid file handle, assume not encrypted
		return false;
	}
	
	// Save the current file position
	s64 originalPos = pspFileSystem.GetSeekPos(fileHandle);
	
	// Seek to the beginning of the file
	pspFileSystem.SeekFile(fileHandle, 0, FILEMOVE_BEGIN);
	
	// Read 8 bytes from the file
	u8 header[8];
	size_t bytesRead = pspFileSystem.ReadFile(fileHandle, header, 8);
	
	// Restore the original file position
	pspFileSystem.SeekFile(fileHandle, (s32)originalPos, FILEMOVE_BEGIN);
	
	// Check if we read enough bytes
	if (bytesRead < 8) {
		return false;
	}
	
	// Check the magic numbers
	// Bytes 0-3 should be 0x50535000 (PSP\0) in little endian
	// Bytes 4-7 should be 0x54414445 (EDAT) in little endian
	u32 magic1 = (header[3] << 24) | (header[2] << 16) | (header[1] << 8) | header[0];
	u32 magic2 = (header[7] << 24) | (header[6] << 16) | (header[5] << 8) | header[4];
	
	// PSP\0 = 0x50535000, EDAT = 0x54414445
	return (magic1 == 0x50535000 && magic2 == 0x54414445);
}

static int sceNpDrmSetLicenseeKey(u32 npDrmKeyAddr) {
	// Read the licensee key from memory
	// The key is PSP_NPDRM_LICENSEE_KEY_LENGTH bytes (0x10 = 16 bytes)
	if (Memory::IsValidAddress(npDrmKeyAddr)) {
		Memory::Memcpy(licenseeKey, npDrmKeyAddr, PSP_NPDRM_LICENSEE_KEY_LENGTH, "NpDrmSetLicenseeKey");
		isLicenseeKeySet = true;
		return hleLogInfo(Log::sceIo, 0);
	}
	return hleLogError(Log::sceIo, SCE_NPDRM_ERROR_INVALID_FILE, "Invalid address");
}

static int sceNpDrmClearLicenseeKey() {
	// Clear the licensee key and mark it as not set
	memset(licenseeKey, 0, PSP_NPDRM_LICENSEE_KEY_LENGTH);
	isLicenseeKeySet = false;
	return hleLogInfo(Log::sceIo, 0);
}

static int sceNpDrmRenameCheck(const char *filename) {
	return hleLogWarning(Log::sceIo, 0, "UNIMPL");
}

static int sceNpDrmEdataSetupKey(u32 edataFd) {
	// Return an error if the key has not been set.
	// Note: An empty key is valid, as long as it was set with sceNpDrmSetLicenseeKey.
#ifndef __LIBRETRO__
	// FIXME: sceNpDrmSetLicenseeKey is never called (?), therefore skip this check.
	if (!isLicenseeKeySet) {
		return hleLogError(Log::sceIo, SCE_NPDRM_ERROR_NO_K_LICENSEE_SET, "Licensee key not set");
	}
#endif

	// Get file info to validate the file descriptor
	u32 error;
	u32 fileHandle = __IoGetFileHandleFromId(edataFd, error);
	if (fileHandle == (u32)-1) {
		return hleLogError(Log::sceIo, -1, "Invalid file descriptor");
	}

	int result = 0;

	// Check if the file is encrypted
	if (isEncrypted(edataFd)) {
		// File is encrypted, set up PGD decryption
		// In JPCSP, this wraps the file in an EDATVirtualFile (lines 215-218)
		// In this C++ implementation, we use ioctl commands to achieve the same result
		int usec = 0;

		// set PGD offset
		int retval = __IoIoctl(edataFd, 0x04100002, 0x90, 0, 0, 0, usec);
		if (retval < 0) {
			return hleDelayResult(hleLogError(Log::sceIo, retval), "io ctrl command", usec);
		}

		// call PGD open
		// Note that usec accumulates.
		retval = __IoIoctl(edataFd, 0x04100001, 0, 0, 0, 0, usec);
		if (retval < 0) {
			return hleDelayResult(hleLogError(Log::sceIo, retval), "io ctrl command", usec);
		}
		
		return hleDelayResult(hleLogDebug(Log::sceIo, result), "io ctrl command", usec);
	}

	return hleLogInfo(Log::sceIo, result);
}

static int sceNpDrmEdataGetDataSize(u32 edataFd) {
	int retval = hleCall(IoFileMgrForKernel, u32, sceIoIoctl, edataFd, 0x04100010, 0, 0, 0, 0);
	return hleLogInfo(Log::sceIo, retval);
}

static int sceNpDrmOpen() {
	return hleLogError(Log::sceIo, 0, "UNIMPL: sceNpDrmOpen()");
}

const HLEFunction sceNpDrm[] = {
	{0XA1336091, &WrapI_U<sceNpDrmSetLicenseeKey>,   "sceNpDrmSetLicenseeKey",   'i', "x"},
	{0X9B745542, &WrapI_V<sceNpDrmClearLicenseeKey>, "sceNpDrmClearLicenseeKey", 'i', "" },
	{0X275987D1, &WrapI_C<sceNpDrmRenameCheck>,      "sceNpDrmRenameCheck",      'i', "s"},
	{0X08D98894, &WrapI_U<sceNpDrmEdataSetupKey>,    "sceNpDrmEdataSetupKey",    'i', "x"},
	{0X219EF5CC, &WrapI_U<sceNpDrmEdataGetDataSize>, "sceNpDrmEdataGetDataSize", 'i', "x"},
	{0X2BAA4294, &WrapI_V<sceNpDrmOpen>,             "sceNpDrmOpen",             'i', "" },
};

void Register_sceNpDrm() {
	RegisterHLEModule("sceNpDrm", ARRAY_SIZE(sceNpDrm), sceNpDrm);
	RegisterHLEModule("scePspNpDrm_user", ARRAY_SIZE(sceNpDrm), sceNpDrm);
}
