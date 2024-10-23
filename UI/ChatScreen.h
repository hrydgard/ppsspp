#pragma once

#include "ppsspp_config.h"
#include "Common/UI/UIScreen.h"

class ChatMenu : public UI::AnchorLayout {
public:
	ChatMenu(int token, const Bounds &screenBounds, ScreenManager *screenManager, UI::LayoutParams *lp = nullptr)
		: UI::AnchorLayout(lp), screenManager_(screenManager), token_(token) {
		CreateSubviews(screenBounds);
	}
	void Update() override;
	bool SubviewFocused(UI::View *view) override;

	void Close();

	bool Contains(float x, float y) const {
		if (box_)
			return box_->GetBounds().Contains(x, y);
		return false;
	}

private:
	void CreateSubviews(const Bounds &screenBounds);
	void CreateContents(UI::ViewGroup *parent);
	void UpdateChat();

	UI::EventReturn OnAskForChatMessage(UI::EventParams &e);

	UI::EventReturn OnSubmitMessage(UI::EventParams &e);
	UI::EventReturn OnQuickChat1(UI::EventParams &e);
	UI::EventReturn OnQuickChat2(UI::EventParams &e);
	UI::EventReturn OnQuickChat3(UI::EventParams &e);
	UI::EventReturn OnQuickChat4(UI::EventParams &e);
	UI::EventReturn OnQuickChat5(UI::EventParams &e);

	UI::TextEdit *chatEdit_ = nullptr;
	UI::ScrollView *scroll_ = nullptr;
	UI::LinearLayout *chatVert_ = nullptr;
	UI::ViewGroup *box_ = nullptr;
	ScreenManager *screenManager_;

	int chatChangeID_ = 0;
	bool toBottom_ = true;
	bool promptInput_ = false;
	int token_;
	std::string messageTemp_;
	UI::Button *chatButton_ = nullptr;
};
