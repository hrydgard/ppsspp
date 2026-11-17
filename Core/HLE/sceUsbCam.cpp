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

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

#include "ppsspp_config.h"

#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceUsbCam.h"
#include "Core/HLE/sceUsbMic.h"
#include "Core/CoreTiming.h"
#include "Core/HW/Camera.h"
#include "Core/MemMapHelpers.h"
#include "ext/jpge/jpgd.h"
#include "ext/jpge/jpge.h"

#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP) && !defined(__LIBRETRO__)
#define HAVE_WIN32_CAMERA
#endif

#ifdef HAVE_WIN32_CAMERA
#include "Common/CommonWindows.h"
#include "Windows/CaptureDevice.h"
#endif

Camera::Config *config;

unsigned int videoBufferLength = 0;
unsigned int nextVideoFrame = 0;
// When video capture started, which the frame clock counts from. Not saved in states, it only sets the phase.
static u64 videoStartUs = 0;
uint8_t *videoBuffer;
std::mutex videoBufferMutex;

enum {
	VIDEO_BUFFER_SIZE = 40 * 1000,
};

// The image controls, as the driver stores them (getters return the raw value that was set). The
// defaults are what usbcam.prx sets when the camera attaches.
struct UsbCamControls {
	u32 brightness = 128;
	u32 contrast = 64;
	u32 saturation = 125;
	u32 sharpness = 0;
	u32 zoom = 10;
	u32 effectMode = 0;
	u32 antiFlicker = 0;
	u32 evLevel = 0;
	u32 reverseMode = 0;
};

static UsbCamControls controls;
static int lensCallback = -1;
static bool videoReadPending = false;
static u32 lastMicReadSize = 0;
static u64 micReadStartUs = 0;
static bool stillPending = false;
static u32 stillSize = 0;
static u32 stillLength = 0;

// What videoBuffer holds until the camera's first frame.
enum class Placeholder {
	None,
	Black,
	Red,
};
static Placeholder placeholder = Placeholder::None;
// Set by the platform's capture code when the camera won't start.
static std::atomic<bool> captureFailed{ false };
// Where a failed start can't be reported, a camera that hasn't sent a frame by then failed.
static constexpr u64 CAMERA_START_TIMEOUT_US = 3000000;

void __UsbCamInit() {
	config       = new Camera::Config();
	config->mode = Camera::Mode::Unused;
	config->type = Camera::ConfigType::CfNone;
	videoBuffer  = new uint8_t[VIDEO_BUFFER_SIZE];
	controls = UsbCamControls();
	lensCallback = -1;
	videoReadPending = false;
	lastMicReadSize = 0;
	micReadStartUs = 0;
	stillPending = false;
	stillSize = 0;
	stillLength = 0;
	placeholder = Placeholder::None;
	captureFailed = false;
}

void __UsbCamDoState(PointerWrap &p) {
	const bool wasCapturing = config->mode == Camera::Mode::Video;
	auto s = p.Section("sceUsbCam", 0, 2);
	if (!s) {
		if (p.mode == p.MODE_READ) {
			// Older states didn't save the camera, so leave it off.
			if (wasCapturing) {
				Camera::stopCapture();
			}
			config->mode = Camera::Mode::Unused;
			config->type = Camera::ConfigType::CfNone;
		}
		return;
	}

	Do(p, *config);
	if (s >= 2) {
		Do(p, controls.brightness);
		Do(p, controls.contrast);
		Do(p, controls.saturation);
		Do(p, controls.sharpness);
		Do(p, controls.zoom);
		Do(p, controls.effectMode);
		Do(p, controls.antiFlicker);
		Do(p, controls.evLevel);
		Do(p, controls.reverseMode);
		Do(p, lensCallback);
		Do(p, videoReadPending);
		Do(p, lastMicReadSize);
		Do(p, micReadStartUs);
		Do(p, stillPending);
		Do(p, stillSize);
		Do(p, stillLength);
	} else {
		controls = UsbCamControls();
		lensCallback = -1;
		videoReadPending = false;
		lastMicReadSize = 0;
		micReadStartUs = 0;
		stillPending = false;
		stillSize = 0;
		stillLength = 0;
	}
	if (p.mode == p.MODE_READ) {
		if (config->mode == Camera::Mode::Video) { // stillImage? TBD
			Camera::stopCapture();
			Camera::startCapture();
		} else if (wasCapturing) {
			Camera::stopCapture();
		}
	}
}

void __UsbCamShutdown() {
	if (config->mode == Camera::Mode::Video) { // stillImage? TBD
		Camera::stopCapture();
	}
	// Capture can still deliver a frame after stopCapture(), on its own thread.
	std::lock_guard<std::mutex> lock(videoBufferMutex);
	delete[] videoBuffer;
	videoBuffer = nullptr;
	delete config;
	config = nullptr;
}

// TODO: Technically, we should store the videoBuffer into the savestate, if this
// module has been initialized.

static int getCameraResolution(Camera::ConfigType type, int *width, int *height) {
	if (type == Camera::ConfigType::CfStill || type == Camera::ConfigType::CfVideo) {
		switch(config->stillParam.resolution) {
			case 0: *width  = 160; *height = 120; return 0;
			case 1: *width  = 176; *height = 144; return 0;
			case 2: *width  = 320; *height = 240; return 0;
			case 3: *width  = 352; *height = 288; return 0;
			case 4: *width  = 640; *height = 480; return 0;
			case 5: *width  =1024; *height = 768; return 0;
			case 6: *width  =1280; *height = 960; return 0;
			case 7: *width  = 480; *height = 272; return 0;
			case 8: *width  = 360; *height = 272; return 0;
		}
	} else if (type == Camera::ConfigType::CfStillEx || type == Camera::ConfigType::CfVideoEx) {
		switch(config->stillExParam.resolution) {
			case 0: *width  = 160; *height = 120; return 0;
			case 1: *width  = 176; *height = 144; return 0;
			case 2: *width  = 320; *height = 240; return 0;
			case 3: *width  = 352; *height = 288; return 0;
			case 4: *width  = 360; *height = 272; return 0;
			case 5: *width  = 480; *height = 272; return 0;
			case 6: *width  = 640; *height = 480; return 0;
			case 7: *width  =1024; *height = 768; return 0;
			case 8: *width  =1280; *height = 960; return 0;
		}
	}
	*width  = 0; *height = 0; return 1;
}


