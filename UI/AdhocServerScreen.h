#pragma once

#include <vector>
#include <string>
#include <thread>
#include <condition_variable>

#include "Common/UI/View.h"
#include "Common/UI/PopupScreens.h"
#include "Core/Config.h"  // for AdhocServerListEntry!
#include "Common/UI/Notice.h"
#include "Core/HLE/sceNetAdhoc.h"

struct AdhocUser {
	std::string name;
	std::vector<int> pdp_ports;
	std::vector<int> ptp_ports;
};

struct AdhocGroup {
	std::string name;
	int usercount;
	std::vector<AdhocUser> users;
};

struct AdhocGame {
	std::string name;
	int usercount;
	std::vector<AdhocGroup> groups;
	std::vector<std::string> game_ids;
};

// Later, this might also show games-in-progress.
// For now, it's just a simple metadata viewer.
class AdhocServerInfoScreen : public UI::PopupScreen {
public:
	AdhocServerInfoScreen(const AdhocServerListEntry &entry);

	const char *tag() const override { return "AdhocServerInfo"; }

protected:
	bool FillVertical() const override { return false; }
	UI::Size PopupWidth() const override { return 650; }
	bool ShowButtons() const override { return true; }

	void CreatePopupContents(UI::ViewGroup *parent) override;
	void update() override;

private:
	AdhocServerListEntry entry_;
	std::vector<AdhocGame> games_;
	std::shared_ptr<http::Request> statusRequest_;
};

class AdhocServerScreen : public UI::PopupScreen {
public:
	AdhocServerScreen(std::string *value, std::string_view title);
	~AdhocServerScreen();

	void CreatePopupContents(UI::ViewGroup *parent) override;

	const char *tag() const override { return "AdhocServer"; }

	bool RecreateParent() const {
		return recreateParent_;
	}

protected:
	void OnCompleted(DialogResult result) override;
	bool CanComplete(DialogResult result) override;
	virtual UI::Size PopupWidth() const override { return 650; }

	void sendMessage(UIMessage message, const char *value) override;

	void dialogFinished(const Screen *screen, DialogResult result) override {
		RecreateViews();
	}
private:
	void ResolverThread();

	enum class ResolverState {
		WAITING,
		QUEUED,
		PROGRESS,
		READY,
		QUIT,
	};

	std::string *value_;
	std::string editValue_;
	NoticeView *progressView_ = nullptr;

	std::thread resolver_;
	ResolverState resolverState_ = ResolverState::WAITING;
	std::mutex resolverLock_;
	std::condition_variable resolverCond_;
	std::string toResolve_ = "";
	bool toResolveResult_ = false;
	std::string lastResolved_ = "";
	bool lastResolvedResult_ = false;
	bool recreateParent_ = false;
};

void AskToEditCurrentServer(int requestToken, ScreenManager *screenManager);
bool AdhocServerNameIsCustom();
