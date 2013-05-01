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

// This is pretty much a stub implementation. Doesn't actually do anything, just tries to return values
// to keep games happy anyway. So, no ATRAC3 music until someone has reverse engineered Atrac3+.


#include "HLE.h"
#include "../MIPS/MIPS.h"
#include "../CoreTiming.h"
#include "ChunkFile.h"

#include "sceKernel.h"
#include "sceUtility.h"

#define ATRAC_ERROR_API_FAIL                 0x80630002
#define ATRAC_ERROR_ALL_DATA_DECODED         0x80630024
#define ATRAC_ERROR_SECOND_BUFFER_NOT_NEEDED 0x80630022
#define ATRAC_ERROR_INCORRECT_READ_SIZE	     0x80630013

#define FMT_CHUNK_MAGIC		0x20746D66
#define DATA_CHUNK_MAGIC	0x61746164
#define SMPL_CHUNK_MAGIC	0x6C706D73
#define FACT_CHUNK_MAGIC	0x74636166

#define AT3_MAGIC			0x0270
#define AT3_PLUS_MAGIC		0xFFFE
#define RIFF_MAGIC			0x46464952
#define WAVE_MAGIC			0x45564157

#define PSP_MODE_AT_3_PLUS	0x00001000
#define PSP_MODE_AT_3		0x00001001

const u32 ATRAC_MAX_SAMPLES = 1024;

struct InputBuffer {
	u32 addr;
	u32 size;
	u32 offset;
	u32 writableBytes;
	u32 neededBytes;
	u32 filesize;
	u32 fileoffset;
};

struct AtracLoopInfo {
	int cuePointID;
	int type;
	int startSample;
	int endSample;
	int fraction;
	int playCount;
};

struct Atrac {
	Atrac() : decodePos(0), decodeEnd(0), loopNum(0), 
		atracChannels(2), atracBitrate(64), atracBytesPerFrame(0x0230), atracSampleRate(0xAC44),
		currentSample(0), endSample(-1), loopinfoNum(0), firstSampleoffset(0),
		loopStartSample(0), loopEndSample(-1) {
		memset(&first, 0, sizeof(first));
		memset(&second, 0, sizeof(second));
	}
	void DoState(PointerWrap &p) {
		p.Do(decodePos);
		p.Do(decodeEnd);
		p.Do(atracChannels);
		p.Do(atracBitrate);
		p.Do(atracSampleRate);
		p.Do(atracBytesPerFrame);
		p.Do(loopStartSample);
		p.Do(loopEndSample);
		p.Do(loopNum);
		p.Do(loopinfoNum);
		p.Do(currentSample);
		p.Do(endSample);
		p.Do(firstSampleoffset);
		p.Do(first);
		p.Do(second);
		p.DoMarker("Atrac");
	}

	void Analyze();

	u32 decodePos;
	u32 decodeEnd;
	u16 atracChannels;
	u32 atracBitrate;
	u16 atracBytesPerFrame;
	u32 atracSampleRate;
	u32 codeType;

	int loopStartSample;
	int loopEndSample;
	int loopNum;
	int loopinfoNum;
	int currentSample;
	int endSample;
	int firstSampleoffset;

	std::vector<AtracLoopInfo> loopinfo;

	InputBuffer first;
	InputBuffer second;
};

std::map<int, Atrac *> atracMap;
u8 nextAtracID;

void __AtracInit() {
	nextAtracID = 0;
}

void __AtracDoState(PointerWrap &p) {
	p.Do(atracMap);
	p.Do(nextAtracID);

	p.DoMarker("sceAtrac");
}

void __AtracShutdown() {
	for (auto it = atracMap.begin(), end = atracMap.end(); it != end; ++it) {
		delete it->second;
	}
	atracMap.clear();
}

Atrac *getAtrac(int atracID) {
	if (atracMap.find(atracID) == atracMap.end()) {
		return NULL;
	}
	return atracMap[atracID];
}

