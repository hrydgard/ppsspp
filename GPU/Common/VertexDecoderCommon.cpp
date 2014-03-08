// Copyright (c) 2013- PPSSPP Project.

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

#include <stdio.h>
#include "GPU/Common/VertexDecoderCommon.h"

int DecFmtSize(u8 fmt) {
	switch (fmt) {
	case DEC_NONE: return 0;
	case DEC_FLOAT_1: return 4;
	case DEC_FLOAT_2: return 8;
	case DEC_FLOAT_3: return 12;
	case DEC_FLOAT_4: return 16;
	case DEC_S8_3: return 4;
	case DEC_S16_3: return 8;
	case DEC_U8_1: return 4;
	case DEC_U8_2: return 4;
	case DEC_U8_3: return 4;
	case DEC_U8_4: return 4;
	case DEC_U16_1: return 4;
	case DEC_U16_2: return 4;
	case DEC_U16_3: return 8;
	case DEC_U16_4: return 8;
	case DEC_U8A_2: return 4;
	case DEC_U16A_2: return 4;
	default:
		return 0;
	}
}

void GetIndexBounds(const void *inds, int count, u32 vertType, u16 *indexLowerBound, u16 *indexUpperBound) {
	// Find index bounds. Could cache this in display lists.
	// Also, this could be greatly sped up with SSE2/NEON, although rarely a bottleneck.
	int lowerBound = 0x7FFFFFFF;
	int upperBound = 0;
	u32 idx = vertType & GE_VTYPE_IDX_MASK;
	if (idx == GE_VTYPE_IDX_8BIT) {
		const u8 *ind8 = (const u8 *)inds;
		for (int i = 0; i < count; i++) {
			u8 value = ind8[i];
			if (value > upperBound)
				upperBound = value;
			if (value < lowerBound)
				lowerBound = value;
		}
	} else if (idx == GE_VTYPE_IDX_16BIT) {
		const u16 *ind16 = (const u16*)inds;
		for (int i = 0; i < count; i++) {
			u16 value = ind16[i];
			if (value > upperBound)
				upperBound = value;
			if (value < lowerBound)
				lowerBound = value;
		}
	} else {
		lowerBound = 0;
		upperBound = count - 1;
	}
	*indexLowerBound = (u16)lowerBound;
	*indexUpperBound = (u16)upperBound;
}

void PrintDecodedVertex(VertexReader &vtx) {
	if (vtx.hasNormal()) {
		float nrm[3];
		vtx.ReadNrm(nrm);
		printf("N: %f %f %f\n", nrm[0], nrm[1], nrm[2]);
	}
	if (vtx.hasUV()) {
		float uv[2];
		vtx.ReadUV(uv);
		printf("TC: %f %f\n", uv[0], uv[1]);
	}
	if (vtx.hasColor0()) {
		float col0[4];
		vtx.ReadColor0(col0);
		printf("C0: %f %f %f %f\n", col0[0], col0[1], col0[2], col0[3]);
	}
	if (vtx.hasColor1()) {
		float col1[3];
		vtx.ReadColor1(col1);
		printf("C1: %f %f %f\n", col1[0], col1[1], col1[2]);
	}
	// Etc..
	float pos[3];
	vtx.ReadPos(pos);
	printf("P: %f %f %f\n", pos[0], pos[1], pos[2]);
}