static int sceUsbCamSetupMic(u32 paramAddr, u32 workareaAddr, int wasize) {
	auto param = PSPPointer<PspUsbCamSetupMicParam>::Create(paramAddr);
	if (param.IsValid()) {
		config->micParam = *param;
		param.NotifyRead("UsbCamSetupMic");
	}
	return hleLogInfo(Log::sceMisc, 0);
}

static int sceUsbCamSetupMicEx(u32 paramAddr, u32 workareaAddr, int wasize) {
	if (wasize < 0x108) {
		return hleLogError(Log::HLE, SCE_ERROR_USBCAM_BUFFER_TOO_SMALL, "work area too small");
	}
	if (!Memory::IsValidRange(paramAddr, 0x24) || !Memory::IsValidAddress(workareaAddr)) {
		return hleLogError(Log::HLE, SCE_ERROR_USBCAM_INVALID_ADDRESS, "bad address");
	}
	// Unlike SetupMic, Ex also takes 48 kHz. A nonzero mode word at +0x20 asks for 132-byte packets,
	// which isn't emulated.
	const u32 frequency = Memory::ReadUnchecked_U32(paramAddr + 0x1C);
	const u32 mode = Memory::ReadUnchecked_U32(paramAddr + 0x20);
	if (frequency != 44100 && frequency != 22050 && frequency != 11025 && frequency != 48000) {
		return hleLogError(Log::HLE, SCE_ERROR_USBCAM_INVALID_FREQUENCY, "bad frequency");
	}
	config->micParam.frequency = frequency;
	if (mode != 0) {
		return hleLogWarning(Log::HLE, 0, "packet mode %d not supported", mode);
	}
	return hleLogInfo(Log::HLE, 0);
}

static int sceUsbCamStartMic() {
	INFO_LOG(Log::HLE, "UNIMPL sceUsbCamStartMic");
	return 0;
}

static int sceUsbCamStopMic() {
	INFO_LOG(Log::HLE, "UNIMPL sceUsbCamStopMic");
	return 0;
}

static int sceUsbCamReadMicBlocking(u32 bufAddr, u32 size) {
	if (!Memory::IsValidAddress(bufAddr)) {
		ERROR_LOG(Log::HLE,"sceUsbCamReadMicBlocking(%08x, %d): invalid addresses", bufAddr, size);
		return -1;
	}

	INFO_LOG(Log::HLE, "sceUsbCamReadMicBlocking: size: %d", size);
	return __MicInput(size >> 1, config->micParam.frequency, bufAddr, CAMERAMIC);
}

static int sceUsbCamReadMic(u32 bufAddr, u32 size) {
	if (!Memory::IsValidAddress(bufAddr)) {
		ERROR_LOG(Log::HLE, "sceUsbCamReadMic(%08x, %d): invalid addresses", bufAddr, size);
		return -1;
	}

	INFO_LOG(Log::HLE, "sceUsbCamReadMic: size: %d", size);
	lastMicReadSize = size;
	micReadStartUs = CoreTiming::GetGlobalTimeUs();
	return __MicInput(size >> 1, config->micParam.frequency, bufAddr, CAMERAMIC, false);
}

// Returns the byte count of the last read, also when nothing is pending (as the driver does).
static int sceUsbCamWaitReadMicEnd() {
	if (__MicWaitInputEnd(lastMicReadSize)) {
		return hleLogDebug(Log::HLE, lastMicReadSize, "waiting");
	}
	// Without a host mic the read never fills, but the camera's mic is real-time: wait until the
	// samples would have arrived, so a reader thread doesn't spin.
	const u32 frequency = config->micParam.frequency != 0 ? config->micParam.frequency : 44100;
	const u64 dueUs = micReadStartUs + (u64)lastMicReadSize * 1000000ULL / 2 / frequency;
	const u64 nowUs = CoreTiming::GetGlobalTimeUs();
	if (dueUs > nowUs) {
		return hleDelayResult(hleLogDebug(Log::HLE, lastMicReadSize), "camera mic", (int)(dueUs - nowUs));
	}
	return hleLogDebug(Log::HLE, lastMicReadSize);
}

static int sceUsbCamGetMicDataLength() {
	return Microphone::getReadMicDataLength();
}

static int sceUsbCamSetupVideo(u32 paramAddr, u32 workareaAddr, int wasize) {
	auto param = PSPPointer<PspUsbCamSetupVideoParam>::Create(paramAddr);
	if (param.IsValid()) {
		config->videoParam = *param;
		param.NotifyRead("UsbCamSetupVideo");
	}
	config->type = Camera::ConfigType::CfVideo;
	return 0;
}

static int sceUsbCamSetupVideoEx(u32 paramAddr, u32 workareaAddr, int wasize) {
	auto param = PSPPointer<PspUsbCamSetupVideoExParam>::Create(paramAddr);
	if (param.IsValid()) {
		config->videoExParam = *param;
		param.NotifyRead("UsbCamSetupVideoEx");
	}
	config->type = Camera::ConfigType::CfVideoEx;
	return 0;
}

// Whether startCapture() has a camera to start.
static bool hasCaptureDevice() {
#ifdef HAVE_WIN32_CAMERA
	return winCamera && !winCamera->getDeviceList().empty();
#elif PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(IOS)
	return !System_GetCameraDeviceList().empty();
#else
	return false;
#endif
}

