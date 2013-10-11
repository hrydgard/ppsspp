#include "RepositionOnScreenControlScreen.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "base/colorutil.h"
#include "ui/ui_context.h"
#include "ui_atlas.h"
#include "gfx_es2/draw_buffer.h"
#include "GamepadEmu.h"
#include "i18n/i18n.h"

static const int leftMargin = 140; 


//convert from screen coordinates (leftMargin to dp_xres) to actual fullscreen coordinates (0 to dp_xres)
int toFullscreenCoord(int screenx){
	return  ((float)dp_xres / (dp_xres - leftMargin)) * (screenx - leftMargin);
}

//convert from external fullscreen  coordinates(0 to dp_xres)  to the current partial coordinates (leftMargin to dp_xres)
int fromFullscreenCoord(int controllerX){
	return leftMargin + ((dp_xres - leftMargin) / (float)dp_xres) * controllerX;
};

class DragDropButton : public MultiTouchButton{
public:
	DragDropButton(int &x, int &y, int bgImg, int img, float scale) 
	: MultiTouchButton(bgImg, img, scale, new UI::AnchorLayoutParams(fromFullscreenCoord(x), y, UI::NONE, UI::NONE,  true)), x_(x), y_(y){
		scale_ = scale;
	}

	virtual bool IsDown(){
		//don't want the button to enlarge and throw the user's perspective
		//of button size off whack.
		return false;
	};

	void SavePosition(){
		x_ = toFullscreenCoord(bounds_.centerX());
		y_ = bounds_.centerY();
	}

private:
	int &x_, &y_;
};


class PSPActionButtons : public DragDropButton{
public:
	PSPActionButtons(int &x, int &y, int actionButtonSpacing, float scale) 
	: DragDropButton(x, y, -1, -1, scale), actionButtonSpacing_(actionButtonSpacing){
		using namespace UI;
		roundId_ = I_ROUND;

		circleId_ = I_CIRCLE;
		crossId_ = I_CROSS;
		triangleId_ = I_TRIANGLE;
		squareId_ = I_SQUARE;
	};

	void Draw(UIContext &dc){
		float opacity = g_Config.iTouchButtonOpacity / 100.0f;


		uint32_t colorBg = colorAlpha(0xc0b080, opacity);
		uint32_t color = colorAlpha(0xFFFFFF, opacity);

		int centerX = bounds_.centerX();
		int centerY = bounds_.centerY();

		dc.Draw()->DrawImageRotated(roundId_, centerX + actionButtonSpacing_, centerY, scale_, 0, colorBg, false);
		dc.Draw()->DrawImageRotated(circleId_,  centerX + actionButtonSpacing_, centerY, scale_, 0, color, false);

		dc.Draw()->DrawImageRotated(roundId_, centerX, centerY + actionButtonSpacing_, scale_, 0, colorBg, false);
		dc.Draw()->DrawImageRotated(crossId_, centerX, centerY + actionButtonSpacing_, scale_, 0, color, false);

		dc.Draw()->DrawImageRotated(roundId_, centerX, centerY - actionButtonSpacing_, scale_, 0, colorBg, false);
		dc.Draw()->DrawImageRotated(triangleId_, centerX, centerY - actionButtonSpacing_, scale_, 0, color, false);

		dc.Draw()->DrawImageRotated(roundId_, centerX -  actionButtonSpacing_, centerY, scale_, 0, colorBg, false);
		dc.Draw()->DrawImageRotated(squareId_, centerX -  actionButtonSpacing_, centerY, scale_, 0, color, false);
	};

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const{
		const AtlasImage &image = dc.Draw()->GetAtlas()->images[roundId_];
		w = 2 * actionButtonSpacing_ + image.w * scale_;
		h = 2 * actionButtonSpacing_ + image.h * scale_;

		//w += 2 * actionButtonSpacing_;
		//h += 2 * actionButtonSpacing_;
	};
private:
	int roundId_;
	int circleId_, crossId_, triangleId_, squareId_;

	int actionButtonSpacing_;
};

class PSPDPadButtons : public DragDropButton{
public:
	PSPDPadButtons(int &x, int &y, int DpadRadius, float scale) 
	: DragDropButton(x, y, -1, -1, scale), DpadRadius_(DpadRadius){
	}

	void Draw(UIContext &dc){
		float opacity = g_Config.iTouchButtonOpacity / 100.0f;

		uint32_t colorBg = colorAlpha(0xc0b080, opacity);
		uint32_t color = colorAlpha(0xFFFFFF, opacity);

		static const float xoff[4] = {1, 0, -1, 0};
		static const float yoff[4] = {0, 1, 0, -1};

		for (int i = 0; i < 4; i++) {
			float x = bounds_.centerX() + xoff[i] * DpadRadius_;
			float y  = bounds_.centerY() + yoff[i] * DpadRadius_;
			float angle = i * M_PI / 2;

			dc.Draw()->DrawImageRotated(I_DIR, x, y, scale_, angle + PI, colorBg, false);
			dc.Draw()->DrawImageRotated(I_ARROW, x, y, scale_, angle + PI, color);
		}
	}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const{
		const AtlasImage &image = dc.Draw()->GetAtlas()->images[I_DIR];
		w =  2 * DpadRadius_ + image.w * scale_;
		h =  2 * DpadRadius_ + image.h * scale_;

		//w += 2 * DpadRadius_;
		//h += 2 * DpadRadius_;
	};

private:
	int DpadRadius_;
}; 