int createAtrac(Atrac *atrac) {
	int id = nextAtracID++;
	atracMap[id] = atrac;
	return id;
}

void deleteAtrac(int atracID) {
	if (atracMap.find(atracID) != atracMap.end()) {
		delete atracMap[atracID];
		atracMap.erase(atracID);
	}
}

int getCodecType(const u32 addr) {
	int at3magic = Memory::Read_U16(addr + 20);
	if (at3magic == AT3_MAGIC) {
		return PSP_MODE_AT_3;
	} else if (at3magic == AT3_PLUS_MAGIC) {
		return PSP_MODE_AT_3_PLUS;
	}
	return 0;
}

void Atrac::Analyze()
{
	currentSample = 0;
	endSample = -1;
	loopNum = 0;
	loopinfoNum = 0;
	loopStartSample = -1;
	loopEndSample = -1;
	decodePos = 0;

	if (first.size < 0x100)	{
		ERROR_LOG(HLE, "Atrac buffer very small: %d", first.size);
		return;
	}

	if (!Memory::IsValidAddress(first.addr)) {
		WARN_LOG(HLE, "Atrac buffer at invalid address: %08x-%08x", first.addr, first.size);
		return;
	}

	int RIFFMagic = Memory::Read_U32(first.addr);
	int WAVEMagic = Memory::Read_U32(first.addr + 8);
	if (RIFFMagic != RIFF_MAGIC || WAVEMagic != WAVE_MAGIC) {
		ERROR_LOG(HLE, "Not a RIFF or WAVE format: %08x-%08x", RIFFMagic, WAVEMagic);
		return;
	}

	first.filesize = Memory::Read_U32(first.addr + 4) + 8;

	u32 offset = 12;
	int atracSampleoffset = 0;

	this->decodeEnd = first.filesize;
	bool bfoundData = false;
	while ((first.filesize - offset) >= 8 && !bfoundData) {
		int chunkMagic = Memory::Read_U32(first.addr + offset);
		int chunkSize = Memory::Read_U32(first.addr + offset + 4);
		offset += 8;
		if (chunkSize > first.filesize - offset)
			break;

		switch (chunkMagic) {
			case FMT_CHUNK_MAGIC: {
					if (chunkSize >= 16) {
						atracChannels = Memory::Read_U16(first.addr + offset + 2);
						atracSampleRate = Memory::Read_U32(first.addr + offset + 4);  
						atracBitrate = Memory::Read_U32(first.addr + offset + 8);
						atracBytesPerFrame = Memory::Read_U16(first.addr + offset + 12);
					}
				}
				break;
			case FACT_CHUNK_MAGIC: {
					if (chunkSize >= 8) {
						endSample = Memory::Read_U32(first.addr + offset);
						atracSampleoffset = Memory::Read_U32(first.addr + offset + 4);
					}
				}
				break;
			case SMPL_CHUNK_MAGIC: {
					if (chunkSize >= 36) {
						int checkNumLoops = Memory::Read_U32(first.addr + offset + 28);
						if (chunkSize >= 36 + checkNumLoops * 24) {
							loopinfoNum = checkNumLoops;
							loopinfo.resize(loopinfoNum);
							u32 loopinfoAddr = first.addr + offset + 36;
							for (int i = 0; i < loopinfoNum; i++, loopinfoAddr += 24) {
								loopinfo[i].cuePointID = Memory::Read_U32(loopinfoAddr);
								loopinfo[i].type = Memory::Read_U32(loopinfoAddr + 4);
								loopinfo[i].startSample = Memory::Read_U32(loopinfoAddr + 8) - atracSampleoffset;
								loopinfo[i].endSample = Memory::Read_U32(loopinfoAddr + 12) - atracSampleoffset;
								loopinfo[i].fraction = Memory::Read_U32(loopinfoAddr + 16);
								loopinfo[i].playCount = Memory::Read_U32(loopinfoAddr + 20);

								if (loopinfo[i].endSample > endSample)
									loopinfo[i].endSample = endSample;
							}
						}
					} 
				}
				break;
			case DATA_CHUNK_MAGIC: {
					bfoundData = true;
					firstSampleoffset = offset;
				}
				break;
		}
		offset += chunkSize;
	}

	// set the loopStartSample and loopEndSample by loopinfo
	if (loopinfoNum > 0) {
		loopStartSample = loopinfo[0].startSample;
		loopEndSample = loopinfo[0].endSample;
	} else {
		loopStartSample = loopEndSample = -1;
	}

	// if there is no correct endsample, try to guess it
	if (endSample < 0)
		endSample = (first.filesize / atracBytesPerFrame) * ATRAC_MAX_SAMPLES;
}

