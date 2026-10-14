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

#include "ppsspp_config.h"
#include <algorithm>

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceUsbMic.h"
#include "Core/CoreTiming.h"
#include "Core/MemMapHelpers.h"

int eventMicBlockingResume = -1;

static QueueBuf *audioBuf = nullptr;
static u32 numNeedSamples;
static std::vector<MicWaitInfo> waitingThreads;
static bool isNeedInput;  // Unused, kept for savestates.
static u32 curSampleRate;
static u32 curChannels;  // Always 1, kept for savestates.
static u32 readMicDataLength;
static u32 curTargetAddr;
static int micState; // 0 means stopped, 1 means started, for save state.
static bool micPolling = false;
static constexpr int MIC_POLL_INTERVAL_US = 5000;

static void PollMicrophone() {
	if (micPolling) {
		System_MicrophoneCommand("pollRecording");
	}
}

static void __MicBlockingResume(u64 userdata, int cyclesLate) {
	PollMicrophone();
	// Thread ID zero is reserved for polling the host capture stream.
	if (userdata == 0) {
		if (micPolling) {
			CoreTiming::ScheduleEvent(usToCycles(MIC_POLL_INTERVAL_US), eventMicBlockingResume, 0);
		}
		return;
	}
	SceUID threadID = (SceUID)userdata;
	u32 error;
	// On each path, we must either erase-iter-idiom, or increment iter
	for (auto iter = waitingThreads.begin(); iter != waitingThreads.end();) {
		if (iter->threadID != threadID) {
			iter++;
			continue;
		}

		SceUID waitID = __KernelGetWaitID(threadID, WAITTYPE_MICINPUT, error);
		if (waitID == 0) {
			iter++;
			continue;
		}

		if (Microphone::isHaveDevice()) {
			// Hosts deliver audio in chunks. Allow 100 ms of capture jitter before padding.
			const u64 retries = userdata >> 32;
			if (micPolling && Microphone::getReadMicDataLength() < (u32)iter->needSize && retries < 20) {
				CoreTiming::ScheduleEvent(usToCycles(MIC_POLL_INTERVAL_US), eventMicBlockingResume, (u32)threadID | ((retries + 1) << 32));
				iter++;
				continue;
			}
			// A stalled host microphone must not hang the game (as happened in Go!Edit).
			// Fill any samples still missing with silence.
			const u32 needSize = (u32)iter->needSize;
			const u32 have = std::min((u32)Microphone::getReadMicDataLength(), needSize);
			if (have < needSize) {
				DEBUG_LOG(Log::HLE, "sceUsbMic: host mic only delivered %d of %d bytes, padding with silence", have, needSize);
				if (Memory::IsValidRange(iter->addr + have, needSize - have)) {
					Memory::Memset(iter->addr + have, 0, needSize - have, "MicSilence");
				}
				readMicDataLength = needSize;
			}
			u32 ret = __KernelGetWaitValue(threadID, error);
			DEBUG_LOG(Log::HLE, "sceUsbMic: Waking up thread(%d)", (int)iter->threadID);
			__KernelResumeThreadFromWait(threadID, ret);
			iter = waitingThreads.erase(iter);
		} else {
			for (int i = 0; i < iter->needSize; i++) {
				if (Memory::IsValidAddress(iter->addr + i)) {
					Memory::WriteUnchecked_U8(i & 0xFF, iter->addr + i);
				}
			}
			u32 ret = __KernelGetWaitValue(threadID, error);
			DEBUG_LOG(Log::HLE, "sceUsbMic: Waking up thread(%d)", (int)iter->threadID);
			__KernelResumeThreadFromWait(threadID, ret);
			readMicDataLength += iter->needSize;
			iter = waitingThreads.erase(iter);
		}
	}
}

void __UsbMicInit() {
	if (micPolling) {
		Microphone::stopMic();
	}
	if (audioBuf) {
		delete audioBuf;
		audioBuf = nullptr;
	}
	numNeedSamples = 0;
	waitingThreads.clear();
	isNeedInput = true;
	curSampleRate = 44100;
	curChannels = 1;
	curTargetAddr = 0;
	readMicDataLength = 0;
	micState = 0;
	eventMicBlockingResume = CoreTiming::RegisterEvent("MicBlockingResume", &__MicBlockingResume);
}

void __UsbMicShutdown() {
	Microphone::stopMic();
	if (audioBuf) {
		delete audioBuf;
		audioBuf = nullptr;
	}
}

