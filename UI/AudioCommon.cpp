#include "Common/System/System.h"
#include "Core/HW/StereoResampler.h"  // TODO: Consider relocating to a more appropriate module.
#include "UI/AudioCommon.h"
#include "UI/BackgroundAudio.h"

// Global stereo resampler instance for handling audio mixing and processing.
StereoResampler g_resampler;

/**
 * Mixes audio for the emulator.
 * This function is called from outside the emulator thread.
 *
 * @param outStereo Output buffer for stereo audio samples.
 * @param numFrames Number of stereo frames to process.
 * @param sampleRateHz Sample rate in Hz for audio processing.
 * @return Number of valid frames mixed.
 */
int System_AudioMix(int16_t* outStereo, int numFrames, int sampleRateHz) {
    if (!outStereo || numFrames <= 0 || sampleRateHz <= 0) {
        return 0;  // Handle invalid input parameters gracefully.
    }

    // Resample and mix audio into the output buffer.
    int validFrames = g_resampler.Mix(outStereo, numFrames, false, sampleRateHz);

    // Mix background sound effects on top of the existing audio.
    g_BackgroundAudio.SFX().Mix(outStereo, validFrames, sampleRateHz);

    return validFrames;
}

/**
 * Retrieves or resets audio debug statistics.
 *
 * @param buf Buffer to write debug statistics to.
 * @param bufSize Size of the buffer in bytes.
 */
void System_AudioGetDebugStats(char* buf, size_t bufSize) {
    if (buf && bufSize > 0) {
        g_resampler.GetAudioDebugStats(buf, bufSize);
    } else {
        g_resampler.ResetStatCounters();  // Reset stats if no valid buffer is provided.
    }
}

/**
 * Clears all audio-related data.
 */
void System_AudioClear() {
    g_resampler.Clear();
}

/**
 * Pushes new audio samples into the resampler.
 *
 * @param audio Input buffer containing audio samples.
 * @param numSamples Number of audio samples in the input buffer.
 */
void System_AudioPushSamples(const int32_t* audio, int numSamples) {
    if (audio && numSamples > 0) {
        g_resampler.PushSamples(audio, numSamples);
    } else {
        g_resampler.Clear();  // Clear resampler if invalid input is provided.
    }
}
