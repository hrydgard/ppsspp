#pragma once

// Had to break this out into its own header to solve a circular include issue.

#include "Common/CommonTypes.h"

struct PSPThreadContext {
	void reset();

	// r must be followed by f.
	u32 r[32];
	union {
		float f[32];
		u32 fi[32];
		int fs[32];
	};
	union {
		float v[128];
		u32 vi[128];
	};
	u32 vfpuCtrl[16];

	union {
		struct {
			u32 pc;

			u32 lo;
			u32 hi;

			u32 fcr31;
			u32 fpcond;
		};
		u32 other[6];
	};
};