void __UsbMicDoState(PointerWrap &p) {
	auto s = p.Section("sceUsbMic", 0, 3);
	if (!s) {
		// Still need to restore the event (unless this is a save that failed earlier.)
		if (p.mode == p.MODE_READ) {
			eventMicBlockingResume = -1;
			CoreTiming::RestoreRegisterEvent(eventMicBlockingResume, "MicBlockingResume", &__MicBlockingResume);
			waitingThreads.clear();
			// Nor was the mic, so leave it off.
			if (Microphone::isMicStarted()) {
				Microphone::stopMic();
			}
			numNeedSamples = 0;
			curTargetAddr = 0;
			readMicDataLength = 0;
			micState = 0;
		}
		return;
	}
	bool isMicStartedNow = Microphone::isMicStarted();
	Do(p, numNeedSamples);
	Do(p, waitingThreads);
	Do(p, isNeedInput);
	Do(p, curSampleRate);
	Do(p, curChannels);
	Do(p, micState);
	if (s > 1) {
		Do(p, eventMicBlockingResume);
	} else {
		eventMicBlockingResume = -1;
	}
	CoreTiming::RestoreRegisterEvent(eventMicBlockingResume, "MicBlockingResume", &__MicBlockingResume);

	if (s > 2) {
		Do(p, curTargetAddr);
		Do(p, readMicDataLength);
	} else if (p.mode == p.MODE_READ) {
		// The host mic thread writes to curTargetAddr, so don't leave the one from before the load.
		curTargetAddr = 0;
		readMicDataLength = 0;
	}
	if (!audioBuf && numNeedSamples > 0) {
		audioBuf = new QueueBuf(numNeedSamples << 1);
	}

	if (micState == 0) {
		if (isMicStartedNow)
			Microphone::stopMic();
	} else if (micState == 1) {
		if (isMicStartedNow) {
			// Ok, started.
		} else {
			Microphone::startMic();
		}
	}
	if (p.mode == p.MODE_READ && micPolling) {
		System_MicrophoneCommand("startRecording:" + std::to_string(curSampleRate));
		CoreTiming::UnscheduleEvent(eventMicBlockingResume, 0);
		CoreTiming::ScheduleEvent(usToCycles(MIC_POLL_INTERVAL_US), eventMicBlockingResume, 0);
	}
}

QueueBuf::QueueBuf(int size) : available(0), end(0), capacity(size) {
	buf_ = new u8[size];
}

QueueBuf::~QueueBuf() {
	delete[] buf_;
}

int QueueBuf::push(const u8 *buf, int size) {
	int addedSize = 0;
	// This will overwrite the old data if the size prepare to add more than remaining size.
	if (size > capacity)
		resize(size);
	while (end + size > capacity) {
		memcpy(buf_ + end, buf + addedSize, capacity - end);
		addedSize += capacity - end;
		size -= capacity - end;
		end = 0;
	}
	memcpy(buf_ + end, buf + addedSize, size);
	addedSize += size;
	end = (end + size) % capacity;
	available = std::min(capacity, available + addedSize);
	return addedSize;
}

int QueueBuf::pop(u8 *buf, int size) {
	if (size == 0) {
		return 0;
	}
	int ret = 0;
	if (getAvailableSize() < size)
		size = getAvailableSize();
	ret = size;

	int startPos = getStartPos();
	if (startPos + size <= capacity) {
		memcpy(buf, buf_ + startPos, size);
	} else {
		memcpy(buf, buf_ + startPos, capacity - startPos);
		memcpy(buf + capacity - startPos, buf_, size - (capacity - startPos));
	}
	available -= size;
	return ret;
}

void QueueBuf::resize(int newSize) {
	if (capacity >= newSize) {
		return;
	}
	int availableSize = getAvailableSize();
	u8 *newBuf = new u8[newSize];
	// Unwraps what's buffered to the start of the new buffer.
	pop(newBuf, availableSize);
	delete[] buf_;
	buf_ = newBuf;
	available = availableSize;
	end = availableSize;
	capacity = newSize;
}

void QueueBuf::flush() {
	available = 0;
	end = 0;
}

int QueueBuf::getRemainingSize() const {
	return capacity - getAvailableSize();
}

int QueueBuf::getStartPos() const {
	return end >= available ? end - available : capacity - available + end;
}

static int sceUsbMicPollInputEnd() {
	ERROR_LOG(Log::HLE, "UNIMPL sceUsbMicPollInputEnd");
	return 0;
}

