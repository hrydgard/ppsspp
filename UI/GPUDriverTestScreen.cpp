#include "GPUDriverTestScreen.h"
#include "Common/Data/Text/I18n.h"
#include "Common/UI/UI.h"
#include "Common/UI/View.h"
#include "Common/GPU/Shader.h"
#include "Common/GPU/ShaderWriter.h"

const uint32_t textColorOK = 0xFF30FF30;
const uint32_t textColorBAD = 0xFF3030FF;
const uint32_t bgColorOK = 0xFF106010;
const uint32_t bgColorBAD = 0xFF101060;

// TODO: One day, port these to use ShaderWriter.

static const std::vector<Draw::ShaderSource> fsDiscard = {
	{ShaderLanguage::GLSL_1xx,
	R"(
	#ifdef GL_ES
	precision lowp float;
	#endif
	#if __VERSION__ >= 130
	#define varying in
	#define gl_FragColor fragColor0
	out vec4 fragColor0;
	#endif
	varying vec4 oColor0;
	varying vec2 oTexCoord0;
	uniform sampler2D Sampler0;
	void main() {
	#if __VERSION__ >= 130
		vec4 color = texture(Sampler0, oTexCoord0) * oColor0;
	#else
		vec4 color = texture2D(Sampler0, oTexCoord0) * oColor0;
	#endif
		if (color.a <= 0.0)
			discard;
		gl_FragColor = color;
	})"
	},
	{ShaderLanguage::GLSL_VULKAN,
	R"(#version 450
	#extension GL_ARB_separate_shader_objects : enable
	#extension GL_ARB_shading_language_420pack : enable
	layout(location = 0) in vec4 oColor0;
	layout(location = 1) in vec2 oTexCoord0;
	layout(location = 0) out vec4 fragColor0;
	layout(set = 0, binding = 1) uniform sampler2D Sampler0;
	void main() {
		vec4 color = texture(Sampler0, oTexCoord0) * oColor0;
		if (color.a <= 0.0)
			discard;
		fragColor0 = color;
	})"
	},
};

static const std::vector<Draw::ShaderSource> fsAdrenoLogicTest = {
	{ShaderLanguage::GLSL_1xx,
	R"(
	#ifdef GL_ES
	precision lowp float;
	#endif
	#if __VERSION__ >= 130
	#define varying in
	#define gl_FragColor fragColor0
	out vec4 fragColor0;
	#endif
	varying vec4 oColor0;
	varying vec2 oTexCoord0;
	uniform sampler2D Sampler0;
	void main() {
	#if __VERSION__ >= 130
		vec4 color = (texture(Sampler0, oTexCoord0) * oColor0).aaaa;
	#else
		vec4 color = (texture2D(Sampler0, oTexCoord0) * oColor0).aaaa;
	#endif
		color *= 2.0;
		if (color.r < 0.002 && color.g < 0.002 && color.b < 0.002) discard;
		gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
	})"
	},
	{ShaderLanguage::GLSL_VULKAN,
	R"(#version 450
	#extension GL_ARB_separate_shader_objects : enable
	#extension GL_ARB_shading_language_420pack : enable
	layout(location = 0) in vec4 oColor0;
	layout(location = 1) in highp vec2 oTexCoord0;
	layout(location = 0) out vec4 fragColor0;
	layout(set = 0, binding = 1) uniform sampler2D Sampler0;
	void main() {
		vec4 v = texture(Sampler0, oTexCoord0).aaaa * oColor0;
		if (v.r < 0.2 && v.g < 0.2 && v.b < 0.2) discard;
		fragColor0 = vec4(0.0, 1.0, 0.0, 1.0);
	})"
	},
};

