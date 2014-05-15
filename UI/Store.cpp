#include "native/ext/vjson/json.h"

#include "i18n/i18n.h"
#include "ui/ui_context.h"
#include "ui/viewgroup.h"
#include "gfx_es2/draw_buffer.h"

#include "Common/Log.h"
#include "Core/Config.h"
#include "UI/Store.h"

const std::string storeBaseUrl = "http://store.ppsspp.org/";

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

// This is a "details" view of a game. Let's you install it.
class ProductView : public UI::LinearLayout {
public:
	ProductView(const StoreEntry &entry) : LinearLayout(UI::ORIENT_VERTICAL), entry_(entry), installButton_(0) {
		CreateViews();
	}

	virtual void Update(const InputState &input_state);

private:
	void CreateViews();
	UI::EventReturn OnInstall(UI::EventParams &e);
	UI::EventReturn OnUninstall(UI::EventParams &e);

	StoreEntry entry_;
	UI::Button *installButton_;
};

void ProductView::CreateViews() {
	using namespace UI;
	Clear();

	Add(new TextView(entry_.name));
	Add(new TextView(entry_.author));

	I18NCategory *st = GetI18NCategory("Store");
	if (!g_GameManager.IsGameInstalled(entry_.file)) {
		installButton_ = Add(new Button(st->T("Install")));
		installButton_->OnClick.Handle(this, &ProductView::OnInstall);
	} else {
		Add(new TextView(st->T("Already Installed")));
		Add(new Button(st->T("Uninstall")))->OnClick.Handle(this, &ProductView::OnUninstall);
	}

	// Add star rating, comments etc?
	Add(new TextView(entry_.description));

	float size = entry_.size / (1024.f * 1024.f);
	char temp[256];
	sprintf(temp, "%s: %.2f %s", st->T("Size"), size, st->T("MB"));

	Add(new TextView(temp));
}

void ProductView::Update(const InputState &input_state) {
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

			ParseListing(listingJson);
			RecreateViews();
		} else {
			// Failed to contact store. Don't do anything.
			connectionError_ = true;
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
		content->Add(leftScroll);
		LinearLayout *scrollItemView = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT));
		leftScroll->Add(scrollItemView);

		std::vector<StoreEntry> entries = FilterEntries();
		for (size_t i = 0; i < entries.size(); i++) {
			scrollItemView->Add(new ProductItemView(entries_[i]))->OnClick.Handle(this, &StoreScreen::OnGameSelected);
		}

		// TODO: Similar apps, etc etc
		productPanel_ = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(0.5f));
		content->Add(productPanel_);
	}
	root_->Add(content);
}

std::vector<StoreEntry> StoreScreen::FilterEntries() {
	std::vector<StoreEntry> filtered;
	for (size_t i = 0; i < entries_.size(); i++) {
		// TODO: Actually filter by category etc.
		filtered.push_back(entries_[i]);
	}
	return filtered;
}

UI::EventReturn StoreScreen::OnGameSelected(UI::EventParams &e) {
	ProductItemView *item = static_cast<ProductItemView *>(e.v);
	if (!item)
		return UI::EVENT_DONE;

	productPanel_->Clear();
	productPanel_->Add(new ProductView(item->GetEntry()));
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
