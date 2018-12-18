#include "GPUDriverTestScreen.h"
#include "i18n/i18n.h"
#include "ui/view.h"

static const std::vector<Draw::ShaderSource> fsDiscard = {
	{Draw::ShaderLanguage::GLSL_ES_200,
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
		vec4 color = texture2D(Sampler0, oTexCoord0) * oColor0;
		if (color.a <= 0.0)
			discard;
		gl_FragColor = color;
	})"
	},
	{Draw::ShaderLanguage::GLSL_VULKAN,
	R"(#version 140
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

GPUDriverTestScreen::GPUDriverTestScreen() {
	using namespace Draw;
}

GPUDriverTestScreen::~GPUDriverTestScreen() {
	if (discardWriteDepthStencil_)
		discardWriteDepthStencil_->Release();
	if (drawTestStencilEqual_)
		drawTestStencilEqual_->Release();
	if (drawTestStencilNotEqual_)
		drawTestStencilNotEqual_->Release();
	if (drawTestDepthLessEqual_)
		drawTestDepthLessEqual_->Release();
	if (drawTestDepthGreater_)
		drawTestDepthGreater_->Release();
	if (discard_)
		discard_->Release();
	if (samplerNearest_)
		samplerNearest_->Release();
}

void GPUDriverTestScreen::CreateViews() {
	// Don't bother with views for now.
	using namespace UI;
	I18NCategory *di = GetI18NCategory("Dialog");
	I18NCategory *cr = GetI18NCategory("PSPCredits");

	AnchorLayout *anchor = new AnchorLayout();
	root_ = anchor;

	tabHolder_ = new TabHolder(ORIENT_HORIZONTAL, 30.0f, new AnchorLayoutParams(FILL_PARENT, FILL_PARENT, false));
	anchor->Add(tabHolder_);
	tabHolder_->AddTab("Discard", new LinearLayout(ORIENT_VERTICAL));

	Choice *back = new Choice(di->T("Back"), "", false, new AnchorLayoutParams(100, WRAP_CONTENT, 10, NONE, NONE, 10));
	back->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	anchor->Add(back);
}

void GPUDriverTestScreen::DiscardTest() {
	using namespace UI;
	using namespace Draw;
	if (!discardWriteDepthStencil_) {
		DrawContext *draw = screenManager()->getDrawContext();

		// Create the special shader module.

		discard_ = CreateShader(draw, Draw::ShaderStage::FRAGMENT, fsDiscard);

		InputLayout *inputLayout = ui_draw2d.CreateInputLayout(draw);
		BlendState *blendOff = draw->CreateBlendState({ false, 0xF });

		// Write depth, write stencil.
		DepthStencilStateDesc dsDesc{};
		dsDesc.depthTestEnabled = true;
		dsDesc.depthWriteEnabled = true;
		dsDesc.depthCompare = Comparison::ALWAYS;
		dsDesc.stencilEnabled = true;
		dsDesc.front.compareMask = 0xFF;
		dsDesc.front.compareOp = Comparison::ALWAYS;
		dsDesc.front.passOp = StencilOp::REPLACE;
		dsDesc.front.failOp = StencilOp::ZERO;
		dsDesc.front.depthFailOp = StencilOp::ZERO;
		dsDesc.front.writeMask = 0xFF;
		dsDesc.back = dsDesc.front;
		DepthStencilState *depthStencilWrite = draw->CreateDepthStencilState(dsDesc);

		dsDesc.depthCompare = Comparison::ALWAYS;
		dsDesc.front.compareOp = Comparison::EQUAL;
		dsDesc.back = dsDesc.front;
		DepthStencilState *stencilTestEqual = draw->CreateDepthStencilState(dsDesc);

		dsDesc.depthCompare = Comparison::ALWAYS;
		dsDesc.front.compareOp = Comparison::NOT_EQUAL;
		dsDesc.back = dsDesc.front;
		DepthStencilState *stencilTestNotEqual = draw->CreateDepthStencilState(dsDesc);

		dsDesc.depthCompare = Comparison::LESS_EQUAL;
		dsDesc.front.compareOp = Comparison::ALWAYS;
		dsDesc.back = dsDesc.front;
		DepthStencilState *depthTestLessEqual = draw->CreateDepthStencilState(dsDesc);
		dsDesc.depthCompare = Comparison::GREATER;
		DepthStencilState *depthTestGreater = draw->CreateDepthStencilState(dsDesc);

		RasterState *rasterNoCull = draw->CreateRasterState({});

		PipelineDesc discardDesc{
			Primitive::TRIANGLE_LIST,
			{ draw->GetVshaderPreset(VS_TEXTURE_COLOR_2D), discard_ },
			inputLayout, depthStencilWrite, blendOff, rasterNoCull, &vsColBufDesc,
		};
		discardWriteDepthStencil_ = draw->CreateGraphicsPipeline(discardDesc);

		PipelineDesc testDesc{
			Primitive::TRIANGLE_LIST,
			{ draw->GetVshaderPreset(VS_TEXTURE_COLOR_2D), draw->GetFshaderPreset(FS_TEXTURE_COLOR_2D) },
			inputLayout, stencilTestEqual, blendOff, rasterNoCull, &vsColBufDesc,
		};
		drawTestStencilEqual_ = draw->CreateGraphicsPipeline(testDesc);

		testDesc.depthStencil = stencilTestNotEqual;
		drawTestStencilNotEqual_ = draw->CreateGraphicsPipeline(testDesc);

		testDesc.depthStencil = depthTestGreater;
		drawTestDepthGreater_ = draw->CreateGraphicsPipeline(testDesc);

		testDesc.depthStencil = depthTestLessEqual;
		drawTestDepthLessEqual_ = draw->CreateGraphicsPipeline(testDesc);

		inputLayout->Release();
		blendOff->Release();
		depthStencilWrite->Release();
		stencilTestEqual->Release();
		stencilTestNotEqual->Release();
		depthTestLessEqual->Release();
		depthTestGreater->Release();
		rasterNoCull->Release();

		SamplerStateDesc nearestDesc{};
		samplerNearest_ = draw->CreateSamplerState(nearestDesc);
	}

	UIContext &dc = *screenManager()->getUIContext();
	Draw::DrawContext *draw = dc.GetDrawContext();
	const Bounds &bounds = dc.GetBounds();

	const char *testNames[] = { "Stencil test", "Depth test" };
	Pipeline *testPipeline1[] = { drawTestStencilEqual_, drawTestDepthLessEqual_ };
	Pipeline *testPipeline2[] = { drawTestStencilNotEqual_, drawTestDepthGreater_ };

	const int numTests = ARRAY_SIZE(testNames);

	uint32_t textColorOK = 0xFF30FF30;
	uint32_t textColorBAD = 0xFF3030FF;
	uint32_t bgColorOK = 0xFF106010;
	uint32_t bgColorBAD = 0xFF101060;

	// Don't want any fancy font texture stuff going on here, so use FLAG_DYNAMIC_ASCII everywhere!

	// We draw the background at Z=0.5 and the text at Z=0.9.

	// Then we draw a rectangle with a depth test or stencil test that should mask out the text.
	// Plus a second rectangle with the opposite test.

	// If everything is OK, both the background and the text should be OK.

	float testW = 200.f;
	float padding = 20.0f;
	UI::Style style = dc.theme->itemStyle;

	float y = 100;
	float x = (dc.GetBounds().w - (float)numTests * testW - (float)(numTests - 1) * padding) / 2.0f;
	for (int i = 0; i < numTests; i++) {
		dc.Begin();
		dc.SetFontScale(1.0f, 1.0f);
		Bounds bounds = { x, y + 40, testW, 70 };
		dc.DrawText(testNames[i], bounds.x, y, style.fgColor, FLAG_DYNAMIC_ASCII);
		dc.Flush();

		dc.BeginPipeline(discardWriteDepthStencil_, samplerNearest_);
		// Draw the rectangle with stencil value 0, depth 0.1f and the text with stencil 0xFF, depth 0.9. Then leave 0xFF as the stencil value and draw the rectangles at depth 0.5.
		draw->SetStencilRef(0x0);
		dc.SetCurZ(0.1f);
		dc.FillRect(UI::Drawable(bgColorBAD), bounds);
		// test bounds
		dc.Flush();

		draw->SetStencilRef(0xff);
		dc.SetCurZ(0.9f);
		dc.DrawTextRect("TEST OK", bounds, textColorBAD, ALIGN_HCENTER | ALIGN_VCENTER | FLAG_DYNAMIC_ASCII);
		dc.Flush();

		// Draw rectangle that should result in the text
		dc.BeginPipeline(testPipeline1[i], samplerNearest_);
		draw->SetStencilRef(0xff);
		dc.SetCurZ(0.5f);
		dc.FillRect(UI::Drawable(textColorOK), bounds);
		dc.Flush();

		// Draw rectangle that should result in the bg
		dc.BeginPipeline(testPipeline2[i], samplerNearest_);
		draw->SetStencilRef(0xff);
		dc.SetCurZ(0.5f);
		dc.FillRect(UI::Drawable(bgColorOK), bounds);
		dc.Flush();

		x += testW + padding;
	}
	dc.SetFontScale(1.0f, 1.0f);
	dc.Flush();
}

void GPUDriverTestScreen::render() {
	using namespace Draw;
	UIScreen::render();

	if (tabHolder_->GetCurrentTab() == 0) {
		DiscardTest();
	}
}