static const std::vector<Draw::ShaderSource> vsAdrenoLogicTest = {
	{ GLSL_1xx,
	"#if __VERSION__ >= 130\n"
	"#define attribute in\n"
	"#define varying out\n"
	"#endif\n"
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
	"}\n"
	},
	{ ShaderLanguage::GLSL_VULKAN,
	"#version 450\n"
	"#extension GL_ARB_separate_shader_objects : enable\n"
	"#extension GL_ARB_shading_language_420pack : enable\n"
	"layout (std140, set = 0, binding = 0) uniform bufferVals {\n"
	"    mat4 WorldViewProj;\n"
	"} myBufferVals;\n"
	"layout (location = 0) in vec4 pos;\n"
	"layout (location = 1) in vec4 inColor;\n"
	"layout (location = 3) in vec2 inTexCoord;\n"
	"layout (location = 0) out vec4 outColor;\n"
	"layout (location = 1) out highp vec2 outTexCoord;\n"
	"out gl_PerVertex { vec4 gl_Position; };\n"
	"void main() {\n"
	"   outColor = inColor;\n"
	"   outTexCoord = inTexCoord;\n"
	"   gl_Position = myBufferVals.WorldViewProj * pos;\n"
	"}\n"
	}
};

static const std::vector<Draw::ShaderSource> fsFlat = {
	{ShaderLanguage::GLSL_3xx,
	"#ifdef GL_ES\n"
	"precision lowp float;\n"
	"precision highp int;\n"
	"#endif\n"
	"uniform sampler2D Sampler0;\n"
	"flat in lowp vec4 oColor0;\n"
	"in mediump vec3 oTexCoord0;\n"
	"out vec4 fragColor0;\n"
	"void main() {\n"
	"  vec4 t = texture(Sampler0, oTexCoord0.xy);\n"
	"  vec4 p = oColor0;\n"
	"  vec4 v = p * t;\n"
	"  fragColor0 = v;\n"
	"}\n"
	},
	{ShaderLanguage::GLSL_1xx,
	"#ifdef GL_ES\n"
	"precision lowp float;\n"
	"#endif\n"
	"#if __VERSION__ >= 130\n"
	"#define varying in\n"
	"#define texture2D texture\n"
	"#define gl_FragColor fragColor0\n"
	"out vec4 fragColor0;\n"
	"#endif\n"
	"varying vec4 oColor0;\n"
	"varying vec2 oTexCoord0;\n"
	"uniform sampler2D Sampler0;\n"
	"void main() { gl_FragColor = texture2D(Sampler0, oTexCoord0) * oColor0; }\n"
	},
	{ShaderLanguage::GLSL_VULKAN,
	"#version 450\n"
	"#extension GL_ARB_separate_shader_objects : enable\n"
	"#extension GL_ARB_shading_language_420pack : enable\n"
	"layout(location = 0) flat in lowp vec4 oColor0;\n"
	"layout(location = 1) in highp vec2 oTexCoord0;\n"
	"layout(location = 0) out vec4 fragColor0;\n"
	"layout(set = 0, binding = 1) uniform sampler2D Sampler0;\n"
	"void main() { fragColor0 = texture(Sampler0, oTexCoord0) * oColor0; }\n"
	}
};

static const std::vector<Draw::ShaderSource> vsFlat = {
	{ GLSL_3xx,
	"in vec3 Position;\n"
	"in vec2 TexCoord0;\n"
	"in lowp vec4 Color0;\n"
	"uniform mat4 WorldViewProj;\n"
	"flat out lowp vec4 oColor0;\n"
	"out mediump vec3 oTexCoord0;\n"
	"void main() {\n"
	"  oTexCoord0 = vec3(TexCoord0, 1.0);\n"
	"  oColor0 = Color0;\n"
	"  vec4 outPos = WorldViewProj * vec4(Position, 1.0);\n"
	"  gl_Position = outPos;\n"
	"}\n"
	},
	{ GLSL_1xx,  // Doesn't actually repro the problem since flat support isn't guaranteed
	"#if __VERSION__ >= 130\n"
	"#define attribute in\n"
	"#define varying out\n"
	"#endif\n"
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
	"}\n"
	},
	{ ShaderLanguage::GLSL_VULKAN,
	"#version 450\n"
	"#extension GL_ARB_separate_shader_objects : enable\n"
	"#extension GL_ARB_shading_language_420pack : enable\n"
	"layout (std140, set = 0, binding = 0) uniform bufferVals {\n"
	"    mat4 WorldViewProj;\n"
	"} myBufferVals;\n"
	"layout (location = 0) in vec4 pos;\n"
	"layout (location = 1) in vec4 inColor;\n"
	"layout (location = 3) in vec2 inTexCoord;\n"
	"layout (location = 0) flat out lowp vec4 outColor;\n"
	"layout (location = 1) out highp vec2 outTexCoord;\n"
	"out gl_PerVertex { vec4 gl_Position; };\n"
	"void main() {\n"
	"   outColor = inColor;\n"
	"   outTexCoord = inTexCoord;\n"
	"   gl_Position = myBufferVals.WorldViewProj * pos;\n"
	"}\n"
	}
};

