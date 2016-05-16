#include <string.h>
#include <thin3d/thin3d.h>

#include "base/logging.h"
#include "image/zim_load.h"
#include "image/png_load.h"
#include "file/vfs.h"
#include "ext/jpge/jpgd.h"

// ================================== PIXEL/FRAGMENT SHADERS

// The Vulkan ones can be re-used with modern GL later if desired, as they're just GLSL.

static const char * const glsl_fsTexCol =
"#ifdef GL_ES\n"
"precision lowp float;\n"
"#endif\n"
"varying vec4 oColor0;\n"
"varying vec2 oTexCoord0;\n"
"uniform sampler2D Sampler0;\n"
"void main() { gl_FragColor = texture2D(Sampler0, oTexCoord0) * oColor0; }\n";

static const char * const hlslFsTexCol =
"struct PS_INPUT { float4 color : COLOR0; float2 uv : TEXCOORD0; };\n"
"sampler2D Sampler0 : register(s0);\n"
"float4 main(PS_INPUT input) : COLOR0 {\n"
"  return input.color * tex2D(Sampler0, input.uv);\n"
"}\n";

static const char * const vulkan_fsTexCol =
"#version 140\n"
"#extension GL_ARB_separate_shader_objects : enable\n"
"#extension GL_ARB_shading_language_420pack : enable\n"
"layout(location = 0) in vec4 oColor0;\n"
"layout(location = 1) in vec2 oTexCoord0;\n"
"layout(location = 0) out vec4 fragColor0\n;"
"layout(set = 0, binding = 1) uniform sampler2D Sampler0;\n"
"void main() { fragColor0 = texture(Sampler0, oTexCoord0) * oColor0; }\n";

static const char * const glsl_fsCol =
"#ifdef GL_ES\n"
"precision lowp float;\n"
"#endif\n"
"varying vec4 oColor0;\n"
"void main() { gl_FragColor = oColor0; }\n";

static const char * const hlslFsCol =
"struct PS_INPUT { float4 color : COLOR0; };\n"
"float4 main(PS_INPUT input) : COLOR0 {\n"
"  return input.color;\n"
"}\n";

static const char * const vulkan_fsCol =
"#version 140\n"
"#extension GL_ARB_separate_shader_objects : enable\n"
"#extension GL_ARB_shading_language_420pack : enable\n"
"layout(location = 0) in vec4 oColor0;\n"
"layout(location = 0) out vec4 fragColor0\n;"
"void main() { fragColor0 = oColor0; }\n";



// ================================== VERTEX SHADERS

static const char * const glsl_vsCol =
"attribute vec3 Position;\n"
"attribute vec4 Color0;\n"
"varying vec4 oColor0;\n"
"uniform mat4 WorldViewProj;\n"
"void main() {\n"
"  gl_Position = WorldViewProj * vec4(Position, 1.0);\n"
"  oColor0 = Color0;\n"
"}";

static const char * const hlslVsCol =
"struct VS_INPUT { float3 Position : POSITION; float4 Color0 : COLOR0; };\n"
"struct VS_OUTPUT { float4 Position : POSITION; float4 Color0 : COLOR0; };\n"
"float4x4 WorldViewProj;\n"
"VS_OUTPUT main(VS_INPUT input) {\n"
"  VS_OUTPUT output;\n"
"  output.Position = mul(float4(input.Position, 1.0), WorldViewProj);\n"
"  output.Color0 = input.Color0;\n"
"  return output;\n"
"}\n";

static const char * const vulkan_vsCol =
"#version 400\n"
"#extension GL_ARB_separate_shader_objects : enable\n"
"#extension GL_ARB_shading_language_420pack : enable\n"
"layout (std140, set = 0, binding = 0) uniform bufferVals {\n"
"    mat4 WorldViewProj;\n"
"} myBufferVals;\n"
"layout (location = 0) in vec4 pos;\n"
"layout (location = 1) in vec4 inColor;\n"
"layout (location = 0) out vec4 outColor;\n"
"out gl_PerVertex { vec4 gl_Position; };\n"
"void main() {\n"
"   outColor = inColor;\n"
"   gl_Position = myBufferVals.WorldViewProj * pos;\n"
"}\n";


static const char * const glsl_vsTexCol =
"attribute vec3 Position;\n"
"attribute vec4 Color0;\n"
"attribute vec2 TexCoord0;\n"
"varying vec4 oColor0;\n"
"varying vec2 oTexCoord0;\n"
"uniform mat4 WorldViewProj;\n"
"void main() {\n"
"  gl_Position = WorldViewProj * vec4(Position, 1.0);\n"
"  oColor0 = Color0;\n"
"  oTexCoord0 = TexCoord0;\n"
"}\n";

