
#include <unistd.h>
#include <cmath>

#include <wiiu/os/cache.h>
#include <wiiu/os/foreground.h>
#include <wiiu/os/debug.h>
#include <wiiu/ax/core.h>
#include <wiiu/ax/multivoice.h>
#include <wiiu/vpad.h>
#include <wiiu/kpad.h>
#include <wiiu/procui.h>

#include "Common/Log.h"
#include "UI/OnScreenDisplay.h"
#include "Common/System/System.h"
#include "Common/System/NativeApp.h"
#include "Common/Input/InputState.h"

#include "WiiU/WiiUHost.h"
#include "WiiU/GX2GraphicsContext.h"

static void VPADRangeToPSPRange(VPADVec2D *vec) {
	if (vec->x == 0.0 || vec->y == 0.0) {
		return;
	}
	float x = fabs(vec->x);
	float y = fabs(vec->y);
	float phi = atanf(y / x);
	float scale = (x > y) ? 1.0 / cosf(phi) : 1.0 / sinf(phi);
	vec->x *= scale;
	vec->y *= scale;
}

static void VPADCallback(s32 chan) {
	static keycode_t keymap[] = {
		NKCODE_UNKNOWN,       // VPAD_BUTTON_SYNC_BIT             = 0,
		NKCODE_HOME,          // VPAD_BUTTON_HOME_BIT             = 1,
		NKCODE_BUTTON_SELECT, // VPAD_BUTTON_MINUS_BIT            = 2,
		NKCODE_BUTTON_START,  // VPAD_BUTTON_PLUS_BIT             = 3,
		NKCODE_BUTTON_R1,     // VPAD_BUTTON_R_BIT                = 4,
		NKCODE_BUTTON_L1,     // VPAD_BUTTON_L_BIT                = 5,
		NKCODE_BUTTON_R2,     // VPAD_BUTTON_ZR_BIT               = 6,
		NKCODE_BUTTON_L2,     // VPAD_BUTTON_ZL_BIT               = 7,
		NKCODE_DPAD_DOWN,     // VPAD_BUTTON_DOWN_BIT             = 8,
		NKCODE_DPAD_UP,       // VPAD_BUTTON_UP_BIT               = 9,
		NKCODE_DPAD_RIGHT,    // VPAD_BUTTON_RIGHT_BIT            = 10,
		NKCODE_DPAD_LEFT,     // VPAD_BUTTON_LEFT_BIT             = 11,
		NKCODE_BUTTON_Y,      // VPAD_BUTTON_Y_BIT                = 12,
		NKCODE_BUTTON_X,      // VPAD_BUTTON_X_BIT                = 13,
		NKCODE_BUTTON_B,      // VPAD_BUTTON_B_BIT                = 14,
		NKCODE_BUTTON_A,      // VPAD_BUTTON_A_BIT                = 15,
		NKCODE_UNKNOWN,       // VPAD_BUTTON_TV_BIT               = 16,
		NKCODE_BUTTON_THUMBR, // VPAD_BUTTON_STICK_R_BIT          = 17,
		NKCODE_BUTTON_THUMBL, // VPAD_BUTTON_STICK_L_BIT          = 18,
		NKCODE_UNKNOWN,       // VPAD_BUTTON_TOUCH_BIT            = 19,
		NKCODE_UNKNOWN,       // VPAD_BUTTON_UNUSED1_BIT          = 20,
		NKCODE_UNKNOWN,       // VPAD_BUTTON_UNUSED2_BIT          = 21,
		NKCODE_UNKNOWN,       // VPAD_BUTTON_UNUSED3_BIT          = 22,
		NKCODE_UNKNOWN,       // VPAD_STICK_R_EMULATION_DOWN_BIT  = 23,
		NKCODE_UNKNOWN,       // VPAD_STICK_R_EMULATION_UP_BIT    = 24,
		NKCODE_UNKNOWN,       // VPAD_STICK_R_EMULATION_RIGHT_BIT = 25,
		NKCODE_UNKNOWN,       // VPAD_STICK_R_EMULATION_LEFT_BIT  = 26,
		NKCODE_UNKNOWN,       // VPAD_STICK_L_EMULATION_DOWN_BIT  = 27,
		NKCODE_UNKNOWN,       // VPAD_STICK_L_EMULATION_UP_BIT    = 28,
		NKCODE_UNKNOWN,       // VPAD_STICK_L_EMULATION_RIGHT_BIT = 29,
		NKCODE_UNKNOWN,       // VPAD_STICK_L_EMULATION_LEFT_BIT  = 30,
	};

	VPADStatus vpad;
	VPADReadError readError;
	VPADRead(chan, &vpad, 1, &readError);

	if (!readError) {
		static int touchflags;
		if (vpad.tpFiltered1.validity != VPAD_VALID) {
			vpad.tpFiltered1.touched = false;
		}
		if (touchflags == TOUCH_DOWN || touchflags == TOUCH_MOVE) {
			touchflags = vpad.tpFiltered1.touched ? TOUCH_MOVE : TOUCH_UP;
		} else {
			touchflags = vpad.tpFiltered1.touched ? TOUCH_DOWN : 0;
		}
		if (touchflags) {
			VPADTouchData calibrated;
			VPADGetTPCalibratedPointEx(chan, VPAD_TOUCH_RESOLUTION_854x480, &calibrated, &vpad.tpFiltered1);
			NativeTouch({ (float)calibrated.x, (float)calibrated.y, 0, touchflags });
		}
		for (int i = 0; i < countof(keymap); i++) {
			if (keymap[i] == NKCODE_UNKNOWN)
				continue;

			if ((vpad.trigger | vpad.release) & (1 << i)) {
				NativeKey({ DEVICE_ID_PAD_0 + chan, keymap[i], (vpad.trigger & (1 << i)) ? KEY_DOWN : KEY_UP });
			}
		}

		static VPADVec2D prevLeftStick, prevRightStick;
		if (prevLeftStick.x != vpad.leftStick.x || prevLeftStick.y != vpad.leftStick.y) {
			prevLeftStick = vpad.leftStick;
			VPADRangeToPSPRange(&vpad.leftStick);
			NativeAxis({ DEVICE_ID_PAD_0 + chan, JOYSTICK_OUYA_AXIS_LS_X, vpad.leftStick.x });
			NativeAxis({ DEVICE_ID_PAD_0 + chan, JOYSTICK_OUYA_AXIS_LS_Y, vpad.leftStick.y });
		}
		if (prevRightStick.x != vpad.rightStick.x || prevRightStick.y != vpad.rightStick.y) {
			prevRightStick = vpad.rightStick;
			VPADRangeToPSPRange(&vpad.rightStick);
			NativeAxis({ DEVICE_ID_PAD_0 + chan, JOYSTICK_OUYA_AXIS_RS_X, vpad.rightStick.x });
			NativeAxis({ DEVICE_ID_PAD_0 + chan, JOYSTICK_OUYA_AXIS_RS_Y, vpad.rightStick.y });
		}
#if 1
		if (vpad.trigger & VPAD_BUTTON_ZL) {
			System_SendMessage("finish", "");
		}
		if (vpad.trigger & VPAD_BUTTON_STICK_L) {
			extern bool g_TakeScreenshot;
			g_TakeScreenshot = true;
		}
#endif
	}
}

