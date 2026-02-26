#pragma once

#include <vector>
#include <string>
#include "Common/UI/View.h"
#include "Common/UI/PopupScreens.h"
#include "Core/Config.h"  // for AdhocServerListEntry!

class AdhocServerScreen : public UI::PopupScreen {
public:
	AdhocServerScreen(std::string *value, std::vector<AdhocServerListEntry> &listItems, std::string_view title);
	~AdhocServerScreen();

	void CreatePopupContents(UI::ViewGroup *parent) override;

	const char *tag() const override { return "AdhocServer"; }

protected:
	void OnCompleted(DialogResult result) override;
	bool CanComplete(DialogResult result) override;

private:
	void ResolverThread();
	void SendEditKey(InputKeyCode keyCode, KeyInputFlags flags = (KeyInputFlags)0);

	void OnNumberClick(UI::EventParams &e);
	void OnPointClick(UI::EventParams &e);
	void OnDeleteClick(UI::EventParams &e);
	void OnDeleteAllClick(UI::EventParams &e);
	void OnEditClick(UI::EventParams& e);
	void OnIPClick(UI::EventParams& e);

	enum class ResolverState {
		WAITING,
		QUEUED,
		PROGRESS,
		READY,
		QUIT,
	};

	std::string *value_;
	std::vector<AdhocServerListEntry> listItems_;
	UI::TextEdit *addrView_ = nullptr;
	UI::TextView *progressView_ = nullptr;

	std::thread resolver_;
	ResolverState resolverState_ = ResolverState::WAITING;
	std::mutex resolverLock_;
	std::condition_variable resolverCond_;
	std::string toResolve_ = "";
	bool toResolveResult_ = false;
	std::string lastResolved_ = "";
	bool lastResolvedResult_ = false;
};

