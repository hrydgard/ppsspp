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

#include <algorithm>
#include <cstring>

#include "ext/xxhash.h"
#include "i18n/i18n.h"
#include "math/math_util.h"
#include "profiler/profiler.h"
#include "thin3d/thin3d.h"
#include "thin3d/VulkanRenderManager.h"

#include "Common/ColorConv.h"
#include "Core/Config.h"
#include "Core/Host.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "Core/System.h"

#include "Common/Vulkan/VulkanContext.h"
#include "Common/Vulkan/VulkanImage.h"
#include "Common/Vulkan/VulkanMemory.h"

#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Vulkan/TextureCacheVulkan.h"
#include "GPU/Vulkan/FramebufferVulkan.h"
#include "GPU/Vulkan/FragmentShaderGeneratorVulkan.h"
#include "GPU/Vulkan/DepalettizeShaderVulkan.h"
#include "GPU/Vulkan/ShaderManagerVulkan.h"
#include "GPU/Vulkan/DrawEngineVulkan.h"

#define TEXCACHE_MIN_SLAB_SIZE (8 * 1024 * 1024)
#define TEXCACHE_MAX_SLAB_SIZE (32 * 1024 * 1024)
#define TEXCACHE_SLAB_PRESSURE 4

// Note: some drivers prefer B4G4R4A4_UNORM_PACK16 over R4G4B4A4_UNORM_PACK16.
#define VULKAN_4444_FORMAT VK_FORMAT_B4G4R4A4_UNORM_PACK16
#define VULKAN_1555_FORMAT VK_FORMAT_A1R5G5B5_UNORM_PACK16
#define VULKAN_565_FORMAT  VK_FORMAT_B5G6R5_UNORM_PACK16
#define VULKAN_8888_FORMAT VK_FORMAT_R8G8B8A8_UNORM

static const VkComponentMapping VULKAN_4444_SWIZZLE = { VK_COMPONENT_SWIZZLE_A, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B };
static const VkComponentMapping VULKAN_1555_SWIZZLE = { VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_A };
static const VkComponentMapping VULKAN_565_SWIZZLE = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
static const VkComponentMapping VULKAN_8888_SWIZZLE = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };

// 4xBRZ shader - Copyright (C) 2014-2016 DeSmuME team (GPL2+)
// Hyllian's xBR-vertex code and texel mapping
// Copyright (C) 2011/2016 Hyllian - sergiogdb@gmail.com
// TODO: Handles alpha badly for PSP.
const char *shader4xbrz = R"(
vec4 premultiply_alpha(vec4 c) { float a = clamp(c.a, 0.0, 1.0); return vec4(c.rgb * a, a); }
vec4 postdivide_alpha(vec4 c) { return c.a < 0.001? vec4(0.0,0.0,0.0,0.0) : vec4(c.rgb / c.a, c.a); }

#define BLEND_ALPHA 1
#define BLEND_NONE 0
#define BLEND_NORMAL 1
#define BLEND_DOMINANT 2
#define LUMINANCE_WEIGHT 1.0
#define EQUAL_COLOR_TOLERANCE 30.0/255.0
#define STEEP_DIRECTION_THRESHOLD 2.2
#define DOMINANT_DIRECTION_THRESHOLD 3.6

float reduce(vec4 color) {
	return dot(color.rgb, vec3(65536.0, 256.0, 1.0));
}

float DistYCbCr(vec4 pixA, vec4 pixB) {
	const vec3 w = vec3(0.2627, 0.6780, 0.0593);
	const float scaleB = 0.5 / (1.0 - w.b);
	const float scaleR = 0.5 / (1.0 - w.r);
	vec4 diff = pixA - pixB;
	float Y = dot(diff.rgb, w);
	float Cb = scaleB * (diff.b - Y);
	float Cr = scaleR * (diff.r - Y);

	return sqrt( ((LUMINANCE_WEIGHT * Y) * (LUMINANCE_WEIGHT * Y)) + (Cb * Cb) + (Cr * Cr) + (diff.a * diff.a));
}

bool IsPixEqual(const vec4 pixA, const vec4 pixB) {
	return (DistYCbCr(pixA, pixB) < EQUAL_COLOR_TOLERANCE);
}

bool IsBlendingNeeded(const ivec4 blend) {
	ivec4 diff = blend - ivec4(BLEND_NONE);
	return diff.x != 0 || diff.y != 0 || diff.z != 0 || diff.w != 0;
}

