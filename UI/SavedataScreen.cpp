// Copyright (c) 2013- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <algorithm>
#include <functional>

#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Math/curves.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/System/NativeApp.h"
#include "Common/System/Request.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/AsyncImageFileView.h"
#include "UI/SavedataScreen.h"
#include "UI/MainScreen.h"
#include "UI/GameInfoCache.h"
#include "UI/PauseScreen.h"

#include "Common/File/FileUtil.h"
#include "Common/TimeUtil.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/Loaders.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "Core/HLE/sceUtility.h"

class SavedataButton;

SavedataView::SavedataView(UIContext &dc, const Path &savePath, IdentifiedFileType type, std::string_view title, std::string_view savedataTitle, std::string_view savedataDetail, std::string_view fileSize, std::string_view mtime, bool showIcon, UI::LayoutParams *layoutParams)
	: LinearLayout(UI::ORIENT_VERTICAL, layoutParams)
{
	using namespace UI;

	const Style &textStyle = dc.theme->popupStyle;
	LinearLayout *toprow = new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT));
	Add(toprow);
	toprow->SetSpacing(0.0);

	savedataTitle_ = nullptr;
	fileSize_ = nullptr;
	mTime_ = nullptr;
	detail_ = nullptr;
	if (type == IdentifiedFileType::PSP_SAVEDATA_DIRECTORY) {
		if (showIcon) {
			toprow->Add(new GameIconView(savePath, 2.0f, new LinearLayoutParams(Margins(5, 5))));
		}
		LinearLayout *topright = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 1.0f));
		topright->SetSpacing(1.0f);
		savedataTitle_ = topright->Add(new TextView(savedataTitle, ALIGN_LEFT | FLAG_WRAP_TEXT, false));
		savedataTitle_->SetTextColor(textStyle.fgColor);
		fileSize_ = topright->Add(new TextView(fileSize, 0, true));
		fileSize_->SetTextColor(textStyle.fgColor);
		mTime_ = topright->Add(new TextView(mtime, 0, true));
		mTime_->SetTextColor(textStyle.fgColor);
		toprow->Add(topright);
		Add(new Spacer(3.0));
		detail_ = Add(new TextView(ReplaceAll(savedataDetail, "\r", ""), ALIGN_LEFT | FLAG_WRAP_TEXT, true, new LinearLayoutParams(Margins(10, 0))));
		detail_->SetTextColor(textStyle.fgColor);
		Add(new Spacer(3.0));
	} else {
		_dbg_assert_(type == IdentifiedFileType::PPSSPP_SAVESTATE);
		Path image_path = savePath.WithReplacedExtension(".ppst", ".jpg");
		if (File::Exists(image_path)) {
			toprow->Add(new AsyncImageFileView(image_path, IS_KEEP_ASPECT, new LinearLayoutParams(480, 272, Margins(10, 0))));
		} else {
			auto sa = GetI18NCategory(I18NCat::SAVEDATA);
			toprow->Add(new TextView(sa->T("No screenshot"), new LinearLayoutParams(Margins(10, 5))))->SetTextColor(textStyle.fgColor);
		}
		mTime_ = Add(new TextView(mtime, 0, true, new LinearLayoutParams(Margins(10, 5))));
		mTime_->SetTextColor(textStyle.fgColor);
	}
}

void SavedataView::UpdateGame(GameInfo *ginfo) {
	if (!ginfo->Ready(GameInfoFlags::PARAM_SFO | GameInfoFlags::SIZE)) {
		return;
	}
	_dbg_assert_(savedataTitle_);
	if (savedataTitle_) {
		savedataTitle_->SetText(ginfo->GetParamSFO().GetValueString("SAVEDATA_TITLE"));
	}
	if (detail_) {
		detail_->SetText(ginfo->GetParamSFO().GetValueString("SAVEDATA_DETAIL"));
	}
	if (fileSize_) {
		fileSize_->SetText(NiceSizeFormat(ginfo->gameSizeOnDisk));
	}
	if (mTime_) {
		mTime_->SetText(ginfo->GetMTime());
	}
}

