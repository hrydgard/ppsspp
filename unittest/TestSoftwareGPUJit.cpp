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
#include "GPU/Software/DrawPixel.h"
#include "GPU/Software/Sampler.h"
#include "GPU/Software/SoftGpu.h"

static bool TestPixelJit() {
	using namespace Rasterizer;
	PixelJitCache *cache = new PixelJitCache();

	GMRng rng;
	int successes = 0;
	int count = 3000;
	bool header = false;

	u32 *fb_data = new u32[512 * 2];
	u16 *zb_data = new u16[512 * 2];
	fb.as32 = fb_data;
	depthbuf.as16 = zb_data;

	for (int i = 0; i < count; ) {
		PixelFuncID id;
		memset(&id, 0, sizeof(id));
		id.fullKey = (uint64_t)rng.R32() | ((uint64_t)rng.R32() << 32);

		std::string desc = DescribePixelFuncID(id);
		if (startsWith(desc, "INVALID"))
			continue;
		i++;

		SingleFunc func = cache->GetSingle(id);
		SingleFunc genericFunc = cache->GenericSingle(id);
		if (func != genericFunc) {
			successes++;
		} else {
			if (!header)
				printf("Failed funcs:\n");
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
}

bool TestSoftwareGPUJit() {
	g_Config.bSoftwareRenderingJit = true;
	ResetHitAnyAsserts();

	if (!TestPixelJit()) {
		return false;
	}

	return true;
}
