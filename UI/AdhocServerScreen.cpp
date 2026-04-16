#include <algorithm>
#include <limits>

#include "ppsspp_config.h"

#undef new
#ifdef SYSTEM_RAPIDJSON
#include <rapidjson/document.h>
#else
#include "ext/rapidjson/include/rapidjson/document.h"
#endif
#include "ext/pugixml/pugixml.hpp"
#include "Common/DbgNew.h"

#include "AdhocServerScreen.h"
#include "Core/Util/GameDB.h"

#include "Common/Net/Resolve.h"
#include "Common/UI/Root.h"
#include "Common/UI/PopupScreens.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/StringUtils.h"
#include "Common/Net/HTTPClient.h"
#include "Core/HLE/sceNetAdhoc.h"
#include "UI/MiscViews.h"

static void UpgradeGameName(std::string *str) {
	if (str->size() == 9) {  // TODO: Make a better heuristic, we might make some failed lookup into the DB.
		// It's probably a game ID. Convert it to a name using the database.
		std::vector<GameDBInfo> infos;
		if (g_gameDB.GetGameInfos(*str, &infos)) {
			*str = infos[0].title;
		}
	}
}

static int ParseUserCountValue(const rapidjson::Value &v) {
	if (v.IsInt())
		return v.GetInt();
	else if (v.IsString()) {
		int value = 0;
		if (TryParse(v.GetString(), &value))
			return value;
	}
	return 0;
}

static int ParsePortValue(const rapidjson::Value &v) {
	if (v.IsInt())
		return v.GetInt();
	return -1;
}

static std::string RemoveHttpsIfNeeded(std::string_view url) {
	if (!System_GetPropertyBool(SYSPROP_SUPPORTS_HTTPS)) {
		// Try with http. Needed on Linux installs currently.
		if (startsWith(url, "https://")) {
			return "http://" + std::string(url.substr(8));
		}
	}
	return std::string(url);
}

std::vector<AdhocGame> ParseStatusXML(const std::string& xmlInput) {
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_string(xmlInput.c_str());

	std::vector<AdhocGame> gameList;
	if (!result) {
		ERROR_LOG(Log::sceNet, "XML Parsing Error: %s", result.description());
		return gameList;
	}

	// Root is <prometheus>
	pugi::xml_node prometheus = doc.child("prometheus");

	for (pugi::xml_node xmlGame : prometheus.children("game")) {
		AdhocGame game;
		game.name = xmlGame.attribute("name").as_string();
		game.usercount = xmlGame.attribute("usercount").as_int();

		for (pugi::xml_node xmlGroup : xmlGame.children("group")) {
			AdhocGroup group;
			group.name = xmlGroup.attribute("name").as_string();
			group.usercount = xmlGroup.attribute("usercount").as_int();

			for (pugi::xml_node xmlUser : xmlGroup.children("user")) {
				AdhocUser user;
				// In XML, the username is the text inside the <user> tag
				user.name = xmlUser.child_value();
				group.users.push_back(user);
			}
			game.groups.push_back(group);
		}
		gameList.push_back(game);
	}

	return gameList;
}