static const char * const hlslVsTexCol =
"struct VS_INPUT { float3 Position : POSITION; float2 Texcoord0 : TEXCOORD0; float4 Color0 : COLOR0; };\n"
"struct VS_OUTPUT { float4 Position : POSITION; float2 Texcoord0 : TEXCOORD0; float4 Color0 : COLOR0; };\n"
"float4x4 WorldViewProj;\n"
"VS_OUTPUT main(VS_INPUT input) {\n"
"  VS_OUTPUT output;\n"
"  output.Position = mul(float4(input.Position, 1.0), WorldViewProj);\n"
"  output.Texcoord0 = input.Texcoord0;\n"
"  output.Color0 = input.Color0;\n"
"  return output;\n"
"}\n";

static const char * const vulkan_vsTexCol =
"#version 400\n"
"#extension GL_ARB_separate_shader_objects : enable\n"
"#extension GL_ARB_shading_language_420pack : enable\n"
"layout (std140, set = 0, binding = 0) uniform bufferVals {\n"
"    mat4 WorldViewProj;\n"
"} myBufferVals;\n"
"layout (location = 0) in vec4 pos;\n"
"layout (location = 1) in vec4 inColor;\n"
"layout (location = 2) in vec2 inTexCoord;\n"
"layout (location = 0) out vec4 outColor;\n"
"layout (location = 1) out vec2 outTexCoord;\n"
"out gl_PerVertex { vec4 gl_Position; };\n"
"void main() {\n"
"   outColor = inColor;\n"
"   outTexCoord = inTexCoord;\n"
"   gl_Position = myBufferVals.WorldViewProj * pos;\n"
"}\n";


void Thin3DContext::CreatePresets() {
	// Build prebuilt objects
	T3DBlendStateDesc off = { false };
	T3DBlendStateDesc additive = { true, T3DBlendEquation::ADD, T3DBlendFactor::ONE, T3DBlendFactor::ONE, T3DBlendEquation::ADD, T3DBlendFactor::ONE, T3DBlendFactor::ZERO };
	T3DBlendStateDesc standard_alpha = { true, T3DBlendEquation::ADD, T3DBlendFactor::SRC_ALPHA, T3DBlendFactor::ONE_MINUS_SRC_ALPHA, T3DBlendEquation::ADD, T3DBlendFactor::ONE, T3DBlendFactor::ZERO };
	T3DBlendStateDesc premul_alpha = { true, T3DBlendEquation::ADD, T3DBlendFactor::ONE, T3DBlendFactor::ONE_MINUS_SRC_ALPHA, T3DBlendEquation::ADD, T3DBlendFactor::ONE, T3DBlendFactor::ZERO };

	bsPresets_[BS_OFF] = CreateBlendState(off);
	bsPresets_[BS_ADDITIVE] = CreateBlendState(additive);
	bsPresets_[BS_STANDARD_ALPHA] = CreateBlendState(standard_alpha);
	bsPresets_[BS_PREMUL_ALPHA] = CreateBlendState(premul_alpha);

	T3DSamplerStateDesc nearest = { CLAMP, CLAMP, NEAREST, NEAREST, NEAREST };
	T3DSamplerStateDesc linear = { CLAMP, CLAMP, LINEAR, LINEAR, NEAREST };

	sampsPresets_[SAMPS_NEAREST] = CreateSamplerState(nearest);
	sampsPresets_[SAMPS_LINEAR] = CreateSamplerState(linear);

	vsPresets_[VS_TEXTURE_COLOR_2D] = CreateVertexShader(glsl_vsTexCol, hlslVsTexCol, vulkan_vsTexCol);
	vsPresets_[VS_COLOR_2D] = CreateVertexShader(glsl_vsCol, hlslVsCol, vulkan_vsCol);

	fsPresets_[FS_TEXTURE_COLOR_2D] = CreateFragmentShader(glsl_fsTexCol, hlslFsTexCol, vulkan_fsTexCol);
	fsPresets_[FS_COLOR_2D] = CreateFragmentShader(glsl_fsCol, hlslFsCol, vulkan_fsCol);

	ssPresets_[SS_TEXTURE_COLOR_2D] = CreateShaderSet(vsPresets_[VS_TEXTURE_COLOR_2D], fsPresets_[FS_TEXTURE_COLOR_2D]);
	ssPresets_[SS_COLOR_2D] = CreateShaderSet(vsPresets_[VS_COLOR_2D], fsPresets_[FS_COLOR_2D]);
}

Thin3DContext::~Thin3DContext() {
	for (int i = 0; i < VS_MAX_PRESET; i++) {
		if (vsPresets_[i]) {
			vsPresets_[i]->Release();
		}
	}
	for (int i = 0; i < FS_MAX_PRESET; i++) {
		if (fsPresets_[i]) {
			fsPresets_[i]->Release();
		}
	}
	for (int i = 0; i < BS_MAX_PRESET; i++) {
		if (bsPresets_[i]) {
			bsPresets_[i]->Release();
		}
	}
	for (int i = 0; i < SS_MAX_PRESET; i++) {
		if (ssPresets_[i]) {
			ssPresets_[i]->Release();
		}
	}
	for (int i = 0; i < SAMPS_MAX_PRESET; i++) {
		if (sampsPresets_[i]) {
			sampsPresets_[i]->Release();
		}
	}
}

