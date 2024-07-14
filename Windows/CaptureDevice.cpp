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

#include <shlwapi.h>

#include "Common/Thread/ThreadUtil.h"
#include "CaptureDevice.h"
#include "BufferLock.h"
#include "ext/jpge/jpge.h"
#include "CommonTypes.h"
#include "Core/HLE/sceUsbCam.h"
#include "Core/Config.h"

namespace MFAPI {
	HINSTANCE Mflib;
	HINSTANCE Mfplatlib;
	HINSTANCE Mfreadwritelib;

	typedef HRESULT(WINAPI *MFEnumDeviceSourcesFunc)(IMFAttributes *, IMFActivate ***, UINT32 *);
	typedef HRESULT(WINAPI *MFGetStrideForBitmapInfoHeaderFunc)(DWORD, DWORD, LONG *);
	typedef HRESULT(WINAPI *MFCreateSourceReaderFromMediaSourceFunc)(IMFMediaSource *, IMFAttributes *, IMFSourceReader **);
	typedef HRESULT(WINAPI *MFCopyImageFunc)(BYTE *, LONG, const BYTE *, LONG, DWORD, DWORD);

	MFEnumDeviceSourcesFunc EnumDeviceSources;
	MFGetStrideForBitmapInfoHeaderFunc GetStrideForBitmapInfoHeader;
	MFCreateSourceReaderFromMediaSourceFunc CreateSourceReaderFromMediaSource;
	MFCopyImageFunc CopyImage;
}

using namespace MFAPI;

bool RegisterCMPTMFApis(){
	//For the compatibility,these funcs don't be supported on vista.
	Mflib = LoadLibrary(L"Mf.dll");
	Mfplatlib = LoadLibrary(L"Mfplat.dll");
	Mfreadwritelib = LoadLibrary(L"Mfreadwrite.dll");
	if (!Mflib || !Mfplatlib || !Mfreadwritelib)
		return false;

	EnumDeviceSources = (MFEnumDeviceSourcesFunc)GetProcAddress(Mflib, "MFEnumDeviceSources");
	GetStrideForBitmapInfoHeader = (MFGetStrideForBitmapInfoHeaderFunc)GetProcAddress(Mfplatlib, "MFGetStrideForBitmapInfoHeader");
	MFAPI::CopyImage = (MFCopyImageFunc)GetProcAddress(Mfplatlib, "MFCopyImage");
	CreateSourceReaderFromMediaSource = (MFCreateSourceReaderFromMediaSourceFunc)GetProcAddress(Mfreadwritelib, "MFCreateSourceReaderFromMediaSource");
	if (!EnumDeviceSources || !GetStrideForBitmapInfoHeader || !CreateSourceReaderFromMediaSource || !MFAPI::CopyImage)
		return false;

	return true;
}

bool unRegisterCMPTMFApis() {
	if (Mflib) {
		FreeLibrary(Mflib);
		Mflib = nullptr;
	}

	if (Mfplatlib) {
		FreeLibrary(Mfplatlib);
		Mfplatlib = nullptr;
	}

	if (Mfreadwritelib) {
		FreeLibrary(Mfreadwritelib);
		Mfreadwritelib = nullptr;
	}

	EnumDeviceSources = nullptr;
	GetStrideForBitmapInfoHeader = nullptr;
	CreateSourceReaderFromMediaSource = nullptr;
	MFAPI::CopyImage = nullptr;

	return true;
}

WindowsCaptureDevice *winCamera;
WindowsCaptureDevice *winMic;

// TODO: Add more formats, but need some tests.
VideoFormatTransform g_VideoFormats[] =
{
	{ MFVideoFormat_RGB32, AV_PIX_FMT_RGBA    },
	{ MFVideoFormat_RGB24, AV_PIX_FMT_RGB24   },
	{ MFVideoFormat_YUY2,  AV_PIX_FMT_YUYV422 },
	{ MFVideoFormat_NV12,  AV_PIX_FMT_NV12    }
};

AudioFormatTransform g_AudioFormats[] = {
	{ MFAudioFormat_PCM,    8, AV_SAMPLE_FMT_U8 },
	{ MFAudioFormat_PCM,   16, AV_SAMPLE_FMT_S16 },
	{ MFAudioFormat_PCM,   32, AV_SAMPLE_FMT_S32 },
	{ MFAudioFormat_Float, 32, AV_SAMPLE_FMT_FLT }
};

const int g_cVideoFormats = ARRAYSIZE(g_VideoFormats);
const int g_cAudioFormats = ARRAYSIZE(g_AudioFormats);

