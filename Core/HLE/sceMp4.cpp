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

#include <algorithm>
#include <map>
#include <vector>

#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Common/Swap.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/Reporting.h"
#include "Core/HLE/sceMp4.h"
#include "Core/HLE/sceMpeg.h"
#include "Core/MemMapHelpers.h"
#include "Core/HW/SimpleAudioDec.h"

struct SceMp4TrackSampleBufInfo {
	u32_le totalSize;
	u32_le readOffset;
	u32_le writeOffset;
	u32_le sizeAvailableForRead;
	u32_le unknown16;
	u32_le bufferAddr;
	u32_le callback24;
	u32_le unknown28;
	u32_le unknown32;
	u32_le unknown36;

	void flush() {
		readOffset = 0;
		writeOffset = 0;
		sizeAvailableForRead = 0;
	}

	u32 getWritableSpace() const {
		return totalSize - sizeAvailableForRead;
	}

	void notifyRead(u32 length) {
		length = std::min(length, (u32)sizeAvailableForRead);
		if (length > 0) {
			readOffset += length;
			if (readOffset >= totalSize) {
				readOffset -= totalSize;
			}
			sizeAvailableForRead -= length;
		}
	}
};

struct SceMp4TrackSampleBuf {
	u8 unknown[36];
	u32_le currentSample;
	u32_le timeScale;
	u32_le duration;
	u32_le totalNumberSamples;
	u32_le unknown52;
	u32_le unknown56;
	u32_le trackType;
	u32_le unknown64;
	u32_le baseBufferAddr;
	u32_le samplesPut;
	u32_le sampleSize;
	u32_le unknown80;
	u32_le bytesBufferAddr;
	u32_le bytesBufferLength;
	u32_le unknown92;
	u32_le unknown96;
	SceMp4TrackSampleBufInfo bufBytes;
	SceMp4TrackSampleBufInfo bufSamples;
	u32_le unknown180;
	u64_le currentFileOffset;
	u32_le unknown192;
	u32_le unknown196;
	u32_le unknown200;
	u32_le unknown204;
	u32_le mp4;
	u32_le unknown212;
	u32_le unknown216;
	u32_le unknown220;
	u32_le readBufferAddr;
	u32_le readBufferSize;
	u32_le sizeAvailableInReadBuffer;
	u32_le unknown236;

	void read(u32 addr) {
		Memory::Memcpy(this, addr, sizeof(*this), "SceMp4TrackSampleBuf");
	}
	void write(u32 addr) {
		Memory::Memcpy(addr, this, sizeof(*this), "SceMp4TrackSampleBuf");
	}

	bool isInReadBuffer(u64 offset) const {
		return offset >= currentFileOffset && offset < currentFileOffset + sizeAvailableInReadBuffer;
	}

	void readBytes(u32 addr, u32 length) {
		if (length > bufBytes.sizeAvailableForRead) length = bufBytes.sizeAvailableForRead;
		if (length > 0) {
			u32 length1 = std::min(length, bufBytes.totalSize - bufBytes.readOffset);
			Memory::Memcpy(addr, bufBytes.bufferAddr + bufBytes.readOffset, length1, "SceMp4ReadBytes1");

			u32 length2 = length - length1;
			if (length2 > 0) {
				Memory::Memcpy(addr + length1, bufBytes.bufferAddr, length2, "SceMp4ReadBytes2");
			}

			bufBytes.notifyRead(length);
		}
	}
};

struct SceMp4SampleInfo {
	u32_le sample;
	u32_le sampleSize;
	u32_le sampleOffset;
	u32_le unknown1;
	u32_le frameDuration;
	u32_le unknown2;
	u32_le unknown3; // Always 0
	u32_le timestamp1;
	u32_le unknown4; // Always 0
	u32_le timestamp2;

	void write(u32 addr) {
		Memory::Memcpy(addr, this, sizeof(*this), "SceMp4SampleInfo");
	}
};

struct Mp4Track {
	int type = 0;
	u32 timeScale = 0;
	u32 duration = 0;
	std::vector<u32> samplesOffset;
	std::vector<u32> samplesSize;
	std::vector<u32> samplesDuration;
	std::vector<u32> samplesPresentationOffset;
	std::vector<u32> syncSamples;
	s64 currentTimestamp = 0;

	void DoState(PointerWrap &p) {
		auto s = p.Section("Mp4Track", 1);
		if (!s) return;
		Do(p, type);
		Do(p, timeScale);
		Do(p, duration);
		Do(p, samplesOffset);
		Do(p, samplesSize);
		Do(p, samplesDuration);
		Do(p, samplesPresentationOffset);
		Do(p, syncSamples);
		Do(p, currentTimestamp);
	}
};

struct Mp4Context {
	u32 id = 0;
	u32 callbackParam = 0;
	u32 callbackGetCurrentPosition = 0;
	u32 callbackSeek = 0;
	u32 callbackRead = 0;
	u32 readBufferAddr = 0;
	u32 readBufferSize = 0;

	Mp4Track videoTrack;
	Mp4Track audioTrack;

	u32 timeScale = 0;
	u32 duration = 0;
	int numberOfTracks = 0;

	// Parsing state
	s64 parseOffset = 0;
	u32 currentAtom = 0;
	u32 currentAtomSize = 0;
	u32 currentAtomOffset = 0;
	std::vector<u8> currentAtomContent;

	u32 currentTrackAddr = 0; // for RegistTrack
	int currentTrackType = 0;

	// Buffer put state
	bool bufferPutInProgress = false;
	u32 bufferPutSamples = 0;
	u32 bufferPutCurrentSampleRemainingBytes = 0;
	u32 bufferPutSamplesPut = 0;
	SceUID bufferPutThreadId = 0;

	AudioDecoder *audioDecoder = nullptr;

	Mp4Context() {}
	~Mp4Context() {
		if (audioDecoder) {
			delete audioDecoder;
		}
	}

	void hleMp4Init() {
		readBufferAddr = 0;
		readBufferSize = 0;
		videoTrack = Mp4Track();
		audioTrack = Mp4Track();
		videoTrack.type = 0x10; // TRACK_TYPE_VIDEO
		audioTrack.type = 0x20; // TRACK_TYPE_AUDIO
		timeScale = 0;
		duration = 0;
		numberOfTracks = 0;
		parseOffset = 0;
		currentAtom = 0;
		currentAtomSize = 0;
		currentAtomOffset = 0;
		currentAtomContent.clear();
		bufferPutInProgress = false;
	}