std::vector<AdhocGame> ParseDataJson(std::string_view json) {
	rapidjson::Document d;
	d.Parse(json.data(), json.size());

	std::vector<AdhocGame> gameList;

	if (d.HasParseError() || !d.IsObject() || !d.HasMember("games") || !d["games"].IsArray())
		return gameList;

	const auto& gamesArray = d["games"];
	for (auto& g : gamesArray.GetArray()) {
		if (!g.IsObject())
			continue;

		AdhocGame game;
		if (!g.HasMember("name") || !g["name"].IsString())
			continue;
		game.name = g["name"].GetString();
		UpgradeGameName(&game.name);

		game.usercount = g.HasMember("usercount") ? ParseUserCountValue(g["usercount"]) : 0;

		if (g.HasMember("game_ids") && g["game_ids"].IsArray()) {
			for (auto& id : g["game_ids"].GetArray()) {
				if (!id.IsString())
					continue;
				game.game_ids.push_back(id.GetString());
			}
		}

		if (g.HasMember("groups") && g["groups"].IsArray()) {
			for (auto& grp : g["groups"].GetArray()) {
				if (!grp.IsObject())
					continue;

				AdhocGroup group;
				if (!grp.HasMember("name") || !grp["name"].IsString())
					continue;
				group.name = grp["name"].GetString();
				group.usercount = grp.HasMember("usercount") ? ParseUserCountValue(grp["usercount"]) : 0;

				if (grp.HasMember("users") && grp["users"].IsArray()) {
					for (auto& u : grp["users"].GetArray()) {
						if (!u.IsObject() || !u.HasMember("name") || !u["name"].IsString())
							continue;

						AdhocUser user;
						user.name = u["name"].GetString();

						if (u.HasMember("pdp_ports") && u["pdp_ports"].IsArray()) {
							for (auto& p : u["pdp_ports"].GetArray()) {
								int port = ParsePortValue(p);
								if (port >= 0)
									user.pdp_ports.push_back(port);
							}
						}

						if (u.HasMember("ptp_ports") && u["ptp_ports"].IsArray()) {
							for (auto& p : u["ptp_ports"].GetArray()) {
								int port = ParsePortValue(p);
								if (port >= 0)
									user.ptp_ports.push_back(port);
							}
						}

						group.users.push_back(user);
					}
				}
				game.groups.push_back(group);
			}
		}
		gameList.push_back(game);
	}
	return gameList;
}

class AdhocAddServerPopupScreen : public UI::PopupScreen {
public:
	AdhocAddServerPopupScreen(std::string *outEditValue) : PopupScreen(T(I18NCat::NETWORKING, "Add server"), T(I18NCat::DIALOG, "Add"), T(I18NCat::DIALOG, "Cancel")), outEditValue_(outEditValue) {
	}

	void CreatePopupContents(UI::ViewGroup *parent) override {
		using namespace UI;
		auto ni = GetI18NCategory(I18NCat::NETWORKING);

		PopupTextInputChoice *textInputChoice = parent->Add(new PopupTextInputChoice(GetRequesterToken(), &editValue_, ni->T("Hostname or IP"), "", 450, screenManager()));
		textInputChoice->SetShadowText(ni->T("Hostname or IP"));
		parent->Add(new CheckBox(&hasRelay_, ni->T("Relay server mode")));
	}

	virtual void OnCompleted(DialogResult result) override {
		if (result == DialogResult::DR_OK) {
			std::vector<AdhocServerListEntry> servers = AdhocGetServerList(AdhocLoadListMode::CacheOnlySync);
			bool preset = false;
			for (auto &iter : servers) {
				if (equalsNoCase(editValue_, iter.host)) {
					// We have this predefined.
					preset = true;
				}
			}
			if (!preset) {
				if (hasRelay_) {
					// Insert at the start of the vector.
					if (!ContainsNoCase(g_Config.vCustomAdhocServerListWithRelay, editValue_) &&
						!ContainsNoCase(g_Config.vCustomAdhocServerList, editValue_)) {
						g_Config.vCustomAdhocServerListWithRelay.insert(g_Config.vCustomAdhocServerListWithRelay.begin(), editValue_);
					}
				} else {
					if (!ContainsNoCase(g_Config.vCustomAdhocServerList, editValue_) &&
						!ContainsNoCase(g_Config.vCustomAdhocServerListWithRelay, editValue_)) {
						g_Config.vCustomAdhocServerList.insert(g_Config.vCustomAdhocServerList.begin(), editValue_);
					}
				}
			}
			*outEditValue_ = editValue_;
		}
	}
	virtual bool CanComplete(DialogResult result) override { return result == DR_OK ? !editValue_.empty() : true; }

	const char *tag() const override { return "AdhocAddServerPopup"; }

private:
	std::string editValue_;
	std::string *outEditValue_;
	bool hasRelay_ = false;
};

