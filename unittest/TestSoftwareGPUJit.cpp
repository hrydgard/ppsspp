// Copyright (c) 2022- PPSSPP Project.

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

#include "Common/Data/Random/Rng.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "GPU/Software/BinManager.h"
#include "GPU/Software/DrawPixel.h"
#include "GPU/Software/Sampler.h"
#include "GPU/Software/SoftGpu.h"

static bool TestSamplerJit() {
#if PPSSPP_ARCH(AMD64)
	using namespace Sampler;
	SamplerJitCache *cache = new SamplerJitCache();
	BinManager binner;

	auto GetLinear = [&](SamplerID &id) {
		id.linear = true;
		id.fetch = false;
		return cache->GetLinear(id, &binner);
	};
	auto GetNearest = [&](SamplerID &id) {
		id.linear = false;
		id.fetch = false;
		return cache->GetNearest(id, &binner);
	};
	auto GetFetch = [&](SamplerID &id) {
		id.linear = false;
		id.fetch = true;
		return cache->GetFetch(id, &binner);
	};

	GMRng rng;
	int successes = 0;
	int count = 3000;
	bool header = false;

	u8 **tptr = new u8 *[8];
	uint16_t *bufw = new uint16_t[8];
	u8 *clut = new u8[1024];
	memset(clut, 0, 1024);

	for (int i = 0; i < 8; ++i) {
		tptr[i] = new u8[1024 * 1024 * 4];
		memset(tptr[i], 0, 1024 * 1024 * 4);
		bufw[i] = 1;
	}

	for (int i = 0; i < count; ) {
		SamplerID id;
		memset(&id, 0, sizeof(id));
		id.fullKey = rng.R32();
		id.cached.clut = clut;

		for (int i = 0; i < 8; ++i) {
			id.cached.sizes[i].w = 1;
			id.cached.sizes[i].h = 1;
		}

		std::string desc = DescribeSamplerID(id);
		if (startsWith(desc, "INVALID"))
			continue;
		i++;

		LinearFunc linearFunc = GetLinear(id);
		NearestFunc nearestFunc = GetNearest(id);
		FetchFunc fetchFunc = GetFetch(id);
		if (linearFunc != nullptr && nearestFunc != nullptr && fetchFunc != nullptr) {
			successes++;
		} else {
			if (!header)
				printf("Failed sampler funcs:\n");
			header = true;
			printf(" * %s (L:%d, N:%d, F:%d)\n", desc.c_str(), linearFunc != nullptr, nearestFunc != nullptr, fetchFunc != nullptr);
			continue;
		}

		// Try running each to make sure they don't trivially crash.
		const auto primArg = Rasterizer::ToVec4IntArg(Math3D::Vec4<int>(127, 127, 127, 127));
		linearFunc(0.0f, 0.0f, primArg, tptr, bufw, 1, 7, id);
		nearestFunc(0.0f, 0.0f, primArg, tptr, bufw, 1, 7, id);
		fetchFunc(0, 0, tptr[0], bufw[0], 1, id);
	}

	if (successes < count)
		printf("SamplerFunc success: %d / %d\n", successes, count);

	for (int i = 0; i < 8; ++i) {
		delete [] tptr[i];
	}
	delete [] tptr;
	delete [] bufw;
	delete [] clut;

	delete cache;
	return successes == count && !HitAnyAsserts();
#else
	// Don't test sampler jit, not supported.
	return true;
#endif
}

static bool TestPixelJit() {
#if PPSSPP_ARCH(AMD64)
	using namespace Rasterizer;
	PixelJitCache *cache = new PixelJitCache();
	BinManager binner;

	GMRng rng;
	int successes = 0;
	int count = 3000;
	bool header = false;

	u32 *fb_data = new u32[512 * 2];
	u16 *zb_data = new u16[512 * 2];
	fb.as32 = fb_data;
	depthbuf.as16 = zb_data;
	memset(fb_data, 0, sizeof(u32) * 512 * 2);
	memset(zb_data, 0, sizeof(u16) * 512 * 2);

	for (int i = 0; i < count; ) {
		PixelFuncID id;
		memset(&id, 0, sizeof(id));
		id.fullKey = (uint64_t)rng.R32() | ((uint64_t)rng.R32() << 32);

		std::string desc = DescribePixelFuncID(id);
		if (startsWith(desc, "INVALID"))
			continue;
		i++;

		SingleFunc func = cache->GetSingle(id, &binner);
		SingleFunc genericFunc = cache->GenericSingle(id);
		if (func != genericFunc) {
			successes++;
		} else {
			if (!header)
				printf("Failed pixel funcs:\n");
			header = true;
			printf(" * %s\n", desc.c_str());
		}

		// Try running it to make sure it doesn't trivially crash.
		func(0, 0, 1000, 255, ToVec4IntArg(Math3D::Vec4<int>(127, 127, 127, 127)), id);
	}

	if (successes < count)
		printf("PixelFunc success: %d / %d\n", successes, count);

	delete [] fb_data;
	delete [] zb_data;
	delete cache;
	return successes == count && !HitAnyAsserts();
#else
	// Not yet supported
	return true;
#endif
}

bool TestSoftwareGPUJit() {
	g_Config.bSoftwareRenderingJit = true;
	ResetHitAnyAsserts();

	if (!TestSamplerJit()) {
		return false;
	}

	if (!TestPixelJit()) {
		return false;
	}

	return true;
}