static_assert(Draw::SEM_TEXCOORD0 == 3, "Semantic shader hardcoded in glsl above.");

GPUDriverTestScreen::GPUDriverTestScreen() {
	using namespace Draw;
}

GPUDriverTestScreen::~GPUDriverTestScreen() {
	if (discardWriteDepthStencil_)
		discardWriteDepthStencil_->Release();
	if (discardWriteDepth_)
		discardWriteDepth_->Release();
	if (discardWriteStencil_)
		discardWriteStencil_->Release();

	if (drawTestStencilEqualDepthAlways_)
		drawTestStencilEqualDepthAlways_->Release();
	if (drawTestStencilNotEqualDepthAlways_)
		drawTestStencilNotEqualDepthAlways_->Release();
	if (drawTestStencilEqual_)
		drawTestStencilEqual_->Release();
	if (drawTestStencilNotEqual_)
		drawTestStencilNotEqual_->Release();
	if (drawTestStencilAlwaysDepthLessEqual_)
		drawTestStencilAlwaysDepthLessEqual_->Release();
	if (drawTestStencilAlwaysDepthGreater_)
		drawTestStencilAlwaysDepthGreater_->Release();
	if (drawTestDepthLessEqual_)
		drawTestDepthLessEqual_->Release();
	if (drawTestDepthGreater_)
		drawTestDepthGreater_->Release();

	if (discardFragShader_)
		discardFragShader_->Release();

	// Shader test
	if (adrenoLogicDiscardPipeline_)
		adrenoLogicDiscardPipeline_->Release();
	if (flatShadingPipeline_)
		flatShadingPipeline_->Release();

	if (adrenoLogicDiscardFragShader_)
		adrenoLogicDiscardFragShader_->Release();
	if (adrenoLogicDiscardVertShader_)
		adrenoLogicDiscardVertShader_->Release();
	if (flatFragShader_)
		flatFragShader_->Release();
	if (flatVertShader_)
		flatVertShader_->Release();

	if (samplerNearest_)
		samplerNearest_->Release();
}

void GPUDriverTestScreen::CreateViews() {
	using namespace Draw;

	if (!samplerNearest_) {
		DrawContext *draw = screenManager()->getDrawContext();
		SamplerStateDesc nearestDesc{};
		samplerNearest_ = draw->CreateSamplerState(nearestDesc);
	}

	// Don't bother with views for now.
	using namespace UI;
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto cr = GetI18NCategory(I18NCat::PSPCREDITS);

	AnchorLayout *anchor = new AnchorLayout();
	root_ = anchor;

	tabHolder_ = new TabHolder(ORIENT_HORIZONTAL, 30.0f, new AnchorLayoutParams(FILL_PARENT, FILL_PARENT, false));
	anchor->Add(tabHolder_);
	tabHolder_->AddTab("Discard", new LinearLayout(ORIENT_VERTICAL));
	tabHolder_->AddTab("Shader", new LinearLayout(ORIENT_VERTICAL));

	Choice *back = new Choice(di->T("Back"), "", false, new AnchorLayoutParams(100, WRAP_CONTENT, 10, NONE, NONE, 10));
	back->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	anchor->Add(back);
}

