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
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspaudio.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <string.h>

PSP_MODULE_INFO("PSP Microphone Demo", 0, 1, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
#define BLOCK 1024
#define RATE 44100
#define MAX_BLOCKS (RATE * 60 / BLOCK)
static short samples[BLOCK] __attribute__((aligned(64)));
static short recording[MAX_BLOCKS][BLOCK] __attribute__((aligned(64)));
enum { READY, RECORDING, PLAYING };

int main(void) {
	pspDebugScreenInit();
	sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);
	int init = sceAudioInputInit(0, 4096, 0);
	int channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, BLOCK, PSP_AUDIO_FORMAT_MONO);
	int state = READY, blocks = 0, position = 0, result = 0, peak = 0;
	unsigned previous = 0;
	for (;;) {
		SceCtrlData pad;
		sceCtrlPeekBufferPositive(&pad, 1);
		unsigned pressed = pad.Buttons & ~previous;
		previous = pad.Buttons;
		if (pressed & PSP_CTRL_START) {
			break;
		}
		if (pressed & PSP_CTRL_CROSS) {
			if (state == RECORDING) {
				position = 0;
				state = blocks && channel >= 0 ? PLAYING : READY;
			} else {
				blocks = 0;
				position = 0;
				state = RECORDING;
			}
			pspDebugScreenClear();
		}
		pspDebugScreenSetXY(0, 0);
		pspDebugScreenSetTextColor(0xFFFFFFFF);
		pspDebugScreenPrintf("PSP MICROPHONE RECORDER\n\n");
		pspDebugScreenSetTextColor(state == PLAYING ? 0xFF00FF00 :
								   state == RECORDING ? 0xFF4040FF : 0xFFFFFFFF);
		pspDebugScreenPrintf("%s                  \n\n", state == PLAYING ? "Play" :
							 state == RECORDING ? "Recording" : "Ready");
		pspDebugScreenSetTextColor(0xFFFFFFFF);
		int elapsed = (state == PLAYING ? position : blocks) * BLOCK / RATE;
		int duration = blocks * BLOCK / RATE;
		pspDebugScreenPrintf("Time: %02d:%02d / %02d:%02d       \n"
			"Peak: %5d / 32768       \n\n"
			"X: record / X again: play\n"
			"X during playback: new recording\n"
			"Start: exit\n\n"
			"Maximum recording: 60 seconds\n"
			"44100 Hz / mono / signed 16-bit PCM\n\n"
			"Init: 0x%08X\nAudio channel: %d\nResult: 0x%08X       \n",
			elapsed / 60, elapsed % 60, duration / 60, duration % 60,
			peak, (unsigned)init, channel, (unsigned)result);
		if (state == PLAYING) {
			result = sceAudioOutputBlocking(channel, PSP_AUDIO_VOLUME_MAX, recording[position]);
			if (result < 0 || ++position >= blocks) {
				state = READY;
				pspDebugScreenClear();
			}
		} else {
			result = sceAudioInputBlocking(BLOCK, RATE, samples);
			peak = 0;
			if (result >= 0) {
				for (int i = 0; i < BLOCK; i++) {
					int value = samples[i];
					if (value < 0) {
						value = -value;
					}
					if (value > peak) {
						peak = value;
					}
				}
				if (state == RECORDING) {
					memcpy(recording[blocks++], samples, sizeof(samples));
					if (blocks >= MAX_BLOCKS) {
						position = 0;
						state = channel >= 0 ? PLAYING : READY;
						pspDebugScreenClear();
					}
				}
			} else if (state == RECORDING) {
				state = READY;
				pspDebugScreenClear();
			}
		}
		/* Blocking PSP audio calls already pace capture/playback at 44100 Hz. */
	}
	if (channel >= 0) {
		sceAudioChRelease(channel);
	}
	sceKernelExitGame();
	return 0;
}