u32 sceAtracGetAtracID(int codecType)
{
	ERROR_LOG(HLE, "FAKE sceAtracGetAtracID(%i)", codecType);
	return createAtrac(new Atrac);
}

u32 sceAtracAddStreamData(int atracID, u32 bytesToAdd)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracAddStreamData(%i, %08x)", atracID, bytesToAdd);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	// TODO
	return 0;
}

u32 sceAtracDecodeData(int atracID, u32 outAddr, u32 numSamplesAddr, u32 finishFlagAddr, u32 remainAddr)
{
	DEBUG_LOG(HLE, "FAKE sceAtracDecodeData(%i, %08x, %08x, %08x, %08x)", atracID, outAddr, numSamplesAddr, finishFlagAddr, remainAddr);
	Atrac *atrac = getAtrac(atracID);

	u32 ret = 0;
	if (atrac != NULL) {
		// We already passed the end - return an error (many games check for this.)
		if (atrac->decodePos >= atrac->decodeEnd && atrac->loopNum == 0) {
			Memory::Write_U32(0, numSamplesAddr);
			Memory::Write_U32(1, finishFlagAddr);
			Memory::Write_U32(0, remainAddr);

			ret = ATRAC_ERROR_ALL_DATA_DECODED;
		} else {
			// TODO: This isn't at all right, but at least it makes the music "last" some time.
			u32 numSamples = (atrac->decodeEnd - atrac->decodePos) / (sizeof(s16) * 2);
			if (atrac->decodePos >= atrac->decodeEnd) {
				numSamples = 0;
			} else if (numSamples > ATRAC_MAX_SAMPLES) {
				numSamples = ATRAC_MAX_SAMPLES;
			}

			// Should we loop?
			if (numSamples == 0 && atrac->loopNum != 0) {
				// Restart.
				atrac->decodePos = 0;
				if (atrac->loopNum > 0)
					atrac->loopNum--;
				numSamples = ATRAC_MAX_SAMPLES;
			}

			Memory::Memset(outAddr, 0, numSamples * sizeof(s16) * 2);
			Memory::Write_U32(numSamples, numSamplesAddr);
			atrac->decodePos += ATRAC_MAX_SAMPLES;

			if (numSamples < ATRAC_MAX_SAMPLES) {
				Memory::Write_U32(1, finishFlagAddr);
			} else {
				Memory::Write_U32(0, finishFlagAddr);
			}
			Memory::Write_U32(-1, remainAddr);
		}
	// TODO: Can probably remove this after we validate no wrong ids?
	} else {
		Memory::Write_U16(0, outAddr);	// Write a single 16-bit stereo
		Memory::Write_U16(0, outAddr + 2);

		Memory::Write_U32(1, numSamplesAddr);
		Memory::Write_U32(1, finishFlagAddr);	// Lie that decoding is finished
		Memory::Write_U32(-1, remainAddr);	// Lie that decoding is finished
	}

	return ret;
}

u32 sceAtracEndEntry()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracEndEntry(.)");
	return 0;
}

