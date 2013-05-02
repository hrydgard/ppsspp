// See header for documentation.

#include <string>
#include <vector>
#include <cmath>
#include <cstring>

#include "base/colorutil.h"
#include "ui/ui.h"
#include "ui/ui_context.h"
#include "gfx/texture.h"
#include "gfx/texture_atlas.h"
#include "gfx_es2/draw_buffer.h"

// TODO: UI should probably not own these.
DrawBuffer ui_draw2d;
DrawBuffer ui_draw2d_front;

// This one, though, is OK.
UIState uistate;
UIState savedUistate;

// Theme.
static const Atlas *themeAtlas;
static UITheme theme;

void UIInit(const Atlas *atlas, const UITheme &ui_theme) {
	ui_draw2d.SetAtlas(atlas);
	ui_draw2d_front.SetAtlas(atlas);
	themeAtlas = atlas;
	theme = ui_theme;
	memset(&uistate, 0, sizeof(uistate));
}

void UIDisableBegin()
{
	savedUistate = uistate;
	memset(&uistate, 0, sizeof(uistate));
}

void UIDisableEnd()
{
	uistate = savedUistate;
}

void UIUpdateMouse(int i, float x, float y, bool down) {
	if (down && !uistate.mousedown[i]) {
		uistate.mousepressed[i] = 1;
		uistate.mouseStartX[i] = x;
		uistate.mouseStartY[i] = y;
	} else {
		uistate.mousepressed[i] = 0;
	}
	uistate.mousex[i] = x;
	uistate.mousey[i] = y;
	uistate.mousedown[i] = down;

	if (uistate.mousedown[i])
		uistate.mouseframesdown[i]++;
	else
		uistate.mouseframesdown[i] = 0;
}

void UIReset() {
	memset(&uistate, 0, sizeof(uistate));
}

bool UIRegionHit(int i, int x, int y, int w, int h, int margin) {
	// Input handling
	if (uistate.mousex[i] < x - margin ||
			uistate.mousey[i] < y - margin ||
			uistate.mousex[i] >= x + w + margin ||
			uistate.mousey[i] >= y + h + margin) {
		return false;
	} else {
		return true;
	}
}

void UIBegin(const GLSLProgram *shader) {
	for (int i = 0; i < MAX_POINTERS; i++)
		uistate.hotitem[i] = 0;
	ui_draw2d.Begin(shader);
	ui_draw2d_front.Begin(shader);
}

void UIFlush() {
	ui_draw2d.Flush();
	ui_draw2d_front.Flush();
}


void UIEnd() {
	for (int i = 0; i < MAX_POINTERS; i++) {
		if (uistate.mousedown[i] == 0) {
			uistate.activeitem[i] = 0;
		} else {
			if (uistate.activeitem[i] == 0) {
				uistate.activeitem[i] = -1;
			}
		}
	}
	ui_draw2d.End();
	ui_draw2d_front.End();

	if (uistate.ui_tick > 0)
		uistate.ui_tick--;
	ui_draw2d.Flush();
	ui_draw2d_front.Flush();
}

void UIText(int x, int y, const char *text, uint32_t color, float scale, int align) {
	UIText(theme.uiFont, x, y, text, color, scale, align);
}

void UIText(int font, int x, int y, const char *text, uint32_t color, float scale, int align) {
	ui_draw2d.SetFontScale(scale, scale);
	ui_draw2d.DrawTextShadow(font, text, x, y, color, align);
	ui_draw2d.SetFontScale(1.0f, 1.0f);
}

void UIText(int font, const LayoutManager &layout, const char *text, uint32_t color, float scale, int align)
{
	ui_draw2d.SetFontScale(scale, scale);
	float w, h;
	ui_draw2d.MeasureText(font, text, &w, &h);
	float x, y;
	layout.GetPos(&w, &h, &x, &y);
	UIText(font, x, y, text, color, scale, 0);
	ui_draw2d.SetFontScale(1.0f, 1.0f);
}