AdhocServerCompactInfo::AdhocServerCompactInfo(const AdhocServerListEntry &entry, UI::LayoutParams *layoutParams)
	: UI::LinearLayout(ORIENT_HORIZONTAL, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::WRAP_CONTENT, UI::Margins(5.0f, 0.0f))), entry_(entry) {
	using namespace UI;

	SetSpacing(5.0f);

	LinearLayout *lines = Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(Margins(5, 5))));
	lines->SetSpacing(0.0f);
	TextView *name = lines->Add(new TextView(entry.name));

	std::string secondLine = entry.host;
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	if (!entry.location.empty()) {
		secondLine += ": " + entry.location;
	}

	lines->Add(new TextView(secondLine))->SetTextSize(TextSize::Small)->SetWordWrap();

	Add(new Spacer(0.0f, new LinearLayoutParams(1.0f, Margins(0.0f, 5.0f))));

	if (entry.mode == AdhocDataMode::AemuPostoffice) {
		TextView *relay = Add(new TextView(n->T("Relay"), new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, Gravity::G_VCENTER, Margins(10.0))));
	}

	Add(new Choice(ImageID("I_FILE_COPY"), new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)))->OnClick.Add([host = entry_.host](UI::EventParams &) {
		System_CopyStringToClipboard(host);
	});
}

void AdhocServerCompactInfo::Draw(UIContext &dc) {
	UI::LinearLayout::Draw(dc);
	// Underline
	dc.Draw()->DrawImageCenterTexel(dc.GetTheme().whiteImage, bounds_.x, bounds_.y2() - 2, bounds_.x2(), bounds_.y2(), dc.GetTheme().popupTitleStyle.fgColor);
}

static UI::View *CreateInfoItemWithButton(std::string_view text, ImageID buttonImage, std::function<void(UI::EventParams &)> onClick) {
	using namespace UI;
	LinearLayout *line = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(12, 0)));
	line->Add(new TextView(text, new LinearLayoutParams(0.0f, Gravity::G_VCENTER)));
	line->Add(new Spacer(0, new LinearLayoutParams(1.0f)));
	line->Add(new Choice(buttonImage, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)))->OnClick.Add(onClick);
	return line;
}

static UI::View *CreateLinkButton(std::string url, std::string_view title = "") {
	using namespace UI;

	// steal strings from all over the place
	auto cr = GetI18NCategory(I18NCat::PSPCREDITS);
	auto st = GetI18NCategory(I18NCat::STORE);

	ImageID icon = ImageID("I_LINK_OUT_QUESTION");
	if (startsWith(url, "https://discord")) {
		icon = ImageID("I_LOGO_DISCORD");
		if (title.empty())
			title = cr->T("Discord");
	} else {
		icon = ImageID("I_LINK_OUT");
		if (title.empty())
			title = st->T("Website");
	}

	Choice *choice = new Choice(title, icon, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT));
	choice->OnClick.Add([url](UI::EventParams &) {
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, url);
	});
	return choice;
}

// Later, this might also show games-in-progress.
// For now, it's just a simple metadata viewer.
AdhocServerInfoScreen::AdhocServerInfoScreen(const AdhocServerListEntry &entry)
	: UI::PopupScreen("", T(I18NCat::DIALOG, "Back")), entry_(entry) {

	std::string dataUrl;
	if (!entry.dataJsonUrl.empty()) {
		dataUrl = RemoveHttpsIfNeeded(entry.dataJsonUrl);
	} else if (!entry.statusXmlUrl.empty()) {
		dataUrl = RemoveHttpsIfNeeded(entry.statusXmlUrl);
	}

	if (!dataUrl.empty()) {
		statusRequest_ = g_DownloadManager.StartDownload(dataUrl, Path(), http::RequestFlags::KeepInMemory, nullptr, "status");
	}
}

