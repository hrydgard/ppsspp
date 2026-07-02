#pragma once

// The plan is to converge Texture and Framebuffer more and more.
// Here are things that are common between the two.

#include "Common/CommonTypes.h"

enum class TextureAlpha : u8 {
	Any = 0,
	Solid = 1,
};
