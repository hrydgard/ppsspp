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

#include <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>

#define SAMPLE_RATE 44100

static AudioComponentInstance audioInstance = nil;
static bool g_displayConnected = false;

void iOSCoreAudioUpdateSession() {
	NSError *error = nil;
	if (g_displayConnected) {
		INFO_LOG(Log::Audio, "Display connected, setting Playback mode");
		// Special handling when a display is connected. Always exclusive.
		// Let's revisit this later.
		[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayback error:&error];
		return;
	}

	INFO_LOG(Log::Audio, "RespectSilentMode: %d MixWithOthers: %d", g_Config.bAudioRespectSilentMode, g_Config.bAudioMixWithOthers);

	// Hacky hack to force iOS to re-evaluate.
	// Switching from CatogoryPlayback to CategoryPlayback with an option otherwise does nothing.
	[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryAudioProcessing error:&error];

	// Here, we apply the settings.
	const bool mixWithOthers = g_Config.bAudioMixWithOthers;
	if (g_Config.bAudioMixWithOthers) {
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

void iOSCoreAudioSetDisplayConnected(bool connected) {
	g_displayConnected = connected;
	iOSCoreAudioUpdateSession();
}

int NativeMix(short *audio, int numSamples, int sampleRate);

OSStatus iOSCoreAudioCallback(void *inRefCon,
							  AudioUnitRenderActionFlags *ioActionFlags,
							  const AudioTimeStamp *inTimeStamp,
							  UInt32 inBusNumber,
							  UInt32 inNumberFrames,
							  AudioBufferList *ioData)
{
	// see if we have any sound to play
	short *output = (short *)ioData->mBuffers[0].mData;
	UInt32 framesReady = NativeMix(output, inNumberFrames, SAMPLE_RATE);
	
	if (framesReady == 0) {
		// oops, we don't currently have any sound, so return silence
		*ioActionFlags |= kAudioUnitRenderAction_OutputIsSilence;
	}
	
	/* 
	 * You'd think iOS would want to know how many frames were
	 * actually generated in case it was less than asked for, but
	 * apparently that causes micro-stuttering and everything just
	 * works better if we lie and say we successfully generated as
	 * many frames as it wanted... weird. We still get micro-stuttering
	 * but it's less noticeable this way.
	 */ 
	//UInt32 bytesReady = framesReady * sizeof(short) * 2;
	UInt32 bytesReady = inNumberFrames * sizeof(short) * 2;
	ioData->mBuffers[0].mDataByteSize = bytesReady;
	
	return noErr;
}

void iOSCoreAudioInit()
{
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
}

