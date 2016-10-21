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

void ChatMenu::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	//tried to give instance to proAdhoc not working
	//setChatPointer(this);
	I18NCategory *n = GetI18NCategory("Networking");
	LinearLayout *outer = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, 400));
	scroll_ = outer->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(PopupWidth(), FILL_PARENT, 1.0f)));
	chatVert_ = scroll_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(PopupWidth(), WRAP_CONTENT)));
	chatVert_->SetSpacing(0);
	LinearLayout *bottom = outer->Add(new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
	chatEdit_ = bottom->Add(new TextEdit("", n->T("Chat Here"), new LinearLayoutParams(1.0)));
	chatEdit_->OnEnter.Handle(this, &ChatMenu::OnSubmit);
	bottom->Add(new Button(n->T("Send")))->OnClick.Handle(this, &ChatMenu::OnSubmit);
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

std::vector<std::string> Split(const std::string& str, int splitLength)
{
	int NumSubstrings = str.length() / splitLength;
	std::vector<std::string> ret;

	// TODO sub string in white space  
	for (auto i = 0; i < NumSubstrings; i++)
	{
		ret.push_back(str.substr(i * splitLength, splitLength));
	}

	// If there are leftover characters, create a shorter item at the end.
	if (str.length() % splitLength != 0)
	{
		ret.push_back(str.substr(splitLength * NumSubstrings));
	}


	return ret;
}

void ChatMenu::UpdateChat() {
	using namespace UI;
	if (chatVert_ != NULL) {
		chatVert_->Clear();
		std::vector<std::string> chatLog = getChatLog();
		for (auto i : chatLog) {
			if (i.length() > 30) {
				//split long text
				std::vector<std::string> splitted = Split(i, 32);
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
		scroll_->ScrollToBottom();
	}
}

bool ChatMenu::touch(const TouchInput &touch) {
	if (!box_ || (touch.flags & TOUCH_DOWN) == 0 || touch.id != 0) {
		return UIDialogScreen::touch(touch);
	}

	if (!box_->GetBounds().Contains(touch.x, touch.y))
		screenManager()->finishDialog(this, DR_BACK);

	return UIDialogScreen::touch(touch);
}