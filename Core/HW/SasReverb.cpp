// Copyright (c) 2015- PPSSPP Project.

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

#include <cstdint>
#include <cstring>

#include "Common/Math/math_util.h"
#include "Core/Config.h"
#include "Core/HW/SasReverb.h"
#include "Core/Util/AudioFormat.h"

// This is under the assumption that the reverb used in Sas is the same as the PSX SPU reverb.

// Source: http://problemkaputt.de/psx-spx.htm#spureverbformula

struct SasReverbData {
	const char *name;
	int32_t size;

	int16_t dAPF1;
	int16_t dAPF2;
	int16_t vIIR;
	int16_t vCOMB1;
	int16_t vCOMB2;
	int16_t vCOMB3;
	int16_t vCOMB4;
	int16_t vWALL;

	int16_t vAPF1;
	int16_t vAPF2;
	int16_t mLSAME;
	int16_t mRSAME;
	int16_t mLCOMB1;
	int16_t mRCOMB1;
	int16_t mLCOMB2;
	int16_t mRCOMB2;

	int16_t dLSAME;
	int16_t dRSAME;
	int16_t mLDIFF;
	int16_t mRDIFF;
	int16_t mLCOMB3;
	int16_t mRCOMB3;
	int16_t mLCOMB4;
	int16_t mRCOMB4;

	int16_t dLDIFF;
	int16_t dRDIFF;
	int16_t mLAPF1;
	int16_t mRAPF1;
	int16_t mLAPF2;
	int16_t mRAPF2;

	// These aren't used for anything else than 1.0 in any of the presets so let's drop them.
	// int16_t vLIN;
	// int16_t vRIN;
};

static const SasReverbData presets[10] = {
	{
		"Room",
		0x26C0,
		0x007D,0x005B,0x6D80,0x54B8,(int16_t)0xBED0,0x0000,0x0000,(int16_t)0xBA80,
		0x5800,0x5300,0x04D6,0x0333,0x03F0,0x0227,0x0374,0x01EF,
		0x0334,0x01B5,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x01B4,0x0136,0x00B8,0x005C, //(int16_t)0x8000,(int16_t)0x8000,
	},
	{
		"Studio Small",
		0x1F40,
		0x0033,0x0025,0x70F0,0x4FA8,(int16_t)0xBCE0,0x4410,(int16_t)0xC0F0,(int16_t)0x9C00,
		0x5280,0x4EC0,0x03E4,0x031B,0x03A4,0x02AF,0x0372,0x0266,
		0x031C,0x025D,0x025C,0x018E,0x022F,0x0135,0x01D2,0x00B7,
		0x018F,0x00B5,0x00B4,0x0080,0x004C,0x0026, //(int16_t)0x8000,(int16_t)0x8000,
	},

	{
		"Studio Medium",
		0x4840,
		0x00B1,0x007F,0x70F0,0x4FA8,(int16_t)0xBCE0,0x4510,(int16_t)0xBEF0,(int16_t)0xB4C0,
		0x5280,0x4EC0,0x0904,0x076B,0x0824,0x065F,0x07A2,0x0616,
		0x076C,0x05ED,0x05EC,0x042E,0x050F,0x0305,0x0462,0x02B7,
		0x042F,0x0265,0x0264,0x01B2,0x0100,0x0080, //(int16_t)0x8000,(int16_t)0x8000,
	},

	// Studio Large(size = 6FE0h bytes)
	{
		"Studio Large",
		0x6FE0,
		0x00E3,0x00A9,0x6F60,0x4FA8,(int16_t)0xBCE0,0x4510,(int16_t)0xBEF0,(int16_t)0xA680,
		0x5680,0x52C0,0x0DFB,0x0B58,0x0D09,0x0A3C,0x0BD9,0x0973,
		0x0B59,0x08DA,0x08D9,0x05E9,0x07EC,0x04B0,0x06EF,0x03D2,
		0x05EA,0x031D,0x031C,0x0238,0x0154,0x00AA, //(int16_t)0x8000,(int16_t)0x8000,
	},

	{
		"Hall",
		0xADE0,
		0x01A5,0x0139,0x6000,0x5000,0x4C00,(int16_t)0xB800,(int16_t)0xBC00,(int16_t)0xC000,
		0x6000,0x5C00,0x15BA,0x11BB,0x14C2,0x10BD,0x11BC,0x0DC1,
		0x11C0,0x0DC3,0x0DC0,0x09C1,0x0BC4,0x07C1,0x0A00,0x06CD,
		0x09C2,0x05C1,0x05C0,0x041A,0x0274,0x013A, //(int16_t)0x8000,(int16_t)0x8000,
	},

	{
		"Space Echo",
		0xF6C0,
		0x033D,0x0231,0x7E00,0x5000,(int16_t)0xB400,(int16_t)0xB000,0x4C00,(int16_t)0xB000,
		0x6000,0x5400,0x1ED6,0x1A31,0x1D14,0x183B,0x1BC2,0x16B2,
		0x1A32,0x15EF,0x15EE,0x1055,0x1334,0x0F2D,0x11F6,0x0C5D,
		0x1056,0x0AE1,0x0AE0,0x07A2,0x0464,0x0232, //(int16_t)0x8000,(int16_t)0x8000,
	},

	{
		"Echo (almost infinite)",
		0x18040,
		0x0001,0x0001,0x7FFF,0x7FFF,0x0000,0x0000,0x0000,(int16_t)0xC080,
		0x0000,0x0000,0x1FFF,0x0FFF,0x1005,0x0005,0x0000,0x0000,
		0x1005,0x0005,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x1004,0x1002,0x0004,0x0002, //(int16_t)0x8000,(int16_t)0x8000,
	},

	{
		"Delay (one - shot echo)",
		0x18040,
		0x0001,0x0001,0x7FFF,0x7FFF,0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x1FFF,0x0FFF,0x1005,0x0005,0x0000,0x0000,
		0x1005,0x0005,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x1004,0x1002,0x0004,0x0002, //(int16_t)0x8000,(int16_t)0x8000,
	},

	{
		"Half Echo",
		0x3C00,
		0x0017,0x0013,0x70F0,0x4FA8,(int16_t)0xBCE0,0x4510,(int16_t)0xBEF0,(int16_t)0x8500,
		0x5F80,0x54C0,0x0371,0x02AF,0x02E5,0x01DF,0x02B0,0x01D7,
		0x0358,0x026A,0x01D6,0x011E,0x012D,0x00B1,0x011F,0x0059,
		0x01A0,0x00E3,0x0058,0x0040,0x0028,0x0014, //(int16_t)0x8000,(int16_t)0x8000,
	},
};

