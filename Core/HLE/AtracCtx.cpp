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

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Log.h"
#include "Core/Reporting.h"
#include "Core/MemMapHelpers.h"
#include "Core/System.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceAtrac.h"
#include "Core/HLE/AtracCtx.h"
#include "Core/HW/Atrac3Standalone.h"
#include "Core/HLE/sceKernelMemory.h"
#include <sstream>
#include <iomanip>

const size_t overAllocBytes = 16384;

const int RIFF_CHUNK_MAGIC = 0x46464952;
const int RIFF_WAVE_MAGIC = 0x45564157;
const int FMT_CHUNK_MAGIC = 0x20746D66;
const int DATA_CHUNK_MAGIC = 0x61746164;
const int SMPL_CHUNK_MAGIC = 0x6C706D73;
const int FACT_CHUNK_MAGIC = 0x74636166;

void Atrac::DoState(PointerWrap &p) {
	auto s = p.Section("Atrac", 1, 9);
	if (!s)
		return;

	Do(p, track_.channels);
	Do(p, outputChannels_);
	if (s >= 5) {
		Do(p, track_.jointStereo);
	}

	Do(p, atracID_);
	if (p.mode != p.MODE_READ) {
		first_._filesize_dontuse = track_.fileSize;
	}
	Do(p, first_);
	if (p.mode == p.MODE_READ) {
		track_.fileSize = first_._filesize_dontuse;
	}

	Do(p, bufferMaxSize_);
	Do(p, track_.codecType);

	Do(p, currentSample_);
	Do(p, track_.endSample);
	Do(p, track_.firstSampleOffset);
	if (s >= 3) {
		Do(p, track_.dataByteOffset);
	} else {
		track_.dataByteOffset = track_.firstSampleOffset;
	}

	u32 hasDataBuf = dataBuf_ != nullptr;
	Do(p, hasDataBuf);
	if (hasDataBuf) {
		if (p.mode == p.MODE_READ) {
			if (dataBuf_)
				delete[] dataBuf_;
			dataBuf_ = new u8[track_.fileSize + overAllocBytes];
			memset(dataBuf_, 0, track_.fileSize + overAllocBytes);
		}
		DoArray(p, dataBuf_, track_.fileSize);
	}
	Do(p, second_);

	Do(p, decodePos_);
	if (s < 9) {
		u32 oldDecodeEnd = 0;
		Do(p, oldDecodeEnd);
	}
	if (s >= 4) {
		Do(p, bufferPos_);
	} else {
		bufferPos_ = decodePos_;
	}

	Do(p, track_.bitrate);
	Do(p, track_.bytesPerFrame);

	Do(p, track_.loopinfo);
	if (s < 9) {
		int oldLoopInfoNum = 42;
		Do(p, oldLoopInfoNum);
	}

	Do(p, track_.loopStartSample);
	Do(p, track_.loopEndSample);
	Do(p, loopNum_);

	Do(p, context_);
	if (s >= 6) {
		Do(p, bufferState_);
	} else {
		if (dataBuf_ == nullptr) {
			bufferState_ = ATRAC_STATUS_NO_DATA;
		} else {
			UpdateBufferState();
		}
	}

	if (s >= 7) {
		Do(p, ignoreDataBuf_);
	} else {
		ignoreDataBuf_ = false;
	}

	if (s >= 9) {
		Do(p, bufferValidBytes_);
		Do(p, bufferHeaderSize_);
	} else {
		bufferHeaderSize_ = track_.dataByteOffset;
		bufferValidBytes_ = std::min(first_.size - track_.dataByteOffset, StreamBufferEnd() - track_.dataByteOffset);
		if ((bufferState_ & ATRAC_STATUS_STREAMED_MASK) == ATRAC_STATUS_STREAMED_MASK) {
			bufferPos_ = track_.dataByteOffset;
		}
	}

	if (s < 8 && bufferState_ == ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER) {
		// We didn't actually allow the second buffer to be set this far back.
		// Pretend it's a regular loop, we'll just try our best.
		bufferState_ = ATRAC_STATUS_STREAMED_LOOP_FROM_END;
	}

	// Make sure to do this late; it depends on things like bytesPerFrame_.
	if (p.mode == p.MODE_READ && bufferState_ != ATRAC_STATUS_NO_DATA) {
		CreateDecoder();
	}

	if (s >= 2 && s < 9) {
		bool oldResetBuffer = false;
		Do(p, oldResetBuffer);
	}
}

void Atrac::ResetData() {
	delete decoder_;
	decoder_ = nullptr;

	if (dataBuf_)
		delete[] dataBuf_;
	dataBuf_ = 0;
	ignoreDataBuf_ = false;
	bufferState_ = ATRAC_STATUS_NO_DATA;

	if (context_.IsValid())
		kernelMemory.Free(context_.ptr);
}

void Atrac::AnalyzeReset() {
	// Reset some values.
	track_.AnalyzeReset();

	currentSample_ = 0;
	loopNum_ = 0;
	decodePos_ = 0;
	bufferPos_ = 0;
}

u8 *Atrac::BufferStart() {
	return ignoreDataBuf_ ? Memory::GetPointerWrite(first_.addr) : dataBuf_;
}

void AtracBase::EnsureContext(int atracID) {
	if (!context_.IsValid()) {
		// allocate a new context_
		u32 contextSize = sizeof(SceAtracContext);
		// Note that Alloc can increase contextSize to the "grain" size.
		context_ = kernelMemory.Alloc(contextSize, false, StringFromFormat("AtracCtx/%d", atracID).c_str());
		if (context_.IsValid())
			Memory::Memset(context_.ptr, 0, contextSize, "AtracContextClear");
		context_->info.state = ATRAC_STATUS_NO_DATA;
		context_->info.atracID = atracID;
		WARN_LOG(Log::ME, "AtracBase::EnsureContext(): allocated new context", context_.ptr, atracID);
	}
}

void Atrac::UpdateContextFromPSPMem() {
	if (!context_.IsValid()) {
		return;
	}

	// Read in any changes from the game to the context.
	// TODO: Might be better to just always track in RAM.
	// TODO: It's possible that there are more changes we should read. Who knows,
	// problem games like FlatOut might poke stuff into the context?
	bufferState_ = context_->info.state;
	// This value is actually abused by games to store the SAS voice number.
	loopNum_ = context_->info.loopNum;
}

