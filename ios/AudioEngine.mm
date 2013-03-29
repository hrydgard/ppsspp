//
//  AudioEngine.mm
//  PPSSPP
//
//  Created by rock88 on 15/03/2013.
//  Copyright (c) 2013 Homebrew. All rights reserved.
//

#import "AudioEngine.h"
#import <OpenAL/al.h>
#import <OpenAL/alc.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>

#import <string>

static volatile BOOL done = 0;

#define SAMPLE_SIZE 44100
static short stream[SAMPLE_SIZE];

int NativeMix(short *audio, int num_samples);

@interface AudioEngine ()

@property (nonatomic,assign) ALCdevice *alcDevice;
@property (nonatomic,assign) ALCcontext *alContext;
@property (nonatomic,assign) ALuint buffer;
@property (nonatomic,assign) ALuint source;

@end

@implementation AudioEngine
@synthesize alcDevice,alContext,buffer,source;

- (id)init
{
    self = [super init];
    if (self)
    {
        [self audioInit];
        [self audioLoop];
    }
    return self;
}

- (void)dealloc
{
    [self audioShutdown];
    [super dealloc];
}

- (void)checkALError
{
    ALenum ErrCode;
    std::string Err = "OpenAL error: ";
    if ((ErrCode = alGetError()) != AL_NO_ERROR)
    {
        Err += (char *)alGetString(ErrCode);
        printf("%s\n",Err.c_str());
    }
}

- (void)audioInit
{
    done = 0;
    alcDevice = alcOpenDevice(NULL);
    
    if (alcDevice)
    {
        NSLog(@"OpenAL device opened: %s",alcGetString(alcDevice, ALC_DEVICE_SPECIFIER));
    }
    else
    {
        NSLog(@"WARNING: could not open OpenAL device");
        return;
    }
    
    alContext = alcCreateContext(alcDevice, NULL);
    
    if (alContext)
    {
        alcMakeContextCurrent(alContext);
    }
    else
    {
        NSLog(@"ERROR: no OpenAL context");
        return;
    }
    
    alGenSources(1, &source);
    alGenBuffers(1, &buffer);
}

- (void)audioShutdown
{
    done = 1;
    alcMakeContextCurrent(NULL);
    
    if (alContext)
    {
        alcDestroyContext(alContext);
        alContext = NULL;
    }
    
    if (alcDevice)
    {
        alcCloseDevice(alcDevice);
        alcDevice = NULL;
    }
}

- (bool)playing
{
    ALenum state;
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    return (state == AL_PLAYING);
}

- (void)audioLoop
{
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^(void){
        while (!done)
        {
            size_t frames_ready;
            if (![self playing])
                frames_ready = NativeMix(stream, SAMPLE_SIZE / 2);
            else
                frames_ready = 0;

            if (frames_ready > 0)
            {
                const size_t bytes_ready = frames_ready * sizeof(short) * 2;
                alSourcei(source, AL_BUFFER, 0);
                alBufferData(buffer, AL_FORMAT_STEREO16, stream, bytes_ready, 44100);
                alSourcei(source, AL_BUFFER, buffer);
                alSourcePlay(source);

                // TODO: Maybe this could get behind?
                usleep((1000000 * frames_ready) / 44100);
            }
            else
                usleep(100);
            pthread_yield_np();
        }
    });
}

@end