MediaParam defaultVideoParam = { { 640, 480,  0, MFVideoFormat_RGB24 } };
MediaParam defaultAudioParam = { { 44100, 2, 16, MFAudioFormat_PCM } };

HRESULT GetDefaultStride(IMFMediaType *pType, LONG *plStride);

ReaderCallback::ReaderCallback(WindowsCaptureDevice *_device) : device(_device) {}

ReaderCallback::~ReaderCallback() {
#ifdef USE_FFMPEG
	if (img_convert_ctx) {
		sws_freeContext(img_convert_ctx);
	}
	if (resample_ctx) {
		swr_free(&resample_ctx);
	}
#endif
}

HRESULT ReaderCallback::QueryInterface(REFIID riid, void** ppv)
{
	static const QITAB qit[] =
	{
		QITABENT(ReaderCallback, IMFSourceReaderCallback),
		{ 0 },
	};
	return QISearch(this, qit, riid, ppv);
}

HRESULT ReaderCallback::OnReadSample(
		HRESULT hrStatus,
		DWORD dwStreamIndex,
		DWORD dwStreamFlags,
		LONGLONG llTimestamp,
		IMFSample *pSample) {
	HRESULT hr = S_OK;
	IMFMediaBuffer *pBuffer = nullptr;
	std::lock_guard<std::mutex> lock(device->sdMutex);
	if (device->isShutDown())
		return hr;

	if (FAILED(hrStatus))
		hr = hrStatus;

	if (SUCCEEDED(hr)) {
		if (pSample) {
			hr = pSample->GetBufferByIndex(0, &pBuffer);
		}
	}
	if (SUCCEEDED(hr)) {
		switch (device->type) {
		case CAPTUREDEVIDE_TYPE::VIDEO: {
			BYTE *pbScanline0 = nullptr;
			VideoBufferLock *videoBuffer = nullptr;
			int imgJpegSize = device->imgJpegSize;
			unsigned char* invertedSrcImg = nullptr;
			LONG srcPadding = 0;
			LONG lStride = 0;

			UINT32 srcW = device->deviceParam.width;
			UINT32 srcH = device->deviceParam.height;
			UINT32 dstW = device->targetMediaParam.width;
			UINT32 dstH = device->targetMediaParam.height;
			GUID srcMFVideoFormat = device->deviceParam.videoFormat;

			// pSample can be null, in this case ReadSample still should be called to request next frame.
			if (pSample) {
				videoBuffer = new VideoBufferLock(pBuffer);
				hr = videoBuffer->LockBuffer(device->deviceParam.default_stride, device->deviceParam.height, &pbScanline0, &lStride);

				if (lStride > 0)
					srcPadding = lStride - device->deviceParam.default_stride;
				else
					srcPadding = device->deviceParam.default_stride - lStride;

#ifdef USE_FFMPEG
				if (SUCCEEDED(hr)) {
					// Convert image to RGB24
					if (lStride > 0) {
						imgConvert(
							device->imageRGB, dstW, dstH, device->imgRGBLineSizes,
							pbScanline0, srcW, srcH, srcMFVideoFormat, srcPadding);
					} else {
						// If stride < 0, the pointer to the first row of source image is the last row in memory,should invert it in memory.
						invertedSrcImg = (unsigned char*)av_malloc(av_image_get_buffer_size(getAVVideoFormatbyMFVideoFormat(srcMFVideoFormat), srcW, srcH, 1));
						imgInvert(invertedSrcImg, pbScanline0, srcW, srcH, device->deviceParam.videoFormat, lStride);
						// We alloc a inverted image with no padding, set padding to zero.
						srcPadding = 0;
						imgConvert(
							device->imageRGB, dstW, dstH, device->imgRGBLineSizes,
							invertedSrcImg, srcW, srcH, srcMFVideoFormat, srcPadding);
						av_free(invertedSrcImg);
					}

					// Mirror the image in-place if needed.
					if (g_Config.bCameraMirrorHorizontal) {
						for (int y = 0; y < dstH; y++) {
							uint8_t *line = device->imageRGB + y * device->imgRGBLineSizes[0];
							for (int x = 0; x < dstW / 2; x++) {
								const int invX = dstW - 1 - x;
								const uint8_t r = line[x * 3 + 0];
								const uint8_t g = line[x * 3 + 1];
								const uint8_t b = line[x * 3 + 2];
								line[x * 3 + 0] = line[invX * 3 + 0];
								line[x * 3 + 1] = line[invX * 3 + 1];
								line[x * 3 + 2] = line[invX * 3 + 2];
								line[invX * 3 + 0] = r;
								line[invX * 3 + 1] = g;
								line[invX * 3 + 2] = b;
							}
						}
					}

					// Compress image to jpeg from RGB24.
					jpge::compress_image_to_jpeg_file_in_memory(
						device->imageJpeg, imgJpegSize,
						dstW,
						dstH,
						3,
						device->imageRGB);
				}
#endif
				Camera::pushCameraImage(imgJpegSize, device->imageJpeg);
			}
			// Request the next frame.
			if (SUCCEEDED(hr)) {
				hr = device->m_pReader->ReadSample(
					(DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
					0,
					nullptr,
					nullptr,
					nullptr,
					nullptr
				);
			}
			delete videoBuffer;
			break;
		}
		case CAPTUREDEVIDE_TYPE::Audio: {
			BYTE *sampleBuf = nullptr;
			DWORD length = 0;
			u32 sizeAfterResample = 0;
			// pSample can be null, in this case ReadSample still should be called to request next frame.
			if (pSample) {
				pBuffer->Lock(&sampleBuf, nullptr, &length);
				if (device->needResample()) {
					sizeAfterResample = doResample(
						&device->resampleBuf, device->targetMediaParam.sampleRate, device->targetMediaParam.channels, &device->resampleBufSize,
						sampleBuf, device->deviceParam.sampleRate, device->deviceParam.channels, device->deviceParam.audioFormat, length, device->deviceParam.bitsPerSample);
					if (device->resampleBuf)
						Microphone::addAudioData(device->resampleBuf, sizeAfterResample);	
				} else {
					Microphone::addAudioData(sampleBuf, length);
				}
				pBuffer->Unlock();
			}
			// Request the next frame.
			if (SUCCEEDED(hr)) {
				hr = device->m_pReader->ReadSample(
					(DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
					0,
					nullptr,
					nullptr,
					nullptr,
					nullptr
				);
			}
			break;
		}
		}
	}

	SafeRelease(&pBuffer);
	return hr;
}

AVPixelFormat ReaderCallback::getAVVideoFormatbyMFVideoFormat(const GUID &MFVideoFormat) {
	for (int i = 0; i < g_cVideoFormats; i++) {
		if (MFVideoFormat == g_VideoFormats[i].MFVideoFormat)
			return g_VideoFormats[i].AVVideoFormat;
	}
	return AV_PIX_FMT_RGB24;
}

AVSampleFormat ReaderCallback::getAVAudioFormatbyMFAudioFormat(const GUID &MFAudioFormat, const u32 &bitsPerSample) {
	for (int i = 0; i < g_cAudioFormats; i++) {
		if (MFAudioFormat == g_AudioFormats[i].MFAudioFormat && bitsPerSample == g_AudioFormats[i].bitsPerSample)
			return g_AudioFormats[i].AVAudioFormat;
	}
	return AV_SAMPLE_FMT_S16;
}

void ReaderCallback::imgConvert(
	unsigned char *dst, unsigned int &dstW, unsigned int &dstH, int dstLineSizes[4],
	unsigned char *src, const unsigned int &srcW, const unsigned int &srcH, const GUID &srcFormat, 
	const int &srcPadding) {
#ifdef USE_FFMPEG
	int srcLineSizes[4] = { 0, 0, 0, 0 };
	unsigned char *pSrc[4];
	unsigned char *pDst[4];

	AVPixelFormat srcAVFormat = getAVVideoFormatbyMFVideoFormat(srcFormat);


	av_image_fill_linesizes(srcLineSizes, srcAVFormat, srcW);

	// Is this correct?
	if (srcPadding != 0) {
		for (int i = 0; i < 4; i++) {
			if (srcLineSizes[i] != 0)
				srcLineSizes[i] += srcPadding;
		}
	}

	av_image_fill_pointers(pSrc, srcAVFormat, srcH, src, srcLineSizes);
	av_image_fill_pointers(pDst, AV_PIX_FMT_RGB24, dstH, dst, dstLineSizes);

	if (img_convert_ctx == nullptr) {
		img_convert_ctx = sws_getContext(
			srcW,
			srcH,
			srcAVFormat,
			dstW,
			dstH,
			AV_PIX_FMT_RGB24,
			SWS_BICUBIC,
			nullptr,
			nullptr,
			nullptr
		);
	}

	if (img_convert_ctx) {
		sws_scale(img_convert_ctx,
			(const uint8_t *const *)pSrc,
			srcLineSizes,
			0,
			srcH,
			(uint8_t *const *)pDst,
			dstLineSizes
		);
	}
#endif
}

void ReaderCallback::imgInvert(unsigned char *dst, unsigned char *src, const int &srcW, const int &srcH, const GUID &srcFormat, const int &srcStride) {
#ifdef USE_FFMPEG
	AVPixelFormat srcAvFormat = getAVVideoFormatbyMFVideoFormat(srcFormat);
	int dstLineSizes[4] = { 0, 0, 0, 0 };

	av_image_fill_linesizes(dstLineSizes, srcAvFormat, srcW);

	if(srcFormat == MFVideoFormat_RGB32)
		imgInvertRGBA(dst, dstLineSizes[0], src, srcStride, srcH);
	else if(srcFormat == MFVideoFormat_RGB24)
		imgInvertRGB(dst, dstLineSizes[0], src, srcStride, srcH);
	else if (srcFormat == MFVideoFormat_YUY2)
		imgInvertYUY2(dst, dstLineSizes[0], src, srcStride, srcH);
	else if (srcFormat == MFVideoFormat_NV12)
		imgInvertNV12(dst, dstLineSizes[0], src, srcStride, srcH);;
#endif
}

void ReaderCallback::imgInvertRGBA(unsigned char *dst, int &dstStride, unsigned char *src, const int &srcStride, const int &h) {
	MFAPI::CopyImage(dst, dstStride, src, srcStride, dstStride, h);
}

void ReaderCallback::imgInvertRGB(unsigned char *dst, int &dstStride, unsigned char *src, const int &srcStride, const int &h) {
	for (int y = 0; y < h; y++) {
		for (int srcx = dstStride - 1, dstx = 0; dstx < dstStride; srcx--, dstx++) {
			dst[dstx] = src[srcx];
		}
		dst += dstStride;
		src += srcStride;
	}
}

void ReaderCallback::imgInvertYUY2(unsigned char *dst, int &dstStride, unsigned char *src, const int &srcStride, const int &h) {
	for (int y = 0; y < h; y++) {
		for (int srcx = dstStride - 1, dstx = 0; dstx < dstStride; srcx--, dstx++) {
			dst[dstx] = src[srcx];
		}
		dst += dstStride;
		src += srcStride;
	}
}

void ReaderCallback::imgInvertNV12(unsigned char *dst, int &dstStride, unsigned char *src, const int &srcStride, const int &h) {
	unsigned char *dstY = dst;
	unsigned char *dstU = dst + dstStride * h;
	unsigned char *srcY = src;
	unsigned char *srcV = src + srcStride * h;

	unsigned char *srcY1 = srcY;
	unsigned char *srcY2 = srcY1 + srcStride;
	unsigned char *dstY1 = dstY;
	unsigned char *dstY2 = dstY1 + dstStride;

	bool isodd = h % 2 != 0;

	for (int y = 0; y < (isodd ? h - 1 : h); y += 2) {
		for (int srcx = dstStride - 1, dstx = 0; dstx < dstStride; srcx--, dstx++) {
			dstY1[dstx] = srcY1[srcx];
			dstY2[dstx] = srcY2[srcx];
			dstU[dstx] = srcV[srcx];
		}
		dstY += dstStride * 2;
		srcY1 += srcStride * 2;
		srcY2 += srcStride *2;
		srcV += srcStride;
		dstU += dstStride;
	}
}

u32 ReaderCallback::doResample(u8 **dst, u32 &dstSampleRate, u32 &dstChannels, u32 *dstSize, u8 *src, const u32 &srcSampleRate, const u32 &srcChannels, const GUID &srcFormat, const u32& srcSize, const u32& srcBitsPerSample) {
#ifdef USE_FFMPEG
	AVSampleFormat srcAVFormat = getAVAudioFormatbyMFAudioFormat(srcFormat, srcBitsPerSample);
	int outSamplesCount = 0;
	if (resample_ctx == nullptr) {
		resample_ctx = swr_alloc_set_opts(nullptr,
			av_get_default_channel_layout(dstChannels),
			AV_SAMPLE_FMT_S16,
			dstSampleRate,
			av_get_default_channel_layout(srcChannels),
			srcAVFormat,
			srcSampleRate,
			0,
			nullptr);
		if (resample_ctx == nullptr || swr_init(resample_ctx) < 0) {
			swr_free(&resample_ctx);
			return 0;
		}
	}
	int srcSamplesCount = srcSize / srcChannels / av_get_bytes_per_sample(srcAVFormat); // per channel.
	int outCount = srcSamplesCount * dstSampleRate / srcSampleRate + 256;
	unsigned int outSize = av_samples_get_buffer_size(nullptr, dstChannels, outCount, AV_SAMPLE_FMT_S16, 0);
	
	if (!*dst) {
		*dst = (u8 *)av_malloc(outSize);
		*dstSize = outSize;
	}
	if (!*dst)
		return 0;

	if(*dstSize < outSize)
		av_fast_malloc(dst, dstSize, outSize);

	outSamplesCount = swr_convert(resample_ctx, dst, outCount, (const uint8_t **)&src, srcSamplesCount);
	if (outSamplesCount < 0)
		return 0;
	return av_samples_get_buffer_size(nullptr, dstChannels, outSamplesCount, AV_SAMPLE_FMT_S16, 0);
#else
	return 0;
#endif
}

WindowsCaptureDevice::WindowsCaptureDevice(CAPTUREDEVIDE_TYPE _type) :
	type(_type),
	error(CAPTUREDEVIDE_ERROR_NO_ERROR),
	state(CAPTUREDEVIDE_STATE::UNINITIALIZED) {
	param = { 0 };
	deviceParam = { { 0 } };

	switch (type) {
	case CAPTUREDEVIDE_TYPE::VIDEO:
		targetMediaParam = defaultVideoParam;
		break;
	case CAPTUREDEVIDE_TYPE::Audio:
		targetMediaParam = defaultAudioParam;
		break;
	}

	std::thread t(&WindowsCaptureDevice::messageHandler, this);
	t.detach();
}

WindowsCaptureDevice::~WindowsCaptureDevice() {
#ifdef USE_FFMPEG
	switch (type) {
	case CAPTUREDEVIDE_TYPE::VIDEO:
		av_freep(&imageRGB);
		av_freep(&imageJpeg);
		break;
	case CAPTUREDEVIDE_TYPE::Audio:
		av_freep(&resampleBuf);
		break;
	}
#endif
}

void WindowsCaptureDevice::CheckDevices() {
	isDeviceChanged = true;
}

bool WindowsCaptureDevice::init() {
	HRESULT hr = S_OK;

	if (!RegisterCMPTMFApis()) {
		setError(CAPTUREDEVIDE_ERROR_INIT_FAILED, "Cannot register devices");
		return false;
	}
	std::unique_lock<std::mutex> lock(paramMutex);
	hr = enumDevices();
	lock.unlock();

	if (FAILED(hr)) {
		setError(CAPTUREDEVIDE_ERROR_INIT_FAILED, "Cannot enumerate devices");
		return false;
	}

	updateState(CAPTUREDEVIDE_STATE::STOPPED);
	return true;
}

bool WindowsCaptureDevice::start(void *startParam) {
	HRESULT hr = S_OK;
	IMFAttributes *pAttributes = nullptr;
	IMFMediaType *pType = nullptr;
	UINT32 selection = 0;
	UINT32 count = 0;

	// Release old sources first(if any).
	SafeRelease(&m_pSource);
	SafeRelease(&m_pReader);
	if (m_pCallback) {
		delete m_pCallback;
		m_pCallback = nullptr;
	}
	// Need to re-enumerate the list,because old sources were released.
	std::vector<std::string> deviceList = getDeviceList(true);

	if (deviceList.size() < 1) {
		setError(CAPTUREDEVIDE_ERROR_START_FAILED, "Has no device");
		return false;
	}

	m_pCallback = new ReaderCallback(this);

	std::string selectedDeviceName = type == CAPTUREDEVIDE_TYPE::VIDEO ? g_Config.sCameraDevice : g_Config.sMicDevice;

	switch (state) {
	case CAPTUREDEVIDE_STATE::STOPPED:
		for (auto &name : deviceList) {
			if (name == selectedDeviceName) {
				selection = count;
				break;
			}
			++count;
		}
		setSelction(selection);
		hr = param.ppDevices[param.selection]->ActivateObject(
			__uuidof(IMFMediaSource),
			(void**)&m_pSource);

		if (SUCCEEDED(hr))
			hr = MFCreateAttributes(&pAttributes, 2);

		// Use async mode
		if (SUCCEEDED(hr))
			hr = pAttributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, m_pCallback);

		if (SUCCEEDED(hr))
			hr = pAttributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE);

		if (SUCCEEDED(hr)) {
			hr = CreateSourceReaderFromMediaSource(
					m_pSource,
					pAttributes,
					&m_pReader
				);
		}

		if (!m_pReader)
			hr = E_FAIL;

		if (SUCCEEDED(hr)) {
			switch (type) {
			case CAPTUREDEVIDE_TYPE::VIDEO: {
				if (startParam) {
					std::vector<int> *resolution = static_cast<std::vector<int>*>(startParam);
					targetMediaParam.width = resolution->at(0);
					targetMediaParam.height = resolution->at(1);
					delete resolution;
				}
#ifdef USE_FFMPEG

				av_freep(&imageRGB);
				av_freep(&imageJpeg);
				imageRGB = (unsigned char*)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_RGB24, targetMediaParam.width, targetMediaParam.height, 1));
				imgJpegSize = av_image_get_buffer_size(AV_PIX_FMT_YUVJ411P, targetMediaParam.width, targetMediaParam.height, 1);
				imageJpeg = (unsigned char*)av_malloc(imgJpegSize);
				av_image_fill_linesizes(imgRGBLineSizes, AV_PIX_FMT_RGB24, targetMediaParam.width);
#endif

				for (DWORD i = 0; ; i++) {
					hr = m_pReader->GetNativeMediaType(
						(DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
						i,
						&pType
					);

					if (FAILED(hr)) { break; }

					hr = setDeviceParam(pType);

					if (SUCCEEDED(hr))
						break;
				}
				/*
				hr = m_pReader->GetNativeMediaType(
					(DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
					(DWORD)0xFFFFFFFF,//MF_SOURCE_READER_CURRENT_TYPE_INDEX
					&pType
				);
				if (SUCCEEDED(hr))
					hr = setDeviceParam(pType);*/ // Don't support on Win7

				// Request the first frame, in async mode, OnReadSample will be called when ReadSample completed.
				if (SUCCEEDED(hr)) {
					hr = m_pReader->ReadSample(
						(DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
						0,
						nullptr,
						nullptr,
						nullptr,
						nullptr
					);
				}
				break;
			}

			case CAPTUREDEVIDE_TYPE::Audio: {
				if (startParam) {
					std::vector<u32> *micParam = static_cast<std::vector<u32>*>(startParam);
					targetMediaParam.sampleRate = micParam->at(0);
					targetMediaParam.channels = micParam->at(1);
					delete micParam;
				}

				for (DWORD i = 0; ; i++) {
					hr = m_pReader->GetNativeMediaType(
						(DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
						i,
						&pType
					);

					if (FAILED(hr)) { break; }

					hr = setDeviceParam(pType);

					if (SUCCEEDED(hr))
						break;
				}

				if (SUCCEEDED(hr)) {
					hr = m_pReader->ReadSample(
						(DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
						0,
						nullptr,
						nullptr,
						nullptr,
						nullptr
					);
				}
				break;
			}
			}
		}

		if (FAILED(hr)) {
			setError(CAPTUREDEVIDE_ERROR_START_FAILED, "Cannot start");
			if(m_pSource)
				m_pSource->Shutdown();
			SafeRelease(&m_pSource);
			SafeRelease(&pAttributes);
			SafeRelease(&pType);
			SafeRelease(&m_pReader);
			return false;
		}

		SafeRelease(&pAttributes);
		SafeRelease(&pType);
		updateState(CAPTUREDEVIDE_STATE::STARTED);
		break;
	case CAPTUREDEVIDE_STATE::LOST:
		setError(CAPTUREDEVIDE_ERROR_START_FAILED, "Device has lost");
		return false;
	case CAPTUREDEVIDE_STATE::STARTED:
		setError(CAPTUREDEVIDE_ERROR_START_FAILED, "Device has started");
		return false;
	case CAPTUREDEVIDE_STATE::UNINITIALIZED:
		setError(CAPTUREDEVIDE_ERROR_START_FAILED, "Device doesn't initialize");
		return false;
	default:
		break;
	}
	return true;
}

bool WindowsCaptureDevice::stop() {
	if (state == CAPTUREDEVIDE_STATE::STOPPED)
		return true;
	if (m_pSource)
		m_pSource->Stop();

	updateState(CAPTUREDEVIDE_STATE::STOPPED);

	return true;
};

std::vector<std::string> WindowsCaptureDevice::getDeviceList(bool forceEnum, int *pActuallCount) {
	HRESULT hr = S_OK;
	UINT32 count = 0;
	LPWSTR pwstrName = nullptr;
	char *cstrName = nullptr;
	std::string strName;
	DWORD dwMinSize = 0;
	std::vector<std::string> deviceList;

	if (isDeviceChanged || forceEnum) {
		std::unique_lock<std::mutex> lock(paramMutex);
		for (DWORD i = 0; i < param.count; i++) {
			SafeRelease(&param.ppDevices[i]);
		}
		CoTaskMemFree(param.ppDevices); // Null pointer is okay.

		hr = enumDevices();

		lock.unlock();

		if (SUCCEEDED(hr))
			isDeviceChanged = false;
		else
			return deviceList;
	}

	for (; count < param.count; count++) {
		hr = param.ppDevices[count]->GetAllocatedString(
			MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
			&pwstrName,
			nullptr
		);

		if (SUCCEEDED(hr)) {
			// Get the size needed first
			dwMinSize = WideCharToMultiByte(CP_UTF8, NULL, pwstrName, -1, nullptr, 0, nullptr, FALSE);
			if (dwMinSize == 0)
				hr = E_FAIL;
		}
		if (SUCCEEDED(hr)) {
			cstrName = new char[dwMinSize];
			WideCharToMultiByte(CP_UTF8, NULL, pwstrName, -1, cstrName, dwMinSize, NULL, FALSE);
			strName = cstrName;
			delete[] cstrName;

			deviceList.push_back(strName);
		}

		CoTaskMemFree(pwstrName);

		if (FAILED(hr)) {
			setError(CAPTUREDEVIDE_ERROR_GETNAMES_FAILED, "Error occurred,gotten " + std::to_string((int)count) + " device names");
			if(pActuallCount)
				*pActuallCount = count;
			return deviceList;
		}
	}
	if (pActuallCount)
		*pActuallCount = count + 1;
	return deviceList;
}

HRESULT WindowsCaptureDevice::setDeviceParam(IMFMediaType *pType) {
	HRESULT hr = S_OK;
	GUID subtype = { 0 };
	bool getFormat = false;

	switch (type) {
	case CAPTUREDEVIDE_TYPE::VIDEO:
		hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);
		if (FAILED(hr))
			break;

		for (int i = 0; i < g_cVideoFormats; i++) {
			if (subtype == g_VideoFormats[i].MFVideoFormat) {
				deviceParam.videoFormat = subtype;
				getFormat = true;
				break;
			}
		}

		if (!getFormat) {
			for (int i = 0; i < g_cVideoFormats; i++) {
				hr = pType->SetGUID(MF_MT_SUBTYPE, g_VideoFormats[i].MFVideoFormat);
				if (FAILED(hr))
					continue;

				hr = m_pReader->SetCurrentMediaType(
					(DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
					NULL,
					pType
				);

				if (SUCCEEDED(hr)) {
					deviceParam.videoFormat = g_VideoFormats[i].MFVideoFormat;
					getFormat = true;
					break;
				}
			}
		}
		if (SUCCEEDED(hr))
			hr = MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &deviceParam.width, &deviceParam.height);

		if (SUCCEEDED(hr))
			hr = GetDefaultStride(pType, &deviceParam.default_stride);

		break;
	case CAPTUREDEVIDE_TYPE::Audio:
		hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);
		if (FAILED(hr))
			break;

		for (int i = 0; i < g_cVideoFormats; i++) {
			if (subtype == g_AudioFormats[i].MFAudioFormat) {
				deviceParam.audioFormat = subtype;
				getFormat = true;
				break;
			}
		}

		if (!getFormat) {
			for (int i = 0; i < g_cAudioFormats; i++) {
				hr = pType->SetGUID(MF_MT_SUBTYPE, g_AudioFormats[i].MFAudioFormat);
				if (FAILED(hr))
					continue;

				hr = m_pReader->SetCurrentMediaType(
					(DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
					NULL,
					pType
				);

				if (SUCCEEDED(hr)) {
					deviceParam.audioFormat = g_AudioFormats[i].MFAudioFormat;
					getFormat = true;
					break;
				}
			}
		}
		if (SUCCEEDED(hr))
			hr = pType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &deviceParam.sampleRate);

		if (SUCCEEDED(hr))
			hr = pType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &deviceParam.channels);

		if (SUCCEEDED(hr))
			hr = pType->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, (UINT32 *)&deviceParam.bitsPerSample);

		break;
	}

	return hr;
}

