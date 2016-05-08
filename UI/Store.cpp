// Copyright (c) 2014- PPSSPP Project.

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

#include "base/functional.h"
#include "base/basictypes.h"
#include "ext/vjson/json.h"

#include "i18n/i18n.h"
#include "ui/screen.h"
#include "ui/ui_context.h"
#include "ui/viewgroup.h"
#include "gfx_es2/draw_buffer.h"

#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/Util/GameManager.h"
#include "UI/EmuScreen.h"
#include "UI/Store.h"

const std::string storeBaseUrl = "http://store.ppsspp.org/";

// baseUrl is assumed to have a trailing slash, and not contain any subdirectories.
std::string ResolveUrl(std::string baseUrl, std::string url) {
	if (url.empty()) {
		return baseUrl;
	} else if (url[0] == '/') {
		return baseUrl + url.substr(1);
	} else if (url.substr(0, 7) == "http://") {
		return url;
	} else {
		// Huh.
		return baseUrl + url;
	}
}

class HttpImageFileView : public UI::View {
public:
	HttpImageFileView(http::Downloader *downloader, const std::string &path, UI::ImageSizeMode sizeMode = UI::IS_DEFAULT, UI::LayoutParams *layoutParams = 0)
		: UI::View(layoutParams), path_(path), color_(0xFFFFFFFF), sizeMode_(sizeMode), downloader_(downloader), texture_(nullptr), textureFailed_(false), fixedSizeW_(0.0f), fixedSizeH_(0.0f) {}

	~HttpImageFileView() {
		if (download_)
			download_->Cancel();
		delete texture_;
	}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;
	void Draw(UIContext &dc) override;

	void SetFilename(std::string filename);
	void SetColor(uint32_t color) { color_ = color; }
	void SetFixedSize(float fixW, float fixH) { fixedSizeW_ = fixW; fixedSizeH_ = fixH; }
	void SetCanBeFocused(bool can) { canFocus_ = can; }

	bool CanBeFocused() const override { return false; }

	const std::string &GetFilename() const { return path_; }

private:
	void DownloadCompletedCallback(http::Download &download);

	bool canFocus_;
	std::string path_;
	uint32_t color_;
	UI::ImageSizeMode sizeMode_;
	http::Downloader *downloader_;
	std::shared_ptr<http::Download> download_;

	std::string textureData_;
	Thin3DTexture *texture_;
	bool textureFailed_;
	float fixedSizeW_;
	float fixedSizeH_;
};

void HttpImageFileView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	switch (sizeMode_) {
	case UI::IS_FIXED:
		w = fixedSizeW_;
		h = fixedSizeH_;
		break;
	case UI::IS_DEFAULT:
	default:
		if (texture_) {
			float texw = (float)texture_->Width();
			float texh = (float)texture_->Height();
			w = texw;
			h = texh;
		} else {
			w = 16;
			h = 16;
		}
		break;
	}
}

void HttpImageFileView::SetFilename(std::string filename) {
	if (path_ != filename) {
		textureFailed_ = false;
		path_ = filename;
		if (texture_) {
			texture_->Release();
			texture_ = nullptr;
		}
	}
}

void HttpImageFileView::DownloadCompletedCallback(http::Download &download) {
	if (download.IsCancelled()) {
		// We were probably destroyed. Can't touch "this" (heh).
		return;
	}
	if (download.ResultCode() == 200) {
		download.buffer().TakeAll(&textureData_);
	} else {
		textureFailed_ = true;
	}
}

