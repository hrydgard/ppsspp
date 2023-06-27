#include "UI/RetroAchievementScreens.h"
#include "Common/System/OSD.h"
#include "Common/System/Request.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/Context.h"
#include "Common/Data/Text/I18n.h"
#include "Common/UI/IconCache.h"

#include "Core/Config.h"
#include "Core/RetroAchievements.h"

void RetroAchievementsListScreen::CreateTabs() {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	UI::LinearLayout *achievements = AddTab("Achievements", ac->T("Achievements"));
	achievements->SetSpacing(5.0f);
	CreateAchievementsTab(achievements);

	UI::LinearLayout *leaderboards = AddTab("Leaderboards", ac->T("Leaderboards"));
	leaderboards->SetSpacing(5.0f);
	CreateLeaderboardsTab(leaderboards);

#ifdef _DEBUG
	CreateStatisticsTab(AddTab("AchievementsStatistics", ac->T("Statistics")));
#endif
}

void RetroAchievementsListScreen::CreateAchievementsTab(UI::ViewGroup *achievements) {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	using namespace UI;

	rc_client_achievement_list_t *list = rc_client_create_achievement_list(Achievements::GetClient(),
		RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE_AND_UNOFFICIAL, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);

	std::vector<const rc_client_achievement_t *> unlockedAchievements;
	std::vector<const rc_client_achievement_t *> lockedAchievements;
	std::vector<const rc_client_achievement_t *> otherAchievements;

	for (uint32_t i = 0; i < list->num_buckets; i++) {
		const rc_client_achievement_bucket_t &bucket = list->buckets[i];
		for (uint32_t j = 0; j < bucket.num_achievements; j++) {
			switch (bucket.bucket_type) {
			case RC_CLIENT_ACHIEVEMENT_BUCKET_LOCKED:
				lockedAchievements.push_back(bucket.achievements[j]);
				break;
			case RC_CLIENT_ACHIEVEMENT_BUCKET_UNLOCKED:
				unlockedAchievements.push_back(bucket.achievements[j]);
				break;
			default:
				otherAchievements.push_back(bucket.achievements[j]);
				break;
			}
		}
	}

	achievements->Add(new ItemHeader(ac->T("Achievements")));

	achievements->Add(new GameAchievementSummaryView());

	achievements->Add(new ItemHeader(ac->T("Unlocked achievements")));
	for (auto &achievement : unlockedAchievements) {
		achievements->Add(new AchievementView(achievement));
	}
	achievements->Add(new ItemHeader(ac->T("Locked achievements")));
	for (auto &achievement : lockedAchievements) {
		achievements->Add(new AchievementView(achievement));
	}
	achievements->Add(new ItemHeader(ac->T("Other achievements")));
	for (auto &achievement : otherAchievements) {
		achievements->Add(new AchievementView(achievement));
	}
}

void RetroAchievementsListScreen::CreateLeaderboardsTab(UI::ViewGroup *viewGroup) {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	using namespace UI;

	viewGroup->Add(new GameAchievementSummaryView());

	viewGroup->Add(new ItemHeader(ac->T("Leaderboards")));

	std::vector<rc_client_leaderboard_t *> leaderboards;
	rc_client_leaderboard_list_t *list = rc_client_create_leaderboard_list(Achievements::GetClient(), RC_CLIENT_LEADERBOARD_LIST_GROUPING_NONE);
	for (uint32_t i = 0; i < list->num_buckets; i++) {
		const rc_client_leaderboard_bucket_t &bucket = list->buckets[i];
		for (uint32_t j = 0; j < bucket.num_leaderboards; j++) {
			leaderboards.push_back(bucket.leaderboards[j]);
		}
	}

	for (auto &leaderboard : leaderboards) {
		int leaderboardID = leaderboard->id;
		viewGroup->Add(new LeaderboardSummaryView(leaderboard))->OnClick.Add([=](UI::EventParams &e) -> UI::EventReturn {
			screenManager()->push(new RetroAchievementsLeaderboardScreen(gamePath_, leaderboardID));
			return UI::EVENT_DONE;
		});
	}
}

