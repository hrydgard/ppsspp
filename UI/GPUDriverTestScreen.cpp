#include "GPUDriverTestScreen.h"
#include "i18n/i18n.h"
#include "ui/view.h"

static const std::vector<Draw::ShaderSource> fsDiscard = {
	{Draw::ShaderLanguage::GLSL_ES_200,
	R"(varying vec4 oColor0;
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
	R"(layout(location = 0) in vec4 oColor0;
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
	if (discardWriteStencil_)
		discardWriteStencil_->Release();
	if (drawTestStencil_)
		drawTestStencil_->Release();
	if (drawTestDepth_)
		drawTestDepth_->Release();
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

	auto tabs = new TabHolder(UI::ORIENT_HORIZONTAL, 30.0f);
	root_ = tabs;
	tabs->AddTab("Discard", new LinearLayout(UI::ORIENT_VERTICAL));
}

void GPUDriverTestScreen::render() {
	using namespace Draw;
	UIScreen::render();

	if (!discardWriteStencil_) {
		DrawContext *draw = screenManager()->getDrawContext();

		// Create the special shader module.

		discard_ = CreateShader(draw, Draw::ShaderStage::FRAGMENT, fsDiscard);

		InputLayout *inputLayout = ui_draw2d.CreateInputLayout(draw);
		BlendState *blendOff = draw->CreateBlendState({false, 0xF});

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
		dsDesc.front.reference = 0xFF;
		dsDesc.front.writeMask = 0xFF;
		dsDesc.back = dsDesc.front;
		DepthStencilState *depthStencilWrite = draw->CreateDepthStencilState(dsDesc);

		dsDesc.depthCompare = Comparison::ALWAYS;
		dsDesc.front.compareOp = Comparison::EQUAL;
		DepthStencilState *stencilTestEqual = draw->CreateDepthStencilState(dsDesc);

		dsDesc.depthCompare = Comparison::LESS_EQUAL;
		dsDesc.front.compareOp = Comparison::ALWAYS;
		DepthStencilState *depthTestEqual = draw->CreateDepthStencilState(dsDesc);

		RasterState *rasterNoCull = draw->CreateRasterState({});

		PipelineDesc discardDesc{
			Primitive::TRIANGLE_LIST,
			{ draw->GetVshaderPreset(VS_TEXTURE_COLOR_2D), discard_ },
			inputLayout, depthStencilWrite, blendOff, rasterNoCull, &vsColBufDesc,
		};
		discardWriteStencil_ = draw->CreateGraphicsPipeline(discardDesc);

		PipelineDesc testStencilDesc{
			Primitive::TRIANGLE_LIST,
			{ draw->GetVshaderPreset(VS_TEXTURE_COLOR_2D), draw->GetFshaderPreset(FS_TEXTURE_COLOR_2D) },
			inputLayout, stencilTestEqual, blendOff, rasterNoCull, &vsColBufDesc,
		};
		drawTestStencil_ = draw->CreateGraphicsPipeline(testStencilDesc);

		PipelineDesc testDepthDesc{
			Primitive::TRIANGLE_LIST,
			{draw->GetVshaderPreset(VS_TEXTURE_COLOR_2D), draw->GetFshaderPreset(FS_TEXTURE_COLOR_2D)},
			inputLayout, stencilTestEqual, blendOff, rasterNoCull, &vsColBufDesc,
		};
		drawTestDepth_ = draw->CreateGraphicsPipeline(testDepthDesc);

		inputLayout->Release();
		blendOff->Release();
		depthStencilWrite->Release();
		stencilTestEqual->Release();
		depthTestEqual->Release();
		rasterNoCull->Release();

		SamplerStateDesc nearestDesc{};
		samplerNearest_ = draw->CreateSamplerState(nearestDesc);
	}

	UIContext &dc = *screenManager()->getUIContext();
	const Bounds &bounds = dc.GetBounds();

	const char *testNames[] = {"Normal", "Z test", "Stencil test"};

	const int numTests = ARRAY_SIZE(testNames);

	uint32_t textColorOK = 0xFF30FF30;
	uint32_t textColorBAD = 0xFF3030FF;
	uint32_t bgColorOK = 0xFF106010;
	uint32_t bgColorBAD = 0xFF101060;

	// Don't want any fancy font texture stuff going on here, so use FLAG_DYNAMIC_ASCII everywhere!

	float testW = 200.f;
	float padding = 20.0f;
	UI::Style style = dc.theme->itemStyle;

	float y = 100;
	float x = dc.GetBounds().centerX() - ((float)numTests * testW + (float)(numTests - 1) * padding) / 2.0f;
	for (int i = 0; i < 3; i++) {
		dc.Begin();
		dc.SetFontScale(1.0f, 1.0f);
		Bounds bounds = {x - testW / 2, y + 40, testW, 70};
		dc.DrawText(testNames[i], bounds.x, y, style.fgColor, FLAG_DYNAMIC_ASCII);

		dc.FillRect(UI::Drawable(bgColorOK), bounds);
		// test bounds
		dc.Flush();

		dc.BeginPipeline(discardWriteStencil_, samplerNearest_);

		dc.DrawTextRect("TEST OK", bounds, textColorBAD, ALIGN_HCENTER | ALIGN_VCENTER);
		dc.Flush();

		dc.BeginPipeline(drawTestStencil_, samplerNearest_);
		dc.FillRect(UI::Drawable(textColorOK), bounds);
		dc.Flush();

		// Methodology:
		// 1. Draw text in red, writing to stencil.
		// 2. Use stencil test equality to make it green.

		x += testW + padding;
	}
	dc.SetFontScale(1.0f, 1.0f);
	dc.Flush();
}
