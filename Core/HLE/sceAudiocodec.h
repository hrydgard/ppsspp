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

#pragma once

#include <map>

class PointerWrap;

// audioType. Every sceAudiocodec entry point validates these as "codec - 0x1000 < 6", so the
// range is exactly 0x1000..0x1005 - see avcodec.prx.
enum PSPAudioType {
	PSP_CODEC_AT3PLUS = 0x00001000,
	PSP_CODEC_AT3 = 0x00001001,
	PSP_CODEC_MP3 = 0x00001002,
	PSP_CODEC_AAC = 0x00001003,  // sceMp4 decodes this in an mp4 container
	// 0x1004 is handled by parts of the firmware but it's ultimately rejected by the ME code. Never implemented.
	PSP_CODEC_UNKNOWN_1004 = 0x00001004,
	PSP_CODEC_WMA = 0x00001005,
};

// The context the Media Engine works on. Games allocate this themselves and hand it over, so the
// layout is fixed - don't reorder anything here.
//
// Offsets 0x00..0x27 are identical for every codec, and sceVideocodec's context shares them too
// (mpeg.prx and videocodec_260.prx annotate the same fields), so this really is one ME "codec
// context" ABI. Everything from 0x28 on is per-codec, which is what the union models.
//
// What each call writes was dumped from hardware after every call (pspautotests
// audio/audiocodec/context), and read out of me_wrapper.prx, the main-CPU side of the ME calls,
// which fills in most of these fields itself around the ME commands it sends. The ME only ever
// sees the first 0x68 bytes: me_wrapper flushes exactly that much before each command.
struct SceAudiocodecCodec {
	u32 magic;              // 0x00  set to 0x05100601 before every ME call
	s32 unk4;               // 0x04  set by GetInfo type 3 (ME command 0x61 and friends)
	s32 err;                // 0x08
	u32 edramAddr;          // 0x0c  in ME memory, 64-byte aligned. ReleaseEDRAM clears it.
	s32 neededMem;          // 0x10  from CheckNeedMem
	s32 inited;             // 0x14  Atrac3+ Init fails with err 0x8001 unless this is exactly 1
	u32 inBuf;              // 0x18  the raw frame to decode
	s32 srcBytesRead;       // 0x1c  written by the decoder
	u32 outBuf;             // 0x20  where decoded PCM goes
	s32 dstBytesWritten;    // 0x24  written by the decoder, in bytes like srcBytesRead

	// Codec-specific, 0x28..0x67.
	union {
		// Atrac3plus (0x1000). The caller writes the format bytes; libatrac3plus.prx uses 0x28/0x5c
		// (the worst case) to size EDRAM, and zeroes at3Related before every decode.
		struct {
			// 0x28/0x29, as a big-endian word: bits 13-15 sample rate (0 = 32000, 1 = 44100,
			// 2 = 48000), 10-12 channels, 0-9 frame bytes / 8 - 1. ME command 0x63 parses them.
			u8 formatByte1;
			u8 formatByte2;
			u8 unk2a;            // 0x2a
			u8 unk2b;            // 0x2b
			u32 unk2c;           // 0x2c  Init's ME command 0x69 writes here (0 in every test)
			// 0x30  Frames start with an 8-byte header (0F D0 + the format bytes) when set.
			u32 at3Related;
			u32 sampleRate;      // 0x34  CheckNeedMem and Init write these three from the format
			u32 unk38;           // 0x38
			u32 channels;        // 0x3c
			u32 frameBytes;      // 0x40
			// 0x44  Init: the bitrate in kbps, frameBytes * 8 * sampleRate / 2048 / 1000.
			u32 bitrateKbps;
			// 0x48  Init: the output channels, 2 for 1-2 channels, one more than the input for
			// 5-7; InitMono: 1. Read at every decode, and GetOutputBytes is this * 0x1000. Written
			// as 1 over a stereo stream, the hardware mixes it down somehow; see the decode.
			u32 outputChannels;
			s32 unk4c[7];        // 0x4c..0x67
		} at3;

