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

#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include "Core/HLE/FunctionWrappers.h"

void Register_sceUsbCam();

void __UsbCamInit();
void __UsbCamDoState(PointerWrap &p);
void __UsbCamShutdown();

typedef struct {
	int size;
	u32 unk;
	int gain;
	u32 unk2;
	int frequency;
} PspUsbCamSetupMicParam;

typedef struct {
	int size;
	int resolution;
	int jpegsize;
	int reverseflags;
	int delay;
	int complevel;
} PspUsbCamSetupStillParam;

typedef struct {
	int size;
	u32 unk;
	int resolution;
	int jpegsize;
	int complevel;
	u32 unk2;
	u32 unk3;
	int flip;
	int mirror;
	int delay;
	u32 unk4[5];
} PspUsbCamSetupStillExParam;

typedef struct {
	int size;
	int resolution;
	int framerate;
	int wb;
	int saturation;
	int brightness;
	int contrast;
	int sharpness;
	int effectmode;
	int framesize;
	u32 unk;
	int evlevel;
} PspUsbCamSetupVideoParam;

typedef struct {
	int size;
	u32 unk;
	int resolution;
	int framerate;
	u32 unk2;
	u32 unk3;
	int wb;
	int saturation;
	int brightness;
	int contrast;
	int sharpness;
	u32 unk4;
	u32 unk5;
	u32 unk6[3];
	int effectmode;
	u32 unk7;
	u32 unk8;
	u32 unk9;
	u32 unk10;
	u32 unk11;
	int framesize;
	u32 unk12;
	int evlevel;
} PspUsbCamSetupVideoExParam;

namespace Camera {
	enum Mode       { Unused, Still, Video };
	enum ConfigType { CfNone, CfStill, CfStillEx, CfVideo, CfVideoEx };

	typedef struct {
		Mode mode;
		ConfigType type;
		PspUsbCamSetupMicParam         micParam;
		union {
			PspUsbCamSetupStillParam   stillParam;
			PspUsbCamSetupStillExParam stillExParam;
			PspUsbCamSetupVideoParam   videoParam;
			PspUsbCamSetupVideoExParam videoExParam;
		};
	} Config;

	std::vector<std::string> getDeviceList();
	void onCameraDeviceChange();
	int startCapture();
	int stopCapture();
	void pushCameraImage(long long length, unsigned char *image);
}
