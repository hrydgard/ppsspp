// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

// This code implements the emulated audio using CoreAudio for iOS
// Originally written by jtraynham

#include "iOSCoreAudio.h"

#include <algorithm>
#include <atomic>
#include <cstring>

#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/HLE/sceUsbMic.h"

#include <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>

#define SAMPLE_RATE 44100

static AudioComponentInstance audioInstance = nil;

// Microphone input comes from the input side of the same RemoteIO unit, which is how iOS wants it.
// Turning it on or off means reinitializing the unit, so playback glitches for a moment.
// Everything here except the ring buffer is only touched on the main thread.
static bool g_micWanted = false;   // The game is recording.
static bool g_micEnabled = false;  // ...and we have permission, so the unit should have its input on.
static int g_micSampleRate = SAMPLE_RATE;
static bool g_micInputOn = false;  // What the unit is actually configured with.
static int g_micInputRate = 0;

// Single producer (the input callback, on the audio thread), single consumer (the CPU thread, in
// iOSCoreAudioPollRecording). Input that doesn't fit is dropped.
static constexpr uint32_t MIC_RING_SIZE = 32768;  // Samples, a power of two. 0.7 s at 44.1 kHz.
static int16_t g_micRing[MIC_RING_SIZE];
static std::atomic<uint32_t> g_micWritePos{0};
static std::atomic<uint32_t> g_micReadPos{0};

// Where AudioUnitRender puts the input. The unit's slice limit is set to match, so the audio thread
// never has to allocate.
static constexpr UInt32 MIC_MAX_FRAMES = 4096;
static int16_t g_micScratch[MIC_MAX_FRAMES];

void iOSCoreAudioUpdateSession() {
	NSError *error = nil;
	INFO_LOG(Log::Audio, "RespectSilentMode: %d MixWithOthers: %d Recording: %d", g_Config.bAudioRespectSilentMode, g_Config.bAudioMixWithOthers, g_micEnabled);

	// Hacky hack to force iOS to re-evaluate.
	// Switching from CatogoryPlayback to CategoryPlayback with an option otherwise does nothing.
	[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryAudioProcessing error:&error];

	// Here, we apply the settings.
	if (g_micEnabled) {
		// Recording needs PlayAndRecord, which ignores the silent switch. A2DP rather than the
		// hands-free profile keeps Bluetooth headphones at full quality, with the built-in mic as input.
		AVAudioSessionCategoryOptions options = AVAudioSessionCategoryOptionDefaultToSpeaker | AVAudioSessionCategoryOptionAllowBluetoothA2DP;
		if (g_Config.bAudioMixWithOthers) {
			options |= AVAudioSessionCategoryOptionMixWithOthers;
		}
		[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayAndRecord withOptions:options error:&error];
	} else if (g_Config.bAudioMixWithOthers) {
		if (g_Config.bAudioRespectSilentMode) {
			[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryAmbient error:&error];
		} else {
			[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayback withOptions:AVAudioSessionCategoryOptionMixWithOthers error:&error];
		}
	} else {
		if (g_Config.bAudioRespectSilentMode) {
			[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategorySoloAmbient error:&error];
		} else {
			[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayback withOptions:0 error:&error];
		}
		// Can't achieve exclusive + respect silent mode
	}

	if (error) {
		NSLog(@"%@", error);
	}
}

void NativeMix(short *audio, int numSamples, int sampleRateHz, void *userdata);

OSStatus iOSCoreAudioCallback(void *inRefCon,
							  AudioUnitRenderActionFlags *ioActionFlags,
							  const AudioTimeStamp *inTimeStamp,
							  UInt32 inBusNumber,
							  UInt32 inNumberFrames,
							  AudioBufferList *ioData) {
	short *output = (short *)ioData->mBuffers[0].mData;
	if (g_Config.bEnableSound) {
		NativeMix(output, inNumberFrames, SAMPLE_RATE, nullptr);
	} else {
		// The unit only runs for the microphone.
		memset(output, 0, inNumberFrames * sizeof(short) * 2);
		*ioActionFlags |= kAudioUnitRenderAction_OutputIsSilence;
	}
	ioData->mBuffers[0].mDataByteSize = inNumberFrames * sizeof(short) * 2;
	return noErr;
}

