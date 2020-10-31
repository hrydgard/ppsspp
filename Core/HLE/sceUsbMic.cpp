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

#include <mutex>

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/System/System.h"
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
#include "Windows/CaptureDevice.h"
#endif

enum {
	SCE_USBMIC_ERROR_INVALID_MAX_SAMPLES = 0x80243806,
	SCE_USBMIC_ERROR_INVALID_SAMPLERATE  = 0x8024380A,
};

int eventUsbMicAudioUpdate = -1;

QueueBuf *audioBuf = nullptr;
u32 numNeedSamples;
static std::vector<MicWaitInfo> waitingThreads;
std::mutex wtMutex;
bool isNeedInput;
u32 curSampleRate;
u32 curChannels;
int micState; // 0 means stopped, 1 means started, for save state.

static void __UsbMicAudioUpdate(u64 userdata, int cyclesLate) {
	SceUID threadID = (SceUID)userdata;
	u32 error;
	int count = 0;
	std::unique_lock<std::mutex> lock(wtMutex);
	for (auto waitingThread : waitingThreads) {
		if (waitingThread.threadID == threadID) {
			SceUID waitID = __KernelGetWaitID(threadID, WAITTYPE_MICINPUT, error);
			if (waitID == 0)
				continue;
			if (Microphone::isHaveDevice()) {
				if (Microphone::availableAudioBufSize() >= waitingThread.needSize) {
					u8 *tempbuf8 = new u8[waitingThread.needSize];
					Microphone::getAudioData(tempbuf8, waitingThread.needSize);
					Memory::Memcpy(waitingThread.addr, tempbuf8, waitingThread.needSize);
					delete[] tempbuf8;
					u32 ret = __KernelGetWaitValue(threadID, error);
					DEBUG_LOG(HLE, "sceUsbMic: Waking up thread(%d)", (int)waitingThread.threadID);
					__KernelResumeThreadFromWait(threadID, ret);
					waitingThreads.erase(waitingThreads.begin() + count);
					if (waitingThreads.size() == 0)
						isNeedInput = false;
				} else {
					u64 waitTimeus = (waitingThread.needSize - Microphone::availableAudioBufSize()) * 1000000 / 2 / waitingThread.sampleRate;
					if(eventUsbMicAudioUpdate == -1)
						eventUsbMicAudioUpdate = CoreTiming::RegisterEvent("UsbMicAudioUpdate", &__UsbMicAudioUpdate);
					CoreTiming::ScheduleEvent(usToCycles(waitTimeus), eventUsbMicAudioUpdate, userdata);
				}
			} else {
				for (u32 i = 0; i < waitingThread.needSize; i++) {
					if (Memory::IsValidAddress(waitingThread.addr + i)) {
						Memory::Write_U8(i & 0xFF, waitingThread.addr + i);
					}
				}
				u32 ret = __KernelGetWaitValue(threadID, error);
				DEBUG_LOG(HLE, "sceUsbMic: Waking up thread(%d)", (int)waitingThread.threadID);
				__KernelResumeThreadFromWait(threadID, ret);
				waitingThreads.erase(waitingThreads.begin() + count);
				if (waitingThreads.size() == 0)
					isNeedInput = false;
			}
		}
		++count;
	}
	lock.unlock();
}

void __UsbMicInit() {
	if (audioBuf) {
		delete audioBuf;
		audioBuf = nullptr;
	}
	numNeedSamples = 0;
	waitingThreads.clear();
	isNeedInput = false;
	curSampleRate = 44100;
	curChannels = 1;
	micState = 0; 
	eventUsbMicAudioUpdate = CoreTiming::RegisterEvent("UsbMicAudioUpdate", &__UsbMicAudioUpdate);
}

void __UsbMicShutdown() {
	if (audioBuf) {
		delete audioBuf;
		audioBuf = nullptr;
	}
	Microphone::stopMic();
}

void __UsbMicDoState(PointerWrap &p) {
	auto s = p.Section("sceUsbMic", 0, 2);
	if (!s) {
		return;
	}
	Do(p, numNeedSamples);
	Do(p, waitingThreads);
	Do(p, isNeedInput);
	Do(p, curSampleRate);
	Do(p, curChannels);
	Do(p, micState);
	if (s > 1) {
		Do(p, eventUsbMicAudioUpdate);
		CoreTiming::RestoreRegisterEvent(eventUsbMicAudioUpdate, "UsbMicAudioUpdate", &__UsbMicAudioUpdate);
	} else {
		eventUsbMicAudioUpdate = -1;
	}

	if (!audioBuf && numNeedSamples > 0) {
		audioBuf = new QueueBuf(numNeedSamples << 1);
	}

	if (micState == 0) {
		if (Microphone::isMicStarted())
			Microphone::stopMic();
	} else if (micState == 1) {
		if (Microphone::isMicStarted()) {
			// Ok, started.
		} else {
			Microphone::startMic(new std::vector<u32>({ curSampleRate, curChannels }));
		}
	}
}