void GPUDriverTestScreen::DiscardTest(UIContext &dc) {
	using namespace UI;
	using namespace Draw;
	if (!discardWriteDepthStencil_) {
		DrawContext *draw = screenManager()->getDrawContext();

		// Create the special shader module.

		discardFragShader_ = CreateShader(draw, ShaderStage::Fragment, fsDiscard);

		InputLayout *inputLayout = ui_draw2d.CreateInputLayout(draw);
		BlendState *blendOff = draw->CreateBlendState({ false, 0xF });
		BlendState *blendOffNoColor = draw->CreateBlendState({ false, 0x8 });

		// Write depth, write stencil.
		DepthStencilStateDesc dsDesc{};
		dsDesc.depthTestEnabled = true;
		dsDesc.depthWriteEnabled = true;
		dsDesc.depthCompare = Comparison::ALWAYS;
		dsDesc.stencilEnabled = true;
		dsDesc.stencil.compareOp = Comparison::ALWAYS;
		dsDesc.stencil.passOp = StencilOp::REPLACE;
		dsDesc.stencil.failOp = StencilOp::REPLACE;  // These two shouldn't matter, because the test that fails is discard, not stencil.
		dsDesc.stencil.depthFailOp = StencilOp::REPLACE;
		DepthStencilState *depthStencilWrite = draw->CreateDepthStencilState(dsDesc);

		// Write only depth.
		dsDesc.stencilEnabled = false;
		DepthStencilState *depthWrite = draw->CreateDepthStencilState(dsDesc);

		// Write only stencil.
		dsDesc.stencilEnabled = true;
		dsDesc.depthTestEnabled = false;
		dsDesc.depthWriteEnabled = false;  // Just in case the driver is crazy. when test is enabled, though, this should be ignored.
		DepthStencilState *stencilWrite = draw->CreateDepthStencilState(dsDesc);

		// Now for the shaders that read depth and/or stencil.

		dsDesc.depthTestEnabled = true;
		dsDesc.stencilEnabled = true;
		dsDesc.depthCompare = Comparison::ALWAYS;
		dsDesc.stencil.compareOp = Comparison::EQUAL;
		dsDesc.stencil.failOp = StencilOp::KEEP;
		dsDesc.stencil.depthFailOp = StencilOp::KEEP;
		DepthStencilState *stencilEqualDepthAlways = draw->CreateDepthStencilState(dsDesc);

		dsDesc.depthTestEnabled = false;
		dsDesc.stencil.compareOp = Comparison::EQUAL;
		DepthStencilState *stencilEqual = draw->CreateDepthStencilState(dsDesc);

		dsDesc.depthTestEnabled = true;
		dsDesc.depthCompare = Comparison::ALWAYS;
		dsDesc.stencil.compareOp = Comparison::NOT_EQUAL;
		DepthStencilState *stenciNotEqualDepthAlways = draw->CreateDepthStencilState(dsDesc);

		dsDesc.depthTestEnabled = false;
		dsDesc.stencil.compareOp = Comparison::NOT_EQUAL;
		DepthStencilState *stencilNotEqual = draw->CreateDepthStencilState(dsDesc);

		dsDesc.stencilEnabled = true;
		dsDesc.depthTestEnabled = true;
		dsDesc.stencil.compareOp = Comparison::ALWAYS;
		dsDesc.depthCompare = Comparison::LESS_EQUAL;
		DepthStencilState *stencilAlwaysDepthTestLessEqual = draw->CreateDepthStencilState(dsDesc);
		dsDesc.depthCompare = Comparison::GREATER;
		DepthStencilState *stencilAlwaysDepthTestGreater = draw->CreateDepthStencilState(dsDesc);

		dsDesc.stencilEnabled = false;
		dsDesc.depthTestEnabled = true;
		dsDesc.depthCompare = Comparison::LESS_EQUAL;
		DepthStencilState *depthTestLessEqual = draw->CreateDepthStencilState(dsDesc);
		dsDesc.depthCompare = Comparison::GREATER;
		DepthStencilState *depthTestGreater = draw->CreateDepthStencilState(dsDesc);

		RasterState *rasterNoCull = draw->CreateRasterState({});

		PipelineDesc discardDesc{
			Primitive::TRIANGLE_LIST,
			{ draw->GetVshaderPreset(VS_TEXTURE_COLOR_2D), discardFragShader_ },
			inputLayout, depthStencilWrite, blendOffNoColor, rasterNoCull, &vsColBufDesc,
		};
		discardWriteDepthStencil_ = draw->CreateGraphicsPipeline(discardDesc, "test");
		discardDesc.depthStencil = depthWrite;
		discardWriteDepth_ = draw->CreateGraphicsPipeline(discardDesc, "test");
		discardDesc.depthStencil = stencilWrite;
		discardWriteStencil_ = draw->CreateGraphicsPipeline(discardDesc, "test");

		PipelineDesc testDesc{
			Primitive::TRIANGLE_LIST,
			{ draw->GetVshaderPreset(VS_TEXTURE_COLOR_2D), draw->GetFshaderPreset(FS_TEXTURE_COLOR_2D) },
			inputLayout, stencilEqual, blendOff, rasterNoCull, &vsColBufDesc,
		};
		drawTestStencilEqual_ = draw->CreateGraphicsPipeline(testDesc, "test");

		testDesc.depthStencil = stencilEqualDepthAlways;
		drawTestStencilEqualDepthAlways_ = draw->CreateGraphicsPipeline(testDesc, "test");

		testDesc.depthStencil = stencilNotEqual;
		drawTestStencilNotEqual_ = draw->CreateGraphicsPipeline(testDesc, "test");

		testDesc.depthStencil = stenciNotEqualDepthAlways;
		drawTestStencilNotEqualDepthAlways_ = draw->CreateGraphicsPipeline(testDesc, "test");

		testDesc.depthStencil = stencilAlwaysDepthTestGreater;
		drawTestStencilAlwaysDepthGreater_ = draw->CreateGraphicsPipeline(testDesc, "test");

		testDesc.depthStencil = stencilAlwaysDepthTestLessEqual;
		drawTestStencilAlwaysDepthLessEqual_ = draw->CreateGraphicsPipeline(testDesc, "test");

		testDesc.depthStencil = depthTestGreater;
		drawTestDepthGreater_ = draw->CreateGraphicsPipeline(testDesc, "test");

		testDesc.depthStencil = depthTestLessEqual;
		drawTestDepthLessEqual_ = draw->CreateGraphicsPipeline(testDesc, "test");

		inputLayout->Release();
		blendOff->Release();
		depthStencilWrite->Release();
		stencilEqual->Release();
		stencilNotEqual->Release();
		stencilEqualDepthAlways->Release();
		stenciNotEqualDepthAlways->Release();
		stencilAlwaysDepthTestLessEqual->Release();
		stencilAlwaysDepthTestGreater->Release();
		depthTestLessEqual->Release();
		depthTestGreater->Release();
		rasterNoCull->Release();
	}

	Draw::DrawContext *draw = dc.GetDrawContext();

	static const char * const writeModeNames[] = { "Stencil+Depth", "Stencil", "Depth" };
	Pipeline *writePipelines[] = { discardWriteDepthStencil_, discardWriteStencil_, discardWriteDepth_ };
	const int numWriteModes = ARRAY_SIZE(writeModeNames);

	static const char * const testNames[] = { "Stenc", "Stenc+DepthA", "Depth", "StencA+Depth" };
	Pipeline *testPipeline1[] = { drawTestStencilEqual_, drawTestStencilEqualDepthAlways_, drawTestDepthLessEqual_, drawTestStencilAlwaysDepthLessEqual_ };
	Pipeline *testPipeline2[] = { drawTestStencilNotEqual_, drawTestStencilNotEqualDepthAlways_, drawTestDepthGreater_, drawTestStencilAlwaysDepthGreater_ };
	const int numTests = ARRAY_SIZE(testNames);

	static const bool validCombinations[numWriteModes][numTests] = {
		{true, true, true, true},
		{true, true, false, false},
		{false, false, true, true},
	};

	// Don't want any fancy font texture stuff going on here, so use FLAG_DYNAMIC_ASCII everywhere!

	// We draw the background at Z=0.5 and the text at Z=0.9.

	// Then we draw a rectangle with a depth test or stencil test that should mask out the text.
	// Plus a second rectangle with the opposite test.

	// If everything is OK, both the background and the text should be OK.

	Bounds layoutBounds = dc.GetLayoutBounds();

	dc.Begin();
	dc.SetFontScale(1.0f, 1.0f);
	std::string apiName = screenManager()->getDrawContext()->GetInfoString(InfoField::APINAME);
	std::string vendor = screenManager()->getDrawContext()->GetInfoString(InfoField::VENDORSTRING);
	std::string driver = screenManager()->getDrawContext()->GetInfoString(InfoField::DRIVER);
	dc.DrawText(apiName, layoutBounds.centerX(), 20, 0xFFFFFFFF, ALIGN_CENTER);
	dc.DrawText(vendor, layoutBounds.centerX(), 60, 0xFFFFFFFF, ALIGN_CENTER);
	dc.DrawText(driver, layoutBounds.centerX(), 100, 0xFFFFFFFF, ALIGN_CENTER);
	dc.Flush();

	float testW = 170.f;
	float padding = 20.0f;
	UI::Style style = dc.theme->itemStyle;

	float y = 150;
	for (int j = 0; j < numWriteModes; j++, y += 120.f + padding) {
		float x = layoutBounds.x + (layoutBounds.w - (float)numTests * testW - (float)(numTests - 1) * padding) / 2.0f;
		dc.Begin();
		dc.DrawText(writeModeNames[j], layoutBounds.x + padding, y + 40, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII);
		dc.Flush();
		for (int i = 0; i < numTests; i++, x += testW + padding) {
			if (!validCombinations[j][i])
				continue;
			dc.Begin();
			Bounds bounds = { x, y + 40, testW, 70 };
			dc.DrawText(testNames[i], bounds.x, y, style.fgColor, FLAG_DYNAMIC_ASCII);
			dc.Flush();

			dc.BeginPipeline(writePipelines[j], samplerNearest_);
			// Draw the rectangle with stencil value 0, depth 0.1f and the text with stencil 0xFF, depth 0.9. Then set 0xFF as the stencil value and draw the rectangles at depth 0.5.

			draw->SetStencilParams(0, 0xFF, 0xFF);
			dc.SetCurZ(0.1f);
			dc.FillRect(UI::Drawable(bgColorBAD), bounds);
			// test bounds
			dc.Flush();

			draw->SetStencilParams(0xff, 0xFF, 0xFF);
			dc.SetCurZ(0.9f);
			dc.DrawTextRect("TEST OK", bounds, textColorBAD, ALIGN_HCENTER | ALIGN_VCENTER | FLAG_DYNAMIC_ASCII);
			dc.Flush();

			// Draw rectangle that should result in the text
			dc.BeginPipeline(testPipeline1[i], samplerNearest_);
			draw->SetStencilParams(0xff, 0, 0xFF);
			dc.SetCurZ(0.5f);
			dc.FillRect(UI::Drawable(textColorOK), bounds);
			dc.Flush();

			// Draw rectangle that should result in the bg
			dc.BeginPipeline(testPipeline2[i], samplerNearest_);
			draw->SetStencilParams(0xff, 0, 0xFF);
			dc.SetCurZ(0.5f);
			dc.FillRect(UI::Drawable(bgColorOK), bounds);
			dc.Flush();
		}
	}
	dc.Flush();
}

