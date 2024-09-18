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
#include <mutex>

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceUsbMic.h"
#include "Core/CoreTiming.h"
#include "Core/MemMapHelpers.h"

#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP) && !defined(__LIBRETRO__)
#define HAVE_WIN32_MICROPHONE
#endif

#ifdef HAVE_WIN32_MICROPHONE
#include "Common/CommonWindows.h"
#include "Windows/CaptureDevice.h"
#endif

enum {
	SCE_USBMIC_ERROR_INVALID_MAX_SAMPLES = 0x80243806,
	SCE_USBMIC_ERROR_INVALID_SAMPLERATE  = 0x8024380A,
};

int eventMicBlockingResume = -1;

static QueueBuf *audioBuf = nullptr;
static u32 numNeedSamples;
static std::vector<MicWaitInfo> waitingThreads;
static bool isNeedInput;
static u32 curSampleRate;
static u32 curChannels;
static u32 readMicDataLength;
static u32 curTargetAddr;
static int micState; // 0 means stopped, 1 means started, for save state.

static void __MicBlockingResume(u64 userdata, int cyclesLate) {
	SceUID threadID = (SceUID)userdata;
	u32 error;
	int count = 0;
	for (auto waitingThread : waitingThreads) {
		if (waitingThread.threadID == threadID) {
			SceUID waitID = __KernelGetWaitID(threadID, WAITTYPE_MICINPUT, error);
			if (waitID == 0)
				continue;
			if (Microphone::isHaveDevice()) {
				if (Microphone::getReadMicDataLength() >= waitingThread.needSize) {
					u32 ret = __KernelGetWaitValue(threadID, error);
					DEBUG_LOG(Log::HLE, "sceUsbMic: Waking up thread(%d)", (int)waitingThread.threadID);
					__KernelResumeThreadFromWait(threadID, ret);
					waitingThreads.erase(waitingThreads.begin() + count);
				} else {
					u64 waitTimeus = (waitingThread.needSize - Microphone::getReadMicDataLength()) * 1000000 / 2 / waitingThread.sampleRate;
					CoreTiming::ScheduleEvent(usToCycles(waitTimeus), eventMicBlockingResume, userdata);
				}
			} else {
				for (u32 i = 0; i < waitingThread.needSize; i++) {
					if (Memory::IsValidAddress(waitingThread.addr + i)) {
						Memory::Write_U8(i & 0xFF, waitingThread.addr + i);
					}
				}
				u32 ret = __KernelGetWaitValue(threadID, error);
				DEBUG_LOG(Log::HLE, "sceUsbMic: Waking up thread(%d)", (int)waitingThread.threadID);
				__KernelResumeThreadFromWait(threadID, ret);
				waitingThreads.erase(waitingThreads.begin() + count);
				readMicDataLength += waitingThread.needSize;
			}
		}
		++count;
	}
}

void __UsbMicInit() {
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
	if (audioBuf) {
		delete audioBuf;
		audioBuf = nullptr;
	}
	Microphone::stopMic();
}

void __UsbMicDoState(PointerWrap &p) {
	auto s = p.Section("sceUsbMic", 0, 3);
	if (!s) {
		// Still need to restore the event.
		eventMicBlockingResume = -1;
		CoreTiming::RestoreRegisterEvent(eventMicBlockingResume, "MicBlockingResume", &__MicBlockingResume);
		waitingThreads.clear();
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
			Microphone::startMic(new std::vector<u32>({ curSampleRate, curChannels }));
		}
	}
}

QueueBuf::QueueBuf(u32 size) : available(0), end(0), capacity(size) {
	buf_ = new u8[size];
}

QueueBuf::~QueueBuf() {
	delete[] buf_;
}

QueueBuf::QueueBuf(const QueueBuf &buf) {
	buf_ = new u8[buf.capacity];
	memcpy(buf_, buf.buf_, buf.capacity);
	available = buf.available;
	end = buf.end;
	capacity = buf.capacity;
}

QueueBuf& QueueBuf::operator=(const QueueBuf &buf) {
	if (capacity < buf.capacity) {
		resize(buf.capacity);
	}
	std::unique_lock<std::mutex> lock(mutex);
	memcpy(buf_, buf.buf_, buf.capacity);
	available = buf.available;
	end = buf.end;
	lock.unlock();
	return *this;
}