void CreateAdhocServerGameList(UI::ViewGroup *content, const std::vector<AdhocGame> &games, bool requestInProgress) {
	using namespace UI;
	auto ni = GetI18NCategory(I18NCat::NETWORKING);
	if (games.empty()) {
		if (requestInProgress) {
			// Still loading. Show a spinner.
			content->Add(new Spinner(nullptr, 0, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 1.0f, Gravity::G_CENTER)));
		} else {
			content->Add(new TextView(ni->T("No games in progress on this server")));
		}
		return;
	}
	for (const AdhocGame &game : games) {
		std::string title = game.name + " - " + ApplySafeSubstitutions(ni->T("players: %1"), game.usercount) + " " + ApplySafeSubstitutions(ni->T("groups: %1"), (int)game.groups.size());
		CollapsibleSection *gameSection = content->Add(new CollapsibleSection(title));
		gameSection->Header()->SetUnderline(false);
		for (const AdhocGroup &group : game.groups) {
			std::string groupName = group.name;
			if (groupName.empty()) {
				groupName = "???";
			}
			if (group.usercount >= 1 && groupName == "Groupless") {
				gameSection->Add(new TextView("  " + ApplySafeSubstitutions(ni->T("Players waiting: %1"), group.usercount)))->SetTextSize(TextSize::Small);
				continue;
			}
			gameSection->Add(new TextView("  " + groupName + " - " + ApplySafeSubstitutions(ni->T("players: %1"), group.usercount)))->SetTextSize(TextSize::Small);
			for (const AdhocUser &user : group.users) {
				std::string portInfo;
				if (!user.pdp_ports.empty()) {
					portInfo += "PDP: ";
					for (int port : user.pdp_ports) {
						portInfo += std::to_string(port) + " ";
					}
				}
				if (!user.ptp_ports.empty()) {
					portInfo += "PTP: ";
					for (int port : user.ptp_ports) {
						portInfo += std::to_string(port) + " ";
					}
				}
				gameSection->Add(new TextView("    " + user.name + " " + portInfo))->SetTextSize(TextSize::Tiny);
			}
		}
		gameSection->SetOpen(false);  // NOTE: Must be last!
	}
}

void AdhocServerInfoScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	auto pa = GetI18NCategory(I18NCat::PAUSE);
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto ni = GetI18NCategory(I18NCat::NETWORKING);

	Margins contentMargins(12, 0);

	parent->Add(new AdhocServerCompactInfo(entry_, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, contentMargins)));
	parent->Add(new Spacer(5.0f));

	ScrollView *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f));
	LinearLayout *content = new LinearLayout(ORIENT_VERTICAL);
	content->SetSpacing(6.0f);
	if (!entry_.ip.empty()) {
		content->Add(new InfoItem(entry_.ip, ""));
	}
	TextView *desc = content->Add(new TextView(entry_.description, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, contentMargins)));
	desc->SetTextSize(TextSize::Small);
	desc->SetWordWrap();

	LinearLayout *buttonStrip = content->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, contentMargins)));
	buttonStrip->SetSpacing(8);
	if (!entry_.web.empty() || !entry_.discord.empty()) {
		if (!entry_.web.empty()) {
			buttonStrip->Add(CreateLinkButton(entry_.web));
		}
		if (!entry_.discord.empty()) {
			buttonStrip->Add(CreateLinkButton(entry_.discord));
		}
	}

	if (entry_.dataJsonUrl.empty() && entry_.statusXmlUrl.empty()) {
		content->Add(CreateInfoItemWithButton(ni->T("This server has no data.json status page"), ImageID("I_LINK_OUT_QUESTION"), [](UI::EventParams &e) {
			System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org/docs/multiplayer/adhoc-server-status/");
		}));
		if (!entry_.statusWebUrl.empty()) {
			buttonStrip->Add(CreateLinkButton(entry_.statusWebUrl, ni->T("Status")));
		}
		if (!entry_.statusXmlUrl.empty()) {
			buttonStrip->Add(CreateLinkButton(entry_.statusXmlUrl, ni->T("Status")));
		}
	} else {
		CreateAdhocServerGameList(content, games_, statusRequest_.get() ? true : false);
	}

	scroll->Add(content);
	parent->Add(scroll);
}

void AdhocServerInfoScreen::update() {
	UI::PopupScreen::update();
	if (statusRequest_ && statusRequest_->Done()) {
		std::string data;
		statusRequest_->buffer().TakeAll(&data);
		if (endsWith(statusRequest_->url(), ".xml")) {
			games_ = ParseStatusXML(data);
		} else {
			games_ = ParseDataJson(data);
		}
		statusRequest_.reset();
		RecreateViews();
	}
}

