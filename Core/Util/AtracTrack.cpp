#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Core/Util/AtracTrack.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/MemMap.h"

// Atrac file parsing constants
#define AT3_MAGIC           0x0270
#define AT3_PLUS_MAGIC      0xFFFE  // This is normally a marker for WAVE_FORMAT_EXTENSIBLE

const int RIFF_CHUNK_MAGIC = 0x46464952;
const int RIFF_WAVE_MAGIC = 0x45564157;
const int FMT_CHUNK_MAGIC = 0x20746D66;
const int DATA_CHUNK_MAGIC = 0x61746164;
const int SMPL_CHUNK_MAGIC = 0x6C706D73;
const int FACT_CHUNK_MAGIC = 0x74636166;

static u16 Read16(const u8 *buffer, int offset) {
	u16 value;
	memcpy(&value, buffer + offset, sizeof(u16));
	return value;
}

static u32 Read32(const u8 *buffer, int offset) {
	u32 value;
	memcpy(&value, buffer + offset, sizeof(u32));
	return value;
}

int AnalyzeAtracTrack(const u8 *buffer, u32 size, Track *track, std::string *error) {
	// 72 is about the size of the minimum required data to even be valid.
	if (size < 72) {
		return SCE_ERROR_ATRAC_SIZE_TOO_SMALL;
	}

	// If the pointer is bad, let's try to survive, although I'm pretty sure that on a real PSP,
	// we crash here.
	if (!buffer) {
		_dbg_assert_(false);
		return SCE_KERNEL_ERROR_INVALID_POINTER;
	}

	// TODO: Validate stuff more.
	if (Read32(buffer, 0) != RIFF_CHUNK_MAGIC) {
		ERROR_LOG(Log::ME, "Couldn't find RIFF header");
		return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
	}

	struct RIFFFmtChunk {
		u16 fmtTag;
		u16 channels;
		u32 samplerate;
		u32 avgBytesPerSec;
		u16 blockAlign;
	};

	u32 offset = 8;
	track->firstSampleOffset = 0;

	while (Read32(buffer, offset) != RIFF_WAVE_MAGIC) {
		// Get the size preceding the magic.
		int chunk = Read32(buffer, offset - 4);
		// Round the chunk size up to the nearest 2.
		offset += chunk + (chunk & 1);
		if (offset + 12 > size) {
			*error = StringFromFormat("%d too small for WAVE chunk at offset %d", size, offset);
			return SCE_ERROR_ATRAC_SIZE_TOO_SMALL;
		}
		if (Read32(buffer, offset) != RIFF_CHUNK_MAGIC) {
			*error = "RIFF chunk did not contain WAVE";
			return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
		}
		offset += 8;
	}
	offset += 4;

	if (offset != 12) {
		WARN_LOG(Log::ME, "RIFF chunk at offset: %d", offset);
	}

	// RIFF size excluding chunk header.
	track->fileSize = Read32(buffer, offset - 8) + 8;

	// Even if the RIFF size is too low, it may simply be incorrect.  This works on real firmware.
	u32 maxSize = std::max(track->fileSize, size);

	bool bfoundData = false;
	u32 dataChunkSize = 0;
	int sampleOffsetAdjust = 0;

	while (maxSize >= offset + 8 && !bfoundData) {
		int chunkMagic = Read32(buffer, offset);
		u32 chunkSize = Read32(buffer, offset + 4);
		// Account for odd sized chunks.
		if (chunkSize & 1) {
			WARN_LOG(Log::ME, "RIFF chunk had uneven size");
		}
		chunkSize += (chunkSize & 1);
		offset += 8;
		if (chunkSize > maxSize - offset)
			break;
		switch (chunkMagic) {
		case FMT_CHUNK_MAGIC:
		{
			if (track->codecType != 0) {
				*error = "AnalyzeTrack: multiple fmt chunks is not valid";
				return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
			}

			auto at3fmt = (const RIFFFmtChunk *)(buffer + offset);
			if (chunkSize < 32 || (at3fmt->fmtTag == AT3_PLUS_MAGIC && chunkSize < 52)) {
				*error = "AnalyzeTrack: fmt definition too small(%d)";
				return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
			}

			if (at3fmt->fmtTag == AT3_MAGIC)
				track->codecType = PSP_CODEC_AT3;
			else if (at3fmt->fmtTag == AT3_PLUS_MAGIC)
				track->codecType = PSP_CODEC_AT3PLUS;
			else {
				*error = "AnalyzeTrack: invalid fmt magic: %04x";
				return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
			}
			track->channels = at3fmt->channels;
			if (track->channels != 1 && track->channels != 2) {
				*error = "AnalyzeTrack: unsupported channel count %d";
				return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
			}
			if (at3fmt->samplerate != 44100) {
				*error = "AnalyzeTrack: unsupported sample rate %d";
				return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
			}
			track->bitrate = at3fmt->avgBytesPerSec * 8;
			track->bytesPerFrame = at3fmt->blockAlign;
			if (track->bytesPerFrame == 0) {
				*error = "invalid bytes per frame: %d";
				return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
			}

			// TODO: There are some format specific bytes here which seem to have fixed values?
			// Probably don't need them.

			if (at3fmt->fmtTag == AT3_MAGIC) {  // 0x270
				// This is the offset to the jointStereo_ field.
				track->jointStereo = Read16(buffer, offset + 24);
				// Then there are more fields here.
				u16 unknown1_2 = Read16(buffer, offset + 30);
			} else if (at3fmt->fmtTag == AT3_PLUS_MAGIC) {
				_dbg_assert_(at3fmt->fmtTag == 0xFFFE);
				// It's in an "Extensible" wave format. Let's read some more.
			}
			if (chunkSize > 16) {
				// Read and format extra bytes as hexadecimal
				std::string hex;
				DataToHexString(buffer + offset + 16, chunkSize - 16, &hex, false);
				DEBUG_LOG(Log::ME, "Additional chunk data (beyond 16 bytes): %s", hex.c_str());
			}
			break;
		}
		case FACT_CHUNK_MAGIC:
		{
			track->endSample = Read32(buffer, offset);
			if (chunkSize >= 8) {
				track->firstSampleOffset = Read32(buffer, offset + 4);
			}
			if (chunkSize >= 12) {
				u32 largerOffset = Read32(buffer, offset + 8);
				// Works, but "largerOffset"??
				sampleOffsetAdjust = track->firstSampleOffset - largerOffset;
			}
			break;
		}
		case SMPL_CHUNK_MAGIC:
		{
			if (chunkSize < 32) {
				*error = StringFromFormat("smpl chunk too small (%d)", chunkSize);
				return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
			}
			int checkNumLoops = Read32(buffer, offset + 28);
			if (checkNumLoops != 0 && chunkSize < 36 + 20) {
				*error = StringFromFormat("smpl chunk too small for loop (%d, %d)", checkNumLoops, chunkSize);
				return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
			}
			if (checkNumLoops < 0) {
				*error = StringFromFormat("bad checkNumLoops (%d)", checkNumLoops);
				return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
			}

			track->loopinfo.resize(checkNumLoops);
			u32 loopinfoOffset = offset + 36;
			// The PSP only cares about the first loop start and end, it seems.
			// Most likely can skip the rest of this data, but it's not hurting anyone.
			for (int i = 0; i < checkNumLoops && 36 + (u32)i < chunkSize; i++, loopinfoOffset += 24) {
				track->loopinfo[i].cuePointID = Read32(buffer, loopinfoOffset + 0);
				track->loopinfo[i].type = Read32(buffer, loopinfoOffset + 4);
				track->loopinfo[i].startSample = Read32(buffer, loopinfoOffset + 8);
				track->loopinfo[i].endSample = Read32(buffer, loopinfoOffset + 12);
				track->loopinfo[i].fraction = Read32(buffer, loopinfoOffset + 16);
				track->loopinfo[i].playCount = Read32(buffer, loopinfoOffset + 20);
				if (i == 0 && track->loopinfo[i].startSample >= track->loopinfo[i].endSample) {
					*error = "AnalyzeTrack: loop starts after it ends";
					return SCE_ERROR_ATRAC_BAD_CODEC_PARAMS;
				}
			}
			break;
		}
		case DATA_CHUNK_MAGIC:
		{
			bfoundData = true;
			track->dataByteOffset = offset;
			dataChunkSize = chunkSize;
			if (track->fileSize < offset + chunkSize) {
				WARN_LOG(Log::ME, "Atrac data chunk extends beyond riff chunk");
				track->fileSize = offset + chunkSize;
			}
		}
		break;
		}
		offset += chunkSize;
	}

	if (track->codecType == 0) {
		*error = "Could not detect codec";
		return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
	}

	if (!bfoundData) {
		*error = "AnalyzeTrack: No data chunk found";
		return SCE_ERROR_ATRAC_SIZE_TOO_SMALL;
	}

	// set the loopStartSample_ and loopEndSample_ by loopinfo_
	if (track->loopinfo.size() > 0) {
		track->loopStartSample = track->loopinfo[0].startSample + track->FirstOffsetExtra() + sampleOffsetAdjust;
		track->loopEndSample = track->loopinfo[0].endSample + track->FirstOffsetExtra() + sampleOffsetAdjust;
	} else {
		track->loopStartSample = -1;
		track->loopEndSample = -1;
	}

	// if there is no correct endsample, try to guess it
	if (track->endSample <= 0 && track->bytesPerFrame != 0) {
		track->endSample = (dataChunkSize / track->bytesPerFrame) * track->SamplesPerFrame();
		track->endSample -= track->FirstSampleOffsetFull();
	}
	track->endSample -= 1;

	if (track->loopEndSample != -1 && track->loopEndSample > track->endSample + track->FirstSampleOffsetFull()) {
		*error = "AnalyzeTrack: loop after end of data";
		return SCE_ERROR_ATRAC_BAD_CODEC_PARAMS;
	}

	return 0;
}