SavedataView::SavedataView(UIContext &dc, GameInfo *ginfo, IdentifiedFileType type, bool showIcon, UI::LayoutParams *layoutParams)
	: SavedataView(dc,
		ginfo->GetFilePath(),
		type,
		"",
		"",
		"",
		"",
		"",
		showIcon,
		layoutParams) {
	if (ginfo) {
		UpdateGame(ginfo);
	}
}

class SavedataPopupScreen : public PopupScreen {
public:
	SavedataPopupScreen(Path savePath, std::string_view title) : PopupScreen(StripSpaces(title)), savePath_(savePath) { }

	const char *tag() const override { return "SavedataPopup"; }
	void update() override {
		PopupScreen::update();
		std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(screenManager()->getDrawContext(), savePath_, GameInfoFlags::PARAM_SFO | GameInfoFlags::ICON | GameInfoFlags::SIZE);
		if (!ginfo->Ready(GameInfoFlags::PARAM_SFO)) {
			// Hm, this is no good. But hopefully the previous screen loaded it.
			return;
		}
		if (savedataView_) {
			savedataView_->UpdateGame(ginfo.get());
		}
	}

	void CreatePopupContents(UI::ViewGroup *parent) override {
		using namespace UI;
		UIContext &dc = *screenManager()->getUIContext();

		std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(screenManager()->getDrawContext(), savePath_, GameInfoFlags::PARAM_SFO | GameInfoFlags::ICON | GameInfoFlags::SIZE);
		if (!ginfo->Ready(GameInfoFlags::PARAM_SFO)) {
			// This is OK, handled in Update. Though most likely, the previous screen loaded it.
		}

		ScrollView *contentScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f, UI::Margins(0, 3)));
		parent->Add(contentScroll);

		// TODO: If the game info wasn't already loaded, we'll get a bogus fileType here.
		savedataView_ = contentScroll->Add(new SavedataView(dc, ginfo.get(), ginfo->fileType, true));

		auto di = GetI18NCategory(I18NCat::DIALOG);
		LinearLayout *buttons = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		buttons->SetSpacing(0);
		Margins buttonMargins(5, 5);

		buttons->Add(new Button(di->T("Back"), new LinearLayoutParams(1.0f, buttonMargins)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
		buttons->Add(new Button(di->T("Delete"), new LinearLayoutParams(1.0f, buttonMargins)))->OnClick.Handle(this, &SavedataPopupScreen::OnDeleteButtonClick);
		parent->Add(buttons);
	}

protected:
	UI::Size PopupWidth() const override { return 500; }

private:
	UI::EventReturn OnDeleteButtonClick(UI::EventParams &e);
	SavedataView *savedataView_ = nullptr;
	Path savePath_;
};

class SortedLinearLayout : public UI::LinearLayoutList {
public:
	typedef std::function<void(View *)> PrepFunc;
	typedef std::function<bool(const View *, const View *)> CompareFunc;

	SortedLinearLayout(UI::Orientation orientation, UI::LayoutParams *layoutParams = nullptr)
		: UI::LinearLayoutList(orientation, layoutParams) {
	}

	void SetCompare(const PrepFunc &prepFunc, const CompareFunc &lessFunc) {
		prepIndex_ = 0;
		prepFunc_ = prepFunc;
		lessFunc_ = lessFunc;
	}

	void Update() override;

private:
	size_t prepIndex_ = 0;
	PrepFunc prepFunc_;
	CompareFunc lessFunc_;
};

void SortedLinearLayout::Update() {
	if (prepFunc_) {
		// Try to avoid dropping more than a frame, prefer items shift.
		constexpr double ALLOWED_TIME = 0.95 / 60.0;
		double start_time = time_now_d();
		for (; prepIndex_ < views_.size(); ++prepIndex_) {
			prepFunc_(views_[prepIndex_]);
			if (time_now_d() > start_time + ALLOWED_TIME) {
				break;
			}
		}
	}
	if (lessFunc_) {
		// We may sort several times while calculating.
		std::stable_sort(views_.begin(), views_.end(), lessFunc_);
	}
	// We're done if we got through all items.
	if (prepIndex_ >= views_.size()) {
		prepFunc_ = PrepFunc();
		lessFunc_ = CompareFunc();
	}

	UI::LinearLayout::Update();
}

