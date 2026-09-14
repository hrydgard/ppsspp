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

#include <vector>

#include "Core/MemMap.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/ELF/PrxDecrypter.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceResmgr.h"

// The resource manager. Games never touch this - it exists for the VSH, which uses the one
// function below to decrypt flash0:/vsh/etc/index_XXg.dat, the index of what the XMB shows. With
// this unresolved the call traps, the index stays encrypted, the job thread building the top menu
// gives up, and the shell ends on an error screen instead.

// Decrypts a buffer in place. Named after its NID, as in JPCSP - the real name isn't known, though
// the index file is the only caller we've seen.
//
// Parameters:
//  - bufferAddr: encrypted data in, decrypted data out.
//  - bufferSize: how much is there.
//  - resultLengthAddr: receives the decrypted length, which is smaller - the header goes away.
static int sceResmgr_9DC14891(u32 bufferAddr, int bufferSize, u32 resultLengthAddr) {
	if (bufferSize < 0 || !Memory::IsValidRange(bufferAddr, bufferSize))
		return hleLogError(Log::sceMisc, -1, "bad buffer");

	u8 *buffer = Memory::GetPointerWriteUnchecked(bufferAddr);

	// Already plaintext - the check JPCSP uses, and cheap insurance against decrypting twice.
	if (bufferSize >= 8 && !memcmp(buffer, "release:", 8)) {
		if (Memory::IsValidAddress(resultLengthAddr))
			Memory::WriteUnchecked_U32(bufferSize, resultLengthAddr);
		return hleLogDebug(Log::sceMisc, 0, "already decrypted");
	}

	// Decrypting in place is fine for the caller, but pspDecryptPRX walks the input while writing
	// the output, so give it somewhere separate to write and copy back on success. On failure the
	// guest buffer is left exactly as it was.
	std::vector<u8> decrypted(bufferSize);
	const int decryptedSize = pspDecryptPRX(buffer, decrypted.data(), bufferSize);
	if (decryptedSize <= 0) {
		return hleLogError(Log::sceMisc, -1, "failed to decrypt %d bytes (decrypter said %d)", bufferSize, decryptedSize);
	}

	memcpy(buffer, decrypted.data(), decryptedSize);
	NotifyMemInfo(MemBlockFlags::WRITE, bufferAddr, decryptedSize, "sceResmgrDecrypt");
	if (Memory::IsValidAddress(resultLengthAddr))
		Memory::WriteUnchecked_U32(decryptedSize, resultLengthAddr);
	// A correctly decrypted index starts "release:", so say whether it does - a wrong-but-plausible
	// decrypt otherwise just looks like success here and fails much later as an unreadable index.
	const bool plausible = decryptedSize >= 8 && !memcmp(buffer, "release:", 8);
	return hleLogInfo(Log::sceMisc, 0, "decrypted %d bytes to %d (%s)", bufferSize, decryptedSize,
		plausible ? "looks right" : "SUSPECT - does not start with 'release:'");
}

const HLEFunction sceResmgr[] = {
	{0X9DC14891, &WrapI_UIU<sceResmgr_9DC14891>, "sceResmgr_9DC14891", 'i', "xix"},
};

void Register_sceResmgr() {
	RegisterHLEModule("sceResmgr", ARRAY_SIZE(sceResmgr), sceResmgr);
}