void RetroAchievementsListScreen::CreateStatisticsTab(UI::ViewGroup *viewGroup) {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	using namespace UI;

	Achievements::Statistics stats = Achievements::GetStatistics();
	viewGroup->Add(new ItemHeader(ac->T("Statistics")));
	viewGroup->Add(new InfoItem(ac->T("Bad memory accesses"), StringFromFormat("%d", stats.badMemoryAccessCount)));
}

RetroAchievementsLeaderboardScreen::~RetroAchievementsLeaderboardScreen() {
	if (pendingAsyncCall_) {
		rc_client_abort_async(Achievements::GetClient(), pendingAsyncCall_);
	}
	Poll();  // Gets rid of pendingEntryList_.
	if (entryList_) {
		rc_client_destroy_leaderboard_entry_list(entryList_);
	}
}

RetroAchievementsLeaderboardScreen::RetroAchievementsLeaderboardScreen(const Path &gamePath, int leaderboardID)
	: TabbedUIDialogScreenWithGameBackground(gamePath), leaderboardID_(leaderboardID) {
	rc_client_begin_fetch_leaderboard_entries(Achievements::GetClient(), leaderboardID_, 0, 20, [](int result, const char *error_message, rc_client_leaderboard_entry_list_t *list, rc_client_t *client, void *userdata) {
		if (result != RC_OK) {
			g_OSD.Show(OSDType::MESSAGE_ERROR, error_message, 10.0f);
			return;
		}

		RetroAchievementsLeaderboardScreen *thiz = (RetroAchievementsLeaderboardScreen *)userdata;
		thiz->pendingEntryList_ = list;
		thiz->pendingAsyncCall_ = nullptr;
	}, this);
}

void RetroAchievementsLeaderboardScreen::CreateTabs() {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
	const rc_client_leaderboard_t *leaderboard = rc_client_get_leaderboard_info(Achievements::GetClient(), leaderboardID_);

	using namespace UI;
	UI::LinearLayout *layout = AddTab("AchievementsLeaderboard", leaderboard->title);
	layout->Add(new TextView(leaderboard->description));
	layout->Add(new ItemHeader(ac->T("Leaderboard")));

	if (entryList_) {
		for (uint32_t i = 0; i < entryList_->num_entries; i++) {
			bool is_self = (i == entryList_->user_index);
			// Should highlight somehow.
			const rc_client_leaderboard_entry_t &entry = entryList_->entries[i];

			char buffer[512];
			rc_client_leaderboard_entry_get_user_image_url(&entryList_->entries[i], buffer, sizeof(buffer));
			// Can also show entry.submitted, which is a time_t. And maybe highlight recent ones?
			layout->Add(new LeaderboardEntryView(&entryList_->entries[i], is_self));
		}
	}
}

void RetroAchievementsLeaderboardScreen::Poll() {
	if (pendingEntryList_) {
		if (entryList_) {
			rc_client_destroy_leaderboard_entry_list(entryList_);
		}
		entryList_ = pendingEntryList_;
		pendingEntryList_ = nullptr;
		RecreateViews();
	}
}

void RetroAchievementsLeaderboardScreen::update() {
	TabbedUIDialogScreenWithGameBackground::update();
	Poll();
}

RetroAchievementsSettingsScreen::~RetroAchievementsSettingsScreen() {}

void RetroAchievementsSettingsScreen::CreateTabs() {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
	auto di = GetI18NCategory(I18NCat::DIALOG);

	using namespace UI;

	CreateAccountTab(AddTab("AchievementsAccount", ac->T("Account")));
	CreateSettingsTab(AddTab("AchievementsSettings", di->T("Settings")));
}