QueueBuf::QueueBuf(u32 size) : start(0), end(0), capacity(size) {
	buf_ = new u8[size];
}

QueueBuf::~QueueBuf() {
	delete[] buf_;
}

QueueBuf::QueueBuf(const QueueBuf &buf) {
	buf_ = new u8[buf.capacity];
	memcpy(buf_, buf.buf_, buf.capacity);
	start = buf.start;
	end = buf.end;
	capacity = buf.capacity;
}

QueueBuf& QueueBuf::operator=(const QueueBuf &buf) {
	if (capacity < buf.capacity) {
		resize(buf.capacity);
	}
	std::unique_lock<std::mutex> lock(mutex);
	memcpy(buf_, buf.buf_, buf.capacity);
	start = buf.start;
	end = buf.end;
	lock.unlock();
	return *this;
}

void QueueBuf::push(u8 *buf, u32 size) {
	if (getRemainingSize() < size) {
		resize((capacity + size - getRemainingSize()) * 3 / 2);
	}
	std::unique_lock<std::mutex> lock(mutex);
	if (end + size <= capacity) {
		memcpy(buf_ + end, buf, size);
		end += size;
	} else {
		memcpy(buf_ + end, buf, capacity - end);
		size -= capacity - end;
		memcpy(buf_, buf + capacity - end, size);
		end = size;
	}
	lock.unlock();
}

u32 QueueBuf::pop(u8 *buf, u32 size) {
	u32 ret = 0;
	if (getAvailableSize() < size)
		size = getAvailableSize();
	ret = size;

	std::unique_lock<std::mutex> lock(mutex);
	if (start + size <= capacity) {
		memcpy(buf, buf_ + start, size);
		start += size;
	} else {
		memcpy(buf, buf_ + start, capacity - start);
		size -= capacity - start;
		memcpy(buf + capacity - start, buf_, size);
		start = size;
	}
	lock.unlock();
	return ret;
}

void QueueBuf::resize(u32 newSize) {
	if (capacity >= newSize) {
		return;
	}
	u32 availableSize = getAvailableSize();
	u8 *oldbuf = buf_;

	std::unique_lock<std::mutex> lock(mutex);
	buf_ = new u8[newSize];
	if (end >= start) {
		memcpy(buf_, oldbuf + start, availableSize);
	} else {
		memcpy(buf_, oldbuf + start, capacity - start);
		memcpy(buf_ + capacity - start, oldbuf, availableSize - (capacity - start));
	}
	start = 0;
	end = availableSize;
	capacity = newSize;
	delete[] oldbuf;
	lock.unlock();
}

void QueueBuf::flush() {
	std::unique_lock<std::mutex> lock(mutex);
	start = 0;
	end = 0;
	lock.unlock();
}

u32 QueueBuf::getAvailableSize() {
	u32 availableSize = 0;
	if (end >= start) {
		availableSize = end - start;
	} else {
		availableSize = end + capacity - start;
	}
	return availableSize;
}

u32 QueueBuf::getRemainingSize() {
	return capacity - getAvailableSize();
}

static int sceUsbMicPollInputEnd() {
	ERROR_LOG(HLE, "UNIMPL sceUsbMicPollInputEnd");
	return 0;
}

static int sceUsbMicInputBlocking(u32 maxSamples, u32 sampleRate, u32 bufAddr) {
	INFO_LOG(HLE, "sceUsbMicInputBlocking: maxSamples: %d, samplerate: %d, bufAddr: %08x", maxSamples, sampleRate, bufAddr);
	if (maxSamples <= 0 || (maxSamples & 0x3F) != 0) {
		return SCE_USBMIC_ERROR_INVALID_MAX_SAMPLES;
	}

	if (sampleRate != 44100 && sampleRate != 22050 && sampleRate != 11025) {
		return SCE_USBMIC_ERROR_INVALID_SAMPLERATE;
	}
	curSampleRate = sampleRate;
	curChannels = 1;
	return __MicInputBlocking(maxSamples, sampleRate, bufAddr);
}

static int sceUsbMicInputInitEx(u32 paramAddr) {
	ERROR_LOG(HLE, "UNIMPL sceUsbMicInputInitEx: %08x", paramAddr);
	return 0;
}

static int sceUsbMicInput() {
	ERROR_LOG(HLE, "UNIMPL sceUsbMicInput");
	return 0;
}
static int sceUsbMicGetInputLength() {
	int ret = Microphone::availableAudioBufSize() / 2;
	ERROR_LOG(HLE, "UNTEST sceUsbMicGetInputLength(ret: %d)", ret);
	return ret;
}