SasReverb::SasReverb() : preset_(-1), pos_(0) {
	workspace_ = new int16_t[BUFSIZE];
}

SasReverb::~SasReverb() {
	delete[] workspace_;
}

const char *SasReverb::GetPresetName(int preset) {
	if (preset == -1) {
		return "Off";
	}
	return presets[preset].name;
}

void SasReverb::SetPreset(int preset) {
	if (preset < (int)ARRAY_SIZE(presets))
		preset_ = preset;
	if (preset_ != -1) {
		pos_ = BUFSIZE - presets[preset_].size;
		memset(workspace_, 0, sizeof(int16_t) * BUFSIZE);
	} else {
		pos_ = 0;
	}
}

// Wraps around the upper part of a buffer.
template<int bufsize>
class BufferWrapper {
public:
	BufferWrapper(int16_t *buffer, int position, int usedSize) : buf_(buffer), pos_(position), end_(bufsize), base_(bufsize - usedSize), size_(usedSize) {}
	int16_t &operator [](int index) {
		int addr = pos_ + index;
		if (addr >= end_) { addr -= size_; }
		if (addr < base_) { addr += size_; }
		return buf_[addr];
	}

	int GetPosition() { return pos_; }
	void Next() {
		pos_++;
		if (pos_ >= end_) {
			pos_ -= size_;
		}
	}

private:
	int16_t *buf_;
	int pos_;
	int end_;
	int base_;
	int size_;
};