// Fills videoBuffer with a placeholder frame. Call with videoBufferMutex held.
static void setPlaceholderFrame(bool red) {
	int width, height;
	getCameraResolution(config->type, &width, &height);

	unsigned char *jpegData = nullptr;
	int jpegLen = 0;
	__cameraDummyImage(width, height, &jpegData, &jpegLen, red);
	memset(videoBuffer, 0, VIDEO_BUFFER_SIZE);
	videoBufferLength = 0;
	if (jpegData) {
		if (jpegLen <= VIDEO_BUFFER_SIZE) {
			memcpy(videoBuffer, jpegData, jpegLen);
			videoBufferLength = jpegLen;
		}
		free(jpegData);
	}
	placeholder = red ? Placeholder::Red : Placeholder::Black;
}

// Before a read, with videoBufferMutex held: a camera that failed to start shows red.
static void updatePlaceholderFrame() {
	if (placeholder != Placeholder::Black) {
		return;
	}
	if (captureFailed || CoreTiming::GetGlobalTimeUs() - videoStartUs > CAMERA_START_TIMEOUT_US) {
		INFO_LOG(Log::HLE, "Camera: no frame, showing red (%s)", captureFailed ? "start failed" : "timed out");
		setPlaceholderFrame(true);
	}
}

void Camera::onCaptureFailed() {
	captureFailed = true;
}

static int sceUsbCamStartVideo() {
	std::lock_guard<std::mutex> lock(videoBufferMutex);

	// Black until the camera's first frame, or red if there's no camera.
	videoStartUs = CoreTiming::GetGlobalTimeUs();
	captureFailed = false;
	setPlaceholderFrame(!hasCaptureDevice());
	Camera::startCapture();
	return 0;
}

static int sceUsbCamStopVideo() {
	Camera::stopCapture();
	// A read pending across the stop completes with 0.
	if (videoReadPending) {
		nextVideoFrame = 0;
	}
	return 0;
}

// How often the camera delivers a frame, from the framerate in the setup params
// (PSPSDK's PSP_USBCAM_FRAMERATE_*: 3.75, 5, 7.5, 10, 15, 20, 30 and 60 fps).
static int getFrameIntervalUs() {
	static const int intervalsUs[] = { 266667, 200000, 133333, 100000, 66667, 50000, 33333, 16667 };
	int framerate = config->type == Camera::ConfigType::CfVideoEx ? config->videoExParam.framerate : config->videoParam.framerate;
	if (framerate < 0 || framerate >= (int)ARRAY_SIZE(intervalsUs)) {
		framerate = 6;  // 30 fps
	}
	return intervalsUs[framerate];
}

// How long until the camera's next frame.
static int getNextFrameWaitUs() {
	const int intervalUs = getFrameIntervalUs();
	const u64 sinceStartUs = CoreTiming::GetGlobalTimeUs() - videoStartUs;
	return intervalUs - (int)(sinceStartUs % intervalUs);
}

static int sceUsbCamReadVideoFrameBlocking(u32 bufAddr, u32 size) {
	std::lock_guard<std::mutex> lock(videoBufferMutex);
	updatePlaceholderFrame();
	u32 transferSize = std::min(videoBufferLength, size);
	if (Memory::IsValidRange(bufAddr, size)) {
		Memory::Memcpy(bufAddr, videoBuffer, transferSize);
	}
	// This blocks until the camera's next frame. Returning at once lets a high-priority capture thread
	// (Go!Edit's bhCameraGetJpeg) spin in its read loop and starve the rest of the game.
	return hleDelayResult(hleLogDebug(Log::HLE, transferSize), "camera frame", getNextFrameWaitUs());
}

// Queues a read, which WaitReadVideoFrameEnd or PollReadVideoFrameEnd completes. We copy the frame
// right away, and the wait supplies the delay until the next one.
static int sceUsbCamReadVideoFrame(u32 bufAddr, u32 size) {
	if (bufAddr == 0) {
		return hleLogError(Log::HLE, SCE_ERROR_USBCAM_INVALID_POINTER);
	}
	if (config->mode != Camera::Mode::Video) {
		return hleLogError(Log::HLE, SCE_ERROR_USBCAM_NOT_PENDING, "video not started");
	}
	if (videoReadPending) {
		return hleLogError(Log::HLE, SCE_ERROR_USBCAM_BUSY, "read already pending");
	}
	std::lock_guard<std::mutex> lock(videoBufferMutex);
	updatePlaceholderFrame();
	u32 transferSize = std::min(videoBufferLength, size);
	if (Memory::IsValidRange(bufAddr, size)) {
		Memory::Memcpy(bufAddr, videoBuffer, transferSize);
	}
	// A frame bigger than the buffer still copies what fits, and the read reports the error.
	nextVideoFrame = videoBufferLength > size ? SCE_ERROR_USBCAM_BUFFER_TOO_SMALL : transferSize;
	videoReadPending = true;
	return hleLogDebug(Log::HLE, 0);
}

static int sceUsbCamPollReadVideoFrameEnd() {
	if (!videoReadPending) {
		return hleLogVerbose(Log::HLE, SCE_ERROR_USBCAM_NOT_PENDING, "no read pending");
	}
	videoReadPending = false;
	return hleLogVerbose(Log::HLE, nextVideoFrame);
}

static int sceUsbCamWaitReadVideoFrameEnd() {
	if (!videoReadPending) {
		return hleLogError(Log::HLE, SCE_ERROR_USBCAM_NOT_PENDING, "no read pending");
	}
	videoReadPending = false;
	return hleDelayResult(hleLogDebug(Log::HLE, nextVideoFrame), "camera frame", getNextFrameWaitUs());
}

static int sceUsbCamGetReadVideoFrameSize() {
	// The size of the last delivered frame, 0 if that read failed.
	return hleLogDebug(Log::HLE, (int)nextVideoFrame < 0 ? 0 : nextVideoFrame);
}

