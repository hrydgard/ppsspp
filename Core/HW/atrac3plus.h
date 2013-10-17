#pragma once

#include "Core/HW/BufferQueue.h"

namespace Atrac3plus_Decoder {
	bool IsSupported();
	bool IsInstalled();
	bool CanAutoInstall();
	bool DoAutoInstall();
	std::string GetInstalledFilename();

	int Init();
	int Shutdown();

	typedef void* Context;

	Context OpenContext();
	int CloseContext(Context *context);
	bool Decode(Context context, void* inbuf, int inbytes, int *outbytes, void* outbuf);
}