vec4 applyScalingf(uvec2 origxy, uvec2 xy) {
	float dx = 1.0 / params.width;
	float dy = 1.0 / params.height;

	//    A1 B1 C1
	// A0 A  B  C C4
	// D0 D  E  F F4
	// G0 G  H  I I4
	//    G5 H5 I5

	uvec4 t1 = uvec4(origxy.x - 1, origxy.x, origxy.x + 1, origxy.y - 2); // A1 B1 C1
	uvec4 t2 = uvec4(origxy.x - 1, origxy.x, origxy.x + 1, origxy.y - 1); // A B C
	uvec4 t3 = uvec4(origxy.x - 1, origxy.x, origxy.x + 1, origxy.y + 0); // D E F
	uvec4 t4 = uvec4(origxy.x - 1, origxy.x, origxy.x + 1, origxy.y + 1); // G H I
	uvec4 t5 = uvec4(origxy.x - 1, origxy.x, origxy.x + 1, origxy.y + 2); // G5 H5 I5
	uvec4 t6 = uvec4(origxy.x - 2, origxy.y - 1, origxy.y, origxy.y + 1); // A0 D0 G0
	uvec4 t7 = uvec4(origxy.x + 2, origxy.y - 1, origxy.y, origxy.y + 1); // C4 F4 I4

	vec2 f = fract(vec2(float(xy.x) / float(params.scale), float(xy.y) / float(params.scale)));

	//---------------------------------------
	// Input Pixel Mapping:    |21|22|23|
	//                       19|06|07|08|09
	//                       18|05|00|01|10
	//                       17|04|03|02|11
	//                         |15|14|13|

	vec4 src[25];

	src[21] = premultiply_alpha(readColorf(t1.xw));
	src[22] = premultiply_alpha(readColorf(t1.yw));
	src[23] = premultiply_alpha(readColorf(t1.zw));
	src[ 6] = premultiply_alpha(readColorf(t2.xw));
	src[ 7] = premultiply_alpha(readColorf(t2.yw));
	src[ 8] = premultiply_alpha(readColorf(t2.zw));
	src[ 5] = premultiply_alpha(readColorf(t3.xw));
	src[ 0] = premultiply_alpha(readColorf(t3.yw));
	src[ 1] = premultiply_alpha(readColorf(t3.zw));
	src[ 4] = premultiply_alpha(readColorf(t4.xw));
	src[ 3] = premultiply_alpha(readColorf(t4.yw));
	src[ 2] = premultiply_alpha(readColorf(t4.zw));
	src[15] = premultiply_alpha(readColorf(t5.xw));
	src[14] = premultiply_alpha(readColorf(t5.yw));
	src[13] = premultiply_alpha(readColorf(t5.zw));
	src[19] = premultiply_alpha(readColorf(t6.xy));
	src[18] = premultiply_alpha(readColorf(t6.xz));
	src[17] = premultiply_alpha(readColorf(t6.xw));
	src[ 9] = premultiply_alpha(readColorf(t7.xy));
	src[10] = premultiply_alpha(readColorf(t7.xz));
	src[11] = premultiply_alpha(readColorf(t7.xw));

	float v[9];
	v[0] = reduce(src[0]);
	v[1] = reduce(src[1]);
	v[2] = reduce(src[2]);
	v[3] = reduce(src[3]);
	v[4] = reduce(src[4]);
	v[5] = reduce(src[5]);
	v[6] = reduce(src[6]);
	v[7] = reduce(src[7]);
	v[8] = reduce(src[8]);

	ivec4 blendResult = ivec4(BLEND_NONE);

	// Preprocess corners
	// Pixel Tap Mapping: --|--|--|--|--
	//                    --|--|07|08|--
	//                    --|05|00|01|10
	//                    --|04|03|02|11
	//                    --|--|14|13|--
	// Corner (1, 1)
	if ( ((v[0] == v[1] && v[3] == v[2]) || (v[0] == v[3] && v[1] == v[2])) == false) {
		float dist_03_01 = DistYCbCr(src[ 4], src[ 0]) + DistYCbCr(src[ 0], src[ 8]) + DistYCbCr(src[14], src[ 2]) + DistYCbCr(src[ 2], src[10]) + (4.0 * DistYCbCr(src[ 3], src[ 1]));
		float dist_00_02 = DistYCbCr(src[ 5], src[ 3]) + DistYCbCr(src[ 3], src[13]) + DistYCbCr(src[ 7], src[ 1]) + DistYCbCr(src[ 1], src[11]) + (4.0 * DistYCbCr(src[ 0], src[ 2]));
		bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_03_01) < dist_00_02;
		blendResult[2] = ((dist_03_01 < dist_00_02) && (v[0] != v[1]) && (v[0] != v[3])) ? ((dominantGradient) ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;
	}

	// Pixel Tap Mapping: --|--|--|--|--
	//                    --|06|07|--|--
	//                    18|05|00|01|--
	//                    17|04|03|02|--
	//                    --|15|14|--|--
	// Corner (0, 1)
	if ( ((v[5] == v[0] && v[4] == v[3]) || (v[5] == v[4] && v[0] == v[3])) == false) {
		float dist_04_00 = DistYCbCr(src[17], src[ 5]) + DistYCbCr(src[ 5], src[ 7]) + DistYCbCr(src[15], src[ 3]) + DistYCbCr(src[ 3], src[ 1]) + (4.0 * DistYCbCr(src[ 4], src[ 0]));
		float dist_05_03 = DistYCbCr(src[18], src[ 4]) + DistYCbCr(src[ 4], src[14]) + DistYCbCr(src[ 6], src[ 0]) + DistYCbCr(src[ 0], src[ 2]) + (4.0 * DistYCbCr(src[ 5], src[ 3]));
		bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_05_03) < dist_04_00;
		blendResult[3] = ((dist_04_00 > dist_05_03) && (v[0] != v[5]) && (v[0] != v[3])) ? ((dominantGradient) ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;
	}

	// Pixel Tap Mapping: --|--|22|23|--
	//                    --|06|07|08|09
	//                    --|05|00|01|10
	//                    --|--|03|02|--
	//                    --|--|--|--|--
	// Corner (1, 0)
	if ( ((v[7] == v[8] && v[0] == v[1]) || (v[7] == v[0] && v[8] == v[1])) == false) {
		float dist_00_08 = DistYCbCr(src[ 5], src[ 7]) + DistYCbCr(src[ 7], src[23]) + DistYCbCr(src[ 3], src[ 1]) + DistYCbCr(src[ 1], src[ 9]) + (4.0 * DistYCbCr(src[ 0], src[ 8]));
		float dist_07_01 = DistYCbCr(src[ 6], src[ 0]) + DistYCbCr(src[ 0], src[ 2]) + DistYCbCr(src[22], src[ 8]) + DistYCbCr(src[ 8], src[10]) + (4.0 * DistYCbCr(src[ 7], src[ 1]));
		bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_07_01) < dist_00_08;
		blendResult[1] = ((dist_00_08 > dist_07_01) && (v[0] != v[7]) && (v[0] != v[1])) ? ((dominantGradient) ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;
	}

	// Pixel Tap Mapping: --|21|22|--|--
	//                    19|06|07|08|--
	//                    18|05|00|01|--
	//                    --|04|03|--|--
	//                    --|--|--|--|--
	// Corner (0, 0)
	if ( ((v[6] == v[7] && v[5] == v[0]) || (v[6] == v[5] && v[7] == v[0])) == false) {
		float dist_05_07 = DistYCbCr(src[18], src[ 6]) + DistYCbCr(src[ 6], src[22]) + DistYCbCr(src[ 4], src[ 0]) + DistYCbCr(src[ 0], src[ 8]) + (4.0 * DistYCbCr(src[ 5], src[ 7]));
		float dist_06_00 = DistYCbCr(src[19], src[ 5]) + DistYCbCr(src[ 5], src[ 3]) + DistYCbCr(src[21], src[ 7]) + DistYCbCr(src[ 7], src[ 1]) + (4.0 * DistYCbCr(src[ 6], src[ 0]));
		bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_05_07) < dist_06_00;
		blendResult[0] = ((dist_05_07 < dist_06_00) && (v[0] != v[5]) && (v[0] != v[7])) ? ((dominantGradient) ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;
	}

	vec4 dst[16];
	dst[ 0] = src[0];
	dst[ 1] = src[0];
	dst[ 2] = src[0];
	dst[ 3] = src[0];
	dst[ 4] = src[0];
	dst[ 5] = src[0];
	dst[ 6] = src[0];
	dst[ 7] = src[0];
	dst[ 8] = src[0];
	dst[ 9] = src[0];
	dst[10] = src[0];
	dst[11] = src[0];
	dst[12] = src[0];
	dst[13] = src[0];
	dst[14] = src[0];
	dst[15] = src[0];

	// Scale pixel
	if (IsBlendingNeeded(blendResult) == true) {
		float dist_01_04 = DistYCbCr(src[1], src[4]);
		float dist_03_08 = DistYCbCr(src[3], src[8]);
		bool haveShallowLine = (STEEP_DIRECTION_THRESHOLD * dist_01_04 <= dist_03_08) && (v[0] != v[4]) && (v[5] != v[4]);
		bool haveSteepLine   = (STEEP_DIRECTION_THRESHOLD * dist_03_08 <= dist_01_04) && (v[0] != v[8]) && (v[7] != v[8]);
		bool needBlend = (blendResult[2] != BLEND_NONE);
		bool doLineBlend = (  blendResult[2] >= BLEND_DOMINANT ||
			((blendResult[1] != BLEND_NONE && !IsPixEqual(src[0], src[4])) ||
			(blendResult[3] != BLEND_NONE && !IsPixEqual(src[0], src[8])) ||
			(IsPixEqual(src[4], src[3]) && IsPixEqual(src[3], src[2]) && IsPixEqual(src[2], src[1]) && IsPixEqual(src[1], src[8]) && IsPixEqual(src[0], src[2]) == false) ) == false );

		vec4 blendPix = ( DistYCbCr(src[0], src[1]) <= DistYCbCr(src[0], src[3]) ) ? src[1] : src[3];
		dst[ 2] = mix(dst[ 2], blendPix, (needBlend && doLineBlend) ? ((haveShallowLine) ? ((haveSteepLine) ? 1.0/3.0 : 0.25) : ((haveSteepLine) ? 0.25 : 0.00)) : 0.00);
		dst[ 9] = mix(dst[ 9], blendPix, (needBlend && doLineBlend && haveSteepLine) ? 0.25 : 0.00);
		dst[10] = mix(dst[10], blendPix, (needBlend && doLineBlend && haveSteepLine) ? 0.75 : 0.00);
		dst[11] = mix(dst[11], blendPix, (needBlend) ? ((doLineBlend) ? ((haveSteepLine) ? 1.00 : ((haveShallowLine) ? 0.75 : 0.50)) : 0.08677704501) : 0.00);
		dst[12] = mix(dst[12], blendPix, (needBlend) ? ((doLineBlend) ? 1.00 : 0.6848532563) : 0.00);
		dst[13] = mix(dst[13], blendPix, (needBlend) ? ((doLineBlend) ? ((haveShallowLine) ? 1.00 : ((haveSteepLine) ? 0.75 : 0.50)) : 0.08677704501) : 0.00);
		dst[14] = mix(dst[14], blendPix, (needBlend && doLineBlend && haveShallowLine) ? 0.75 : 0.00);
		dst[15] = mix(dst[15], blendPix, (needBlend && doLineBlend && haveShallowLine) ? 0.25 : 0.00);

		dist_01_04 = DistYCbCr(src[7], src[2]);
		dist_03_08 = DistYCbCr(src[1], src[6]);
		haveShallowLine = (STEEP_DIRECTION_THRESHOLD * dist_01_04 <= dist_03_08) && (v[0] != v[2]) && (v[3] != v[2]);
		haveSteepLine   = (STEEP_DIRECTION_THRESHOLD * dist_03_08 <= dist_01_04) && (v[0] != v[6]) && (v[5] != v[6]);
		needBlend = (blendResult[1] != BLEND_NONE);
		doLineBlend = (  blendResult[1] >= BLEND_DOMINANT ||
			!((blendResult[0] != BLEND_NONE && !IsPixEqual(src[0], src[2])) ||
			(blendResult[2] != BLEND_NONE && !IsPixEqual(src[0], src[6])) ||
			(IsPixEqual(src[2], src[1]) && IsPixEqual(src[1], src[8]) && IsPixEqual(src[8], src[7]) && IsPixEqual(src[7], src[6]) && !IsPixEqual(src[0], src[8])) ) );

		blendPix = ( DistYCbCr(src[0], src[7]) <= DistYCbCr(src[0], src[1]) ) ? src[7] : src[1];
		dst[ 1] = mix(dst[ 1], blendPix, (needBlend && doLineBlend) ? ((haveShallowLine) ? ((haveSteepLine) ? 1.0/3.0 : 0.25) : ((haveSteepLine) ? 0.25 : 0.00)) : 0.00);
		dst[ 6] = mix(dst[ 6], blendPix, (needBlend && doLineBlend && haveSteepLine) ? 0.25 : 0.00);
		dst[ 7] = mix(dst[ 7], blendPix, (needBlend && doLineBlend && haveSteepLine) ? 0.75 : 0.00);
		dst[ 8] = mix(dst[ 8], blendPix, (needBlend) ? ((doLineBlend) ? ((haveSteepLine) ? 1.00 : ((haveShallowLine) ? 0.75 : 0.50)) : 0.08677704501) : 0.00);
		dst[ 9] = mix(dst[ 9], blendPix, (needBlend) ? ((doLineBlend) ? 1.00 : 0.6848532563) : 0.00);
		dst[10] = mix(dst[10], blendPix, (needBlend) ? ((doLineBlend) ? ((haveShallowLine) ? 1.00 : ((haveSteepLine) ? 0.75 : 0.50)) : 0.08677704501) : 0.00);
		dst[11] = mix(dst[11], blendPix, (needBlend && doLineBlend && haveShallowLine) ? 0.75 : 0.00);
		dst[12] = mix(dst[12], blendPix, (needBlend && doLineBlend && haveShallowLine) ? 0.25 : 0.00);

		dist_01_04 = DistYCbCr(src[5], src[8]);
		dist_03_08 = DistYCbCr(src[7], src[4]);
		haveShallowLine = (STEEP_DIRECTION_THRESHOLD * dist_01_04 <= dist_03_08) && (v[0] != v[8]) && (v[1] != v[8]);
		haveSteepLine   = (STEEP_DIRECTION_THRESHOLD * dist_03_08 <= dist_01_04) && (v[0] != v[4]) && (v[3] != v[4]);
		needBlend = (blendResult[0] != BLEND_NONE);
		doLineBlend = (  blendResult[0] >= BLEND_DOMINANT ||
			!((blendResult[3] != BLEND_NONE && !IsPixEqual(src[0], src[8])) ||
			(blendResult[1] != BLEND_NONE && !IsPixEqual(src[0], src[4])) ||
			(IsPixEqual(src[8], src[7]) && IsPixEqual(src[7], src[6]) && IsPixEqual(src[6], src[5]) && IsPixEqual(src[5], src[4]) && !IsPixEqual(src[0], src[6])) ) );

		blendPix = ( DistYCbCr(src[0], src[5]) <= DistYCbCr(src[0], src[7]) ) ? src[5] : src[7];
		dst[ 0] = mix(dst[ 0], blendPix, (needBlend && doLineBlend) ? ((haveShallowLine) ? ((haveSteepLine) ? 1.0/3.0 : 0.25) : ((haveSteepLine) ? 0.25 : 0.00)) : 0.00);
		dst[15] = mix(dst[15], blendPix, (needBlend && doLineBlend && haveSteepLine) ? 0.25 : 0.00);
		dst[ 4] = mix(dst[ 4], blendPix, (needBlend && doLineBlend && haveSteepLine) ? 0.75 : 0.00);
		dst[ 5] = mix(dst[ 5], blendPix, (needBlend) ? ((doLineBlend) ? ((haveSteepLine) ? 1.00 : ((haveShallowLine) ? 0.75 : 0.50)) : 0.08677704501) : 0.00);
		dst[ 6] = mix(dst[ 6], blendPix, (needBlend) ? ((doLineBlend) ? 1.00 : 0.6848532563) : 0.00);
		dst[ 7] = mix(dst[ 7], blendPix, (needBlend) ? ((doLineBlend) ? ((haveShallowLine) ? 1.00 : ((haveSteepLine) ? 0.75 : 0.50)) : 0.08677704501) : 0.00);
		dst[ 8] = mix(dst[ 8], blendPix, (needBlend && doLineBlend && haveShallowLine) ? 0.75 : 0.00);
		dst[ 9] = mix(dst[ 9], blendPix, (needBlend && doLineBlend && haveShallowLine) ? 0.25 : 0.00);

		dist_01_04 = DistYCbCr(src[3], src[6]);
		dist_03_08 = DistYCbCr(src[5], src[2]);
		haveShallowLine = (STEEP_DIRECTION_THRESHOLD * dist_01_04 <= dist_03_08) && (v[0] != v[6]) && (v[7] != v[6]);
		haveSteepLine   = (STEEP_DIRECTION_THRESHOLD * dist_03_08 <= dist_01_04) && (v[0] != v[2]) && (v[1] != v[2]);
		needBlend = (blendResult[3] != BLEND_NONE);
		doLineBlend = (  blendResult[3] >= BLEND_DOMINANT ||
			!((blendResult[2] != BLEND_NONE && !IsPixEqual(src[0], src[6])) ||
			(blendResult[0] != BLEND_NONE && !IsPixEqual(src[0], src[2])) ||
			(IsPixEqual(src[6], src[5]) && IsPixEqual(src[5], src[4]) && IsPixEqual(src[4], src[3]) && IsPixEqual(src[3], src[2]) && !IsPixEqual(src[0], src[4])) ) );

		blendPix = ( DistYCbCr(src[0], src[3]) <= DistYCbCr(src[0], src[5]) ) ? src[3] : src[5];
		dst[ 3] = mix(dst[ 3], blendPix, (needBlend && doLineBlend) ? ((haveShallowLine) ? ((haveSteepLine) ? 1.0/3.0 : 0.25) : ((haveSteepLine) ? 0.25 : 0.00)) : 0.00);
		dst[12] = mix(dst[12], blendPix, (needBlend && doLineBlend && haveSteepLine) ? 0.25 : 0.00);
		dst[13] = mix(dst[13], blendPix, (needBlend && doLineBlend && haveSteepLine) ? 0.75 : 0.00);
		dst[14] = mix(dst[14], blendPix, (needBlend) ? ((doLineBlend) ? ((haveSteepLine) ? 1.00 : ((haveShallowLine) ? 0.75 : 0.50)) : 0.08677704501) : 0.00);
		dst[15] = mix(dst[15], blendPix, (needBlend) ? ((doLineBlend) ? 1.00 : 0.6848532563) : 0.00);
		dst[ 4] = mix(dst[ 4], blendPix, (needBlend) ? ((doLineBlend) ? ((haveShallowLine) ? 1.00 : ((haveSteepLine) ? 0.75 : 0.50)) : 0.08677704501) : 0.00);
		dst[ 5] = mix(dst[ 5], blendPix, (needBlend && doLineBlend && haveShallowLine) ? 0.75 : 0.00);
		dst[ 6] = mix(dst[ 6], blendPix, (needBlend && doLineBlend && haveShallowLine) ? 0.25 : 0.00);
	}

	// select output pixel
	vec4 res = mix(mix(mix(mix(dst[ 6], dst[ 7], step(0.25, f.x)),
					mix(dst[ 8], dst[ 9], step(0.75, f.x)),
					step(0.50, f.x)),
				mix(mix(dst[ 5], dst[ 0], step(0.25, f.x)),
					mix(dst[ 1], dst[10], step(0.75, f.x)),
					step(0.50, f.x)),
				step(0.25, f.y)),
		mix(mix(mix(dst[ 4], dst[ 3], step(0.25, f.x)),
					mix(dst[ 2], dst[11], step(0.75, f.x)),
					step(0.50, f.x)),
				mix(mix(dst[15], dst[14], step(0.25, f.x)),
					mix(dst[13], dst[12], step(0.75, f.x)),
					step(0.50, f.x)),
				step(0.75, f.y)),
		step(0.50, f.y));

	return postdivide_alpha(res);
}