void SasReverb::ProcessReverb(int16_t *output, const int16_t *input, size_t inputSize, uint16_t volLeft, uint16_t volRight) {
	// This means replicate the input signal in the processed buffer.
	// Can also be used to verify that the error is in here...
	if (preset_ == -1) {
		// Strangely, OFF is not filled with zeroes every other.  Seems special cased.
		for (size_t i = 0; i < inputSize; ++i) {
			output[i * 4 + 0] = clamp_s16((int)input[i * 2 + 0] * volLeft >> 15);
			output[i * 4 + 1] = clamp_s16((int)input[i * 2 + 1] * volRight >> 15);
			output[i * 4 + 2] = clamp_s16((int)input[i * 2 + 0] * volLeft >> 15);
			output[i * 4 + 3] = clamp_s16((int)input[i * 2 + 1] * volRight >> 15);
		}
		return;
	}

	const uint8_t reverbVolume = Clamp(g_Config.iReverbVolume, 0, 25);
	// Standard volume is 10, which pairs with a normal shift of 15.
	const uint8_t finalShift = 25 - reverbVolume;
	if (reverbVolume == 0) {
		// Force to zero output, which is not the same as "Off."
		memset(output, 0, inputSize * 4);
		return;
	}

	const SasReverbData &d = presets[preset_];

	// We put this on the stack instead of in the object to let the compiler optimize better (avoid mem r/w).
	BufferWrapper<BUFSIZE> b(workspace_, pos_, d.size);

	// This runs at 22khz.
	// Very unoptimized, straight from the description. Can probably be reformulated into something way more efficient.
	// Or we could actually template the whole thing with the parameters as template arguments, as the presets are fixed.
	for (size_t i = 0; i < inputSize; i++) {
		// Dividing by two here is an incorrect hack. Some multiplication factor is needed to prevent the reverb from getting too loud, though.
		int16_t LeftInput = input[i * 2] >> 1;
		int16_t RightInput = input[i * 2 + 1] >> 1;

		int16_t Lin = LeftInput; //  (d.vLIN * LeftInput) >> 15;
		int16_t Rin = RightInput; // (d.vRIN * RightInput) >> 15;

		// ____Same Side Reflection(left - to - left and right - to - right)___________________
		b[d.mLSAME] = clamp_s16(Lin + (b[d.dLSAME] * d.vWALL >> 15) - (b[d.mLSAME - 1]*d.vIIR >> 15) + b[d.mLSAME - 1]); // L - to - L
		b[d.mRSAME] = clamp_s16(Rin + (b[d.dRSAME] * d.vWALL >> 15) - (b[d.mRSAME - 1]*d.vIIR >> 15) + b[d.mRSAME - 1]); // R - to - R
		// ___Different Side Reflection(left - to - right and right - to - left)_______________
		b[d.mLDIFF] = clamp_s16(Lin + (b[d.dRDIFF] * d.vWALL >> 15) - (b[d.mLDIFF - 1]*d.vIIR >> 15) + b[d.mLDIFF - 1]); // R - to - L
		b[d.mRDIFF] = clamp_s16(Rin + (b[d.dLDIFF] * d.vWALL >> 15) - (b[d.mRDIFF - 1]*d.vIIR >> 15) + b[d.mRDIFF - 1]); // L - to - R
		// ___Early Echo(Comb Filter, with input from buffer)__________________________
		int32_t Lout = ((d.vCOMB1*b[d.mLCOMB1] + d.vCOMB2*b[d.mLCOMB2] + d.vCOMB3*b[d.mLCOMB3] + d.vCOMB4*b[d.mLCOMB4]) >> 15);
		int32_t Rout = ((d.vCOMB1*b[d.mRCOMB1] + d.vCOMB2*b[d.mRCOMB2] + d.vCOMB3*b[d.mRCOMB3] + d.vCOMB4*b[d.mRCOMB4]) >> 15);
		// ___Late Reverb APF1(All Pass Filter 1, with input from COMB)________________
		b[d.mLAPF1] = clamp_s16(Lout - (d.vAPF1*b[(d.mLAPF1 - d.dAPF1)] >> 15));
		Lout = b[(d.mLAPF1 - d.dAPF1)] + (b[d.mLAPF1] * d.vAPF1 >> 15);
		b[d.mRAPF1] = clamp_s16(Rout - (d.vAPF1*b[(d.mRAPF1 - d.dAPF1)] >> 15));
		Rout = b[(d.mRAPF1 - d.dAPF1)] + (b[d.mRAPF1] * d.vAPF1 >> 15);
		// ___Late Reverb APF2(All Pass Filter 2, with input from APF1)________________
		b[d.mLAPF2] = clamp_s16(Lout - (d.vAPF2*b[(d.mLAPF2 - d.dAPF2)] >> 15));
		Lout = b[(d.mLAPF2 - d.dAPF2)] + (b[d.mLAPF2] * d.vAPF2 >> 15);
		b[d.mRAPF2] = clamp_s16(Rout - (d.vAPF2*b[(d.mRAPF2 - d.dAPF2)] >> 15));
		Rout = b[(d.mRAPF2 - d.dAPF2)] + (b[d.mRAPF2] * d.vAPF2 >> 15);
		// ___Output to Mixer(Output volume multiplied with input from APF2)___________
		output[i * 4 + 0] = clamp_s16((Lout * volLeft) >> finalShift);
		output[i * 4 + 1] = clamp_s16((Rout * volRight) >> finalShift);
		output[i * 4 + 2] = 0;
		output[i * 4 + 3] = 0;

		b.Next();
	}

	// Save the state in the object.
	pos_ = b.GetPosition();
}