void SavedataButton::UpdateTotalSize() {
	if (hasTotalSize_)
		return;

	File::FileInfo info;
	if (File::GetFileInfo(savePath_, &info)) {
		totalSize_ = info.size;
		if (info.isDirectory)
			totalSize_ = File::ComputeRecursiveDirectorySize(savePath_);
	}

	hasTotalSize_ = true;
}

void SavedataButton::UpdateDateSeconds() {
	if (hasDateSeconds_)
		return;

	File::FileInfo info;
	if (File::GetFileInfo(savePath_, &info)) {
		dateSeconds_ = info.mtime;
		if (info.isDirectory && File::GetFileInfo(savePath_ / "PARAM.SFO", &info)) {
			dateSeconds_ = info.mtime;
		}
	}

	hasDateSeconds_ = true;
}

UI::EventReturn SavedataPopupScreen::OnDeleteButtonClick(UI::EventParams &e) {
	std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(nullptr, savePath_, GameInfoFlags::PARAM_SFO);
	ginfo->Delete();
	TriggerFinish(DR_NO);
	return UI::EVENT_DONE;
}

static std::string CleanSaveString(std::string_view str) {
	std::string s = ReplaceAll(str, "\n", " ");
	s = ReplaceAll(s, "\r", " ");
	return s;
}

bool SavedataButton::UpdateText() {
	std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(nullptr, savePath_, GameInfoFlags::PARAM_SFO);
	if (ginfo->Ready(GameInfoFlags::PARAM_SFO)) {
		UpdateText(ginfo);
		return true;
	}
	return false;
}

void SavedataButton::UpdateText(const std::shared_ptr<GameInfo> &ginfo) {
	const std::string currentTitle = ginfo->GetTitle();
	if (!currentTitle.empty()) {
		title_ = CleanSaveString(currentTitle);
	}
	if (subtitle_.empty() && ginfo->gameSizeOnDisk > 0) {
		std::string date = ginfo->GetMTime();
		std::string savedata_title = ginfo->GetParamSFO().GetValueString("SAVEDATA_TITLE");
		subtitle_ = CleanSaveString(savedata_title) + " (" + NiceSizeFormat(ginfo->gameSizeOnDisk) + ", " + date.c_str() + ")";
	}
}