uint applyScalingu(uvec2 origxy, uvec2 xy) {
	return packUnorm4x8(applyScalingf(origxy, xy));
}
)";

const char *copyShader = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

// No idea what's optimal here...
#define WORKGROUP_SIZE 16
layout (local_size_x = WORKGROUP_SIZE, local_size_y = WORKGROUP_SIZE, local_size_z = 1) in;

layout(std430, binding = 1) buffer Buf1 {
	uint data[];
} buf1;

layout(std430, binding = 2) buffer Buf2 {
	uint data[];
} buf2;

layout(push_constant) uniform Params {
	int width;
	int height;
	int scale;
	int fmt;
} params;

uint readColoru(uvec2 p) {
	// Note that if the pixels are packed, we can do multiple stores
	// and only launch this compute shader for every N pixels,
	// by slicing the width in half and multiplying x by 2, for example.
	if (params.fmt == 0) {
		return buf1.data[p.y * params.width + p.x];
	} else {
		uint offset = p.y * params.width + p.x;
		uint data = buf1.data[offset / 2];
		if ((offset & 1) != 0) {
			data = data >> 16;
		}
		if (params.fmt == 6) {
			uint r = ((data << 3) & 0xF8) | ((data >> 2) & 0x07);
			uint g = ((data >> 3) & 0xFC) | ((data >> 9) & 0x03);
			uint b = ((data >> 8) & 0xF8) | ((data >> 13) & 0x07);
			return 0xFF000000 | (b << 16) | (g << 8) | r;
		} else if (params.fmt == 5) {
			uint r = ((data << 3) & 0xF8) | ((data >> 2) & 0x07);
			uint g = ((data >> 2) & 0xF8) | ((data >> 7) & 0x07);
			uint b = ((data >> 7) & 0xF8) | ((data >> 12) & 0x07);
			uint a = ((data >> 15) & 0x01) == 0 ? 0x00 : 0xFF;
			return (a << 24) | (b << 16) | (g << 8) | r;
		} else if (params.fmt == 4) {
			uint r = (data & 0x0F) | ((data << 4) & 0x0F);
			uint g = (data & 0xF0) | ((data >> 4) & 0x0F);
			uint b = ((data >> 8) & 0x0F) | ((data >> 4) & 0xF0);
			uint a = ((data >> 12) & 0x0F) | ((data >> 8) & 0xF0);
			return (a << 24) | (b << 16) | (g << 8) | r;
		}
	}
}

vec4 readColorf(uvec2 p) {
	return unpackUnorm4x8(readColoru(p));
}

%s

void main() {
	uvec2 xy = gl_GlobalInvocationID.xy;
	// Kill off any out-of-image threads to avoid stray writes.
	// Should only happen on the tiniest mipmaps as PSP textures are power-of-2,
	// and we use a 16x16 workgroup size.
	if (xy.x >= params.width || xy.y >= params.height)
		return;

	uvec2 origxy = xy / params.scale;
	if (params.scale == 1) {
		buf2.data[xy.y * params.width + xy.x] = readColoru(origxy);
	} else {
		buf2.data[xy.y * params.width + xy.x] = applyScalingu(origxy, xy);
	}
}
)";

const char *uploadShader = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

// No idea what's optimal here...
#define WORKGROUP_SIZE 16
layout (local_size_x = WORKGROUP_SIZE, local_size_y = WORKGROUP_SIZE, local_size_z = 1) in;

uniform layout(binding = 0, rgba8) writeonly image2D img;

