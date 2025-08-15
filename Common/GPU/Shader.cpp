#include "Common/GPU/Shader.h"

#ifdef USE_CRT_DBG
#undef new
#endif

#include "ext/glslang/SPIRV/GlslangToSpv.h"

const char *ShaderLanguageAsString(ShaderLanguage lang) {
	switch (lang) {
	case GLSL_1xx: return "GLSL 1.x";
	case GLSL_3xx: return "GLSL 3.x";
	case GLSL_VULKAN: return "GLSL-VK";
	case HLSL_D3D11: return "HLSL-D3D11";
	default: return "(combination)";
	}
}

const char *ShaderStageAsString(ShaderStage stage) {
	switch (stage) {
	case ShaderStage::Fragment: return "Fragment";
	case ShaderStage::Vertex: return "Vertex";
	case ShaderStage::Geometry: return "Geometry";
	case ShaderStage::Compute: return "Compute";
	default: return "(unknown)";
	}
}

ShaderLanguageDesc::ShaderLanguageDesc(ShaderLanguage lang) {
	Init(lang);
}

void ShaderLanguageDesc::Init(ShaderLanguage lang) {
	shaderLanguage = lang;
	strcpy(driverInfo, "");
	switch (lang) {
	case GLSL_1xx:
		// Just used in the shader test, and as a basis for the others in DetectShaderLanguage.
		// The real OpenGL initialization happens in thin3d_gl.cpp.
		glslVersionNumber = 110;
		attribute = "attribute";
		varying_vs = "varying";
		varying_fs = "varying";
		fragColor0 = "gl_FragColor";
		fragColor1 = "fragColor1";
		texture = "texture2D";
		texture3D = "texture3D";
		texelFetch = nullptr;
		bitwiseOps = false;
		lastFragData = nullptr;
		gles = false;
		forceMatrix4x4 = true;
		break;
	case GLSL_3xx:
		// Just used in the shader test.
		glslVersionNumber = 300;  // GLSL ES 3.0
		varying_vs = "out";
		varying_fs = "in";
		attribute = "in";
		fragColor0 = "fragColor0";
		fragColor1 = "fragColor1";
		texture = "texture";
		texture3D = "texture";
		texelFetch = "texelFetch";
		bitwiseOps = true;
		lastFragData = nullptr;
		gles = true;
		forceMatrix4x4 = true;
		glslES30 = true;
		break;
	case GLSL_VULKAN:
		fragColor0 = "fragColor0";
		fragColor1 = "fragColor1";
		varying_fs = "in";
		varying_vs = "out";
		attribute = "in";
		bitwiseOps = true;
		framebufferFetchExtension = nullptr;
		gles = false;
		glslES30 = true;
		glslVersionNumber = 450;
		lastFragData = nullptr;
		texture = "texture";
		texture3D = "texture";
		texelFetch = "texelFetch";
		forceMatrix4x4 = false;
		coefsFromBuffers = true;
		vertexIndex = true;
		break;
	case HLSL_D3D11:
		fragColor0 = "outfragment.target";
		fragColor1 = "outfragment.target1";
		vertexIndex = true;  // if declared as a semantic input
		varying_fs = "in";
		varying_vs = "out";
		attribute = "in";
		bitwiseOps = lang == HLSL_D3D11;
		framebufferFetchExtension = nullptr;
		gles = false;
		glslES30 = true;
		glslVersionNumber = 0;
		lastFragData = nullptr;
		texture = "texture";
		texture3D = "texture";
		texelFetch = "texelFetch";
		forceMatrix4x4 = false;
		coefsFromBuffers = true;
		vsOutPrefix = "Out.";
		viewportYSign = "-";
		break;
	}
}