void AddDeleteButton(std::string *editValue, ScreenManager *screenManager, UI::ViewGroup *viewGroup, const AdhocServerListEntry &entry) {
	using namespace UI;
	Choice *deleteButton = viewGroup->Add(new Choice(ImageID("I_TRASHCAN"), new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, Gravity::G_VCENTER, Margins(0, 0, 10, 0))));
	deleteButton->OnClick.Add([host = entry.host, screenManager, editValue](UI::EventParams &e) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		const std::string quotedHost = "\"" + host + "\"";
		const std::string message = ApplySafeSubstitutions(di->T("Are you sure you want to delete %1?"), quotedHost);
		screenManager->push(new UI::MessagePopupScreen(di->T("Delete"), message, di->T("Delete"), di->T("Cancel"), [host, editValue](bool confirmed) {
			if (confirmed) {
				RemoveNoCase(g_Config.vCustomAdhocServerList, host);
				RemoveNoCase(g_Config.vCustomAdhocServerListWithRelay, host);
				if (*editValue == host) {
					// Reset to socom.cc, which will always be in a list.
					*editValue = DefaultProAdhocServer();
				}
			}
			}));
		});
}

class AdhocServerRow : public UI::LinearLayout {
public:
	AdhocServerRow(std::string *value, const AdhocServerListEntry &entry, bool showDeleteButton, ScreenManager *screenManager, UI::LayoutParams *layoutParams = nullptr);

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		w = 500; h = 90;
	}

	void Draw(UIContext &dc) override {
		dc.FillRect(dc.GetTheme().itemStyle.background, bounds_);
		if (*value_ == entry_.host) {
			// TODO: Make this highlight themable
			dc.FillRect(UI::Drawable(0x48FFFFFF), GetBounds());
		}
		LinearLayout::Draw(dc);
	}

	bool Touch(const TouchInput &input) override {
		using namespace UI;
		if (UI::LinearLayout::Touch(input)) {
			return true;
		}
		if (input.flags & TouchInputFlags::DOWN) {
			if (bounds_.Contains(input.x, input.y)) {
				dragging_ = true;
				return true;
			}
		}
		if (dragging_ && (input.flags & TouchInputFlags::UP)) {
			dragging_ = false;
			if (!(input.flags & TouchInputFlags::CANCEL) && bounds_.Contains(input.x, input.y)) {
				EventParams e;
				e.v = this;
				OnSelected.Trigger(e);
				return true;
			}
		}
		return false;
	}

	UI::Event OnSelected;

private:
	bool dragging_ = false;
	std::string *value_;
	AdhocServerListEntry entry_;
};

AdhocServerRow::AdhocServerRow(std::string *editValue, const AdhocServerListEntry &entry, bool showDeleteButton, ScreenManager *screenManager, UI::LayoutParams *layoutParams)
	: UI::LinearLayout(ORIENT_HORIZONTAL, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::WRAP_CONTENT, UI::Margins(5.0f, 0.0f))), value_(editValue), entry_(entry) {
	using namespace UI;

	SetSpacing(5.0f);
	// Show as radio button to make it really clear that selection actually is the choice.
	int number = 0;
	Add(new ImageView([editValue, host = entry.host]() { return host == *editValue ? ImageID("I_RADIO_SELECTED") : ImageID("I_RADIO_EMPTY"); },
		new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, Gravity::G_VCENTER, Margins(5, 0, 0, 0))));

	LinearLayout *lines = Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(Margins(5, 5))));
	lines->SetSpacing(0.0f);
	ClickableTextView *name = lines->Add(new ClickableTextView(entry.name));
	name->SetFocusable(true);
	name->OnClick.Add([this](UI::EventParams &e) {
		EventParams e2;
		e2.v = this;
		OnSelected.Trigger(e2);
	});

	std::string secondLine = entry.host;
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	if (entry.host == "localhost") {
		// Special case this to add a hint.
		secondLine = n->T("Ad hoc server address hint");
	}
	if (!entry.location.empty()) {
		secondLine += ": " + entry.location;
	}

	lines->Add(new TextView(secondLine))->SetTextSize(TextSize::Small)->SetWordWrap();

	Add(new Spacer(0.0f, new LinearLayoutParams(1.0f, Margins(0.0f, 5.0f))));

	if (entry.mode == AdhocDataMode::AemuPostoffice) {
		TextView *relay = Add(new TextView(n->T("Relay"), new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, Gravity::G_VCENTER, Margins(10.0))));
	}
	if (showDeleteButton) {
		AddDeleteButton(editValue, screenManager, this, entry);
	}

	if (!entry.description.empty()) {
		Choice *infoButton = Add(new Choice(ImageID("I_INFO"), new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, Gravity::G_VCENTER, Margins(0, 0, 10, 0))));
		AdhocServerListEntry copy = entry;
		infoButton->OnClick.Add([this, copy, screenManager](UI::EventParams &e) {
			e.v = this;
			screenManager->push(new AdhocServerInfoScreen(copy));
		});
	}
}

