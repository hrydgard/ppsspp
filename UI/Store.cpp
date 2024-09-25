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

#include <functional>

#include "Common/UI/Screen.h"
#include "Common/UI/Context.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/IconCache.h"
#include "Common/Render/DrawBuffer.h"

#include "Common/Log.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Format/JSONReader.h"
#include "Common/StringUtils.h"
#include "Common/Render/ManagedTexture.h"
#include "Common/Net/NetBuffer.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/Util/GameManager.h"
#include "UI/EmuScreen.h"
#include "UI/Store.h"

const char *storeBaseUrlHttp = "http://store.ppsspp.org/";
const char *storeBaseUrlHttps = "https://store.ppsspp.org/";

static std::string StoreBaseUrl() {
	return System_GetPropertyBool(SYSPROP_SUPPORTS_HTTPS) ? storeBaseUrlHttps : storeBaseUrlHttp;
}

// baseUrl is assumed to have a trailing slash, and not contain any subdirectories.
std::string ResolveUrl(const std::string &baseUrl, const std::string &url) {
	if (url.empty()) {
		return baseUrl;
	} else if (url[0] == '/') {
		return baseUrl + url.substr(1);
	} else if (startsWith(url, "http://") || startsWith(url, "https://")) {
		return url;
	} else {
		// Huh.
		return baseUrl + url;
	}
}

class HttpImageFileView : public UI::View {
public:
	HttpImageFileView(http::RequestManager *requestManager, const std::string &path, UI::ImageSizeMode sizeMode = UI::IS_DEFAULT, bool useIconCache = true, UI::LayoutParams *layoutParams = nullptr)
		: UI::View(layoutParams), path_(path), sizeMode_(sizeMode), requestManager_(requestManager), useIconCache_(useIconCache) {

		if (useIconCache && g_iconCache.MarkPending(path_)) {
			const char *acceptMime = "image/png, image/jpeg, image/*; q=0.9, */*; q=0.8";
			requestManager_->StartDownloadWithCallback(path_, Path(), http::ProgressBarMode::DELAYED, [](http::Request &download) {
				// Can't touch 'this' in this function! Don't use captures!
				std::string path = download.url();
				if (download.ResultCode() == 200) {
					std::string data;
					download.buffer().TakeAll(&data);
					if (!data.empty()) {
						g_iconCache.InsertIcon(path, IconFormat::PNG, std::move(data));
					} else {
						g_iconCache.CancelPending(path);
					}
				} else {
					g_iconCache.CancelPending(path);
				}
			}, acceptMime);
		}
	}

	~HttpImageFileView() {
		if (download_) {
			download_->Cancel();
		}
	}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;
	void Draw(UIContext &dc) override;
	std::string DescribeText() const override { return ""; }

	void SetFilename(const std::string &filename);
	void SetColor(uint32_t color) { color_ = color; }
	void SetFixedSize(float fixW, float fixH) { fixedSizeW_ = fixW; fixedSizeH_ = fixH; }
	void SetCanBeFocused(bool can) { canFocus_ = can; }

	bool CanBeFocused() const override { return false; }

	const std::string &GetFilename() const { return path_; }

private:
	void DownloadCompletedCallback(http::Request &download);

	bool canFocus_ = false;
	bool useIconCache_ = false;
	std::string path_;  // or cache key
	uint32_t color_ = 0xFFFFFFFF;
	UI::ImageSizeMode sizeMode_;
	http::RequestManager *requestManager_;
	std::shared_ptr<http::Request> download_;

	std::string textureData_;
	Draw::AutoRef<Draw::Texture> texture_;
	bool textureFailed_ = false;
	float fixedSizeW_ = 0.0f;
	float fixedSizeH_ = 0.0f;
};

void HttpImageFileView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	switch (sizeMode_) {
	case UI::IS_FIXED:
		w = fixedSizeW_;
		h = fixedSizeH_;
		break;
	case UI::IS_DEFAULT:
	default:
		if (useIconCache_) {
			int width, height;
			if (g_iconCache.GetDimensions(path_, &width, &height)) {
				w = width;
				h = height;
			} else {
				w = 16;
				h = 16;
			}
		} else {
			if (texture_) {
				float texw = (float)texture_->Width();
				float texh = (float)texture_->Height();
				w = texw;
				h = texh;
			} else {
				w = 16;
				h = 16;
			}
		}
		break;
	}
}

