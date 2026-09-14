// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/WaveFile.h"
#include "Common/Log.h"
#include "Core/Config.h"

WaveFileWriter::WaveFileWriter() {}

WaveFileWriter::~WaveFileWriter()
{
	Stop();
}

bool WaveFileWriter::Start(const Path &filename, unsigned int HLESampleRate)
{
	// Check if the file is already open
	if (file) {
		ERROR_LOG(Log::System, "The file %s was already open, the file header will not be written.", filename.c_str());
		return false;
	}

	file.Open(filename, "wb");
	if (!file) {
		ERROR_LOG(Log::IO, "The file %s could not be opened for writing. Please check if it's already opened by another program.", filename.c_str());
		return false;
	}

	audio_size = 0;

	// -----------------
	// Write file header
	// -----------------
	Write4("RIFF");
	Write(100 * 1000 * 1000);  // write big value in case the file gets truncated
	Write4("WAVE");
	Write4("fmt ");

	Write(16);          // size of fmt block
	Write(0x00020001);  // two channels, uncompressed

	const uint32_t sample_rate = HLESampleRate;
	Write(sample_rate);
	Write(sample_rate * 2 * 2);  // two channels, 16bit

	Write(0x00100004);
	Write4("data");
	Write(100 * 1000 * 1000 - 32);

	// We are now at offset 44
	uint64_t offset = file.Tell();
	_assert_msg_(offset == 44, "Wrong offset: %lld", (long long)offset);
	return true;
}

void WaveFileWriter::Stop()
{
	// u32 file_size = (u32)ftello(file);
	file.Seek(4, SEEK_SET);
	Write(audio_size + 36);

	file.Seek(40, SEEK_SET);
	Write(audio_size);

	file.Close();
}

void WaveFileWriter::Write(uint32_t value)
{
	file.WriteArray(&value, 1);
}

void WaveFileWriter::Write4(const char* ptr)
{
	file.WriteBytes(ptr, 4);
}

// count is the number of stereo frames, so 4 bytes each.
void WaveFileWriter::AddStereoSamples(const short* sample_data, uint32_t count)
{
	if (!file) {
		// Callers are supposed to check that Start() succeeded, but this used to assert - which
		// isn't compiled out in release builds, so a file we couldn't open crashed the emulator.
		ERROR_LOG(Log::System, "WaveFileWriter - file not open.");
		return;
	}

	if (skip_silence)
	{
		bool all_zero = true;

		for (uint32_t i = 0; i < count * 2; i++)
		{
			if (sample_data[i])
				all_zero = false;
		}

		if (all_zero)
			return;
	}

	file.WriteBytes(sample_data, count * 4);
	audio_size += count * 4;
}
