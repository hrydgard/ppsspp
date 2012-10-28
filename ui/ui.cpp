// See header for documentation.

#include <string>
#include <vector>

#include "ui/ui.h"
#include "gfx/texture_atlas.h"
#include "gfx_es2/draw_buffer.h"

// TODO: UI should probably not own these.
DrawBuffer ui_draw2d;
DrawBuffer ui_draw2d_front;

// This one, though, is OK.
UIState uistate;

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

void UIUpdateMouse(int i, float x, float y, bool down) {
  if (down && !uistate.mousedown[i]) {
    uistate.mousepressed[i] = 1;
    uistate.mouseStartX[i] = x;
    uistate.mouseStartY[i] = y;
  } else {
    uistate.mousepressed[i] = 0;
  }
  if (uistate.mousedown[i])
    uistate.mouseframesdown[i]++;
  else
    uistate.mouseframesdown[i] = 0;

  uistate.mousex[i] = x;
  uistate.mousey[i] = y;
  uistate.mousedown[i] = down;
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

void UIBegin() {
  for (int i = 0; i < MAX_POINTERS; i++)
    uistate.hotitem[i] = 0;
  ui_draw2d.Begin();
  ui_draw2d_front.Begin();
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
}

void UIText(int x, int y, const char *text, uint32_t color, float scale, int align) {
  UIText(theme.uiFont, x, y, text, color, scale, align);
}

void UIText(int font, int x, int y, const char *text, uint32_t color, float scale, int align) {
  ui_draw2d.SetFontScale(scale, scale);
  ui_draw2d.DrawTextShadow(font, text, x, y, color, align);
  ui_draw2d.SetFontScale(1.0f, 1.0f);
}

int UIButton(int id, const LayoutManager &layout, float w, const char *text, int button_align) {
  float h = themeAtlas->images[theme.buttonImage].h;

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

  ui_draw2d.DrawImage2GridH(theme.buttonImage, x, y, x + w);
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


int UICheckBox(int id, int x, int y, const char *text, int align, bool *value) {
  const int h = 64;
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

}

int UIList::Do(int id, int x, int y, int w, int h, UIListAdapter *adapter) {
  int clicked = 0;

  for (int i = 0; i < MAX_POINTERS; i++) {
    // Check whether the button should be hot
    if (UIRegionHit(i, x, y, w, h, 0)) {
      uistate.hotitem[i] = id;
      if (uistate.activeitem[i] == 0 && uistate.mousedown[i])
        uistate.activeitem[i] = id;
    }

    // If button is hot and active, but mouse button is not
    // down, the user must have clicked a list item (unless after the last item).
    if (uistate.mousedown[i] == 0 && 
        uistate.hotitem[i] == id && 
        uistate.activeitem[i] == id &&
        selected != -1) {
      clicked = 1;
    }
  }

  // render items
  int itemHeight = adapter->itemHeight(0);
  int numItems = adapter->getCount();
  for (int i = 0; i < numItems; i++) {
    int item_y = y + i * itemHeight - scrollY;

		for (int k = 0; k < MAX_POINTERS; k++) {
			if (uistate.mousedown[k] &&
					uistate.mouseframesdown[k] > 10 &&
					adapter->itemEnabled(i) && 
					item_y >= y - itemHeight && 
					item_y <= y + h &&
					UIRegionHit(k, x, item_y, w, itemHeight, 0)) {
				printf("%i", item_y);
				selected = i;
			}
		}
    adapter->drawItem(i, x, item_y, w, itemHeight, i == selected);
  }
  uistate.lastwidget = id;

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