u32 sceAtracGetBufferInfoForReseting(int atracID, int sample, u32 bufferInfoAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetBufferInfoForReseting(%i, %i, %08x)",atracID, sample, bufferInfoAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		// TODO: Write the right stuff instead.
		Memory::Memset(bufferInfoAddr, 0, 32);
		//return -1;
	} else {
		int sampleOffset = (u32)(atrac->firstSampleoffset + sample / ATRAC_MAX_SAMPLES * atrac->atracBytesPerFrame);
		Memory::Write_U32(atrac->first.addr, bufferInfoAddr);
		Memory::Write_U32(atrac->first.writableBytes, bufferInfoAddr + 4);
		Memory::Write_U32(atrac->first.neededBytes, bufferInfoAddr + 8);
		Memory::Write_U32(sampleOffset, bufferInfoAddr + 12);
		Memory::Write_U32(atrac->second.addr, bufferInfoAddr + 16);
		Memory::Write_U32(atrac->second.writableBytes, bufferInfoAddr + 20);
		Memory::Write_U32(atrac->second.neededBytes, bufferInfoAddr + 24);
		Memory::Write_U32(atrac->second.fileoffset, bufferInfoAddr + 28);
	}
	return 0;
}

u32 sceAtracGetBitrate(int atracID, u32 outBitrateAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetBitrate(%i, %08x)", atracID, outBitrateAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}

	atrac->atracBitrate = ( atrac->atracBytesPerFrame * 352800 ) / 1000;
	if (atrac->codeType == PSP_MODE_AT_3_PLUS) 
		atrac->atracBitrate = ((atrac->atracBitrate >> 11) + 8) & 0xFFFFFFF0;
	else
		atrac->atracBitrate = (atrac->atracBitrate + 511) >> 10;

	if (Memory::IsValidAddress(outBitrateAddr))
		Memory::Write_U32(atrac->atracBitrate, outBitrateAddr);

	return 0;
}

u32 sceAtracGetChannel(int atracID, u32 channelAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetChannel(%i, %08x)", atracID, channelAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
//Fix Sengoku Musou 3 Z Special [JPN] crash
		return -1;
	}
	if (Memory::IsValidAddress(channelAddr))
		Memory::Write_U32(atrac->atracChannels, channelAddr);
	return 0;
}

u32 sceAtracGetLoopStatus(int atracID, u32 loopNumAddr, u32 statusAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetLoopStatus(%i, %08x, %08x)", atracID, loopNumAddr, statusAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	} else {
		if (Memory::IsValidAddress(loopNumAddr))
			Memory::Write_U32(atrac->loopNum, loopNumAddr);

		if (Memory::IsValidAddress(statusAddr)) {
			if (atrac->loopinfoNum > 0)
				Memory::Write_U32(1, statusAddr);
			else
				Memory::Write_U32(0, statusAddr);
		}
	}
	return 0;
}

u32 sceAtracGetInternalErrorInfo(int atracID, u32 errorAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetInternalErrorInfo(%i, %08x)", atracID, errorAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	if (Memory::IsValidAddress(errorAddr))
		Memory::Write_U32(0, errorAddr);
	return 0;
}

u32 sceAtracGetMaxSample(int atracID, u32 maxSamplesAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetMaxSample(%i, %08x)", atracID, maxSamplesAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	if (Memory::IsValidAddress(maxSamplesAddr))
		Memory::Write_U32(ATRAC_MAX_SAMPLES, maxSamplesAddr);
	return 0;
}

u32  sceAtracGetNextDecodePosition(int atracID, u32 outposAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetNextDecodePosition(%i, %08x)", atracID, outposAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	if (atrac->first.writableBytes - atrac->firstSampleoffset < atrac->atracBytesPerFrame || atrac->endSample < atrac->decodePos)
		return ATRAC_ERROR_ALL_DATA_DECODED;
	Memory::Write_U32(atrac->decodePos - atrac->currentSample, outposAddr); // outpos
	return 0; 
}