		// Atrac3 (0x1001). The caller writes the parameter, an index into me_wrapper.prx's table
		// (see at3Params); Init fills in the rest.
		struct {
			u32 param;           // 0x28
			u32 sampleRate;      // 0x2c  44100
			u32 channelBytes;    // 0x30  the frame size of one channel
			// 0x34  Init writes 2 and InitMono 1, for the mono layouts only. Read at every decode:
			// a mono layout is written as stereo only when this is 2.
			u32 outputChannels;
			s32 unk38[12];       // 0x38..0x67
		} atrac3;

		// MP3 (0x1002). GetInfo, and every decode, fill 0x38-0x64 in from the frame header; these
		// are the header's own fields except for the version and layer numbering.
		struct {
			// 0x28  libmp3.prx writes 0x5A1 here once at setup, and never per frame: it is the
			// largest an MP3 frame can be (144 * 320000 / 32000 + 1 padding byte). The hardware
			// only uses it as the length for a cache writeback over inBuf before handing the
			// frame to the ME, so it is an upper bound rather than this frame's size.
			u32 maxFrameBytes;
			u32 unk2c;           // 0x2c  0 after a decode
			s32 channels;        // 0x30  decode: 1 for mono, else 2
			s32 granules;        // 0x34  decode: 2 for MPEG1, 1 for MPEG2 and 2.5
			// 0x38  MPEG version index: 0 = MPEG2, 1 = MPEG1, 2 = MPEG2.5. sceAudiocodecInit
			// presets it to 9999 to mean "not known yet"; GetInfo fills it in.
			s32 version;
			s32 layer;           // 0x3c  3 for Layer III
			s32 crc;             // 0x40  the protection bit, inverted: 1 if the frame has a CRC
			s32 bitrateIndex;    // 0x44  index into the MPEG Layer III bitrate table (9 = 128kbps)
			s32 sampleRateIndex; // 0x48  0..2 within the version's rates (0 = 44100 for MPEG1)
			s32 padding;         // 0x4c
			s32 privateBit;      // 0x50
			// 0x54  The channel mode. libmp3.prx reads it as: channels = (channelConfig == 3) ? 1 : 2.
			s32 channelConfig;
			s32 modeExtension;   // 0x58
			s32 copyright;       // 0x5c
			s32 original;        // 0x60
			s32 emphasis;        // 0x64
		} mp3;

		// AAC (0x1003). The input frame size is 0x609 when flag2c is nonzero and 0x600 when it is
		// zero, and the output size 0x2000 when flag2d is set (avcodec.prx decodeUtility). Init
		// sends extra ME commands for either flag (0x95, 0x97), which fail on the firmware tested.
		struct {
			// 0x28  One of the AAC rates from 8000 to 96000 (not 12000 or 7350), or Init fails.
			u32 sampleRate;
			u8 flag2c;           // 0x2c
			u8 flag2d;           // 0x2d
			u8 unk2e;            // 0x2e
			u8 unk2f;            // 0x2f
			s32 unk30[14];       // 0x30..0x67
		} aac;

		u8 raw[0x40];
	} fmt;

	// 0x68  What GetEDRAM got from the ME's allocator, before rounding up to edramAddr.
	// ReleaseEDRAM clears it. Past what the ME sees.
	u32 allocMem;
	u8 unk[0x14];  // 0x6c
};

void __AudioCodecInit();
void __AudioCodecShutdown();
void Register_sceAudiocodec();
void __sceAudiocodecDoState(PointerWrap &p);

class AudioDecoder;
extern std::map<u32, AudioDecoder *> g_audioDecoderContexts;

bool IsAtrac3StreamJointStereo(int codecType, int bytesPerFrame, int channels);
// The channel count the Atrac3 decoder for a track works with, which can differ from the header's.
bool Atrac3DecoderChannels(int bytesPerFrame, bool jointStereo, int *channels);

// ME time at the default clock, for sceAtrac too (it drives the same decoder): setting up a decoder,
// and decoding one frame. AudioCodecDecodeUs takes Atrac3 and Atrac3+ only.
int AudioCodecInitUs(int codec, bool monoAt3Plus);
int AudioCodecDecodeUs(int codec, int channels, int frameBytes);
