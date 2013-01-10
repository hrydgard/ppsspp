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


void PSPSetTexture();
void TextureCache_Init();
void TextureCache_Shutdown();
void TextureCache_Clear(bool delete_them);
void TextureCache_StartFrame();
void TextureCache_Decimate();  // Run this once per frame to get rid of old textures.
void TextureCache_Invalidate(u32 addr, int size, bool force);
void TextureCache_InvalidateAll(bool force);
int TextureCache_NumLoadedTextures();