layout(std430, binding = 1) buffer Buf {
	uint data[];
} buf;

layout(push_constant) uniform Params {
	int width;
	int height;
	int scale;
	int fmt;
} params;

uint readColoru(uvec2 p) {
	// Note that if the pixels are packed, we can do multiple stores
	// and only launch this compute shader for every N pixels,
	// by slicing the width in half and multiplying x by 2, for example.
	if (params.fmt == 0) {
		return buf.data[p.y * params.width + p.x];
	} else {
		uint offset = p.y * params.width + p.x;
		uint data = buf.data[offset / 2];
		if ((offset & 1) != 0) {
			data = data >> 16;
		}
		if (params.fmt == 6) {
			uint r = ((data << 3) & 0xF8) | ((data >> 2) & 0x07);
			uint g = ((data >> 3) & 0xFC) | ((data >> 9) & 0x03);
			uint b = ((data >> 8) & 0xF8) | ((data >> 13) & 0x07);
			return 0xFF000000 | (b << 16) | (g << 8) | r;
		} else if (params.fmt == 5) {
			uint r = ((data << 3) & 0xF8) | ((data >> 2) & 0x07);
			uint g = ((data >> 2) & 0xF8) | ((data >> 7) & 0x07);
			uint b = ((data >> 7) & 0xF8) | ((data >> 12) & 0x07);
			uint a = ((data >> 15) & 0x01) == 0 ? 0x00 : 0xFF;
			return (a << 24) | (b << 16) | (g << 8) | r;
		} else if (params.fmt == 4) {
			uint r = (data & 0x0F) | ((data << 4) & 0x0F);
			uint g = (data & 0xF0) | ((data >> 4) & 0x0F);
			uint b = ((data >> 8) & 0x0F) | ((data >> 4) & 0xF0);
			uint a = ((data >> 12) & 0x0F) | ((data >> 8) & 0xF0);
			return (a << 24) | (b << 16) | (g << 8) | r;
		}
	}
}

vec4 readColorf(uvec2 p) {
	// Unpack the color (we could look it up in a CLUT here if we wanted...)
	// It's a bit silly that we need to unpack to float and then have imageStore repack,
	// but the alternative is to store to a buffer, and then launch a vkCmdCopyBufferToImage instead.
	return unpackUnorm4x8(readColoru(p));
}

%s

void main() {
	uvec2 xy = gl_GlobalInvocationID.xy;
	// Kill off any out-of-image threads to avoid stray writes.
	// Should only happen on the tiniest mipmaps as PSP textures are power-of-2,
	// and we use a 16x16 workgroup size.
	if (xy.x >= params.width || xy.y >= params.height)
		return;

	uvec2 origxy = xy / params.scale;
	if (params.scale == 1) {
		imageStore(img, ivec2(xy.x, xy.y), readColorf(origxy));
	} else {
		imageStore(img, ivec2(xy.x, xy.y), applyScalingf(origxy, xy));
	}
}
)";

SamplerCache::~SamplerCache() {
	DeviceLost();
}

VkSampler SamplerCache::GetOrCreateSampler(const SamplerCacheKey &key) {
	VkSampler sampler = cache_.Get(key);
	if (sampler != VK_NULL_HANDLE)
		return sampler;

	VkSamplerCreateInfo samp = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	samp.addressModeU = key.sClamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samp.addressModeV = key.tClamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samp.addressModeW = samp.addressModeU;  // irrelevant, but Mali recommends that all clamp modes are the same if possible.
	samp.compareOp = VK_COMPARE_OP_ALWAYS;
	samp.flags = 0;
	samp.magFilter = key.magFilt ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	samp.minFilter = key.minFilt ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	samp.mipmapMode = key.mipFilt ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
	if (key.aniso) {
		// Docs say the min of this value and the supported max are used.
		samp.maxAnisotropy = 1 << g_Config.iAnisotropyLevel;
		samp.anisotropyEnable = true;
	} else {
		samp.maxAnisotropy = 1.0f;
		samp.anisotropyEnable = false;
	}
	samp.maxLod = (float)(int32_t)key.maxLevel * (1.0f / 256.0f);
	samp.minLod = (float)(int32_t)key.minLevel * (1.0f / 256.0f);
	samp.mipLodBias = (float)(int32_t)key.lodBias * (1.0f / 256.0f);

	VkResult res = vkCreateSampler(vulkan_->GetDevice(), &samp, nullptr, &sampler);
	assert(res == VK_SUCCESS);
	cache_.Insert(key, sampler);
	return sampler;
}

std::string SamplerCache::DebugGetSamplerString(std::string id, DebugShaderStringType stringType) {
	SamplerCacheKey key;
	key.FromString(id);
	return StringFromFormat("%s/%s mag:%s min:%s mip:%s maxLod:%f minLod:%f bias:%f",
		key.sClamp ? "Clamp" : "Wrap",
		key.tClamp ? "Clamp" : "Wrap",
		key.magFilt ? "Linear" : "Nearest",
		key.minFilt ? "Linear" : "Nearest",
		key.mipFilt ? "Linear" : "Nearest",
		key.maxLevel / 256.0f,
		key.minLevel / 256.0f,
		key.lodBias / 256.0f);
}

void SamplerCache::DeviceLost() {
	cache_.Iterate([&](const SamplerCacheKey &key, VkSampler sampler) {
		vulkan_->Delete().QueueDeleteSampler(sampler);
	});
	cache_.Clear();
}

void SamplerCache::DeviceRestore(VulkanContext *vulkan) {
	vulkan_ = vulkan;
}

std::vector<std::string> SamplerCache::DebugGetSamplerIDs() const {
	std::vector<std::string> ids;
	cache_.Iterate([&](const SamplerCacheKey &id, VkSampler sampler) {
		std::string idstr;
		id.ToString(&idstr);
		ids.push_back(idstr);
	});
	return ids;
}

TextureCacheVulkan::TextureCacheVulkan(Draw::DrawContext *draw, VulkanContext *vulkan)
	: TextureCacheCommon(draw),
		vulkan_(vulkan),
		samplerCache_(vulkan),
		computeShaderManager_(vulkan) {
	timesInvalidatedAllThisFrame_ = 0;
	DeviceRestore(vulkan, draw);
	SetupTextureDecoder();
}

TextureCacheVulkan::~TextureCacheVulkan() {
	DeviceLost();
}

void TextureCacheVulkan::SetFramebufferManager(FramebufferManagerVulkan *fbManager) {
	framebufferManagerVulkan_ = fbManager;
	framebufferManager_ = fbManager;
}

void TextureCacheVulkan::SetVulkan2D(Vulkan2D *vk2d) {
	vulkan2D_ = vk2d;
	depalShaderCache_->SetVulkan2D(vk2d);
}

void TextureCacheVulkan::DeviceLost() {
	Clear(true);

	if (allocator_) {
		allocator_->Destroy();

		// We have to delete on queue, so this can free its queued deletions.
		vulkan_->Delete().QueueCallback([](void *ptr) {
			auto allocator = static_cast<VulkanDeviceAllocator *>(ptr);
			delete allocator;
		}, allocator_);
		allocator_ = nullptr;
	}

	samplerCache_.DeviceLost();

	if (samplerNearest_)
		vulkan_->Delete().QueueDeleteSampler(samplerNearest_);

	if (uploadCS_ != VK_NULL_HANDLE)
		vulkan_->Delete().QueueDeleteShaderModule(uploadCS_);
	if (copyCS_ != VK_NULL_HANDLE)
		vulkan_->Delete().QueueDeleteShaderModule(copyCS_);

	computeShaderManager_.DeviceLost();

	nextTexture_ = nullptr;
}