void HttpImageFileView::SetFilename(const std::string &filename) {
	if (!useIconCache_ && path_ != filename) {
		textureFailed_ = false;
		path_ = filename;
		if (texture_) {
			texture_.reset(nullptr);
		}
	}
}

void HttpImageFileView::DownloadCompletedCallback(http::Request &download) {
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
	using namespace Draw;

	if (!useIconCache_) {
		if (!texture_ && !textureFailed_ && !path_.empty() && !download_) {
			auto cb = std::bind(&HttpImageFileView::DownloadCompletedCallback, this, std::placeholders::_1);
			const char *acceptMime = "image/png, image/jpeg, image/*; q=0.9, */*; q=0.8";
			requestManager_->StartDownloadWithCallback(path_, Path(), http::ProgressBarMode::NONE, cb, acceptMime);
		}

		if (!textureData_.empty()) {
			texture_ = CreateTextureFromFileData(dc.GetDrawContext(), (const uint8_t *)(textureData_.data()), textureData_.size(), ImageFileType::DETECT, false, "store_icon");
			if (!texture_)
				textureFailed_ = true;
			textureData_.clear();
			download_.reset();
		}
	}

	if (HasFocus()) {
		dc.FillRect(dc.theme->itemFocusedStyle.background, bounds_.Expand(3));
	}

	// TODO: involve sizemode
	Draw::Texture *texture = nullptr;
	if (useIconCache_) {
		texture = g_iconCache.BindIconTexture(&dc, path_);
	} else {
		texture = texture_;
	}

	if (texture) {
		float tw = texture->Width();
		float th = texture->Height();

		float x = bounds_.x;
		float y = bounds_.y;
		float w = bounds_.w;
		float h = bounds_.h;

		if (tw / th < w / h) {
			float nw = h * tw / th;
			x += (w - nw) / 2.0f;
			w = nw;
		} else {
			float nh = w * th / tw;
			y += (h - nh) / 2.0f;
			h = nh;
		}

		dc.Flush();
		dc.GetDrawContext()->BindTexture(0, texture);
		dc.Draw()->Rect(x, y, w, h, color_);
		dc.Flush();
		dc.RebindTexture();
	} else {
		// draw a black rectangle to represent the missing image.
		dc.FillRect(UI::Drawable(0x7F000000), GetBounds());
	}
}

// This is the entry in a list. Does not have install buttons and so on.
class ProductItemView : public UI::StickyChoice {
public:
	ProductItemView(const StoreEntry &entry, UI::LayoutParams *layoutParams = 0)
		: UI::StickyChoice(entry.name, "", layoutParams), entry_(entry) {}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		w = 300;
		h = 164;
	}

	StoreEntry GetEntry() const { return entry_; }

private:
	const StoreEntry entry_;
};

// This is a "details" view of a game. Lets you install it.
class ProductView : public UI::LinearLayout {
public:
	ProductView(const StoreEntry &entry)
		: LinearLayout(UI::ORIENT_VERTICAL), entry_(entry) {
		CreateViews();
	}

	void Update() override;

	UI::Event OnClickLaunch;

private:
	void CreateViews();
	UI::EventReturn OnInstall(UI::EventParams &e);
	UI::EventReturn OnCancel(UI::EventParams &e);
	UI::EventReturn OnLaunchClick(UI::EventParams &e);

	bool IsGameInstalled() {
		return g_GameManager.IsGameInstalled(entry_.file);
	}
	std::string DownloadURL();

	StoreEntry entry_;
	UI::Button *uninstallButton_ = nullptr;
	UI::Button *installButton_ = nullptr;
	UI::Button *launchButton_ = nullptr;
	UI::Button *cancelButton_ = nullptr;
	bool wasInstalled_ = false;
};