void GPUDriverTestScreen::ShaderTest(UIContext &dc) {
	using namespace Draw;

	Draw::DrawContext *draw = dc.GetDrawContext();

	if (!adrenoLogicDiscardPipeline_) {
		adrenoLogicDiscardFragShader_ = CreateShader(draw, ShaderStage::Fragment, fsAdrenoLogicTest);
		adrenoLogicDiscardVertShader_ = CreateShader(draw, ShaderStage::Vertex, vsAdrenoLogicTest);

		flatFragShader_ = CreateShader(draw, ShaderStage::Fragment, fsFlat);
		flatVertShader_ = CreateShader(draw, ShaderStage::Vertex, vsFlat);

		InputLayout *inputLayout = ui_draw2d.CreateInputLayout(draw);
		// No blending used.
		BlendState *blendOff = draw->CreateBlendState({ false, 0xF });

		// No depth or stencil, we only use discard in this test.
		DepthStencilStateDesc dsDesc{};
		dsDesc.depthTestEnabled = false;
		dsDesc.depthWriteEnabled = false;
		DepthStencilState *depthStencilOff = draw->CreateDepthStencilState(dsDesc);

		RasterState *rasterNoCull = draw->CreateRasterState({});

		PipelineDesc adrenoLogicDiscardDesc{
			Primitive::TRIANGLE_LIST,
			{ adrenoLogicDiscardVertShader_, adrenoLogicDiscardFragShader_ },
			inputLayout, depthStencilOff, blendOff, rasterNoCull, &vsColBufDesc,
		};
		adrenoLogicDiscardPipeline_ = draw->CreateGraphicsPipeline(adrenoLogicDiscardDesc, "test");

		PipelineDesc flatDesc{
			Primitive::TRIANGLE_LIST,
			{ flatVertShader_, flatFragShader_ },
			inputLayout, depthStencilOff, blendOff, rasterNoCull, &vsColBufDesc,
		};
		flatShadingPipeline_ = draw->CreateGraphicsPipeline(flatDesc, "test");

		inputLayout->Release();
		blendOff->Release();
		depthStencilOff->Release();
		rasterNoCull->Release();
	}

	Bounds layoutBounds = dc.GetLayoutBounds();

	dc.Begin();
	dc.SetFontScale(1.0f, 1.0f);
	std::string apiName = screenManager()->getDrawContext()->GetInfoString(InfoField::APINAME);
	std::string vendor = screenManager()->getDrawContext()->GetInfoString(InfoField::VENDORSTRING);
	std::string driver = screenManager()->getDrawContext()->GetInfoString(InfoField::DRIVER);
	dc.DrawText(apiName, layoutBounds.centerX(), 20, 0xFFFFFFFF, ALIGN_CENTER);
	dc.DrawText(vendor, layoutBounds.centerX(), 60, 0xFFFFFFFF, ALIGN_CENTER);
	dc.DrawText(driver, layoutBounds.centerX(), 100, 0xFFFFFFFF, ALIGN_CENTER);
	dc.Flush();

	float y = layoutBounds.y + 150;
	float x = layoutBounds.x + 10;
	dc.Begin();
	dc.DrawText("Adreno logic", x, y, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII);
	dc.Flush();

	float testW = 170.f;

	Bounds bounds = { x + 200, y, testW, 70 };

	// Draw rectangle that should result in the bg
	dc.Begin();
	dc.FillRect(UI::Drawable(bgColorOK), bounds);
	dc.Flush();

	// Draw text on it using the shader.
	dc.BeginPipeline(adrenoLogicDiscardPipeline_, samplerNearest_);
	dc.DrawTextRect("TEST OK", bounds, textColorOK, ALIGN_HCENTER | ALIGN_VCENTER | FLAG_DYNAMIC_ASCII);
	dc.Flush();

	y += 100;

	dc.Begin();
	dc.DrawText("Flat shaded tex", x, y, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII);
	dc.DrawText("(TEST OK if logo but no gradient!)", x + 400, y, 0xFFFFFFFF, ALIGN_LEFT);
	dc.Flush();

	bounds = { x + 200, y, 100, 100 };

	// Draw rectangle that should be flat shaded
	dc.BeginPipeline(flatShadingPipeline_, samplerNearest_);
	// There is a "provoking vertex" difference here between GL and Vulkan when using flat shading. One gets one color, one gets the other.
	// However, we now use the VK_EXT_provoking_vertex extension to make it match up (and match with the PSP).
	dc.DrawImageVGradient(ImageID("I_ICON"), 0xFFFFFFFF, 0xFF808080, bounds);
	dc.Flush();

	y += 120;

	dc.Begin();
	dc.DrawText("Test done", x, y, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII);
	dc.Flush();
}

void GPUDriverTestScreen::DrawForeground(UIContext &dc) {
	switch (tabHolder_->GetCurrentTab()) {
	case 0:
		DiscardTest(dc);
		break;
	case 1:
		ShaderTest(dc);
		break;
	}
}