void RetroAchievementsSettingsScreen::sendMessage(const char *message, const char *value) {
	TabbedUIDialogScreenWithGameBackground::sendMessage(message, value);

	if (!strcmp(message, "achievements_loginstatechange")) {
		RecreateViews();
	}
}

void RetroAchievementsSettingsScreen::CreateAccountTab(UI::ViewGroup *viewGroup) {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
	auto di = GetI18NCategory(I18NCat::DIALOG);

	using namespace UI;

	if (!g_Config.bAchievementsEnable) {
		viewGroup->Add(new TextView(ac->T("Achievements are disabled")));
	} else if (Achievements::IsLoggedIn()) {
		const rc_client_user_t *info = rc_client_get_user_info(Achievements::GetClient());

		// In the future, RetroAchievements will support display names. Prepare for that.
		if (strcmp(info->display_name, info->username) != 0) {
			viewGroup->Add(new InfoItem(ac->T("Name"), info->display_name));
		}
		viewGroup->Add(new InfoItem(ac->T("Username"), info->username));
		// viewGroup->Add(new InfoItem(ac->T("Unread messages"), info.numUnreadMessages));
		viewGroup->Add(new Choice(di->T("Log out")))->OnClick.Add([=](UI::EventParams &) -> UI::EventReturn {
			Achievements::Logout();
			return UI::EVENT_DONE;
		});
	} else {
		// TODO: Add UI for platforms that don't support System_AskUsernamePassword.
		if (System_GetPropertyBool(SYSPROP_HAS_LOGIN_DIALOG)) {
			viewGroup->Add(new Choice(ac->T("Log in")))->OnClick.Add([=](UI::EventParams &) -> UI::EventReturn {
				System_AskUsernamePassword(ac->T("Log in"), [](const std::string &value, int) {
					std::vector<std::string> parts;
					SplitString(value, '\n', parts);
					if (parts.size() == 2 && !parts[0].empty() && !parts[1].empty()) {
						Achievements::LoginAsync(parts[0].c_str(), parts[1].c_str());
					}
				});
				return UI::EVENT_DONE;
			});
		} else {
			// Hack up a temporary quick login-form-ish-thing
			viewGroup->Add(new PopupTextInputChoice(&username_, di->T("Username"), "", 128, screenManager()));
			viewGroup->Add(new PopupTextInputChoice(&password_, di->T("Password"), "", 128, screenManager()));
			viewGroup->Add(new Choice(di->T("Log in")))->OnClick.Add([=](UI::EventParams &) -> UI::EventReturn {
				if (!username_.empty() && !password_.empty()) {
					Achievements::LoginAsync(username_.c_str(), password_.c_str());
				}
				return UI::EVENT_DONE;
			});
		}
		viewGroup->Add(new Choice(ac->T("Register on www.retroachievements.org")))->OnClick.Add([&](UI::EventParams &) -> UI::EventReturn {
			System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://retroachievements.org/createaccount.php");
			return UI::EVENT_DONE;
		});
	}

	viewGroup->Add(new ItemHeader(di->T("Links")));
	viewGroup->Add(new Choice(ac->T("RetroAchievements website")))->OnClick.Add([&](UI::EventParams &) -> UI::EventReturn {
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.retroachievements.org/");
		return UI::EVENT_DONE;
	});
	viewGroup->Add(new Choice(ac->T("How to use RetroAchievements")))->OnClick.Add([&](UI::EventParams &) -> UI::EventReturn {
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org/docs/reference/retro-achievements");
		return UI::EVENT_DONE;
	});
}

