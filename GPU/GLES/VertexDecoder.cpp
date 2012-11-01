// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#ifdef ANDROID
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#else
#include <GL/glew.h>
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#endif

#include "math/lin/matrix4x4.h"

#include "../../Core/MemMap.h"
#include "../ge_constants.h"
#include "../GPUState.h"

#include "VertexDecoder.h"

const int tcsize[4] = {0,2,4,8}, tcalign[4] = {0,1,2,4};
const int colsize[8] = {0,0,0,0,2,2,2,4}, colalign[8] = {0,0,0,0,2,2,2,4};
const int nrmsize[4] = {0,3,6,12}, nrmalign[4] = {0,1,2,4};
const int possize[4] = {0,3,6,12}, posalign[4] = {0,1,2,4};
const int wtsize[4] = {0,1,2,4}, wtalign[4] = {0,1,2,4};

inline int align(int n, int align)
{
	return (n+(align-1)) & ~(align-1);
}

static int onesize;

void VertexDecoder::SetVertexType(u32 fmt)
{
	fmt = fmt;
	throughmode = (fmt & GE_VTYPE_THROUGH) != 0;

	int biggest = 0;
	size = 0;
	
	tc				 = fmt & 0x3;
	col				= (fmt >> 2) & 0x7;
	nrm				= (fmt >> 5) & 0x3;
	pos				= (fmt >> 7) & 0x3;
	weighttype = (fmt >> 9) & 0x3;
	idx				= (fmt >> 11) & 0x3;
	morphcount = ((fmt >> 18) & 0x7)+1;
	nweights	 = ((fmt >> 14) & 0x7)+1;

	DEBUG_LOG(G3D,"VTYPE: THRU=%i TC=%i COL=%i POS=%i NRM=%i WT=%i NW=%i IDX=%i MC=%i", (int)throughmode, tc,col,pos,nrm,weighttype,nweights,idx,morphcount);
	
	if (weighttype)
	{
		//size = align(size, wtalign[weighttype]);	unnecessary
		size += wtsize[weighttype] * nweights;
		if (wtalign[weighttype] > biggest)
			biggest = wtalign[weighttype];
	}

	if (tc)
	{
		size = align(size, tcalign[tc]);
		tcoff = size;
		size += tcsize[tc];
		if (tcalign[tc]>biggest)
			biggest=tcalign[tc];
	}

	if (col)
	{
		size = align(size,colalign[col]);
		coloff = size;
		size += colsize[col];
		if (colalign[col] > biggest)
			biggest = colalign[col]; 
	}
	else
	{
		coloff = 0;
	}
	if (nrm)
	{
		size = align(size,nrmalign[nrm]);
		nrmoff = size;
		size += nrmsize[nrm];
		if (nrmalign[nrm] > biggest)
			biggest = nrmalign[nrm]; 
	}

	//if (pos)
	{
		size = align(size,posalign[pos]);
		posoff = size;
		size += possize[pos];
		if (posalign[pos] > biggest)
			biggest = posalign[pos];
	}

	size = align(size,biggest);
	onesize = size;
	size *= morphcount;
	DEBUG_LOG(G3D,"SVT : size = %i, aligned to biggest %i", size, biggest);
}