static void SaveCallback(void) { OSSavesDone_ReadyToRelease(); }

WiiUHost::WiiUHost() {
	ProcUIInit(&SaveCallback);
	VPADInit();
	WPADEnableURCC(true);
	WPADEnableWiiRemote(true);
	KPADInit();
	VPADSetSamplingCallback(0, VPADCallback);

	chdir("sd:/ppsspp/"); // probably useless...
}

WiiUHost::~WiiUHost() {
	VPADSetSamplingCallback(0, nullptr);
	ProcUIShutdown();
}

bool WiiUHost::InitGraphics(std::string *error_message, GraphicsContext **ctx) {
	if (ctx_.Init()) {
		*ctx = &ctx_;
		return true;
	} else {
		*ctx = nullptr;
		return false;
	}
}

void WiiUHost::ShutdownGraphics() { ctx_.Shutdown(); }

#define AX_FRAMES 2
static_assert(!(AX_FRAMES & (AX_FRAMES - 1)), "AX_FRAMES must be a power of two");

static AXMVoice *mvoice;
static s16 __attribute__((aligned(64))) axBuffers[2][AX_FRAMES][AX_FRAME_SIZE];

static void AXCallback() {
	static s16 mixBuffer[AX_FRAME_SIZE * 2];
	static int pos;
#if 1
	AXVoiceOffsets offsets;
	AXGetVoiceOffsets(mvoice->v[0], &offsets);
	if ((offsets.currentOffset / AX_FRAME_SIZE) == pos) {
		pos = ((offsets.currentOffset / AX_FRAME_SIZE) + (AX_FRAMES >> 1)) & (AX_FRAMES - 1);
	}
#endif

	int count = NativeMix(mixBuffer, AX_FRAME_SIZE);
	int extra = AX_FRAME_SIZE - count;

	const s16 *src = mixBuffer;
	s16 *dst_l = axBuffers[0][pos];
	s16 *dst_r = axBuffers[1][pos];

	while (count--) {
		*dst_l++ = *src++;
		*dst_r++ = *src++;
	}
	while (extra--) {
		*dst_l++ = 0;
		*dst_r++ = 0;
	}

	DCStoreRangeNoSync(axBuffers[0][pos], AX_FRAME_SIZE * sizeof(s16));
	DCStoreRangeNoSync(axBuffers[1][pos], AX_FRAME_SIZE * sizeof(s16));

	pos++;
	pos &= AX_FRAMES - 1;
}