void TextureCacheVulkan::DeviceRestore(VulkanContext *vulkan, Draw::DrawContext *draw) {
	vulkan_ = vulkan;
	draw_ = draw;

	assert(!allocator_);

	allocator_ = new VulkanDeviceAllocator(vulkan_, TEXCACHE_MIN_SLAB_SIZE, TEXCACHE_MAX_SLAB_SIZE);
	samplerCache_.DeviceRestore(vulkan);

	VkSamplerCreateInfo samp{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samp.magFilter = VK_FILTER_NEAREST;
	samp.minFilter = VK_FILTER_NEAREST;
	samp.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	vkCreateSampler(vulkan_->GetDevice(), &samp, nullptr, &samplerNearest_);

	std::string error;
	std::string fullUploadShader = StringFromFormat(uploadShader, shader4xbrz);
	std::string fullCopyShader = StringFromFormat(copyShader, shader4xbrz);

	if (g_Config.bTexHardwareScaling) {
		uploadCS_ = CompileShaderModule(vulkan_, VK_SHADER_STAGE_COMPUTE_BIT, fullUploadShader.c_str(), &error);
		_dbg_assert_msg_(G3D, uploadCS_ != VK_NULL_HANDLE, "failed to compile upload shader");
		copyCS_ = CompileShaderModule(vulkan_, VK_SHADER_STAGE_COMPUTE_BIT, fullCopyShader.c_str(), &error);
		_dbg_assert_msg_(G3D, copyCS_!= VK_NULL_HANDLE, "failed to compile copy shader");
	}

	computeShaderManager_.DeviceRestore(vulkan);
}

void TextureCacheVulkan::ReleaseTexture(TexCacheEntry *entry, bool delete_them) {
	DEBUG_LOG(G3D, "Deleting texture %p", entry->vkTex);
	delete entry->vkTex;
	entry->vkTex = nullptr;
}

VkFormat getClutDestFormatVulkan(GEPaletteFormat format) {
	switch (format) {
	case GE_CMODE_16BIT_ABGR4444:
		return VULKAN_4444_FORMAT;
	case GE_CMODE_16BIT_ABGR5551:
		return VULKAN_1555_FORMAT;
	case GE_CMODE_16BIT_BGR5650:
		return VULKAN_565_FORMAT;
	case GE_CMODE_32BIT_ABGR8888:
		return VULKAN_8888_FORMAT;
	}
	return VK_FORMAT_UNDEFINED;
}

static const VkFilter MagFiltVK[2] = {
	VK_FILTER_NEAREST,
	VK_FILTER_LINEAR
};

void TextureCacheVulkan::SetFramebufferSamplingParams(u16 bufferWidth, u16 bufferHeight, SamplerCacheKey &key) {
	int minFilt;
	int magFilt;
	bool sClamp;
	bool tClamp;
	float lodBias;
	GETexLevelMode mode;
	GetSamplingParams(minFilt, magFilt, sClamp, tClamp, lodBias, 0, 0, mode);

	key.minFilt = minFilt & 1;
	key.mipFilt = 0;
	key.magFilt = magFilt & 1;
	key.sClamp = sClamp;
	key.tClamp = tClamp;

	// Often the framebuffer will not match the texture size.  We'll wrap/clamp in the shader in that case.
	// This happens whether we have OES_texture_npot or not.
	int w = gstate.getTextureWidth(0);
	int h = gstate.getTextureHeight(0);
	if (w != bufferWidth || h != bufferHeight) {
		key.sClamp = true;
		key.tClamp = true;
	}
}

void TextureCacheVulkan::StartFrame() {
	InvalidateLastTexture();
	depalShaderCache_->Decimate();

	timesInvalidatedAllThisFrame_ = 0;
	texelsScaledThisFrame_ = 0;

	if (clearCacheNextFrame_) {
		Clear(true);
		clearCacheNextFrame_ = false;
	} else {
		int slabPressureLimit = TEXCACHE_SLAB_PRESSURE;
		if (g_Config.iTexScalingLevel > 1) {
			// Since textures are 2D maybe we should square this, but might get too non-aggressive.
			slabPressureLimit *= g_Config.iTexScalingLevel;
		}
		Decimate(allocator_->GetSlabCount() > slabPressureLimit);
	}

	allocator_->Begin();
	computeShaderManager_.BeginFrame();
}

void TextureCacheVulkan::EndFrame() {
	allocator_->End();
	computeShaderManager_.EndFrame();

	if (texelsScaledThisFrame_) {
		// INFO_LOG(G3D, "Scaled %i texels", texelsScaledThisFrame_);
	}
}

void TextureCacheVulkan::UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple) {
	const u32 clutBaseBytes = clutFormat == GE_CMODE_32BIT_ABGR8888 ? (clutBase * sizeof(u32)) : (clutBase * sizeof(u16));
	// Technically, these extra bytes weren't loaded, but hopefully it was loaded earlier.
	// If not, we're going to hash random data, which hopefully doesn't cause a performance issue.
	//
	// TODO: Actually, this seems like a hack.  The game can upload part of a CLUT and reference other data.
	// clutTotalBytes_ is the last amount uploaded.  We should hash clutMaxBytes_, but this will often hash
	// unrelated old entries for small palettes.
	// Adding clutBaseBytes may just be mitigating this for some usage patterns.
	const u32 clutExtendedBytes = std::min(clutTotalBytes_ + clutBaseBytes, clutMaxBytes_);

	clutHash_ = DoReliableHash32((const char *)clutBufRaw_, clutExtendedBytes, 0xC0108888);
	clutBuf_ = clutBufRaw_;

	// Special optimization: fonts typically draw clut4 with just alpha values in a single color.
	clutAlphaLinear_ = false;
	clutAlphaLinearColor_ = 0;
	if (clutFormat == GE_CMODE_16BIT_ABGR4444 && clutIndexIsSimple) {
		const u16_le *clut = GetCurrentClut<u16_le>();
		clutAlphaLinear_ = true;
		clutAlphaLinearColor_ = clut[15] & 0x0FFF;
		for (int i = 0; i < 16; ++i) {
			u16 step = clutAlphaLinearColor_ | (i << 12);
			if (clut[i] != step) {
				clutAlphaLinear_ = false;
				break;
			}
		}
	}

	clutLastFormat_ = gstate.clutformat;
}

void TextureCacheVulkan::BindTexture(TexCacheEntry *entry) {
	if (!entry || !entry->vkTex) {
		imageView_ = VK_NULL_HANDLE;
		curSampler_ = VK_NULL_HANDLE;
		return;
	}

	entry->vkTex->Touch();
	imageView_ = entry->vkTex->GetImageView();
	SamplerCacheKey key{};
	UpdateSamplingParams(*entry, key);
	curSampler_ = samplerCache_.GetOrCreateSampler(key);
	drawEngine_->SetDepalTexture(VK_NULL_HANDLE);
	gstate_c.SetUseShaderDepal(false);
}

void TextureCacheVulkan::Unbind() {
	imageView_ = VK_NULL_HANDLE;
	curSampler_ = VK_NULL_HANDLE;
	InvalidateLastTexture();
}

