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

#ifdef __cplusplus
extern "C" {
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
}
#endif // __cplusplus

struct VideoFormatTransform {
	GUID MFVideoFormat;
	AVPixelFormat AVVideoFormat;
};

enum class CAPTUREDEVIDE_TYPE {
	VIDEO,
	AUDIO
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
		LONG   padding;
		GUID   audioFomat;
	};
};

template <class T> void SafeRelease(T **ppT) {
	if (*ppT) {
		(*ppT)->Release();
		*ppT = nullptr;
	}
}

class WindowsCaptureDevice;

class ReaderCallback : public IMFSourceReaderCallback {
public:
	ReaderCallback(WindowsCaptureDevice *device);
	~ReaderCallback();

	// IUnknown methods.
	STDMETHODIMP QueryInterface(REFIID iid, void** ppv);
	STDMETHODIMP_(ULONG) AddRef() { return 0; }  // Unused, just define.
	STDMETHODIMP_(ULONG) Release() { return 0; } // Unused, just define.

	// IMFSourceReaderCallback methods.
	STDMETHODIMP OnReadSample(
		HRESULT hrStatus,
		DWORD dwStreamIndex,
		DWORD dwStreamFlags,
		LONGLONG llTimestamp,
		IMFSample *pSample   // Can be null,even if hrStatus is success.
	);

	STDMETHODIMP OnEvent(DWORD, IMFMediaEvent *) { return S_OK; }
	STDMETHODIMP OnFlush(DWORD) { return S_OK; }

	AVPixelFormat getAVVideoFormatbyMFVideoFormat(const GUID &MFVideoFormat);

	/*
	 * Always convet the image to RGB24
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


protected:
	WindowsCaptureDevice *device;
	SwsContext *img_convert_ctx;
};

class WindowsCaptureDevice {
public:
	WindowsCaptureDevice(CAPTUREDEVIDE_TYPE type);
	~WindowsCaptureDevice();

	static void CheckDevices();

	bool init();
	bool start(UINT32 width, UINT32 height);
	bool stop();

	CAPTUREDEVIDE_ERROR getError() const { return error; }
	std::string getErrorMessage() const { return errorMessage; }
	int getDeviceCounts() const { return param.count; }
	// Get a list contained friendly device name.
	std::vector<std::string> getDeviceList(bool forceEnum = false, int *pActuallCount = nullptr);

	void setError(const CAPTUREDEVIDE_ERROR &newError, const std::string &newErrorMessage) { error = newError; errorMessage = newErrorMessage; }
	void setSelction(const UINT32 &selection) { param.selection = selection; }
	void updateState(const CAPTUREDEVIDE_STATE &newState) { state = newState; }
	HRESULT setDeviceParam(IMFMediaType *pType);

	bool isShutDown() const { return state == CAPTUREDEVIDE_STATE::SHUTDOWN; }

	void sendMessage(CAPTUREDEVIDE_MESSAGE message);
	CAPTUREDEVIDE_MESSAGE getMessage();

	HRESULT enumDevices();

	friend class ReaderCallback;

protected:
// Handle message here.
	void messageHandler();

	CAPTUREDEVIDE_TYPE type;
	MediaParam deviceParam;
	MediaParam targetMediaParam;
	CAPTUREDEVIDE_STATE state;
	ChooseDeviceParam param;

	CAPTUREDEVIDE_ERROR error;
	std::string errorMessage;

// MF interface.
	ReaderCallback *m_pCallback;
	IMFSourceReader *m_pReader;
	IMFMediaSource *m_pSource;

// Message loop.
	std::mutex mutex;
	std::condition_variable cond;
	std::queue<CAPTUREDEVIDE_MESSAGE> messageQueue;

// For the shutdown event safety.
	std::mutex sdMutex;

// Param updating synchronously.
	std::mutex paramMutex;

// Camera only
	unsigned char *imageRGB;
	int imgRGBLineSizes[4];
	unsigned char *imageJpeg;
	int imgJpegSize;
};

extern WindowsCaptureDevice *winCamera;