void SavedataButton::Draw(UIContext &dc) {
	std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(dc.GetDrawContext(), savePath_, GameInfoFlags::ICON | GameInfoFlags::PARAM_SFO | GameInfoFlags::SIZE);
	Draw::Texture *texture = 0;
	u32 color = 0, shadowColor = 0;
	using namespace UI;

	if (ginfo->Ready(GameInfoFlags::ICON) && ginfo->icon.texture) {
		texture = ginfo->icon.texture;
	}

	int x = bounds_.x;
	int y = bounds_.y;
	int w = 144;
	int h = bounds_.h;

	UI::Style style = dc.theme->itemStyle;
	if (down_)
		style = dc.theme->itemDownStyle;

	h = bounds_.h;
	if (HasFocus())
		style = down_ ? dc.theme->itemDownStyle : dc.theme->itemFocusedStyle;

	Drawable bg = style.background;

	dc.Draw()->Flush();
	dc.RebindTexture();
	dc.FillRect(bg, bounds_);
	dc.Draw()->Flush();

	if (texture) {
		color = whiteAlpha(ease((time_now_d() - ginfo->icon.timeLoaded) * 2));
		shadowColor = blackAlpha(ease((time_now_d() - ginfo->icon.timeLoaded) * 2));
		float tw = texture->Width();
		float th = texture->Height();

		// Adjust position so we don't stretch the image vertically or horizontally.
		// TODO: Add a param to specify fit?  The below assumes it's never too wide.
		float nw = h * tw / th;
		x += (w - nw) / 2.0f;
		w = nw;
	}

	// int txOffset = down_ ? 4 : 0;
	int txOffset = 0;

	Bounds overlayBounds = bounds_;

	// Render button
	int dropsize = 10;
	if (texture) {
		if (txOffset) {
			dropsize = 3;
			y += txOffset * 2;
			overlayBounds.y += txOffset * 2;
		}
		if (HasFocus()) {
			dc.Draw()->Flush();
			dc.RebindTexture();
			float pulse = sin(time_now_d() * 7.0) * 0.25 + 0.8;
			dc.Draw()->DrawImage4Grid(dc.theme->dropShadow4Grid, x - dropsize*1.5f, y - dropsize*1.5f, x + w + dropsize*1.5f, y + h + dropsize*1.5f, alphaMul(color, pulse), 1.0f);
			dc.Draw()->Flush();
		} else {
			dc.Draw()->Flush();
			dc.RebindTexture();
			dc.Draw()->DrawImage4Grid(dc.theme->dropShadow4Grid, x - dropsize, y - dropsize*0.5f, x + w + dropsize, y + h + dropsize*1.5, alphaMul(shadowColor, 0.5f), 1.0f);
			dc.Draw()->Flush();
		}
	}

	if (texture) {
		dc.Draw()->Flush();
		dc.GetDrawContext()->BindTexture(0, texture);
		dc.Draw()->DrawTexRect(x, y, x + w, y + h, 0, 0, 1, 1, color);
		dc.Draw()->Flush();
	}

	dc.Draw()->Flush();
	dc.RebindTexture();
	dc.SetFontStyle(dc.theme->uiFont);

	float tw, th;
	dc.Draw()->Flush();
	dc.PushScissor(bounds_);

	UpdateText(ginfo);
	dc.MeasureText(dc.GetFontStyle(), 1.0f, 1.0f, title_.c_str(), &tw, &th, 0);

	int availableWidth = bounds_.w - 150;
	float sineWidth = std::max(0.0f, (tw - availableWidth)) / 2.0f;

	float tx = 150.0f;
	if (availableWidth < tw) {
		float overageRatio = 1.5f * availableWidth * 1.0f / tw;
		tx -= (1.0f + sin(time_now_d() * overageRatio)) * sineWidth;
		Bounds tb = bounds_;
		tb.x = bounds_.x + 150.0f;
		tb.w = std::max(1.0f, bounds_.w - 150.0f);
		dc.PushScissor(tb);
	}
	dc.DrawText(title_.c_str(), bounds_.x + tx, bounds_.y + 4, style.fgColor, ALIGN_TOPLEFT);
	dc.SetFontScale(0.6f, 0.6f);
	dc.DrawText(subtitle_.c_str(), bounds_.x + tx, bounds_.y2() - 7, style.fgColor, ALIGN_BOTTOM);
	dc.SetFontScale(1.0f, 1.0f);

	if (availableWidth < tw) {
		dc.PopScissor();
	}
	dc.Draw()->Flush();
	dc.PopScissor();

	dc.RebindTexture();
}

std::string SavedataButton::DescribeText() const {
	auto u = GetI18NCategory(I18NCat::UI_ELEMENTS);
	return ApplySafeSubstitutions(u->T("%1 button"), title_) + "\n" + subtitle_;
}

SavedataBrowser::SavedataBrowser(const Path &path, UI::LayoutParams *layoutParams)
	: LinearLayout(UI::ORIENT_VERTICAL, layoutParams), path_(path) {
	Refresh();
}

void SavedataBrowser::Update() {
	LinearLayout::Update();
	if (searchPending_) {
		searchPending_ = false;

		int n = gameList_->GetNumSubviews();
		bool matches = searchFilter_.empty();
		for (int i = 0; i < n; ++i) {
			SavedataButton *v = static_cast<SavedataButton *>(gameList_->GetViewByIndex(i));

			// Note: might be resetting to empty string.  Can do that right away.
			if (searchFilter_.empty()) {
				v->SetVisibility(UI::V_VISIBLE);
				continue;
			}

			if (!v->UpdateText()) {
				// We'll need to wait until the text is loaded.
				searchPending_ = true;
				v->SetVisibility(UI::V_GONE);
				continue;
			}

			std::string label = v->DescribeText();
			std::transform(label.begin(), label.end(), label.begin(), tolower);
			bool match = label.find(searchFilter_) != label.npos;
			matches = matches || match;
			v->SetVisibility(match ? UI::V_VISIBLE : UI::V_GONE);
		}

		if (searchingView_) {
			bool show = !searchFilter_.empty() && (matches || searchPending_);
			searchingView_->SetVisibility(show ? UI::V_VISIBLE : UI::V_GONE);
		}
		if (noMatchView_)
			noMatchView_->SetVisibility(matches || searchPending_ ? UI::V_GONE : UI::V_VISIBLE);
	}
}

