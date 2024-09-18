// Copyright (c) 2020- PPSSPP Project.

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

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <queue>
#include <thread>

#include "Core/HLE/sceUsbMic.h"

#ifdef __cplusplus
extern "C" {
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavutil/imgutils.h"
}
#endif // __cplusplus

struct VideoFormatTransform {
	GUID MFVideoFormat;
	AVPixelFormat AVVideoFormat;
};

struct AudioFormatTransform {
	GUID MFAudioFormat;
	u32 bitsPerSample;
	AVSampleFormat AVAudioFormat;
};

enum class CAPTUREDEVIDE_TYPE {
	VIDEO,
	Audio
};

enum class CAPTUREDEVIDE_STATE {
	UNINITIALIZED,
	LOST,
	STOPPED,
	STARTED,
	SHUTDOWN
};

enum class CAPTUREDEVIDE_COMMAND {
	INITIALIZE,
	START,
	STOP,
	SHUTDOWN,
	UPDATE_STATE
};

enum CAPTUREDEVIDE_ERROR {
	CAPTUREDEVIDE_ERROR_NO_ERROR,
	CAPTUREDEVIDE_ERROR_UNKNOWN_TYPE = 0x80000001,
	CAPTUREDEVIDE_ERROR_INIT_FAILED,
	CAPTUREDEVIDE_ERROR_START_FAILED,
	CAPTUREDEVIDE_ERROR_STOP_FAILED,
	CAPTUREDEVIDE_ERROR_GETNAMES_FAILED
};

struct CAPTUREDEVIDE_MESSAGE{
	CAPTUREDEVIDE_COMMAND command;
	void *opacity;
};

struct ChooseDeviceParam {
	IMFActivate **ppDevices;
	UINT32      count;
	UINT32      selection;
};

union MediaParam {
	struct  {
		UINT32 width;
		UINT32 height;
		LONG   default_stride;
		GUID   videoFormat;
	};
	struct  {
		UINT32 sampleRate;
		UINT32 channels;
		LONG bitsPerSample;
		GUID   audioFormat;
	};
};

template <class T> void SafeRelease(T **ppT) {
	if (*ppT) {
		(*ppT)->Release();
		*ppT = nullptr;
	}
}

class WindowsCaptureDevice;

class ReaderCallback final : public IMFSourceReaderCallback {
public:
	ReaderCallback(WindowsCaptureDevice *device);
	virtual ~ReaderCallback();

	// IUnknown methods.
	STDMETHODIMP QueryInterface(REFIID iid, void** ppv) override;
	STDMETHODIMP_(ULONG) AddRef() override { return 0; }  // Unused, just define.
	STDMETHODIMP_(ULONG) Release() override { return 0; } // Unused, just define.

	// IMFSourceReaderCallback methods.
	STDMETHODIMP OnReadSample(
		HRESULT hrStatus,
		DWORD dwStreamIndex,
		DWORD dwStreamFlags,
		LONGLONG llTimestamp,
		IMFSample *pSample   // Can be null,even if hrStatus is success.
	) override;

	STDMETHODIMP OnEvent(DWORD, IMFMediaEvent *) override { return S_OK; }
	STDMETHODIMP OnFlush(DWORD) override { return S_OK; }

	AVPixelFormat getAVVideoFormatbyMFVideoFormat(const GUID &MFVideoFormat);
	AVSampleFormat getAVAudioFormatbyMFAudioFormat(const GUID &MFAudioFormat, const u32 &bitsPerSample);

	/*
	 * Always convert the image to RGB24
	 * @param dst/src pointer to destination/source image
	 * @param dstW/srcW, dstH/srcH destination/source image's width and height in pixels
	 * @param dstLineSizes get the linesize of each plane by av_image_fill_linesizes()
	 * @param srcFormat MF_MT_SUBTYPE attribute of source image
	 * @param srcPadding should be setted to non-zero if source image has padding 
	 */
	void imgConvert(
		unsigned char *dst, unsigned int &dstW, unsigned int &dstH, int dstLineSizes[4],
		unsigned char *src, const unsigned int &srcW, const unsigned int &srcH, const GUID &srcFormat,
		const int &srcPadding);

