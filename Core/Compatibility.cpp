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

#include <cstring>

#include "Common/Log.h"
#include "Common/Data/Format/IniFile.h"
#include "Common/Data/Text/I18n.h"
#include "Common/File/VFS/VFS.h"
#include "Common/StringUtils.h"
#include "Common/System/OSD.h"
#include "Core/Compatibility.h"
#include "Core/Config.h"
#include "Core/System.h"

void Compatibility::Load(const std::string &gameID) {
	Clear();

	// Allow ignoring compat settings by name (regardless of game ID.)
	std::vector<std::string> ignored;
	SplitString(g_Config.sIgnoreCompatSettings, ',', ignored);
	ignored_ = std::set<std::string>(ignored.begin(), ignored.end());
	// If ALL, don't process any compat flags.
	if (ignored_.find("ALL") != ignored_.end())
		return;

	{
		IniFile compat;
		// This loads from assets.
		if (compat.LoadFromVFS(g_VFS, "compat.ini")) {
			CheckSettings(compat, gameID);
		} else {
			auto e = GetI18NCategory(I18NCat::ERRORS);
			std::string msg = ApplySafeSubstitutions(e->T("File not found: %1"), "compat.ini");
			g_OSD.Show(OSDType::MESSAGE_ERROR, msg, 3.0f);
		}
	}

	{
		IniFile compat2;
		// This one is user-editable. Need to load it after the system one.
		Path path = GetSysDirectory(DIRECTORY_SYSTEM) / "compat.ini";
		if (compat2.Load(path)) {
			CheckSettings(compat2, gameID);
		}
	}

	{
		IniFile compat;
		// This loads from assets.
		if (compat.LoadFromVFS(g_VFS, "compatvr.ini")) {
			CheckVRSettings(compat, gameID);
		}
	}

	{
		IniFile compat2;
		// This one is user-editable. Need to load it after the system one.
		Path path = GetSysDirectory(DIRECTORY_SYSTEM) / "compatvr.ini";
		if (compat2.Load(path)) {
			CheckVRSettings(compat2, gameID);
		}
	}
}

void Compatibility::Clear() {
	memset(&flags_, 0, sizeof(flags_));
	memset(&vrCompat_, 0, sizeof(vrCompat_));
	activeList_.clear();
}