void ProductView::CreateViews() {
	using namespace UI;
	Clear();

	if (!entry_.iconURL.empty()) {
		Add(new HttpImageFileView(&g_DownloadManager, ResolveUrl(StoreBaseUrl(), entry_.iconURL), IS_FIXED))->SetFixedSize(144, 88);
	}
	Add(new TextView(entry_.name))->SetBig(true);
	Add(new TextView(entry_.author));

	auto st = GetI18NCategory(I18NCat::STORE);
	auto di = GetI18NCategory(I18NCat::DIALOG);
	wasInstalled_ = IsGameInstalled();
	bool isDownloading = g_GameManager.IsDownloading(DownloadURL());
	if (!wasInstalled_) {
		launchButton_ = nullptr;
		LinearLayout *progressDisplay = new LinearLayout(ORIENT_HORIZONTAL);
		installButton_ = progressDisplay->Add(new Button(st->T("Install")));
		installButton_->OnClick.Handle(this, &ProductView::OnInstall);
		uninstallButton_ = nullptr;

		Add(progressDisplay);
	} else {
		installButton_ = nullptr;
		launchButton_ = new Button(st->T("Launch Game"));
		launchButton_->OnClick.Handle(this, &ProductView::OnLaunchClick);
		Add(launchButton_);
		uninstallButton_ = new Button(st->T("Uninstall"));
		Add(uninstallButton_)->OnClick.Add([=](UI::EventParams &e) {
			g_GameManager.UninstallGameOnThread(entry_.file);
			return UI::EVENT_DONE;
		});
		// Add(new TextView(st->T("Installed")));  // Not really needed
	}

	cancelButton_ = Add(new Button(di->T("Cancel")));
	cancelButton_->OnClick.Handle(this, &ProductView::OnCancel);
	cancelButton_->SetVisibility(isDownloading ? V_VISIBLE : V_GONE);

	// Add star rating, comments etc?

	// Draw each line separately so focusing can scroll.
	std::vector<std::string> lines;
	SplitString(entry_.description, '\n', lines);
	for (auto &line : lines) {
		Add(new TextView(line, ALIGN_LEFT | FLAG_WRAP_TEXT, false))->SetFocusable(true);
	}

	float size = entry_.size / (1024.f * 1024.f);
	Add(new TextView(StringFromFormat("%s: %.2f %s", st->T_cstr("Size"), size, st->T_cstr("MB"))));

	if (!entry_.license.empty()) {
		LinearLayout *horiz = Add(new LinearLayout(ORIENT_HORIZONTAL));
		horiz->Add(new TextView(StringFromFormat("%s: %s", st->T_cstr("License"), entry_.license.c_str()), new LinearLayoutParams(0.0, G_VCENTER)));
		horiz->Add(new Button(di->T("More information..."), new LinearLayoutParams(0.0, G_VCENTER)))->OnClick.Add([this](UI::EventParams) {
			std::string url = StringFromFormat("https://www.ppsspp.org/docs/reference/homebrew-store-distribution/#%s", entry_.file.c_str());
			System_LaunchUrl(LaunchUrlType::BROWSER_URL, url.c_str());
			return UI::EVENT_DONE;
		});
	}
	if (!entry_.websiteURL.empty()) {
		// Display in a few different ways depending on the URL
		size_t slashes = std::count(entry_.websiteURL.begin(), entry_.websiteURL.end(), '/');
		std::string buttonText;
		if (slashes == 2) {
			// Just strip https and show the URL.
			std::string_view name = StripPrefix("https://", entry_.websiteURL);
			name = StripPrefix("http://", name);
			if (name.size() < entry_.websiteURL.size()) {
				buttonText = name;
			}
		}
		if (buttonText.empty()) {
			// Fall back
			buttonText = st->T("Website");
		}
		Add(new Button(buttonText))->OnClick.Add([this](UI::EventParams) {
			System_LaunchUrl(LaunchUrlType::BROWSER_URL, entry_.websiteURL.c_str());
			return UI::EVENT_DONE;
		});
	}
}

void ProductView::Update() {
	if (wasInstalled_ != IsGameInstalled()) {
		CreateViews();
	}
	if (installButton_) {
		installButton_->SetEnabled(g_GameManager.GetState() == GameManagerState::IDLE);
	}
	if (uninstallButton_) {
		uninstallButton_->SetEnabled(g_GameManager.GetState() == GameManagerState::IDLE);
	}
	if (g_GameManager.GetState() != GameManagerState::DOWNLOADING) {
		if (cancelButton_)
			cancelButton_->SetVisibility(UI::V_GONE);
	}
	if (launchButton_)
		launchButton_->SetEnabled(g_GameManager.GetState() == GameManagerState::IDLE);
	View::Update();
}

