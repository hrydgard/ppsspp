#pragma once

#include <functional>
#include <string_view>

#include "Common/File/Path.h"
#include "Common/UI/ViewGroup.h"
#include "Common/File/PathBrowser.h"
#include "Core/ConfigValues.h"
#include "UI/MiscViews.h"

constexpr float MIN_GAME_GRID_SCALE = 0.8f;
constexpr float MAX_GAME_GRID_SCALE = 3.0f;

// The icon size setting for a view mode (grid and list have one each).
float *GameBrowserIconScale(GameBrowserViewMode viewMode);
void StepGameBrowserIconScale(GameBrowserViewMode viewMode, bool bigger);

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
	UMDS_ONLY = 64,  // Only what can go in the UMD drive (see UmdMount).
	STANDARD = 1 | 2 | 4 | 8 | 32,
};
ENUM_CLASS_BITOPS(BrowseFlags);

class GameBrowser : public UI::LinearLayout {
public:
	GameBrowser(int token, const Path &path, BrowseFlags browseFlags, bool portrait, int *viewMode, ScreenManager *screenManager, std::string_view lastText, std::string_view lastLink, UI::LayoutParams *layoutParams = nullptr);

	UI::Event OnChoice;
	UI::Event OnHoldChoice;
	UI::Event OnHighlight;

	void FocusGame(const Path &gamePath);
	void SetPath(const Path &path);
	void SetSearchBar(SearchBar *searchBar);
	bool Key(const KeyInput &key) override;
	bool Touch(const TouchInput &input) override;
	void SetSearchFilter(const std::string &filter, bool setKeyboardFocus);
	void Measure(const UIContext &dc, UI::MeasureSpec horiz, UI::MeasureSpec vert) override;
	void Draw(UIContext &dc) override;
	void Update() override;
	void RequestRefresh() {
		refreshPending_ = true;
	}

	void SetHomePath(const Path &path) {
		homePath_ = path;
	}

protected:
	virtual bool HasSpecialFiles(std::vector<Path> &filenames);
	virtual Path HomePath();

	void Refresh();
	GameBrowserViewMode ViewMode() const;

	Path homePath_;

private:
	bool IsCurrentPathPinned();
	std::vector<Path> GetPinnedPaths() const;

	void GameButtonClick(UI::EventParams &e);
	void GameButtonHoldClick(UI::EventParams &e);
	void GameButtonHighlight(UI::EventParams &e);
	void NavigateClick(UI::EventParams &e);
	void NavigateUp();
	void PathChanged();
	void LayoutChange(UI::EventParams &e);
	void LastClick(UI::EventParams &e);
	void BrowseClick(UI::EventParams &e);
	void StorageClick(UI::EventParams &e);
	void OnHomeClick(UI::EventParams &e);
	void PinToggleClick(UI::EventParams &e);
	void GridSettingsClick(UI::EventParams &e);
	void OnRecentClear(UI::EventParams &e);
	void OnHomebrewStore(UI::EventParams &e);

	UI::ViewGroup *gameList_ = nullptr;
	// Same as gameList_ in the grid view, else null.
	UI::GridLayoutList *gameGrid_ = nullptr;
	PathBrowser path_;
	int *viewMode_ = nullptr;
	BrowseFlags browseFlags_;
	std::string lastText_;
	std::string lastLink_;

	ViewSearch search_{};

	// A game or directory for Refresh to focus.
	Path focusPath_;
	// Set when entering a directory, for Refresh to focus its first entry.
	bool focusFirstItem_ = false;
	bool backspaceHeld_ = false;
	bool mouseHover_ = false;
	int wheelZoomAccum_ = 0;
	double lastWheelZoomTime_ = 0.0;
	bool listingPending_ = false;
	bool refreshPending_ = false;
	float lastScale_ = 1.0f;
	GameBrowserViewMode lastViewMode_ = GameBrowserViewMode::GRID;
	ScreenManager *screenManager_;
	int token_ = -1;
	bool portrait_ = false;
	Path aliasMatch_;
	std::string aliasDisplay_;
};