AdhocServerScreen::AdhocServerScreen(std::string *value, std::string_view title)
	: UI::PopupScreen(title, T(I18NCat::DIALOG, "OK"), T(I18NCat::DIALOG, "Cancel")), value_(value) {
	resolver_ = std::thread([](AdhocServerScreen *thiz) {
		thiz->ResolverThread();
	}, this);
	editValue_ = *value;
	AdhocLoadServerList(AdhocLoadListMode::AllSourcesAsync);
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

	// We already kicked off loading the server list in the constructor, so by now it should be either loaded or in the process of loading.
	// We can get the cached list immediately, and then update it when the async load finishes.
	std::vector<AdhocServerListEntry> listItems = AdhocGetServerList(AdhocLoadListMode::CacheOnlySync);

	Choice *addServer = parent->Add(new Choice(n->T("Add server"), ImageID("I_PLUS")));
	addServer->OnClick.Add([this](UI::EventParams &e) {
		AdhocAddServerPopupScreen *addScreen = new AdhocAddServerPopupScreen(&editValue_);
		screenManager()->push(addScreen);
	});

	parent->Add(new Spacer(5.0f));

	// editValue_ has the currently selected server. On closing the dialog, we copy that to settings.

	// Start with the downloaded list.
	std::vector<AdhocServerListEntry> entries = listItems;
	std::vector<AdhocServerListEntry> localEntries;
	std::vector<AdhocServerListEntry> customEntries;

	bool currentServerFound = false;  // If the current server is not found, we'll have to add it to one of the lists.

	for (const auto &iter : entries) {
		if (iter.host == editValue_) {
			currentServerFound = true;
		}
	}

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

			if (ipAddress == editValue_) {
				currentServerFound = true;
			}
		}
	}

	auto hostInEntries = [entries](const std::string &host) {
		for (const auto &entry : entries) {
			if (entry.host == host) {
				return true;
			}
		}
		return false;
	};

	for (auto iter = g_Config.vCustomAdhocServerListWithRelay.begin(); iter != g_Config.vCustomAdhocServerListWithRelay.end();) {
		// If the host is already in the public list, skip it. We don't want duplicates.
		if (hostInEntries(*iter) || iter->empty()) {
			// Remove things that duplicate the public list, or that are empty (probably added by mistake).
			iter = g_Config.vCustomAdhocServerListWithRelay.erase(iter);
			recreateParent_ = true;
			continue;
		}
		AdhocServerListEntry entry;
		entry.name = *iter;
		entry.host = *iter;
		entry.mode = AdhocDataMode::AemuPostoffice;
		customEntries.push_back(entry);

		if (*iter == editValue_) {
			currentServerFound = true;
		}
		iter++;
	}

	for (auto iter = g_Config.vCustomAdhocServerList.begin(); iter != g_Config.vCustomAdhocServerList.end();) {
		// If the host is already in the public list, skip it. We don't want duplicates.
		if (hostInEntries(*iter) || iter->empty()) {
			// Remove things that duplicate the public list, or that are empty (probably added by mistake).
			iter = g_Config.vCustomAdhocServerList.erase(iter);
			recreateParent_ = true;
			continue;
		}
		AdhocServerListEntry entry;
		entry.name = *iter;
		entry.host = *iter;
		entry.mode = AdhocDataMode::P2P;
		customEntries.push_back(entry);

		if (*iter == editValue_) {
			currentServerFound = true;
		}
		iter++;
	}

	ScrollView *scrollView = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f));
	LinearLayout *innerView = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	innerView->SetSpacing(5.0f);

	auto AddButtonFromEntry = [this](UI::ViewGroup *parent, const AdhocServerListEntry &entry, bool showDeleteButton) {
		AdhocServerRow *row = new AdhocServerRow(&editValue_, entry, showDeleteButton, screenManager());
		parent->Add(row);
		row->OnSelected.Add([this](UI::EventParams &e) {
			std::string value = e.v->Tag();
			if (!value.empty()) {
				editValue_ = value;
			}
		});
		row->SetTag(entry.host);
	};

	if (!customEntries.empty()) {
		CollapsibleSection *customSection = innerView->Add(new CollapsibleSection(n->T("Custom server list"), new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));

		if (!currentServerFound) {
			// Add the entry to one of the lists.
			AdhocServerListEntry entry;
			entry.name = editValue_;
			entry.host = editValue_;
			// Let's do a heuristic, we don't have a good value here..
			entry.mode = (AdhocServerRelayMode)g_Config.iAdhocServerRelayMode == AdhocServerRelayMode::AlwaysOn ? AdhocDataMode::AemuPostoffice : AdhocDataMode::P2P;
			if (entry.mode == AdhocDataMode::AemuPostoffice) {
				g_Config.vCustomAdhocServerListWithRelay.insert(g_Config.vCustomAdhocServerListWithRelay.begin(), editValue_);
			} else {
				g_Config.vCustomAdhocServerList.insert(g_Config.vCustomAdhocServerList.begin(), editValue_);
			}
			recreateParent_ = true;
			AddButtonFromEntry(customSection, entry, true);
		}

		for (const auto &entry : customEntries) {
			AddButtonFromEntry(customSection, entry, true);
		}
	}

	CollapsibleSection *publicSection = innerView->Add(new CollapsibleSection(n->T("Public server list"), new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	for (const auto &entry : entries) {
		AddButtonFromEntry(publicSection, entry, false);
	}

	CollapsibleSection *localSection = innerView->Add(new CollapsibleSection(n->T("Local network addresses"), new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	for (const auto &entry : localEntries) {
		AddButtonFromEntry(localSection, entry, false);
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

bool AdhocServerNameIsCustom() {
	for (auto iter : g_Config.vCustomAdhocServerList) {
		if (iter == g_Config.sProAdhocServer) {
			return true;
		}
	}
	for (auto iter : g_Config.vCustomAdhocServerListWithRelay) {
		if (iter == g_Config.sProAdhocServer) {
			return true;
		}
	}
	return false;
}

static void EditServerName(std::string_view newServerName) {
	newServerName = StripSpaces(newServerName);
	if (newServerName != g_Config.sProAdhocServer) {
		for (auto &name : g_Config.vCustomAdhocServerList) {
			if (name == g_Config.sProAdhocServer) {
				name = newServerName;
			}
		}
		for (auto &name : g_Config.vCustomAdhocServerListWithRelay) {
			if (name == g_Config.sProAdhocServer) {
				name = newServerName;
			}
		}
		g_Config.sProAdhocServer = newServerName;
	}
}

void AskToEditCurrentServer(int requestToken, ScreenManager *screenManager) {
	using namespace UI;
	auto n = GetI18NCategory(I18NCat::NETWORKING);

	// Choose method depending on platform capabilities.
	if (System_GetPropertyBool(SYSPROP_HAS_TEXT_INPUT_DIALOG)) {
		System_InputBoxGetString(requestToken, n->T("Ad hoc server address"), g_Config.sProAdhocServer, false, [](const std::string &enteredValue, int) {
			EditServerName(enteredValue);
		});
		return;
	}
	static std::string editText = g_Config.sProAdhocServer;
	TextEditPopupScreen *popupScreen = new TextEditPopupScreen(&editText, editText, n->T("Ad hoc server address"), 256);
	if (System_GetPropertyBool(SYSPROP_KEYBOARD_IS_SOFT)) {
		popupScreen->SetAlignTop(true);
	}
	popupScreen->OnChange.Add([](EventParams &e) {
		EditServerName(editText);
	});
	screenManager->push(popupScreen);
}
