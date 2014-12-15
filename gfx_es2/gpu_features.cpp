#include <cstring>
#include "base/logging.h"
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

	ILOG("Checking for GL driver bugs... vendor=%i model='%s'", (int)gl_extensions.gpuVendor, gl_extensions.model);
	// Should be table driven instead, this is a quick hack for Galaxy Y
	if (System_GetProperty(SYSPROP_NAME) == "samsung:GT-S5360") {
		gl_extensions.bugs |= BUG_FBO_UNUSABLE;
	}

	if (gl_extensions.gpuVendor == GPU_VENDOR_POWERVR) {
		if (!strcmp(gl_extensions.model, "PowerVR SGX 540") ||
			  !strcmp(gl_extensions.model, "PowerVR SGX 530") ||
				!strcmp(gl_extensions.model, "PowerVR SGX 520") ) {
			WLOG("GL DRIVER BUG: PVR with bad and terrible precision");
			gl_extensions.bugs |= BUG_PVR_SHADER_PRECISION_TERRIBLE | BUG_PVR_SHADER_PRECISION_BAD;
		} else {
			WLOG("GL DRIVER BUG: PVR with bad precision");
			gl_extensions.bugs |= BUG_PVR_SHADER_PRECISION_BAD;
		}
	}
}