u32 sceAtracGetNextSample(int atracID, u32 outNAddr)
{
	ERROR_LOG(HLE, "FAKE sceAtracGetNextSample(%i, %08x)", atracID, outNAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
		Memory::Write_U32(1, outNAddr);
	} else {
		if (atrac->currentSample >= atrac->endSample) {
			Memory::Write_U32(0, outNAddr);
		} else {
			u32 numSamples = atrac->endSample - atrac->currentSample;
			if (numSamples > ATRAC_MAX_SAMPLES)
				numSamples = ATRAC_MAX_SAMPLES;
			Memory::Write_U32(numSamples, outNAddr);
		}
	}
	return 0;
}

u32 sceAtracGetRemainFrame(int atracID, u32 remainAddr)
{
	ERROR_LOG(HLE, "sceAtracGetRemainFrame(%i, %08x)", atracID, remainAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
		Memory::Write_U32(12, remainAddr); // outpos
	} else {
		// TODO: Seems like this is the number of bytes remaining to read from the file (or -1 if none)?
		Memory::Write_U32(-1, remainAddr);
	}
	return 0;
}

u32 sceAtracGetSecondBufferInfo(int atracID, u32 outposAddr, u32 outBytesAddr)
{
	ERROR_LOG(HLE, "sceAtracGetSecondBufferInfo(%i, %08x, %08x)", atracID, outposAddr, outBytesAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		Memory::Write_U32(0, outposAddr);
		Memory::Write_U32(0, outBytesAddr);
		return ATRAC_ERROR_SECOND_BUFFER_NOT_NEEDED;
	}

	// TODO: When sceAtracIsSecondBufferNeeded() says yes, change this function.  Maybe it's wrong anyway.
#if 0
	u32 pos = (((atrac->loopEndSample >> (0x100B - atrac->codeType)) + 1) * atrac->atracBytesPerFrame + atrac->second.fileoffset - 1 ) + 1;
	Memory::Write_U32(pos, outposAddr);
	Memory::Write_U32(atrac->second.writableBytes - pos, outBytesAddr);
	return 0;
#else
	Memory::Write_U32(0, outposAddr);
	Memory::Write_U32(0, outBytesAddr);
	return ATRAC_ERROR_SECOND_BUFFER_NOT_NEEDED;
#endif
}

u32 sceAtracGetSoundSample(int atracID, u32 outEndSampleAddr, u32 outLoopStartSampleAddr, u32 outLoopEndSampleAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetSoundSample(%i, %08x, %08x, %08x)", atracID, outEndSampleAddr, outLoopStartSampleAddr, outLoopEndSampleAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	if (Memory::IsValidAddress(outEndSampleAddr))
		Memory::Write_U32(atrac->endSample - atrac->currentSample, outEndSampleAddr);
	if (atrac->loopEndSample != 0) {
		if (Memory::IsValidAddress(outLoopStartSampleAddr))
			Memory::Write_U32(atrac->loopStartSample - atrac->currentSample, outLoopStartSampleAddr); 
		if (Memory::IsValidAddress(outLoopEndSampleAddr))
			Memory::Write_U32(atrac->loopEndSample - atrac->currentSample, outLoopEndSampleAddr); 
	} else {
		if (Memory::IsValidAddress(outLoopStartSampleAddr))
			Memory::Write_U32(-1, outLoopStartSampleAddr);
		if (Memory::IsValidAddress(outLoopEndSampleAddr))
			Memory::Write_U32(-1, outLoopEndSampleAddr);
	}
	return 0;
}

u32 sceAtracGetStreamDataInfo(int atracID, u32 writeAddr, u32 writableBytesAddr, u32 readOffsetAddr)
{
	ERROR_LOG(HLE, "FAKE sceAtracGetStreamDataInfo(%i, %08x, %08x, %08x)", atracID, writeAddr, writableBytesAddr, readOffsetAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	} else {
		Memory::Write_U32(atrac->first.addr, writeAddr);
		Memory::Write_U32(atrac->first.writableBytes, writableBytesAddr);
		Memory::Write_U32(atrac->first.fileoffset, readOffsetAddr);
	}
	return 0;
}