void TextureCacheVulkan::ApplyTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer) {
	SamplerCacheKey samplerKey{};
	SetFramebufferSamplingParams(framebuffer->bufferWidth, framebuffer->bufferHeight, samplerKey);

	DepalShaderVulkan *depalShader = nullptr;
	uint32_t clutMode = gstate.clutformat & 0xFFFFFF;

	bool useShaderDepal = framebufferManager_->GetCurrentRenderVFB() != framebuffer;
	bool expand32 = !gstate_c.Supports(GPU_SUPPORTS_16BIT_FORMATS);

	if ((entry->status & TexCacheEntry::STATUS_DEPALETTIZE) && !g_Config.bDisableSlowFramebufEffects) {
		if (useShaderDepal) {
			depalShaderCache_->SetPushBuffer(drawEngine_->GetPushBufferForTextureData());
			const GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
			VulkanTexture *clutTexture = depalShaderCache_->GetClutTexture(clutFormat, clutHash_, clutBuf_, expand32);
			drawEngine_->SetDepalTexture(clutTexture ? clutTexture->GetImageView() : VK_NULL_HANDLE);
			// Only point filtering enabled.
			samplerKey.magFilt = false;
			samplerKey.minFilt = false;
			samplerKey.mipFilt = false;
			// Make sure to update the uniforms, and also texture - needs a recheck.
			gstate_c.Dirty(DIRTY_DEPAL);
			gstate_c.SetUseShaderDepal(true);
			gstate_c.depalFramebufferFormat = framebuffer->drawnFormat;
			const u32 bytesPerColor = clutFormat == GE_CMODE_32BIT_ABGR8888 ? sizeof(u32) : sizeof(u16);
			const u32 clutTotalColors = clutMaxBytes_ / bytesPerColor;
			TexCacheEntry::TexStatus alphaStatus = CheckAlpha(clutBuf_, getClutDestFormatVulkan(clutFormat), clutTotalColors, clutTotalColors, 1);
			gstate_c.SetTextureFullAlpha(alphaStatus == TexCacheEntry::STATUS_ALPHA_FULL);
			curSampler_ = samplerCache_.GetOrCreateSampler(samplerKey);
			InvalidateLastTexture(entry);
			imageView_ = framebufferManagerVulkan_->BindFramebufferAsColorTexture(0, framebuffer, BINDFBCOLOR_MAY_COPY_WITH_UV | BINDFBCOLOR_APPLY_TEX_OFFSET);
			return;
		} else {
			depalShader = depalShaderCache_->GetDepalettizeShader(clutMode, framebuffer->drawnFormat);
			drawEngine_->SetDepalTexture(VK_NULL_HANDLE);
			gstate_c.SetUseShaderDepal(false);
		}
	}
	if (depalShader) {
		depalShaderCache_->SetPushBuffer(drawEngine_->GetPushBufferForTextureData());
		const GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
		VulkanTexture *clutTexture = depalShaderCache_->GetClutTexture(clutFormat, clutHash_, clutBuf_, expand32);

		Draw::Framebuffer *depalFBO = framebufferManager_->GetTempFBO(TempFBO::DEPAL, framebuffer->renderWidth, framebuffer->renderHeight, Draw::FBO_8888);
		draw_->BindFramebufferAsRenderTarget(depalFBO, { Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE });

		Vulkan2D::Vertex verts[4] = {
			{ -1, -1, 0.0f, 0, 0 },
			{  1, -1, 0.0f, 1, 0 },
			{ -1,  1, 0.0f, 0, 1 },
			{  1,  1, 0.0f, 1, 1 },
		};

		// If min is not < max, then we don't have values (wasn't set during decode.)
		if (gstate_c.vertBounds.minV < gstate_c.vertBounds.maxV) {
			const float invWidth = 1.0f / (float)framebuffer->bufferWidth;
			const float invHeight = 1.0f / (float)framebuffer->bufferHeight;
			// Inverse of half = double.
			const float invHalfWidth = invWidth * 2.0f;
			const float invHalfHeight = invHeight * 2.0f;

			const int u1 = gstate_c.vertBounds.minU + gstate_c.curTextureXOffset;
			const int v1 = gstate_c.vertBounds.minV + gstate_c.curTextureYOffset;
			const int u2 = gstate_c.vertBounds.maxU + gstate_c.curTextureXOffset;
			const int v2 = gstate_c.vertBounds.maxV + gstate_c.curTextureYOffset;

			const float left = u1 * invHalfWidth - 1.0f;
			const float right = u2 * invHalfWidth - 1.0f;
			const float top = v1 * invHalfHeight - 1.0f;
			const float bottom = v2 * invHalfHeight - 1.0f;
			// Points are: BL, BR, TR, TL.
			verts[0].x = left;
			verts[0].y = bottom;
			verts[1].x = right;
			verts[1].y = bottom;
			verts[2].x = left;
			verts[2].y = top;
			verts[3].x = right;
			verts[3].y = top;

			// And also the UVs, same order.
			const float uvleft = u1 * invWidth;
			const float uvright = u2 * invWidth;
			const float uvtop = v1 * invHeight;
			const float uvbottom = v2 * invHeight;
			verts[0].u = uvleft;
			verts[0].v = uvbottom;
			verts[1].u = uvright;
			verts[1].v = uvbottom;
			verts[2].u = uvleft;
			verts[2].v = uvtop;
			verts[3].u = uvright;
			verts[3].v = uvtop;

			// We need to reapply the texture next time since we cropped UV.
			gstate_c.Dirty(DIRTY_TEXTURE_PARAMS);
		}

		VkBuffer pushed;
		uint32_t offset = push_->PushAligned(verts, sizeof(verts), 4, &pushed);

		draw_->BindFramebufferAsTexture(framebuffer->fbo, 0, Draw::FB_COLOR_BIT, 0);
		VkImageView fbo = (VkImageView)draw_->GetNativeObject(Draw::NativeObject::BOUND_TEXTURE0_IMAGEVIEW);

		VkDescriptorSet descSet = vulkan2D_->GetDescriptorSet(fbo, samplerNearest_, clutTexture->GetImageView(), samplerNearest_);
		VulkanRenderManager *renderManager = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
		renderManager->BindPipeline(depalShader->pipeline);
		renderManager->SetScissor(VkRect2D{ {0, 0}, { framebuffer->renderWidth, framebuffer->renderHeight} });
		renderManager->SetViewport(VkViewport{ 0.f, 0.f, (float)framebuffer->renderWidth, (float)framebuffer->renderHeight, 0.f, 1.f });
		renderManager->Draw(vulkan2D_->GetPipelineLayout(), descSet, 0, nullptr, pushed, offset, 4);
		shaderManagerVulkan_->DirtyLastShader();

		const u32 bytesPerColor = clutFormat == GE_CMODE_32BIT_ABGR8888 ? sizeof(u32) : sizeof(u16);
		const u32 clutTotalColors = clutMaxBytes_ / bytesPerColor;

		TexCacheEntry::TexStatus alphaStatus = CheckAlpha(clutBuf_, getClutDestFormatVulkan(clutFormat), clutTotalColors, clutTotalColors, 1);
		gstate_c.SetTextureFullAlpha(alphaStatus == TexCacheEntry::STATUS_ALPHA_FULL);

		framebufferManager_->RebindFramebuffer();
		draw_->BindFramebufferAsTexture(depalFBO, 0, Draw::FB_COLOR_BIT, 0);
		imageView_ = (VkImageView)draw_->GetNativeObject(Draw::NativeObject::BOUND_TEXTURE0_IMAGEVIEW);

		// Need to rebind the pipeline since we switched it.
		drawEngine_->DirtyPipeline();
		// Since we may have switched render targets, we need to re-set depth/stencil etc states.
		gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_BLEND_STATE | DIRTY_RASTER_STATE);
	} else {
		entry->status &= ~TexCacheEntry::STATUS_DEPALETTIZE;

		framebufferManager_->RebindFramebuffer();  // TODO: This line should usually not be needed.
		imageView_ = framebufferManagerVulkan_->BindFramebufferAsColorTexture(0, framebuffer, BINDFBCOLOR_MAY_COPY_WITH_UV | BINDFBCOLOR_APPLY_TEX_OFFSET);
		drawEngine_->SetDepalTexture(VK_NULL_HANDLE);
		gstate_c.SetUseShaderDepal(false);

		gstate_c.SetTextureFullAlpha(gstate.getTextureFormat() == GE_TFMT_5650);
	}
	curSampler_ = samplerCache_.GetOrCreateSampler(samplerKey);
	InvalidateLastTexture(entry);
}

ReplacedTextureFormat FromVulkanFormat(VkFormat fmt) {
	switch (fmt) {
	case VULKAN_565_FORMAT: return ReplacedTextureFormat::F_5650;
	case VULKAN_1555_FORMAT: return ReplacedTextureFormat::F_5551;
	case VULKAN_4444_FORMAT: return ReplacedTextureFormat::F_4444;
	case VULKAN_8888_FORMAT: default: return ReplacedTextureFormat::F_8888;
	}
}

VkFormat ToVulkanFormat(ReplacedTextureFormat fmt) {
	switch (fmt) {
	case ReplacedTextureFormat::F_5650: return VULKAN_565_FORMAT;
	case ReplacedTextureFormat::F_5551: return VULKAN_1555_FORMAT;
	case ReplacedTextureFormat::F_4444: return VULKAN_4444_FORMAT;
	case ReplacedTextureFormat::F_8888: default: return VULKAN_8888_FORMAT;
	}
}