// Called on the audio thread when input is ready. We have to pull it out ourselves.
static OSStatus iOSCoreAudioInputCallback(void *inRefCon,
										  AudioUnitRenderActionFlags *ioActionFlags,
										  const AudioTimeStamp *inTimeStamp,
										  UInt32 inBusNumber,
										  UInt32 inNumberFrames,
										  AudioBufferList *ioData) {
	if (inNumberFrames > MIC_MAX_FRAMES) {
		return kAudioUnitErr_TooManyFramesToProcess;
	}

	AudioBufferList bufferList;
	bufferList.mNumberBuffers = 1;
	bufferList.mBuffers[0].mNumberChannels = 1;
	bufferList.mBuffers[0].mDataByteSize = inNumberFrames * sizeof(int16_t);
	bufferList.mBuffers[0].mData = g_micScratch;

	AudioComponentInstance unit = (AudioComponentInstance)inRefCon;
	OSStatus err = AudioUnitRender(unit, ioActionFlags, inTimeStamp, inBusNumber, inNumberFrames, &bufferList);
	if (err != noErr) {
		return err;
	}

	const UInt32 frames = bufferList.mBuffers[0].mDataByteSize / sizeof(int16_t);
	const uint32_t write = g_micWritePos.load(std::memory_order_relaxed);
	const uint32_t read = g_micReadPos.load(std::memory_order_acquire);
	const uint32_t count = std::min((uint32_t)frames, MIC_RING_SIZE - (write - read));
	for (uint32_t i = 0; i < count; i++) {
		g_micRing[(write + i) & (MIC_RING_SIZE - 1)] = g_micScratch[i];
	}
	g_micWritePos.store(write + count, std::memory_order_release);
	return noErr;
}

// Needs the unit uninitialized.
static void ConfigureInput(AudioComponentInstance unit, bool enable) {
	OSStatus err;
	UInt32 enableIO = enable ? 1 : 0;
	err = AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &enableIO, sizeof(enableIO));
	if (err != noErr) {
		ERROR_LOG(Log::Audio, "Failed to %s RemoteIO input: %d", enable ? "enable" : "disable", (int)err);
		enable = false;
	}
	g_micInputOn = enable;
	if (!enable) {
		return;
	}

	// Mono 16-bit at the rate the game asked for. RemoteIO converts from the hardware format.
	AudioStreamBasicDescription format{};
	format.mSampleRate = g_micSampleRate;
	format.mFormatID = kAudioFormatLinearPCM;
	format.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
	format.mBitsPerChannel = 16;
	format.mChannelsPerFrame = 1;
	format.mFramesPerPacket = 1;
	format.mBytesPerFrame = 2;
	format.mBytesPerPacket = 2;
	err = AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &format, sizeof(format));
	if (err != noErr) {
		ERROR_LOG(Log::Audio, "Failed to set the microphone format: %d", (int)err);
	}

	AURenderCallbackStruct callback{};
	callback.inputProc = iOSCoreAudioInputCallback;
	callback.inputProcRefCon = (void *)unit;
	err = AudioUnitSetProperty(unit, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 1, &callback, sizeof(callback));
	if (err != noErr) {
		ERROR_LOG(Log::Audio, "Failed to set the microphone callback: %d", (int)err);
	}
	g_micInputRate = g_micSampleRate;
	INFO_LOG(Log::Audio, "Microphone input on, at %d Hz", g_micSampleRate);
}

