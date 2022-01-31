// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

// ---------------------------------------------------------------------------------
// Class: WaveFileWriter
// Description: Simple utility class to make it easy to write long 16-bit stereo
// audio streams to disk.
// Use Start() to start recording to a file, and AddStereoSamples to add wave data.
// If Stop is not called when it destructs, the destructor will call Stop().
// ---------------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstdint>

#include "Common/File/FileUtil.h"

class WaveFileWriter
{
public:
	WaveFileWriter();
	~WaveFileWriter();

	bool Start(const Path& filename, unsigned int HLESampleRate);
	void Stop();

	void SetSkipSilence(bool skip) { skip_silence = skip; }
	void AddStereoSamples(const short* sample_data, uint32_t count);
	uint32_t GetAudioSize() const { return audio_size; }
private:
	static constexpr size_t BUFFER_SIZE = 32 * 1024;

	File::IOFile file;
	bool skip_silence = false;
	uint32_t audio_size = 0;
	void Write(uint32_t value);
	void Write4(const char* ptr);
};