	// Flip image start and end in memory, it is neccessary if stride of source image is a negative value
	// Might need some tests in different machine.
	void imgInvert(unsigned char *dst, unsigned char *src, const int &srcW, const int &srcH, const GUID &srcFormat, const int &srcStride);
	void imgInvertRGBA(unsigned char *dst, int &dstStride, unsigned char *src, const int &srcStride, const int &h);
	void imgInvertRGB(unsigned char *dst, int &dstStride, unsigned char *src, const int &srcStride, const int &h);
	void imgInvertYUY2(unsigned char *dst, int &dstStride, unsigned char *src, const int &srcStride, const int &h);
	void imgInvertNV12(unsigned char *dst, int &dstStride, unsigned char *src, const int &srcStride, const int &h);

	/*
	 * Always resample to uncompressed signed 16bits
	 * @param dst pointer to pointer of dst buffer could be a nullptr, would be overwritten by a new pointer if space too small or a nullptr, should be freed by av_free/av_freep by caller
	 * @param dstSize pointer to size of the dst buffer, could be modified after this func
	 * @param srcFormat MF_MT_SUBTYPE attribute of the source audio data
	 * @param srcSize size of valid data in the source buffer, in bytes
	 * @return size of output in bytes 
	 */
	u32 doResample(u8 **dst, u32 &dstSampleRate, u32 &dstChannels, u32 *dstSize, u8 *src, const u32 &srcSampleRate, const u32 &srcChannels, const GUID &srcFormat, const u32 &srcSize, const u32& srcBitsPerSamples);

protected:
	WindowsCaptureDevice *device;
	SwsContext *img_convert_ctx = nullptr;
	SwrContext *resample_ctx = nullptr;
};

class WindowsCaptureDevice {
public:
	WindowsCaptureDevice(CAPTUREDEVIDE_TYPE type);
	~WindowsCaptureDevice();

	void CheckDevices();

	bool init();
	bool start(void *startParam);
	bool stop();

	CAPTUREDEVIDE_ERROR getError() const { return error; }
	std::string getErrorMessage() const { return errorMessage; }
	int getDeviceCounts() const { return param.count; }
	// Get a list contained friendly device name.
	std::vector<std::string> getDeviceList(bool forceEnum = false, int *pActuallCount = nullptr);

	void setError(const CAPTUREDEVIDE_ERROR &newError, const std::string &newErrorMessage) { error = newError; errorMessage = newErrorMessage; }
	void setSelction(const UINT32 &selection) { param.selection = selection; }
	HRESULT setDeviceParam(IMFMediaType *pType);

	bool isShutDown() const { return state == CAPTUREDEVIDE_STATE::SHUTDOWN; }
	bool isStarted() const { return state == CAPTUREDEVIDE_STATE::STARTED; }
	void waitShutDown();

	void sendMessage(CAPTUREDEVIDE_MESSAGE message);
	CAPTUREDEVIDE_MESSAGE getMessage();

	HRESULT enumDevices();

	bool needResample();

	friend class ReaderCallback;

protected:
	void updateState(const CAPTUREDEVIDE_STATE &newState);
	// Handle message here.
	void messageHandler();

	CAPTUREDEVIDE_TYPE type;
	MediaParam deviceParam;
	MediaParam targetMediaParam;
	CAPTUREDEVIDE_STATE state;
	ChooseDeviceParam param;

	CAPTUREDEVIDE_ERROR error;
	std::string errorMessage;

	bool isDeviceChanged = false;

// MF interface.
	ReaderCallback *m_pCallback = nullptr;
	IMFSourceReader *m_pReader = nullptr;
	IMFMediaSource *m_pSource = nullptr;

// Message loop.
	std::mutex mutex;
	std::condition_variable cond;
	std::queue<CAPTUREDEVIDE_MESSAGE> messageQueue;

// For the shutdown event safety.
	std::mutex sdMutex;

// Param updating synchronously.
	std::mutex paramMutex;
	std::mutex stateMutex_;
	std::condition_variable stateCond_;

// Camera only
	unsigned char *imageRGB = nullptr;
	int imgRGBLineSizes[4]{};
	unsigned char *imageJpeg = nullptr;
	int imgJpegSize = 0;

//Microphone only
	u8 *resampleBuf = nullptr;
	u32 resampleBufSize = 0;
};

extern WindowsCaptureDevice *winCamera;
extern WindowsCaptureDevice *winMic;