u32 sceAtracReleaseAtracID(int atracID)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracReleaseAtracID(%i)", atracID);
	deleteAtrac(atracID);
	return 0;
}

u32 sceAtracResetPlayPosition(int atracID, int sample, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracResetPlayPosition(%i, %i, %i, %i)", atracID, sample, bytesWrittenFirstBuf, bytesWrittenSecondBuf);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	} else {
		// TODO: Not sure what this means?
		atrac->decodePos = sample;
	}
	return 0;
}

u32 sceAtracSetHalfwayBuffer(int atracID, u32 halfBuffer, u32 readSize, u32 halfBufferSize)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetHalfwayBuffer(%i, %08x, %8x, %8x)", atracID, halfBuffer, readSize, halfBufferSize);
	if (readSize > halfBufferSize)
		return ATRAC_ERROR_INCORRECT_READ_SIZE;

	Atrac *atrac = getAtrac(atracID);
	if (atrac) {
		atrac->first.addr = halfBuffer;
		atrac->first.size = halfBufferSize;
		atrac->Analyze();
	}
	return 0;
}

u32 sceAtracSetSecondBuffer(int atracID, u32 secondBuffer, u32 secondBufferSize)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetSecondBuffer(%i, %08x, %8x)", atracID, secondBuffer, secondBufferSize);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	return 0;
}

u32 sceAtracSetData(int atracID, u32 buffer, u32 bufferSize)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetData(%i, %08x, %08x)", atracID, buffer, bufferSize);
	Atrac *atrac = getAtrac(atracID);
	if (atrac != NULL) {
		atrac->first.addr = buffer;
		atrac->first.size = bufferSize;
		atrac->Analyze();
	}
	return 0;
} 

int sceAtracSetDataAndGetID(u32 buffer, u32 bufferSize)
{	
	ERROR_LOG(HLE, "UNIMPL sceAtracSetDataAndGetID(%08x, %08x)", buffer, bufferSize);
	int codecType = getCodecType(buffer);

	Atrac *atrac = new Atrac();
	atrac->first.addr = buffer;
	atrac->first.size = bufferSize;
	atrac->Analyze();
	return createAtrac(atrac);
}

int sceAtracSetHalfwayBufferAndGetID(u32 halfBuffer, u32 readSize, u32 halfBufferSize)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetHalfwayBufferAndGetID(%08x, %08x, %08x)", halfBuffer, readSize, halfBufferSize);
	if (readSize > halfBufferSize)
		return ATRAC_ERROR_INCORRECT_READ_SIZE;

	if (readSize < 0 || halfBufferSize < 0) 
		return -1;

	int codecType = getCodecType(halfBuffer);
	Atrac *atrac = new Atrac();
	atrac->first.addr = halfBuffer;
	atrac->first.size = halfBufferSize;
	atrac->Analyze();
	return createAtrac(atrac);
}

u32 sceAtracStartEntry()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracStartEntry(.)");
	return 0;
}

u32 sceAtracSetLoopNum(int atracID, int loopNum)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetLoopNum(%i, %i)", atracID, loopNum);
	Atrac *atrac = getAtrac(atracID);
	if (atrac) {
		atrac->loopNum = loopNum;
		if (loopNum != 0 && atrac->loopinfoNum == 0) {
			atrac->loopStartSample = 0;
			atrac->loopEndSample = atrac->endSample;
		}
	}
	return 0;
}

int sceAtracReinit()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracReinit(..)");
	return 0;
}

int sceAtracGetOutputChannel(int atracID, u32 outputChanPtr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetOutputChannel(%i, %08x)", atracID, outputChanPtr);
	Atrac *atrac = getAtrac(atracID);
	if (Memory::IsValidAddress(outputChanPtr))
		Memory::Write_U32(atrac->atracChannels, outputChanPtr);
	return 0;
}

