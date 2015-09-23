#include "i18n/i18n.h"
#include "UI/OnScreenDisplay.h"
#include "Common/StringUtils.h"
#include "Common/ChunkFile.h"
#include "Common/FileUtil.h"
#include "Core/CoreTiming.h"
#include "Core/CoreParameter.h"
#include "Core/DarkFrost.h"
#include "Core/Config.h"
#include "Core/MIPS/MIPS.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/System.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/MIPS/JitCommon/NativeJit.h"

#ifdef _WIN32
#include "util/text/utf8.h"
#endif

std::string gameTitle;
std::string fname;
static DarkFrostEngine *darkFrostEngine;
static bool cheatsEnabled;
static bool realAddressing;

DarkFrostEngine::DarkFrostEngine()
{
	cheatsEnabled=false;
	realAddressing=false;
}

void DarkFrostEngine::setEngine(DarkFrostEngine *nDarkFrostEngine)
{
	darkFrostEngine=nDarkFrostEngine;
}

void DarkFrostEngine::loadCheats()
{
	//
}

void DarkFrostEngine::saveCheats()
{
	//
}

void DarkFrostEngine::toggleRealAddressing() { realAddressing=!realAddressing; }
bool DarkFrostEngine::getRealAddressing() { return realAddressing; }

void DarkFrostEngine::toggleCheatsEnabled() { cheatsEnabled=!cheatsEnabled; }
bool DarkFrostEngine::getCheatsEnabled() { return cheatsEnabled; }