void Atrac::WriteContextToPSPMem() {
	if (!context_.IsValid()) {
		return;
	}
	// context points into PSP memory.
	SceAtracContext *context = context_;
	context->info.buffer = first_.addr;
	context->info.bufferByte = bufferMaxSize_;
	context->info.secondBuffer = second_.addr;
	context->info.secondBufferByte = second_.size;
	context->info.codec = track_.codecType;
	context->info.loopNum = loopNum_;
	context->info.loopStart = track_.loopStartSample > 0 ? track_.loopStartSample : 0;
	context->info.loopEnd = track_.loopEndSample > 0 ? track_.loopEndSample : 0;

	// Note that we read in the state when loading the atrac object, so it's safe
	// to update it back here all the time.  Some games, like Sol Trigger, change it.
	// TODO: Should we just keep this in PSP ram then, or something?
	context->info.state = bufferState_;
	if (track_.firstSampleOffset != 0) {
		context->info.samplesPerChan = track_.FirstSampleOffsetFull();
	} else {
		context->info.samplesPerChan = (track_.codecType == PSP_MODE_AT_3_PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES);
	}
	context->info.sampleSize = track_.bytesPerFrame;
	context->info.numChan = track_.channels;
	context->info.dataOff = track_.dataByteOffset;
	context->info.endSample = track_.endSample + track_.FirstSampleOffsetFull();
	context->info.dataEnd = track_.fileSize;
	context->info.curOff = first_.fileoffset;
	context->info.decodePos = track_.DecodePosBySample(currentSample_);
	context->info.streamDataByte = first_.size - track_.dataByteOffset;

	u8 *buf = (u8 *)context;
	*(u32_le *)(buf + 0xfc) = atracID_;

	NotifyMemInfo(MemBlockFlags::WRITE, context_.ptr, sizeof(SceAtracContext), "AtracContext");
}

void Track::DebugLog() {
	DEBUG_LOG(Log::ME, "ATRAC analyzed: %s channels: %d filesize: %d bitrate: %d kbps jointStereo: %d",
		codecType == PSP_MODE_AT_3 ? "AT3" : "AT3Plus", channels, fileSize, bitrate / 1024, jointStereo);
	DEBUG_LOG(Log::ME, "dataoff: %d firstSampleOffset: %d endSample: %d", dataByteOffset, firstSampleOffset, endSample);
	DEBUG_LOG(Log::ME, "loopStartSample: %d loopEndSample: %d", loopStartSample, loopEndSample);
}

int Atrac::Analyze(u32 addr, u32 size) {
	track_ = {};
	first_ = {};
	first_.addr = addr;
	first_.size = size;

	AnalyzeReset();

	int retval = AnalyzeAtracTrack(addr, size, &track_);
	if (retval < 0) {
		return retval;
	}

	first_._filesize_dontuse = track_.fileSize;
	track_.DebugLog();
	return 0;
}

