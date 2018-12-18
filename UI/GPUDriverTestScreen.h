#pragma once

#include <algorithm>

#include "base/display.h"
#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "ui/ui.h"

#include "Common/LogManager.h"
#include "UI/MiscScreens.h"
#include "thin3d/thin3d.h"

class GPUDriverTestScreen : public UIDialogScreenWithBackground {
public:
	GPUDriverTestScreen();
	~GPUDriverTestScreen();

	void CreateViews() override;
	void render() override;

private:
	void DiscardTest();

	Draw::ShaderModule *discard_ = nullptr;
	Draw::Pipeline *discardWriteDepthStencil_ = nullptr;
	Draw::Pipeline *drawTestStencilEqual_ = nullptr;
	Draw::Pipeline *drawTestStencilNotEqual_ = nullptr;
	Draw::Pipeline *drawTestDepthLessEqual_ = nullptr;
	Draw::Pipeline *drawTestDepthGreater_ = nullptr;
	Draw::SamplerState *samplerNearest_ = nullptr;
	UI::TabHolder *tabHolder_ = nullptr;
};