int UIButton(int id, const LayoutManager &layout, float w, float h, const char *text, int button_align) {
	if (h == 0.0f)
		h = themeAtlas->images[theme.buttonImage].h;

	float x, y;
	layout.GetPos(&w, &h, &x, &y);

	if (button_align & ALIGN_HCENTER) x -= w / 2;
	if (button_align & ALIGN_VCENTER) y -= h / 2;
	if (button_align & ALIGN_RIGHT) x -= w;
	if (button_align & ALIGN_BOTTOM) y -= h;

	int txOffset = 0;

	int clicked = 0;
	for (int i = 0; i < MAX_POINTERS; i++) {
		// Check whether the button should be hot, use a generous margin for touch ease
		if (UIRegionHit(i, x, y, w, h, 8)) {
			uistate.hotitem[i] = id;
			if (uistate.activeitem[i] == 0 && uistate.mousedown[i]) {
				uistate.activeitem[i] = id;
			}
		}

		if (uistate.hotitem[i] == id) {
			if (uistate.activeitem[i] == id) {
				// Button is both 'hot' and 'active'
				txOffset = 2;
			} else {
				// Button is merely 'hot'
			}
		} else {
			// button is not hot, but it may be active
		}

		// If button is hot and active, but mouse button is not
		// down, the user must have clicked the button.
		if (uistate.mousedown[i] == 0 &&
				uistate.hotitem[i] == id &&
				uistate.activeitem[i] == id) {
			clicked = 1;
		}
	}

	// Render button

	if (h == themeAtlas->images[theme.buttonImage].h)
		ui_draw2d.DrawImage2GridH((txOffset && theme.buttonSelected) ? theme.buttonSelected : theme.buttonImage, x, y, x + w);
	else
		ui_draw2d.DrawImage4Grid((txOffset && theme.buttonSelected) ? theme.buttonSelected : theme.buttonImage, x, y, x + w, y + h);
	ui_draw2d.DrawTextShadow(theme.uiFont, text, x + w/2, y + h/2 + txOffset, 0xFFFFFFFF, ALIGN_HCENTER | ALIGN_VCENTER);

	uistate.lastwidget = id;
	return clicked;
}

int UIImageButton(int id, const LayoutManager &layout, float w, int image, int button_align) {
	float h = 64;
	float x, y;
	layout.GetPos(&w, &h, &x, &y);

	if (button_align & ALIGN_HCENTER) x -= w / 2;
	if (button_align & ALIGN_VCENTER) y -= h / 2;
	if (button_align & ALIGN_RIGHT) x -= w;
	if (button_align & ALIGN_BOTTOMRIGHT) y -= h;

	int txOffset = 0;
	int clicked = 0;
	for (int i = 0; i < MAX_POINTERS; i++) {
		// Check whether the button should be hot, use a generous margin for touch ease
		if (UIRegionHit(i, x, y, w, h, 8)) {
			uistate.hotitem[i] = id;
			if (uistate.activeitem[i] == 0 && uistate.mousedown[i])
				uistate.activeitem[i] = id;
		}

		if (uistate.hotitem[i] == id) {
			if (uistate.activeitem[i] == id) {
				// Button is both 'hot' and 'active'
				txOffset = 2;
			} else {
				// Button is merely 'hot'
			}
		} else {
			// button is not hot, but it may be active
		}

		// If button is hot and active, but mouse button is not
		// down, the user must have clicked the button.
		if (uistate.mousedown[i] == 0 &&
			uistate.hotitem[i] == id &&
			uistate.activeitem[i] == id) {
				clicked = 1;
		}
	}

	// Render button

	ui_draw2d.DrawImage2GridH(theme.buttonImage, x, y, x + w);
	ui_draw2d.DrawImage(image, x + w/2, y + h/2 + txOffset, 1.0f, 0xFFFFFFFF, ALIGN_HCENTER | ALIGN_VCENTER);

	uistate.lastwidget = id;
	return clicked;
}