void HttpImageFileView::Draw(UIContext &dc) {
	if (!texture_ && !textureFailed_ && !path_.empty() && !download_) {
		download_ = downloader_->StartDownloadWithCallback(path_, "", std::bind(&HttpImageFileView::DownloadCompletedCallback, this, placeholder::_1));
		download_->SetHidden(true);
	}

	if (!textureData_.empty()) {
		texture_ = dc.GetThin3DContext()->CreateTextureFromFileData((const uint8_t *)(textureData_.data()), (int)textureData_.size(), DETECT);
		if (!texture_)
			textureFailed_ = true;
		textureData_.clear();
		download_.reset();
	}

	if (HasFocus()) {
		dc.FillRect(dc.theme->itemFocusedStyle.background, bounds_.Expand(3));
	}

	// TODO: involve sizemode
	if (texture_) {
		dc.Flush();
		dc.GetThin3DContext()->SetTexture(0, texture_);
		dc.Draw()->Rect(bounds_.x, bounds_.y, bounds_.w, bounds_.h, color_);
		dc.Flush();
		dc.RebindTexture();
	} else {
		// draw a black rectangle to represent the missing image.
		dc.FillRect(UI::Drawable(0xFF000000), GetBounds());
	}
}



// This is the entry in a list. Does not have install buttons and so on.
class ProductItemView : public UI::Choice {
public:
	ProductItemView(const StoreEntry &entry, UI::LayoutParams *layoutParams = 0)
		: UI::Choice(entry.name, layoutParams), entry_(entry) {}

	virtual void GetContentDimensions(const UIContext &dc, float &w, float &h) const {
		w = 300;
		h = 164;
	}
	virtual void Update(const InputState &input_state);
	virtual void Draw(UIContext &dc);

	StoreEntry GetEntry() const { return entry_; }

private:
	const StoreEntry &entry_;
};

void ProductItemView::Draw(UIContext &dc) {
	UI::Choice::Draw(dc);
	// dc.DrawText(entry_.name.c_str(), bounds_.centerX(), bounds_.centerY(), 0xFFFFFFFF, ALIGN_CENTER);
}

void ProductItemView::Update(const InputState &input_state) {
	View::Update(input_state);
}

// This is a "details" view of a game. Lets you install it.
class ProductView : public UI::LinearLayout {
public:
	ProductView(const StoreEntry &entry)
		: LinearLayout(UI::ORIENT_VERTICAL), entry_(entry), installButton_(0), wasInstalled_(false) {
		CreateViews();
	}

	virtual void Update(const InputState &input_state);

	UI::Event OnClickLaunch;

private:
	void CreateViews();
	UI::EventReturn OnInstall(UI::EventParams &e);
	UI::EventReturn OnUninstall(UI::EventParams &e);
	UI::EventReturn OnLaunchClick(UI::EventParams &e);

	bool IsGameInstalled() {
		return g_GameManager.IsGameInstalled(entry_.file);
	}

	StoreEntry entry_;
	UI::Button *installButton_;
	bool wasInstalled_;
};

void ProductView::CreateViews() {
	using namespace UI;
	Clear();

	if (!entry_.iconURL.empty()) {
		Add(new HttpImageFileView(&g_DownloadManager, ResolveUrl(storeBaseUrl, entry_.iconURL), IS_FIXED))->SetFixedSize(144, 88);
	}
	Add(new TextView(entry_.name));
	Add(new TextView(entry_.author));

	I18NCategory *st = GetI18NCategory("Store");
	wasInstalled_ = IsGameInstalled();
	if (!wasInstalled_) {
		installButton_ = Add(new Button(st->T("Install")));
		installButton_->OnClick.Handle(this, &ProductView::OnInstall);
	} else {
		installButton_ = nullptr;
		Add(new TextView(st->T("Already Installed")));
		Add(new Button(st->T("Uninstall")))->OnClick.Handle(this, &ProductView::OnUninstall);
		Add(new Button(st->T("Launch Game")))->OnClick.Handle(this, &ProductView::OnLaunchClick);
	}

	// Add star rating, comments etc?
	Add(new TextView(entry_.description));

	float size = entry_.size / (1024.f * 1024.f);
	char temp[256];
	sprintf(temp, "%s: %.2f %s", st->T("Size"), size, st->T("MB"));

	Add(new TextView(temp));
}