void SavedataBrowser::SetSearchFilter(const std::string &filter) {
	auto sa = GetI18NCategory(I18NCat::SAVEDATA);

	searchFilter_.resize(filter.size());
	std::transform(filter.begin(), filter.end(), searchFilter_.begin(), tolower);

	if (gameList_)
		searchPending_ = true;
	if (noMatchView_)
		noMatchView_->SetText(ApplySafeSubstitutions(sa->T("Nothing matching '%1' was found."), filter));
	if (searchingView_)
		searchingView_->SetText(ApplySafeSubstitutions(sa->T("Showing matches for '%1'."), filter));
}

void SavedataBrowser::SetSortOption(SavedataSortOption opt) {
	sortOption_ = opt;
	if (gameList_) {
		SortedLinearLayout *gl = static_cast<SortedLinearLayout *>(gameList_);
		if (sortOption_ == SavedataSortOption::FILENAME) {
			gl->SetCompare(&PrepFilename, &ByFilename);
		} else if (sortOption_ == SavedataSortOption::SIZE) {
			gl->SetCompare(&PrepSize, &BySize);
		} else if (sortOption_ == SavedataSortOption::DATE) {
			gl->SetCompare(&PrepDate, &ByDate);
		}
	}
}

void SavedataBrowser::PrepFilename(UI::View *v) {
	// Nothing needed.
}

bool SavedataBrowser::ByFilename(const UI::View *v1, const UI::View *v2) {
	const SavedataButton *b1 = static_cast<const SavedataButton *>(v1);
	const SavedataButton *b2 = static_cast<const SavedataButton *>(v2);

	return strcmp(b1->GamePath().c_str(), b2->GamePath().c_str()) < 0;
}

void SavedataBrowser::PrepSize(UI::View *v) {
	SavedataButton *b = static_cast<SavedataButton *>(v);
	b->UpdateTotalSize();
}

bool SavedataBrowser::BySize(const UI::View *v1, const UI::View *v2) {
	const SavedataButton *b1 = static_cast<const SavedataButton *>(v1);
	const SavedataButton *b2 = static_cast<const SavedataButton *>(v2);
	const uint64_t size1 = b1->GetTotalSize();
	const uint64_t size2 = b2->GetTotalSize();

	if (size1 > size2)
		return true;
	else if (size1 < size2)
		return false;
	return strcmp(b1->GamePath().c_str(), b2->GamePath().c_str()) < 0;
}

void SavedataBrowser::PrepDate(UI::View *v) {
	SavedataButton *b = static_cast<SavedataButton *>(v);
	b->UpdateDateSeconds();
}

bool SavedataBrowser::ByDate(const UI::View *v1, const UI::View *v2) {
	const SavedataButton *b1 = static_cast<const SavedataButton *>(v1);
	const SavedataButton *b2 = static_cast<const SavedataButton *>(v2);
	const int64_t time1 = b1->GetDateSeconds();
	const int64_t time2 = b2->GetDateSeconds();

	if (time1 > time2)
		return true;
	if (time1 < time2)
		return false;
	return strcmp(b1->GamePath().c_str(), b2->GamePath().c_str()) < 0;
}

