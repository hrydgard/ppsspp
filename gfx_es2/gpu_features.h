// This file will not pull in the OpenGL headers but will still let you
// access information about the features of the current GPU, for auto-config
// and similar purposes.

#pragma once

#include "base/NativeApp.h"

enum {
	GPU_VENDOR_NVIDIA = 1,
	GPU_VENDOR_AMD = 2,
	GPU_VENDOR_INTEL = 3,
	GPU_VENDOR_ARM = 4,  // Mali
	GPU_VENDOR_POWERVR = 5,
	GPU_VENDOR_ADRENO = 6,
	GPU_VENDOR_BROADCOM = 7,
	GPU_VENDOR_UNKNOWN = 0,
};

enum {
	BUG_FBO_UNUSABLE=1
};

// Extensions to look at using:
// GL_NV_map_buffer_range (same as GL_ARB_map_buffer_range ?)

// WARNING: This gets memset-d - so no strings please
// TODO: Rename this GLFeatures or something.
struct GLExtensions {
	int ver[3];
	int gpuVendor;
	bool GLES3;  // true if the full OpenGL ES 3.0 is supported
	bool OES_depth24;
	bool OES_packed_depth_stencil;
	bool OES_depth_texture;
	bool EXT_discard_framebuffer;
	bool FBO_ARB;
	bool FBO_EXT;
	bool PBO_ARB;
	bool EXT_swap_control_tear;
	bool OES_mapbuffer;
	bool OES_vertex_array_object;
	bool EXT_shader_framebuffer_fetch;
	bool EXT_blend_minmax;
	bool ATIClampBug;
	bool NV_draw_texture;
	bool NV_copy_image;
	bool EXT_unpack_subimage;  // always supported on desktop and ES3
	// EGL extensions

	bool EGL_NV_system_time;
	bool EGL_NV_coverage_sample;

	// Bugs
	int bugs;
};

extern GLExtensions gl_extensions;


// Call this after filling out vendor etc to lookup the bugs etc.
// Only needs to be called ones. Currently called by CheckGLExtensions().
void ProcessGPUFeatures();

