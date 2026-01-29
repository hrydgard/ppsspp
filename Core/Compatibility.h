// Copyright (c) 2015- PPSSPP Project.

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

#pragma once

#include <string>
#include <cstdint>
#include <set>

// Compatibility flags are controlled by assets/compat.ini.
// Alternatively, if PSP/System/compat.ini exists, it is merged on top, to enable editing
// the file on Android for tests.
//
// This file is not meant to be user-editable, although is kept as a separate ini
// file instead of compiled into the code for debugging purposes.
//
// The uses cases are strict:
//   * Enable fixes for things we can't reasonably emulate without completely ruining
//     performance for other games, such as the screen copies in Dangan Ronpa
//   * Disabling accuracy features like 16-bit depth rounding, when we can't seem to
//     implement them at all in a 100% compatible way
//   * Emergency game-specific compatibility fixes before releases, such as the GTA
//     music problem where every attempted fix has reduced compatibility with other games
//   * Enable "unsafe" performance optimizations that some games can tolerate and
//     others cannot. We do not currently have any of those.
//
// This functionality should NOT be used for any of the following:
//   * Cheats
//   * Fun hacks, like enlarged heads or whatever
//   * Fixing general compatibility issues. First try to find a general solution. Try hard.
//
// We already have the Action Replay-based cheat system for such use cases.

// TODO: Turn into bitfield for smaller mem footprint.
struct CompatFlags {
	bool VertexDepthRounding;
	bool PixelDepthRounding;
	bool DepthRangeHack;
	bool ClearToRAM;
	bool Force04154000Download;
	bool DrawSyncEatCycles;
	bool DrawSyncInstant;
	bool FakeMipmapChange;
	bool RequireBufferedRendering;
	bool RequireBlockTransfer;
	bool RequireDefaultCPUClock;
	bool DisableAccurateDepth;
	bool MGS2AcidHack;
	bool SonicRivalsHack;
	bool BlockTransferAllowCreateFB;
	bool IntraVRAMBlockTransferAllowCreateFB;
	bool YugiohSaveFix;
	bool ForceUMDDelay;
	bool ForceMax60FPS;
	bool GoWFramerateHack60;
	bool FramerateHack30;
	bool JitInvalidationHack;
	bool HideISOFiles;
	bool MoreAccurateVMMUL;
	bool ForceSoftwareRenderer;
	bool DarkStalkersPresentHack;
	bool ReportSmallMemstick;
	bool MemstickFixedFree;
	bool DateLimited;
	bool ShaderColorBitmask;
	bool DisableFirstFrameReadback;
	bool MpegAvcWarmUp;
	bool BlueToAlpha;
	bool CenteredLines;
	bool MaliDepthStencilBugWorkaround;
	bool ZZT3SelectHack;
	bool AllowLargeFBTextureOffsets;
	bool AtracLoopHack;
	bool DeswizzleDepth;
	bool SplitFramebufferMargin;
	bool ForceLowerResolutionForEffectsOn;
	bool ForceLowerResolutionForEffectsOff;
	bool AllowDownloadCLUT;
	bool NearestFilteringOnFramebufferCreate;
	bool SecondaryTextureCache;
	bool EnglishOrJapaneseOnly;
	bool OldAdrenoPixelDepthRoundingGL;
	bool ForceCircleButtonConfirm;
	bool DisallowFramebufferAtOffset;
	bool RockmanDash2SoundFix;
	bool ReadbackDepth;
	bool BlockTransferDepth;
	bool DaxterRotatedAnalogStick;
	bool ForceMaxDepthResolution;
	bool SOCOMClut8Replacement;
	bool Fontltn12Hack;
	bool LoadCLUTFromCurrentFrameOnly;
	bool ForceUMDReadSpeed;
	bool KernelGetSystemTimeLowEatMoreCycles;
	bool TacticsOgreEliminateDebugReadback;
	bool FramebufferAllowLargeVerticalOffset;
	bool DisableMemcpySlicing;
	bool ForceEnableGPUReadback;
	bool UseFFMPEGFindStreamInfo;
	bool SoftwareRasterDepth;
	bool DisableHLESceFont;
	bool ForceHLEPsmf;
	bool SaveStatesNotRecommended;
	bool IgnoreEnqueue;
	bool MsgDialogAutoStatus;
	bool NullPageValid;
	bool DetectDestBlendSquared;
	bool BoostExactFramebufferMatch;
};

struct VRCompat {
	bool ForceMono;
	bool ForceFlatScreen;
	bool IdentityViewHack;
	int MirroringVariant;
	bool ProjectionHack;
	bool Skyplane;
	float UnitsPerMeter;
};

class IniFile;

class Compatibility {
public:
	Compatibility() {
		Clear();
	}

	// Flags enforced read-only through const. Only way to change them is to load assets/compat.ini.
	const CompatFlags &flags() const { return flags_; }

	const VRCompat &vrCompat() const { return vrCompat_; }

	void Load(const std::string &gameID);

	const std::string &GetActiveFlagsString() const {
		return activeList_;
	}

private:
	void Clear();
	void CheckSettings(IniFile &iniFile, const std::string &gameID);
	void CheckVRSettings(IniFile &iniFile, const std::string &gameID);
	void CheckSetting(IniFile &iniFile, const std::string &gameID, const char *option, bool *flag);
	void CheckSetting(IniFile &iniFile, const std::string &gameID, const char *option, float *value);
	void CheckSetting(IniFile &iniFile, const std::string &gameID, const char *option, int *value);

	CompatFlags flags_{};
	VRCompat vrCompat_{};
	std::set<std::string> ignored_;
	std::string activeList_;
};