static int sceUsbCamSetupStill(u32 paramAddr) {
	INFO_LOG(Log::HLE, "UNIMPL sceUsbCamSetupStill");
	auto param = PSPPointer<PspUsbCamSetupStillParam>::Create(paramAddr);
	if (param.IsValid()) {
		config->stillParam = *param;
		param.NotifyRead("UsbCamSetupStill");
	}
	config->type = Camera::ConfigType::CfStill;
	return 0;
}

static int sceUsbCamSetupStillEx(u32 paramAddr) {
	INFO_LOG(Log::HLE, "UNIMPL sceUsbCamSetupStillEx");
	auto param = PSPPointer<PspUsbCamSetupStillExParam>::Create(paramAddr);
	if (param.IsValid()) {
		config->stillExParam = *param;
		param.NotifyRead("UsbCamSetupStillEx");
	}
	config->type = Camera::ConfigType::CfStillEx;
	return 0;
}

static bool isStillSetUp() {
	return config->type == Camera::ConfigType::CfStill || config->type == Camera::ConfigType::CfStillEx;
}

// The latest video frame, scaled to width x height and encoded to fit maxSize. False if there's no
// frame or it doesn't decode.
static bool scaleLastFrame(int width, int height, int maxSize, std::vector<uint8_t> *out) {
	int frameWidth = 0, frameHeight = 0, comps = 0;
	unsigned char *rgb = jpgd::decompress_jpeg_image_from_memory(videoBuffer, (int)videoBufferLength, &frameWidth, &frameHeight, &comps, 3);
	if (!rgb) {
		return false;
	}
	std::vector<uint8_t> scaled(width * height * 3);
	for (int y = 0; y < height; y++) {
		const int sy = y * frameHeight / height;
		for (int x = 0; x < width; x++) {
			const int sx = x * frameWidth / width;
			memcpy(&scaled[(y * width + x) * 3], &rgb[(sy * frameWidth + sx) * 3], 3);
		}
	}
	free(rgb);
	out->resize(scaled.size() + 1024);
	int size = Camera::encodeToFit(maxSize, [&](int quality) {
		jpge::params params;
		params.m_quality = quality;
		int outSize = (int)out->size();
		return jpge::compress_image_to_jpeg_file_in_memory(out->data(), outSize, width, height, 3, scaled.data(), params) ? outSize : -1;
	});
	if (size < 0) {
		return false;
	}
	out->resize(size);
	return true;
}

// Takes the picture into bufAddr and returns its length: the latest video frame at the still
// resolution, or a placeholder if there's none.
static u32 captureStill(u32 bufAddr, u32 size) {
	int width, height;
	getCameraResolution(config->type, &width, &height);
	const int jpegSize = config->type == Camera::ConfigType::CfStillEx ? config->stillExParam.jpegsize : config->stillParam.jpegsize;
	const int maxSize = jpegSize > 0 ? std::min(jpegSize, (int)size) : (int)size;

	std::lock_guard<std::mutex> lock(videoBufferMutex);
	std::vector<uint8_t> image;
	if (videoBufferLength > 0 && scaleLastFrame(width, height, maxSize, &image)) {
		Memory::Memcpy(bufAddr, image.data(), std::min((u32)image.size(), size), "UsbCamStill");
		return (u32)image.size();
	}
	unsigned char *jpegData = nullptr;
	int jpegLen = 0;
	__cameraDummyImage(width, height, &jpegData, &jpegLen, true);
	if (!jpegData) {
		return 0;
	}
	Memory::Memcpy(bufAddr, jpegData, std::min((u32)jpegLen, size), "UsbCamStill");
	free(jpegData);
	return jpegLen;
}

static int stillInputCheck(u32 bufAddr, u32 size) {
	if (size < 0x40) {
		return SCE_ERROR_USBCAM_INVALID_SIZE;
	}
	if (!Memory::IsValidRange(bufAddr, size)) {
		return SCE_ERROR_USBCAM_INVALID_ADDRESS;
	}
	if (!isStillSetUp()) {
		return SCE_ERROR_USBCAM_NOT_SET_UP;
	}
	// A still can't be taken while video runs.
	if (config->mode == Camera::Mode::Video || stillPending) {
		return SCE_ERROR_USBCAM_BUSY;
	}
	return 0;
}

static int sceUsbCamStillInput(u32 bufAddr, u32 size) {
	int error = stillInputCheck(bufAddr, size);
	if (error != 0) {
		return hleLogError(Log::HLE, error);
	}
	stillSize = size;
	stillLength = captureStill(bufAddr, size);
	stillPending = true;
	return hleLogInfo(Log::HLE, 0);
}

static int sceUsbCamStillInputBlocking(u32 bufAddr, u32 size) {
	int error = stillInputCheck(bufAddr, size);
	if (error != 0) {
		return hleLogError(Log::HLE, error);
	}
	stillSize = size;
	stillLength = captureStill(bufAddr, size);
	return hleLogInfo(Log::HLE, stillLength > size ? SCE_ERROR_USBCAM_BUFFER_TOO_SMALL : stillLength);
}

static int stillInputEnd() {
	if (!stillPending) {
		return SCE_ERROR_USBCAM_NOT_PENDING;
	}
	stillPending = false;
	return stillLength > stillSize ? SCE_ERROR_USBCAM_BUFFER_TOO_SMALL : stillLength;
}

static int sceUsbCamStillWaitInputEnd() {
	return hleLogInfo(Log::HLE, stillInputEnd());
}

static int sceUsbCamStillPollInputEnd() {
	return hleLogDebug(Log::HLE, stillInputEnd());
}

static int sceUsbCamStillCancelInput() {
	if (!stillPending) {
		return hleLogError(Log::HLE, SCE_ERROR_USBCAM_NOT_PENDING);
	}
	stillPending = false;
	return hleLogDebug(Log::HLE, 0);
}

