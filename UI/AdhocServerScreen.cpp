#include "AdhocServerScreen.h"

#include "Common/Net/Resolve.h"
#include "Common/UI/Root.h"
#include "Common/StringUtils.h"
#include "Core/HLE/sceNetAdhoc.h"

static UI::View *CreateLinkButton(std::string url) {
	using namespace UI;

	// steal strings from all over the place
	auto cr = GetI18NCategory(I18NCat::PSPCREDITS);
	auto st = GetI18NCategory(I18NCat::STORE);

	ImageID icon = ImageID("I_LINK_OUT_QUESTION");
	std::string title;

	if (startsWith(url, "https://discord")) {
		icon = ImageID("I_LOGO_DISCORD");
		title = cr->T("Discord");
	} else {
		icon = ImageID("I_WEB_BROWSER");
		title = st->T("Website");
	}

	Choice *choice = new Choice(title, icon, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, Margins(12, 0)));
	choice->OnClick.Add([url](UI::EventParams &) {
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, url);
	});
	return choice;
}

// Later, this might also show games-in-progress.
// For now, it's just a simple metadata viewer.
class AdhocServerInfoScreen : public UI::PopupScreen {
public:
	AdhocServerInfoScreen(const AdhocServerListEntry &entry)
		: PopupScreen(entry.name, T(I18NCat::DIALOG, "Back")), entry_(entry) {
	}   // PopupScreen will translate Back on its own

	const char *tag() const override { return "AdhocServerInfo"; }

protected:
	bool FillVertical() const override { return false; }
	UI::Size PopupWidth() const override { return 500; }
	bool ShowButtons() const override { return true; }

	void CreatePopupContents(UI::ViewGroup *parent) override {
		using namespace UI;
		auto pa = GetI18NCategory(I18NCat::PAUSE);
		auto di = GetI18NCategory(I18NCat::DIALOG);
		auto ni = GetI18NCategory(I18NCat::NETWORKING);

		ScrollView *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f));
		LinearLayout *content = new LinearLayout(ORIENT_VERTICAL);
		Margins contentMargins(10, 0);

		content->Add(new InfoItem(entry_.host, ""));
		if (!entry_.ip.empty()) {
			content->Add(new InfoItem(entry_.ip, ""));
		}
		content->Add(new InfoItem(entry_.location, ""));
		content->Add(new InfoItem(ni->T("Relay server mode"), entry_.mode == AdhocDataMode::AemuPostoffice ? di->T("Yes") : di->T("No")));
		TextView *desc = content->Add(new TextView(entry_.description, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(10))));
		desc->SetTextSize(TextSize::Small);
		desc->SetWordWrap();
		if (!entry_.web.empty()) {
			content->Add(CreateLinkButton(entry_.web));
		}
		if (!entry_.discord.empty()) {
			content->Add(CreateLinkButton(entry_.discord));
		}
		scroll->Add(content);
		parent->Add(scroll);
	}
private:
	AdhocServerListEntry entry_;
};

class AdhocServerRow : public UI::LinearLayout {
public:
	AdhocServerRow(std::string *value, const AdhocServerListEntry &entry, ScreenManager *screenManager, UI::LayoutParams *layoutParams = nullptr);

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		w = 500; h = 90;
	}

	void Draw(UIContext &dc) override {
		dc.FillRect(dc.GetTheme().itemStyle.background, bounds_);
		if (*value_ == entry_.host) {
			// TODO: Make this highlight themable
			dc.FillRect(UI::Drawable(0x30FFFFFF), GetBounds());
		}
		LinearLayout::Draw(dc);
	}

	bool Touch(const TouchInput &input) override {
		using namespace UI;
		if (UI::LinearLayout::Touch(input)) {
			return true;
		}
		if (input.flags & TouchInputFlags::DOWN) {
			if (bounds_.Contains(input.x, input.y)) {
				dragging_ = true;
				return true;
			}
		}
		if (dragging_ && (input.flags & TouchInputFlags::UP)) {
			dragging_ = false;
			if (!(input.flags & TouchInputFlags::CANCEL) && bounds_.Contains(input.x, input.y)) {
				EventParams e;
				e.v = this;
				OnSelected.Trigger(e);
				return true;
			}
		}
		return false;
	}

	UI::Event OnSelected;

private:
	bool dragging_ = false;
	std::string *value_;
	AdhocServerListEntry entry_;
};

AdhocServerRow::AdhocServerRow(std::string *value, const AdhocServerListEntry &entry, ScreenManager *screenManager, UI::LayoutParams *layoutParams)
	: UI::LinearLayout(ORIENT_HORIZONTAL, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::WRAP_CONTENT, UI::Margins(5.0f, 0.0f))), value_(value), entry_(entry) {
	using namespace UI;

	int number = 0;

	LinearLayout *lines = Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(Margins(5, 5))));
	lines->SetSpacing(0.0f);
	lines->Add(new TextView(entry.name));
	lines->Add(new TextView(entry.host))->SetTextSize(TextSize::Small)->SetWordWrap();

	Add(new Spacer(0.0f, new LinearLayoutParams(1.0f, Margins(0.0f, 5.0f))));

	if (entry.mode == AdhocDataMode::AemuPostoffice) {
		TextView *relay = Add(new TextView("Relay", new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, Gravity::G_VCENTER, Margins(10.0))));
	}

	if (!entry.description.empty()) {
		Choice *infoButton = Add(new Choice(ImageID("I_INFO"), new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, Gravity::G_VCENTER, Margins(0, 0, 10, 0))));
		AdhocServerListEntry copy = entry;
		infoButton->OnClick.Add([this, copy, screenManager](UI::EventParams &e) {
			e.v = this;
			screenManager->push(new AdhocServerInfoScreen(copy));
		});
	}
}