u32 QueueBuf::push(u8 *buf, u32 size) {
	u32 addedSize = 0;
	if (size > capacity)
		resize(size);
	// This will overwrite the old data if the size prepare to add more than remaining size.
	std::unique_lock<std::mutex> lock(mutex);
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
	lock.unlock();
	return addedSize;
}

u32 QueueBuf::pop(u8 *buf, u32 size) {
	u32 ret = 0;
	if (getAvailableSize() < size)
		size = getAvailableSize();
	ret = size;

	std::unique_lock<std::mutex> lock(mutex);
	if (getStartPos() + size <= capacity) {
		memcpy(buf, buf_ + getStartPos(), size);
	} else {
		memcpy(buf, buf_ + getStartPos(), capacity - getStartPos());
		memcpy(buf + capacity - getStartPos(), buf_, size - (capacity - getStartPos()));
	}
	available -= size;
	lock.unlock();
	return ret;
}

void QueueBuf::resize(u32 newSize) {
	if (capacity >= newSize) {
		return;
	}
	u32 availableSize = getAvailableSize();
	u8 *oldbuf = buf_;

	buf_ = new u8[newSize];
	pop(buf_, std::min(availableSize, newSize));
	available = availableSize;
	end = availableSize;
	capacity = newSize;
	delete[] oldbuf;
}

void QueueBuf::flush() {
	std::unique_lock<std::mutex> lock(mutex);
	available = 0;
	end = 0;
	lock.unlock();
}

u32 QueueBuf::getAvailableSize() {
	return available;
}

u32 QueueBuf::getRemainingSize() {
	return capacity - getAvailableSize();
}

