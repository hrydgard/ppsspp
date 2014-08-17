#include <thin3d/thin3d.h>

static const char * const fsTexCol =
"varying vec4 oColor0;\n"
"varying vec2 oTexCoord0;\n"
"uniform sampler2D Sampler0;\n"
"void main() { gl_FragColor = oColor0 * texture2D(Sampler0, oTexCoord0); }\n";

static const char * const hlslFsTexCol = "";

static const char * const fsCol =
"varying vec4 oColor0;\n"
"uniform sampler2D Sampler0;\n"
"void main() { gl_FragColor = oColor0; }\n";

static const char * const hlslFsCol = "";

static const char * const vsCol =
"attribute vec3 Position;\n"
"attribute vec4 Color0;\n"
"varying vec4 oColor0;\n"
"uniform mat4 WorldViewProj;\n"
"void main() {\n"
"	gl_Position = WorldViewProj * vec4(Position, 1.0);\n"
"	oColor0 = Color0;\n"
"}";

static const char * const hlslVsCol = "";

static const char * const vsTexCol =
"attribute vec3 Position;\n"
"attribute vec4 Color0;\n"
"attribute vec2 TexCoord0;\n"
"varying vec4 oColor0;\n"
"varying vec2 oTexCoord0;\n"
"uniform mat4 WorldViewProj;\n"
"void main() {\n"
"	gl_Position = WorldViewProj * vec4(Position, 1.0);\n"
"	oColor0 = Color0;\n"
" oTexCoord0 = TexCoord0; \n"
"}";

static const char * const hlslVsTexCol = "";

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

	vsPresets_[VS_TEXTURE_COLOR_2D] = CreateVertexShader(vsTexCol, hlslVsTexCol);
	vsPresets_[VS_COLOR_2D] = CreateVertexShader(vsCol, hlslVsCol);

	fsPresets_[FS_TEXTURE_COLOR_2D] = CreateFragmentShader(fsTexCol, hlslFsTexCol);
	fsPresets_[FS_COLOR_2D] = CreateFragmentShader(fsCol, hlslFsCol);

	ssPresets_[SS_TEXTURE_COLOR_2D] = CreateShaderSet(vsPresets_[VS_TEXTURE_COLOR_2D], fsPresets_[FS_TEXTURE_COLOR_2D]);
	ssPresets_[SS_COLOR_2D] = CreateShaderSet(vsPresets_[VS_COLOR_2D], fsPresets_[FS_COLOR_2D]);
}

Thin3DContext::~Thin3DContext() {
	for (int i = 0; i < VS_MAX_PRESET; i++) {
		vsPresets_[i]->Release();
	}
	for (int i = 0; i < FS_MAX_PRESET; i++) {
		fsPresets_[i]->Release();
	}
	for (int i = 0; i < BS_MAX_PRESET; i++) {
		bsPresets_[i]->Release();
	}
	for (int i = 0; i < SS_MAX_PRESET; i++) {
		ssPresets_[i]->Release();
	}
}