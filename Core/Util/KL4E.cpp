// Copyright (c) 2026- PPSSPP Project.

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

// KL4E / KL3E decompression. See docs/KL4E.md.
//
// LZ77 tokens - "emit this literal" or "repeat N bytes from M back" - where every bit is coded
// with an adaptive binary arithmetic coder rather than written directly. Structurally close to
// LZMA. KL3E differs from KL4E in exactly one constant (kPowLimitLong).
//
// Written from the format description, not ported from existing code.

#include <cstring>

#include "Common/Log.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/Util/KL4E.h"
namespace {

// Adaptation profiles. A probability is the chance the next bit is a 1, out of 256: on a 0 it
// just decays, on a 1 it decays and then gains the bonus back, so it climbs toward a ceiling of
// roughly (bonus << decay).
const int kDecayNormal = 3, kBonusNormal = 31;
const int kDecayFlag = 4, kBonusFlag = 15;

// Table sizes. Each is exactly large enough for the highest index its indexing scheme can
// produce - except copyDistProbs, see the bounds check in the distance decode.
const int kLitProbsSize = 2040;          // 8 literal contexts * 255
const int kCopyCountBitsProbsSize = 64;  // 8 states * 8 unary steps
const int kCopyCountProbsSize = 256;
const int kCopyDistBitsProbsSize = 304;
const int kCopyDistProbsSize = 144;

// The doubling walk that sizes a distance code stops once it reaches this. The single constant
// that separates the two formats.
const int kPowLimitKL4E = 256;
const int kPowLimitKL3E = 128;
// Short matches use a tighter limit in both formats.
const int kPowLimitShort = 64;

struct ArithDecoder {
	const u8 *in;
	const u8 *inEnd;
	u32 range;
	u32 code;
	bool overrun;

	u8 NextByte() {
		if (in >= inEnd) {
			// The format carries no length and relies on the stream terminating itself, so a
			// truncated file would otherwise read forever. Feed zeroes and remember.
			overrun = true;
			return 0;
		}
		return *in++;
	}

	// One bit against an adaptive probability.
	int ReadBit(u8 *prob, int decay, int bonus) {
		u32 bound;
		const u32 p = *prob;
		if ((range >> 24) == 0) {
			// Renormalize. The bound uses the range from *before* the shift.
			code = (code << 8) + NextByte();
			bound = range * p;
			range <<= 8;
		} else {
			bound = (range >> 8) * p;
		}
		const u32 decayed = p - (p >> decay);
		if (code >= bound) {
			code -= bound;
			range -= bound;
			*prob = (u8)decayed;
			return 0;
		}
		range = bound;
		*prob = (u8)(decayed + bonus);
		return 1;
	}

	// A bit the encoder judged incompressible: no probability, no adaptation.
	int ReadBitUniform() {
		if ((range >> 24) == 0) {
			code = (code << 8) + NextByte();
			range <<= 7;
		} else {
			range >>= 1;
		}
		if (code >= range) {
			code -= range;
			return 0;
		}
		return 1;
	}

	// Same, skipping the refill check. Only valid straight after a read that did refill, which
	// is how runs of uniform bits are emitted.
	int ReadBitUniformNoNorm() {
		range >>= 1;
		if (code >= range) {
			code -= range;
			return 0;
		}
		return 1;
	}

	// Eight bits into an accumulator that starts at 1, so the count is implicit. The context is
	// a mix of the previous byte and the output position's alignment; `shift` slides the window
	// between the two, the same choice LZMA's lc makes.
	u32 ReadLiteral(u8 *litProbs, int outPos, u32 prevByte, int shift) {
		const u32 ctx = ((u32)(outPos & 7) << 8) | (prevByte & 0xFF);
		u8 *probs = litProbs + ((ctx >> shift) & 7) * 255 - 1;
		u32 acc = 1;
		while (acc < 0x100) {
			acc = (acc << 1) | ReadBit(&probs[acc], kDecayNormal, kBonusNormal);
		}
		return acc & 0xFF;
	}
};

}  // namespace

bool IsKL4EMagic(const u8 *data, size_t size, bool *isKL3E) {
	if (size < 4) {
		return false;
	}
	if (!memcmp(data, "KL4E", 4)) {
		if (isKL3E) {
			*isKL3E = false;
		}
		return true;
	}
	if (!memcmp(data, "KL3E", 4)) {
		if (isKL3E) {
			*isKL3E = true;
		}
		return true;
	}
	return false;
}