static int sceUsbMicInputInit(int unknown1, int inputVolume, int unknown2) {
	ERROR_LOG(HLE, "UNIMPL sceUsbMicInputInit(unknown1: %d, inputVolume: %d, unknown2: %d)", unknown1, inputVolume, unknown2);
	return 0;
}

static int sceUsbMicWaitInputEnd() {
	ERROR_LOG(HLE, "UNIMPL sceUsbMicWaitInputEnd");
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
	INFO_LOG(HLE, "microphone_command : sr = %d", sampleRate);
	System_SendMessage("microphone_command", ("startRecording:" + std::to_string(sampleRate)).c_str());
#endif
	micState = 1;
	return 0;
}

int Microphone::stopMic() {
#ifdef HAVE_WIN32_MICROPHONE
	if (winMic)
		winMic->sendMessage({ CAPTUREDEVIDE_COMMAND::STOP, nullptr });
#elif PPSSPP_PLATFORM(ANDROID)
	System_SendMessage("microphone_command", "stopRecording");
#endif
	micState = 0;
	return 0;
}

bool Microphone::isHaveDevice() {
#ifdef HAVE_WIN32_MICROPHONE
	return winMic->getDeviceCounts() >= 1;
#elif PPSSPP_PLATFORM(ANDROID)
	return audioRecording_Available();
#endif
	return false;
}

bool Microphone::isMicStarted() {
#ifdef HAVE_WIN32_MICROPHONE
	if(winMic)
		return winMic->isStarted();
#elif PPSSPP_PLATFORM(ANDROID)
	return audioRecording_State();
#endif
	return false;
}

bool Microphone::isNeedInput() {
	return ::isNeedInput;
}

u32 Microphone::numNeedSamples() {
	return ::numNeedSamples;
}

u32 Microphone::availableAudioBufSize() {
	return audioBuf->getAvailableSize();
}

int Microphone::addAudioData(u8 *buf, u32 size) {
	if (audioBuf)
		audioBuf->push(buf, size);
	else
		return 0;

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

u32 __MicInputBlocking(u32 maxSamples, u32 sampleRate, u32 bufAddr) {
	u32 size = maxSamples << 1;
	if (size > numNeedSamples << 1) {
		if (!audioBuf) {
			audioBuf = new QueueBuf(size);
		} else {
			audioBuf->resize(size);
		}
	}
	if (!audioBuf)
		return 0;

	numNeedSamples = maxSamples;
	Microphone::flushAudioData();
	if (!Microphone::isMicStarted()) {
		std::vector<u32> *param = new std::vector<u32>({ sampleRate, 1 });
		Microphone::startMic(param);
	}
	u64 waitTimeus = 0;
	if (Microphone::availableAudioBufSize() < size) {
		waitTimeus = (size - Microphone::availableAudioBufSize()) * 1000000 / 2 / sampleRate;
		isNeedInput = true;
	}
	if(eventUsbMicAudioUpdate == -1)
		eventUsbMicAudioUpdate = CoreTiming::RegisterEvent("UsbMicAudioUpdate", &__UsbMicAudioUpdate);
	CoreTiming::ScheduleEvent(usToCycles(waitTimeus), eventUsbMicAudioUpdate, __KernelGetCurThread());
	MicWaitInfo waitInfo = { __KernelGetCurThread(), bufAddr, size, sampleRate };
	std::unique_lock<std::mutex> lock(wtMutex);
	waitingThreads.push_back(waitInfo);
	lock.unlock();
	DEBUG_LOG(HLE, "MicInputBlocking: blocking thread(%d)", (int)__KernelGetCurThread());
	__KernelWaitCurThread(WAITTYPE_MICINPUT, 1, size, 0, false, "blocking microphone");

	return maxSamples;
}

const HLEFunction sceUsbMic[] =
{
	{0x06128E42, &WrapI_V<sceUsbMicPollInputEnd>,    "sceUsbMicPollInputEnd",         'i', "" },
	{0x2E6DCDCD, &WrapI_UUU<sceUsbMicInputBlocking>, "sceUsbMicInputBlocking",        'i', "xxx" },
	{0x45310F07, &WrapI_U<sceUsbMicInputInitEx>,     "sceUsbMicInputInitEx",          'i', "x" },
	{0x5F7F368D, &WrapI_V<sceUsbMicInput>,           "sceUsbMicInput",                'i', "" },
	{0x63400E20, &WrapI_V<sceUsbMicGetInputLength>,  "sceUsbMicGetInputLength",       'i', "" },
	{0xB8E536EB, &WrapI_III<sceUsbMicInputInit>,     "sceUsbMicInputInit",            'i', "iii" },
	{0xF899001C, &WrapI_V<sceUsbMicWaitInputEnd>,    "sceUsbMicWaitInputEnd",         'i', "" },
};

void Register_sceUsbMic()
{
	RegisterModule("sceUsbMic", ARRAY_SIZE(sceUsbMic), sceUsbMic);
}
