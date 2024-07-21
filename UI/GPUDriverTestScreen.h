#pragma once

#include "Common/System/Display.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"

#include "Common/Log.h"
#include "UI/MiscScreens.h"
#include "Common/GPU/thin3d.h"

class GPUDriverTestScreen : public UIDialogScreenWithBackground {
public:
	GPUDriverTestScreen();
	~GPUDriverTestScreen();

	void CreateViews() override;
	void DrawForeground(UIContext &dc) override;

	const char *tag() const override { return "GPUDriverTest"; }

private:
	void DiscardTest(UIContext &dc);
	void ShaderTest(UIContext &dc);

	// Common objects
	Draw::SamplerState *samplerNearest_ = nullptr;

	// Discard/depth/stencil stuff
	// ===========================

	Draw::ShaderModule *discardFragShader_ = nullptr;
	Draw::Pipeline *discardWriteDepthStencil_ = nullptr;
	Draw::Pipeline *discardWriteDepth_ = nullptr;
	Draw::Pipeline *discardWriteStencil_ = nullptr;

	// Stencil test, with and without DepthAlways
	Draw::Pipeline *drawTestStencilEqual_ = nullptr;
	Draw::Pipeline *drawTestStencilNotEqual_ = nullptr;
	Draw::Pipeline *drawTestStencilEqualDepthAlways_ = nullptr;
	Draw::Pipeline *drawTestStencilNotEqualDepthAlways_ = nullptr;

	// Depth tests with and without StencilAlways
	Draw::Pipeline *drawTestStencilAlwaysDepthLessEqual_ = nullptr;
	Draw::Pipeline *drawTestStencilAlwaysDepthGreater_ = nullptr;
	Draw::Pipeline *drawTestDepthLessEqual_ = nullptr;
	Draw::Pipeline *drawTestDepthGreater_ = nullptr;


	// Shader tests
	// ============

	Draw::Pipeline *adrenoLogicDiscardPipeline_ = nullptr;
	Draw::ShaderModule *adrenoLogicDiscardFragShader_ = nullptr;
	Draw::ShaderModule *adrenoLogicDiscardVertShader_ = nullptr;
	Draw::Pipeline *flatShadingPipeline_ = nullptr;
	Draw::ShaderModule *flatFragShader_ = nullptr;
	Draw::ShaderModule *flatVertShader_ = nullptr;

	UI::TabHolder *tabHolder_ = nullptr;
};
