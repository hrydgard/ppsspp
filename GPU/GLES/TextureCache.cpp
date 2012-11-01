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

#include <map>

#include "../../Core/MemMap.h"
#include "../ge_constants.h"
#include "../GPUState.h"
#include "TextureCache.h"


struct TexCacheEntry
{
	u32 addr;
	u32 hash;
	u32 frameCounter;
	u32 numMips;
	int dim;
	GLuint texture;
};

typedef std::map<u32, TexCacheEntry> TexCache;
static TexCache cache;

u8 *tempArea;

void TextureCache_Clear(bool delete_them)
{
	if (delete_them)
	{
		for (TexCache::iterator iter = cache.begin(); iter != cache.end(); ++iter)
		{
			DEBUG_LOG(G3D, "Deleting texture %i", iter->second.texture);
			glDeleteTextures(1, &iter->second.texture);
		}
	}
	if (cache.size()) {
		INFO_LOG(G3D, "Texture cached cleared from %i textures", (int)cache.size());
		cache.clear();
	}
}

u32 PaletteLoad(int index)
{
	int pf = gstate.clutformat & 3;
	int shift = (gstate.clutformat >> 2) & 31;
	int mask = (gstate.clutformat >> 8) & 255;
	int start = ((gstate.clutformat >> 16) & 31) * 16;

	if (pf<3)
	{
		//16-bit
		u16 *p = (u16*)gstate.paletteMem;
		u16 col = p[((start+index)>>shift) & mask];
		int r,g,b,a;

		// TODO: properly expand the lower bits.
		switch (pf)
		{
		case 0:
			r = (col&0x1f)*8;
			g = ((col>>5)&0x3f)*4;
			b = ((col>>11)&0x1f)*8;
			a = 255;
			break;
		case 1:
			r = (col&0x1f)*8;
			g = ((col>>5)&0x1f)*8;
			b = ((col>>10)&0x1f)*8;
			a = (col>>15)*255;
			break;
		case 2:
			r = (col&0xf)*16;
			g = ((col>>4)&0xf)*16;
			b = ((col>>8)&0xf)*16;
			a = ((col>>12)&0xF)*16;
			break;
		}

		// We now use OpenGL ES 2.0 style colors.
		return (a << 24) | (b<<16) | (g<<8) | (r<<0);
	}
	else
	{
		u32 *p = (u32*)gstate.paletteMem;
		u32 col = p[((start + index) >> shift) & mask];
		return col;
	}
}

// This should not have to be done per texture! OpenGL is silly yo
// TODO: Dirty-check this against the current texture.
void UpdateSamplingParams()
{
	int minFilt = gstate.texfilter & 0x7;
	int magFilt = (gstate.texfilter>>8)&1;
	minFilt &= 1; //no mipmaps yet

	int sClamp = gstate.texwrap & 1;
	int tClamp = (gstate.texwrap>>8) & 1;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sClamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, tClamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilt ? GL_LINEAR : GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilt ? GL_LINEAR : GL_NEAREST);
}


// Convert from PSP bit order to GLES bit order
u16 convert565(u16 c) {
	return (c >> 11) | (c & 0x07E0) | (c << 11); 
}

// Convert from PSP bit order to GLES bit order
u16 convert4444(u16 c) {
	return (c >> 12) | ((c >> 4) & 0xF0) | ((c << 4) & 0xF00) | (c << 12);
}

// Convert from PSP bit order to GLES bit order
u16 convert5551(u16 c) {
	return ((c & 0x8000) >> 15) | (c << 1);
}


struct DXT1Block
{
	u8 lines[4];
	u16 color1;
	u16 color2;
};

inline u8 Convert5To8(u8 v)
{
	// Swizzle bits: 00012345 -> 12345123
	return (v << 3) | (v >> 2);
}

inline u8 Convert6To8(u8 v)
{
	// Swizzle bits: 00123456 -> 12345612
	return (v << 2) | (v >> 4);
}

inline u32 makecol(int r, int g, int b, int a)
{
	return (a << 24)|(r << 16)|(g << 8)|b;
}