int sceAtracIsSecondBufferNeeded(int atracID)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracIsSecondBufferNeeded(%i)", atracID);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	return 0;
}

int sceAtracSetMOutHalfwayBuffer(int atracID, u32 MOutHalfBuffer, int readSize, int MOutHalfBufferSize)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetMOutHalfwayBuffer(%i, %08x, %i, %i)", atracID, MOutHalfBuffer, readSize, MOutHalfBufferSize);
	if (readSize > MOutHalfBufferSize)
		return ATRAC_ERROR_INCORRECT_READ_SIZE;
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	return 0;
}

int sceAtracSetAA3HalfwayBufferAndGetID(u32 halfBuffer, u32 readSize, u32 halfBufferSize)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetAA3HalfwayBufferAndGetID(%08x, %08x, %08x)", halfBuffer, readSize, halfBufferSize);
	if (readSize > halfBufferSize)
		return ATRAC_ERROR_INCORRECT_READ_SIZE;

	if (readSize < 0 || halfBufferSize < 0) 
		return -1;

	int codecType = getCodecType(halfBuffer);
	Atrac *atrac = new Atrac();
	atrac->first.addr = halfBuffer;
	atrac->first.size = halfBufferSize;
	atrac->Analyze();
	return createAtrac(atrac);
}

int sceAtracSetAA3DataAndGetID(u32 buffer, int bufferSize, int fileSize, u32 metadataSizeAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetAA3DataAndGetID(%08x, %i, %i, %08x)", buffer, bufferSize, fileSize, metadataSizeAddr);
	int codecType = getCodecType(buffer);

	Atrac *atrac = new Atrac();
	atrac->first.addr = buffer;
	atrac->first.size = bufferSize;
	atrac->Analyze();
	return createAtrac(atrac);
}

int _sceAtracGetContextAddress(int atracID)
{
	ERROR_LOG(HLE, "UNIMPL _sceAtracGetContextAddress(%i)", atracID);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	return 0;
}

int sceAtracLowLevelInitDecoder(int atracID, u32 paramsAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracLowLevelInitDecoder(%i, %08x)", atracID, paramsAddr);
	return 0;
}

int sceAtracLowLevelDecode(int atracID, u32 sourceAddr, u32 sourceBytesConsumedAddr, u32 samplesAddr, u32 sampleBytesAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracLowLevelDecode(%i, %08x, %08x, %08x, %08x)", atracID, sourceAddr, sourceBytesConsumedAddr, samplesAddr, sampleBytesAddr);
	if (Memory::IsValidAddress(sampleBytesAddr))
		Memory::Write_U32(0, sampleBytesAddr);
	return 0;
}

