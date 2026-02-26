#pragma once

#include <vector>
#include <string>
#include <thread>
#include <condition_variable>

#include "Common/UI/View.h"
#include "Common/UI/PopupScreens.h"
#include "Core/Config.h"  // for AdhocServerListEntry!
#include "Common/UI/Notice.h"

class AdhocServerScreen : public UI::PopupScreen {
public:
	AdhocServerScreen(std::string *value, std::string_view title);
	~AdhocServerScreen();

	void CreatePopupContents(UI::ViewGroup *parent) override;

	const char *tag() const override { return "AdhocServer"; }

protected:
	void OnCompleted(DialogResult result) override;
	bool CanComplete(DialogResult result) override;

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
	std::vector<AdhocServerListEntry> listItems_;
	NoticeView *progressView_ = nullptr;

	std::thread resolver_;
	ResolverState resolverState_ = ResolverState::WAITING;
	std::mutex resolverLock_;
	std::condition_variable resolverCond_;
	std::string toResolve_ = "";
	bool toResolveResult_ = false;
	std::string lastResolved_ = "";
	bool lastResolvedResult_ = false;
};

