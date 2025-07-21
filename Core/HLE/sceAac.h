#pragma once

#include <map>

#include "Common/Serialize/Serializer.h"


enum {
	ERROR_AAC_INVALID_ADDRESS = 0x80691002,
	ERROR_AAC_INVALID_PARAMETER = 0x80691003,
};
void __AACShutdown();
void __AACDoState(PointerWrap &p);

void Register_sceAac();

class AuCtx;
// Just for the debugger
extern std::map<u32, AuCtx*> g_aacMap;