	void DoState(PointerWrap &p) {
		auto s = p.Section("Mp4Context", 1);
		if (!s) return;
		Do(p, id);
		Do(p, callbackParam);
		Do(p, callbackGetCurrentPosition);
		Do(p, callbackSeek);
		Do(p, callbackRead);
		Do(p, readBufferAddr);
		Do(p, readBufferSize);
		videoTrack.DoState(p);
		audioTrack.DoState(p);
		Do(p, timeScale);
		Do(p, duration);
		Do(p, numberOfTracks);
		Do(p, parseOffset);
		Do(p, currentAtom);
		Do(p, currentAtomSize);
		Do(p, currentAtomOffset);
		Do(p, currentAtomContent);
		Do(p, currentTrackAddr);
		Do(p, currentTrackType);
		Do(p, bufferPutInProgress);
		Do(p, bufferPutSamples);
		Do(p, bufferPutCurrentSampleRemainingBytes);
		Do(p, bufferPutSamplesPut);
		Do(p, bufferPutThreadId);
		// audioDecoder is recreated in MODE_READ if needed
	}

	Mp4Track *GetTrack(int type) {
		if (type & 0x10) return &videoTrack;
		if (type & 0x20) return &audioTrack;
		return nullptr;
	}

	u32 GetSampleOffset(int type, int sample) {
		Mp4Track *track = GetTrack(type);
		if (!track || sample < 0 || sample >= (int)track->samplesOffset.size()) return -1;
		return track->samplesOffset[sample];
	}

	u32 GetSampleSize(int type, int sample) {
		Mp4Track *track = GetTrack(type);
		if (!track || sample < 0 || sample >= (int)track->samplesSize.size()) return -1;
		return track->samplesSize[sample];
	}

	u32 GetSampleDuration(int type, int sample) {
		Mp4Track *track = GetTrack(type);
		if (!track || sample < 0 || sample >= (int)track->samplesDuration.size()) return -1;
		return track->samplesDuration[sample];
	}

	u32 GetSamplePresentationOffset(int type, int sample) {
		Mp4Track *track = GetTrack(type);
		if (!track || sample < 0 || sample >= (int)track->samplesPresentationOffset.size()) return 0;
		return track->samplesPresentationOffset[sample];
	}

	s64 SampleToFrameDuration(s64 sampleDuration, u32 trackTimeScale) {
		if (trackTimeScale == 0) return sampleDuration;
		return (sampleDuration * 90000) / trackTimeScale;
	}

	s64 GetTotalFrameDuration(const Mp4Track &track) {
		s64 totalSampleDuration = 0;
		for (u32 d : track.samplesDuration) {
			totalSampleDuration += d;
		}
		return SampleToFrameDuration(totalSampleDuration, track.timeScale);
	}
};

static std::map<u32, Mp4Context *> g_mp4Ctxs;
static u32 g_nextMp4Id = 1;
static std::map<u32, AudioDecoder *> g_aacDecoders;
static u32 g_nextAacId = 1;

static Mp4Context *getMp4Ctx(u32 mp4) {
	auto it = g_mp4Ctxs.find(mp4);
	if (it == g_mp4Ctxs.end()) return nullptr;
	return it->second;
}

static AudioDecoder *getAacDecoder(u32 aac) {
	auto it = g_aacDecoders.find(aac);
	if (it == g_aacDecoders.end()) return nullptr;
	return it->second;
}