int AnalyzeAtracTrack(u32 addr, u32 size, Track *track) {
	// 72 is about the size of the minimum required data to even be valid.
	if (size < 72) {
		return SCE_ERROR_ATRAC_SIZE_TOO_SMALL;
	}

	// TODO: Check the range (addr, size) instead.
	if (!Memory::IsValidAddress(addr)) {
		return SCE_KERNEL_ERROR_ILLEGAL_ADDRESS;
	}

	// TODO: Validate stuff.
	if (Memory::ReadUnchecked_U32(addr) != RIFF_CHUNK_MAGIC) {
		ERROR_LOG(Log::ME, "Couldn't find RIFF header");
		return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
	}

	struct RIFFFmtChunk {
		u16_le fmtTag;
		u16_le channels;
		u32_le samplerate;
		u32_le avgBytesPerSec;
		u16_le blockAlign;
	};

	u32 offset = 8;
	track->firstSampleOffset = 0;

	while (Memory::Read_U32(addr + offset) != RIFF_WAVE_MAGIC) {
		// Get the size preceding the magic.
		int chunk = Memory::Read_U32(addr + offset - 4);
		// Round the chunk size up to the nearest 2.
		offset += chunk + (chunk & 1);
		if (offset + 12 > size) {
			ERROR_LOG(Log::ME, "AnalyzeAtracTrack(%08x, %d): too small for WAVE chunk at offset %d", addr, size, offset);
			return SCE_ERROR_ATRAC_SIZE_TOO_SMALL;
		}
		if (Memory::Read_U32(addr + offset) != RIFF_CHUNK_MAGIC) {
			ERROR_LOG(Log::ME, "AnalyzeAtracTrack(%08x, %d): RIFF chunk did not contain WAVE", addr, size);
			return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
		}
		offset += 8;
	}
	offset += 4;

	if (offset != 12) {
		WARN_LOG(Log::ME, "RIFF chunk at offset: %d", offset);
	}

	// RIFF size excluding chunk header.
	track->fileSize = Memory::Read_U32(addr + offset - 8) + 8;

	// Even if the RIFF size is too low, it may simply be incorrect.  This works on real firmware.
	u32 maxSize = std::max(track->fileSize, size);

	bool bfoundData = false;
	u32 dataChunkSize = 0;
	int sampleOffsetAdjust = 0;

	while (maxSize >= offset + 8 && !bfoundData) {
		int chunkMagic = Memory::Read_U32(addr + offset);
		u32 chunkSize = Memory::Read_U32(addr + offset + 4);
		// Account for odd sized chunks.
		if (chunkSize & 1) {
			WARN_LOG_REPORT_ONCE(oddchunk, Log::ME, "RIFF chunk had uneven size");
		}
		chunkSize += (chunkSize & 1);
		offset += 8;
		if (chunkSize > maxSize - offset)
			break;
		switch (chunkMagic) {
		case FMT_CHUNK_MAGIC:
		{
			if (track->codecType != 0) {
				ERROR_LOG(Log::ME, "AnalyzeTrack: multiple fmt chunks is not valid");
				return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
			}

			auto at3fmt = PSPPointer<const RIFFFmtChunk>::Create(addr + offset);
			if (chunkSize < 32 || (at3fmt->fmtTag == AT3_PLUS_MAGIC && chunkSize < 52)) {
				ERROR_LOG(Log::ME, "AnalyzeTrack: fmt definition too small(%d)", chunkSize);
				return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
			}

			if (at3fmt->fmtTag == AT3_MAGIC)
				track->codecType = PSP_MODE_AT_3;
			else if (at3fmt->fmtTag == AT3_PLUS_MAGIC)
				track->codecType = PSP_MODE_AT_3_PLUS;
			else {
				ERROR_LOG(Log::ME, "AnalyzeTrack: invalid fmt magic: %04x", at3fmt->fmtTag);
				return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
			}
			track->channels = at3fmt->channels;
			if (track->channels != 1 && track->channels != 2) {
				ERROR_LOG_REPORT(Log::ME, "AnalyzeTrack: unsupported channel count %d", track->channels);
				return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
			}
			if (at3fmt->samplerate != 44100) {
				ERROR_LOG_REPORT(Log::ME, "AnalyzeTrack: unsupported sample rate %d", at3fmt->samplerate);
				return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
			}
			track->bitrate = at3fmt->avgBytesPerSec * 8;
			track->bytesPerFrame = at3fmt->blockAlign;
			if (track->bytesPerFrame == 0) {
				ERROR_LOG_REPORT(Log::ME, "invalid bytes per frame: %d", track->bytesPerFrame);
				return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
			}

			// TODO: There are some format specific bytes here which seem to have fixed values?
			// Probably don't need them.

			if (at3fmt->fmtTag == AT3_MAGIC) {
				// This is the offset to the jointStereo_ field.
				track->jointStereo = Memory::Read_U32(addr + offset + 24);
			}
			if (chunkSize > 16) {
				// Read and format extra bytes as hexadecimal
				std::string hex;
				DataToHexString(Memory::GetPointer(addr + offset + 16), chunkSize - 16, &hex, false);
				DEBUG_LOG(Log::ME, "Additional chunk data (beyond 16 bytes): %s", hex.c_str());
			}
			break;
		}
		case FACT_CHUNK_MAGIC:
		{
			track->endSample = Memory::Read_U32(addr + offset);
			if (chunkSize >= 8) {
				track->firstSampleOffset = Memory::Read_U32(addr + offset + 4);
			}
			if (chunkSize >= 12) {
				u32 largerOffset = Memory::Read_U32(addr + offset + 8);
				sampleOffsetAdjust = track->firstSampleOffset - largerOffset;
			}
			break;
		}
		case SMPL_CHUNK_MAGIC:
		{
			if (chunkSize < 32) {
				ERROR_LOG(Log::ME, "AnalyzeTrack: smpl chunk too small (%d)", chunkSize);
				return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
			}
			int checkNumLoops = Memory::Read_U32(addr + offset + 28);
			if (checkNumLoops != 0 && chunkSize < 36 + 20) {
				ERROR_LOG(Log::ME, "AnalyzeTrack: smpl chunk too small for loop (%d, %d)", checkNumLoops, chunkSize);
				return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
			}
			if (checkNumLoops < 0) {
				ERROR_LOG(Log::ME, "bad checkNumLoops (%d)", checkNumLoops);
				return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
			}

			track->loopinfo.resize(checkNumLoops);
			u32 loopinfoAddr = addr + offset + 36;
			// The PSP only cares about the first loop start and end, it seems.
			// Most likely can skip the rest of this data, but it's not hurting anyone.
			for (int i = 0; i < checkNumLoops && 36 + (u32)i < chunkSize; i++, loopinfoAddr += 24) {
				track->loopinfo[i].cuePointID = Memory::Read_U32(loopinfoAddr);
				track->loopinfo[i].type = Memory::Read_U32(loopinfoAddr + 4);
				track->loopinfo[i].startSample = Memory::Read_U32(loopinfoAddr + 8);
				track->loopinfo[i].endSample = Memory::Read_U32(loopinfoAddr + 12);
				track->loopinfo[i].fraction = Memory::Read_U32(loopinfoAddr + 16);
				track->loopinfo[i].playCount = Memory::Read_U32(loopinfoAddr + 20);

				if (track->loopinfo[i].startSample >= track->loopinfo[i].endSample) {
					ERROR_LOG(Log::ME, "AnalyzeTrack: loop starts after it ends");
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
				WARN_LOG_REPORT(Log::ME, "Atrac data chunk extends beyond riff chunk");
				track->fileSize = offset + chunkSize;
			}
		}
		break;
		}
		offset += chunkSize;
	}

	if (track->codecType == 0) {
		ERROR_LOG(Log::ME, "AnalyzeTrack: Could not detect codec");
		return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
	}

	if (!bfoundData) {
		ERROR_LOG(Log::ME, "AnalyzeTrack: No data chunk found");
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
		ERROR_LOG(Log::ME, "AnalyzeTrack: loop after end of data");
		return SCE_ERROR_ATRAC_BAD_CODEC_PARAMS;
	}

	return 0;
}

int AnalyzeAA3Track(u32 addr, u32 size, u32 fileSize, Track *track) {
	if (size < 10) {
		return SCE_ERROR_ATRAC_AA3_SIZE_TOO_SMALL;
	}
	// TODO: Make sure this validation is correct, more testing.

	const u8 *buffer = Memory::GetPointer(addr);
	if (buffer[0] != 'e' || buffer[1] != 'a' || buffer[2] != '3') {
		return SCE_ERROR_ATRAC_AA3_INVALID_DATA;
	}

	// It starts with an id3 header (replaced with ea3.)  This is the size.
	u32 tagSize = buffer[9] | (buffer[8] << 7) | (buffer[7] << 14) | (buffer[6] << 21);
	if (size < tagSize + 36) {
		return SCE_ERROR_ATRAC_AA3_SIZE_TOO_SMALL;
	}

	// EA3 header starts at id3 header (10) + tagSize.
	buffer = Memory::GetPointer(addr + 10 + tagSize);
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
		track->codecType = PSP_MODE_AT_3;
		track->bytesPerFrame = (codecParams & 0x03FF) * 8;
		track->bitrate = at3SampleRates[(codecParams >> 13) & 7] * track->bytesPerFrame * 8 / 1024;
		track->channels = 2;
		track->jointStereo = (codecParams >> 17) & 1;
		break;
	case 1:
		track->codecType = PSP_MODE_AT_3_PLUS;
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

int Atrac::AnalyzeAA3(u32 addr, u32 size, u32 fileSize) {
	first_.addr = addr;
	first_.size = size;
	first_._filesize_dontuse = fileSize;

	AnalyzeReset();

	return AnalyzeAA3Track(addr, size, fileSize, &track_);
}

void Atrac::CalculateStreamInfo(u32 *outReadOffset) {
	u32 readOffset = first_.fileoffset;
	if (bufferState_ == ATRAC_STATUS_ALL_DATA_LOADED) {
		// Nothing to write.
		readOffset = 0;
		first_.offset = 0;
		first_.writableBytes = 0;
	} else if (bufferState_ == ATRAC_STATUS_HALFWAY_BUFFER) {
		// If we're buffering the entire file, just give the same as readOffset.
		first_.offset = readOffset;
		// In this case, the bytes writable are just the remaining bytes, always.
		first_.writableBytes = track_.fileSize - readOffset;
	} else {
		u32 bufferEnd = StreamBufferEnd();
		u32 bufferValidExtended = bufferPos_ + bufferValidBytes_;
		if (bufferValidExtended < bufferEnd) {
			first_.offset = bufferValidExtended;
			first_.writableBytes = bufferEnd - bufferValidExtended;
		} else {
			u32 bufferStartUsed = bufferValidExtended - bufferEnd;
			first_.offset = bufferStartUsed;
			first_.writableBytes = bufferPos_ - bufferStartUsed;
		}

		if (readOffset >= track_.fileSize) {
			if (bufferState_ == ATRAC_STATUS_STREAMED_WITHOUT_LOOP) {
				// We don't need anything more, so all 0s.
				readOffset = 0;
				first_.offset = 0;
				first_.writableBytes = 0;
			} else {
				readOffset = track_.FileOffsetBySample(track_.loopStartSample - track_.FirstSampleOffsetFull() - track_.SamplesPerFrame() * 2);
			}
		}

		if (readOffset + first_.writableBytes > track_.fileSize) {
			// Never ask for past the end of file, even when the space is free.
			first_.writableBytes = track_.fileSize - readOffset;
		}

		// If you don't think this should be here, remove it.  It's just a temporary safety check.
		if (first_.offset + first_.writableBytes > bufferMaxSize_) {
			ERROR_LOG_REPORT(Log::ME, "Somehow calculated too many writable bytes: %d + %d > %d", first_.offset, first_.writableBytes, bufferMaxSize_);
			first_.offset = 0;
			first_.writableBytes = bufferMaxSize_;
		}
	}

	if (outReadOffset) {
		*outReadOffset = readOffset;
	}
}

void AtracBase::CreateDecoder() {
	if (decoder_) {
		delete decoder_;
	}

	// First, init the standalone decoder.
	if (track_.codecType == PSP_MODE_AT_3) {
		// We don't pull this from the RIFF so that we can support OMA also.
		uint8_t extraData[14]{};
		// The only thing that changes are the jointStereo_ values.
		extraData[0] = 1;
		extraData[3] = track_.channels << 3;
		extraData[6] = track_.jointStereo;
		extraData[8] = track_.jointStereo;
		extraData[10] = 1;
		decoder_ = CreateAtrac3Audio(track_.channels, track_.bytesPerFrame, extraData, sizeof(extraData));
	} else {
		decoder_ = CreateAtrac3PlusAudio(track_.channels, track_.bytesPerFrame);
	}
}

void Atrac::GetResetBufferInfo(AtracResetBufferInfo *bufferInfo, int sample) {
	if (bufferState_ == ATRAC_STATUS_ALL_DATA_LOADED) {
		bufferInfo->first.writePosPtr = first_.addr;
		// Everything is loaded, so nothing needs to be read.
		bufferInfo->first.writableBytes = 0;
		bufferInfo->first.minWriteBytes = 0;
		bufferInfo->first.filePos = 0;
	} else if (bufferState_ == ATRAC_STATUS_HALFWAY_BUFFER) {
		// Here the message is: you need to read at least this many bytes to get to that position.
		// This is because we're filling the buffer start to finish, not streaming.
		bufferInfo->first.writePosPtr = first_.addr + first_.size;
		bufferInfo->first.writableBytes = track_.fileSize - first_.size;
		int minWriteBytes = track_.FileOffsetBySample(sample) - first_.size;
		if (minWriteBytes > 0) {
			bufferInfo->first.minWriteBytes = minWriteBytes;
		} else {
			bufferInfo->first.minWriteBytes = 0;
		}
		bufferInfo->first.filePos = first_.size;
	} else {
		// This is without the sample offset.  The file offset also includes the previous batch of samples?
		int sampleFileOffset = track_.FileOffsetBySample(sample - track_.firstSampleOffset - track_.SamplesPerFrame());

		// Update the writable bytes.  When streaming, this is just the number of bytes until the end.
		const u32 bufSizeAligned = (bufferMaxSize_ / track_.bytesPerFrame) * track_.bytesPerFrame;
		const int needsMoreFrames = track_.FirstOffsetExtra();  // ?

		bufferInfo->first.writePosPtr = first_.addr;
		bufferInfo->first.writableBytes = std::min(track_.fileSize - sampleFileOffset, bufSizeAligned);
		if (((sample + track_.firstSampleOffset) % (int)track_.SamplesPerFrame()) >= (int)track_.SamplesPerFrame() - needsMoreFrames) {
			// Not clear why, but it seems it wants a bit extra in case the sample is late?
			bufferInfo->first.minWriteBytes = track_.bytesPerFrame * 3;
		} else {
			bufferInfo->first.minWriteBytes = track_.bytesPerFrame * 2;
		}
		if ((u32)sample < (u32)track_.firstSampleOffset && sampleFileOffset != track_.dataByteOffset) {
			sampleFileOffset -= track_.bytesPerFrame;
		}
		bufferInfo->first.filePos = sampleFileOffset;

		if (second_.size != 0) {
			// TODO: We have a second buffer.  Within it, minWriteBytes should be zero.
			// The filePos should be after the end of the second buffer (or zero.)
			// We actually need to ensure we READ from the second buffer before implementing that.
		}
	}

	// It seems like this is always the same as the first buffer's pos, weirdly.
	bufferInfo->second.writePosPtr = first_.addr;
	// Reset never needs a second buffer write, since the loop is in a fixed place.
	bufferInfo->second.writableBytes = 0;
	bufferInfo->second.minWriteBytes = 0;
	bufferInfo->second.filePos = 0;
}

int Atrac::SetData(u32 buffer, u32 readSize, u32 bufferSize, int outputChannels, int successCode) {
	outputChannels_ = outputChannels;

	if (outputChannels != track_.channels) {
		WARN_LOG(Log::ME, "Atrac::SetData: outputChannels %d doesn't match track_.channels %d", outputChannels, track_.channels);
	}

	first_.addr = buffer;
	first_.size = readSize;

	if (first_.size > track_.fileSize)
		first_.size = track_.fileSize;
	first_.fileoffset = first_.size;

	// got the size of temp buf, and calculate offset
	bufferMaxSize_ = bufferSize;
	first_.offset = first_.size;

	// some games may reuse an atracID for playing sound
	ResetData();
	UpdateBufferState();

	if (track_.codecType != PSP_MODE_AT_3 && track_.codecType != PSP_MODE_AT_3_PLUS) {
		// Shouldn't have gotten here, Analyze() checks this.
		bufferState_ = ATRAC_STATUS_NO_DATA;
		ERROR_LOG(Log::ME, "unexpected codec type %d in set data", track_.codecType);
		return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
	}

	if (bufferState_ == ATRAC_STATUS_ALL_DATA_LOADED || bufferState_ == ATRAC_STATUS_HALFWAY_BUFFER) {
		// This says, don't use the dataBuf_ array, use the PSP RAM.
		// This way, games can load data async into the buffer, and it still works.
		// TODO: Support this always, even for streaming.
		ignoreDataBuf_ = true;
	}
	if (bufferState_ == ATRAC_STATUS_STREAMED_WITHOUT_LOOP || bufferState_ == ATRAC_STATUS_STREAMED_LOOP_FROM_END || bufferState_ == ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER) {
		bufferHeaderSize_ = track_.dataByteOffset;
		bufferPos_ = track_.dataByteOffset + track_.bytesPerFrame;
		bufferValidBytes_ = first_.size - bufferPos_;
	}

	const char *codecName = track_.codecType == PSP_MODE_AT_3 ? "atrac3" : "atrac3+";
	const char *channelName = track_.channels == 1 ? "mono" : "stereo";

	// Over-allocate databuf to prevent going off the end if the bitstream is bad or if there are
	// bugs in the decoder. This happens, see issue #15788. Arbitrary, but let's make it a whole page on the popular
	// architecture that has the largest pages (M1).
	dataBuf_ = new u8[track_.fileSize + overAllocBytes];
	memset(dataBuf_, 0, track_.fileSize + overAllocBytes);
	if (!ignoreDataBuf_) {
		u32 copybytes = std::min(bufferSize, track_.fileSize);
		Memory::Memcpy(dataBuf_, buffer, copybytes, "AtracSetData");
	}
	CreateDecoder();
	INFO_LOG(Log::ME, "Atrac::SetData (buffer=%08x, readSize=%d, bufferSize=%d): %s %s (%d channels) audio", buffer, readSize, bufferSize, codecName, channelName, track_.channels);
	return successCode;
}

u32 Atrac::SetSecondBuffer(u32 secondBuffer, u32 secondBufferSize) {
	u32 secondFileOffset = track_.FileOffsetBySample(track_.loopEndSample - track_.firstSampleOffset);
	u32 desiredSize = track_.fileSize - secondFileOffset;

	// 3 seems to be the number of frames required to handle a loop.
	if (secondBufferSize < desiredSize && secondBufferSize < (u32)track_.BytesPerFrame() * 3) {
		return SCE_ERROR_ATRAC_SIZE_TOO_SMALL;
	}
	if (BufferState() != ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER) {
		return SCE_ERROR_ATRAC_SECOND_BUFFER_NOT_NEEDED;
	}

	second_.addr = secondBuffer;
	second_.size = secondBufferSize;
	second_.fileoffset = secondFileOffset;
	return 0;
}

int AtracBase::GetSecondBufferInfo(u32 *fileOffset, u32 *desiredSize) {
	if (BufferState() != ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER) {
		// Writes zeroes in this error case.
		*fileOffset = 0;
		*desiredSize = 0;
		return SCE_ERROR_ATRAC_SECOND_BUFFER_NOT_NEEDED;
	}

	*fileOffset = track_.FileOffsetBySample(track_.loopEndSample - track_.firstSampleOffset);
	*desiredSize = track_.fileSize - *fileOffset;
	return 0;
}

void Atrac::GetStreamDataInfo(u32 *writePtr, u32 *writableBytes, u32 *readOffset) {
	u32 calculatedReadOffset;
	CalculateStreamInfo(&calculatedReadOffset);

	*writePtr = first_.addr + first_.offset;
	*writableBytes = first_.writableBytes;
	*readOffset = calculatedReadOffset;
}

void Atrac::UpdateBufferState() {
	if (bufferMaxSize_ >= track_.fileSize) {
		if (first_.size < track_.fileSize) {
			// The buffer is big enough in RAM, but we don't have all the data yet.
			bufferState_ = ATRAC_STATUS_HALFWAY_BUFFER;
		} else {
			bufferState_ = ATRAC_STATUS_ALL_DATA_LOADED;
		}
	} else {
		if (track_.loopEndSample <= 0) {
			// There's no looping, but we need to stream the data in our buffer.
			bufferState_ = ATRAC_STATUS_STREAMED_WITHOUT_LOOP;
		} else if (track_.loopEndSample == track_.endSample + track_.FirstSampleOffsetFull()) {
			bufferState_ = ATRAC_STATUS_STREAMED_LOOP_FROM_END;
		} else {
			bufferState_ = ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER;
		}
	}
}

// The game calls this after actually writing data to the buffer, as specified by the return values from GetStreamDataInfo.
// So, we should not have to call CalculateStreamInfo again here (although, might not be a bad idea for safety).
int Atrac::AddStreamData(u32 bytesToAdd) {
	u32 readOffset;
	CalculateStreamInfo(&readOffset);
	if (bytesToAdd > first_.writableBytes)
		return SCE_ERROR_ATRAC_ADD_DATA_IS_TOO_BIG;

	if (bytesToAdd > 0) {
		first_.fileoffset = readOffset;
		int addbytes = std::min(bytesToAdd, track_.fileSize - first_.fileoffset);
		if (!ignoreDataBuf_) {
			Memory::Memcpy(dataBuf_ + first_.fileoffset, first_.addr + first_.offset, addbytes, "AtracAddStreamData");
		}
		first_.fileoffset += addbytes;
	}
	first_.size += bytesToAdd;
	if (first_.size >= track_.fileSize) {
		first_.size = track_.fileSize;
		if (bufferState_ == ATRAC_STATUS_HALFWAY_BUFFER)
			bufferState_ = ATRAC_STATUS_ALL_DATA_LOADED;
		WriteContextToPSPMem();
	}

	first_.offset += bytesToAdd;
	bufferValidBytes_ += bytesToAdd;

	if (PSP_CoreParameter().compat.flags().AtracLoopHack && bufferState_ == ATRAC_STATUS_STREAMED_LOOP_FROM_END && RemainingFrames() > 2) {
		loopNum_++;
		SeekToSample(track_.loopStartSample - track_.FirstSampleOffsetFull());
	}

	return 0;
}

u32 Atrac::AddStreamDataSas(u32 bufPtr, u32 bytesToAdd) {
	int addbytes = std::min(bytesToAdd, track_.fileSize - first_.fileoffset - track_.FirstOffsetExtra());
	Memory::Memcpy(dataBuf_ + first_.fileoffset + track_.FirstOffsetExtra(), bufPtr, addbytes, "AtracAddStreamData");
	first_.size += bytesToAdd;
	if (first_.size >= track_.fileSize) {
		first_.size = track_.fileSize;
		if (bufferState_ == ATRAC_STATUS_HALFWAY_BUFFER)
			bufferState_ = ATRAC_STATUS_ALL_DATA_LOADED;
	}
	first_.fileoffset += addbytes;
	// refresh context_
	WriteContextToPSPMem();
	return 0;
}

u32 Atrac::GetNextSamples() {
	// It seems like the PSP aligns the sample position to 0x800...?
	u32 skipSamples = track_.FirstSampleOffsetFull();
	u32 firstSamples = (track_.SamplesPerFrame() - skipSamples) % track_.SamplesPerFrame();
	u32 numSamples = track_.endSample + 1 - currentSample_;
	if (currentSample_ == 0 && firstSamples != 0) {
		numSamples = firstSamples;
	}
	u32 unalignedSamples = (skipSamples + currentSample_) % track_.SamplesPerFrame();
	if (unalignedSamples != 0) {
		// We're off alignment, possibly due to a loop.  Force it back on.
		numSamples = track_.SamplesPerFrame() - unalignedSamples;
	}
	if (numSamples > track_.SamplesPerFrame())
		numSamples = track_.SamplesPerFrame();
	if (bufferState_ == ATRAC_STATUS_STREAMED_LOOP_FROM_END && (int)numSamples + currentSample_ > track_.endSample) {
		// This probably only happens in PPSSPP due to our internal buffer, which needs to go away.
		bufferState_ = ATRAC_STATUS_ALL_DATA_LOADED;
	}
	return numSamples;
}

void Atrac::ForceSeekToSample(int sample) {
	if (decoder_) {
		decoder_->FlushBuffers();
	}
	currentSample_ = sample;
}

void Atrac::SeekToSample(int sample) {
	// It seems like the PSP aligns the sample position to 0x800...?
	const u32 offsetSamples = track_.FirstSampleOffsetFull();
	const u32 unalignedSamples = (offsetSamples + sample) % track_.SamplesPerFrame();
	int seekFrame = sample + offsetSamples - unalignedSamples;

	if ((sample != currentSample_ || sample == 0) && decoder_ != nullptr) {
		// Prefill the decode buffer with packets before the first sample offset.
		decoder_->FlushBuffers();

		int adjust = 0;
		if (sample == 0) {
			int offsetSamples = track_.FirstSampleOffsetFull();
			adjust = -(int)(offsetSamples % track_.SamplesPerFrame());
		}
		const u32 off = track_.FileOffsetBySample(sample + adjust);
		const u32 backfill = track_.bytesPerFrame * 2;
		const u32 start = off - track_.dataByteOffset < backfill ? track_.dataByteOffset : off - backfill;

		for (u32 pos = start; pos < off; pos += track_.bytesPerFrame) {
			decoder_->Decode(BufferStart() + pos, track_.bytesPerFrame, nullptr, 2, nullptr, nullptr);
		}
	}

	currentSample_ = sample;
}

int Atrac::RemainingFrames() const {
	if (bufferState_ == ATRAC_STATUS_ALL_DATA_LOADED) {
		// Meaning, infinite I guess?  We've got it all.
		return PSP_ATRAC_ALLDATA_IS_ON_MEMORY;
	}

	u32 currentFileOffset = track_.FileOffsetBySample(currentSample_ - track_.SamplesPerFrame() + track_.FirstOffsetExtra());
	if (first_.fileoffset >= track_.fileSize) {
		if (bufferState_ == ATRAC_STATUS_STREAMED_WITHOUT_LOOP) {
			return PSP_ATRAC_NONLOOP_STREAM_DATA_IS_ON_MEMORY;
		}
		int loopEndAdjusted = track_.loopEndSample - track_.FirstOffsetExtra() - track_.firstSampleOffset;
		if (bufferState_ == ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER && currentSample_ > loopEndAdjusted) {
			// No longer looping in this case, outside the loop.
			return PSP_ATRAC_NONLOOP_STREAM_DATA_IS_ON_MEMORY;
		}
		if ((bufferState_ & ATRAC_STATUS_STREAMED_MASK) == ATRAC_STATUS_STREAMED_MASK && loopNum_ == 0) {
			return PSP_ATRAC_LOOP_STREAM_DATA_IS_ON_MEMORY;
		}
	}

	if ((bufferState_ & ATRAC_STATUS_STREAMED_MASK) == ATRAC_STATUS_STREAMED_MASK) {
		// Since we're streaming, the remaining frames are what's valid in the buffer.
		return bufferValidBytes_ / track_.bytesPerFrame;
	}

	// Since the first frame is shorter by this offset, add to round up at this offset.
	const int remainingBytes = first_.fileoffset - currentFileOffset;
	if (remainingBytes < 0) {
		// Just in case.  Shouldn't happen, but once did by mistake.
		return 0;
	}
	return remainingBytes / track_.bytesPerFrame;
}

void Atrac::ConsumeFrame() {
	bufferPos_ += track_.bytesPerFrame;
	if ((bufferState_ & ATRAC_STATUS_STREAMED_MASK) == ATRAC_STATUS_STREAMED_MASK) {
		if (bufferValidBytes_ > track_.bytesPerFrame) {
			bufferValidBytes_ -= track_.bytesPerFrame;
		} else {
			bufferValidBytes_ = 0;
		}
	}
	if (bufferPos_ >= StreamBufferEnd()) {
		// Wrap around... theoretically, this should only happen at exactly StreamBufferEnd.
		bufferPos_ -= StreamBufferEnd();
		bufferHeaderSize_ = 0;
	}
}

u32 Atrac::DecodeData(u8 *outbuf, u32 outbufPtr, u32 *SamplesNum, u32 *finish, int *remains) {
	int loopNum = loopNum_;
	if (bufferState_ == ATRAC_STATUS_FOR_SCESAS) {
		// TODO: Might need more testing.
		loopNum = 0;
	}

	// We already passed the end - return an error (many games check for this.)
	if (currentSample_ >= track_.endSample && loopNum == 0) {
		*SamplesNum = 0;
		*finish = 1;
		// refresh context_
		WriteContextToPSPMem();
		return SCE_ERROR_ATRAC_ALL_DATA_DECODED;
	}

	u32 numSamples = 0;

	// It seems like the PSP aligns the sample position to 0x800...?
	int offsetSamples = track_.FirstSampleOffsetFull();
	int skipSamples = 0;
	u32 maxSamples = track_.endSample + 1 - currentSample_;
	u32 unalignedSamples = (offsetSamples + currentSample_) % track_.SamplesPerFrame();
	if (unalignedSamples != 0) {
		// We're off alignment, possibly due to a loop.  Force it back on.
		maxSamples = track_.SamplesPerFrame() - unalignedSamples;
		skipSamples = unalignedSamples;
	}

	if (skipSamples != 0 && bufferHeaderSize_ == 0) {
		// Skip the initial frame used to load state for the looped frame.
		// TODO: We will want to actually read this in.
		// TODO again: This seems to happen on the first frame of playback regardless of loops.
		// Can't be good.
		ConsumeFrame();
	}

	SeekToSample(currentSample_);

	bool gotFrame = false;
	u32 off = track_.FileOffsetBySample(currentSample_ - skipSamples);
	if (off < first_.size) {
		uint8_t *indata = BufferStart() + off;
		int bytesConsumed = 0;
		int outSamples = track_.SamplesPerFrame();
		int outBytes = outSamples * outputChannels_ * sizeof(int16_t);
		gotFrame = true;

		numSamples = outSamples;
		uint32_t packetAddr = CurBufferAddress(-skipSamples);
		// got a frame
		int skipped = std::min((u32)skipSamples, numSamples);
		skipSamples -= skipped;
		numSamples = numSamples - skipped;
		// If we're at the end, clamp to samples we want.  It always returns a full chunk.
		numSamples = std::min(maxSamples, numSamples);

		outSamples = numSamples;
		if (!decoder_->Decode(indata, track_.bytesPerFrame, &bytesConsumed, outputChannels_, (int16_t *)outbuf, &outSamples)) {
			// Decode failed.
			*SamplesNum = 0;
			*finish = 1;
			return SCE_ERROR_ATRAC_ALL_DATA_DECODED;
		}

		if (packetAddr != 0 && MemBlockInfoDetailed()) {
			char tagData[128];
			size_t tagSize = FormatMemWriteTagAt(tagData, sizeof(tagData), "AtracDecode/", packetAddr, track_.bytesPerFrame);
			NotifyMemInfo(MemBlockFlags::READ, packetAddr, track_.bytesPerFrame, tagData, tagSize);
			NotifyMemInfo(MemBlockFlags::WRITE, outbufPtr, outBytes, tagData, tagSize);
		} else {
			NotifyMemInfo(MemBlockFlags::WRITE, outbufPtr, outBytes, "AtracDecode");
		}
		// We only want one frame per call, let's continue the next time.
	}

	if (!gotFrame && currentSample_ < track_.endSample) {
		// Never got a frame.  We may have dropped a GHA frame or otherwise have a bug.
		// For now, let's try to provide an extra "frame" if possible so games don't infinite loop.
		if (track_.FileOffsetBySample(currentSample_) < track_.fileSize) {
			numSamples = std::min(maxSamples, track_.SamplesPerFrame());
			u32 outBytes = numSamples * outputChannels_ * sizeof(s16);
			if (outbuf != nullptr) {
				memset(outbuf, 0, outBytes);
				NotifyMemInfo(MemBlockFlags::WRITE, outbufPtr, outBytes, "AtracDecode");
			}
		}
	}

	*SamplesNum = numSamples;
	// update current sample and decodePos
	currentSample_ += numSamples;
	decodePos_ = track_.DecodePosBySample(currentSample_);

	ConsumeFrame();

	int finishFlag = 0;
	// TODO: Verify.
	bool hitEnd = currentSample_ >= track_.endSample || (numSamples == 0 && first_.size >= track_.fileSize);
	int loopEndAdjusted = track_.loopEndSample - track_.FirstSampleOffsetFull();
	if ((hitEnd || currentSample_ > loopEndAdjusted) && loopNum != 0) {
		SeekToSample(track_.loopStartSample - track_.FirstSampleOffsetFull());
		if (bufferState_ != ATRAC_STATUS_FOR_SCESAS) {
			if (loopNum_ > 0)
				loopNum_--;
		}
		if ((bufferState_ & ATRAC_STATUS_STREAMED_MASK) == ATRAC_STATUS_STREAMED_MASK) {
			// Whatever bytes we have left were added from the loop.
			u32 loopOffset = track_.FileOffsetBySample(track_.loopStartSample - track_.FirstSampleOffsetFull() - track_.SamplesPerFrame() * 2);
			// TODO: Hmm, need to manage the buffer better.  But don't move fileoffset if we already have valid data.
			if (loopOffset > first_.fileoffset || loopOffset + bufferValidBytes_ < first_.fileoffset) {
				// Skip the initial frame at the start.
				first_.fileoffset = track_.FileOffsetBySample(track_.loopStartSample - track_.FirstSampleOffsetFull() - track_.SamplesPerFrame() * 2);
			}
		}
	} else if (hitEnd) {
		finishFlag = 1;

		// Still move forward, so we know that we've read everything.
		// This seems to be reflected in the context as well.
		currentSample_ += track_.SamplesPerFrame() - numSamples;
	}

	*finish = finishFlag;
	*remains = RemainingFrames();
	// refresh context_
	WriteContextToPSPMem();
	return 0;
}

int Atrac::SetLoopNum(int loopNum) {
	if (track_.loopinfo.size() == 0) {
		return SCE_ERROR_ATRAC_NO_LOOP_INFORMATION;
	}

	// Spammed in MHU
	loopNum_ = loopNum;
	// Logic here looks wacky?
	if (loopNum != 0 && track_.loopinfo.size() == 0) {
		// Just loop the whole audio
		// This is a rare modification of track_ after the fact.
		// Maybe we can get away with setting these by default.
		track_.loopStartSample = track_.FirstSampleOffsetFull();
		track_.loopEndSample = track_.endSample + track_.FirstSampleOffsetFull();
	}
	WriteContextToPSPMem();
	return 0;
}

u32 Atrac::ResetPlayPosition(int sample, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf) {
	// Reuse the same calculation as before.
	AtracResetBufferInfo bufferInfo;
	GetResetBufferInfo(&bufferInfo, sample);

	if ((u32)bytesWrittenFirstBuf < bufferInfo.first.minWriteBytes || (u32)bytesWrittenFirstBuf > bufferInfo.first.writableBytes) {
		return hleLogError(Log::ME, SCE_ERROR_ATRAC_BAD_FIRST_RESET_SIZE, "first byte count not in valid range");
	}
	if ((u32)bytesWrittenSecondBuf < bufferInfo.second.minWriteBytes || (u32)bytesWrittenSecondBuf > bufferInfo.second.writableBytes) {
		return hleLogError(Log::ME, SCE_ERROR_ATRAC_BAD_SECOND_RESET_SIZE, "second byte count not in valid range");
	}

	if (bufferState_ == ATRAC_STATUS_ALL_DATA_LOADED) {
		// Always adds zero bytes.
	} else if (bufferState_ == ATRAC_STATUS_HALFWAY_BUFFER) {
		// Okay, it's a valid number of bytes.  Let's set them up.
		if (bytesWrittenFirstBuf != 0) {
			if (!ignoreDataBuf_) {
				Memory::Memcpy(dataBuf_ + first_.size, first_.addr + first_.size, bytesWrittenFirstBuf, "AtracResetPlayPosition");
			}
			first_.fileoffset += bytesWrittenFirstBuf;
			first_.size += bytesWrittenFirstBuf;
			first_.offset += bytesWrittenFirstBuf;
		}

		// Did we transition to a full buffer?
		if (first_.size >= track_.fileSize) {
			first_.size = track_.fileSize;
			bufferState_ = ATRAC_STATUS_ALL_DATA_LOADED;
		}
	} else {
		if (bufferInfo.first.filePos > track_.fileSize) {
			return hleDelayResult(hleLogError(Log::ME, SCE_ERROR_ATRAC_API_FAIL, "invalid file position"), "reset play pos", 200);
		}

		// Move the offset to the specified position.
		first_.fileoffset = bufferInfo.first.filePos;

		if (bytesWrittenFirstBuf != 0) {
			if (!ignoreDataBuf_) {
				Memory::Memcpy(dataBuf_ + first_.fileoffset, first_.addr, bytesWrittenFirstBuf, "AtracResetPlayPosition");
			}
			first_.fileoffset += bytesWrittenFirstBuf;
		}
		first_.size = first_.fileoffset;
		first_.offset = bytesWrittenFirstBuf;

		bufferHeaderSize_ = 0;
		bufferPos_ = track_.bytesPerFrame;
		bufferValidBytes_ = bytesWrittenFirstBuf - bufferPos_;
	}

	if (track_.codecType == PSP_MODE_AT_3 || track_.codecType == PSP_MODE_AT_3_PLUS) {
		SeekToSample(sample);
	}

	WriteContextToPSPMem();
	return hleNoLog(0);
}

void Atrac::InitLowLevel(u32 paramsAddr, bool jointStereo, int atracID) {
	track_.channels = Memory::Read_U32(paramsAddr);
	outputChannels_ = Memory::Read_U32(paramsAddr + 4);
	bufferMaxSize_ = Memory::Read_U32(paramsAddr + 8);
	track_.bytesPerFrame = bufferMaxSize_;
	first_.writableBytes = track_.bytesPerFrame;
	ResetData();

	if (track_.codecType == PSP_MODE_AT_3) {
		track_.bitrate = (track_.bytesPerFrame * 352800) / 1000;
		track_.bitrate = (track_.bitrate + 511) >> 10;
		track_.jointStereo = false;
	} else if (track_.codecType == PSP_MODE_AT_3_PLUS) {
		track_.bitrate = (track_.bytesPerFrame * 352800) / 1000;
		track_.bitrate = ((track_.bitrate >> 11) + 8) & 0xFFFFFFF0;
		track_.jointStereo = false;
	}

	track_.dataByteOffset = 0;
	first_.size = 0;
	track_.fileSize = track_.bytesPerFrame;  // not really meaningful
	bufferState_ = ATRAC_STATUS_LOW_LEVEL;
	currentSample_ = 0;
	CreateDecoder();
	WriteContextToPSPMem();
}
