// See header for documentation.

#include <cmath>
#include <cstring>

#include "Common/UI/UI.h"
#include "Common/UI/Context.h"
#include "Common/Render/DrawBuffer.h"

// TODO: UI should probably not own these.
DrawBuffer ui_draw2d;
DrawBuffer ui_draw2d_front;

void UIBegin(Draw::Pipeline *pipeline) {
	ui_draw2d.Begin(pipeline);
}

void UIFlush() {
	ui_draw2d.Flush();
}