void SavedataBrowser::Refresh() {
	using namespace UI;

	// Kill all the contents
	Clear();

	Add(new Spacer(1.0f));
	auto mm = GetI18NCategory(I18NCat::MAINMENU);
	auto sa = GetI18NCategory(I18NCat::SAVEDATA);

	// Find games in the current directory and create new ones.
	std::vector<SavedataButton *> savedataButtons;

	std::vector<File::FileInfo> fileInfo;
	GetFilesInDir(path_, &fileInfo, "ppst:");

	for (size_t i = 0; i < fileInfo.size(); i++) {
		bool isState = !fileInfo[i].isDirectory;
		bool isSaveData = false;
		
		if (!isState && File::Exists(path_ / fileInfo[i].name / "PARAM.SFO"))
			isSaveData = true;

		if (isSaveData || isState) {
			savedataButtons.push_back(new SavedataButton(fileInfo[i].fullName, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::WRAP_CONTENT)));
		}
	}

	ViewGroup *group = new LinearLayout(ORIENT_VERTICAL, new UI::LinearLayoutParams(UI::Margins(12, 0)));
	Add(group);

	if (savedataButtons.empty()) {
		group->Add(new TextView(sa->T("None yet. Things will appear here after you save.")));
		gameList_ = nullptr;
		noMatchView_ = nullptr;
		searchingView_ = nullptr;
	} else {
		noMatchView_ = group->Add(new TextView(sa->T("Nothing matching '%1' was found")));
		noMatchView_->SetVisibility(UI::V_GONE);
		searchingView_ = group->Add(new TextView(sa->T("Showing matches for '%1'")));
		searchingView_->SetVisibility(UI::V_GONE);

		SortedLinearLayout *gl = new SortedLinearLayout(UI::ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		gl->SetSpacing(4.0f);
		gameList_ = gl;
		Add(gameList_);

		for (size_t i = 0; i < savedataButtons.size(); i++) {
			SavedataButton *b = gameList_->Add(savedataButtons[i]);
			b->OnClick.Handle(this, &SavedataBrowser::SavedataButtonClick);
		}
	}

	// Reapply.
	SetSortOption(sortOption_);
	if (!searchFilter_.empty())
		SetSearchFilter(searchFilter_);
}

UI::EventReturn SavedataBrowser::SavedataButtonClick(UI::EventParams &e) {
	SavedataButton *button = static_cast<SavedataButton *>(e.v);
	UI::EventParams e2{};
	e2.v = e.v;
	e2.s = button->GamePath().ToString();
	// Insta-update - here we know we are already on the right thread.
	OnChoice.Trigger(e2);
	return UI::EVENT_DONE;
}

SavedataScreen::SavedataScreen(const Path &gamePath) : UIDialogScreenWithGameBackground(gamePath) {
}

SavedataScreen::~SavedataScreen() {
	if (g_gameInfoCache) {
		g_gameInfoCache->PurgeType(IdentifiedFileType::PPSSPP_SAVESTATE);
		g_gameInfoCache->PurgeType(IdentifiedFileType::PSP_SAVEDATA_DIRECTORY);
	}
}

void SavedataScreen::CreateViews() {
	using namespace UI;
	auto sa = GetI18NCategory(I18NCat::SAVEDATA);
	Path savedata_dir = GetSysDirectory(DIRECTORY_SAVEDATA);
	Path savestate_dir = GetSysDirectory(DIRECTORY_SAVESTATE);

	gridStyle_ = false;
	root_ = new AnchorLayout();

	// Make space for buttons.
	LinearLayout *main = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(FILL_PARENT, FILL_PARENT, 0, 0, 0, 84.0f));

	TabHolder *tabs = new TabHolder(ORIENT_HORIZONTAL, 64, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f));
	tabs->SetTag("Savedata");
	ScrollView *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	scroll->SetTag("SavedataBrowser");
	dataBrowser_ = scroll->Add(new SavedataBrowser(savedata_dir, new LayoutParams(FILL_PARENT, FILL_PARENT)));
	dataBrowser_->SetSortOption(sortOption_);
	if (!searchFilter_.empty())
		dataBrowser_->SetSearchFilter(searchFilter_);
	dataBrowser_->OnChoice.Handle(this, &SavedataScreen::OnSavedataButtonClick);

	tabs->AddTab(sa->T("Save Data"), scroll);

	ScrollView *scroll2 = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	scroll2->SetTag("SavedataStatesBrowser");
	stateBrowser_ = scroll2->Add(new SavedataBrowser(savestate_dir));
	stateBrowser_->SetSortOption(sortOption_);
	if (!searchFilter_.empty())
		stateBrowser_->SetSearchFilter(searchFilter_);
	stateBrowser_->OnChoice.Handle(this, &SavedataScreen::OnSavedataButtonClick);
	tabs->AddTab(sa->T("Save States"), scroll2);

	main->Add(tabs);

	ChoiceStrip *sortStrip = new ChoiceStrip(ORIENT_HORIZONTAL, new AnchorLayoutParams(NONE, 0, 0, NONE));
	sortStrip->AddChoice(sa->T("Filename"));
	sortStrip->AddChoice(sa->T("Size"));
	sortStrip->AddChoice(sa->T("Date"));
	sortStrip->SetSelection((int)sortOption_, false);
	sortStrip->OnChoice.Handle<SavedataScreen>(this, &SavedataScreen::OnSortClick);

	AddStandardBack(root_);
	if (System_GetPropertyBool(SYSPROP_HAS_TEXT_INPUT_DIALOG)) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		root_->Add(new Choice(di->T("Search"), "", false, new AnchorLayoutParams(WRAP_CONTENT, 64, NONE, NONE, 10, 10)))->OnClick.Handle<SavedataScreen>(this, &SavedataScreen::OnSearch);
	}

	root_->Add(main);
	root_->Add(sortStrip);
}