std::string ProductView::DownloadURL() {
	if (entry_.downloadURL.empty()) {
		// Construct the URL.
		return StoreBaseUrl() + "files/" + entry_.file + ".zip";
	} else {
		// Use the provided URL, for external hosting.
		return entry_.downloadURL;
	}
}

UI::EventReturn ProductView::OnInstall(UI::EventParams &e) {
	std::string fileUrl = DownloadURL();
	if (installButton_) {
		installButton_->SetEnabled(false);
	}
	if (cancelButton_) {
		cancelButton_->SetVisibility(UI::V_VISIBLE);
	}
	INFO_LOG(Log::System, "Triggering install of '%s'", fileUrl.c_str());
	g_GameManager.DownloadAndInstall(fileUrl);
	return UI::EVENT_DONE;
}

UI::EventReturn ProductView::OnCancel(UI::EventParams &e) {
	g_GameManager.CancelDownload();
	return UI::EVENT_DONE;
}

UI::EventReturn ProductView::OnLaunchClick(UI::EventParams &e) {
	if (g_GameManager.GetState() != GameManagerState::IDLE) {
		// Button should have been disabled. Just a safety check.
		return UI::EVENT_DONE;
	}

	Path pspGame = GetSysDirectory(DIRECTORY_GAME);
	Path path = pspGame / entry_.file;
	UI::EventParams e2{};
	e2.v = e.v;
	e2.s = path.ToString();
	// Insta-update - here we know we are already on the right thread.
	OnClickLaunch.Trigger(e2);
	return UI::EVENT_DONE;
}

StoreScreen::StoreScreen() {
	lang_ = g_Config.sLanguageIni;
	loading_ = true;

	std::string indexPath = StoreBaseUrl() + "index.json";
	const char *acceptMime = "application/json, */*; q=0.8";
	listing_ = g_DownloadManager.StartDownload(indexPath, Path(), http::ProgressBarMode::DELAYED, acceptMime);
}

StoreScreen::~StoreScreen() {
	g_DownloadManager.CancelAll();
}

// Handle async download tasks
void StoreScreen::update() {
	UIDialogScreenWithBackground::update();

	g_DownloadManager.Update();

	if (listing_.get() != 0 && listing_->Done()) {
		resultCode_ = listing_->ResultCode();
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
			ERROR_LOG(Log::IO, "Download failed : error code %d", resultCode_);
			connectionError_ = true;
			loading_ = false;
			RecreateViews();
		}

		// Forget the listing.
		listing_.reset();
	}
}

void StoreScreen::ParseListing(const std::string &json) {
	using namespace json;
	JsonReader reader(json.c_str(), json.size());
	if (!reader.ok() || !reader.root()) {
		ERROR_LOG(Log::IO, "Error parsing JSON from store");
		connectionError_ = true;
		RecreateViews();
		return;
	}
	const JsonGet root = reader.root();
	const JsonNode *entries = root.getArray("entries");
	if (entries) {
		entries_.clear();
		for (const JsonNode *pgame : entries->value) {
			JsonGet game = pgame->value;
			StoreEntry e{};
			e.type = ENTRY_PBPZIP;
			e.name = GetTranslatedString(game, "name");
			e.description = GetTranslatedString(game, "description", "");
			e.author = ReplaceAll(game.getStringOr("author", "?"), "&&", "&");  // Can't remove && in the JSON source data due to old app versions, so we do the opposite replacement here.
			e.size = game.getInt("size");
			e.downloadURL = game.getStringOr("download-url", "");
			e.iconURL = game.getStringOr("icon-url", "");
			e.contentRating = game.getInt("content-rating", 0);
			e.websiteURL = game.getStringOr("website-url", "");
			e.license = game.getStringOr("license", "");
#if PPSSPP_PLATFORM(IOS_APP_STORE)
			if (e.contentRating >= 100) {
				continue;
			}
#endif
			e.hidden = false;  // NOTE: Handling of the "hidden" flag is broken in old versions of PPSSPP. Do not use.
			const char *file = game.getStringOr("file", nullptr);
			if (!file)
				continue;
			e.file = file;
			entries_.push_back(e);
		}
	}
}

