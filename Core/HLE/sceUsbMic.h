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
	int needSize;
	u32 sampleRate;
};

class QueueBuf {
public:
	QueueBuf(int size);
	~QueueBuf();

	QueueBuf(const QueueBuf &buf) = delete;
	QueueBuf& operator=(const QueueBuf &buf) = delete;

	int push(const u8 *buf, int size);
	int pop(u8 *buf, int size);
	void resize(int newSize);
	void flush();
	int getAvailableSize() const {
		return available;
	}
	int getRemainingSize() const;
	int getStartPos() const;
	int getCapacity() const {
		return capacity;
	}

private:
	int available = 0;
	int end = 0;
	int capacity;  // set in constructor
	u8 *buf_;
	// TODO: Turn back to a regular mutex, will take some refactoring.
	std::recursive_mutex mutex;
};

namespace Microphone {
	int startMic(void *param);
	int stopMic();
	bool isHaveDevice();
	bool isMicStarted();
	int numNeedSamples();
	int availableAudioBufSize();
	int getReadMicDataLength();

	int addAudioData(u8 *buf, int size);
	int getAudioData(u8 *buf, int size);
	void flushAudioData();

	std::vector<std::string> getDeviceList();
	void onMicDeviceChange();

	// Deprecated.
	bool isNeedInput();
}

u32 __MicInput(u32 maxSamples, u32 sampleRate, u32 bufAddr, MICTYPE type, bool block = true);
