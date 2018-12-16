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
	Draw::ShaderModule *discard_ = nullptr;
	Draw::Pipeline *discardWriteStencil_ = nullptr;
	Draw::Pipeline *drawTestStencil_ = nullptr;
	Draw::Pipeline *drawTestDepth_ = nullptr;
	Draw::SamplerState *samplerNearest_ = nullptr;
};