void decodeDXT1Block(u32 *dst, const DXT1Block *src, int pitch)
{
	// S3TC Decoder
	// Needs more speed and debugging.
	u16 c1 = src->color1;
	u16 c2 = src->color2;
	int blue1 = Convert5To8(c1 & 0x1F);
	int blue2 = Convert5To8(c2 & 0x1F);
	int green1 = Convert6To8((c1 >> 5) & 0x3F);
	int green2 = Convert6To8((c2 >> 5) & 0x3F);
	int red1 = Convert5To8((c1 >> 11) & 0x1F);
	int red2 = Convert5To8((c2 >> 11) & 0x1F);
	int colors[4];
	colors[0] = makecol(red1, green1, blue1, 255);
	colors[1] = makecol(red2, green2, blue2, 255);
	if (c1 > c2)
	{
		int blue3 = ((blue2 - blue1) >> 1) - ((blue2 - blue1) >> 3);
		int green3 = ((green2 - green1) >> 1) - ((green2 - green1) >> 3);
		int red3 = ((red2 - red1) >> 1) - ((red2 - red1) >> 3);				
		colors[2] = makecol(red1 + red3, green1 + green3, blue1 + blue3, 255);
		colors[3] = makecol(red2 - red3, green2 - green3, blue2 - blue3, 255); 
	}
	else
	{
		colors[2] = makecol((red1 + red2 + 1) / 2, // Average
			(green1 + green2 + 1) / 2,
			(blue1 + blue2 + 1) / 2, 255);
		colors[3] = makecol(red2, green2, blue2, 0);	// Color2 but transparent
	}

	for (int y = 0; y < 4; y++)
	{ 
		int val = src->lines[y];
		for (int x = 0; x < 4; x++)
		{
			dst[x] = colors[(val >> 6) & 3];
			val <<= 2;
		}
		dst += pitch;
	}
}