void ProductView::Update(const InputState &input_state) {
	if (wasInstalled_ != IsGameInstalled()) {
		CreateViews();
	}
	if (installButton_) {
		installButton_->SetEnabled(!g_GameManager.IsInstallInProgress());
	}
	View::Update(input_state);
}

UI::EventReturn ProductView::OnInstall(UI::EventParams &e) {
	std::string zipUrl;
	if (entry_.downloadURL.empty()) {
		// Construct the URL, easy to predict from our server
		zipUrl = storeBaseUrl + "files/" + entry_.file + ".zip";
	} else {
		// Use the provided URL, for external hosting.
		zipUrl = entry_.downloadURL;
	}
	if (installButton_) {
		installButton_->SetEnabled(false);
	}
	INFO_LOG(HLE, "Triggering install of %s", zipUrl.c_str());
	g_GameManager.DownloadAndInstall(zipUrl);
	return UI::EVENT_DONE;
}

UI::EventReturn ProductView::OnUninstall(UI::EventParams &e) {
	g_GameManager.Uninstall(entry_.file);
	CreateViews();
	return UI::EVENT_DONE;
}

UI::EventReturn ProductView::OnLaunchClick(UI::EventParams &e) {
	std::string pspGame = GetSysDirectory(DIRECTORY_GAME);
	std::string path = pspGame + entry_.file;
#ifdef _WIN32
	path = ReplaceAll(path, "\\", "/");
#endif

	UI::EventParams e2;
	e2.s = path;
	// Insta-update - here we know we are already on the right thread.
	OnClickLaunch.Trigger(e2);
	return UI::EVENT_DONE;
}

StoreScreen::StoreScreen() : loading_(true), connectionError_(false) {
	StoreFilter noFilter;
	SetFilter(noFilter);
	lang_ = g_Config.sLanguageIni;
	loading_ = true;

	std::string indexPath = storeBaseUrl + "index.json";

	listing_ = g_DownloadManager.StartDownload(indexPath, "");
}

StoreScreen::~StoreScreen() {
	g_DownloadManager.CancelAll();
}

// Handle async download tasks
void StoreScreen::update(InputState &input) {
	UIDialogScreenWithBackground::update(input);

	g_DownloadManager.Update();

	if (listing_.get() != 0 && listing_->Done()) {
		if (listing_->ResultCode() == 200) {
			std::string listingJson;
			listing_->buffer().TakeAll(&listingJson);
			// printf("%s\n", listingJson.c_str());
			loading_ = false;
			connectionError_ = false;

			ParseListing(listingJson);
			RecreateViews();
		} else {
			// Failed to contact store. Don't do anything.
			ELOG("Download failed : error code %d", listing_->ResultCode());
			connectionError_ = true;
			loading_ = false;
			RecreateViews();
		}

		// Forget the listing.
		listing_.reset();
	}
}

void StoreScreen::ParseListing(std::string json) {
	JsonReader reader(json.c_str(), json.size());
	if (!reader.ok()) {
		ELOG("Error parsing JSON from store");
		connectionError_ = true;
		RecreateViews();
		return;
	}
	json_value *root = reader.root();
	const json_value *entries = root->getArray("entries");
	if (entries) {
		entries_.clear();
		const json_value *game = entries->first_child;
		while (game) {
			StoreEntry e;
			e.type = ENTRY_PBPZIP;
			e.name = GetTranslatedString(game, "name");
			e.description = GetTranslatedString(game, "description", "");
			e.author = game->getString("author", "?");
			e.size = game->getInt("size");
			e.downloadURL = game->getString("download-url", "");
			e.iconURL = game->getString("icon-url", "");
			e.hidden = game->getBool("hidden", false);
			const char *file = game->getString("file", 0);
			if (!file)
				continue;
			e.file = file;
			entries_.push_back(e);
			game = game->next_sibling;
		}
	}
}

