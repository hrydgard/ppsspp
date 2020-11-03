#include "Common/GPU/Shader.h"

ShaderLanguageDesc::ShaderLanguageDesc(ShaderLanguage lang) {
	Init(lang);
}

void ShaderLanguageDesc::Init(ShaderLanguage lang) {
	shaderLanguage = lang;
	switch (lang) {
	case GLSL_1xx:
		// Just used in the shader test, and as a basis for the others in DetectShaderLanguage.
		glslVersionNumber = 110;
		attribute = "attribute";
		varying_vs = "varying";
		varying_fs = "varying";
		fragColor0 = "gl_FragColor";
		fragColor1 = "fragColor1";
		texture = "texture2D";
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
		texelFetch = "texelFetch";
		bitwiseOps = true;
		lastFragData = nullptr;
		gles = true;
		forceMatrix4x4 = true;
		glslES30 = true;
		texelFetch = "texelFetch";
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
		texelFetch = "texelFetch";
		forceMatrix4x4 = false;
		coefsFromBuffers = true;
		break;
	case HLSL_D3D9:
	case HLSL_D3D11:
		fragColor0 = "outfragment.target";
		fragColor1 = "outfragment.target1";
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
		texelFetch = "texelFetch";
		forceMatrix4x4 = false;
		coefsFromBuffers = true;
		vsOutPrefix = "Out.";
		break;
	}
}