static int sceUsbCamStillGetInputLength() {
	if (!isStillSetUp()) {
		return hleLogError(Log::HLE, SCE_ERROR_USBCAM_NOT_SET_UP);
	}
	return hleLogDebug(Log::HLE, std::min(stillSize, stillLength));
}

static int sceUsbCamAutoImageReverseSW(int on) {
	INFO_LOG(Log::HLE, "UNIMPL sceUsbCamAutoImageReverseSW: %d", on);
	return 0;
}

static int sceUsbCamGetLensDirection() {
	INFO_LOG(Log::HLE, "UNIMPL sceUsbCamGetLensDirection");
	return 0;
}

// The image controls by the ids usbcam.prx's dispatcher uses; sceUsbCam_1E958148 takes them directly.
enum UsbCamControlId : u32 {
	USBCAM_CONTROL_BRIGHTNESS = 1,
	USBCAM_CONTROL_CONTRAST = 2,
	USBCAM_CONTROL_SATURATION = 3,
	USBCAM_CONTROL_SHARPNESS = 4,
	USBCAM_CONTROL_ZOOM = 5,
	USBCAM_CONTROL_REVERSE = 6,
	USBCAM_CONTROL_EFFECT = 7,
	USBCAM_CONTROL_RESOLUTION = 9,
	USBCAM_CONTROL_ANTIFLICKER = 0x10,
	USBCAM_CONTROL_EVLEVEL = 0x14,
	USBCAM_CONTROL_GET = 0x80000000,
};

static int setControl(u32 id, u32 value) {
	switch (id) {
	case USBCAM_CONTROL_BRIGHTNESS:
	case USBCAM_CONTROL_CONTRAST:
	case USBCAM_CONTROL_SATURATION:
	case USBCAM_CONTROL_SHARPNESS:
		if (value >= 256) {
			return SCE_ERROR_USBCAM_INVALID_ADJUSTMENT;
		}
		if (id == USBCAM_CONTROL_BRIGHTNESS) {
			controls.brightness = value;
		} else if (id == USBCAM_CONTROL_CONTRAST) {
			controls.contrast = value;
		} else if (id == USBCAM_CONTROL_SATURATION) {
			controls.saturation = value;
		} else {
			controls.sharpness = value;
		}
		return 0;
	case USBCAM_CONTROL_ZOOM:
		// The driver also limits it by the resolution, which isn't checked here.
		if (value != 0 && (value < 10 || value > 80)) {
			return SCE_ERROR_USBCAM_OUT_OF_RANGE;
		}
		controls.zoom = value;
		return 0;
	case USBCAM_CONTROL_REVERSE:
		controls.reverseMode = value & 0xFFFF;
		return 0;
	case USBCAM_CONTROL_EFFECT:
		controls.effectMode = value;
		return 0;
	case USBCAM_CONTROL_RESOLUTION:
		return value < 9 ? 0 : SCE_ERROR_USBCAM_OUT_OF_RANGE;
	case USBCAM_CONTROL_ANTIFLICKER:
		if (value >= 3) {
			return SCE_ERROR_USBCAM_OUT_OF_RANGE;
		}
		controls.antiFlicker = value;
		return 0;
	case USBCAM_CONTROL_EVLEVEL:
		if (value >= 17) {
			return SCE_ERROR_USBCAM_OUT_OF_RANGE;
		}
		controls.evLevel = value;
		return 0;
	default:
		return SCE_ERROR_USBCAM_UNKNOWN_CONTROL;
	}
}

static int getControl(u32 id, u32 *value) {
	switch (id) {
	case USBCAM_CONTROL_BRIGHTNESS: *value = controls.brightness; return 0;
	case USBCAM_CONTROL_CONTRAST: *value = controls.contrast; return 0;
	case USBCAM_CONTROL_SATURATION: *value = controls.saturation; return 0;
	case USBCAM_CONTROL_SHARPNESS: *value = controls.sharpness; return 0;
	case USBCAM_CONTROL_ZOOM: *value = controls.zoom; return 0;
	// Bit 16 is the inverse of the lens direction, which we always report as 0.
	case USBCAM_CONTROL_REVERSE: *value = controls.reverseMode | 0x10000; return 0;
	case USBCAM_CONTROL_EFFECT: *value = controls.effectMode <= 6 ? controls.effectMode : 6; return 0;
	case USBCAM_CONTROL_ANTIFLICKER: *value = controls.antiFlicker; return 0;
	case USBCAM_CONTROL_EVLEVEL: *value = controls.evLevel; return 0;
	default: return SCE_ERROR_USBCAM_UNKNOWN_CONTROL;
	}
}

template <u32 id>
static int sceUsbCamSetControl(u32 value) {
	return hleLogDebugOrError(Log::HLE, setControl(id, value));
}

template <u32 id>
static int sceUsbCamGetControl(u32 outAddr) {
	u32 value = 0;
	int result = getControl(id, &value);
	if (result == 0 && Memory::IsValidRange(outAddr, 4)) {
		Memory::WriteUnchecked_U32(value, outAddr);
	}
	return hleLogDebugOrError(Log::HLE, result);
}

static int sceUsbCamSetReverseMode(int reverseflags) {
	return hleLogDebug(Log::HLE, setControl(USBCAM_CONTROL_REVERSE, reverseflags));
}

// Which bytes the generic control call reads or writes for an id, 0 for an unknown id.
static u32 controlSize(u32 id) {
	switch (id) {
	case 0xA: case 0x8000000A:
		return 12;
	case 0xB: case 0xC: case 0x11: case 0x40000001: case 0x40000002:
	case 0x8000000B: case 0x8000000C: case 0x80000011: case 0xC0000001: case 0xC0000002:
		return 8;
	case 0x8001:
		return 0x90;
	case 0xC0000003:
		return 4;
	}
	const u32 index = id & ~USBCAM_CONTROL_GET;
	if ((index >= 1 && index <= 7) || index == 9 || (index >= 0xD && index <= 0x10) || (index >= 0x12 && index <= 0x14)) {
		// 9 is set only.
		return id == (USBCAM_CONTROL_GET | 9) ? 0 : 4;
	}
	return 0;
}

