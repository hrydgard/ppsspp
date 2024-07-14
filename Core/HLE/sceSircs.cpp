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

#ifdef __MINGW32__
#include <unistd.h>
#endif
#include <ctime>

#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceSircs.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/MemMapHelpers.h"

int sceSircsSend(u32 dataAddr, int count) {
	auto data = PSPPointer<SircsData>::Create(dataAddr);
	if (data.IsValid()) {
		INFO_LOG(Log::HLE, "%s (version=0x%x, command=0x%x, address=0x%x, count=%d)",
			__FUNCTION__, data->version, data->command, data->address, count);
		#if PPSSPP_PLATFORM(ANDROID)
			char command[40] = {0};
			snprintf(command, sizeof(command), "sircs_%d_%d_%d_%d",
				data->version, data->command, data->address, count);
			System_InfraredCommand(command);
		#endif
		data.NotifyRead("sceSircsSend");
	}
	return 0;
}

const HLEFunction sceSircs[] =
{
	{0X62411801, nullptr,                 "sceSircsInit",    '?', "" },
	{0X19155A2F, nullptr,                 "sceSircsEnd",     '?', "" },
	{0X71EEF62D, &WrapI_UI<sceSircsSend>, "sceSircsSend",    'i', "xi" },
	{0x83381633, nullptr,                 "sceSircsReceive", '?', "" },
};

void Register_sceSircs()
{
	RegisterModule("sceSircs", ARRAY_SIZE(sceSircs), sceSircs);
}
