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
#include <AudioToolbox/AudioToolbox.h>

#define SAMPLE_RATE 44100

#define STREAM_MAX_FRAME_COUNT 2048
static short stream[STREAM_MAX_FRAME_COUNT * 2 * 2]; // frames * sample size * number of channels

AudioComponentInstance audioInstance = nil;

int NativeMix(short *audio, int num_samples);

OSStatus iOSCoreAudioCallback(void *inRefCon,
                              AudioUnitRenderActionFlags *ioActionFlags,
                              const AudioTimeStamp *inTimeStamp,
                              UInt32 inBusNumber,
                              UInt32 inNumberFrames,
                              AudioBufferList *ioData)
{
    // see if we have any sound to play
    UInt32 frames = (inNumberFrames > STREAM_MAX_FRAME_COUNT ? STREAM_MAX_FRAME_COUNT : inNumberFrames);
    UInt32 framesReady = NativeMix(stream, frames);
    if (framesReady == 0) {
        // oops, we don't currently have any sound, so return silence
        *ioActionFlags |= kAudioUnitRenderAction_OutputIsSilence;
        return noErr;
    }
    
    // grab the output buffer and copy data into it
    AudioSampleType *output = (AudioSampleType *)ioData->mBuffers[0].mData;
    UInt32 bytesReady = framesReady * sizeof(short) * 2;
    memcpy(output, stream, bytesReady);
    // make sure and tell it how much audio data is there
    ioData->mBuffers[0].mDataByteSize = bytesReady;
    
    return noErr;
}

void iOSCoreAudioInit()
{
    if (!audioInstance) {
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
        streamFormat.mBitsPerChannel = sizeof(AudioSampleType) * 8;
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