void RetroAchievementsSettingsScreen::CreateSettingsTab(UI::ViewGroup *viewGroup) {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	using namespace UI;
	viewGroup->Add(new ItemHeader(ac->T("Settings")));
	viewGroup->Add(new CheckBox(&g_Config.bAchievementsEnable, ac->T("Achievements enabled")))->OnClick.Add([&](UI::EventParams &e) -> UI::EventReturn {
		Achievements::UpdateSettings();
		RecreateViews();
		return UI::EVENT_DONE;
	});
	viewGroup->Add(new CheckBox(&g_Config.bAchievementsChallengeMode, ac->T("Challenge Mode (no savestates)")));
	viewGroup->Add(new CheckBox(&g_Config.bAchievementsRichPresence, ac->T("Rich Presence")))->SetEnabledPtr(&g_Config.bAchievementsEnable);
	viewGroup->Add(new CheckBox(&g_Config.bAchievementsSoundEffects, ac->T("Sound Effects")))->SetEnabledPtr(&g_Config.bAchievementsEnable);  // not yet implemented
	viewGroup->Add(new CheckBox(&g_Config.bAchievementsLogBadMemReads, ac->T("Log bad memory accesses")))->SetEnabledPtr(&g_Config.bAchievementsEnable);

	// TODO: What are these for?
	// viewGroup->Add(new CheckBox(&g_Config.bAchievementsTestMode, ac->T("Test Mode")));
	// viewGroup->Add(new CheckBox(&g_Config.bAchievementsUnofficialTestMode, ac->T("Unofficial Test Mode")));
}

void MeasureAchievement(const UIContext &dc, const rc_client_achievement_t *achievement, AchievementRenderStyle style, float *w, float *h) {
	*w = 0.0f;
	switch (style) {
	case AchievementRenderStyle::PROGRESS_INDICATOR:
		*h = 36.0f;
		break;
	default:
		*h = 72.0f;
		break;
	}
}

void MeasureGameAchievementSummary(const UIContext &dc, float *w, float *h) {
	*w = 0.0f;
	*h = 72.0f;
	if (Achievements::ChallengeModeActive()) {
		*h += 20.0f;
	}
}

void MeasureLeaderboardSummary(const UIContext &dc, const rc_client_leaderboard_t *leaderboard, float *w, float *h) {
	*w = 0.0f;
	*h = 72.0f;
}

void MeasureLeaderboardEntry(const UIContext &dc, const rc_client_leaderboard_entry_t *entry, float *w, float *h) {
	*w = 0.0f;
	*h = 72.0f;
}

