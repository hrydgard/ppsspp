#include "gfx_es2/gpu_features.h"

void ProcessGPUFeatures() {
	gl_extensions.bugs = 0;
	// Should be table driven instead, this is a quick hack for Galaxy Y
	if (System_GetProperty(SYSPROP_NAME) == "samsung:GT-S5360") {
		gl_extensions.bugs |= BUG_FBO_UNUSABLE;
	}
}