void TextureCacheVulkan::BuildTexture(TexCacheEntry *const entry) {
	entry->status &= ~TexCacheEntry::STATUS_ALPHA_MASK;

	VkCommandBuffer cmdInit = (VkCommandBuffer)draw_->GetNativeObject(Draw::NativeObject::INIT_COMMANDBUFFER);
	// For the estimate, we assume cluts always point to 8888 for simplicity.
	cacheSizeEstimate_ += EstimateTexMemoryUsage(entry);

	if (entry->framebuffer) {
		// Nothing else to do here.
		return;
	}

	if ((entry->bufw == 0 || (gstate.texbufwidth[0] & 0xf800) != 0) && entry->addr >= PSP_GetKernelMemoryEnd()) {
		ERROR_LOG_REPORT(G3D, "Texture with unexpected bufw (full=%d)", gstate.texbufwidth[0] & 0xffff);
		// Proceeding here can cause a crash.
		return;
	}

	// Adjust maxLevel to actually present levels..
	bool badMipSizes = false;
	int maxLevel = entry->maxLevel;
	for (int i = 0; i <= maxLevel; i++) {
		// If encountering levels pointing to nothing, adjust max level.
		u32 levelTexaddr = gstate.getTextureAddress(i);
		if (!Memory::IsValidAddress(levelTexaddr)) {
			maxLevel = i - 1;
			break;
		}

		// If size reaches 1, stop, and override maxlevel.
		int tw = gstate.getTextureWidth(i);
		int th = gstate.getTextureHeight(i);
		if (tw == 1 || th == 1) {
			maxLevel = i;
			break;
		}

		if (i > 0 && gstate_c.Supports(GPU_SUPPORTS_TEXTURE_LOD_CONTROL)) {
			if (tw != 1 && tw != (gstate.getTextureWidth(i - 1) >> 1))
				badMipSizes = true;
			else if (th != 1 && th != (gstate.getTextureHeight(i - 1) >> 1))
				badMipSizes = true;
		}
	}

	// In addition, simply don't load more than level 0 if g_Config.bMipMap is false.
	if (badMipSizes) {
		maxLevel = 0;
	}

	// If GLES3 is available, we can preallocate the storage, which makes texture loading more efficient.
	VkFormat dstFmt = GetDestFormat(GETextureFormat(entry->format), gstate.getClutPaletteFormat());

	int scaleFactor = standardScaleFactor_;

	// Rachet down scale factor in low-memory mode.
	if (lowMemoryMode_) {
		// Keep it even, though, just in case of npot troubles.
		scaleFactor = scaleFactor > 4 ? 4 : (scaleFactor > 2 ? 2 : 1);
	}

	u64 cachekey = replacer_.Enabled() ? entry->CacheKey() : 0;
	int w = gstate.getTextureWidth(0);
	int h = gstate.getTextureHeight(0);
	ReplacedTexture &replaced = replacer_.FindReplacement(cachekey, entry->fullhash, w, h);
	if (replaced.GetSize(0, w, h)) {
		// We're replacing, so we won't scale.
		scaleFactor = 1;
		entry->status |= TexCacheEntry::STATUS_IS_SCALED;
		maxLevel = replaced.MaxLevel();
		badMipSizes = false;
	}

	// Don't scale the PPGe texture.
	if (entry->addr > 0x05000000 && entry->addr < PSP_GetKernelMemoryEnd())
		scaleFactor = 1;
	if ((entry->status & TexCacheEntry::STATUS_CHANGE_FREQUENT) != 0 && scaleFactor != 1 && !g_Config.bTexHardwareScaling) {
		// Remember for later that we /wanted/ to scale this texture.
		entry->status |= TexCacheEntry::STATUS_TO_SCALE;
		scaleFactor = 1;
	}

	if (scaleFactor != 1) {
		if (texelsScaledThisFrame_ >= TEXCACHE_MAX_TEXELS_SCALED && !g_Config.bTexHardwareScaling) {
			entry->status |= TexCacheEntry::STATUS_TO_SCALE;
			scaleFactor = 1;
		} else {
			entry->status &= ~TexCacheEntry::STATUS_TO_SCALE;
			entry->status |= TexCacheEntry::STATUS_IS_SCALED;
			texelsScaledThisFrame_ += w * h;
		}
	}

	// TODO
	if (scaleFactor > 1) {
		maxLevel = 0;
	}

	VkFormat actualFmt = scaleFactor > 1 ? VULKAN_8888_FORMAT : dstFmt;
	if (replaced.Valid()) {
		actualFmt = ToVulkanFormat(replaced.Format(0));
	}

	bool computeUpload = false;
	bool computeCopy = false;

	{
		delete entry->vkTex;
		entry->vkTex = new VulkanTexture(vulkan_);
		VulkanTexture *image = entry->vkTex;

		const VkComponentMapping *mapping;
		switch (actualFmt) {
		case VULKAN_4444_FORMAT:
			mapping = &VULKAN_4444_SWIZZLE;
			break;

		case VULKAN_1555_FORMAT:
			mapping = &VULKAN_1555_SWIZZLE;
			break;

		case VULKAN_565_FORMAT:
			mapping = &VULKAN_565_SWIZZLE;
			break;

		default:
			mapping = &VULKAN_8888_SWIZZLE;
			break;
		}

		VkImageLayout imageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		// If we want to use the GE debugger, we should add VK_IMAGE_USAGE_TRANSFER_SRC_BIT too...

		// Compute experiment
		if (actualFmt == VULKAN_8888_FORMAT && scaleFactor > 1 && g_Config.bTexHardwareScaling) {
			// Enable the experiment you want.
			if (uploadCS_ != VK_NULL_HANDLE)
				computeUpload = true;
			else if (copyCS_ != VK_NULL_HANDLE)
				computeCopy = true;
		}

		if (computeUpload) {
			usage |= VK_IMAGE_USAGE_STORAGE_BIT;
			imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		}

		char texName[128]{};
		snprintf(texName, sizeof(texName), "Texture%08x", entry->addr);
		image->SetTag(texName);

		bool allocSuccess = image->CreateDirect(cmdInit, allocator_, w * scaleFactor, h * scaleFactor, maxLevel + 1, actualFmt, imageLayout, usage, mapping);
		if (!allocSuccess && !lowMemoryMode_) {
			WARN_LOG_REPORT(G3D, "Texture cache ran out of GPU memory; switching to low memory mode");
			lowMemoryMode_ = true;
			decimationCounter_ = 0;
			Decimate();
			// TODO: We should stall the GPU here and wipe things out of memory.
			// As is, it will almost definitely fail the second time, but next frame it may recover.

			auto err = GetI18NCategory("Error");
			if (scaleFactor > 1) {
				host->NotifyUserMessage(err->T("Warning: Video memory FULL, reducing upscaling and switching to slow caching mode"), 2.0f);
			} else {
				host->NotifyUserMessage(err->T("Warning: Video memory FULL, switching to slow caching mode"), 2.0f);
			}

			scaleFactor = 1;
			actualFmt = dstFmt;

			allocSuccess = image->CreateDirect(cmdInit, allocator_, w * scaleFactor, h * scaleFactor, maxLevel + 1, actualFmt, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, mapping);
		}

		if (!allocSuccess) {
			ERROR_LOG(G3D, "Failed to create texture (%dx%d)", w, h);
			delete entry->vkTex;
			entry->vkTex = nullptr;
		}
	}
	lastBoundTexture = entry->vkTex;

	ReplacedTextureDecodeInfo replacedInfo;
	if (replacer_.Enabled() && !replaced.Valid()) {
		replacedInfo.cachekey = cachekey;
		replacedInfo.hash = entry->fullhash;
		replacedInfo.addr = entry->addr;
		replacedInfo.isVideo = videos_.find(entry->addr & 0x3FFFFFFF) != videos_.end();
		replacedInfo.isFinal = (entry->status & TexCacheEntry::STATUS_TO_SCALE) == 0;
		replacedInfo.scaleFactor = scaleFactor;
		replacedInfo.fmt = FromVulkanFormat(actualFmt);
	}

	if (entry->vkTex) {
		// NOTE: Since the level is not part of the cache key, we assume it never changes.
		u8 level = std::max(0, gstate.getTexLevelOffset16() / 16);
		bool fakeMipmap = IsFakeMipmapChange() && level > 0;
		// Upload the texture data.
		for (int i = 0; i <= maxLevel; i++) {
			int mipWidth = gstate.getTextureWidth(i) * scaleFactor;
			int mipHeight = gstate.getTextureHeight(i) * scaleFactor;
			if (replaced.Valid()) {
				replaced.GetSize(i, mipWidth, mipHeight);
			}
			int srcBpp = dstFmt == VULKAN_8888_FORMAT ? 4 : 2;
			int srcStride = mipWidth * srcBpp;
			int srcSize = srcStride * mipHeight;
			int bpp = actualFmt == VULKAN_8888_FORMAT ? 4 : 2;
			int stride = (mipWidth * bpp + 15) & ~15;
			int size = stride * mipHeight;
			uint32_t bufferOffset;
			VkBuffer texBuf;
			// nvidia returns 1 but that can't be healthy... let's align by 16 as a minimum.
			int pushAlignment = std::max(16, (int)vulkan_->GetPhysicalDeviceProperties().properties.limits.optimalBufferCopyOffsetAlignment);
			void *data;
			bool dataScaled = true;
			if (replaced.Valid()) {
				data = drawEngine_->GetPushBufferForTextureData()->PushAligned(size, &bufferOffset, &texBuf, pushAlignment);
				replaced.Load(i, data, stride);
				entry->vkTex->UploadMip(cmdInit, i, mipWidth, mipHeight, texBuf, bufferOffset, stride / bpp);
			} else {
				auto dispatchCompute = [&](VkDescriptorSet descSet) {
					struct Params { int x; int y; int s; int fmt; } params{ mipWidth, mipHeight, scaleFactor, 0 };
					if (dstFmt == VULKAN_4444_FORMAT) {
						params.fmt = 4;
					} else if (dstFmt == VULKAN_1555_FORMAT) {
						params.fmt = 5;
					} else if (dstFmt == VULKAN_565_FORMAT) {
						params.fmt = 6;
					}
					vkCmdBindDescriptorSets(cmdInit, VK_PIPELINE_BIND_POINT_COMPUTE, computeShaderManager_.GetPipelineLayout(), 0, 1, &descSet, 0, nullptr);
					vkCmdPushConstants(cmdInit, computeShaderManager_.GetPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);
					vkCmdDispatch(cmdInit, (mipWidth + 15) / 16, (mipHeight + 15) / 16, 1);
				};

				if (fakeMipmap) {
					data = drawEngine_->GetPushBufferForTextureData()->PushAligned(size, &bufferOffset, &texBuf, pushAlignment);
					LoadTextureLevel(*entry, (uint8_t *)data, stride, level, scaleFactor, dstFmt);
					entry->vkTex->UploadMip(cmdInit, 0, mipWidth, mipHeight, texBuf, bufferOffset, stride / bpp);
					break;
				} else {
					if (computeUpload) {
						data = drawEngine_->GetPushBufferForTextureData()->PushAligned(srcSize, &bufferOffset, &texBuf, pushAlignment);
						dataScaled = false;
						LoadTextureLevel(*entry, (uint8_t *)data, srcStride, i, 1, dstFmt);
						// This format can be used with storage images.
						VkImageView view = entry->vkTex->CreateViewForMip(i);
						VkDescriptorSet descSet = computeShaderManager_.GetDescriptorSet(view, texBuf, bufferOffset, srcSize);
						vkCmdBindPipeline(cmdInit, VK_PIPELINE_BIND_POINT_COMPUTE, computeShaderManager_.GetPipeline(uploadCS_));
						dispatchCompute(descSet);
						vulkan_->Delete().QueueDeleteImageView(view);
					} else if (computeCopy) {
						data = drawEngine_->GetPushBufferForTextureData()->PushAligned(srcSize, &bufferOffset, &texBuf, pushAlignment);
						dataScaled = false;
						LoadTextureLevel(*entry, (uint8_t *)data, srcStride, i, 1, dstFmt);
						// Simple test of using a "copy shader" before the upload. This one could unswizzle or whatever
						// and will work for any texture format including 16-bit as long as the shader is written to pack it into int32 size bits
						// which is the smallest possible write.
						VkBuffer localBuf;
						uint32_t localOffset;
						uint32_t localSize = size;
						localOffset = (uint32_t)drawEngine_->GetPushBufferLocal()->Allocate(localSize, &localBuf);

						VkDescriptorSet descSet = computeShaderManager_.GetDescriptorSet(VK_NULL_HANDLE, texBuf, bufferOffset, srcSize, localBuf, localOffset, localSize);
						vkCmdBindPipeline(cmdInit, VK_PIPELINE_BIND_POINT_COMPUTE, computeShaderManager_.GetPipeline(copyCS_));
						dispatchCompute(descSet);

						// After the compute, before the copy, we need a memory barrier.
						VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
						barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
						barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
						barrier.buffer = localBuf;
						barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
						barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
						barrier.offset = localOffset;
						barrier.size = localSize;
						vkCmdPipelineBarrier(cmdInit, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
							0, 0, nullptr, 1, &barrier, 0, nullptr);

						entry->vkTex->UploadMip(cmdInit, i, mipWidth, mipHeight, localBuf, localOffset, stride / bpp);
					} else {
						data = drawEngine_->GetPushBufferForTextureData()->PushAligned(size, &bufferOffset, &texBuf, pushAlignment);
						LoadTextureLevel(*entry, (uint8_t *)data, stride, i, scaleFactor, dstFmt);
						entry->vkTex->UploadMip(cmdInit, i, mipWidth, mipHeight, texBuf, bufferOffset, stride / bpp);
					}
				}
				if (replacer_.Enabled()) {
					// When hardware texture scaling is enabled, this saves the original.
					int w = dataScaled ? mipWidth : mipWidth / scaleFactor;
					int h = dataScaled ? mipHeight : mipHeight / scaleFactor;
					replacer_.NotifyTextureDecoded(replacedInfo, data, stride, i, w, h);
				}
			}
		}

		if (maxLevel == 0) {
			entry->status |= TexCacheEntry::STATUS_BAD_MIPS;
		} else {
			entry->status &= ~TexCacheEntry::STATUS_BAD_MIPS;
		}
		if (replaced.Valid()) {
			entry->SetAlphaStatus(TexCacheEntry::TexStatus(replaced.AlphaStatus()));
		}
		entry->vkTex->EndCreate(cmdInit, false, computeUpload ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	}

	gstate_c.SetTextureFullAlpha(entry->GetAlphaStatus() == TexCacheEntry::STATUS_ALPHA_FULL);
}

VkFormat TextureCacheVulkan::GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) const {
	if (!gstate_c.Supports(GPU_SUPPORTS_16BIT_FORMATS)) {
		return VK_FORMAT_R8G8B8A8_UNORM;
	}
	switch (format) {
	case GE_TFMT_CLUT4:
	case GE_TFMT_CLUT8:
	case GE_TFMT_CLUT16:
	case GE_TFMT_CLUT32:
		return getClutDestFormatVulkan(clutFormat);
	case GE_TFMT_4444:
		return VULKAN_4444_FORMAT;
	case GE_TFMT_5551:
		return VULKAN_1555_FORMAT;
	case GE_TFMT_5650:
		return VULKAN_565_FORMAT;
	case GE_TFMT_8888:
	case GE_TFMT_DXT1:
	case GE_TFMT_DXT3:
	case GE_TFMT_DXT5:
	default:
		return VULKAN_8888_FORMAT;
	}
}