const HLEFunction sceAtrac3plus[] =
{
	{0x7db31251,WrapU_IU<sceAtracAddStreamData>,"sceAtracAddStreamData"},
	{0x6a8c3cd5,WrapU_IUUUU<sceAtracDecodeData>,"sceAtracDecodeData"},
	{0xd5c28cc0,WrapU_V<sceAtracEndEntry>,"sceAtracEndEntry"},
	{0x780f88d1,WrapU_I<sceAtracGetAtracID>,"sceAtracGetAtracID"},
	{0xca3ca3d2,WrapU_IIU<sceAtracGetBufferInfoForReseting>,"sceAtracGetBufferInfoForReseting"},
	{0xa554a158,WrapU_IU<sceAtracGetBitrate>,"sceAtracGetBitrate"},
	{0x31668baa,WrapU_IU<sceAtracGetChannel>,"sceAtracGetChannel"},
	{0xfaa4f89b,WrapU_IUU<sceAtracGetLoopStatus>,"sceAtracGetLoopStatus"},
	{0xe88f759b,WrapU_IU<sceAtracGetInternalErrorInfo>,"sceAtracGetInternalErrorInfo"},
	{0xd6a5f2f7,WrapU_IU<sceAtracGetMaxSample>,"sceAtracGetMaxSample"},
	{0xe23e3a35,WrapU_IU<sceAtracGetNextDecodePosition>,"sceAtracGetNextDecodePosition"},
	{0x36faabfb,WrapU_IU<sceAtracGetNextSample>,"sceAtracGetNextSample"},
	{0x9ae849a7,WrapU_IU<sceAtracGetRemainFrame>,"sceAtracGetRemainFrame"},
	{0x83e85ea0,WrapU_IUU<sceAtracGetSecondBufferInfo>,"sceAtracGetSecondBufferInfo"},
	{0xa2bba8be,WrapU_IUUU<sceAtracGetSoundSample>,"sceAtracGetSoundSample"},
	{0x5d268707,WrapU_IUUU<sceAtracGetStreamDataInfo>,"sceAtracGetStreamDataInfo"},
	{0x61eb33f5,WrapU_I<sceAtracReleaseAtracID>,"sceAtracReleaseAtracID"},
	{0x644e5607,WrapU_IIII<sceAtracResetPlayPosition>,"sceAtracResetPlayPosition"},
	{0x3f6e26b5,WrapU_IUUU<sceAtracSetHalfwayBuffer>,"sceAtracSetHalfwayBuffer"},
	{0x83bf7afd,WrapU_IUU<sceAtracSetSecondBuffer>,"sceAtracSetSecondBuffer"},
	{0x0E2A73AB,WrapU_IUU<sceAtracSetData>,"sceAtracSetData"}, //?
	{0x7a20e7af,WrapI_UU<sceAtracSetDataAndGetID>,"sceAtracSetDataAndGetID"},
	{0xd1f59fdb,WrapU_V<sceAtracStartEntry>,"sceAtracStartEntry"},
	{0x868120b5,WrapU_II<sceAtracSetLoopNum>,"sceAtracSetLoopNum"},
	{0x132f1eca,WrapI_V<sceAtracReinit>,"sceAtracReinit"},
	{0xeca32a99,WrapI_I<sceAtracIsSecondBufferNeeded>,"sceAtracIsSecondBufferNeeded"},
	{0x0fae370e,WrapI_UUU<sceAtracSetHalfwayBufferAndGetID>,"sceAtracSetHalfwayBufferAndGetID"},
	{0x2DD3E298,WrapU_IIU<sceAtracGetBufferInfoForReseting>,"sceAtracGetBufferInfoForResetting"},
	{0x5CF9D852,WrapI_IUII<sceAtracSetMOutHalfwayBuffer>,"sceAtracSetMOutHalfwayBuffer"},
	{0xB3B5D042,WrapI_IU<sceAtracGetOutputChannel>,"sceAtracGetOutputChannel"},
	{0xF6837A1A,0,"sceAtracSetMOutData"},
	{0x472E3825,0,"sceAtracSetMOutDataAndGetID"},
	{0x9CD7DE03,0,"sceAtracSetMOutHalfwayBufferAndGetID"},
	{0x5622B7C1,WrapI_UIIU<sceAtracSetAA3DataAndGetID>,"sceAtracSetAA3DataAndGetID"},
	{0x5DD66588,WrapI_UUU<sceAtracSetAA3HalfwayBufferAndGetID>,"sceAtracSetAA3HalfwayBufferAndGetID"},
	{0x231FC6B7,WrapI_I<_sceAtracGetContextAddress>,"_sceAtracGetContextAddress"},
	{0x1575D64B,WrapI_IU<sceAtracLowLevelInitDecoder>,"sceAtracLowLevelInitDecoder"},
	{0x0C116E1B,WrapI_IUUUU<sceAtracLowLevelDecode>,"sceAtracLowLevelDecode"},
};


void Register_sceAtrac3plus()
{
	// Two names
	RegisterModule("sceATRAC3plus_Library", ARRAY_SIZE(sceAtrac3plus), sceAtrac3plus);
	RegisterModule("sceAtrac3plus", ARRAY_SIZE(sceAtrac3plus), sceAtrac3plus);
}
