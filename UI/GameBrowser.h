#pragma once

#include <functional>
#include <string_view>

#include "Common/File/Path.h"
#include "Common/UI/ViewGroup.h"
#include "Common/File/PathBrowser.h"

enum GameBrowserFlags {
	FLAG_HOMEBREWSTOREBUTTON = 1
};

enum class BrowseFlags {
	NONE = 0,
	NAVIGATE = 1,
	BROWSE = 2,
	ARCHIVES = 4,
	PIN = 8,
	HOMEBREW_STORE = 16,
	UPLOAD_BUTTON = 32,
	STANDARD = 1 | 2 | 4 | 8 | 32,
};
ENUM_CLASS_BITOPS(BrowseFlags);

class GameBrowser : public UI::LinearLayout {
public:
	GameBrowser(int token, const Path &path, BrowseFlags browseFlags, bool portrait, bool *gridStyle, ScreenManager *screenManager, std::string_view lastText, std::string_view lastLink, UI::LayoutParams *layoutParams = nullptr);

	UI::Event OnChoice;
	UI::Event OnHoldChoice;
	UI::Event OnHighlight;

	void FocusGame(const Path &gamePath);
	void SetPath(const Path &path);
	void ApplySearchFilter(const std::string &filter);
	void Draw(UIContext &dc) override;
	void Update() override;
	void RequestRefresh() {
		refreshPending_ = true;
	}

	void SetHomePath(const Path &path) {
		homePath_ = path;
	}

protected:
	virtual bool DisplayTopBar();
	virtual bool HasSpecialFiles(std::vector<Path> &filenames);
	virtual Path HomePath();
	void ApplySearchFilter();

	void Refresh();

	Path homePath_;

private:
	bool IsCurrentPathPinned();
	std::vector<Path> GetPinnedPaths() const;

	void GameButtonClick(UI::EventParams &e);
	void GameButtonHoldClick(UI::EventParams &e);
	void GameButtonHighlight(UI::EventParams &e);
	void NavigateClick(UI::EventParams &e);
	void LayoutChange(UI::EventParams &e);
	void LastClick(UI::EventParams &e);
	void BrowseClick(UI::EventParams &e);
	void StorageClick(UI::EventParams &e);
	void OnHomeClick(UI::EventParams &e);
	void PinToggleClick(UI::EventParams &e);
	void GridSettingsClick(UI::EventParams &e);
	void OnRecentClear(UI::EventParams &e);
	void OnHomebrewStore(UI::EventParams &e);

	enum class SearchState {
		MATCH,
		MISMATCH,
		PENDING,
	};

	UI::ViewGroup *gameList_ = nullptr;
	PathBrowser path_;
	bool *gridStyle_ = nullptr;
	BrowseFlags browseFlags_;
	std::string lastText_;
	std::string lastLink_;
	std::string searchFilter_;
	std::vector<SearchState> searchStates_;
	Path focusGamePath_;
	bool listingPending_ = false;
	bool searchPending_ = false;
	bool refreshPending_ = false;
	float lastScale_ = 1.0f;
	bool lastLayoutWasGrid_ = true;
	ScreenManager *screenManager_;
	int token_ = -1;
	bool portrait_ = false;
	Path aliasMatch_;
	std::string aliasDisplay_;
};