static T3DImageFormat ZimToT3DFormat(int zim) {
	switch (zim) {
	case ZIM_ETC1: return T3DImageFormat::ETC1;
	case ZIM_RGBA8888: return T3DImageFormat::RGBA8888;
	case ZIM_LUMINANCE: return T3DImageFormat::LUMINANCE;
	default: return T3DImageFormat::RGBA8888;
	}
}

static T3DImageType DetectImageFileType(const uint8_t *data, size_t size) {
	if (!memcmp(data, "ZIMG", 4)) {
		return ZIM;
	} else if (!memcmp(data, "\x89\x50\x4E\x47", 4)) {
		return PNG;
	} else if (!memcmp(data, "\xff\xd8\xff\xe0", 4)) {
		return JPEG;
	} else {
		return TYPE_UNKNOWN;
	}
}

static bool LoadTextureLevels(const uint8_t *data, size_t size, T3DImageType type, int width[16], int height[16], int *num_levels, T3DImageFormat *fmt, uint8_t *image[16], int *zim_flags) {
	if (type == DETECT) {
		type = DetectImageFileType(data, size);
	}
	if (type == TYPE_UNKNOWN) {
		ELOG("File has unknown format");
		return false;
	}

	*num_levels = 0;
	*zim_flags = 0;

	switch (type) {
	case ZIM:
	{
		*num_levels = LoadZIMPtr((const uint8_t *)data, size, width, height, zim_flags, image);
		*fmt = ZimToT3DFormat(*zim_flags & ZIM_FORMAT_MASK);
	}
		break;

	case PNG:
		if (1 == pngLoadPtr((const unsigned char *)data, size, &width[0], &height[0], &image[0], false)) {
			*num_levels = 1;
			*fmt = RGBA8888;
		}
		break;

	case JPEG:
		{
			int actual_components = 0;
			unsigned char *jpegBuf = jpgd::decompress_jpeg_image_from_memory(data, (int)size, &width[0], &height[0], &actual_components, 4);
			if (jpegBuf) {
				*num_levels = 1;
				*fmt = RGBA8888;
				image[0] = (uint8_t *)jpegBuf;
			}
		}
		break;

	default:
		ELOG("Unknown image format");
		return false;
	}

	return *num_levels > 0;
}

bool Thin3DTexture::LoadFromFileData(const uint8_t *data, size_t dataSize, T3DImageType type) {
	int width[16], height[16];
	uint8_t *image[16] = { nullptr };

	int num_levels;
	int zim_flags;
	T3DImageFormat fmt;

	if (!LoadTextureLevels(data, dataSize, type, width, height, &num_levels, &fmt, image, &zim_flags)) {
		return false;
	}

	if (num_levels < 0 || num_levels >= 16) {
		ELOG("Invalid num_levels: %d. Falling back to one. Image: %dx%d", num_levels, width[0], height[0]);
		num_levels = 1;
	}

	Create(LINEAR2D, fmt, width[0], height[0], 1, num_levels);
	for (int i = 0; i < num_levels; i++) {
		if (image[i]) {
			SetImageData(0, 0, 0, width[i], height[i], 1, i, width[i] * 4, image[i]);
			free(image[i]);
		} else {
			ELOG("Missing image level %i", i);
		}
	}

	Finalize(zim_flags);
	return true;
}

bool Thin3DTexture::LoadFromFile(const std::string &filename, T3DImageType type) {
	filename_ = "";
	size_t fileSize;
	uint8_t *buffer = VFSReadFile(filename.c_str(), &fileSize);
	if (!buffer) {
		return false;
	}
	bool retval = LoadFromFileData(buffer, fileSize, type);
	if (retval) {
		filename_ = filename;
	} else {
		ELOG("%s: Failed to load texture %s", __FUNCTION__, filename.c_str());
	}
	delete[] buffer;
	return retval;
}

Thin3DTexture *Thin3DContext::CreateTextureFromFile(const char *filename, T3DImageType type) {
	Thin3DTexture *tex = CreateTexture();
	if (!tex->LoadFromFile(filename, type)) {
		tex->Release();
		return NULL;
	}
	return tex;
}

// TODO: Remove the code duplication between this and LoadFromFileData
Thin3DTexture *Thin3DContext::CreateTextureFromFileData(const uint8_t *data, int size, T3DImageType type) {
	int width[16], height[16];
	int num_levels = 0;
	int zim_flags = 0;
	T3DImageFormat fmt;
	uint8_t *image[16] = { nullptr };

	if (!LoadTextureLevels(data, size, type, width, height, &num_levels, &fmt, image, &zim_flags)) {
		return NULL;
	}

	Thin3DTexture *tex = CreateTexture(LINEAR2D, fmt, width[0], height[0], 1, num_levels);
	for (int i = 0; i < num_levels; i++) {
		tex->SetImageData(0, 0, 0, width[i], height[i], 1, i, width[i] * 4, image[i]);
		free(image[i]);
	}

	tex->Finalize(zim_flags);
	return tex;
}
