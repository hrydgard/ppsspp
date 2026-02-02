#include <ctype.h>
#include "ppsspp_config.h"

#include "Common/UI/Root.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/ScrollView.h"
#include "Common/UI/UI.h"

#include "Common/Data/Text/I18n.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/System/Request.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/HLE/proAdhoc.h"
#include "UI/ChatScreen.h"
#include "UI/PopupScreens.h"

void ChatMenu::CreateContents(UI::ViewGroup *parent) {
	using namespace UI;
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	LinearLayout *outer = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT,400));
	scroll_ = outer->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0)));
	LinearLayout *bottom = outer->Add(new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));

	chatButton_ = nullptr;
	chatEdit_ = nullptr;
	chatVert_ = nullptr;

	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_DESKTOP) {
		// We have direct keyboard input.
		chatEdit_ = bottom->Add(new TextEdit("", n->T("Chat message"), n->T("Chat Here"), new LinearLayoutParams(1.0)));
		chatEdit_->OnEnter.Handle(this, &ChatMenu::OnSubmitMessage);
	} else {
		// If we have a native input box, like on Android, or at least we can do a popup text input with our UI...
		chatButton_ = bottom->Add(new Button(n->T("Chat message"), new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
		chatButton_->OnClick.Handle(this, &ChatMenu::OnAskForChatMessage);
	}

	if (g_Config.bEnableQuickChat) {
		LinearLayout *quickChat = outer->Add(new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
		for (int i = 0; i < 5; i++) {
			std::string name = std::to_string(i + 1);
			quickChat->Add(new Button(name, new LinearLayoutParams(1.0)))->OnClick.Add([i](UI::EventParams &e) {
				sendChat(g_Config.sQuickChat[i]);
			});
		}
	}
	chatVert_ = scroll_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	chatVert_->SetSpacing(0);
	parent->Add(outer);
}

void ChatMenu::CreateSubviews(const Bounds &screenBounds) {
	using namespace UI;

	float width = 550.0f;

	switch (g_Config.iChatScreenPosition) {
	// the chat screen size is still static 280x240 need a dynamic size based on device resolution 
	case 0:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(width, WRAP_CONTENT, 280, NONE, NONE, 240, Centering::Both));
		break;
	case 1:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(width, WRAP_CONTENT, screenBounds.centerX(), NONE, NONE, 240, Centering::Both));
		break;
	case 2:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(width, WRAP_CONTENT, NONE, NONE, 280, 240, Centering::Both));
		break;
	case 3:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(width, WRAP_CONTENT, 280, 240, NONE, NONE, Centering::Both));
		break;
	case 4:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(width, WRAP_CONTENT, screenBounds.centerX(), 240, NONE, NONE, Centering::Both));
		break;
	case 5:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(width, WRAP_CONTENT, NONE, 240, 280, NONE, Centering::Both));
		break;
	default:
		box_ = nullptr;
		break;
	}

	if (box_) {
		Add(box_);
		box_->SetBG(UI::Drawable(0x99303030));
		box_->SetHasDropShadow(false);

		auto n = GetI18NCategory(I18NCat::NETWORKING);
		View *title = new PopupHeader(n->T("Chat"));
		box_->Add(title);

		CreateContents(box_);
	}

	UpdateChat();
}

void ChatMenu::OnSubmitMessage(UI::EventParams &e) {
	std::string chat = chatEdit_->GetText();
	chatEdit_->SetText("");
	chatEdit_->SetFocus();
	sendChat(chat);
}

void ChatMenu::OnAskForChatMessage(UI::EventParams &e) {
	auto n = GetI18NCategory(I18NCat::NETWORKING);

	using namespace UI;

	if (System_GetPropertyBool(SYSPROP_HAS_TEXT_INPUT_DIALOG)) {
		System_InputBoxGetString(token_, n->T("Chat"), "", false, [](const std::string &value, int) {
			sendChat(value);
		});
	} else {
		// We need to pop up a UI inputbox.
		messageTemp_.clear();
		TextEditPopupScreen *popupScreen = new TextEditPopupScreen(&messageTemp_, "", n->T("Chat message"), 256);
		if (System_GetPropertyBool(SYSPROP_KEYBOARD_IS_SOFT)) {
			popupScreen->SetAlignTop(true);
		}
		popupScreen->OnChange.Add([=](UI::EventParams &e) {
			sendChat(messageTemp_);
		});
		popupScreen->SetPopupOrigin(chatButton_);
		screenManager_->push(popupScreen);
	}
}

void ChatMenu::UpdateChat() {
	using namespace UI;
	if (chatVert_ != nullptr) {
		chatVert_->Clear(); //read Access violation is proadhoc.cpp use NULL_->Clear() pointer?
		std::vector<std::string> chatLog = getChatLog();
		for (auto i : chatLog) {
			uint32_t namecolor = 0x29B6F6;
			uint32_t textcolor = 0xFFFFFF;
			uint32_t infocolor = 0xFDD835;

			std::string name = g_Config.sNickName;
			std::string displayname = i.substr(0, i.find(':'));
			
			if (name.substr(0, 8) == displayname) {
				namecolor = 0xE53935;
			}

			if (i.length() <= displayname.length() || i[displayname.length()] != ':') {
				TextView *v = chatVert_->Add(new TextView(i, ALIGN_LEFT | FLAG_WRAP_TEXT, true, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
				v->SetTextColor(0xFF000000 | infocolor);
			} else {
				LinearLayout *line = chatVert_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, FILL_PARENT)));
				line->SetSpacing(0.0f);
				TextView *nameView = line->Add(new TextView(displayname, ALIGN_LEFT, true, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 0.0f)));
				nameView->SetTextColor(0xFF000000 | namecolor);

				std::string chattext = i.substr(displayname.length());
				TextView *chatView = line->Add(new TextView(chattext, ALIGN_LEFT | FLAG_WRAP_TEXT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f)));
				chatView->SetTextColor(0xFF000000 | textcolor);
			}
		}
		toBottom_ = true;
	}
}

void ChatMenu::Update() {
	AnchorLayout::Update();
	if (scroll_ && toBottom_) {
		toBottom_ = false;
		scroll_->ScrollToBottom();
	}

	if (chatChangeID_ != GetChatChangeID()) {
		chatChangeID_ = GetChatChangeID();
		UpdateChat();
	}

#if defined(USING_WIN_UI)
	// Could remove the fullscreen check here, it works now.
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	if (promptInput_ && g_Config.bBypassOSKWithKeyboard && !g_Config.bFullScreen) {
		System_InputBoxGetString(token_, n->T("Chat"), n->T("Chat Here"), false, [](const std::string &value, int) {
			sendChat(value);
		});
		promptInput_ = false;
	}
#endif
}

bool ChatMenu::SubviewFocused(UI::View *view) {
	if (!AnchorLayout::SubviewFocused(view))
		return false;

	promptInput_ = true;
	return true;
}

void ChatMenu::Close() {
	SetVisibility(UI::V_GONE);
}