TexCacheEntry::TexStatus TextureCacheVulkan::CheckAlpha(const u32 *pixelData, VkFormat dstFmt, int stride, int w, int h) {
	CheckAlphaResult res;
	switch (dstFmt) {
	case VULKAN_4444_FORMAT:
		res = CheckAlphaRGBA4444Basic(pixelData, stride, w, h);
		break;
	case VULKAN_1555_FORMAT:
		res = CheckAlphaRGBA5551Basic(pixelData, stride, w, h);
		break;
	case VULKAN_565_FORMAT:
		// Never has any alpha.
		res = CHECKALPHA_FULL;
		break;
	default:
		res = CheckAlphaRGBA8888Basic(pixelData, stride, w, h);
		break;
	}

	return (TexCacheEntry::TexStatus)res;
}

void TextureCacheVulkan::LoadTextureLevel(TexCacheEntry &entry, uint8_t *writePtr, int rowPitch, int level, int scaleFactor, VkFormat dstFmt) {
	VulkanTexture *tex = entry.vkTex;
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);

	{
		PROFILE_THIS_SCOPE("decodetex");

		GETextureFormat tfmt = (GETextureFormat)entry.format;
		GEPaletteFormat clutformat = gstate.getClutPaletteFormat();
		u32 texaddr = gstate.getTextureAddress(level);
		int bufw = GetTextureBufw(level, texaddr, tfmt);
		int bpp = dstFmt == VULKAN_8888_FORMAT ? 4 : 2;

		u32 *pixelData = (u32 *)writePtr;
		int decPitch = rowPitch;
		if (scaleFactor > 1) {
			tmpTexBufRearrange_.resize(std::max(bufw, w) * h);
			pixelData = tmpTexBufRearrange_.data();
			// We want to end up with a neatly packed texture for scaling.
			decPitch = w * bpp;
		}

		bool expand32 = !gstate_c.Supports(GPU_SUPPORTS_16BIT_FORMATS);
		DecodeTextureLevel((u8 *)pixelData, decPitch, tfmt, clutformat, texaddr, level, bufw, false, false, expand32);
		gpuStats.numTexturesDecoded++;

		// We check before scaling since scaling shouldn't invent alpha from a full alpha texture.
		if ((entry.status & TexCacheEntry::STATUS_CHANGE_FREQUENT) == 0) {
			// TODO: When we decode directly, this can be more expensive (maybe not on mobile?)
			// This does allow us to skip alpha testing, though.
			TexCacheEntry::TexStatus alphaStatus = CheckAlpha(pixelData, dstFmt, decPitch / bpp, w, h);
			entry.SetAlphaStatus(alphaStatus, level);
		} else {
			entry.SetAlphaStatus(TexCacheEntry::STATUS_ALPHA_UNKNOWN);
		}

		if (scaleFactor > 1) {
			u32 fmt = dstFmt;
			// CPU scaling reads from the destination buffer so we want cached RAM.
			uint8_t *rearrange = (uint8_t *)AllocateAlignedMemory(w * scaleFactor * h * scaleFactor * 4, 16);
			scaler.ScaleAlways((u32 *)rearrange, pixelData, fmt, w, h, scaleFactor);
			pixelData = (u32 *)writePtr;
			dstFmt = (VkFormat)fmt;

			// We always end up at 8888.  Other parts assume this.
			assert(dstFmt == VULKAN_8888_FORMAT);
			bpp = sizeof(u32);
			decPitch = w * bpp;

			if (decPitch != rowPitch) {
				for (int y = 0; y < h; ++y) {
					memcpy(writePtr + rowPitch * y, rearrange + decPitch * y, w * bpp);
				}
				decPitch = rowPitch;
			} else {
				memcpy(writePtr, rearrange, w * h * 4);
			}
			FreeAlignedMemory(rearrange);
		}
	}
}

bool TextureCacheVulkan::GetCurrentTextureDebug(GPUDebugBuffer &buffer, int level) {
	SetTexture(false);
	if (!nextTexture_)
		return false;

	// Apply texture may need to rebuild the texture if we're about to render, or bind a framebuffer.
	TexCacheEntry *entry = nextTexture_;
	ApplyTexture();

	// TODO: Centralize?
	if (entry->framebuffer) {
		VirtualFramebuffer *vfb = entry->framebuffer;
		buffer.Allocate(vfb->bufferWidth, vfb->bufferHeight, GPU_DBG_FORMAT_8888, false);
		bool retval = draw_->CopyFramebufferToMemorySync(vfb->fbo, Draw::FB_COLOR_BIT, 0, 0, vfb->bufferWidth, vfb->bufferHeight, Draw::DataFormat::R8G8B8A8_UNORM, buffer.GetData(), vfb->bufferWidth);
		// Vulkan requires us to re-apply all dynamic state for each command buffer, and the above will cause us to start a new cmdbuf.
		// So let's dirty the things that are involved in Vulkan dynamic state. Readbacks are not frequent so this won't hurt other backends.
		gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE);
		// We may have blitted to a temp FBO.
		framebufferManager_->RebindFramebuffer();
		return retval;
	}

	if (!entry->vkTex)
		return false;
	VulkanTexture *texture = entry->vkTex;
	VulkanRenderManager *renderManager = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);

	GPUDebugBufferFormat bufferFormat;
	Draw::DataFormat drawFormat;
	switch (texture->GetFormat()) {
	case VULKAN_565_FORMAT:
		bufferFormat = GPU_DBG_FORMAT_565;
		drawFormat = Draw::DataFormat::B5G6R5_UNORM_PACK16;
		break;
	case VULKAN_1555_FORMAT:
		bufferFormat = GPU_DBG_FORMAT_5551;
		drawFormat = Draw::DataFormat::B5G5R5A1_UNORM_PACK16;
		break;
	case VULKAN_4444_FORMAT:
		bufferFormat = GPU_DBG_FORMAT_4444;
		drawFormat = Draw::DataFormat::B4G4R4A4_UNORM_PACK16;
		break;
	case VULKAN_8888_FORMAT:
	default:
		bufferFormat = GPU_DBG_FORMAT_8888;
		drawFormat = Draw::DataFormat::R8G8B8A8_UNORM;
		break;
	}

	int w = texture->GetWidth();
	int h = texture->GetHeight();
	buffer.Allocate(w, h, bufferFormat);

	renderManager->CopyImageToMemorySync(texture->GetImage(), level, 0, 0, w, h, drawFormat, (uint8_t *)buffer.GetData(), w);

	// Vulkan requires us to re-apply all dynamic state for each command buffer, and the above will cause us to start a new cmdbuf.
	// So let's dirty the things that are involved in Vulkan dynamic state. Readbacks are not frequent so this won't hurt other backends.
	gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE);
	framebufferManager_->RebindFramebuffer();
	return true;
}

void TextureCacheVulkan::GetStats(char *ptr, size_t size) {
	snprintf(ptr, size, "Alloc: %d slabs\nSlab min/max: %d/%d\nAlloc usage: %d%%",
		allocator_->GetSlabCount(), allocator_->GetMinSlabSize(), allocator_->GetMaxSlabSize(), allocator_->ComputeUsagePercent());
}

std::vector<std::string> TextureCacheVulkan::DebugGetSamplerIDs() const {
	return samplerCache_.DebugGetSamplerIDs();
}

std::string TextureCacheVulkan::DebugGetSamplerString(std::string id, DebugShaderStringType stringType) {
	return samplerCache_.DebugGetSamplerString(id, stringType);
}