void VertexDecoder::DecodeVerts(DecodedVertex *decoded, const void *verts, const void *inds, int prim, int count) const
{
	if (morphcount == 1)
		gstate.morphWeights[0] = 1.0f;

	char *ptr = (char *)verts;

	for (int i = 0; i < count; i++)
	{
		int index;
		if (idx == (GE_VTYPE_IDX_8BIT >> 11))
		{
			index = ((u8*)inds)[i];
		} 
		else if (idx == (GE_VTYPE_IDX_16BIT >> 11))
		{
			index = ((u16*)inds)[i];
		}
		else
		{
			index = i;
		}

		ptr = (char*)verts + (index * size);

		float *wt = decoded[index].weights;
		switch (weighttype)
		{
		case GE_VTYPE_WEIGHT_NONE >> 9:
			break;

		case GE_VTYPE_WEIGHT_8BIT >> 9:
			{
				u8 *wdata = (u8*)(ptr);
				for (int j=0; j<nweights; j++)
					wt[j] = (float)wdata[j] / 255.0f;
			}
			break;

		case GE_VTYPE_WEIGHT_16BIT >> 9:
			{
				u16 *wdata = (u16*)(ptr);
				for (int j=0; j<nweights; j++)
					wt[j] = (float)wdata[j] / 65535.0f;
			}
			break;

		case GE_VTYPE_WEIGHT_FLOAT >> 9:
			{
				float *wdata = (float*)(ptr+0);
				for (int j=0; j<nweights; j++)
					wt[j] = wdata[j];
			}
			break;
		}

		float *uv = decoded[index].uv;
		switch (tc)
		{
		case GE_VTYPE_TC_NONE:
			uv[0] = 0.0f;
			uv[1] = 0.0f;
			break;

		case GE_VTYPE_TC_8BIT:
			{
				u8 *uvdata = (u8*)(ptr + tcoff);
				for (int j=0; j<2; j++)
					uv[j] = (float)uvdata[j]/255.0f;
				break;
			}

		case GE_VTYPE_TC_16BIT:
			{
				u16 *uvdata = (u16*)(ptr + tcoff);
				if (throughmode)
				{
					uv[0] = (float)uvdata[0] / (float)(gstate.curTextureWidth);
					uv[1] = (float)uvdata[1] / (float)(gstate.curTextureHeight);
				}
				else
				{
					uv[0] = (float)uvdata[0] / 65535.0f;
					uv[1] = (float)uvdata[1] / 65535.0f;
				}
			}
			break;

		case GE_VTYPE_TC_FLOAT:
			{
				float *uvdata = (float*)(ptr + tcoff);
				for (int j=0; j<2; j++)
					uv[j] = uvdata[j];
			}
			break;
		}

		float *c = decoded[index].color;
		switch (col)
		{
		case GE_VTYPE_COL_4444>>2:
			{
				u16 cdata = *(u16*)(ptr + coloff);
				for (int j = 0; j < 4; j++)
					c[j] = (float)(cdata>>(j * 4) & 0xF) / 15.0f;
			}
			break;

		case GE_VTYPE_COL_565>>2:
			{
				u16 cdata = *(u16*)(ptr + coloff);
				c[0] = (float)(cdata & 0x1f) / 31.0f;
				c[1] = (float)((cdata>>5) & 0x3f) / 63.0f;
				c[2] = (float)((cdata>>11) & 0x1f) / 31.0f;
				c[3] = 1.0f;
			}
			break;

		case GE_VTYPE_COL_5551>>2:
			{
				u16 cdata = *(u16*)(ptr + coloff);
				c[0] = (float)(cdata & 0x1f) / 31.0f;
				c[1] = (float)((cdata>>5) & 0x1f) / 31.0f;
				c[2] = (float)((cdata>>10) & 0x1f) / 31.0f;
				c[3] = (float)(cdata>>15);
			}
			break;

		case GE_VTYPE_COL_8888>>2:
			{
				u8 *cdata = (u8*)(ptr + coloff);
				for (int j=0; j<4; j++)
					c[j] = (float)cdata[j] / 255.0f;
			}
			break;

		default:
			c[0]=1.0f; c[1]=1.0f; c[2]=1.0f; c[3]=1.0f;
			break;
		}


		float *normal = decoded[index].normal;
		memset(normal,0,sizeof(float)*3);
		for (int n=0; n<morphcount; n++)
		{
			switch (nrm)
			{
			case 0:
				//no normals
				break;

			case GE_VTYPE_NRM_FLOAT>>5:
				{
					float *fv = (float*)(ptr + onesize*n + nrmoff);
					for (int j=0; j<3; j++)
						normal[j] += fv[j] * gstate.morphWeights[n];
				}
				break;

			case GE_VTYPE_NRM_16BIT>>5:
				{
					short *sv = (short*)(ptr + onesize*n + nrmoff);
					for (int j=0; j<3; j++)
						normal[j] += (sv[j]/32767.0f) * gstate.morphWeights[n];
				}
				break;

			default:
				DEBUG_LOG(G3D,"Unknown normal format %i",nrm);
				break;
			}
		}

		if (gstate.reversenormals)
		{
			for (int j = 0; j < 3; j++)
				normal[j] = -normal[j];
		}

		float *v = decoded[index].pos;
		memset(v, 0, sizeof(float)*3);
		for (int n = 0; n < morphcount; n++)
		{
			switch (pos)
			{
			case GE_VTYPE_POS_FLOAT>>7:
				{
					float *fv = (float*)(ptr + onesize*n + posoff);
					for (int j=0; j<3; j++)
						v[j] += fv[j] * gstate.morphWeights[n];
				}
				break;

			case GE_VTYPE_POS_16BIT>>7:
				{
					short *sv = (short*)(ptr + onesize*n + posoff);
					for (int j = 0; j < 3; j++)
						v[j] += sv[j] * gstate.morphWeights[n];
				}
				break;

			default:
				DEBUG_LOG(G3D,"Unknown position format %i",pos);
				break;
			}
		}
	}
}