void Compatibility::CheckSettings(IniFile &iniFile, const std::string &gameID) {
	CheckSetting(iniFile, gameID, "VertexDepthRounding", &flags_.VertexDepthRounding);
	CheckSetting(iniFile, gameID, "PixelDepthRounding", &flags_.PixelDepthRounding);
	CheckSetting(iniFile, gameID, "DepthRangeHack", &flags_.DepthRangeHack);
	CheckSetting(iniFile, gameID, "ClearToRAM", &flags_.ClearToRAM);
	CheckSetting(iniFile, gameID, "Force04154000Download", &flags_.Force04154000Download);
	CheckSetting(iniFile, gameID, "DrawSyncEatCycles", &flags_.DrawSyncEatCycles);
	CheckSetting(iniFile, gameID, "DrawSyncInstant", &flags_.DrawSyncInstant);
	CheckSetting(iniFile, gameID, "FakeMipmapChange", &flags_.FakeMipmapChange);
	CheckSetting(iniFile, gameID, "RequireBufferedRendering", &flags_.RequireBufferedRendering);
	CheckSetting(iniFile, gameID, "RequireBlockTransfer", &flags_.RequireBlockTransfer);
	CheckSetting(iniFile, gameID, "RequireDefaultCPUClock", &flags_.RequireDefaultCPUClock);
	CheckSetting(iniFile, gameID, "DisableAccurateDepth", &flags_.DisableAccurateDepth);
	CheckSetting(iniFile, gameID, "MGS2AcidHack", &flags_.MGS2AcidHack);
	CheckSetting(iniFile, gameID, "SonicRivalsHack", &flags_.SonicRivalsHack);
	CheckSetting(iniFile, gameID, "BlockTransferAllowCreateFB", &flags_.BlockTransferAllowCreateFB);
	CheckSetting(iniFile, gameID, "IntraVRAMBlockTransferAllowCreateFB", &flags_.IntraVRAMBlockTransferAllowCreateFB);
	CheckSetting(iniFile, gameID, "YugiohSaveFix", &flags_.YugiohSaveFix);
	CheckSetting(iniFile, gameID, "ForceUMDDelay", &flags_.ForceUMDDelay);
	CheckSetting(iniFile, gameID, "ForceMax60FPS", &flags_.ForceMax60FPS);
	CheckSetting(iniFile, gameID, "GoWFramerateHack60", &flags_.GoWFramerateHack60);
	CheckSetting(iniFile, gameID, "FramerateHack30", &flags_.FramerateHack30);
	CheckSetting(iniFile, gameID, "JitInvalidationHack", &flags_.JitInvalidationHack);
	CheckSetting(iniFile, gameID, "HideISOFiles", &flags_.HideISOFiles);
	CheckSetting(iniFile, gameID, "MoreAccurateVMMUL", &flags_.MoreAccurateVMMUL);
	CheckSetting(iniFile, gameID, "ForceSoftwareRenderer", &flags_.ForceSoftwareRenderer);
	CheckSetting(iniFile, gameID, "DarkStalkersPresentHack", &flags_.DarkStalkersPresentHack);
	CheckSetting(iniFile, gameID, "ReportSmallMemstick", &flags_.ReportSmallMemstick);
	CheckSetting(iniFile, gameID, "MemstickFixedFree", &flags_.MemstickFixedFree);
	CheckSetting(iniFile, gameID, "DateLimited", &flags_.DateLimited);
	CheckSetting(iniFile, gameID, "ShaderColorBitmask", &flags_.ShaderColorBitmask);
	CheckSetting(iniFile, gameID, "DisableFirstFrameReadback", &flags_.DisableFirstFrameReadback);
	CheckSetting(iniFile, gameID, "MpegAvcWarmUp", &flags_.MpegAvcWarmUp);
	CheckSetting(iniFile, gameID, "BlueToAlpha", &flags_.BlueToAlpha);
	CheckSetting(iniFile, gameID, "CenteredLines", &flags_.CenteredLines);
	CheckSetting(iniFile, gameID, "MaliDepthStencilBugWorkaround", &flags_.MaliDepthStencilBugWorkaround);
	CheckSetting(iniFile, gameID, "ZZT3SelectHack", &flags_.ZZT3SelectHack);
	CheckSetting(iniFile, gameID, "AllowLargeFBTextureOffsets", &flags_.AllowLargeFBTextureOffsets);
	CheckSetting(iniFile, gameID, "AtracLoopHack", &flags_.AtracLoopHack);
	CheckSetting(iniFile, gameID, "DeswizzleDepth", &flags_.DeswizzleDepth);
	CheckSetting(iniFile, gameID, "SplitFramebufferMargin", &flags_.SplitFramebufferMargin);
	CheckSetting(iniFile, gameID, "ForceLowerResolutionForEffectsOn", &flags_.ForceLowerResolutionForEffectsOn);
	CheckSetting(iniFile, gameID, "ForceLowerResolutionForEffectsOff", &flags_.ForceLowerResolutionForEffectsOff);
	CheckSetting(iniFile, gameID, "AllowDownloadCLUT", &flags_.AllowDownloadCLUT);
	CheckSetting(iniFile, gameID, "NearestFilteringOnFramebufferCreate", &flags_.NearestFilteringOnFramebufferCreate);
	CheckSetting(iniFile, gameID, "SecondaryTextureCache", &flags_.SecondaryTextureCache);
	CheckSetting(iniFile, gameID, "EnglishOrJapaneseOnly", &flags_.EnglishOrJapaneseOnly);
	CheckSetting(iniFile, gameID, "OldAdrenoPixelDepthRoundingGL", &flags_.OldAdrenoPixelDepthRoundingGL);
	CheckSetting(iniFile, gameID, "ForceCircleButtonConfirm", &flags_.ForceCircleButtonConfirm);
	CheckSetting(iniFile, gameID, "DisallowFramebufferAtOffset", &flags_.DisallowFramebufferAtOffset);
	CheckSetting(iniFile, gameID, "RockmanDash2SoundFix", &flags_.RockmanDash2SoundFix);
	CheckSetting(iniFile, gameID, "ReadbackDepth", &flags_.ReadbackDepth);
	CheckSetting(iniFile, gameID, "BlockTransferDepth", &flags_.BlockTransferDepth);
	CheckSetting(iniFile, gameID, "DaxterRotatedAnalogStick", &flags_.DaxterRotatedAnalogStick);
	CheckSetting(iniFile, gameID, "ForceMaxDepthResolution", &flags_.ForceMaxDepthResolution);
	CheckSetting(iniFile, gameID, "SOCOMClut8Replacement", &flags_.SOCOMClut8Replacement);
	CheckSetting(iniFile, gameID, "Fontltn12Hack", &flags_.Fontltn12Hack);
	CheckSetting(iniFile, gameID, "LoadCLUTFromCurrentFrameOnly", &flags_.LoadCLUTFromCurrentFrameOnly);
	CheckSetting(iniFile, gameID, "ForceUMDReadSpeed", &flags_.ForceUMDReadSpeed);
	CheckSetting(iniFile, gameID, "KernelGetSystemTimeLowEatMoreCycles", &flags_.KernelGetSystemTimeLowEatMoreCycles);
	CheckSetting(iniFile, gameID, "TacticsOgreEliminateDebugReadback", &flags_.TacticsOgreEliminateDebugReadback);
	CheckSetting(iniFile, gameID, "FramebufferAllowLargeVerticalOffset", &flags_.FramebufferAllowLargeVerticalOffset);
	CheckSetting(iniFile, gameID, "DisableMemcpySlicing", &flags_.DisableMemcpySlicing);
	CheckSetting(iniFile, gameID, "ForceEnableGPUReadback", &flags_.ForceEnableGPUReadback);
	CheckSetting(iniFile, gameID, "UseFFMPEGFindStreamInfo", &flags_.UseFFMPEGFindStreamInfo);
	CheckSetting(iniFile, gameID, "SoftwareRasterDepth", &flags_.SoftwareRasterDepth);
	CheckSetting(iniFile, gameID, "DisableHLESceFont", &flags_.DisableHLESceFont);
	CheckSetting(iniFile, gameID, "ForceHLEPsmf", &flags_.ForceHLEPsmf);
	CheckSetting(iniFile, gameID, "SaveStatesNotRecommended", &flags_.SaveStatesNotRecommended);
	CheckSetting(iniFile, gameID, "IgnoreEnqueue", &flags_.IgnoreEnqueue);
	CheckSetting(iniFile, gameID, "MsgDialogAutoStatus", &flags_.MsgDialogAutoStatus);
	CheckSetting(iniFile, gameID, "NullPageValid", &flags_.NullPageValid);
	CheckSetting(iniFile, gameID, "DetectDestBlendSquared", &flags_.DetectDestBlendSquared);
	CheckSetting(iniFile, gameID, "BoostExactFramebufferMatch", &flags_.BoostExactFramebufferMatch);
}