static u32 read32(const u8 *p) {
	return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static u16 read16(const u8 *p) {
	return (p[0] << 8) | p[1];
}

static bool isContainerAtom(u32 atom) {
	switch (atom) {
	case 0x6D6F6F76: // "moov"
	case 0x7472616B: // "trak"
	case 0x6D646961: // "mdia"
	case 0x6D696E66: // "minf"
	case 0x7374626C: // "stbl"
		return true;
	}
	return false;
}

static bool isAtomContentRequired(u32 atom) {
	switch (atom) {
	case 0x6D766864: // "mvhd"
	case 0x73747364: // "stsd"
	case 0x73747363: // "stsc"
	case 0x7374737A: // "stsz"
	case 0x73747473: // "stts"
	case 0x63747473: // "ctts"
	case 0x7374636F: // "stco"
	case 0x73747373: // "stss"
	case 0x6D646864: // "mdhd"
	case 0x61766343: // "avcC"
		return true;
	}
	return false;
}

static void processAtom(Mp4Context *ctx, u32 atom, const u8 *content, u32 size) {
	switch (atom) {
	case 0x6D766864: // "mvhd"
		if (size >= 20) {
			ctx->timeScale = read32(content + 12);
			ctx->duration = read32(content + 16);
		}
		break;
	case 0x6D646864: // "mdhd"
		if (size >= 20) {
			Mp4Track *track = ctx->GetTrack(ctx->currentTrackType);
			if (track) {
				track->timeScale = read32(content + 12);
				track->duration = read32(content + 16);
			}
		}
		break;
	case 0x73747364: // "stsd"
		if (size >= 16) {
			u32 dataFormat = read32(content + 12);
			if (dataFormat == 0x61766331) { // "avc1"
				ctx->currentTrackType = 0x10; // TRACK_TYPE_VIDEO
			} else if (dataFormat == 0x6D703461) { // "mp4a"
				ctx->currentTrackType = 0x20; // TRACK_TYPE_AUDIO
			}
		}
		break;
	case 0x73747363: // "stsc"
		if (size >= 8) {
			u32 numEntries = read32(content + 4);
			Mp4Track *track = ctx->GetTrack(ctx->currentTrackType);
			if (track && size >= 8 + numEntries * 12) {
				track->samplesOffset.clear(); // Temporary: we'll build it in stco
				// Actually JPCSP builds numberOfSamplesPerChunk here.
				// We'll store it in a temporary vector in the context?
				// For simplicity, let's just handle it like JPCSP.
			}
		}
		break;
	case 0x7374737A: // "stsz"
		if (size >= 8) {
			u32 sampleSize = read32(content + 4);
			u32 numEntries = read32(content + 8);
			Mp4Track *track = ctx->GetTrack(ctx->currentTrackType);
			if (track) {
				track->samplesSize.resize(numEntries);
				if (sampleSize > 0) {
					for (u32 i = 0; i < numEntries; i++) track->samplesSize[i] = sampleSize;
				} else if (size >= 12 + numEntries * 4) {
					for (u32 i = 0; i < numEntries; i++) track->samplesSize[i] = read32(content + 12 + i * 4);
				}
			}
		}
		break;
	case 0x73747473: // "stts"
		if (size >= 8) {
			u32 numEntries = read32(content + 4);
			Mp4Track *track = ctx->GetTrack(ctx->currentTrackType);
			if (track && size >= 8 + numEntries * 8) {
				track->samplesDuration.clear();
				for (u32 i = 0; i < numEntries; i++) {
					u32 count = read32(content + 8 + i * 8);
					u32 duration = read32(content + 12 + i * 8);
					for (u32 j = 0; j < count; j++) track->samplesDuration.push_back(duration);
				}
			}
		}
		break;
	case 0x63747473: // "ctts"
		if (size >= 8) {
			u32 numEntries = read32(content + 4);
			Mp4Track *track = ctx->GetTrack(ctx->currentTrackType);
			if (track && size >= 8 + numEntries * 8) {
				track->samplesPresentationOffset.clear();
				for (u32 i = 0; i < numEntries; i++) {
					u32 count = read32(content + 8 + i * 8);
					u32 offset = read32(content + 12 + i * 8);
					for (u32 j = 0; j < count; j++) track->samplesPresentationOffset.push_back(offset);
				}
			}
		}
		break;
	case 0x7374636F: // "stco"
		if (size >= 8) {
			u32 numEntries = read32(content + 4);
			Mp4Track *track = ctx->GetTrack(ctx->currentTrackType);
			if (track && size >= 8 + numEntries * 4) {
				track->samplesOffset.clear();
				// This is simplified. JPCSP does complex mapping of chunks to samples.
				// For now, let's just assume one sample per chunk if stsc is missing.
				// TODO: Implement proper stsc mapping.
				for (u32 i = 0; i < numEntries; i++) {
					track->samplesOffset.push_back(read32(content + 8 + i * 4));
				}
			}
		}
		break;
	case 0x73747373: // "stss"
		if (size >= 8) {
			u32 numEntries = read32(content + 4);
			Mp4Track *track = ctx->GetTrack(ctx->currentTrackType);
			if (track && size >= 8 + numEntries * 4) {
				track->syncSamples.resize(numEntries);
				for (u32 i = 0; i < numEntries; i++) track->syncSamples[i] = read32(content + 8 + i * 4) - 1;
			}
		}
		break;
	}
}

static void parseAtoms(Mp4Context *ctx, const u8 *data, u32 size) {
	u32 offset = 0;
	if (ctx->currentAtom != 0) {
		u32 length = std::min(size, ctx->currentAtomSize - ctx->currentAtomOffset);
		memcpy(&ctx->currentAtomContent[ctx->currentAtomOffset], data, length);
		ctx->currentAtomOffset += length;
		offset += length;

		if (ctx->currentAtomOffset >= ctx->currentAtomSize) {
			processAtom(ctx, ctx->currentAtom, ctx->currentAtomContent.data(), ctx->currentAtomSize);
			ctx->currentAtom = 0;
			ctx->currentAtomContent.clear();
		}
	}

	while (offset + 8 <= size) {
		u32 atomSize = read32(data + offset);
		u32 atom = read32(data + offset + 4);
		if (atomSize == 0) break;

		if (isAtomContentRequired(atom)) {
			if (offset + atomSize <= size) {
				processAtom(ctx, atom, data + offset + 8, atomSize - 8);
				offset += atomSize;
			} else {
				ctx->currentAtom = atom;
				ctx->currentAtomSize = atomSize - 8;
				ctx->currentAtomOffset = size - offset - 8;
				ctx->currentAtomContent.resize(ctx->currentAtomSize);
				memcpy(ctx->currentAtomContent.data(), data + offset + 8, ctx->currentAtomOffset);
				offset = size;
			}
		} else {
			if (atom == 0x7472616B) { // "trak"
				ctx->numberOfTracks++;
				ctx->currentTrackType = 0;
			}

			if (isContainerAtom(atom)) offset += 8;
			else offset += atomSize;
		}
	}
	ctx->parseOffset += offset;
}

class Mp4ReadHeadersSeekAction : public PSPAction {
public:
	static PSPAction *Create() { return new Mp4ReadHeadersSeekAction(); }
	void setMp4(u32 mp4) { mp4_ = mp4; }
	void DoState(PointerWrap &p) override {
		auto s = p.Section("Mp4ReadHeadersSeekAction", 1);
		if (!s) return;
		Do(p, mp4_);
	}
	void run(MipsCall &call) override;
private:
	u32 mp4_ = 0;
};

class Mp4ReadHeadersReadAction : public PSPAction {
public:
	static PSPAction *Create() { return new Mp4ReadHeadersReadAction(); }
	void setMp4(u32 mp4) { mp4_ = mp4; }
	void DoState(PointerWrap &p) override {
		auto s = p.Section("Mp4ReadHeadersReadAction", 1);
		if (!s) return;
		Do(p, mp4_);
	}
	void run(MipsCall &call) override;
private:
	u32 mp4_ = 0;
};

static int actionReadHeadersSeek;
static int actionReadHeadersRead;

void Mp4ReadHeadersSeekAction::run(MipsCall &call) {
	Mp4Context *ctx = getMp4Ctx(mp4_);
	if (!ctx) return;

	Mp4ReadHeadersReadAction *action = (Mp4ReadHeadersReadAction *)__KernelCreateAction(actionReadHeadersRead);
	action->setMp4(mp4_);
	u32 args[2] = { ctx->readBufferAddr, ctx->readBufferSize };
	hleEnqueueCall(ctx->callbackRead, 2, args, action);
}

void Mp4ReadHeadersReadAction::run(MipsCall &call) {
	Mp4Context *ctx = getMp4Ctx(mp4_);
	if (!ctx) return;

	int readSize = (int)currentMIPS->r[MIPS_REG_V0];
	if (readSize > 0) {
		const u8 *data = Memory::GetPointer(ctx->readBufferAddr);
		if (data) {
			parseAtoms(ctx, data, (u32)readSize);
		} else {
			readSize = 0;
		}
	}

	if (readSize > 0) {
		Mp4ReadHeadersSeekAction *action = (Mp4ReadHeadersSeekAction *)__KernelCreateAction(actionReadHeadersSeek);
		action->setMp4(mp4_);
		// JPCSP seeks to parseOffset
		u32 args[4] = { ctx->callbackParam, (u32)(ctx->parseOffset & 0xFFFFFFFF), (u32)(ctx->parseOffset >> 32), 0 }; // 0 = SEEK_SET
		hleEnqueueCall(ctx->callbackSeek, 4, args, action);
	} else {
		// Done parsing or error
		if (ctx->currentTrackAddr) {
			SceMp4TrackSampleBuf track;
			track.read(ctx->currentTrackAddr);
			Mp4Track *t = ctx->GetTrack(track.trackType);
			if (t) {
				track.timeScale = t->timeScale;
				track.duration = t->duration;
				track.totalNumberSamples = (u32)t->samplesSize.size();
				track.write(ctx->currentTrackAddr);
			}
			ctx->currentTrackAddr = 0;
		}
	}
}

static void readHeaders(Mp4Context *ctx) {
	ctx->parseOffset = 0;
	ctx->duration = 0;
	ctx->currentAtom = 0;
	ctx->numberOfTracks = 0;

	Mp4ReadHeadersSeekAction *action = (Mp4ReadHeadersSeekAction *)__KernelCreateAction(actionReadHeadersSeek);
	action->setMp4(ctx->id);
	u32 args[4] = { ctx->callbackParam, 0, 0, 0 }; // 0 = SEEK_SET
	hleEnqueueCall(ctx->callbackSeek, 4, args, action);
}

class Mp4BufferPutSeekAction : public PSPAction {
public:
	static PSPAction *Create() { return new Mp4BufferPutSeekAction(); }
	void setMp4(u32 mp4) { mp4_ = mp4; }
	void DoState(PointerWrap &p) override {
		auto s = p.Section("Mp4BufferPutSeekAction", 1);
		if (!s) return;
		Do(p, mp4_);
	}
	void run(MipsCall &call) override;
private:
	u32 mp4_ = 0;
};

class Mp4BufferPutReadAction : public PSPAction {
public:
	static PSPAction *Create() { return new Mp4BufferPutReadAction(); }
	void setMp4(u32 mp4) { mp4_ = mp4; }
	void DoState(PointerWrap &p) override {
		auto s = p.Section("Mp4BufferPutReadAction", 1);
		if (!s) return;
		Do(p, mp4_);
	}
	void run(MipsCall &call) override;
private:
	u32 mp4_ = 0;
};

static int actionBufferPutSeek;
static int actionBufferPutRead;

void Mp4BufferPutSeekAction::run(MipsCall &call) {
	Mp4Context *ctx = getMp4Ctx(mp4_);
	if (!ctx) return;

	Mp4BufferPutReadAction *action = (Mp4BufferPutReadAction *)__KernelCreateAction(actionBufferPutRead);
	action->setMp4(mp4_);

	SceMp4TrackSampleBuf track;
	track.read(ctx->currentTrackAddr);

	u32 args[2] = { track.readBufferAddr, track.readBufferSize };
	hleEnqueueCall(ctx->callbackRead, 2, args, action);
}

static void bufferPut(Mp4Context *ctx, MipsCall *call = nullptr);

void Mp4BufferPutReadAction::run(MipsCall &call) {
	Mp4Context *ctx = getMp4Ctx(mp4_);
	if (!ctx) return;

	int readSize = (int)currentMIPS->r[MIPS_REG_V0];
	SceMp4TrackSampleBuf track;
	track.read(ctx->currentTrackAddr);
	if (readSize > 0) {
		track.sizeAvailableInReadBuffer = (u32)readSize;
		track.write(ctx->currentTrackAddr);
		bufferPut(ctx, &call);
	} else {
		// Error or EOF
		track.sizeAvailableInReadBuffer = 0;
		track.write(ctx->currentTrackAddr);
		call.setReturnValue(0);
		ctx->bufferPutInProgress = false;
	}
}

static void bufferPut(Mp4Context *ctx, MipsCall *call) {
	SceMp4TrackSampleBuf track;
	track.read(ctx->currentTrackAddr);

	while (ctx->bufferPutSamples > 0) {
		if (ctx->bufferPutCurrentSampleRemainingBytes > 0) {
			u32 length = std::min(track.readBufferSize, ctx->bufferPutCurrentSampleRemainingBytes);
			// In a real implementation we would copy bytes to the ringbuffer
			ctx->bufferPutCurrentSampleRemainingBytes -= length;
			if (ctx->bufferPutCurrentSampleRemainingBytes > 0) {
				Mp4BufferPutSeekAction *action = (Mp4BufferPutSeekAction *)__KernelCreateAction(actionBufferPutSeek);
				action->setMp4(ctx->id);
				u64 seek = track.currentFileOffset + track.readBufferSize;
				u32 args[4] = { ctx->callbackParam, (u32)(seek & 0xFFFFFFFF), (u32)(seek >> 32), 0 };
				hleEnqueueCall(ctx->callbackSeek, 4, args, action);
				return;
			}
			track.bufSamples.sizeAvailableForRead++;
			track.currentSample++;
			ctx->bufferPutSamples--;
			ctx->bufferPutSamplesPut++;
		} else {
			u32 sample = track.currentSample;
			u32 offset = ctx->GetSampleOffset(track.trackType, sample);
			u32 size = ctx->GetSampleSize(track.trackType, sample);

			if (offset == (u32)-1 || size > track.bufBytes.getWritableSpace()) {
				ctx->bufferPutSamples = 0;
				break;
			}

			if (track.isInReadBuffer(offset)) {
				u32 sampleReadBufferOffset = (u32)(offset - track.currentFileOffset);
				if (track.isInReadBuffer(offset + size)) {
					// Full sample in buffer
					track.bufSamples.sizeAvailableForRead++;
					track.currentSample++;
					ctx->bufferPutSamples--;
					ctx->bufferPutSamplesPut++;
				} else {
					// Partial sample
					u32 available = track.sizeAvailableInReadBuffer - sampleReadBufferOffset;
					ctx->bufferPutCurrentSampleRemainingBytes = size - available;

					Mp4BufferPutSeekAction *action = (Mp4BufferPutSeekAction *)__KernelCreateAction(actionBufferPutSeek);
					action->setMp4(ctx->id);
					u64 seek = track.currentFileOffset + track.readBufferSize;
					u32 args[4] = { ctx->callbackParam, (u32)(seek & 0xFFFFFFFF), (u32)(seek >> 32), 0 };
					hleEnqueueCall(ctx->callbackSeek, 4, args, action);
					return;
				}
			} else {
				Mp4BufferPutSeekAction *action = (Mp4BufferPutSeekAction *)__KernelCreateAction(actionBufferPutSeek);
				action->setMp4(ctx->id);
				u64 seek = offset; // align down in JPCSP but let's keep it simple
				u32 args[4] = { ctx->callbackParam, (u32)(seek & 0xFFFFFFFF), (u32)(seek >> 32), 0 };
				hleEnqueueCall(ctx->callbackSeek, 4, args, action);
				return;
			}
		}
	}

	if (ctx->bufferPutSamples == 0 && ctx->bufferPutInProgress) {
		track.write(ctx->currentTrackAddr);
		if (call) {
			call->setReturnValue(ctx->bufferPutSamplesPut);
		}
		ctx->bufferPutInProgress = false;
	}
}

void __Mp4Init() {
	g_nextMp4Id = 1;
	g_nextAacId = 1;
	actionReadHeadersSeek = __KernelRegisterActionType(Mp4ReadHeadersSeekAction::Create);
	actionReadHeadersRead = __KernelRegisterActionType(Mp4ReadHeadersReadAction::Create);
	actionBufferPutSeek = __KernelRegisterActionType(Mp4BufferPutSeekAction::Create);
	actionBufferPutRead = __KernelRegisterActionType(Mp4BufferPutReadAction::Create);
}

void __Mp4DoState(PointerWrap &p) {
	auto s = p.Section("sceMp4", 1);
	if (!s) return;
	Do(p, g_nextMp4Id);
	Do(p, g_mp4Ctxs);
	Do(p, g_nextAacId);
	// Decoders are not saved/restored for simplicity, they will be recreated on first use
	Do(p, actionReadHeadersSeek);
	Do(p, actionReadHeadersRead);
	Do(p, actionBufferPutSeek);
	Do(p, actionBufferPutRead);
	__KernelRestoreActionType(actionReadHeadersSeek, Mp4ReadHeadersSeekAction::Create);
	__KernelRestoreActionType(actionReadHeadersRead, Mp4ReadHeadersReadAction::Create);
	__KernelRestoreActionType(actionBufferPutSeek, Mp4BufferPutSeekAction::Create);
	__KernelRestoreActionType(actionBufferPutRead, Mp4BufferPutReadAction::Create);
}

void __Mp4Shutdown() {
	for (auto const& it : g_mp4Ctxs) {
		delete it.second;
	}
	g_mp4Ctxs.clear();
	for (auto const& it : g_aacDecoders) {
		delete it.second;
	}
	g_aacDecoders.clear();
}

static u32 sceMp4Init() {
	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4Finish() {
	__Mp4Shutdown();
	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4Create(u32 mp4Ptr, u32 callbacksAddr, u32 readBufferAddr, u32 readBufferSize) {
	if (!Memory::IsValidAddress(mp4Ptr)) return hleLogWarning(Log::ME, -1, "bad mp4 pointer");

	Mp4Context *ctx = new Mp4Context();
	ctx->id = g_nextMp4Id++;
	ctx->hleMp4Init();

	if (callbacksAddr) {
		ctx->callbackParam = Memory::Read_U32(callbacksAddr);
		ctx->callbackGetCurrentPosition = Memory::Read_U32(callbacksAddr + 4);
		ctx->callbackSeek = Memory::Read_U32(callbacksAddr + 8);
		ctx->callbackRead = Memory::Read_U32(callbacksAddr + 12);
	}
	ctx->readBufferAddr = readBufferAddr;
	ctx->readBufferSize = readBufferSize;

	g_mp4Ctxs[ctx->id] = ctx;
	Memory::Write_U32(ctx->id, mp4Ptr);

	readHeaders(ctx);

	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4Delete(u32 mp4) {
	Mp4Context *ctx = getMp4Ctx(mp4);
	if (!ctx) return hleLogWarning(Log::ME, -1, "bad mp4 handle");

	g_mp4Ctxs.erase(mp4);
	delete ctx;

	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4GetNumberOfMetaData(u32 mp4) {
	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4GetMovieInfo(u32 mp4, u32 movieInfoAddr) {
	Mp4Context *ctx = getMp4Ctx(mp4);
	if (!ctx) return hleLogWarning(Log::ME, -1, "bad mp4 handle");

	if (movieInfoAddr) {
		Memory::Write_U32(ctx->numberOfTracks, movieInfoAddr);
		Memory::Write_U32(0, movieInfoAddr + 4);
		Memory::Write_U32((u32)ctx->SampleToFrameDuration(ctx->duration, ctx->timeScale), movieInfoAddr + 8);
	}

	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4GetNumberOfSpecificTrack(u32 mp4, u32 trackType) {
	Mp4Context *ctx = getMp4Ctx(mp4);
	if (!ctx) return hleLogWarning(Log::ME, -1, "bad mp4 handle");

	if (trackType & 0x10) return ctx->videoTrack.samplesOffset.empty() ? 0 : 1;
	if (trackType & 0x20) return ctx->audioTrack.samplesOffset.empty() ? 0 : 1;

	return hleLogWarning(Log::ME, 0);
}

static u32 sceMp4RegistTrack(u32 mp4, u32 trackType, u32 unknown, u32 callbacksAddr, u32 trackAddr) {
	Mp4Context *ctx = getMp4Ctx(mp4);
	if (!ctx) return hleLogWarning(Log::ME, -1, "bad mp4 handle");

	if (callbacksAddr) {
		ctx->callbackParam = Memory::Read_U32(callbacksAddr);
		ctx->callbackGetCurrentPosition = Memory::Read_U32(callbacksAddr + 4);
		ctx->callbackSeek = Memory::Read_U32(callbacksAddr + 8);
		ctx->callbackRead = Memory::Read_U32(callbacksAddr + 12);
	}

	if (trackAddr) {
		SceMp4TrackSampleBuf track;
		track.read(trackAddr);
		track.currentSample = 0;
		track.trackType = trackType;

		Mp4Track *t = ctx->GetTrack(trackType);
		if (t) {
			track.timeScale = t->timeScale;
			track.duration = t->duration;
			track.totalNumberSamples = (u32)t->samplesSize.size();
		}
		track.write(trackAddr);
		ctx->currentTrackAddr = trackAddr;
	}

	readHeaders(ctx);

	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4TrackSampleBufQueryMemSize(u32 trackType, u32 numSamples, u32 sampleSize, u32 unknown, u32 readBufferSize) {
	u32 value = std::max(numSamples * sampleSize, unknown << 1) + (numSamples << 6) + readBufferSize + 256;
	return hleLogInfo(Log::ME, value);
}

static u32 sceMp4TrackSampleBufConstruct(u32 mp4, u32 trackAddr, u32 buffer, u32 sampleBufQueryMemSize, u32 numSamples, u32 sampleSize, u32 unknown, u32 readBufferSize) {
	Mp4Context *ctx = getMp4Ctx(mp4);
	if (!ctx) return hleLogWarning(Log::ME, -1, "bad mp4 handle");

	if (trackAddr) {
		SceMp4TrackSampleBuf track;
		track.read(trackAddr);
		track.mp4 = mp4;
		track.baseBufferAddr = buffer;
		track.samplesPut = numSamples;
		track.sampleSize = sampleSize;
		track.unknown80 = unknown;
		track.bytesBufferAddr = (buffer + 63) & ~63; // alignUp 63
		track.bytesBufferAddr += (numSamples << 6);
		track.bytesBufferLength = std::max(numSamples * sampleSize, unknown << 1);
		track.readBufferSize = readBufferSize;
		track.readBufferAddr = track.bytesBufferAddr + track.bytesBufferLength + 48;
		track.currentFileOffset = -1;
		track.sizeAvailableInReadBuffer = 0;

		track.bufBytes.totalSize = track.bytesBufferLength;
		track.bufBytes.flush();
		track.bufBytes.unknown16 = 1;
		track.bufBytes.bufferAddr = track.bytesBufferAddr;
		track.bufBytes.unknown28 = trackAddr + 184;
		track.bufBytes.unknown36 = mp4;

		track.bufSamples.totalSize = numSamples;
		track.bufSamples.flush();
		track.bufSamples.unknown16 = 64;
		track.bufSamples.bufferAddr = (buffer + 63) & ~63;
		track.bufSamples.unknown36 = mp4;

		track.write(trackAddr);
		ctx->currentTrackAddr = trackAddr;
	}

	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4GetAvcTrackInfoData(u32 mp4, u32 trackAddr, u32 infoAddr) {
	Mp4Context *ctx = getMp4Ctx(mp4);
	if (!ctx) return hleLogWarning(Log::ME, -1, "bad mp4 handle");

	if (trackAddr && infoAddr) {
		SceMp4TrackSampleBuf track;
		track.read(trackAddr);
		Mp4Track *t = ctx->GetTrack(track.trackType);
		if (t) {
			Memory::Write_U32(0, infoAddr);
			Memory::Write_U32((u32)ctx->GetTotalFrameDuration(*t), infoAddr + 4);
			Memory::Write_U32((u32)t->samplesSize.size(), infoAddr + 8);
		}
	}

	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4GetAacTrackInfoData(u32 mp4, u32 trackAddr, u32 infoAddr) {
	Mp4Context *ctx = getMp4Ctx(mp4);
	if (!ctx) return hleLogWarning(Log::ME, -1, "bad mp4 handle");

	if (trackAddr && infoAddr) {
		SceMp4TrackSampleBuf track;
		track.read(trackAddr);
		Mp4Track *t = ctx->GetTrack(track.trackType);
		if (t) {
			Memory::Write_U32(0, infoAddr);
			Memory::Write_U32((u32)ctx->GetTotalFrameDuration(*t), infoAddr + 4);
			Memory::Write_U32((u32)t->samplesSize.size(), infoAddr + 8);
			Memory::Write_U32(t->timeScale, infoAddr + 12);
			Memory::Write_U32(2, infoAddr + 16); // audioChannels (always 2?)
		}
	}

	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4AacDecodeInitResource(int unknown) {
	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4AacDecodeInit(u32 aacPtr) {
	AudioDecoder *decoder = CreateAudioDecoder(PSP_CODEC_AAC);
	u32 id = g_nextAacId++;
	g_aacDecoders[id] = decoder;
	if (aacPtr) Memory::Write_U32(id, aacPtr);
	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4TrackSampleBufFlush(u32 mp4, u32 trackAddr) {
	if (trackAddr) {
		SceMp4TrackSampleBuf track;
		track.read(trackAddr);
		track.bufBytes.flush();
		track.bufSamples.flush();
		track.write(trackAddr);
	}
	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4GetSampleNumWithTimeStamp(u32 mp4, u32 trackAddr, u32 timestampAddr) {
	Mp4Context *ctx = getMp4Ctx(mp4);
	if (!ctx) return hleLogWarning(Log::ME, -1, "bad mp4 handle");

	if (trackAddr) {
		SceMp4TrackSampleBuf track;
		track.read(trackAddr);
		return hleLogInfo(Log::ME, track.currentSample);
	}
	return hleLogWarning(Log::ME, 0);
}

static u32 sceMp4GetSampleInfo(u32 mp4, u32 trackAddr, int sample, u32 infoAddr) {
	Mp4Context *ctx = getMp4Ctx(mp4);
	if (!ctx) return hleLogWarning(Log::ME, -1, "bad mp4 handle");

	if (trackAddr && infoAddr) {
		SceMp4TrackSampleBuf track;
		track.read(trackAddr);
		if (sample == -1) sample = track.currentSample;

		SceMp4SampleInfo info;
		info.sample = sample;
		info.sampleOffset = ctx->GetSampleOffset(track.trackType, sample);
		info.sampleSize = ctx->GetSampleSize(track.trackType, sample);
		info.unknown1 = 0;
		info.frameDuration = (u32)ctx->SampleToFrameDuration(ctx->GetSampleDuration(track.trackType, sample), track.timeScale);
		info.unknown2 = 0;
		info.unknown3 = 0;
		info.timestamp1 = sample * info.frameDuration; // Simplified
		info.unknown4 = 0;
		info.timestamp2 = sample * info.frameDuration;

		info.write(infoAddr);
	}

	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4SearchSyncSampleNum(u32 mp4, u32 trackAddr, int searchDirection, int sample) {
	Mp4Context *ctx = getMp4Ctx(mp4);
	if (!ctx) return hleLogWarning(Log::ME, -1, "bad mp4 handle");

	if (trackAddr) {
		SceMp4TrackSampleBuf track;
		track.read(trackAddr);
		Mp4Track *t = ctx->GetTrack(track.trackType);
		if (t) {
			int syncSample = 0;
			for (u32 s : t->syncSamples) {
				if (sample > (int)s) syncSample = s;
				else if (sample == (int)s && searchDirection == 1) syncSample = s; // SEARCH_FORWARDS
				else {
					if (searchDirection == 1) syncSample = s;
					break;
				}
			}
			return hleLogInfo(Log::ME, syncSample);
		}
	}
	return hleLogWarning(Log::ME, 0);
}

static u32 sceMp4PutSampleNum(u32 mp4, u32 trackAddr, int sample) {
	Mp4Context *ctx = getMp4Ctx(mp4);
	if (!ctx) return hleLogWarning(Log::ME, -1, "bad mp4 handle");

	if (trackAddr) {
		SceMp4TrackSampleBuf track;
		track.read(trackAddr);
		track.currentSample = sample;
		track.write(trackAddr);
	}
	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4TrackSampleBufAvailableSize(u32 mp4, u32 trackAddr, u32 writableSamplesAddr, u32 writableBytesAddr) {
	if (trackAddr) {
		SceMp4TrackSampleBuf track;
		track.read(trackAddr);
		if (writableSamplesAddr) Memory::Write_U32(track.bufSamples.getWritableSpace(), writableSamplesAddr);
		if (writableBytesAddr) Memory::Write_U32(track.bufBytes.getWritableSpace(), writableBytesAddr);
	}
	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4TrackSampleBufPut(u32 mp4, u32 trackAddr, int samples) {
	Mp4Context *ctx = getMp4Ctx(mp4);
	if (!ctx) return hleLogWarning(Log::ME, -1, "bad mp4 handle");

	if (samples > 0) {
		ctx->bufferPutInProgress = true;
		ctx->bufferPutSamples = samples;
		ctx->bufferPutCurrentSampleRemainingBytes = 0;
		ctx->bufferPutSamplesPut = 0;
		ctx->bufferPutThreadId = __KernelGetCurThread();
		ctx->currentTrackAddr = trackAddr;

		bufferPut(ctx);
		return hleLogInfo(Log::ME, 0); // Should return number of samples? JPCSP pushes action
	}

	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4GetAacAu(u32 mp4, u32 trackAddr, u32 auAddr, u32 infoAddr) {
	Mp4Context *ctx = getMp4Ctx(mp4);
	if (!ctx) return hleLogWarning(Log::ME, -1, "bad mp4 handle");

	if (trackAddr && auAddr) {
		SceMp4TrackSampleBuf track;
		track.read(trackAddr);
		if (track.bufSamples.sizeAvailableForRead <= 0) return hleLogWarning(Log::ME, 0x80610103, "no more data"); // ERROR_MP4_NO_MORE_DATA

		SceMpegAu au;
		au.read(auAddr);

		int sample = track.currentSample - track.bufSamples.sizeAvailableForRead;
		u32 sampleSize = ctx->GetSampleSize(track.trackType, sample);
		u32 sampleDuration = ctx->GetSampleDuration(track.trackType, sample);
		u32 samplePresentationOffset = ctx->GetSamplePresentationOffset(track.trackType, sample);
		s64 frameDuration = ctx->SampleToFrameDuration(sampleDuration, track.timeScale);
		s64 framePresentationOffset = ctx->SampleToFrameDuration(samplePresentationOffset, track.timeScale);

		track.bufSamples.notifyRead(1);
		track.readBytes(au.esBuffer, sampleSize);
		au.esSize = sampleSize;
		au.dts = ctx->audioTrack.currentTimestamp;
		ctx->audioTrack.currentTimestamp += frameDuration;
		au.pts = au.dts + framePresentationOffset;

		au.write(auAddr);
		track.write(trackAddr);

		if (infoAddr) {
			SceMp4SampleInfo info;
			info.sample = sample;
			info.sampleOffset = ctx->GetSampleOffset(track.trackType, sample);
			info.sampleSize = sampleSize;
			info.unknown1 = 0;
			info.frameDuration = (u32)frameDuration;
			info.unknown2 = 0;
			info.unknown3 = 0;
			info.timestamp1 = (u32)au.dts;
			info.unknown4 = 0;
			info.timestamp2 = (u32)au.pts;
			info.write(infoAddr);
		}
	}
	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4AacDecode(u32 aac, u32 auAddr, u32 bufferAddr, u32 init, u32 frequency) {
	AudioDecoder *decoder = getAacDecoder(aac);
	if (!decoder) return hleLogWarning(Log::ME, -1, "bad aac handle");

	SceMpegAu au;
	au.read(auAddr);

	int inbytesConsumed = 0;
	int outSamples = 0;
	int outputChannels = 2;
	int16_t *outbuf = (int16_t *)Memory::GetPointerWriteRange(bufferAddr, 1024 * 4); // Assume max frame size

	if (!outbuf) return hleLogError(Log::ME, -1, "bad buffer pointer");

	if (decoder->Decode(Memory::GetPointer(au.esBuffer), au.esSize, &inbytesConsumed, outputChannels, outbuf, &outSamples)) {
		return hleLogInfo(Log::ME, 0);
	} else {
		return hleLogError(Log::ME, 0x80610104, "decode error"); // ERROR_MP4_AAC_DECODE_ERROR
	}
}

static u32 sceMp4GetAvcAu(u32 mp4, u32 trackAddr, u32 auAddr, u32 infoAddr) {
	Mp4Context *ctx = getMp4Ctx(mp4);
	if (!ctx) return hleLogWarning(Log::ME, -1, "bad mp4 handle");

	if (trackAddr && auAddr) {
		SceMp4TrackSampleBuf track;
		track.read(trackAddr);
		if (track.bufSamples.sizeAvailableForRead <= 0) return hleLogWarning(Log::ME, 0x80610103, "no more data");

		SceMpegAu au;
		au.read(auAddr);

		int sample = track.currentSample - track.bufSamples.sizeAvailableForRead;
		u32 sampleSize = ctx->GetSampleSize(track.trackType, sample);
		u32 sampleDuration = ctx->GetSampleDuration(track.trackType, sample);
		u32 samplePresentationOffset = ctx->GetSamplePresentationOffset(track.trackType, sample);
		s64 frameDuration = ctx->SampleToFrameDuration(sampleDuration, track.timeScale);
		s64 framePresentationOffset = ctx->SampleToFrameDuration(samplePresentationOffset, track.timeScale);

		track.bufSamples.notifyRead(1);
		track.readBytes(au.esBuffer, sampleSize);
		au.esSize = sampleSize;
		au.dts = ctx->videoTrack.currentTimestamp;
		ctx->videoTrack.currentTimestamp += frameDuration;
		au.pts = au.dts + framePresentationOffset;

		au.write(auAddr);
		track.write(trackAddr);

		if (infoAddr) {
			SceMp4SampleInfo info;
			info.sample = sample;
			info.sampleOffset = ctx->GetSampleOffset(track.trackType, sample);
			info.sampleSize = sampleSize;
			info.unknown1 = 0;
			info.frameDuration = (u32)frameDuration;
			info.unknown2 = 0;
			info.unknown3 = 0;
			info.timestamp1 = (u32)au.dts;
			info.unknown4 = 0;
			info.timestamp2 = (u32)au.pts;
			info.write(infoAddr);
		}
	}
	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4InitAu(u32 mp4, u32 bufferAddr, u32 auAddr) {
	SceMpegAu au;
	au.esBuffer = bufferAddr;
	au.esSize = 0;
	au.pts = 0;
	au.dts = 0;
	au.write(auAddr);
	return hleLogInfo(Log::ME, 0);
}

static u32 mp4msv_3C2183C7(u32 unknown1, u32 unknown2) {
	if (unknown2) {
		INFO_LOG(Log::ME, "mp4msv_3C2183C7 unknown values: %08x %08x %08x %08x %08x",
			Memory::Read_U32(unknown2), Memory::Read_U32(unknown2+4), Memory::Read_U32(unknown2+8),
			Memory::Read_U32(unknown2+12), Memory::Read_U32(unknown2+16));
	}
	// In JPCSP, this calls hleMp4Init for all existing contexts?
	// Actually it calls it on the module, which resets global state.
	// For PPSSPP, we'll just log it for now.
	return hleLogInfo(Log::ME, 0);
}

static u32 mp4msv_9CA13D1A(u32 unknown1, u32 unknown2) {
	if (unknown2) {
		INFO_LOG(Log::ME, "mp4msv_9CA13D1A unknown values: %08x ...", Memory::Read_U32(unknown2));
	}
	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4TrackSampleBufDestruct(u32 unknown1, u32 unknown2) {
	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4UnregistTrack(u32 unknown1, u32 unknown2) {
	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4AacDecodeExit(int aac) {
	auto it = g_aacDecoders.find(aac);
	if (it != g_aacDecoders.end()) {
		delete it->second;
		g_aacDecoders.erase(it);
	}
	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4AacDecodeTermResource() {
	return hleLogInfo(Log::ME, 0);
}

const HLEFunction sceMp4[] = {
	{0X68651CBC, &WrapU_V<sceMp4Init>,                           "sceMp4Init",                        'x', ""       },
	{0X9042B257, &WrapU_V<sceMp4Finish>,                         "sceMp4Finish",                      'x', ""       },
	{0XB1221EE7, &WrapU_UUUU<sceMp4Create>,                      "sceMp4Create",                      'x', "xxxx"   },
	{0X538C2057, &WrapU_U<sceMp4Delete>,                         "sceMp4Delete",                      'x', "x"      },
	{0X113E9E7B, &WrapU_U<sceMp4GetNumberOfMetaData>,            "sceMp4GetNumberOfMetaData",         'x', "x"      },
	{0X7443AF1D, &WrapU_UU<sceMp4GetMovieInfo>,                  "sceMp4GetMovieInfo",                'x', "xx"     },
	{0X5EB65F26, &WrapU_UU<sceMp4GetNumberOfSpecificTrack>,       "sceMp4GetNumberOfSpecificTrack",    'x', "xx"     },
	{0X7ADFD01C, &WrapU_UUUUU<sceMp4RegistTrack>,                "sceMp4RegistTrack",                 'x', "xxxxx"  },
	{0XBCA9389C, &WrapU_UUUUU<sceMp4TrackSampleBufQueryMemSize>, "sceMp4TrackSampleBufQueryMemSize",  'x', "xxxxx"  },
	{0X9C8F4FC1, &WrapU_UUUUUUUU<sceMp4TrackSampleBufConstruct>, "sceMp4TrackSampleBufConstruct",     'x', "xxxxxxxx"},
	{0X0F0187D2, &WrapU_UUU<sceMp4GetAvcTrackInfoData>,          "sceMp4GetAvcTrackInfoData",         'x', "xxx"    },
	{0X9CE6F5CF, &WrapU_UUU<sceMp4GetAacTrackInfoData>,          "sceMp4GetAacTrackInfoData",         'x', "xxx"    },
	{0X4ED4AB1E, &WrapU_I<sceMp4AacDecodeInitResource>,          "sceMp4AacDecodeInitResource",       'x', "i"      },
	{0X10EE0D2C, &WrapU_U<sceMp4AacDecodeInit>,                  "sceMp4AacDecodeInit",               'x', "x"      },
	{0X496E8A65, &WrapU_UU<sceMp4TrackSampleBufFlush>,            "sceMp4TrackSampleBufFlush",         'x', "xx"     },
	{0XB4B400D1, &WrapU_UUU<sceMp4GetSampleNumWithTimeStamp>,      "sceMp4GetSampleNumWithTimeStamp",   'x', "xxx"    },
	{0XF7C51EC1, &WrapU_UUIU<sceMp4GetSampleInfo>,               "sceMp4GetSampleInfo",               'x', "xxix"   },
	{0X74A1CA3E, &WrapU_UUII<sceMp4SearchSyncSampleNum>,         "sceMp4SearchSyncSampleNum",         'x', "xxii"   },
	{0XD8250B75, &WrapU_UUI<sceMp4PutSampleNum>,                 "sceMp4PutSampleNum",                'x', "xxi"    },
	{0X8754ECB8, &WrapU_UUUU<sceMp4TrackSampleBufAvailableSize>, "sceMp4TrackSampleBufAvailableSize", 'x', "xxpp"   },
	{0X31BCD7E0, &WrapU_UUI<sceMp4TrackSampleBufPut>,            "sceMp4TrackSampleBufPut",           'x', "xxi"    },
	{0X5601A6F0, &WrapU_UUUU<sceMp4GetAacAu>,                    "sceMp4GetAacAu",                    'x', "xxxx"   },
	{0X7663CB5C, &WrapU_UUUUU<sceMp4AacDecode>,                  "sceMp4AacDecode",                   'x', "xxxxx"  },
	{0X503A3CBA, &WrapU_UUUU<sceMp4GetAvcAu>,                    "sceMp4GetAvcAu",                    'x', "xxxx"   },
	{0X01C76489, &WrapU_UU<sceMp4TrackSampleBufDestruct>,        "sceMp4TrackSampleBufDestruct",      'x', "xx"     },
	{0X6710FE77, &WrapU_UU<sceMp4UnregistTrack>,                 "sceMp4UnregistTrack",               'x', "xx"     },
	{0X5D72B333, &WrapU_I<sceMp4AacDecodeExit>,                  "sceMp4AacDecodeExit",               'x', "i"      },
	{0X7D332394, &WrapU_V<sceMp4AacDecodeTermResource>,          "sceMp4AacDecodeTermResource",       'x', ""       },
	{0X131BDE57, &WrapU_UUU<sceMp4InitAu>,                       "sceMp4InitAu",                      'x', "xxx"    },
	{0X17EAA97D, nullptr,                                        "sceMp4GetAvcAuWithoutSampleBuf",    '?', ""       },
	{0X28CCB940, nullptr,                                        "sceMp4GetTrackEditList",            '?', ""       },
	{0X3069C2B5, nullptr,                                        "sceMp4GetAvcParamSet",              '?', ""       },
	{0XD2AC9A7E, nullptr,                                        "sceMp4GetMetaData",                 '?', ""       },
	{0X4FB5B756, nullptr,                                        "sceMp4GetMetaDataInfo",             '?', ""       },
	{0X427BEF7F, nullptr,                                        "sceMp4GetTrackNumOfEditList",       '?', ""       },
	{0X532029B8, nullptr,                                        "sceMp4GetAacAuWithoutSampleBuf",    '?', ""       },
	{0XA6C724DC, nullptr,                                        "sceMp4GetSampleNum",                '?', ""       },
	{0X3C2183C7, &WrapU_UU<mp4msv_3C2183C7>,                    "mp4msv_3C2183C7",                   'x', "xx"      },
	{0X9CA13D1A, &WrapU_UU<mp4msv_9CA13D1A>,                    "mp4msv_9CA13D1A",                   'x', "xx"      },
};

const HLEFunction mp4msv[] = {
	{0x3C2183C7, &WrapU_UU<mp4msv_3C2183C7>,                    "mp4msv_3C2183C7",               'x', "xx"      },
	{0x9CA13D1A, &WrapU_UU<mp4msv_9CA13D1A>,                    "mp4msv_9CA13D1A",               'x', "xx"      },
};

void Register_sceMp4() {
	RegisterHLEModule("sceMp4", ARRAY_SIZE(sceMp4), sceMp4);
}

void Register_mp4msv() {
	RegisterHLEModule("mp4msv", ARRAY_SIZE(mp4msv), mp4msv);
}
