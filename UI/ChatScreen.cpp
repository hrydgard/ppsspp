#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "ui/ui.h"
#include "ChatScreen.h"
#include "Core/Config.h"
#include "Core/Host.h"
#include "Core/System.h"
#include "Common/LogManager.h"
#include "Core/HLE/proAdhoc.h"
#include "i18n/i18n.h"
#include <ctype.h>
#include "util/text/utf8.h"


void ChatMenu::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	I18NCategory *n = GetI18NCategory("Networking");
	LinearLayout *outer = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT,400));
	scroll_ = outer->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0)));
	LinearLayout *bottom = outer->Add(new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
	
#if defined(_WIN32) || defined(USING_QT_UI)
	chatEdit_ = bottom->Add(new TextEdit("", n->T("Chat Here"), new LinearLayoutParams(1.0)));
	if (g_Config.bBypassOSKWithKeyboard && !g_Config.bFullScreen)
	{
		std::wstring titleText = ConvertUTF8ToWString(n->T("Chat"));
		std::wstring defaultText = ConvertUTF8ToWString(n->T("Chat Here"));
		std::wstring inputChars;
		if (System_InputBoxGetWString(titleText.c_str(), defaultText, inputChars)) {
			chatEdit_->SetText(ConvertWStringToUTF8(inputChars));
		}
	}

	chatEdit_->OnEnter.Handle(this, &ChatMenu::OnSubmit);
	bottom->Add(new Button(n->T("Send")))->OnClick.Handle(this, &ChatMenu::OnSubmit);
#elif defined(__ANDROID__)
	bottom->Add(new Button(n->T("Chat Here"),new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->OnClick.Handle(this, &ChatMenu::OnSubmit);
#endif
	chatVert_ = scroll_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	chatVert_->SetSpacing(0);
	parent->Add(outer);
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
	UpdateChat();
	g_Config.iNewChat = 0;
}

void ChatMenu::dialogFinished(const Screen *dialog, DialogResult result) {
	UpdateUIState(UISTATE_INGAME);
}

UI::EventReturn ChatMenu::OnSubmit(UI::EventParams &e) {
#if defined(_WIN32) || defined(USING_QT_UI)
	std::string chat = chatEdit_->GetText();
	chatEdit_->SetText("");
	chatEdit_->SetFocus();
	sendChat(chat);
#elif defined(__ANDROID__)
	System_SendMessage("inputbox", "Chat:");
#endif
	return UI::EVENT_DONE;
}

std::vector<std::string> Split(const std::string& str)
{
	std::vector<std::string> ret;
	int counter = 0;
	int firstSentenceEnd = 0;
	int secondSentenceEnd = 0;
	//NOTICE_LOG(HLE, "Splitted %s %i", str.c_str(),str.size());
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
					TextView *v = chatVert_->Add(new TextView(j, FLAG_DYNAMIC_ASCII, false));
					uint32_t color = 0xFFFFFF;
					v->SetTextColor(0xFF000000 | color);
				}
			}
			else {
				TextView *v = chatVert_->Add(new TextView(i, FLAG_DYNAMIC_ASCII, false));
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
	}

	return UIDialogScreen::touch(touch);
}

void ChatMenu::postRender() {
	if (scroll_ && toBottom_) {
		scroll_->ScrollToBottom();
		toBottom_ = false;
	}
}

ChatMenu::~ChatMenu() {
	setChatPointer(NULL);
	scroll_ = NULL;
	chatVert_ = NULL;
}
