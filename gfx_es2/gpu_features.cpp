#include "gfx_es2/gpu_features.h"

bool GLExtensions::VersionGEThan(int major, int minor, int sub) {
	if (gl_extensions.ver[0] > major)
		return true;
	if (gl_extensions.ver[0] < major)
		return false;
	if (gl_extensions.ver[1] > minor)
		return true;
	if (gl_extensions.ver[1] < minor)
		return false;
	return gl_extensions.ver[2] >= sub;
}

void ProcessGPUFeatures() {
	gl_extensions.bugs = 0;
	// Should be table driven instead, this is a quick hack for Galaxy Y
	if (System_GetProperty(SYSPROP_NAME) == "samsung:GT-S5360") {
		gl_extensions.bugs |= BUG_FBO_UNUSABLE;
	}
}