void InitShaderResources(TBuiltInResource &Resources) {
	Resources.maxLights = 32;
	Resources.maxClipPlanes = 6;
	Resources.maxTextureUnits = 32;
	Resources.maxTextureCoords = 32;
	Resources.maxVertexAttribs = 64;
	Resources.maxVertexUniformComponents = 4096;
	Resources.maxVaryingFloats = 64;
	Resources.maxVertexTextureImageUnits = 32;
	Resources.maxCombinedTextureImageUnits = 80;
	Resources.maxTextureImageUnits = 32;
	Resources.maxFragmentUniformComponents = 4096;
	Resources.maxDrawBuffers = 32;
	Resources.maxVertexUniformVectors = 128;
	Resources.maxVaryingVectors = 8;
	Resources.maxFragmentUniformVectors = 16;
	Resources.maxVertexOutputVectors = 16;
	Resources.maxFragmentInputVectors = 15;
	Resources.minProgramTexelOffset = -8;
	Resources.maxProgramTexelOffset = 7;
	Resources.maxClipDistances = 8;
	Resources.maxComputeWorkGroupCountX = 65535;
	Resources.maxComputeWorkGroupCountY = 65535;
	Resources.maxComputeWorkGroupCountZ = 65535;
	Resources.maxComputeWorkGroupSizeX = 1024;
	Resources.maxComputeWorkGroupSizeY = 1024;
	Resources.maxComputeWorkGroupSizeZ = 64;
	Resources.maxComputeUniformComponents = 1024;
	Resources.maxComputeTextureImageUnits = 16;
	Resources.maxComputeImageUniforms = 8;
	Resources.maxComputeAtomicCounters = 8;
	Resources.maxComputeAtomicCounterBuffers = 1;
	Resources.maxVaryingComponents = 60;
	Resources.maxVertexOutputComponents = 64;
	Resources.maxGeometryInputComponents = 64;
	Resources.maxGeometryOutputComponents = 128;
	Resources.maxFragmentInputComponents = 128;
	Resources.maxImageUnits = 8;
	Resources.maxCombinedImageUnitsAndFragmentOutputs = 8;
	Resources.maxCombinedShaderOutputResources = 8;
	Resources.maxImageSamples = 0;
	Resources.maxVertexImageUniforms = 0;
	Resources.maxTessControlImageUniforms = 0;
	Resources.maxTessEvaluationImageUniforms = 0;
	Resources.maxGeometryImageUniforms = 0;
	Resources.maxFragmentImageUniforms = 8;
	Resources.maxCombinedImageUniforms = 8;
	Resources.maxGeometryTextureImageUnits = 16;
	Resources.maxGeometryOutputVertices = 256;
	Resources.maxGeometryTotalOutputComponents = 1024;
	Resources.maxGeometryUniformComponents = 1024;
	Resources.maxGeometryVaryingComponents = 64;
	Resources.maxTessControlInputComponents = 128;
	Resources.maxTessControlOutputComponents = 128;
	Resources.maxTessControlTextureImageUnits = 16;
	Resources.maxTessControlUniformComponents = 1024;
	Resources.maxTessControlTotalOutputComponents = 4096;
	Resources.maxTessEvaluationInputComponents = 128;
	Resources.maxTessEvaluationOutputComponents = 128;
	Resources.maxTessEvaluationTextureImageUnits = 16;
	Resources.maxTessEvaluationUniformComponents = 1024;
	Resources.maxTessPatchComponents = 120;
	Resources.maxPatchVertices = 32;
	Resources.maxTessGenLevel = 64;
	Resources.maxViewports = 16;
	Resources.maxVertexAtomicCounters = 0;
	Resources.maxTessControlAtomicCounters = 0;
	Resources.maxTessEvaluationAtomicCounters = 0;
	Resources.maxGeometryAtomicCounters = 0;
	Resources.maxFragmentAtomicCounters = 8;
	Resources.maxCombinedAtomicCounters = 8;
	Resources.maxAtomicCounterBindings = 1;
	Resources.maxVertexAtomicCounterBuffers = 0;
	Resources.maxTessControlAtomicCounterBuffers = 0;
	Resources.maxTessEvaluationAtomicCounterBuffers = 0;
	Resources.maxGeometryAtomicCounterBuffers = 0;
	Resources.maxFragmentAtomicCounterBuffers = 1;
	Resources.maxCombinedAtomicCounterBuffers = 1;
	Resources.maxAtomicCounterBufferSize = 16384;
	Resources.maxTransformFeedbackBuffers = 4;
	Resources.maxTransformFeedbackInterleavedComponents = 64;
	Resources.maxCullDistances = 8;
	Resources.maxCombinedClipAndCullDistances = 8;
	Resources.maxSamples = 4;
	Resources.maxDualSourceDrawBuffersEXT = 1;
	Resources.limits.nonInductiveForLoops = 1;
	Resources.limits.whileLoops = 1;
	Resources.limits.doWhileLoops = 1;
	Resources.limits.generalUniformIndexing = 1;
	Resources.limits.generalAttributeMatrixVectorIndexing = 1;
	Resources.limits.generalVaryingIndexing = 1;
	Resources.limits.generalSamplerIndexing = 1;
	Resources.limits.generalVariableIndexing = 1;
	Resources.limits.generalConstantMatrixVectorIndexing = 1;
}