void StoreScreen::CreateViews() {
	using namespace UI;

	root_ = new LinearLayout(ORIENT_VERTICAL);
	
	I18NCategory *di = GetI18NCategory("Dialog");
	I18NCategory *st = GetI18NCategory("Store");

	// Top bar
	LinearLayout *topBar = root_->Add(new LinearLayout(ORIENT_HORIZONTAL));
	topBar->Add(new Button(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	topBar->Add(new TextView("PPSSPP Homebrew Store"));
	UI::Drawable solid(0xFFbd9939);
	topBar->SetBG(solid);

	LinearLayout *content;
	if (connectionError_ || loading_) {
		content = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f));
		content->Add(new TextView(loading_ ? st->T("Loading...") : st->T("Connection Error")));
		content->Add(new Button(di->T("Retry")))->OnClick.Handle(this, &StoreScreen::OnRetry);
		content->Add(new Button(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	} else {
		content = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f));
		ScrollView *leftScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(WRAP_CONTENT, FILL_PARENT, 0.4f));
		leftScroll->SetTag("StoreMainList");
		content->Add(leftScroll);
		LinearLayout *scrollItemView = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT));
		leftScroll->Add(scrollItemView);

		std::vector<StoreEntry> entries = FilterEntries();
		for (size_t i = 0; i < entries.size(); i++) {
			scrollItemView->Add(new ProductItemView(entries_[i]))->OnClick.Handle(this, &StoreScreen::OnGameSelected);
		}

		// TODO: Similar apps, etc etc
		productPanel_ = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(0.5f));
		leftScroll->SetTag("StoreMainProduct");
		content->Add(productPanel_);
	}
	root_->Add(content);
}

std::vector<StoreEntry> StoreScreen::FilterEntries() {
	std::vector<StoreEntry> filtered;
	for (size_t i = 0; i < entries_.size(); i++) {
		// TODO: Actually filter by category etc.
		if (!entries_[i].hidden)
			filtered.push_back(entries_[i]);
	}
	return filtered;
}

UI::EventReturn StoreScreen::OnGameSelected(UI::EventParams &e) {
	ProductItemView *item = static_cast<ProductItemView *>(e.v);
	if (!item)
		return UI::EVENT_DONE;

	productPanel_->Clear();
	ProductView *productView = new ProductView(item->GetEntry());
	productView->OnClickLaunch.Handle(this, &StoreScreen::OnGameLaunch);
	productPanel_->Add(productView);
	return UI::EVENT_DONE;
}

UI::EventReturn StoreScreen::OnGameLaunch(UI::EventParams &e) {
	std::string path = e.s;
	screenManager()->switchScreen(new EmuScreen(path));
	return UI::EVENT_DONE;
}

void StoreScreen::SetFilter(const StoreFilter &filter) {
	filter_ = filter;
	RecreateViews();
}

UI::EventReturn StoreScreen::OnRetry(UI::EventParams &e) {
	SetFilter(filter_);
	return UI::EVENT_DONE;
}

std::string StoreScreen::GetStoreJsonURL(std::string storePath) const {
	std::string path = storeBaseUrl + storePath;
	if (*path.rbegin() != '/')
		path += '/';
	path += "index.json";
	return path;
}

std::string StoreScreen::GetTranslatedString(const json_value *json, std::string key, const char *fallback) const {
	const json_value *dict = json->getDict("en_US");
	if (dict && json->hasChild(lang_.c_str(), JSON_OBJECT)) {
		if (json->getDict(lang_.c_str())->hasChild(key.c_str(), JSON_STRING)) {
			dict = json->getDict(lang_.c_str());
		}
	}
	const char *str = 0;
	if (dict) {
		str = dict->getString(key.c_str(), 0);
	}
	if (str) {
		return std::string(str);
	} else {
		return fallback ? fallback : "(error)";
	}
}