u32 QueueBuf::getStartPos() {
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
		return SCE_USBMIC_ERROR_INVALID_MAX_SAMPLES;
	}

	if (sampleRate != 44100 && sampleRate != 22050 && sampleRate != 11025) {
		return SCE_USBMIC_ERROR_INVALID_SAMPLERATE;
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

	ERROR_LOG(Log::HLE, "UNTEST sceUsbMicInput: maxSamples: %d, samplerate: %d, bufAddr: %08x", maxSamples, sampleRate, bufAddr);
	if (maxSamples <= 0 || (maxSamples & 0x3F) != 0) {
		return SCE_USBMIC_ERROR_INVALID_MAX_SAMPLES;
	}

	if (sampleRate != 44100 && sampleRate != 22050 && sampleRate != 11025) {
		return SCE_USBMIC_ERROR_INVALID_SAMPLERATE;
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
	ERROR_LOG(Log::HLE, "UNIMPL sceUsbMicWaitInputEnd");
	return 0;
}

int Microphone::startMic(void *param) {
#ifdef HAVE_WIN32_MICROPHONE
	if (winMic)
		winMic->sendMessage({ CAPTUREDEVIDE_COMMAND::START, param });
#elif PPSSPP_PLATFORM(ANDROID)
	std::vector<u32> *micParam = static_cast<std::vector<u32>*>(param);
	int sampleRate = micParam->at(0);
	int channels = micParam->at(1);
	INFO_LOG(Log::HLE, "microphone_command : sr = %d", sampleRate);
	System_MicrophoneCommand("startRecording:" + std::to_string(sampleRate));
#endif
	micState = 1;
	return 0;
}

int Microphone::stopMic() {
#ifdef HAVE_WIN32_MICROPHONE
	if (winMic)
		winMic->sendMessage({ CAPTUREDEVIDE_COMMAND::STOP, nullptr });
#elif PPSSPP_PLATFORM(ANDROID)
	System_MicrophoneCommand("stopRecording");
#endif
	micState = 0;
	return 0;
}

bool Microphone::isHaveDevice() {
#ifdef HAVE_WIN32_MICROPHONE
	return winMic->getDeviceCounts() >= 1;
#elif PPSSPP_PLATFORM(ANDROID)
	return System_AudioRecordingIsAvailable();
#endif
	return false;
}

bool Microphone::isMicStarted() {
	return micState == 1;
}

// Deprecated.
bool Microphone::isNeedInput() {
	return ::isNeedInput;
}

u32 Microphone::numNeedSamples() {
	return ::numNeedSamples;
}

u32 Microphone::availableAudioBufSize() {
	return audioBuf->getAvailableSize();
}

u32 Microphone::getReadMicDataLength() {
	return ::readMicDataLength;
}

int Microphone::addAudioData(u8 *buf, u32 size) {
	if (!audioBuf)
		return 0;
	audioBuf->push(buf, size);

	u32 addSize = std::min(audioBuf->getAvailableSize(), numNeedSamples() * 2 - getReadMicDataLength());
	if (Memory::IsValidRange(curTargetAddr + readMicDataLength, addSize)) {
		getAudioData(Memory::GetPointerWriteUnchecked(curTargetAddr + readMicDataLength), addSize);
		NotifyMemInfo(MemBlockFlags::WRITE, curTargetAddr + readMicDataLength, addSize, "MicAddAudioData");
	}
	readMicDataLength += addSize;

	return size;
}

u32 Microphone::getAudioData(u8 *buf, u32 size) {
	if(audioBuf)
		return audioBuf->pop(buf, size);

	return 0;
}

void Microphone::flushAudioData() {
	audioBuf->flush();
}

std::vector<std::string> Microphone::getDeviceList() {
#ifdef HAVE_WIN32_MICROPHONE
	if (winMic) {
		return winMic->getDeviceList();
	}
#endif
	return std::vector<std::string>();
}

void Microphone::onMicDeviceChange() {
	if (Microphone::isMicStarted()) {
		Microphone::stopMic();
		// Just use the last param.
		Microphone::startMic(nullptr);
	}
}

u32 __MicInput(u32 maxSamples, u32 sampleRate, u32 bufAddr, MICTYPE type, bool block) {
	curSampleRate = sampleRate;
	curChannels = 1;
	curTargetAddr = bufAddr;
	u32 size = maxSamples << 1;
	if (!audioBuf) {
		audioBuf = new QueueBuf(size);
	} else {
		audioBuf->resize(size);
	}
	if (!audioBuf)
		return 0;

	numNeedSamples = maxSamples;
	readMicDataLength = 0;
	if (!Microphone::isMicStarted()) {
		std::vector<u32> *param = new std::vector<u32>({ sampleRate, 1 });
		Microphone::startMic(param);
	}

	if (Microphone::availableAudioBufSize() > 0) {
		u32 addSize = std::min(Microphone::availableAudioBufSize(), size);
		if (Memory::IsValidRange(curTargetAddr, addSize)) {
			Microphone::getAudioData(Memory::GetPointerWriteUnchecked(curTargetAddr), addSize);
			NotifyMemInfo(MemBlockFlags::WRITE, curTargetAddr, addSize, "MicInput");
		}
		readMicDataLength += addSize;
	}

	if (!block) {
		return type == CAMERAMIC ? size : maxSamples;
	}

	u64 waitTimeus = (size - Microphone::availableAudioBufSize()) * 1000000 / 2 / sampleRate;
	CoreTiming::ScheduleEvent(usToCycles(waitTimeus), eventMicBlockingResume, __KernelGetCurThread());
	MicWaitInfo waitInfo = { __KernelGetCurThread(), bufAddr, size, sampleRate };
	waitingThreads.push_back(waitInfo);
	DEBUG_LOG(Log::HLE, "MicInputBlocking: blocking thread(%d)", (int)__KernelGetCurThread());
	__KernelWaitCurThread(WAITTYPE_MICINPUT, 1, size, 0, false, "blocking microphone");

	return type == CAMERAMIC ? size : maxSamples;
}

const HLEFunction sceUsbMic[] =
{
	{0x06128E42, &WrapI_V<sceUsbMicPollInputEnd>,    "sceUsbMicPollInputEnd",         'i', ""    },
	{0x2E6DCDCD, &WrapI_UUU<sceUsbMicInputBlocking>, "sceUsbMicInputBlocking",        'i', "xxx" },
	{0x45310F07, &WrapI_U<sceUsbMicInputInitEx>,     "sceUsbMicInputInitEx",          'i', "x"   },
	{0x5F7F368D, &WrapI_UUU<sceUsbMicInput>,         "sceUsbMicInput",                'i', "xxx" },
	{0x63400E20, &WrapI_V<sceUsbMicGetInputLength>,  "sceUsbMicGetInputLength",       'i', ""    },
	{0xB8E536EB, &WrapI_III<sceUsbMicInputInit>,     "sceUsbMicInputInit",            'i', "iii" },
	{0xF899001C, &WrapI_V<sceUsbMicWaitInputEnd>,    "sceUsbMicWaitInputEnd",         'i', ""    },
};

void Register_sceUsbMic()
{
	RegisterModule("sceUsbMic", ARRAY_SIZE(sceUsbMic), sceUsbMic);
}