static int sceUsbMicInputBlocking(u32 maxSamples, u32 sampleRate, u32 bufAddr) {
	if (!Memory::IsValidAddress(bufAddr)) {
		ERROR_LOG(Log::HLE, "sceUsbMicInputBlocking(%d, %d, %08x): invalid addresses", maxSamples, sampleRate, bufAddr);
		return -1;
	}

	INFO_LOG(Log::HLE, "sceUsbMicInputBlocking: maxSamples: %d, samplerate: %d, bufAddr: %08x", maxSamples, sampleRate, bufAddr);
	if (maxSamples <= 0 || (maxSamples & 0x3F) != 0) {
		return SCE_ERROR_USBMIC_INVALID_MAX_SAMPLES;
	}

	if (sampleRate != 44100 && sampleRate != 22050 && sampleRate != 11025) {
		return SCE_ERROR_USBMIC_INVALID_SAMPLERATE;
	}

	return __MicInput(maxSamples, sampleRate, bufAddr, USBMIC);
}

static int sceUsbMicInputInitEx(u32 paramAddr) {
	ERROR_LOG(Log::HLE, "UNIMPL sceUsbMicInputInitEx: %08x", paramAddr);
	return 0;
}

static int sceUsbMicInput(u32 maxSamples, u32 sampleRate, u32 bufAddr) {
	if (!Memory::IsValidAddress(bufAddr)) {
		ERROR_LOG(Log::HLE, "sceUsbMicInput(%d, %d, %08x): invalid addresses", maxSamples, sampleRate, bufAddr);
		return -1;
	}

	WARN_LOG(Log::HLE, "UNTEST sceUsbMicInput: maxSamples: %d, samplerate: %d, bufAddr: %08x", maxSamples, sampleRate, bufAddr);
	if (maxSamples <= 0 || (maxSamples & 0x3F) != 0) {
		return SCE_ERROR_USBMIC_INVALID_MAX_SAMPLES;
	}

	if (sampleRate != 44100 && sampleRate != 22050 && sampleRate != 11025) {
		return SCE_ERROR_USBMIC_INVALID_SAMPLERATE;
	}

	return __MicInput(maxSamples, sampleRate, bufAddr, USBMIC, false);
}

static int sceUsbMicGetInputLength() {
	int ret = Microphone::getReadMicDataLength() / 2;
	ERROR_LOG(Log::HLE, "UNTEST sceUsbMicGetInputLength(ret: %d)", ret);
	return ret;
}

static int sceUsbMicInputInit(int unknown1, int inputVolume, int unknown2) {
	ERROR_LOG(Log::HLE, "UNIMPL sceUsbMicInputInit(unknown1: %d, inputVolume: %d, unknown2: %d)", unknown1, inputVolume, unknown2);
	return 0;
}

static int sceUsbMicWaitInputEnd() {
	WARN_LOG(Log::HLE, "UNIMPL sceUsbMicWaitInputEnd");
	// Hack: Just task switch so other threads get to do work. Helps Beaterator (although recording does not appear to work correctly).
	return hleDelayResult(0, "MicWait", 100);
}

int Microphone::startMic() {
	// The rate is curSampleRate, which every caller has set.
	INFO_LOG(Log::HLE, "microphone_command : sr = %d", curSampleRate);
	System_MicrophoneCommand("startRecording:" + std::to_string(curSampleRate));
	micPolling = System_GetPropertyBool(SYSPROP_MICROPHONE_NEEDS_POLLING);
	if (micPolling) {
		CoreTiming::UnscheduleEvent(eventMicBlockingResume, 0);
		CoreTiming::ScheduleEvent(usToCycles(MIC_POLL_INTERVAL_US), eventMicBlockingResume, 0);
	}
	micState = 1;
	return 0;
}

int Microphone::stopMic() {
	if (micPolling) {
		micPolling = false;
		CoreTiming::UnscheduleEvent(eventMicBlockingResume, 0);
	}
	System_MicrophoneCommand("stopRecording");
	micState = 0;
	return 0;
}

bool Microphone::isHaveDevice() {
#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(IOS)
	return System_AudioRecordingIsAvailable();
#else
	return micPolling || !Microphone::getDeviceList().empty();
#endif
}

bool Microphone::isMicStarted() {
	return micState == 1;
}

int Microphone::numNeedSamples() {
	return ::numNeedSamples;
}

int Microphone::availableAudioBufSize() {
	return audioBuf->getAvailableSize();
}

int Microphone::getReadMicDataLength() {
	return ::readMicDataLength;
}