void WindowsCaptureDevice::sendMessage(CAPTUREDEVIDE_MESSAGE message) {
	// Must be unique lock
	std::unique_lock<std::mutex> lock(mutex);
	messageQueue.push(message);
	cond.notify_one();
}

CAPTUREDEVIDE_MESSAGE WindowsCaptureDevice::getMessage() {
	// Must be unique lock
	std::unique_lock<std::mutex> lock(mutex);
	CAPTUREDEVIDE_MESSAGE message;
	cond.wait(lock, [this]() { return !messageQueue.empty(); });
	message = messageQueue.front();
	messageQueue.pop();

	return message;
}

void WindowsCaptureDevice::updateState(const CAPTUREDEVIDE_STATE &newState) {
	state = newState;
	if (isShutDown()) {
		std::unique_lock<std::mutex> guard(stateMutex_);
		stateCond_.notify_all();
	}
}

void WindowsCaptureDevice::waitShutDown() {
	sendMessage({ CAPTUREDEVIDE_COMMAND::SHUTDOWN, nullptr });

	std::unique_lock<std::mutex> guard(stateMutex_);
	while (!isShutDown()) {
		stateCond_.wait(guard);
	}
}

void WindowsCaptureDevice::messageHandler() {
	CoInitializeEx(NULL, COINIT_MULTITHREADED);
	MFStartup(MF_VERSION);
	CAPTUREDEVIDE_MESSAGE message;

	if (type == CAPTUREDEVIDE_TYPE::VIDEO) {
		SetCurrentThreadName("Camera");
	} else if (type == CAPTUREDEVIDE_TYPE::Audio) {
		SetCurrentThreadName("Microphone");
	}

	while ((message = getMessage()).command != CAPTUREDEVIDE_COMMAND::SHUTDOWN) {
		switch (message.command) {
		case CAPTUREDEVIDE_COMMAND::INITIALIZE:
			init();
			break;
		case CAPTUREDEVIDE_COMMAND::START:
			start(message.opacity);
			break;
		case CAPTUREDEVIDE_COMMAND::STOP:
			stop();
			break;
		case CAPTUREDEVIDE_COMMAND::UPDATE_STATE:
			updateState((*(CAPTUREDEVIDE_STATE *)message.opacity));
			break;
		}
	}

	if (state != CAPTUREDEVIDE_STATE::STOPPED)
		stop();

	std::lock_guard<std::mutex> lock(sdMutex);
	SafeRelease(&m_pSource);
	SafeRelease(&m_pReader);
	delete m_pCallback;
	unRegisterCMPTMFApis();

	std::unique_lock<std::mutex> lock2(paramMutex);
	for (DWORD i = 0; i < param.count; i++) {
		SafeRelease(&param.ppDevices[i]);
	}
	CoTaskMemFree(param.ppDevices); // Null pointer is okay.
	lock2.unlock();

	MFShutdown();
	CoUninitialize();

	updateState(CAPTUREDEVIDE_STATE::SHUTDOWN);
}