int AnalyzeAA3Track(const u8 *buffer, u32 size, u32 fileSize, Track *track, std::string *error) {
	// TODO: Make sure this validation is correct, more testing.
	if (size < 10) {
		return SCE_ERROR_ATRAC_AA3_SIZE_TOO_SMALL;
	}

	// If the pointer is bad, let's try to survive, although I'm pretty sure that on a real PSP,
	// we crash here.
	if (!buffer) {
		_dbg_assert_(false);
		return SCE_KERNEL_ERROR_INVALID_POINTER;
	}

	if (buffer[0] != 'e' || buffer[1] != 'a' || buffer[2] != '3') {
		return SCE_ERROR_ATRAC_AA3_INVALID_DATA;
	}

	// It starts with an id3 header (replaced with ea3.)  This is the size.
	u32 tagSize = buffer[9] | (buffer[8] << 7) | (buffer[7] << 14) | (buffer[6] << 21);
	if (size < tagSize + 36) {
		return SCE_ERROR_ATRAC_AA3_SIZE_TOO_SMALL;
	}

	// EA3 header starts at id3 header (10) + tagSize.
	buffer = buffer + 10 + tagSize;
	if (buffer[0] != 'E' || buffer[1] != 'A' || buffer[2] != '3') {
		ERROR_LOG(Log::ME, "AnalyzeAA3Track: Invalid EA3 magic bytes");
		return SCE_ERROR_ATRAC_AA3_INVALID_DATA;
	}

	track->fileSize = fileSize;

	// Based on FFmpeg's code.
	u32 codecParams = buffer[33] | (buffer[34] << 8) | (buffer[35] << 16);
	const u32 at3SampleRates[8] = { 32000, 44100, 48000, 88200, 96000, 0 };

	switch (buffer[32]) {
	case 0:
		track->codecType = PSP_CODEC_AT3;
		track->bytesPerFrame = (codecParams & 0x03FF) * 8;
		track->bitrate = at3SampleRates[(codecParams >> 13) & 7] * track->bytesPerFrame * 8 / 1024;
		track->channels = 2;
		track->jointStereo = (codecParams >> 17) & 1;
		break;
	case 1:
		track->codecType = PSP_CODEC_AT3PLUS;
		track->bytesPerFrame = ((codecParams & 0x03FF) * 8) + 8;
		track->bitrate = at3SampleRates[(codecParams >> 13) & 7] * track->bytesPerFrame * 8 / 2048;
		track->channels = (codecParams >> 10) & 7;
		break;
	case 3:
	case 4:
	case 5:
		ERROR_LOG(Log::ME, "AnalyzeAA3Track: unsupported codec type %d", buffer[32]);
		return SCE_ERROR_ATRAC_AA3_INVALID_DATA;
	default:
		ERROR_LOG(Log::ME, "AnalyzeAA3Track: invalid codec type %d", buffer[32]);
		return SCE_ERROR_ATRAC_AA3_INVALID_DATA;
	}

	track->dataByteOffset = 10 + tagSize + 96;
	track->firstSampleOffset = 0;
	if (track->endSample < 0 && track->bytesPerFrame != 0) {
		track->endSample = ((track->fileSize - track->dataByteOffset) / track->bytesPerFrame) * track->SamplesPerFrame();
	}
	track->endSample -= 1;
	return 0;
}
