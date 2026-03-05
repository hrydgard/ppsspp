#include "AdhocServerScreen.h"

#include "Common/Net/Resolve.h"
#include "Common/UI/Root.h"
#include "Common/StringUtils.h"
#include "Core/HLE/sceNetAdhoc.h"

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

	PopupTextInputChoice *textInputChoice = parent->Add(new PopupTextInputChoice(GetRequesterToken(), &editValue_, n->T("Hostname"), "", 256, screenManager()));
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

	{
		for (const auto &host : g_Config.vCustomAdhocServerList) {
			// If the host is already in the public list, skip it. We don't want duplicates.
			bool found = false;
			for (const auto &entry : entries) {
				if (entry.host == host) {
					found = true;
					break;
				}
			}
			if (found) {
				continue;
			}

			AdhocServerListEntry entry;
			entry.name = host;
			entry.host = host;
			customEntries.push_back(entry);
		}
	}

	ScrollView *scrollView = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f));
	LinearLayout *innerView = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	innerView->SetSpacing(5.0f);

	auto AddButtonFromEntry = [this](UI::ViewGroup *parent, const AdhocServerListEntry &entry) {
		// Filter out IP prefixed with "127." and "169.254." also "0." since they can be redundant or unusable
		auto button = parent->Add(new Button(entry.host, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		button->OnClick.Add([this](UI::EventParams &e) {
			std::string value = e.v->Tag();
			if (!value.empty()) {
				editValue_ = value;
				// TODO: Let's change this to an actual button later.
				System_CopyStringToClipboard(value);
			}
		});
		button->SetTag(entry.host);
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