AdhocServerScreen::AdhocServerScreen(std::string *value, std::string_view title)
	: UI::PopupScreen(title, T(I18NCat::DIALOG, "OK"), T(I18NCat::DIALOG, "Cancel")), value_(value) {
	resolver_ = std::thread([](AdhocServerScreen *thiz) {
		thiz->ResolverThread();
	}, this);
	editValue_ = *value;
	AdhocLoadServerList();
}

AdhocServerScreen::~AdhocServerScreen() {
	{
		std::unique_lock<std::mutex> guard(resolverLock_);
		resolverState_ = ResolverState::QUIT;
		resolverCond_.notify_one();
	}
	resolver_.join();
}

void AdhocServerScreen::sendMessage(UIMessage message, const char *value) {
	if (message == UIMessage::ADHOC_SERVER_LIST_CHANGED) {
		RecreateViews();
	}
}

void AdhocServerScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto n = GetI18NCategory(I18NCat::NETWORKING);

	auto listItems = AdhocGetServerList();

	PopupTextInputChoice *textInputChoice = parent->Add(new PopupTextInputChoice(GetRequesterToken(), &editValue_, n->T("Hostname"), "", 450, screenManager()));
	parent->Add(new Spacer(5.0f));

	// editValue_ has the currently selected server. On closing the dialog, we copy that to settings.

	// Start with the downloaded list.
	std::vector<AdhocServerListEntry> entries = listItems;
	std::vector<AdhocServerListEntry> localEntries;
	std::vector<AdhocServerListEntry> customEntries;

	// Add localhost and local IPs, since those are common ones to connect to.
	{
		AdhocServerListEntry localhostEntry;
		localhostEntry.name = "localhost";
		localhostEntry.host = "localhost";
		localEntries.push_back(localhostEntry);

		std::vector<std::string> listIP;
		net::GetLocalIP4List(listIP);

		for (const auto &ipAddress : listIP) {
			if (startsWith(ipAddress, "127.") || startsWith(ipAddress, "169.254.") || startsWith(ipAddress, "0.")) {
				continue;
			}
			AdhocServerListEntry entry;
			entry.name = ipAddress;
			entry.host = ipAddress;
			localEntries.push_back(entry);
		}
	}

	auto hostInEntries = [entries](const std::string &host) {
		for (const auto &entry : entries) {
			if (entry.host == host) {
				return true;
			}
		}
		return false;
	};

	for (const auto &host : g_Config.vCustomAdhocServerListWithRelay) {
		// If the host is already in the public list, skip it. We don't want duplicates.
		if (hostInEntries(host) || host.empty()) {
			continue;
		}
		AdhocServerListEntry entry;
		entry.name = host;
		entry.host = host;
		entry.mode = AdhocDataMode::AemuPostoffice;
		customEntries.push_back(entry);
	}

	for (const auto &host : g_Config.vCustomAdhocServerList) {
		// If the host is already in the public list, skip it. We don't want duplicates.
		if (hostInEntries(host) || host.empty()) {
			continue;
		}
		AdhocServerListEntry entry;
		entry.name = host;
		entry.host = host;
		entry.mode = AdhocDataMode::P2P;
		customEntries.push_back(entry);
	}

	ScrollView *scrollView = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f));
	LinearLayout *innerView = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	innerView->SetSpacing(5.0f);

	auto AddButtonFromEntry = [this](UI::ViewGroup *parent, const AdhocServerListEntry &entry) {
		AdhocServerRow *row = new AdhocServerRow(&editValue_, entry, screenManager());
		parent->Add(row);
		row->OnSelected.Add([this](UI::EventParams &e) {
			std::string value = e.v->Tag();
			if (!value.empty()) {
				editValue_ = value;
				// TODO: Let's change this to an actual button later.
				System_CopyStringToClipboard(value);
			}
		});
		row->SetTag(entry.host);
	};

	if (!customEntries.empty()) {
		CollapsibleSection *customSection = innerView->Add(new CollapsibleSection(n->T("Custom server list"), new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		for (const auto &entry : customEntries) {
			AddButtonFromEntry(customSection, entry);
		}
	}

	CollapsibleSection *publicSection = innerView->Add(new CollapsibleSection(n->T("Public server list"), new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	for (const auto &entry : entries) {
		AddButtonFromEntry(publicSection, entry);
	}

	CollapsibleSection *localSection = innerView->Add(new CollapsibleSection(n->T("Local network addresses"), new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	for (const auto &entry : localEntries) {
		AddButtonFromEntry(localSection, entry);
	}

	scrollView->Add(innerView);
	parent->Add(scrollView);

	progressView_ = parent->Add(new NoticeView(NoticeLevel::INFO, n->T("Validating address..."), "", new LinearLayoutParams(Margins(0, 5, 0, 0))));
	progressView_->SetVisibility(UI::V_GONE);
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

	std::string value = editValue_;
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
				progressView_->SetLevel(NoticeLevel::ERROR);
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
	progressView_->SetLevel(NoticeLevel::INFO);
	progressView_->SetVisibility(UI::V_VISIBLE);

	return false;
}

void AdhocServerScreen::OnCompleted(DialogResult result) {
	if (result == DR_OK) {
		*value_ = StripSpaces(editValue_);
	}
}
