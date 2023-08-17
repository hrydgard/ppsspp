// This file will not pull in the OpenGL headers but will still let you
// access information about the features of the current GPU, for auto-config
// and similar purposes.

#pragma once

#include <string>
#include <cstdint>

// TODO: Replace with thin3d's vendor enum.
enum {
	GPU_VENDOR_NVIDIA = 1,
	GPU_VENDOR_AMD = 2,
	GPU_VENDOR_INTEL = 3,
	GPU_VENDOR_ARM = 4,  // Mali
	GPU_VENDOR_IMGTEC = 5,  // PowerVR
	GPU_VENDOR_QUALCOMM = 6,  // Adreno
	GPU_VENDOR_BROADCOM = 7,  // Raspberry PI etc
	GPU_VENDOR_VIVANTE = 8,
	GPU_VENDOR_APPLE = 9,
	GPU_VENDOR_MESA = 10,
	GPU_VENDOR_UNKNOWN = 0,
};

// TODO: Move to Draw::Bugs
enum {
	BUG_FBO_UNUSABLE = 1,
	BUG_PVR_SHADER_PRECISION_BAD = 2,
	BUG_PVR_SHADER_PRECISION_TERRIBLE = 4,
};

// Extensions to look at using:
// GL_NV_map_buffer_range (same as GL_ARB_map_buffer_range ?)

// WARNING: This gets memset-d - so no strings or other non-POD types please
// TODO: Rename this GLFeatures or something.
struct GLExtensions {
	int ver[3];
	int gpuVendor;
	char model[128];
	int modelNumber;

	bool IsGLES;
	bool IsCoreContext;
	bool GLES3;  // true if the full OpenGL ES 3.0 is supported

	// OES
	bool OES_depth24;
	bool OES_packed_depth_stencil;
	bool OES_depth_texture;
	bool OES_texture_npot;  // If this is set, can wrap non-pow-2 textures. Set on desktop.
	bool OES_mapbuffer;
	bool OES_vertex_array_object;
	bool OES_copy_image;
	bool OES_texture_float;
	bool OES_texture_3D;
	bool OES_texture_compression_astc;

	// ARB
	bool ARB_framebuffer_object;
	bool ARB_pixel_buffer_object;
	bool ARB_blend_func_extended;  // dual source blending
	bool EXT_blend_func_extended;  // dual source blending (GLES, new 2015)
	bool ARB_explicit_attrib_location;
	bool ARB_shader_image_load_store;
	bool ARB_shading_language_420pack;
	bool ARB_conservative_depth;
	bool ARB_copy_image;
	bool ARB_vertex_array_object;
	bool ARB_texture_float;
	bool ARB_draw_instanced;
	bool ARB_buffer_storage;
	bool ARB_cull_distance;
	bool ARB_depth_clamp;
	bool ARB_uniform_buffer_object;
	bool ARB_texture_non_power_of_two;
	bool ARB_stencil_texturing;
	bool ARB_shader_stencil_export;
	bool ARB_texture_compression_bptc;
	bool ARB_texture_compression_rgtc;

	// KHR
	bool KHR_texture_compression_astc_ldr;

	// EXT
	bool EXT_texture_compression_s3tc;
	bool EXT_swap_control_tear;
	bool EXT_discard_framebuffer;
	bool EXT_unpack_subimage;  // always supported on desktop and ES3
	bool EXT_bgra;
	bool EXT_shader_framebuffer_fetch;
	bool EXT_gpu_shader4;
	bool EXT_blend_minmax;
	bool EXT_framebuffer_object;
	bool EXT_copy_image;
	bool EXT_texture_filter_anisotropic;
	bool PBO_EXT;
	bool EXT_draw_instanced;
	bool EXT_buffer_storage;
	bool EXT_clip_cull_distance;
	bool EXT_depth_clamp;

	// NV
	bool NV_copy_image;
	bool NV_framebuffer_blit;
	bool NV_pixel_buffer_object; // GL_NV_pixel_buffer_object

	// ARM
	bool ARM_shader_framebuffer_fetch;

	// APPLE
	bool APPLE_clip_distance;

	// EGL
	bool EGL_NV_system_time;
	bool EGL_NV_coverage_sample;

	// Bugs
	int bugs;

	// Shader precision. Only fetched on ES for now.
	int range[2][6][2];  // [vs,fs][lowf,mediumf,highf,lowi,mediumi,highi][min,max]
	int precision[2][6];  // [vs,fs][lowf...]

	int maxVertexTextureUnits;

	bool supportsETC2;
	bool supportsBC123;
	bool supportsBC45;
	bool supportsBC7;
	bool supportsASTC;

	// greater-or-equal than
	bool VersionGEThan(int major, int minor, int sub = 0);
	int GLSLVersion();
};

extern GLExtensions gl_extensions;

// Call this after filling out vendor etc to lookup the bugs etc.
// Only needs to be called once. Currently called by CheckGLExtensions().
void ProcessGPUFeatures();

extern std::string g_all_gl_extensions;
extern std::string g_all_egl_extensions;

// If this returns false, we're not gonna be able to use a GL context.
bool CheckGLExtensions();

void SetGLCoreContext(bool flag);
void ResetGLExtensions();

std::string ApplyGLSLPrelude(const std::string &source, uint32_t stage);
