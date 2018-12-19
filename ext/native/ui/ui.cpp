// See header for documentation.

#include <string>
#include <vector>
#include <cmath>
#include <cstring>

#include "base/colorutil.h"
#include "ui/ui.h"
#include "ui/ui_context.h"
#include "gfx/texture_atlas.h"
#include "gfx_es2/draw_buffer.h"

// TODO: UI should probably not own these.
DrawBuffer ui_draw2d;
DrawBuffer ui_draw2d_front;

void UIBegin(Draw::Pipeline *pipeline) {
	ui_draw2d.Begin(pipeline);
	ui_draw2d_front.Begin(pipeline);
}

void UIFlush() {
	ui_draw2d.Flush();
	ui_draw2d_front.Flush();
}