// usbcam.prx's generic control call: bit 31 of the id selects get. Ids beyond the named controls
// (lighting and the like) are accepted and read back as zero.
static int sceUsbCam_1E958148(u32 id, u32 ptr) {
	if (ptr == 0) {
		return hleLogError(Log::HLE, SCE_ERROR_USBCAM_INVALID_POINTER);
	}
	const u32 size = controlSize(id);
	if (size == 0) {
		return hleLogError(Log::HLE, SCE_ERROR_USBCAM_UNKNOWN_CONTROL);
	}
	if (!Memory::IsValidRange(ptr, size)) {
		return hleLogError(Log::HLE, SCE_ERROR_USBCAM_INVALID_ADDRESS);
	}
	if (id & USBCAM_CONTROL_GET) {
		u32 value = 0;
		if (getControl(id & ~USBCAM_CONTROL_GET, &value) != 0) {
			Memory::Memset(ptr, 0, size, "UsbCamControl");
			return hleLogInfo(Log::HLE, 0, "unknown control, reads as zero");
		}
		Memory::WriteUnchecked_U32(value, ptr);
		return hleLogDebug(Log::HLE, 0);
	}
	int result = setControl(id, Memory::ReadUnchecked_U32(ptr));
	if (result == (int)SCE_ERROR_USBCAM_UNKNOWN_CONTROL) {
		return hleLogInfo(Log::HLE, 0, "unknown control, ignored");
	}
	return hleLogDebugOrError(Log::HLE, result);
}

// The camera's version: 1 for one older than 0x500, 2 for a newer one, which sends header packets.
static int sceUsbCam_00631D06() {
	return hleLogDebug(Log::HLE, 1);
}

static int sceUsbCamRegisterLensRotationCallback(u32 cbId) {
	u32 error;
	if (!kernelObjects.Get<PSPCallback>(cbId, error)) {
		return hleLogError(Log::HLE, SCE_ERROR_USBCAM_OUT_OF_RANGE, "not a callback");
	}
	// A second registration fails, and drops the first one too.
	if (lensCallback > 0) {
		lensCallback = -1;
		return hleLogError(Log::HLE, SCE_ERROR_USBCAM_CALLBACK_ALREADY_REGISTERED);
	}
	// The lens never turns here, so the callback is never notified.
	lensCallback = cbId;
	return hleLogDebug(Log::HLE, 0);
}

static int sceUsbCamUnregisterLensRotationCallback() {
	if (lensCallback < 0) {
		return hleLogError(Log::HLE, SCE_ERROR_USBCAM_NO_CALLBACK);
	}
	// Returns the old callback's UID.
	const int old = lensCallback;
	lensCallback = -1;
	return hleLogDebug(Log::HLE, old);
}

