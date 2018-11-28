// Copyright (c) 2013- PPSSPP Project.

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

#include "file/ini_file.h"
#include "Core/Compatibility.h"
#include "Core/System.h"

void Compatibility::Load(const std::string &gameID) {
	Clear();

	{
		IniFile compat;
		// This loads from assets.
		if (compat.LoadFromVFS("compat.ini")) {
			CheckSettings(compat, gameID);
		}
	}

	{
		IniFile compat2;
		// This one is user-editable. Need to load it after the system one.
		std::string path = GetSysDirectory(DIRECTORY_SYSTEM) + "compat.ini";
		if (compat2.Load(path)) {
			CheckSettings(compat2, gameID);
		}
	}
}

void Compatibility::Clear() {
	memset(&flags_, 0, sizeof(flags_));
}

void Compatibility::CheckSettings(IniFile &iniFile, const std::string &gameID) {
	CheckSetting(iniFile, gameID, "VertexDepthRounding", &flags_.VertexDepthRounding);
	CheckSetting(iniFile, gameID, "PixelDepthRounding", &flags_.PixelDepthRounding);
	CheckSetting(iniFile, gameID, "DepthRangeHack", &flags_.DepthRangeHack);
	CheckSetting(iniFile, gameID, "ClearToRAM", &flags_.ClearToRAM);
	CheckSetting(iniFile, gameID, "Force04154000Download", &flags_.Force04154000Download);
	CheckSetting(iniFile, gameID, "DrawSyncEatCycles", &flags_.DrawSyncEatCycles);
	CheckSetting(iniFile, gameID, "FakeMipmapChange", &flags_.FakeMipmapChange);
	CheckSetting(iniFile, gameID, "RequireBufferedRendering", &flags_.RequireBufferedRendering);
	CheckSetting(iniFile, gameID, "RequireBlockTransfer", &flags_.RequireBlockTransfer);
	CheckSetting(iniFile, gameID, "RequireDefaultCPUClock", &flags_.RequireDefaultCPUClock);
	CheckSetting(iniFile, gameID, "DisableReadbacks", &flags_.DisableReadbacks);
	CheckSetting(iniFile, gameID, "DisableAccurateDepth", &flags_.DisableAccurateDepth);
	CheckSetting(iniFile, gameID, "MGS2AcidHack", &flags_.MGS2AcidHack);
	CheckSetting(iniFile, gameID, "SonicRivalsHack", &flags_.SonicRivalsHack);
	CheckSetting(iniFile, gameID, "BlockTransferAllowCreateFB", &flags_.BlockTransferAllowCreateFB);
	CheckSetting(iniFile, gameID, "YugiohSaveFix", &flags_.YugiohSaveFix);
}

void Compatibility::CheckSetting(IniFile &iniFile, const std::string &gameID, const char *option, bool *flag) {
	iniFile.Get(option, gameID.c_str(), flag, *flag);
}