RepositionOnScreenControlScreen::RepositionOnScreenControlScreen(){
	pickedControl_ = 0;
};

void RepositionOnScreenControlScreen::touch(const TouchInput &touch){
	UIScreen::touch(touch);

	using namespace UI;

//this is if - not else if as up and move can occur simultaneously
	if ((touch.flags & TOUCH_MOVE) && pickedControl_ != 0){
		const Bounds &bounds = pickedControl_->GetBounds();
		
		int mintouchX = leftMargin + bounds.w * 0.5;
		int maxTouchX = dp_xres - bounds.w * 0.5;

		int minTouchY = bounds.h * 0.5;
		int maxTouchY = dp_yres - bounds.h * 0.5;

		int newX = bounds.centerX(), newY = bounds.centerY();

		//we have to handle x and y separately since even if x is blocked, y may not be.
		if(touch.x > mintouchX && touch.x < maxTouchX){
			//if the leftmost point of the control is ahead of the margin,
			//move it. Otherwise, don't.
			newX = touch.x;
		}
		if(touch.y > minTouchY && touch.y < maxTouchY){
			newY = touch.y;
		}

		ILOG("position: x = %d; y = %d", newX, newY);
		pickedControl_->ReplaceLayoutParams(new UI::AnchorLayoutParams(newX, newY, NONE, NONE,true));
	}

	else if ((touch.flags & TOUCH_DOWN) && pickedControl_ == 0){
		ILOG("->->->picked up")
		pickedControl_ = getPickedControl(touch.x, touch.y);
	}

	else if ((touch.flags & TOUCH_UP) && pickedControl_ != 0){
		pickedControl_->SavePosition();
		ILOG("->->->dropped down")
		pickedControl_ = 0;
	} 


};

UI::EventReturn RepositionOnScreenControlScreen::OnBack(UI::EventParams &e){
	g_Config.Save();

	if(PSP_IsInited()) {
		screenManager()->finishDialog(this, DR_CANCEL);
	} else {
		screenManager()->finishDialog(this, DR_OK);
	}

	return UI::EVENT_DONE;
};




void RepositionOnScreenControlScreen::CreateViews(){
//setup g_Config for button layout
	InitPadLayout();


	using namespace UI;

	I18NCategory *d = GetI18NCategory("Dialog");

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	Choice *back = new Choice(d->T("Back"), "", false, new AnchorLayoutParams(leftMargin, WRAP_CONTENT, 10, NONE, NONE, 10));
	back->OnClick.Handle(this, &RepositionOnScreenControlScreen::OnBack);
	root_->Add(back);


	TabHolder *tabHolder = new TabHolder(ORIENT_VERTICAL, leftMargin, new AnchorLayoutParams(10, 0, 10, 0, false));
	root_->Add(tabHolder);

	//this is more for show than anything else. It's used to provide a boundary 
	//so that buttons like back can be placed within the boundary.
	//serves no other purpose.
	AnchorLayout *controlsHolder = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	float scale = g_Config.fButtonScale;
	controls_.push_back(new PSPActionButtons(g_Config.iActionButtonCenterX, g_Config.iActionButtonCenterY, g_Config.iActionButtonSpacing, scale));
	controls_.push_back(new PSPDPadButtons(g_Config.iDpadX, g_Config.iDpadY, g_Config.iDpadRadius, scale));

	controls_.push_back(new DragDropButton(g_Config.iSelectKeyX, g_Config.iSelectKeyY, I_RECT, I_SELECT, scale));
	controls_.push_back(new DragDropButton(g_Config.iStartKeyX, g_Config.iStartKeyY, I_RECT, I_START, scale));
	controls_.push_back(new DragDropButton(g_Config.iUnthrottleKeyX, g_Config.iUnthrottleKeyY, I_RECT, I_ARROW, scale));

	controls_.push_back(new DragDropButton(g_Config.iLKeyX, g_Config.iLKeyY, I_SHOULDER, I_L, scale));
	controls_.push_back(new DragDropButton(g_Config.iRKeyX, g_Config.iRKeyY, I_SHOULDER, I_R, scale));

	if (g_Config.bShowAnalogStick) {
		controls_.push_back(new DragDropButton(g_Config.iAnalogStickX, g_Config.iAnalogStickY, I_STICKBG, I_STICK, scale));
	};

	I18NCategory *c = GetI18NCategory("Controls");
	tabHolder->AddTab(c->T("Controls"), controlsHolder);


	for(UI::View *control : controls_){
		root_->Add(control);
	}
};


DragDropButton *RepositionOnScreenControlScreen::getPickedControl(const int x, const int y){

	if(pickedControl_ != 0){
		return pickedControl_;
	}

	for(DragDropButton *control : controls_){
		const Bounds &bounds = control->GetBounds();
		static const int thresholdFactor = 1.5;

		Bounds tolerantBounds(bounds.x, bounds.y, bounds.w * thresholdFactor, bounds.h * thresholdFactor);
		if(tolerantBounds.Contains(x, y)){
			return control;
		}
	}

	return 0;
}