int DecompressKL4E(u8 *out, int outSize, const u8 *in, size_t inSize, const u8 **end, bool isKL3E) {
	if (!out || !in || outSize <= 0 || inSize < 5) {
		return SCE_KERNEL_ERROR_INVALID_FORMAT;
	}

	// --- five-byte stream header ---
	const u8 flags = in[0];
	// Big-endian, which almost nothing else on the PSP is.
	const u32 headerWord = ((u32)in[1] << 24) | ((u32)in[2] << 16) | ((u32)in[3] << 8) | in[4];

	if (flags & 0x80) {
		// Stored, not compressed: the header word is a length and the bytes follow verbatim.
		// A payload that exactly fills the output buffer is rejected, not accepted.
		if (headerWord >= (u32)outSize) {
			return SCE_KERNEL_ERROR_INVALID_SIZE;
		}
		if (5 + (size_t)headerWord > inSize) {
			return SCE_KERNEL_ERROR_INVALID_FORMAT;
		}
		memcpy(out, in + 5, headerWord);
		if (end) {
			*end = in + 5 + headerWord;
		}
		return (int)headerWord;
	}

	// bits 4-3 pick the starting probability, bits 2-0 the literal context selector.
	const u8 seed = (u8)(0x80 - (((flags >> 3) & 3) << 4));
	const int shift = flags & 7;

	u8 litProbs[kLitProbsSize];
	u8 copyCountBitsProbs[kCopyCountBitsProbsSize];
	u8 copyCountProbs[kCopyCountProbsSize];
	u8 copyDistBitsProbs[kCopyDistBitsProbsSize];
	u8 copyDistProbs[kCopyDistProbsSize];
	memset(litProbs, seed, sizeof(litProbs));
	memset(copyCountBitsProbs, seed, sizeof(copyCountBitsProbs));
	memset(copyCountProbs, seed, sizeof(copyCountProbs));
	memset(copyDistBitsProbs, seed, sizeof(copyDistBitsProbs));
	memset(copyDistProbs, seed, sizeof(copyDistProbs));

	ArithDecoder dec;
	dec.in = in + 5;
	dec.inEnd = in + inSize;
	dec.range = 0xFFFFFFFF;
	dec.code = headerWord;
	dec.overrun = false;

	const int powLimitLong = isKL3E ? kPowLimitKL3E : kPowLimitKL4E;

	// outPos indexes the byte being produced. The loop head advances it, which is why a match
	// only advances it by copyCount after writing copyCount + 1 bytes.
	int outPos = 0;
	u32 prevByte = 0;

	// An index into copyCountBitsProbs that persists across tokens - the "state". The unary walk
	// below moves it in steps of 8 and does not put it back, so its low three bits are what the
	// length code's context uses.
	int countBitsIdx = 0;

	// The first literal has no flag bit in front of it.
	prevByte = dec.ReadLiteral(litProbs, outPos, prevByte, shift);
	out[0] = (u8)prevByte;

	while (true) {
		outPos++;

		if (dec.overrun) {
			return SCE_KERNEL_ERROR_INVALID_FORMAT;
		}

		if (dec.ReadBit(&copyCountBitsProbs[countBitsIdx], kDecayFlag, kBonusFlag) == 0) {
			// Literal.
			countBitsIdx = countBitsIdx > 0 ? countBitsIdx - 1 : 0;
			if (outPos >= outSize) {
				return SCE_KERNEL_ERROR_INVALID_SIZE;
			}
			prevByte = dec.ReadLiteral(litProbs, outPos, prevByte, shift);
			out[outPos] = (u8)prevByte;
			continue;
		}

		// A match. First, how many bits the length code uses, as a unary run stepping the index
		// by 8 so the low three bits stay put.
		u32 copyCount = 1;
		int copyCountBits = -1;
		while (copyCountBits < 6) {
			countBitsIdx += 8;
			if (countBitsIdx >= kCopyCountBitsProbsSize) {
				return SCE_KERNEL_ERROR_INVALID_FORMAT;
			}
			if (!dec.ReadBit(&copyCountBitsProbs[countBitsIdx], kDecayFlag, kBonusFlag)) {
				break;
			}
			copyCountBits++;
		}

		// The length, and with it the distance code's parameters: the bit that lands in the
		// length's low position also selects them, for short matches.
		int powLimit = kPowLimitShort;
		int distBase = copyCountBits;
		if (copyCountBits >= 0) {
			const int offset = (copyCountBits << 5)
				| (((outPos & 3) << (copyCountBits + 3)) & 0x18)
				| (countBitsIdx & 7);
			u8 *probs = copyCountProbs + offset;

			// High bits: two adaptive, then uniform for anything longer.
			if (copyCountBits >= 3) {
				copyCount = 2 + dec.ReadBit(probs + 24, kDecayNormal, kBonusNormal);
				if (copyCountBits > 3) {
					copyCount = (copyCount << 1) | dec.ReadBit(probs + 24, kDecayNormal, kBonusNormal);
					if (copyCountBits > 4) {
						copyCount = (copyCount << 1) | dec.ReadBitUniform();
					}
					for (int i = 5; i < copyCountBits; i++) {
						copyCount = (copyCount << 1) | dec.ReadBitUniformNoNorm();
					}
				}
			}

			copyCount <<= 1;
			if (dec.ReadBit(probs, kDecayNormal, kBonusNormal)) {
				copyCount |= 1;
				if (copyCountBits <= 0) {
					powLimit = powLimitLong;
					distBase = 56 + copyCountBits;
				}
			} else if (copyCountBits <= 0) {
				powLimit = kPowLimitShort;
				distBase = copyCountBits;
			}

			if (copyCountBits > 0) {
				copyCount = (copyCount << 1) | dec.ReadBit(probs + 8, kDecayNormal, kBonusNormal);
				if (copyCountBits != 1) {
					copyCount <<= 1;
					if (dec.ReadBit(probs + 16, kDecayNormal, kBonusNormal)) {
						copyCount++;
						// 0xFF ends the stream. There is no length in the header and no
						// terminator byte; this is the only normal way out of the loop.
						if (copyCount == 0xFF) {
							if (end) {
								*end = dec.in;
							}
							return outPos;
						}
					}
				}
				powLimit = powLimitLong;
				distBase = 56 + copyCountBits;
			}
		}

		// How many bits the distance code uses, by a doubling walk. curPow only picks up the
		// extra 8 when the walk continues, which is what makes the deepest reachable index fit
		// copyDistBitsProbs exactly.
		u32 copyDist = 0;
		bool distanceIsZero = false;
		int curPow = 8;
		int copyDistBits = 0;
		while (true) {
			const int probIndex = distBase + curPow - 7;
			if (probIndex < 0 || probIndex >= kCopyDistBitsProbsSize) {
				return SCE_KERNEL_ERROR_INVALID_FORMAT;
			}
			u8 *prob = &copyDistBitsProbs[probIndex];
			curPow <<= 1;
			copyDistBits = curPow - powLimit;
			if (!dec.ReadBit(prob, kDecayNormal, kBonusNormal)) {
				if (copyDistBits >= 0) {
					if (copyDistBits != 0) {
						copyDistBits -= 8;
						break;
					}
					// No distance bits at all: a match against the byte just emitted, i.e. a run.
					distanceIsZero = true;
					break;
				}
			} else {
				curPow += 8;
				if (copyDistBits >= 0) {
					break;
				}
			}
		}

		if (!distanceIsZero) {
			// The distance value. Same shape as the length: two adaptive high bits, uniform in
			// the middle, then three adaptive low bits whose +1/-1 adjustments keep the code
			// ranges contiguous across bit counts.
			if (copyDistBits < 0 || copyDistBits + 3 >= kCopyDistProbsSize) {
				// Only reachable from a stream encoding a distance far larger than any real
				// module needs - the hardware routine reads past its own table here.
				return SCE_KERNEL_ERROR_INVALID_FORMAT;
			}
			u8 *probs = copyDistProbs + copyDistBits;
			int readBits = copyDistBits / 8;

			if (readBits < 3) {
				copyDist = 1;
			} else {
				copyDist = 2 + dec.ReadBit(probs + 3, kDecayNormal, kBonusNormal);
				if (readBits > 3) {
					copyDist = (copyDist << 1) | dec.ReadBit(probs + 3, kDecayNormal, kBonusNormal);
					if (readBits > 4) {
						copyDist = (copyDist << 1) | dec.ReadBitUniform();
						readBits--;
					}
					while (readBits > 4) {
						copyDist = (copyDist << 1) + dec.ReadBitUniformNoNorm();
						readBits--;
					}
				}
			}

			copyDist <<= 1;
			if (dec.ReadBit(probs, kDecayNormal, kBonusNormal)) {
				if (readBits > 0) {
					copyDist++;
				}
			} else if (readBits <= 0) {
				copyDist--;
			}

			if (readBits > 0) {
				copyDist <<= 1;
				if (dec.ReadBit(probs + 1, kDecayNormal, kBonusNormal)) {
					if (readBits != 1) {
						copyDist++;
					}
				} else if (readBits == 1) {
					copyDist--;
				}
				if (readBits != 1) {
					copyDist <<= 1;
					if (!dec.ReadBit(probs + 2, kDecayNormal, kBonusNormal)) {
						copyDist--;
					}
				}
			}

			if (copyDist >= (u32)outPos) {
				return SCE_KERNEL_ERROR_INVALID_FORMAT;
			}
		}

		const int copyLen = (int)copyCount + 1;
		// The hardware routine bounds-checks only the literal path, so a crafted stream can
		// write up to 255 bytes past the end of the output buffer here. Check it.
		if (copyLen > outSize - outPos) {
			return SCE_KERNEL_ERROR_INVALID_SIZE;
		}

		// Forward, byte at a time - overlap is legal and is how runs are encoded.
		const u8 *src = out + outPos - copyDist - 1;
		for (int i = 0; i < copyLen; i++) {
			out[outPos + i] = src[i];
		}
		prevByte = out[outPos + (int)copyCount];
		outPos += (int)copyCount;
		countBitsIdx = 6 + (outPos & 1);
	}
}
