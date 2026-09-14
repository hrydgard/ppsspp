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
// context" ABI. Everything from 0x28 on is per-codec, which is what the union models: each
// library writes a different set of fields there.
struct SceAudiocodecCodec {
	u32 magic;              // 0x00  set to 0x05100601 before every ME call
	s32 unk4;               // 0x04
	s32 err;                // 0x08
	u32 edramAddr;          // 0x0c  in ME memory
	s32 neededMem;          // 0x10  0x102400 for Atrac3+
	s32 inited;             // 0x14
	u32 inBuf;              // 0x18  the raw frame to decode
	s32 srcBytesRead;       // 0x1c  written by the decoder
	u32 outBuf;             // 0x20  where decoded PCM goes
	s32 dstSamplesWritten;  // 0x24  written by the decoder

	// Codec-specific, 0x28..0x67.
	union {
		// Atrac3plus and Atrac3. libatrac3plus.prx writes formatByte1/formatByte2 at init
		// (0x28/0x5c for the worst case it sizes EDRAM against) and zeroes at3Related before
		// every Atrac3plus decode.
		struct {
			u8 formatByte1;  // 0x28  bit 3 appears to mean stereo
			u8 formatByte2;  // 0x29  frame size in bytes = formatByte2 * 8 + 8
			u8 unk2a;        // 0x2a
			u8 unk2b;        // 0x2b
			u32 unk2c;       // 0x2c
			u32 at3Related;  // 0x30  zeroed for Atrac3plus, left alone for Atrac3
			s32 unk34;       // 0x34
			s32 unk38[12];   // 0x38..0x67
		} at3;

		// MP3. Field meanings are from avcodec.prx's frame-size calculator, which indexes the
		// standard MPEG bitrate and sample-rate tables with them, and from libmp3.prx reading
		// version and channelConfig back out.
		struct {
			// 0x28  libmp3.prx writes 0x5A1 here once at setup, and never per frame: it is the
			// largest an MP3 frame can be (144 * 320000 / 32000 + 1 padding byte). The hardware
			// only uses it as the length for a cache writeback over inBuf before handing the
			// frame to the ME, so it is an upper bound rather than this frame's size.
			u32 maxFrameBytes;
			u32 unk2c;           // 0x2c
			s32 unk30;           // 0x30
			s32 unk34;           // 0x34
			// 0x38  MPEG version index: 0 = MPEG2, 1 = MPEG1, 2 = MPEG2.5. sceAudiocodecInit
			// presets it to 9999 to mean "not known yet"; GetInfo fills it in.
			s32 version;
			// 0x3c  Observed to be 3. avcodec's (unreachable) frame-size path treats this as the
			// raw MPEG layer bits and only computes a size for 1 or 2, where 1 is Layer III -
			// so 3 would fall through to its maximum-size fallback. Unexplained.
			s32 unk3c;
			s32 unk40;           // 0x40
			s32 bitrateIndex;    // 0x44  index into the MPEG Layer III bitrate table (9 = 128kbps)
			s32 sampleRateIndex; // 0x48  0..2 within the version's rates (0 = 44100 for MPEG1)
			s32 unk4c;           // 0x4c
			s32 unk50;           // 0x50
			// 0x54  libmp3.prx reads this as: channels = (channelConfig == 3) ? 1 : 2.
			s32 channelConfig;
			s32 unk58;           // 0x58
			s32 unk5c;           // 0x5c
			s32 unk60;           // 0x60
			s32 unk64;           // 0x64
		} mp3;

		// AAC. The hardware sizes its input from the byte at 0x2c and its output from the byte
		// at 0x2d; the sample rate at 0x28 is our own observation from games.
		struct {
			u32 sampleRate;  // 0x28
			u8 unk2c;        // 0x2c
			u8 unk2d;        // 0x2d
			u8 unk2e;        // 0x2e
			u8 unk2f;        // 0x2f
			s32 unk30[14];   // 0x30..0x67
		} aac;

		u8 raw[0x40];
	} fmt;

	u32 allocMem;  // 0x68  not hardware - where our GetEDRAM stores what it handed out
	u8 unk[0x14];  // 0x6c
};

void __AudioCodecInit();
void __AudioCodecShutdown();
void Register_sceAudiocodec();
void __sceAudiocodecDoState(PointerWrap &p);

class AudioDecoder;
extern std::map<u32, AudioDecoder *> g_audioDecoderContexts;

bool IsAtrac3StreamJointStereo(int codecType, int bytesPerFrame, int channels);