int UITextureButton(UIContext *ctx, int id, const LayoutManager &layout, float w, float h, Texture *texture, int button_align, uint32_t color, int drop_shadow)	// uses current UI atlas for fetching images.
{
	float x, y;
	layout.GetPos(&w, &h, &x, &y);

	if (button_align & ALIGN_HCENTER) x -= w / 2;
	if (button_align & ALIGN_VCENTER) y -= h / 2;
	if (button_align & ALIGN_RIGHT) x -= w;
	if (button_align & ALIGN_BOTTOMRIGHT) y -= h;

	int txOffset = 0;
	int clicked = 0;
	for (int i = 0; i < MAX_POINTERS; i++) {
		// Check whether the button should be hot, use a generous margin for touch ease
		if (UIRegionHit(i, x, y, w, h, 8)) {
			uistate.hotitem[i] = id;
			if (uistate.activeitem[i] == 0 && uistate.mousedown[i])
				uistate.activeitem[i] = id;
		}

		if (uistate.hotitem[i] == id) {
			if (uistate.activeitem[i] == id) {
				// Button is both 'hot' and 'active'
				txOffset = 2;
			} else {
				// Button is merely 'hot'
			}
		} else {
			// button is not hot, but it may be active
		}

		// If button is hot and active, but mouse button is not
		// down, the user must have clicked the button.
		if (uistate.mousedown[i] == 0 &&
			uistate.hotitem[i] == id &&
			uistate.activeitem[i] == id) {
				clicked = 1;
		}
	}
	if (texture) {
		float tw = texture->Width();
		float th = texture->Height();

		// Adjust position so we don't stretch the image vertically or horizontally.
		// TODO: Add a param to specify fit?  The below assumes it's never too wide.
		float nw = h * tw / th;
		x += (w - nw) / 2.0f;
		w = nw;
	}

	// Render button
	int dropsize = 10;
	if (drop_shadow && texture)
	{
		if (txOffset) {
			dropsize = 3;
			y += txOffset * 2;
		}
		ui_draw2d.DrawImage4Grid(drop_shadow, x - dropsize, y, x+w + dropsize, y+h+dropsize*1.5, 
			alphaMul(color,0.5f), 1.0f);
		ui_draw2d.Flush(true);
	}

	if (texture) {
		texture->Bind(0);
	} else {
		ui_draw2d.DrawImage2GridH(theme.buttonImage, x, y, x + w, color);
		ui_draw2d.Flush();

		Texture::Unbind();
	}
	ui_draw2d.DrawTexRect(x, y, x+w, y+h, 0, 0, 1, 1, color);

	ui_draw2d.Flush();
	ctx->RebindTexture();

	uistate.lastwidget = id;
	return clicked;
}

int UICheckBox(int id, int x, int y, const char *text, int align, bool *value) {
#ifdef _WIN32
	const int h = 32;
#else
	const int h = 48;
#endif
	float tw, th;
	ui_draw2d.MeasureText(theme.uiFont, text, &tw, &th);
	int w = themeAtlas->images[theme.checkOn].w + UI_SPACE + tw;
	if (align & ALIGN_HCENTER) x -= w / 2;
	if (align & ALIGN_VCENTER) y -= h / 2;
	if (align & ALIGN_RIGHT) x -= w;
	if (align & ALIGN_BOTTOMRIGHT) y -= h;

	int txOffset = 0;
	int clicked = 0;
	for (int i = 0; i < MAX_POINTERS; i++) {

		// Check whether the button should be hot
		if (UIRegionHit(i, x, y, w, h, 8)) {
			uistate.hotitem[i] = id;
			if (uistate.activeitem[i] == 0 && uistate.mousedown[i])
				uistate.activeitem[i] = id;
		}

		// Render button

		if (uistate.hotitem[i] == id) {
			if (uistate.activeitem[i] == id) {
				// Button is both 'hot' and 'active'
				txOffset = 2;
			} else {
				// Button is merely 'hot'
			}
		} else {
			// button is not hot, but it may be active
		}
		// If button is hot and active, but mouse button is not
		// down, the user must have clicked the button.
		if (uistate.mousedown[i] == 0 &&
			uistate.hotitem[i] == id &&
			uistate.activeitem[i] == id) {
				*value = !(*value);
				clicked = 1;
		}
	}

	ui_draw2d.DrawImage((*value) ? theme.checkOn : theme.checkOff, x, y+h/2, 1.0f, 0xFFFFFFFF, ALIGN_LEFT | ALIGN_VCENTER);
	ui_draw2d.DrawTextShadow(theme.uiFont, text, x + themeAtlas->images[theme.checkOn].w + UI_SPACE, y + txOffset + h/2, 0xFFFFFFFF, ALIGN_LEFT | ALIGN_VCENTER);


	uistate.lastwidget = id;
	return clicked;
}

