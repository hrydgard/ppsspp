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

#pragma once

#include "Common/CommonTypes.h"

struct VirtualFramebuffer;

enum FramebufferNotification {
	NOTIFY_FB_CREATED,
	NOTIFY_FB_UPDATED,
	NOTIFY_FB_DESTROYED,
};

class TextureCacheCommon {
public:
	virtual ~TextureCacheCommon();

	virtual bool SetOffsetTexture(u32 offset);
	virtual void ForgetLastTexture() = 0;
	virtual void NotifyFramebuffer(u32 address, VirtualFramebuffer *framebuffer, FramebufferNotification msg) = 0;

	// OpenGL leaking...
	virtual u32 AllocTextureName() { return 0; }
};