HRESULT WindowsCaptureDevice::enumDevices() {
	HRESULT hr = S_OK;
	IMFAttributes *pAttributes = nullptr;

	hr = MFCreateAttributes(&pAttributes, 1);
	if (SUCCEEDED(hr)) {
		switch (type) {
		case CAPTUREDEVIDE_TYPE::VIDEO:
			hr = pAttributes->SetGUID(
				MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
				MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
			);

			break;
		case CAPTUREDEVIDE_TYPE::Audio:
			hr = pAttributes->SetGUID(
				MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
				MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID
			);

			break;
		default:
			setError(CAPTUREDEVIDE_ERROR_UNKNOWN_TYPE, "Unknown device type");
			return E_FAIL;
		}
	}
	if (SUCCEEDED(hr)) {
		hr = EnumDeviceSources(pAttributes, &param.ppDevices, &param.count);
	}

	SafeRelease(&pAttributes);
	return hr;
}

bool WindowsCaptureDevice::needResample() {
	return deviceParam.sampleRate    != targetMediaParam.sampleRate     || 
		   deviceParam.channels      != targetMediaParam.channels       || 
		   deviceParam.audioFormat   != targetMediaParam.audioFormat    ||
		   deviceParam.bitsPerSample != targetMediaParam.bitsPerSample;
}

//-----------------------------------------------------------------------------
// GetDefaultStride
//
// Gets the default stride for a video frame, assuming no extra padding bytes.
//
//-----------------------------------------------------------------------------

HRESULT GetDefaultStride(IMFMediaType *pType, LONG *plStride)
{
	LONG lStride = 0;

	// Try to get the default stride from the media type.
	HRESULT hr = pType->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32*)&lStride);
	if (FAILED(hr))
	{
		// Attribute not set. Try to calculate the default stride.
		GUID subtype = GUID_NULL;

		UINT32 width = 0;
		UINT32 height = 0;

		// Get the subtype and the image size.
		hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);
		if (SUCCEEDED(hr))
		{
			hr = MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height);
		}
		if (SUCCEEDED(hr))
		{
			hr = GetStrideForBitmapInfoHeader(subtype.Data1, width, &lStride);
		}

		// Set the attribute for later reference.
		if (SUCCEEDED(hr))
		{
			(void)pType->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(lStride));
		}
	}

	if (SUCCEEDED(hr))
	{
		*plStride = lStride;
	}
	return hr;
}
