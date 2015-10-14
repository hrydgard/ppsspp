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

static DarkFrostEngine *darkFrostEngine;

DarkFrostEngine::DarkFrostEngine()
{
	cheatsEnabled=false;
	realAddressing=false;
	valueFormat=0;
}

void DarkFrostEngine::setEngine(DarkFrostEngine *nDarkFrostEngine)
{
	darkFrostEngine=nDarkFrostEngine;
}

void DarkFrostEngine::reloadCheats()
{
	//
}

void DarkFrostEngine::saveCheats()
{
	//
}

void DarkFrostEngine::toggleRealAddressing() { realAddressing=!realAddressing; }
bool DarkFrostEngine::getRealAddressing() const { return realAddressing; }

void DarkFrostEngine::toggleCheatsEnabled() { cheatsEnabled=!cheatsEnabled; }
bool DarkFrostEngine::getCheatsEnabled() const { return cheatsEnabled; }

unsigned char DarkFrostEngine::getASCII(unsigned int value, unsigned int byte)
{
	unsigned char val=*((unsigned char*)(&value+byte));
	if((val<=0x20) || (val==0xFF)) val='.';
	return val;
}