const HLEFunction sceUsbCam[] =
{
	{ 0X03ED7A82, &WrapI_UUI<sceUsbCamSetupMic>,              "sceUsbCamSetupMic",                       'i', "xxi" },
	{ 0X2E930264, &WrapI_UUI<sceUsbCamSetupMicEx>,            "sceUsbCamSetupMicEx",                     'i', "xxi" },
	{ 0X82A64030, &WrapI_V<sceUsbCamStartMic>,                "sceUsbCamStartMic",                       'i', "" },
	{ 0X5145868A, &WrapI_V<sceUsbCamStopMic>,                 "sceUsbCamStopMic",                        'i', "" },
	{ 0X36636925, &WrapI_UU<sceUsbCamReadMicBlocking>,        "sceUsbCamReadMicBlocking",                'i', "xx" },
	{ 0X3DC0088E, &WrapI_UU<sceUsbCamReadMic>,                "sceUsbCamReadMic",                        'i', "xx" },
	{ 0XB048A67D, &WrapI_V<sceUsbCamWaitReadMicEnd>,          "sceUsbCamWaitReadMicEnd",                 'i', "" },
	{ 0XF8847F60, nullptr,                                    "sceUsbCamPollReadMicEnd",                 '?', "" },
	{ 0X5778B452, &WrapI_V<sceUsbCamGetMicDataLength>,        "sceUsbCamGetMicDataLength",               'i', "" },
	{ 0X08AEE98A, nullptr,                                    "sceUsbCamSetMicGain",                     '?', "" },

	{ 0X17F7B2FB, &WrapI_UUI<sceUsbCamSetupVideo>,            "sceUsbCamSetupVideo",                     'i', "xxi" },
	{ 0XCFE9E999, &WrapI_UUI<sceUsbCamSetupVideoEx>,          "sceUsbCamSetupVideoEx",                   'i', "xxi" },
	{ 0X574A8C3F, &WrapI_V<sceUsbCamStartVideo>,              "sceUsbCamStartVideo",                     'i', "" },
	{ 0X6CF32CB9, &WrapI_V<sceUsbCamStopVideo>,               "sceUsbCamStopVideo",                      'i', "" },
	{ 0X7DAC0C71, &WrapI_UU<sceUsbCamReadVideoFrameBlocking>, "sceUsbCamReadVideoFrameBlocking",         'i', "xx" },
	{ 0X99D86281, &WrapI_UU<sceUsbCamReadVideoFrame>,         "sceUsbCamReadVideoFrame",                 'i', "xx" },
	{ 0XF90B2293, &WrapI_V<sceUsbCamWaitReadVideoFrameEnd>,   "sceUsbCamWaitReadVideoFrameEnd",          'i', "" },
	{ 0X41E73E95, &WrapI_V<sceUsbCamPollReadVideoFrameEnd>,   "sceUsbCamPollReadVideoFrameEnd",          'i', "" },
	{ 0XDF9D0C92, &WrapI_V<sceUsbCamGetReadVideoFrameSize>,   "sceUsbCamGetReadVideoFrameSize",          'i', "" },

	{ 0X3F0CF289, &WrapI_U<sceUsbCamSetupStill>,              "sceUsbCamSetupStill",                     'i', "x" },
	{ 0X0A41A298, &WrapI_U<sceUsbCamSetupStillEx>,            "sceUsbCamSetupStillEx",                   'i', "x" },
	{ 0X61BE5CAC, &WrapI_UU<sceUsbCamStillInputBlocking>,     "sceUsbCamStillInputBlocking",             'i', "xx" },
	{ 0XFB0A6C5D, &WrapI_UU<sceUsbCamStillInput>,             "sceUsbCamStillInput",                     'i', "xx" },
	{ 0X7563AFA1, &WrapI_V<sceUsbCamStillWaitInputEnd>,       "sceUsbCamStillWaitInputEnd",              'i', "" },
	{ 0X1A46CFE7, &WrapI_V<sceUsbCamStillPollInputEnd>,       "sceUsbCamStillPollInputEnd",              'i', "" },
	{ 0XA720937C, &WrapI_V<sceUsbCamStillCancelInput>,        "sceUsbCamStillCancelInput",               'i', "" },
	{ 0XE5959C36, &WrapI_V<sceUsbCamStillGetInputLength>,     "sceUsbCamStillGetInputLength",            'i', "" },

	{ 0XF93C4669, &WrapI_I<sceUsbCamAutoImageReverseSW>,      "sceUsbCamAutoImageReverseSW",             'i', "i" },
	{ 0X11A1F128, nullptr,                                    "sceUsbCamGetAutoImageReverseState",       '?', "" },
	{ 0X4C34F553, &WrapI_V<sceUsbCamGetLensDirection>,        "sceUsbCamGetLensDirection",               'i', "" },

	{ 0X383E9FA8, &WrapI_U<sceUsbCamGetControl<USBCAM_CONTROL_SATURATION>>,  "sceUsbCamGetSaturation",      'i', "p" },
	{ 0X6E205974, &WrapI_U<sceUsbCamSetControl<USBCAM_CONTROL_SATURATION>>,  "sceUsbCamSetSaturation",      'i', "i" },
	{ 0X70F522C5, &WrapI_U<sceUsbCamGetControl<USBCAM_CONTROL_BRIGHTNESS>>,  "sceUsbCamGetBrightness",      'i', "p" },
	{ 0X4F3D84D5, &WrapI_U<sceUsbCamSetControl<USBCAM_CONTROL_BRIGHTNESS>>,  "sceUsbCamSetBrightness",      'i', "i" },
	{ 0XA063A957, &WrapI_U<sceUsbCamGetControl<USBCAM_CONTROL_CONTRAST>>,    "sceUsbCamGetContrast",        'i', "p" },
	{ 0X09C26C7E, &WrapI_U<sceUsbCamSetControl<USBCAM_CONTROL_CONTRAST>>,    "sceUsbCamSetContrast",        'i', "i" },
	{ 0XFDB68C23, &WrapI_U<sceUsbCamGetControl<USBCAM_CONTROL_SHARPNESS>>,   "sceUsbCamGetSharpness",       'i', "p" },
	{ 0X622F83CC, &WrapI_U<sceUsbCamSetControl<USBCAM_CONTROL_SHARPNESS>>,   "sceUsbCamSetSharpness",       'i', "i" },
	{ 0X994471E0, &WrapI_U<sceUsbCamGetControl<USBCAM_CONTROL_EFFECT>>,      "sceUsbCamGetImageEffectMode", 'i', "p" },
	{ 0XD4876173, &WrapI_U<sceUsbCamSetControl<USBCAM_CONTROL_EFFECT>>,      "sceUsbCamSetImageEffectMode", 'i', "i" },
	{ 0X2BCD50C0, &WrapI_U<sceUsbCamGetControl<USBCAM_CONTROL_EVLEVEL>>,     "sceUsbCamGetEvLevel",         'i', "p" },
	{ 0X1D686870, &WrapI_U<sceUsbCamSetControl<USBCAM_CONTROL_EVLEVEL>>,     "sceUsbCamSetEvLevel",         'i', "i" },
	{ 0XD5279339, &WrapI_U<sceUsbCamGetControl<USBCAM_CONTROL_REVERSE>>,     "sceUsbCamGetReverseMode",     'i', "p" },
	{ 0X951BEDF5, &WrapI_I<sceUsbCamSetReverseMode>,          "sceUsbCamSetReverseMode",                 'i', "i" },
	{ 0X9E8AAF8D, &WrapI_U<sceUsbCamGetControl<USBCAM_CONTROL_ZOOM>>,        "sceUsbCamGetZoom",            'i', "p" },
	{ 0XC484901F, &WrapI_U<sceUsbCamSetControl<USBCAM_CONTROL_ZOOM>>,        "sceUsbCamSetZoom",            'i', "i" },
	{ 0XAA7D94BA, &WrapI_U<sceUsbCamGetControl<USBCAM_CONTROL_ANTIFLICKER>>, "sceUsbCamGetAntiFlicker",     'i', "p" },
	{ 0X6784E6A8, &WrapI_U<sceUsbCamSetControl<USBCAM_CONTROL_ANTIFLICKER>>, "sceUsbCamSetAntiFlicker",     'i', "i" },

	{ 0XD293A100, &WrapI_U<sceUsbCamRegisterLensRotationCallback>,   "sceUsbCamRegisterLensRotationCallback",   'i', "x" },
	{ 0X41EE8797, &WrapI_V<sceUsbCamUnregisterLensRotationCallback>, "sceUsbCamUnregisterLensRotationCallback", 'i', "" },
	{ 0X1E958148, &WrapI_UU<sceUsbCam_1E958148>,              "sceUsbCam_1E958148",                      'i', "xx" },
	{ 0X00631D06, &WrapI_V<sceUsbCam_00631D06>,               "sceUsbCam_00631D06",                      'i', "" },
};

