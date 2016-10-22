#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "ui/ui.h"
#include "ChatScreen.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Common/LogManager.h"
#include "Core/HLE/proAdhoc.h"
#include "i18n/i18n.h"
#include <ctype.h>

void ChatMenu::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	I18NCategory *n = GetI18NCategory("Networking");
	LinearLayout *outer = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT,400));
	scroll_ = outer->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0)));
	LinearLayout *bottom = outer->Add(new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
	chatEdit_ = bottom->Add(new TextEdit("", n->T("Chat Here"), new LinearLayoutParams(1.0)));
	chatEdit_->OnEnter.Handle(this, &ChatMenu::OnSubmit);
	bottom->Add(new Button(n->T("Send")))->OnClick.Handle(this, &ChatMenu::OnSubmit);
	chatVert_ = scroll_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	chatVert_->SetSpacing(0);
	parent->Add(outer);
	UpdateChat();
}

void ChatMenu::CreateViews() {
	using namespace UI;

	I18NCategory *n = GetI18NCategory("Networking");
	UIContext &dc = *screenManager()->getUIContext();

	AnchorLayout *anchor = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));
	anchor->Overflow(false);
	root_ = anchor;

	float yres = screenManager()->getUIContext()->GetBounds().h;

	box_ = new LinearLayout(ORIENT_VERTICAL,
		new AnchorLayoutParams(PopupWidth(), FillVertical() ? yres - 30 : WRAP_CONTENT, 280, NONE, NONE, 250, true));

	root_->Add(box_);
	box_->SetBG(UI::Drawable(0x66303030));
	box_->SetHasDropShadow(false);

	View *title = new PopupHeader(n->T("Chat"));
	box_->Add(title);

	CreatePopupContents(box_);
	root_->SetDefaultFocusView(box_);
}

void ChatMenu::dialogFinished(const Screen *dialog, DialogResult result) {
	UpdateUIState(UISTATE_INGAME);
	// Close when a subscreen got closed.
	// TODO: a bug in screenmanager causes this not to work here.
	// screenManager()->finishDialog(this, DR_OK);
}

UI::EventReturn ChatMenu::OnSubmit(UI::EventParams &e) {
	std::string chat = chatEdit_->GetText();
	NOTICE_LOG(HLE, "Chat Send to socket: %s", chat.c_str());
	chatEdit_->SetText("");
	chatEdit_->SetFocus();
	sendChat(chat);
	UpdateChat();
	return UI::EVENT_DONE;
}

std::vector<std::string> Split(const std::string& str)
{
	std::vector<std::string> ret;
	int counter = 0;
	int firstSentenceEnd = 0;
	int secondSentenceEnd = 0;
	NOTICE_LOG(HLE, "Splitted %s %i", str.c_str(),str.size());
	for (auto i = 0; i<str.length(); i++) {
		if (isspace(str[i])) {
			if (i < 35) {
				if(str[i-1]!=':')
					firstSentenceEnd = i+1;
			}
			else if (i > 35) {
				secondSentenceEnd = i;
			}
		}
	}

	if (firstSentenceEnd == 0) {
		firstSentenceEnd = 35;
	}
	
	if(secondSentenceEnd == 0){
		secondSentenceEnd = str.length();
	}

	ret.push_back(str.substr(0, firstSentenceEnd));
	ret.push_back(str.substr(firstSentenceEnd, secondSentenceEnd));
	return ret;
}

void ChatMenu::UpdateChat() {
	using namespace UI;
	if (chatVert_ != NULL) {
		chatVert_->Clear(); //read Access violation is proadhoc.cpp use NULL_->Clear() pointer?
		std::vector<std::string> chatLog = getChatLog();
		for (auto i : chatLog) {
			if (i.length() > 30) {
				//split long text
				std::vector<std::string> splitted = Split(i);
				for (auto j : splitted) {
					TextView *v = chatVert_->Add(new TextView(j, FLAG_DYNAMIC_ASCII, false, new LayoutParams(PopupWidth(), WRAP_CONTENT)));
					uint32_t color = 0xFFFFFF;
					v->SetTextColor(0xFF000000 | color);
				}
			}
			else {
				TextView *v = chatVert_->Add(new TextView(i, FLAG_DYNAMIC_ASCII, false, new LayoutParams(PopupWidth(), WRAP_CONTENT)));
				uint32_t color = 0xFFFFFF;
				v->SetTextColor(0xFF000000 | color);
			}
		}
		toBottom_ = true;
	}
}

bool ChatMenu::touch(const TouchInput &touch) {
	if (!box_ || (touch.flags & TOUCH_DOWN) == 0 || touch.id != 0) {
		return UIDialogScreen::touch(touch);
	}

	if (!box_->GetBounds().Contains(touch.x, touch.y)){
		screenManager()->finishDialog(this, DR_BACK);
		setChatPointer(NULL); //fix the random crash
	}

	return UIDialogScreen::touch(touch);
}

void ChatMenu::update(InputState &input) {
	if (toBottom_) {
		toBottom_ = false;
		scroll_->ScrollToBottom();
	}
	PopupScreen::update(input);
}