void StoreScreen::CreateViews() {
	using namespace UI;

	root_ = new LinearLayout(ORIENT_VERTICAL);
	
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto mm = GetI18NCategory(I18NCat::MAINMENU);

	// Top bar
	LinearLayout *topBar = root_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, 64.0f)));
	topBar->Add(new Choice(di->T("Back"), new LinearLayoutParams(WRAP_CONTENT, FILL_PARENT)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	titleText_ = new TextView(mm->T("PPSSPP Homebrew Store"), ALIGN_VCENTER, false, new LinearLayoutParams(WRAP_CONTENT, FILL_PARENT));
	topBar->Add(titleText_);
	UI::Drawable solid(0xFFbd9939);
	topBar->SetBG(solid);

	LinearLayout *content;
	if (connectionError_ || loading_) {
		auto st = GetI18NCategory(I18NCat::STORE);
		content = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f));
		content->Add(new TextView(loading_ ? std::string(st->T("Loading...")) : StringFromFormat("%s: %d", st->T_cstr("Connection Error"), resultCode_)));
		if (!loading_) {
			content->Add(new Button(di->T("Retry")))->OnClick.Handle(this, &StoreScreen::OnRetry);

		}
		content->Add(new Button(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

		scrollItemView_ = nullptr;
		productPanel_ = nullptr;
	} else {
		content = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f));
		ScrollView *leftScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(WRAP_CONTENT, FILL_PARENT, 0.4f));
		leftScroll->SetTag("StoreMainList");
		content->Add(leftScroll);
		scrollItemView_ = new LinearLayoutList(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT));
		leftScroll->Add(scrollItemView_);

		for (size_t i = 0; i < entries_.size(); i++) {
			scrollItemView_->Add(new ProductItemView(entries_[i]))->OnClick.Handle(this, &StoreScreen::OnGameSelected);
		}

		// TODO: Similar apps, etc etc
		productPanel_ = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(0.5f));
		leftScroll->SetTag("StoreMainProduct");
		content->Add(productPanel_);

		ProductItemView *selectedItem = GetSelectedItem();
		if (selectedItem) {
			ProductView *productView = new ProductView(selectedItem->GetEntry());
			productView->OnClickLaunch.Handle(this, &StoreScreen::OnGameLaunch);
			productPanel_->Add(productView);

			selectedItem->Press();
		} else {
			lastSelectedName_.clear();
		}
	}
	root_->Add(content);
}

ProductItemView *StoreScreen::GetSelectedItem() {
	for (int i = 0; i < scrollItemView_->GetNumSubviews(); ++i) {
		ProductItemView *item = static_cast<ProductItemView *>(scrollItemView_->GetViewByIndex(i));
		if (item->GetEntry().name == lastSelectedName_)
			return item;
	}

	return nullptr;
}

UI::EventReturn StoreScreen::OnGameSelected(UI::EventParams &e) {
	ProductItemView *item = static_cast<ProductItemView *>(e.v);
	if (!item)
		return UI::EVENT_DONE;

	productPanel_->Clear();
	ProductView *productView = new ProductView(item->GetEntry());
	productView->OnClickLaunch.Handle(this, &StoreScreen::OnGameLaunch);
	productPanel_->Add(productView);

	ProductItemView *previousItem = GetSelectedItem();
	if (previousItem && previousItem != item)
		previousItem->Release();
	lastSelectedName_ = item->GetEntry().name;
	return UI::EVENT_DONE;
}

UI::EventReturn StoreScreen::OnGameLaunch(UI::EventParams &e) {
	std::string path = e.s;
	screenManager()->switchScreen(new EmuScreen(Path(path)));
	return UI::EVENT_DONE;
}

UI::EventReturn StoreScreen::OnRetry(UI::EventParams &e) {
	RecreateViews();
	return UI::EVENT_DONE;
}

std::string StoreScreen::GetTranslatedString(const json::JsonGet json, const std::string &key, const char *fallback) const {
	json::JsonGet dict = json.getDict("en_US");
	if (dict && json.hasChild(lang_.c_str(), JSON_OBJECT)) {
		if (json.getDict(lang_.c_str()).hasChild(key.c_str(), JSON_STRING)) {
			dict = json.getDict(lang_.c_str());
		}
	}
	const char *str = nullptr;
	if (dict) {
		str = dict.getStringOr(key.c_str(), nullptr);
	}
	if (str) {
		return std::string(str);
	} else {
		return fallback ? fallback : "(error)";
	}
}