void StringVectorListAdapter::drawItem(int item, int x, int y, int w, int h, bool selected) const
{
	ui_draw2d.DrawImage2GridH(theme.buttonImage, x, y, x + w);
	ui_draw2d.DrawTextShadow(theme.uiFont, (*items_)[item].c_str(), x + UI_SPACE , y, 0xFFFFFFFF, ALIGN_LEFT | ALIGN_VCENTER);
}

UIList::UIList()
	: scrollY(0.0f), startDragY(0.0f), dragFinger(-1), selected(-1) {
	movedDistanceX = 0.0f;
	movedDistanceY = 0.0f;
	scrolling = false;
	inertiaY = 0.0f;
}

void UIList::pointerDown(int pointer, float x, float y) {
	// Instantly halt on pointerDown if inertia-scrolling
	scrolling = false;
	inertiaY = 0.0f;

	startScrollY = scrollY;
	startDragY = y;
	movedDistanceX = 0.0f;
	movedDistanceY = 0.0f;
}

const int holdFramesClick = 3;
const int holdFramesScroll = 10;

void UIList::pointerMove(int pointer, float x, float y, bool inside) {
	float deltaX = x - lastX;
	float deltaY = y - lastY;
	movedDistanceY += fabsf(deltaY);
	movedDistanceX += fabsf(deltaX);

	if (inertiaY <= 0.0f && deltaY > 0.0f) {
		inertiaY = -deltaY;
	} else if (inertiaY >= 0.0f && deltaY < 0.0f) {
		inertiaY = -deltaY;
	} else {
		inertiaY = 0.8 * inertiaY + 0.2 * -deltaY;
	}

	const int deadzone = 30 * holdFramesScroll;
	if (inside && movedDistanceY * uistate.mouseframesdown[0] > deadzone && !scrolling) {
		scrolling = true;
	}
}

void UIList::pointerUp(int pointer, float x, float y, bool inside) {
	// printf("PointerUp %f %f\n", x, y);
}

int UIList::Do(int id, int x, int y, int w, int h, UIListAdapter *adapter) {
	int clicked = 0;

	// Pointer and focus handling

	// UIList only cares about the first pointer for simplicity.
	// Probably not much need to scroll one of these while dragging something
	// else.

	// TODO: Abstract this stuff out into EmulatePointerEvents
	for (int i = 0; i < 1; i++) {
		// Check for hover
		bool isInside = false;
		if (UIRegionHit(i, x, y, w, h, 0)) {
			isInside = true;
			uistate.hotitem[i] = id;
			if (uistate.activeitem[i] == 0 && uistate.mousedown[i]) {
				// Mousedown
				uistate.activeitem[i] = id;
				pointerDown(i, uistate.mousex[i], uistate.mousey[i]);
			}
		}

		if (uistate.activeitem[i] == id) {
			// NOTE: won't work with multiple pointers
			if (uistate.mousex[i] != lastX || uistate.mousey[i] != lastY) {
				pointerMove(i, uistate.mousex[i], uistate.mousey[i], isInside);
			}
		}

		// If button is hot and active, but mouse button is not
		// down, the user must have clicked a list item (unless after the last item).
		if (uistate.mousedown[i] == 0 &&
				uistate.activeitem[i] == id &&
				selected != -1) {
			if (uistate.hotitem[i] == id) {
				clicked = 1;
			}
			pointerUp(i, uistate.mousex[i], uistate.mousey[i], isInside);
		}
	}

	int itemHeight = adapter->itemHeight(0);
	int numItems = (int)adapter->getCount();

	// Cap total inertia
	if (inertiaY > 20) inertiaY = 20;
	if (inertiaY < -20) inertiaY = -20;

	float mouseY = uistate.mousey[0];
	if (!uistate.mousedown[0]) {
		// Let it slide if the pointer is not down
		scrollY += inertiaY;
	} else if (scrolling /* && mouseY > y && mouseY < y + h*/ ) {
		// Pointer is down so stick to it
		scrollY = startScrollY - (uistate.mousey[0] - startDragY);
	}

	// Inertia gradually trails off
	inertiaY *= 0.92f;
	if (scrolling && fabsf(inertiaY) < 0.001f)
		scrolling = false;

	// Cap top and bottom softly.
	float maxScrollY = numItems * itemHeight - h;
	if (maxScrollY < 0.0f) maxScrollY = 0.0f;
	if (scrollY > maxScrollY) {
		scrollY -= 0.4f * (scrollY - maxScrollY);
	} else if (scrollY < 0.0f) {
		scrollY += 0.4f * -scrollY;
	}

	lastX = uistate.mousex[0];
	lastY = uistate.mousey[0];
	uistate.lastwidget = id;

	// Drawing and item hittesting

	// render items
	for (int i = 0; i < numItems; i++) {
		int item_y = y + i * itemHeight - scrollY;

		if (item_y >= y - itemHeight && item_y <= y + h) {
			for (int k = 0; k < 1; k++) {  // MAX_POINTERS if we add back multitouch
				if (uistate.mousedown[k] &&
						uistate.mouseframesdown[k] >= holdFramesClick &&
						adapter->itemEnabled(i) &&
						!scrolling &&
						selected == -1 &&
						UIRegionHit(k, x, item_y, w, itemHeight, 0)) {
					selected = i;
				} else if (scrolling) {
					selected = -1;
				}
			}
			adapter->drawItem(i, x, item_y, w, itemHeight, i == selected);
		}
	}

	// Prevent scroll-clicks from registering
	if (selected == -1) clicked = 0;
	// Otherwise, no clicky.
	return clicked;
}