int Microphone::addAudioData(u8 *buf, int size) {
	if (!audioBuf)
		return 0;
	audioBuf->push(buf, size);

	int addSize = std::min(audioBuf->getAvailableSize(), numNeedSamples() * 2 - getReadMicDataLength());
	if (Memory::IsValidRange(curTargetAddr + readMicDataLength, addSize)) {
		getAudioData(Memory::GetPointerWriteUnchecked(curTargetAddr + readMicDataLength), addSize);
		NotifyMemInfo(MemBlockFlags::WRITE, curTargetAddr + readMicDataLength, addSize, "MicAddAudioData");
	}
	readMicDataLength += addSize;

	return size;
}

int Microphone::getAudioData(u8 *buf, int size) {
	if(audioBuf)
		return audioBuf->pop(buf, size);
	return 0;
}

void Microphone::flushAudioData() {
	if (audioBuf) {
		audioBuf->flush();
	}
}

std::vector<std::string> Microphone::getDeviceList() {
	return System_GetPropertyStringVec(SYSPROP_MICROPHONE_DEVICE_LIST);
}

void Microphone::onMicDeviceChange() {
	if (micPolling) {
		// The host reopens its capture device.
		System_MicrophoneCommand("deviceChanged");
	}
}

u32 __MicInput(u32 maxSamples, u32 sampleRate, u32 bufAddr, MICTYPE type, bool block) {
	if (micPolling && curSampleRate != sampleRate) {
		Microphone::stopMic();
	}
	curSampleRate = sampleRate;
	curChannels = 1;
	curTargetAddr = bufAddr;
	int size = maxSamples << 1;
	if (!audioBuf) {
		audioBuf = new QueueBuf(size);
	} else {
		audioBuf->resize(size);
	}

	numNeedSamples = maxSamples;
	readMicDataLength = 0;
	if (!Microphone::isMicStarted()) {
		Microphone::startMic();
	}

	if (Microphone::availableAudioBufSize() > 0) {
		u32 addSize = std::min(Microphone::availableAudioBufSize(), size);
		if (Memory::IsValidRange(curTargetAddr, addSize)) {
			Microphone::getAudioData(Memory::GetPointerWriteUnchecked(curTargetAddr), addSize);
			NotifyMemInfo(MemBlockFlags::WRITE, curTargetAddr, addSize, "MicInput");
		}
		readMicDataLength += addSize;
	}
	PollMicrophone();

	if (!block) {
		return type == CAMERAMIC ? size : maxSamples;
	}

	u64 waitTimeus = (size - std::min(readMicDataLength, (u32)size)) * 1000000ULL / 2 / sampleRate;
	CoreTiming::ScheduleEvent(usToCycles(waitTimeus), eventMicBlockingResume, __KernelGetCurThread());
	MicWaitInfo waitInfo = { __KernelGetCurThread(), bufAddr, size, sampleRate };
	waitingThreads.push_back(waitInfo);
	DEBUG_LOG(Log::HLE, "MicInputBlocking: blocking thread(%d)", (int)__KernelGetCurThread());
	__KernelWaitCurThread(WAITTYPE_MICINPUT, 1, size, 0, false, "blocking microphone");

	return type == CAMERAMIC ? size : maxSamples;
}

const HLEFunction sceUsbMic[] = {
	{0x06128E42, &WrapI_V<sceUsbMicPollInputEnd>,    "sceUsbMicPollInputEnd",         'i', ""    },
	{0x2E6DCDCD, &WrapI_UUU<sceUsbMicInputBlocking>, "sceUsbMicInputBlocking",        'i', "xxx" },
	{0x45310F07, &WrapI_U<sceUsbMicInputInitEx>,     "sceUsbMicInputInitEx",          'i', "x"   },
	{0x5F7F368D, &WrapI_UUU<sceUsbMicInput>,         "sceUsbMicInput",                'i', "xxx" },
	{0x63400E20, &WrapI_V<sceUsbMicGetInputLength>,  "sceUsbMicGetInputLength",       'i', ""    },
	{0xB8E536EB, &WrapI_III<sceUsbMicInputInit>,     "sceUsbMicInputInit",            'i', "iii" },
	{0xF899001C, &WrapI_V<sceUsbMicWaitInputEnd>,    "sceUsbMicWaitInputEnd",         'i', ""    },
};

void Register_sceUsbMic() {
	RegisterHLEModule("sceUsbMic", ARRAY_SIZE(sceUsbMic), sceUsbMic);
}