void iOSCoreAudioInit() {
	if (!g_Config.bEnableSound && !g_micEnabled) {
		return;
	}

	iOSCoreAudioUpdateSession();

	NSError *error = nil;
	AVAudioSession *session = [AVAudioSession sharedInstance];
	if (![session setActive:YES error:&error]) {
		ERROR_LOG(Log::System, "Failed to activate AVFoundation audio session");
		if (error.localizedDescription) {
			NSLog(@"%@", error.localizedDescription);
		}
		if (error.localizedFailureReason) {
			NSLog(@"%@", error.localizedFailureReason);
		}
	}

	if (audioInstance) {
		// Already running
		return;
	}
	OSErr err;

	// first, grab the default output
	AudioComponentDescription defaultOutputDescription;
	defaultOutputDescription.componentType = kAudioUnitType_Output;
	defaultOutputDescription.componentSubType = kAudioUnitSubType_RemoteIO;
	defaultOutputDescription.componentManufacturer = kAudioUnitManufacturer_Apple;
	defaultOutputDescription.componentFlags = 0;
	defaultOutputDescription.componentFlagsMask = 0;
	AudioComponent defaultOutput = AudioComponentFindNext(NULL, &defaultOutputDescription);

	// create our instance
	err = AudioComponentInstanceNew(defaultOutput, &audioInstance);
	if (err != noErr) {
		audioInstance = nil;
		return;
	}

	// create our callback so we can give it the audio data
	AURenderCallbackStruct input;
	input.inputProc = iOSCoreAudioCallback;
	input.inputProcRefCon = NULL;
	err = AudioUnitSetProperty(audioInstance,
								kAudioUnitProperty_SetRenderCallback,
								kAudioUnitScope_Input,
								0,
								&input,
								sizeof(input));
	if (err != noErr) {
		AudioComponentInstanceDispose(audioInstance);
		audioInstance = nil;
		return;
	}

	// setup the audio format we'll be using (stereo pcm)
	AudioStreamBasicDescription streamFormat;
	memset(&streamFormat, 0, sizeof(streamFormat));
	streamFormat.mSampleRate = SAMPLE_RATE;
	streamFormat.mFormatID = kAudioFormatLinearPCM;
	streamFormat.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
	streamFormat.mBitsPerChannel = sizeof(short) * 8;
	streamFormat.mChannelsPerFrame = 2;
	streamFormat.mFramesPerPacket = 1;
	streamFormat.mBytesPerFrame = (streamFormat.mBitsPerChannel / 8) * streamFormat.mChannelsPerFrame;
	streamFormat.mBytesPerPacket = streamFormat.mBytesPerFrame * streamFormat.mFramesPerPacket;
	err = AudioUnitSetProperty(audioInstance,
								kAudioUnitProperty_StreamFormat,
								kAudioUnitScope_Input,
								0,
								&streamFormat,
								sizeof(AudioStreamBasicDescription));
	if (err != noErr) {
		AudioComponentInstanceDispose(audioInstance);
		audioInstance = nil;
		return;
	}

	UInt32 maxFrames = MIC_MAX_FRAMES;
	AudioUnitSetProperty(audioInstance, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxFrames, sizeof(maxFrames));

	// After an interruption or a trip to the background, this brings the microphone back.
	ConfigureInput(audioInstance, g_micEnabled);

	// k, all setup, so init
	err = AudioUnitInitialize(audioInstance);
	if (err != noErr) {
		AudioComponentInstanceDispose(audioInstance);
		audioInstance = nil;
		g_micInputOn = false;
		return;
	}

	// finally start playback
	err = AudioOutputUnitStart(audioInstance);
	if (err != noErr) {
		AudioUnitUninitialize(audioInstance);
		AudioComponentInstanceDispose(audioInstance);
		audioInstance = nil;
		g_micInputOn = false;
		return;
	}

	// we're good to go
}

void iOSCoreAudioShutdown()
{
	if (audioInstance) {
		AudioOutputUnitStop(audioInstance);
		AudioUnitUninitialize(audioInstance);
		AudioComponentInstanceDispose(audioInstance);
		audioInstance = nil;
	}
	// g_micWanted and g_micEnabled stay, so the next iOSCoreAudioInit turns the input back on.
	g_micInputOn = false;
}

