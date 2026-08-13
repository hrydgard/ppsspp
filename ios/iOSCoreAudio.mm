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

#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/HLE/sceUsbMic.h"

#include <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>

#define SAMPLE_RATE 44100

static AudioComponentInstance audioInstance = nil;

// Microphone recording state. The RemoteIO unit is reconfigured in place to add/remove
// the input (mic) side rather than using a second unit - see iOSCoreAudioStartRecordingUnit.
static bool g_recordingActive = false;
static int g_recordingSampleRate = SAMPLE_RATE;
static int16_t *g_recordScratch = nullptr;
static UInt32 g_recordScratchFrames = 0;

void iOSCoreAudioUpdateSession() {
	NSError *error = nil;
	INFO_LOG(Log::Audio, "RespectSilentMode: %d MixWithOthers: %d Recording: %d", g_Config.bAudioRespectSilentMode, g_Config.bAudioMixWithOthers, g_recordingActive);

	// Hacky hack to force iOS to re-evaluate.
	// Switching from CatogoryPlayback to CategoryPlayback with an option otherwise does nothing.
	[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryAudioProcessing error:&error];

	// Here, we apply the settings.
	if (g_recordingActive) {
		// Recording requires PlayAndRecord - silent mode / exclusive playback aren't selectable while active.
		AVAudioSessionCategoryOptions options = AVAudioSessionCategoryOptionDefaultToSpeaker | AVAudioSessionCategoryOptionAllowBluetooth;
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
	NativeMix(output, inNumberFrames, SAMPLE_RATE, nullptr);
	ioData->mBuffers[0].mDataByteSize = inNumberFrames * sizeof(short) * 2;
	return noErr;
}

void iOSCoreAudioInit() {
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

	// k, all setup, so init
	err = AudioUnitInitialize(audioInstance);
	if (err != noErr) {
		AudioComponentInstanceDispose(audioInstance);
		audioInstance = nil;
		return;
	}

	// finally start playback
	err = AudioOutputUnitStart(audioInstance);
	if (err != noErr) {
		AudioUnitUninitialize(audioInstance);
		AudioComponentInstanceDispose(audioInstance);
		audioInstance = nil;
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
	g_recordingActive = false;
	delete[] g_recordScratch;
	g_recordScratch = nullptr;
	g_recordScratchFrames = 0;
}

// Pulled by the RemoteIO unit whenever a new block of mic input is available on bus 1.
// We have to call AudioUnitRender ourselves to actually get the samples - the callback
// only tells us how many frames are ready.
static OSStatus iOSCoreAudioRecordCallback(void *inRefCon,
											AudioUnitRenderActionFlags *ioActionFlags,
											const AudioTimeStamp *inTimeStamp,
											UInt32 inBusNumber,
											UInt32 inNumberFrames,
											AudioBufferList *ioData) {
	if (!audioInstance) {
		return noErr;
	}

	if (inNumberFrames > g_recordScratchFrames) {
		delete[] g_recordScratch;
		g_recordScratch = new int16_t[inNumberFrames];
		g_recordScratchFrames = inNumberFrames;
	}

	AudioBufferList bufferList;
	bufferList.mNumberBuffers = 1;
	bufferList.mBuffers[0].mNumberChannels = 1;
	bufferList.mBuffers[0].mDataByteSize = inNumberFrames * sizeof(int16_t);
	bufferList.mBuffers[0].mData = g_recordScratch;

	OSStatus err = AudioUnitRender(audioInstance, ioActionFlags, inTimeStamp, 1, inNumberFrames, &bufferList);
	if (err != noErr) {
		return err;
	}

	Microphone::addAudioData((uint8_t *)g_recordScratch, (int)bufferList.mBuffers[0].mDataByteSize);
	return noErr;
}

// Reconfigures the shared RemoteIO unit to add the input (mic) side. This briefly stops and
// restarts the unit, so playback will glitch for a moment - unavoidable since output and input
// share the same AudioUnit instance on iOS.
static void iOSCoreAudioStartRecordingUnit(int sampleRate) {
	if (!audioInstance) {
		ERROR_LOG(Log::Audio, "iOSCoreAudioRecording_Start: no audio unit");
		return;
	}
	if (g_recordingActive && g_recordingSampleRate == sampleRate) {
		return;
	}

	g_recordingSampleRate = sampleRate;
	g_recordingActive = true;
	iOSCoreAudioUpdateSession();

	AudioOutputUnitStop(audioInstance);
	AudioUnitUninitialize(audioInstance);

	OSStatus err;
	UInt32 enableInput = 1;
	err = AudioUnitSetProperty(audioInstance, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &enableInput, sizeof(enableInput));
	if (err != noErr) {
		ERROR_LOG(Log::Audio, "Failed to enable RemoteIO input: %d", (int)err);
	}

	AudioStreamBasicDescription recordFormat;
	memset(&recordFormat, 0, sizeof(recordFormat));
	recordFormat.mSampleRate = sampleRate;
	recordFormat.mFormatID = kAudioFormatLinearPCM;
	recordFormat.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
	recordFormat.mBitsPerChannel = sizeof(int16_t) * 8;
	recordFormat.mChannelsPerFrame = 1;
	recordFormat.mFramesPerPacket = 1;
	recordFormat.mBytesPerFrame = (recordFormat.mBitsPerChannel / 8) * recordFormat.mChannelsPerFrame;
	recordFormat.mBytesPerPacket = recordFormat.mBytesPerFrame * recordFormat.mFramesPerPacket;
	err = AudioUnitSetProperty(audioInstance, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &recordFormat, sizeof(recordFormat));
	if (err != noErr) {
		ERROR_LOG(Log::Audio, "Failed to set record stream format: %d", (int)err);
	}

	AURenderCallbackStruct recordCallback;
	recordCallback.inputProc = iOSCoreAudioRecordCallback;
	recordCallback.inputProcRefCon = NULL;
	err = AudioUnitSetProperty(audioInstance, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 1, &recordCallback, sizeof(recordCallback));
	if (err != noErr) {
		ERROR_LOG(Log::Audio, "Failed to set record callback: %d", (int)err);
	}

	err = AudioUnitInitialize(audioInstance);
	if (err != noErr) {
		ERROR_LOG(Log::Audio, "Failed to reinitialize audio unit for recording: %d", (int)err);
		g_recordingActive = false;
		iOSCoreAudioUpdateSession();
		return;
	}
	err = AudioOutputUnitStart(audioInstance);
	if (err != noErr) {
		ERROR_LOG(Log::Audio, "Failed to restart audio unit after enabling recording: %d", (int)err);
		g_recordingActive = false;
		iOSCoreAudioUpdateSession();
		return;
	}

	INFO_LOG(Log::Audio, "iOS microphone recording started at %d Hz", sampleRate);
}

static void iOSCoreAudioStopRecordingUnit() {
	if (!g_recordingActive || !audioInstance) {
		return;
	}
	g_recordingActive = false;

	AudioOutputUnitStop(audioInstance);
	AudioUnitUninitialize(audioInstance);

	UInt32 disableInput = 0;
	AudioUnitSetProperty(audioInstance, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &disableInput, sizeof(disableInput));

	AudioUnitInitialize(audioInstance);
	AudioOutputUnitStart(audioInstance);

	delete[] g_recordScratch;
	g_recordScratch = nullptr;
	g_recordScratchFrames = 0;

	iOSCoreAudioUpdateSession();

	INFO_LOG(Log::Audio, "iOS microphone recording stopped");
}

void iOSCoreAudioRecording_Start(int sampleRate) {
	AVAudioSession *session = [AVAudioSession sharedInstance];
	switch (session.recordPermission) {
	case AVAudioSessionRecordPermissionGranted:
		dispatch_async(dispatch_get_main_queue(), ^{
			iOSCoreAudioStartRecordingUnit(sampleRate);
		});
		break;
	case AVAudioSessionRecordPermissionDenied:
		WARN_LOG(Log::Audio, "Microphone permission previously denied, can't start recording");
		break;
	case AVAudioSessionRecordPermissionUndetermined:
	default:
		[session requestRecordPermission:^(BOOL granted) {
			dispatch_async(dispatch_get_main_queue(), ^{
				if (granted) {
					iOSCoreAudioStartRecordingUnit(sampleRate);
				} else {
					WARN_LOG(Log::Audio, "Microphone permission denied by user");
				}
			});
		}];
		break;
	}
}

void iOSCoreAudioRecording_Stop() {
	dispatch_async(dispatch_get_main_queue(), ^{
		iOSCoreAudioStopRecordingUnit();
	});
}

bool iOSCoreAudioRecording_IsAvailable() {
	// All iOS devices have a microphone (or can have one connected). Permission is handled at start time.
	return true;
}

bool iOSCoreAudioRecording_State() {
	return g_recordingActive;
}

