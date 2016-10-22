#pragma once
#include "file/file_util.h"
#include "ui/ui_screen.h"

class ChatMenu : public PopupScreen {
public:
	ChatMenu() : PopupScreen("Chat") {}
	void CreatePopupContents(UI::ViewGroup *parent) override;
	void CreateViews() override;
	void dialogFinished(const Screen *dialog, DialogResult result) override;
	bool touch(const TouchInput &touch) override;
	void update(InputState &input) override;
	void UpdateChat();
private:
	UI::EventReturn OnSubmit(UI::EventParams &e);
	UI::TextEdit *chatEdit_;
	UI::ScrollView *scroll_;
	UI::LinearLayout *chatVert_;
	UI::ViewGroup *box_;
	UI::Button *defaultButton_;
	bool toBottom_;
};