void PSPSetTexture()
{
	if (!tempArea)
		tempArea = new u8[512*512*4*4];	// PSP maximum texture size

	u32 texaddr = (gstate.texaddr[0] & 0xFFFFF0) | ((gstate.texbufwidth[0]<<8) & 0xFF000000);
	texaddr &= 0xFFFFFFF;

	if (!texaddr) return;

	DEBUG_LOG(G3D,"Texture at %08x",texaddr);
	u8 *texptr = Memory::GetPointer(texaddr);

	TexCache::iterator iter = cache.find(texaddr);
	if (iter != cache.end())
	{
		//Validate the texture here (width, height etc)
		TexCacheEntry &entry = iter->second;

		int dim = gstate.texsize[0] & 0xF0F;
		
		if (dim == entry.dim && entry.hash == *(u32*)texptr)
		{
			//got one!
			glBindTexture(GL_TEXTURE_2D, entry.texture);
			UpdateSamplingParams();
			DEBUG_LOG(G3D,"Texture at %08x Found in Cache, applying", texaddr);
			return; //Done!
		}
		else
		{
			NOTICE_LOG(G3D,"Texture different or overwritten, reloading at %08x", texaddr);

			//Damnit, got overwritten.
			//if (dim != entry.dim)
			//{
			//	glDeleteTextures(1, &entry.texture);
			//}
			cache.erase(iter);
		}
	}
	else
	{
		NOTICE_LOG(G3D,"No texture in cache, decoding...");
	}

	//we have to decode it

	TexCacheEntry entry;

	entry.addr = texaddr;
	entry.hash = *(u32*)texptr;
	glGenTextures(1, &entry.texture);
	NOTICE_LOG(G3D, "Creating texture %i", entry.texture);

	glBindTexture(GL_TEXTURE_2D, entry.texture);
			
	int bufw = gstate.texbufwidth[0] & 0x3ff;
	
	entry.dim = gstate.texsize[0] & 0xF0F;

	int w = 1 << (gstate.texsize[0] & 0xf);
	int h = 1 << ((gstate.texsize[0]>>8) & 0xf);

	gstate.curTextureHeight=h;
	gstate.curTextureWidth=w;
	int format = gstate.texformat & 0xF;

	DEBUG_LOG(G3D,"Texture Width %04x Height %04x Bufw %d Fmt %d", w, h, bufw, format);

	// TODO: Look into using BGRA for 32-bit textures when the GL_EXT_texture_format_BGRA8888 extension is available, as it's faster than RGBA on some chips.

	switch (format)
	{
	case GE_TFMT_4444:
	case GE_TFMT_5551:
	case GE_TFMT_5650:
		{
			u16 *dst = (u16*)tempArea;
			u16 *src = (u16*)texptr;

			int fmt = GL_UNSIGNED_SHORT_5_6_5;
			int internal_format = GL_RGBA;
			u16 (*convFunc)(u16);
			switch (format)
			{
			case GE_TFMT_4444: fmt = GL_UNSIGNED_SHORT_4_4_4_4; convFunc = &convert4444; break;
			case GE_TFMT_5551: fmt = GL_UNSIGNED_SHORT_5_5_5_1; convFunc = &convert5551; break;
			case GE_TFMT_5650: fmt = GL_UNSIGNED_SHORT_5_6_5; convFunc = &convert565; internal_format = GL_RGB; break;
			}

			if (gstate.texmode & 1) //Swizzled!
			{
				for (int y=0; y<h/8; y++)
				{
					for (int x=0; x<w/8; x++)
					{
						for (int yy=0; yy<8; yy++)
						{
							for (int xx=0; xx<8; xx++)
							{
								dst[(y*8+yy)*w + x*8 + xx] = convFunc(src[(y*(bufw/8)+x)*8 + (yy*8+xx)]);
							}
						}
					}
				}
			}
			else
			{
				for (int y=0; y<h; y++)
				{
					const u16 *s = src + bufw * y;
					u16 *d = dst + y * w;
					for (int x = 0; x < w; x++)
						*d++ = convFunc(*s++);
				}
			}

			// TODO: This will have to be redone for OpenGL ES 2.0.

			glTexImage2D(GL_TEXTURE_2D, 0, internal_format, w, h, 0, internal_format, fmt, (GLvoid*)tempArea);
			break;
		}
		
	case GE_TFMT_CLUT4:
		{
			u32 *dst = (u32*)tempArea;
			u8 *src = (u8*)texptr;
			if (gstate.texmode & 1) //Swizzled!
			{
				for (int y=0; y<h/8; y++)
				{
					for (int x=0; x<w/32; x++)
					{
						for (int yy=0; yy<8; yy++)
						{
							for (int xx=0; xx<32; xx++)
							{
								int idx = src[(y*bufw*4+x*16*8) + (yy*16+xx/2)];
								if (xx&1) idx>>=4; else idx&=0xF;
								dst[(y*8+yy)*w + x*32 + xx] = PaletteLoad(idx);
							}
						}
					}
				}
			}
			else
			{
				for (int y=0; y<h; y++)
				{
					for (int x=0; x<w; x++)
					{
						int idx = *src;
						if (x&1) idx >>= 4; else idx &= 0xF;
						dst[x] = PaletteLoad(idx);
						if (x&1)
							src++;
					}
					src -= w/2;
					src += bufw/2;
					dst += w; 
				}
			}
			int fmt = GL_UNSIGNED_BYTE;
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, fmt, (GLvoid*)tempArea);
			break;
		}

	case GE_TFMT_CLUT8:
		{
			u32 *dst = (u32*)tempArea;
			u8 *src = (u8*)texptr;
			if (gstate.texmode & 1) //Swizzled!
			{
				for (int y=0; y<h/8; y++)
				{
					for (int x=0; x<w/16; x++)
					{
						for (int yy=0; yy<8; yy++)
						{
							for (int xx=0; xx<16; xx++)
							{
								int idx = src[(y*bufw*8+x*16*8) + (yy*16+xx)];
								dst[(y*8+yy)*w + x*16 + xx] = PaletteLoad(idx);
							}
						}
					}
				}
			}
			else
			{
				for (int y=0; y<h; y++)
				{
					for (int x=0; x<w; x++)
					{
						int idx = src[x];
						dst[x] = PaletteLoad(idx);
					}
					src += bufw;
					dst += w; 
				}
			}
			int fmt = GL_UNSIGNED_BYTE;
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, fmt, (GLvoid*)tempArea);
			break;
		}

	case GE_TFMT_8888:
		{
			u32 *dst = (u32*)tempArea;
			u32 *src = (u32*)texptr;
			if (gstate.texmode & 1) //Swizzled!
			{
				for (int y=0; y<h/8; y++)
				{
					int i = y*bufw*8;
					for (int x=0; x<w/4; x++)
					{
						for (int yy=0; yy<8; yy++)
						{
							for (int xx=0; xx<4; xx++)
							{
								dst[(y*8+yy)*w + x*4 + xx] = src[i];
								i++;
							}
						}
					}
				}
			}
			else
			{
				for (int y=0; y<h; y++)
				{
					memcpy(dst+y*w,src+bufw*y,4*bufw);
				}
			}
			int fmt = GL_UNSIGNED_BYTE;
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, fmt, (GLvoid*)tempArea);
			break;
		}

	case GE_TFMT_DXT1:
		{
			// THIS IS VERY BROKEN but can be debugged! :)
			u32 *dst = (u32*)tempArea;
			DXT1Block *src = (DXT1Block*)texptr;

			for (int y=0; y<h/4; y++)
			{
				int i = y*w/4;
				for (int x=0; x<w/4; x++)
				{
					decodeDXT1Block(dst + w*4 * y * 4 + x * 4, src + i, w);
					i++;
				}
			}
			int fmt = GL_UNSIGNED_BYTE;
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, fmt, (GLvoid*)tempArea);
			break;
		}
		break;

	case GE_TFMT_DXT3:
	case GE_TFMT_DXT5:
	default:
		ERROR_LOG(G3D, "Unknown Texture Format %i, not setting texture",format);
		PanicAlert("ANOTHER tex format??");
		return;
	}

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	// glGenerateMipmap(GL_TEXTURE_2D);
	UpdateSamplingParams();

	cache[texaddr] = entry;
}
