#pragma once

#include "ppsspp_config.h"
#include "file/file_util.h"
#include "ui/ui_screen.h"

class ChatMenu : public PopupScreen {
public:
	ChatMenu(): PopupScreen("Chat") {}
	~ChatMenu();
	void CreatePopupContents(UI::ViewGroup *parent) override;
	void CreateViews() override;
	void dialogFinished(const Screen *dialog, DialogResult result) override;
	bool touch(const TouchInput &touch) override;
	void update() override;
	void UpdateChat();

	bool toBottom_ = true;

private:
	UI::EventReturn OnSubmit(UI::EventParams &e);
	UI::EventReturn OnQuickChat1(UI::EventParams &e);
	UI::EventReturn OnQuickChat2(UI::EventParams &e);
	UI::EventReturn OnQuickChat3(UI::EventParams &e);
	UI::EventReturn OnQuickChat4(UI::EventParams &e);
	UI::EventReturn OnQuickChat5(UI::EventParams &e);

#if PPSSPP_PLATFORM(WINDOWS) || defined(USING_QT_UI) || defined(SDL)
	UI::TextEdit *chatEdit_ = nullptr;
#endif
	UI::ScrollView *scroll_ = nullptr;
	UI::LinearLayout *chatVert_ = nullptr;
	UI::ViewGroup *box_ = nullptr;
};