/*
struct SlideItem {
const char *text;
int image;
uint32_t bgColor;
};

struct SlideState
{
float scroll;

};

void UISlideChoice(int id, int y, const SlideItem *items, int numItems, SlideState *state)

}*/

// TODO
int UIHSlider(int id, int x, int y, int w, int max, int *value) {
	// Calculate mouse cursor's relative y offset
	int xpos = ((256 - 16) * *value) / max;

	for (int i = 0; i < MAX_POINTERS; i++) {
		// Check for hotness
		if (UIRegionHit(i, x+8, y+8, 16, 255, 0)) {
			uistate.hotitem[i] = id;
			if (uistate.activeitem[i] == 0 && uistate.mousedown[i])
				uistate.activeitem[i] = id;
		}

		// Update widget value
		if (uistate.activeitem[i] == id) {
			int mousepos = uistate.mousey[i] - (y + 8);
			if (mousepos < 0) mousepos = 0;
			if (mousepos > 255) mousepos = 255;
			int v = (mousepos * max) / 255;
			if (v != *value) {
				*value = v;
				return 1;
			}
		}
	}
	// Render the scrollbar
	ui_draw2d.Rect(x, y, 32, 256+16, 0x777777);

	ui_draw2d.Rect(x+8+xpos, y+8, 16, 16, 0xffffff);

	return 0;
}

/*
// TODO
int UIVSlider(int id, int x, int y, int h, int max, int *value) {
	// Calculate mouse cursor's relative y offset
	int ypos = ((256 - 16) * *value) / max;

	// Check for hotness
	if (UIRegionHit(x+8, y+8, 16, 255, 0)) {
		uistate.hotitem = id;
		if (uistate.activeitem == 0 && uistate.mousedown)
			uistate.activeitem = id;
	}
	// Update widget value
	if (uistate.activeitem == id) {
		int mousepos = uistate.mousey - (y + 8);
		if (mousepos < 0) mousepos = 0;
		if (mousepos > 255) mousepos = 255;
		int v = (mousepos * max) / 255;
		if (v != *value) {
			*value = v;
			return 1;
		}
	}
	// Render the scrollbar
	ui_draw2d.Rect(x, y, 32, 256+16, 0x777777);

	ui_draw2d.Rect(x+8, y+8 + ypos, 16, 16, 0xffffff);


	return 0;
}
*/
