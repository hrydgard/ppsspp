#include "AdhocServerScreen.h"

#include "Common/Net/Resolve.h"
#include "Common/UI/Root.h"
#include "Common/StringUtils.h"

AdhocServerScreen::AdhocServerScreen(std::string *value, std::vector<AdhocServerListEntry> &listItems, std::string_view title)
	: UI::PopupScreen(title, T(I18NCat::DIALOG, "OK"), T(I18NCat::DIALOG, "Cancel")), listItems_(listItems), value_(value) {
	resolver_ = std::thread([](AdhocServerScreen *thiz) {
		thiz->ResolverThread();
	}, this);
}
AdhocServerScreen::~AdhocServerScreen() {
	{
		std::unique_lock<std::mutex> guard(resolverLock_);
		resolverState_ = ResolverState::QUIT;
		resolverCond_.notify_one();
	}
	resolver_.join();
}

void AdhocServerScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto n = GetI18NCategory(I18NCat::NETWORKING);

	LinearLayout *valueRow = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, Margins(0, 0, 0, 10)));

	addrView_ = new TextEdit(*value_, n->T("Hostname"), "");
	addrView_->SetTextAlign(FLAG_DYNAMIC_ASCII);
	valueRow->Add(addrView_);
	parent->Add(valueRow);

	LinearLayout *buttonsRow1 = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	LinearLayout *buttonsRow2 = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	parent->Add(buttonsRow1);
	parent->Add(buttonsRow2);

	buttonsRow1->Add(new Spacer(new LinearLayoutParams(1.0, Gravity::G_LEFT)));
	for (char c = '0'; c <= '9'; ++c) {
		char label[] = {c, '\0'};
		auto button = buttonsRow1->Add(new Button(label));
		button->OnClick.Handle(this, &AdhocServerScreen::OnNumberClick);
		button->SetTag(label);
	}
	buttonsRow1->Add(new Button("."))->OnClick.Handle(this, &AdhocServerScreen::OnPointClick);
	buttonsRow1->Add(new Spacer(new LinearLayoutParams(1.0, Gravity::G_RIGHT)));

	buttonsRow2->Add(new Spacer(new LinearLayoutParams(1.0, Gravity::G_LEFT)));
	if (System_GetPropertyBool(SYSPROP_HAS_TEXT_INPUT_DIALOG)) {
		buttonsRow2->Add(new Button(di->T("Edit")))->OnClick.Handle(this, &AdhocServerScreen::OnEditClick);
	}
	buttonsRow2->Add(new Button(di->T("Delete")))->OnClick.Handle(this, &AdhocServerScreen::OnDeleteClick);
	buttonsRow2->Add(new Button(di->T("Delete all")))->OnClick.Handle(this, &AdhocServerScreen::OnDeleteAllClick);
	buttonsRow2->Add(new Spacer(new LinearLayoutParams(1.0, Gravity::G_RIGHT)));

	std::vector<std::string> listIP;
	for (const auto &item : listItems_) {
		listIP.push_back(item.hostname);
	}

	// Add non-editable items
	listIP.push_back("localhost");
	net::GetLocalIP4List(listIP);

	LinearLayout *ipRows_ = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0));
	ScrollView *scrollView = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	LinearLayout* innerView = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	if (listIP.size() > 0) {
		for (const auto& label : listIP) {
			// Filter out IP prefixed with "127." and "169.254." also "0." since they can be rendundant or unusable
			if (label.find("127.") != 0 && label.find("169.254.") != 0 && label.find("0.") != 0) {
				auto button = innerView->Add(new Button(label, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
				button->OnClick.Handle(this, &AdhocServerScreen::OnIPClick);
				button->SetTag(label);
			}
		}
	}

	scrollView->Add(innerView);
	ipRows_->Add(scrollView);
	parent->Add(ipRows_);
	listIP.clear(); listIP.shrink_to_fit();

	progressView_ = parent->Add(new TextView(n->T("Validating address..."), ALIGN_HCENTER, false, new LinearLayoutParams(Margins(0, 5, 0, 0))));
	progressView_->SetVisibility(UI::V_GONE);
}

void AdhocServerScreen::SendEditKey(InputKeyCode keyCode, KeyInputFlags flags) {
	auto oldView = UI::GetFocusedView();
	UI::SetFocusedView(addrView_);
	KeyInput fakeKey{DEVICE_ID_KEYBOARD, keyCode, KeyInputFlags::DOWN | flags};
	addrView_->Key(fakeKey);
	UI::SetFocusedView(oldView);
}

void AdhocServerScreen::OnNumberClick(UI::EventParams &e) {
	std::string text = e.v ? e.v->Tag() : "";
	if (text.length() == 1 && text[0] >= '0' && text[0] <= '9') {
		SendEditKey((InputKeyCode)text[0], KeyInputFlags::CHAR);  // ASCII for digits match keycodes.
	}
}

void AdhocServerScreen::OnPointClick(UI::EventParams &e) {
	SendEditKey((InputKeyCode)'.', KeyInputFlags::CHAR);
}

void AdhocServerScreen::OnDeleteClick(UI::EventParams &e) {
	SendEditKey(NKCODE_DEL);
}

void AdhocServerScreen::OnDeleteAllClick(UI::EventParams &e) {
	addrView_->SetText("");
}

void AdhocServerScreen::OnEditClick(UI::EventParams& e) {
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	System_InputBoxGetString(GetRequesterToken(), n->T("proAdhocServer Address:"), addrView_->GetText(), false, [this](const std::string& value, int) {
		addrView_->SetText(value);
	});
}

void AdhocServerScreen::OnIPClick(UI::EventParams& e) {
	std::string text = e.v ? e.v->Tag() : "";
	if (text.length() > 0) {
		addrView_->SetText(text);
		// Copy the IP to clipboard for the host to easily share their IP through chatting apps.
		System_CopyStringToClipboard(text);
	}
}

void AdhocServerScreen::ResolverThread() {
	std::unique_lock<std::mutex> guard(resolverLock_);

	while (resolverState_ != ResolverState::QUIT) {
		resolverCond_.wait(guard);

		if (resolverState_ == ResolverState::QUEUED) {
			resolverState_ = ResolverState::PROGRESS;

			addrinfo *resolved = nullptr;
			std::string err;
			toResolveResult_ = net::DNSResolve(toResolve_, "80", &resolved, err);
			if (resolved)
				net::DNSResolveFree(resolved);

			resolverState_ = ResolverState::READY;
		}
	}
}

bool AdhocServerScreen::CanComplete(DialogResult result) {
	auto n = GetI18NCategory(I18NCat::NETWORKING);

	if (result != DR_OK)
		return true;

	std::string value = addrView_->GetText();
	if (lastResolved_ == value) {
		return true;
	}

	// Currently running.
	if (resolverState_ == ResolverState::PROGRESS)
		return false;

	std::lock_guard<std::mutex> guard(resolverLock_);
	switch (resolverState_) {
	case ResolverState::PROGRESS:
	case ResolverState::QUIT:
		return false;

	case ResolverState::QUEUED:
	case ResolverState::WAITING:
		break;

	case ResolverState::READY:
		if (toResolve_ == value) {
			// Reset the state, nothing there now.
			resolverState_ = ResolverState::WAITING;
			toResolve_.clear();
			lastResolved_ = value;
			lastResolvedResult_ = toResolveResult_;

			if (lastResolvedResult_) {
				progressView_->SetVisibility(UI::V_GONE);
			} else {
				progressView_->SetText(n->T("Invalid IP or hostname"));
				progressView_->SetTextColor(0xFF3030FF);
				progressView_->SetVisibility(UI::V_VISIBLE);
			}
			return true;
		}

		// Throw away that last result, it was for a different value.
		break;
	}

	resolverState_ = ResolverState::QUEUED;
	toResolve_ = value;
	resolverCond_.notify_one();

	progressView_->SetText(n->T("Validating address..."));
	progressView_->SetTextColor(0xFFFFFFFF);
	progressView_->SetVisibility(UI::V_VISIBLE);

	return false;
}

void AdhocServerScreen::OnCompleted(DialogResult result) {
	if (result == DR_OK) {
		*value_ = StripSpaces(addrView_->GetText());
	}
}
