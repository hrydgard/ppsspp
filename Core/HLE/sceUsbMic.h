// Copyright (c) 2019- PPSSPP Project.

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

#include "sceKernel.h"
#include <mutex>

void Register_sceUsbMic();

void __UsbMicInit();
void __UsbMicShutdown();
void __UsbMicDoState(PointerWrap &p);

enum MICTYPE {
	AUDIOINPUT,
	USBMIC,
	CAMERAMIC
};

struct MicWaitInfo {
	SceUID threadID;
	u32 addr;
	u32 needSize;
	u32 sampleRate;
};

class QueueBuf {
public:
	QueueBuf(u32 size);
	~QueueBuf();

	QueueBuf(const QueueBuf &buf);
	QueueBuf& operator=(const QueueBuf &buf);

	u32 push(u8 *buf, u32 size);
	u32 pop(u8 *buf, u32 size);
	void resize(u32 newSize);
	void flush();
	u32 getAvailableSize();
	u32 getRemainingSize();
	u32 getStartPos();
	u32 getCapacity() const {
		return capacity;
	}

private:
	u32 available;
	u32 end;
	u32 capacity;
	u8 *buf_;
	std::mutex mutex;
};

namespace Microphone {
	int startMic(void *param);
	int stopMic();
	bool isHaveDevice();
	bool isMicStarted();
	u32 numNeedSamples();
	u32 availableAudioBufSize();
	u32 getReadMicDataLength();


	int addAudioData(u8 *buf, u32 size);
	u32 getAudioData(u8 *buf, u32 size);
	void flushAudioData();

	std::vector<std::string> getDeviceList();
	void onMicDeviceChange();

	// Deprecated.
	bool isNeedInput();
}

u32 __MicInput(u32 maxSamples, u32 sampleRate, u32 bufAddr, MICTYPE type, bool block = true);