UI::EventReturn SavedataScreen::OnSortClick(UI::EventParams &e) {
	sortOption_ = SavedataSortOption(e.a);

	dataBrowser_->SetSortOption(sortOption_);
	stateBrowser_->SetSortOption(sortOption_);

	return UI::EVENT_DONE;
}

UI::EventReturn SavedataScreen::OnSearch(UI::EventParams &e) {
	if (System_GetPropertyBool(SYSPROP_HAS_TEXT_INPUT_DIALOG)) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		System_InputBoxGetString(GetRequesterToken(), di->T("Filter"), searchFilter_, false, [](const std::string &value, int ivalue) {
			System_PostUIMessage(UIMessage::SAVEDATA_SEARCH, value);
		});
	}
	return UI::EVENT_DONE;
}

UI::EventReturn SavedataScreen::OnSavedataButtonClick(UI::EventParams &e) {
	std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(screenManager()->getDrawContext(), Path(e.s), GameInfoFlags::PARAM_SFO);
	if (!ginfo->Ready(GameInfoFlags::PARAM_SFO)) {
		return UI::EVENT_DONE;
	}
	SavedataPopupScreen *popupScreen = new SavedataPopupScreen(Path(e.s), ginfo->GetTitle());
	if (e.v) {
		popupScreen->SetPopupOrigin(e.v);
	}
	screenManager()->push(popupScreen);
	// the game path: e.s;
	return UI::EVENT_DONE;
}

void SavedataScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	if (result == DR_NO) {
		RecreateViews();
	}
}

void SavedataScreen::sendMessage(UIMessage message, const char *value) {
	UIDialogScreenWithGameBackground::sendMessage(message, value);
	if (message == UIMessage::SAVEDATA_SEARCH) {
		searchFilter_ = value;
		dataBrowser_->SetSearchFilter(searchFilter_);
		stateBrowser_->SetSearchFilter(searchFilter_);
	}
}

void GameIconView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	w = textureWidth_;
	h = textureHeight_;
}

void GameIconView::Draw(UIContext &dc) {
	using namespace UI;
	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(dc.GetDrawContext(), gamePath_, GameInfoFlags::ICON);
	if (!info->Ready(GameInfoFlags::ICON) || !info->icon.texture) {
		return;
	}

	Draw::Texture *texture = info->icon.texture;

	textureWidth_ = texture->Width() * scale_;
	textureHeight_ = texture->Height() * scale_;

	// Fade icon with the backgrounds.
	double loadTime = info->icon.timeLoaded;
	auto pic = info->GetBGPic();
	if (pic) {
		loadTime = std::max(loadTime, pic->timeLoaded);
	}
	uint32_t color = whiteAlpha(ease((time_now_d() - loadTime) * 3));

	// Adjust size so we don't stretch the image vertically or horizontally.
	// Make sure it's not wider than 144 (like Doom Legacy homebrew), ugly in the grid mode.
	float nw = std::min(bounds_.h * textureWidth_ / textureHeight_, (float)bounds_.w);

	dc.Flush();
	dc.GetDrawContext()->BindTexture(0, texture);
	dc.Draw()->Rect(bounds_.x, bounds_.y, nw, bounds_.h, color);
	dc.Flush();
	dc.RebindTexture();
}