void Register_sceUsbCam()
{
	RegisterHLEModule("sceUsbCam", ARRAY_SIZE(sceUsbCam), sceUsbCam);
}

std::vector<std::string> Camera::getDeviceList() {
#ifdef HAVE_WIN32_CAMERA
	if (winCamera) {
		return winCamera->getDeviceList();
	} else {
		return std::vector<std::string>();
	}
#else
	return System_GetCameraDeviceList();
#endif
}

int Camera::startCapture() {
	int width, height;
	getCameraResolution(config->type, &width, &height);
	INFO_LOG(Log::HLE, "%s resolution: %dx%d", __FUNCTION__, width, height);

	config->mode = Camera::Mode::Video;
	#ifdef HAVE_WIN32_CAMERA
		if (winCamera) {
			if (winCamera->isShutDown()) {
				delete winCamera;
				winCamera = new WindowsCaptureDevice(CAPTUREDEVICE_TYPE::VIDEO);
			}
			void* resolution = static_cast<void*>(new std::vector<int>({ width, height }));
			winCamera->sendMessage({ CAPTUREDEVICE_COMMAND::START, resolution });
		}
	#elif PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(IOS)
		char command[40] = {0};
		snprintf(command, sizeof(command), "startVideo_%dx%d", width, height);
		System_CameraCommand(command);
		ERROR_LOG(Log::HLE, "%s not implemented", __FUNCTION__);
	#endif
	return 0;
}

int Camera::stopCapture() {
	INFO_LOG(Log::HLE, "%s", __FUNCTION__);
	#ifdef HAVE_WIN32_CAMERA
		if (winCamera) {
			winCamera->sendMessage({ CAPTUREDEVICE_COMMAND::STOP, nullptr });
		}
	#elif PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(IOS)
		System_CameraCommand("stopVideo");
	#else
		ERROR_LOG(Log::HLE, "%s not implemented", __FUNCTION__);
	#endif
	config->mode = Camera::Mode::Unused;
	return 0;
}

void Camera::onCameraDeviceChange() {
	if (config != nullptr && config->mode == Camera::Mode::Video) {
		stopCapture();
		startCapture();
	}
}

// Called from the capture thread.
int Camera::getMaxFrameSize() {
	std::lock_guard<std::mutex> lock(videoBufferMutex);
	int framesize = 0;
	if (config) {
		if (config->type == Camera::ConfigType::CfVideoEx) {
			framesize = config->videoExParam.framesize;
		} else if (config->type == Camera::ConfigType::CfVideo) {
			framesize = config->videoParam.framesize;
		}
	}
	if (framesize <= 0 || framesize > VIDEO_BUFFER_SIZE) {
		return VIDEO_BUFFER_SIZE;
	}
	return framesize;
}

// The JPEG quality that fit the last frame. Frames of one scene are similar in size, so it's usually
// right first time. Only the capture thread uses it, but atomic in case a platform has several.
static std::atomic<int> g_jpegQuality{ 80 };

int Camera::encodeToFit(int maxSize, const std::function<int(int quality)> &encode) {
	int quality = g_jpegQuality;
	int size = encode(quality);
	while ((size < 0 || size > maxSize) && quality > 10) {
		quality = std::max(10, quality - 10);
		size = encode(quality);
	}
	// Way under the limit: try a better quality next time.
	if (size >= 0 && size < maxSize / 2 && quality < 90) {
		quality += 10;
	}
	g_jpegQuality = quality;
	return size;
}

// Re-encodes a frame until it fits maxSize, like the PSP camera compresses to the game's framesize.
// Most platforms' capture code encodes at a fixed quality, so this is the common fallback.
static bool RecompressToFit(const unsigned char *image, long long length, int maxSize, std::vector<uint8_t> *out) {
	int width = 0, height = 0, comps = 0;
	unsigned char *rgb = jpgd::decompress_jpeg_image_from_memory(image, (int)length, &width, &height, &comps, 3);
	if (!rgb) {
		return false;
	}
	out->resize(width * height * 3 + 1024);
	int size = Camera::encodeToFit(maxSize, [&](int quality) {
		jpge::params params;
		params.m_quality = quality;
		int outSize = (int)out->size();
		return jpge::compress_image_to_jpeg_file_in_memory(out->data(), outSize, width, height, 3, rgb, params) ? outSize : -1;
	});
	free(rgb);
	if (size < 0 || size > maxSize) {
		return false;
	}
	out->resize(size);
	return true;
}

void Camera::pushCameraImage(long long length, unsigned char* image) {
	std::vector<uint8_t> recompressed;
	const int maxSize = getMaxFrameSize();
	if (length > maxSize && RecompressToFit(image, length, maxSize, &recompressed)) {
		image = recompressed.data();
		length = (long long)recompressed.size();
	}

	std::lock_guard<std::mutex> lock(videoBufferMutex);
	if (!videoBuffer) {
		return;
	}
	memset(videoBuffer, 0, VIDEO_BUFFER_SIZE);
	if (placeholder != Placeholder::None) {
		INFO_LOG(Log::HLE, "Camera: first frame");
	}
	placeholder = Placeholder::None;
	if (length > VIDEO_BUFFER_SIZE) {
		videoBufferLength = 0;
		ERROR_LOG(Log::HLE, "pushCameraImage: length error: %lld > %d", length, VIDEO_BUFFER_SIZE);
	} else {
		videoBufferLength = length;
		memcpy(videoBuffer, image, length);
	}
}