// Brings the unit in line with g_micEnabled. Main thread.
static void UpdateMicrophone() {
	iOSCoreAudioUpdateSession();
	if (!audioInstance) {
		iOSCoreAudioInit();
		return;
	}
	if (!g_Config.bEnableSound && !g_micEnabled) {
		// The unit was only running for the microphone.
		iOSCoreAudioShutdown();
		return;
	}
	if (g_micEnabled == g_micInputOn && (!g_micEnabled || g_micSampleRate == g_micInputRate)) {
		return;
	}

	AudioOutputUnitStop(audioInstance);
	AudioUnitUninitialize(audioInstance);
	ConfigureInput(audioInstance, g_micEnabled);
	OSStatus err = AudioUnitInitialize(audioInstance);
	if (err == noErr) {
		err = AudioOutputUnitStart(audioInstance);
	}
	if (err != noErr) {
		ERROR_LOG(Log::Audio, "Failed to restart the audio unit: %d", (int)err);
		iOSCoreAudioShutdown();
	}
}

// Calls back on the main thread.
static void WithRecordPermission(void (^then)(bool granted)) {
	if (@available(iOS 17.0, *)) {
		switch ([AVAudioApplication sharedInstance].recordPermission) {
		case AVAudioApplicationRecordPermissionGranted:
			then(true);
			break;
		case AVAudioApplicationRecordPermissionDenied:
			then(false);
			break;
		default:
			[AVAudioApplication requestRecordPermissionWithCompletionHandler:^(BOOL granted) {
				dispatch_async(dispatch_get_main_queue(), ^{
					then(granted);
				});
			}];
			break;
		}
	} else {
		AVAudioSession *session = [AVAudioSession sharedInstance];
		switch (session.recordPermission) {
		case AVAudioSessionRecordPermissionGranted:
			then(true);
			break;
		case AVAudioSessionRecordPermissionDenied:
			then(false);
			break;
		default:
			[session requestRecordPermission:^(BOOL granted) {
				dispatch_async(dispatch_get_main_queue(), ^{
					then(granted);
				});
			}];
			break;
		}
	}
}

void iOSCoreAudioStartRecording(int sampleRate) {
	// Whatever is left over is from before, maybe at another rate. We're the consumer, so we can drop it.
	g_micReadPos.store(g_micWritePos.load(std::memory_order_acquire), std::memory_order_release);

	dispatch_async(dispatch_get_main_queue(), ^{
		g_micWanted = true;
		g_micSampleRate = sampleRate;
		WithRecordPermission(^(bool granted) {
			if (!g_micWanted) {
				// Stopped while we were asking.
				return;
			}
			if (!granted) {
				WARN_LOG(Log::Audio, "No microphone permission, the game will get silence");
			}
			g_micEnabled = granted;
			UpdateMicrophone();
		});
	});
}

void iOSCoreAudioStopRecording() {
	dispatch_async(dispatch_get_main_queue(), ^{
		g_micWanted = false;
		g_micEnabled = false;
		UpdateMicrophone();
	});
}

void iOSCoreAudioPollRecording() {
	const uint32_t read = g_micReadPos.load(std::memory_order_relaxed);
	const uint32_t write = g_micWritePos.load(std::memory_order_acquire);
	const uint32_t count = write - read;
	if (count == 0) {
		return;
	}
	// In up to two pieces, if it wraps around the end of the ring.
	const uint32_t start = read & (MIC_RING_SIZE - 1);
	const uint32_t first = std::min(count, MIC_RING_SIZE - start);
	Microphone::addAudioData((u8 *)&g_micRing[start], first * sizeof(int16_t));
	if (count > first) {
		Microphone::addAudioData((u8 *)g_micRing, (count - first) * sizeof(int16_t));
	}
	g_micReadPos.store(read + count, std::memory_order_release);
}
