#pragma once
#include "file/file_util.h"
#include "ui/ui_screen.h"

class ChatMenu : public PopupScreen {
public:
	ChatMenu() : PopupScreen("Chat") , toBottom_(true) {}
	~ChatMenu();
	void CreatePopupContents(UI::ViewGroup *parent) override;
	void CreateViews() override;
	void dialogFinished(const Screen *dialog, DialogResult result) override;
	bool touch(const TouchInput &touch) override;
	void update() override;
	void UpdateChat();
	bool toBottom_;
private:
	UI::EventReturn OnSubmit(UI::EventParams &e);
	UI::EventReturn OnQuickChat1(UI::EventParams &e);
	UI::EventReturn OnQuickChat2(UI::EventParams &e);
	UI::EventReturn OnQuickChat3(UI::EventParams &e);
	UI::EventReturn OnQuickChat4(UI::EventParams &e);
	UI::EventReturn OnQuickChat5(UI::EventParams &e);
	UI::TextEdit *chatEdit_;
	UI::ScrollView *scroll_;
	UI::LinearLayout *chatVert_;
	UI::ViewGroup *box_;
};