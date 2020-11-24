#pragma once

#include "Common/System/Display.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/UI.h"

#include "Common/LogManager.h"
#include "UI/MiscScreens.h"
#include "Common/GPU/thin3d.h"

class GPUDriverTestScreen : public UIDialogScreenWithBackground {
public:
	GPUDriverTestScreen();
	~GPUDriverTestScreen();

	void CreateViews() override;
	void render() override;

private:
	void DiscardTest();

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

	Draw::SamplerState *samplerNearest_ = nullptr;
	UI::TabHolder *tabHolder_ = nullptr;
};