void WiiUHost::InitSound() {
	if (InitSoundRefCount_++)
		return;

	AXInitParams initParams = { AX_INIT_RENDERER_48KHZ };
	AXInitWithParams(&initParams);

	AXMVoiceParams mVoiceParams = {};
	mVoiceParams.count = 2;
	AXAcquireMultiVoice(31, NULL, 0, &mVoiceParams, &mvoice);

	if (mvoice && mvoice->channels == 2) {
		AXVoiceOffsets offsets[2];
		offsets[0].currentOffset = AX_FRAME_SIZE;
		offsets[0].loopOffset = 0;
		offsets[0].endOffset = (AX_FRAMES * AX_FRAME_SIZE) - 1;
		offsets[0].loopingEnabled = AX_VOICE_LOOP_ENABLED;
		offsets[0].dataType = AX_VOICE_FORMAT_LPCM16;
		offsets[0].data = axBuffers[0];

		offsets[1] = offsets[0];
		offsets[1].data = axBuffers[1];
		AXSetMultiVoiceOffsets(mvoice, offsets);

		AXSetMultiVoiceSrcType(mvoice, AX_VOICE_SRC_TYPE_NONE);
		AXSetMultiVoiceSrcRatio(mvoice, 1.0f);
		AXVoiceVeData ve = { 0x8000, 0 };
		AXSetMultiVoiceVe(mvoice, &ve);

		AXSetMultiVoiceDeviceMix(mvoice, AX_DEVICE_TYPE_DRC, 0, 0, 0x8000, 0);
		AXSetMultiVoiceDeviceMix(mvoice, AX_DEVICE_TYPE_TV, 0, 0, 0x8000, 0);
	}

	AXRegisterFrameCallback(AXCallback);
	AXSetMultiVoiceState(mvoice, AX_VOICE_STATE_PLAYING);
}

void WiiUHost::ShutdownSound() {
	if (--InitSoundRefCount_)
		return;

	AXSetMultiVoiceState(mvoice, AX_VOICE_STATE_STOPPED);
	AXRegisterFrameCallback(NULL);

	AXFreeMultiVoice(mvoice);
	AXQuit();
}

void WiiUHost::NotifyUserMessage(const std::string &message, float duration, u32 color, const char *id) { osm.Show(message, duration, color, -1, true, id); }

void WiiUHost::SendUIMessage(const std::string &message, const std::string &value) { NativeMessageReceived(message.c_str(), value.c_str()); }
