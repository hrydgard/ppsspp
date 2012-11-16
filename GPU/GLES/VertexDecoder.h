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
#include "../Globals.h"
#include "base/basictypes.h"

struct DecodedVertex
{
	float pos[3];     // in case of morph, preblend during decode
	float normal[3];  // in case of morph, preblend during decode
	float uv[2];      // scaled by uscale, vscale, if there
	u8 color[4];   // unlit
	float weights[8];  // ugh, expensive
};

struct TransformedVertex
{
	float x, y, z;     // in case of morph, preblend during decode
	float uv[2];      // scaled by uscale, vscale, if there
	float color0[4];   // prelit
	float color1[4];   // prelit
};



// Right now 
//   - only contains computed information
//   - does decoding in nasty branchfilled loops
// Future TODO 
//   - will compile into lighting fast specialized x86 
//   - will not bother translating components that can be read directly
//     by OpenGL ES. Will still have to translate 565 colors, and things
//     like that. DecodedVertex will not be a fixed struct. Will have to
//     do morphing here.
//
// We want 100% perf on 1Ghz even in vertex complex games!
class VertexDecoder 
{
public:
	VertexDecoder() : coloff(0), nrmoff(0), posoff(0) {}
	~VertexDecoder() {}
	void SetVertexType(u32 fmt);
	void DecodeVerts(DecodedVertex *decoded, const void *verts, const void *inds, int prim, int count) const;

private:
	u32 fmt;
	bool throughmode;
	int biggest;
	int size;
	int onesize_;

	int weightoff;
	int tcoff;
	int coloff;
	int nrmoff;
	int posoff;

	int tc;
	int col;
	int nrm;
	int pos;
	int weighttype;
	int idx;
	int morphcount;
	int nweights;

};
