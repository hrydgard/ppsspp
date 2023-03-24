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

#include "ppsspp_config.h"

#include <algorithm>

#include "Common/File/DirListing.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/Input/InputState.h"

#include "Common/StringUtils.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/CoreParameter.h"
#include "Core/System.h"
#include "Core/Debugger/SymbolMap.h"

#include "UI/OnScreenDisplay.h"

#include "Windows/XinputDevice.h"

#include "UWP/XAudioSoundStream.h"
#include "UWP/UWPHost.h"

UWPHost::UWPHost() {
}

UWPHost::~UWPHost() {

}

void UWPHost::NotifyUserMessage(const std::string &message, float duration, u32 color, const char *id) {
	osm.Show(message, duration, color, -1, true, id);
}