// Graphical
void RenderAchievement(UIContext &dc, const rc_client_achievement_t *achievement, AchievementRenderStyle style, const Bounds &bounds, float alpha, float startTime, float time_s) {
	using namespace UI;
	UI::Drawable background = UI::Drawable(dc.theme->backgroundColor);
	if (!achievement->unlocked) {
		// Make the background color gray.
		// TODO: Different colors in challenge mode, or even in the "re-take achievements" mode when we add that?
		background.color = 0x706060;
	}
	background.color = colorAlpha(background.color, alpha);
	uint32_t fgColor = colorAlpha(dc.theme->itemStyle.fgColor, alpha);

	if (style == AchievementRenderStyle::UNLOCKED) {
		float mixWhite = pow(Clamp((float)(1.0f - (time_s - startTime)), 0.0f, 1.0f), 3.0f);
		background.color = colorBlend(0xFFE0FFFF, background.color, mixWhite);
	}

	float iconSpace = 64.0f;
	dc.Flush();

	dc.Begin();
	dc.FillRect(background, bounds);

	dc.SetFontStyle(dc.theme->uiFont);

	dc.SetFontScale(1.0f, 1.0f);
	dc.DrawTextRect(achievement->title, bounds.Inset(iconSpace + 12.0f, 2.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT);

	dc.SetFontScale(0.66f, 0.66f);
	dc.DrawTextRect(achievement->description, bounds.Inset(iconSpace + 12.0f, 39.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT);

	// TODO: Draw measured_progress / measured_percent in a cute way

	char temp[512];
	snprintf(temp, sizeof(temp), "%d", achievement->points);

	dc.SetFontScale(1.5f, 1.5f);
	dc.DrawTextRect(temp, bounds.Expand(-5.0f, -5.0f), fgColor, ALIGN_RIGHT | ALIGN_VCENTER);

	dc.SetFontScale(1.0f, 1.0f);
	dc.Flush();

	// Download and display the image.
	if (RC_OK == rc_client_achievement_get_image_url(achievement, achievement->state, temp, sizeof(temp))) {
		Achievements::DownloadImageIfMissing(achievement->badge_name, std::move(std::string(temp)));
		if (g_iconCache.BindIconTexture(&dc, achievement->badge_name)) {
			dc.Draw()->DrawTexRect(Bounds(bounds.x + 4.0f, bounds.y + 4.0f, iconSpace, iconSpace), 0.0f, 0.0f, 1.0f, 1.0f, whiteAlpha(alpha));
		}
	}

	dc.Flush();
	dc.RebindTexture();
}

void RenderGameAchievementSummary(UIContext &dc, const Bounds &bounds, float alpha) {
	using namespace UI;
	UI::Drawable background = dc.theme->itemStyle.background;

	background.color = alphaMul(background.color, alpha);
	uint32_t fgColor = colorAlpha(dc.theme->itemStyle.fgColor, alpha);

	float iconSpace = 64.0f;
	dc.Flush();

	dc.Begin();
	dc.FillRect(background, bounds);

	dc.SetFontStyle(dc.theme->uiFont);

	const rc_client_game_t *gameInfo = rc_client_get_game_info(Achievements::GetClient());

	dc.SetFontScale(1.0f, 1.0f);
	dc.DrawTextRect(gameInfo->title, bounds.Inset(iconSpace + 5.0f, 2.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT);

	std::string description = Achievements::GetGameAchievementSummary();

	dc.SetFontScale(0.66f, 0.66f);
	dc.DrawTextRect(description.c_str(), bounds.Inset(iconSpace + 5.0f, 38.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT);

	dc.SetFontScale(1.0f, 1.0f);
	dc.Flush();

	char temp[512];
	if (RC_OK == rc_client_game_get_image_url(gameInfo, temp, sizeof(temp))) {
		Achievements::DownloadImageIfMissing(gameInfo->badge_name, std::move(std::string(temp)));
		if (g_iconCache.BindIconTexture(&dc, gameInfo->badge_name)) {
			dc.Draw()->DrawTexRect(Bounds(bounds.x, bounds.y, iconSpace, iconSpace), 0.0f, 0.0f, 1.0f, 1.0f, whiteAlpha(alpha));
		}
	}

	dc.Flush();
	dc.RebindTexture();
}

void RenderLeaderboardSummary(UIContext &dc, const rc_client_leaderboard_t *leaderboard, AchievementRenderStyle style, const Bounds &bounds, float alpha, float startTime, float time_s) {
	using namespace UI;
	UI::Drawable background = UI::Drawable(dc.theme->backgroundColor);
	background.color = colorAlpha(background.color, alpha);
	uint32_t fgColor = colorAlpha(dc.theme->itemStyle.fgColor, alpha);

	if (style == AchievementRenderStyle::UNLOCKED) {
		float mixWhite = pow(Clamp((float)(1.0f - (time_s - startTime)), 0.0f, 1.0f), 3.0f);
		background.color = colorBlend(0xFFE0FFFF, background.color, mixWhite);
	}

	dc.Flush();

	dc.Begin();
	dc.FillRect(background, bounds);

	dc.SetFontStyle(dc.theme->uiFont);

	dc.SetFontScale(1.0f, 1.0f);
	dc.DrawTextRect(leaderboard->title, bounds.Inset(12.0f, 2.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT);

	dc.SetFontScale(0.66f, 0.66f);
	dc.DrawTextRect(leaderboard->description, bounds.Inset(12.0f, 39.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT);

	/*
	char temp[64];
	snprintf(temp, sizeof(temp), "%d", leaderboard.points);

	dc.SetFontScale(1.5f, 1.5f);
	dc.DrawTextRect(temp, bounds.Expand(-5.0f, -5.0f), fgColor, ALIGN_RIGHT | ALIGN_VCENTER);

	dc.SetFontScale(1.0f, 1.0f);
	dc.Flush();
	*/

	dc.Flush();
	dc.RebindTexture();
}

void RenderLeaderboardEntry(UIContext &dc, const rc_client_leaderboard_entry_t *entry, const Bounds &bounds, float alpha) {
	using namespace UI;
	UI::Drawable background = dc.theme->itemStyle.background;

	background.color = alphaMul(background.color, alpha);
	uint32_t fgColor = alphaMul(dc.theme->itemStyle.fgColor, alpha);

	float iconSize = 64.0f;
	float numberSpace = 128.0f;
	float iconLeft = numberSpace + 5.0f;
	float iconSpace = numberSpace + 5.0f + iconSize;

	dc.Flush();

	dc.Begin();
	dc.FillRect(background, bounds);

	dc.SetFontStyle(dc.theme->uiFont);

	dc.SetFontScale(1.5f, 1.5f);
	dc.DrawTextRect(StringFromFormat("%d", entry->rank).c_str(), Bounds(bounds.x + 4.0f, bounds.y + 4.0f, numberSpace - 10.0f, bounds.h - 4.0f * 2.0f), fgColor, ALIGN_TOPRIGHT);

	dc.SetFontScale(1.0f, 1.0f);
	dc.DrawTextRect(entry->user, bounds.Inset(iconSpace + 5.0f, 2.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT);

	dc.SetFontScale(0.66f, 0.66f);
	dc.DrawTextRect(entry->display, bounds.Inset(iconSpace + 5.0f, 38.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT);

	dc.SetFontScale(1.0f, 1.0f);
	dc.Flush();

	// Come up with a unique name for the icon entry.
	char entryName[256];
	snprintf(entryName, sizeof(entryName), "lbe:%s", entry->user);
	char temp[512];
	if (RC_OK == rc_client_leaderboard_entry_get_user_image_url(entry, temp, sizeof(temp))) {
		Achievements::DownloadImageIfMissing(entryName, std::move(std::string(temp)));
		if (g_iconCache.BindIconTexture(&dc, entryName)) {
			dc.Draw()->DrawTexRect(Bounds(bounds.x + iconLeft, bounds.y + 4.0f, 64.0f, 64.0f), 0.0f, 0.0f, 1.0f, 1.0f, whiteAlpha(alpha));
		}
	}

	dc.Flush();
	dc.RebindTexture();
}

void AchievementView::Draw(UIContext &dc) {
	RenderAchievement(dc, achievement_, AchievementRenderStyle::LISTED, bounds_, 1.0f, 0.0f, 0.0f);
}

void AchievementView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	MeasureAchievement(dc, achievement_, AchievementRenderStyle::LISTED, &w, &h);
}

void AchievementView::Click() {
	// In debug builds, clicking achievements will show them being unlocked (which may be a lie).
#ifdef _DEBUG
	g_OSD.ShowAchievementUnlocked(achievement_->id);
#endif
}

void GameAchievementSummaryView::Draw(UIContext &dc) {
	RenderGameAchievementSummary(dc, bounds_, 1.0f);
}

void GameAchievementSummaryView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	MeasureGameAchievementSummary(dc, &w, &h);
}

void LeaderboardSummaryView::Draw(UIContext &dc) {
	RenderLeaderboardSummary(dc, leaderboard_, AchievementRenderStyle::LISTED, bounds_, 1.0f, 0.0f, 0.0f);
}

void LeaderboardSummaryView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	MeasureLeaderboardSummary(dc, leaderboard_, &w, &h);
}

void LeaderboardEntryView::Draw(UIContext &dc) {
	RenderLeaderboardEntry(dc, entry_, bounds_, 1.0f);
}

void LeaderboardEntryView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	MeasureLeaderboardEntry(dc, entry_, &w, &h);
}