void Compatibility::CheckVRSettings(IniFile &iniFile, const std::string &gameID) {
	CheckSetting(iniFile, gameID, "ForceFlatScreen", &vrCompat_.ForceFlatScreen);
	CheckSetting(iniFile, gameID, "ForceMono", &vrCompat_.ForceMono);
	CheckSetting(iniFile, gameID, "IdentityViewHack", &vrCompat_.IdentityViewHack);
	CheckSetting(iniFile, gameID, "MirroringVariant", &vrCompat_.MirroringVariant);
	CheckSetting(iniFile, gameID, "ProjectionHack", &vrCompat_.ProjectionHack);
	CheckSetting(iniFile, gameID, "Skyplane", &vrCompat_.Skyplane);
	CheckSetting(iniFile, gameID, "UnitsPerMeter", &vrCompat_.UnitsPerMeter);

	NOTICE_LOG(Log::G3D, "UnitsPerMeter for %s: %f", gameID.c_str(), vrCompat_.UnitsPerMeter);
}

void Compatibility::CheckSetting(IniFile &iniFile, const std::string &gameID, const char *option, bool *flag) {
	if (ignored_.find(option) == ignored_.end()) {
		Section *section = iniFile.GetSection(option);
		if (!section) {
			// Not found, skip.
			return;
		}
		section->Get(gameID, flag);

		// Shortcut for debugging, sometimes useful to globally enable compat flags.
		bool all = false;
		section->Get("ALL", &all);
		if (all) {
			*flag = true;
			if (!activeList_.empty()) {
				activeList_ += "\n";
			}
			activeList_ += option;
		}
	}
}

void Compatibility::CheckSetting(IniFile &iniFile, const std::string &gameID, const char *option, float *flag) {
	std::string value;
	Section *section = iniFile.GetSection(option);
	if (section && section->Get(gameID.c_str(), &value)) {
		*flag = stof(value);
	}
}

void Compatibility::CheckSetting(IniFile &iniFile, const std::string &gameID, const char *option, int *flag) {
	std::string value;
	Section *section = iniFile.GetSection(option);
	if (section && section->Get(gameID.c_str(), &value)) {
		*flag = stof(value);
	}
}
