#include "UI/RetroAchievementScreens.h"
#include "UI/RetroAchievements.h"
#include "Common/System/OSD.h"
#include "Common/System/Request.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/Context.h"
#include "Common/Data/Text/I18n.h"
#include "Common/UI/IconCache.h"

#include "Core/Config.h"

void RetroAchievementsListScreen::CreateTabs() {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	UI::LinearLayout *achievements = AddTab("Achievements", ac->T("Achievements"));
	achievements->SetSpacing(5.0f);
	CreateAchievementsTab(achievements);

	if (Achievements::GetLeaderboardCount() > 0) {
		UI::LinearLayout *leaderboards = AddTab("Leaderboards", ac->T("Leaderboards"));
		leaderboards->SetSpacing(5.0f);
		CreateLeaderboardsTab(leaderboards);
	}

#ifdef _DEBUG
	CreateStatisticsTab(AddTab("AchievementsStatistics", ac->T("Statistics")));
#endif
}

void RetroAchievementsListScreen::CreateAchievementsTab(UI::ViewGroup *achievements) {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	using namespace UI;

	std::vector<Achievements::Achievement> unlockedAchievements;
	std::vector<Achievements::Achievement> lockedAchievements;

	achievements->Add(new ItemHeader(ac->T("Achievements")));

	achievements->Add(new GameAchievementSummaryView(Achievements::GetGameID()));

	Achievements::EnumerateAchievements([&](const Achievements::Achievement &achievement) {
		if (achievement.locked) {
			lockedAchievements.push_back(achievement);
		} else {
			unlockedAchievements.push_back(achievement);
		}
		return true;
		});

	achievements->Add(new ItemHeader(ac->T("Unlocked achievements")));
	for (auto &achievement : unlockedAchievements) {
		achievements->Add(new AchievementView(std::move(achievement)));
	}
	achievements->Add(new ItemHeader(ac->T("Locked achievements")));
	for (auto &achievement : lockedAchievements) {
		achievements->Add(new AchievementView(std::move(achievement)));
	}
}

void RetroAchievementsListScreen::CreateLeaderboardsTab(UI::ViewGroup *viewGroup) {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	using namespace UI;

	viewGroup->Add(new GameAchievementSummaryView(Achievements::GetGameID()));

	viewGroup->Add(new ItemHeader(ac->T("Leaderboards")));

	std::vector<Achievements::Leaderboard> leaderboards;

	Achievements::EnumerateLeaderboards([&](const Achievements::Leaderboard &leaderboard) {
		leaderboards.push_back(leaderboard);
		return true;
		});

	for (auto &leaderboard : leaderboards) {
		if (!leaderboard.hidden) {
			int leaderboardID = leaderboard.id;
			viewGroup->Add(new LeaderboardSummaryView(std::move(leaderboard)))->OnClick.Add([=](UI::EventParams &e) -> UI::EventReturn {
				screenManager()->push(new RetroAchievementsLeaderboardScreen(gamePath_, leaderboardID));
				return UI::EVENT_DONE;
				});
		}
	}
}

void RetroAchievementsListScreen::CreateStatisticsTab(UI::ViewGroup *viewGroup) {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	using namespace UI;

	Achievements::Statistics stats = Achievements::GetStatistics();
	viewGroup->Add(new ItemHeader(ac->T("Statistics")));
	viewGroup->Add(new InfoItem(ac->T("Bad memory accesses"), StringFromFormat("%d", stats.badMemoryAccessCount)));
}

void RetroAchievementsLeaderboardScreen::CreateTabs() {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
	const Achievements::Leaderboard *leaderboard = Achievements::GetLeaderboardByID(leaderboardID_);
	using namespace UI;

	UI::LinearLayout *layout = AddTab("AchievementsLeaderboard", leaderboard->title.c_str());
	layout->Add(new TextView(leaderboard->description));

	layout->Add(new ItemHeader(ac->T("Leaderboard")));

	Poll();

	// TODO: Make it pretty.
	for (auto &entry : entries_) {
		layout->Add(new TextView(StringFromFormat(" %d: %s: %s%s", entry.rank, entry.user.c_str(), entry.formatted_score.c_str(), entry.is_self ? " <<<<< " : "")));
	}
}

void RetroAchievementsLeaderboardScreen::Poll() {
	if (done_)
		return;

	std::optional<bool> result = Achievements::TryEnumerateLeaderboardEntries(leaderboardID_, [&](const Achievements::LeaderboardEntry &entry) -> bool {
		entries_.push_back(entry);
		return true;
	});

	if (result.has_value()) {
		done_ = true;
		RecreateViews();
	}
}

void RetroAchievementsLeaderboardScreen::update() {
	TabbedUIDialogScreenWithGameBackground::update();
	Poll();
}

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

	if (Achievements::IsLoggedIn()) {
		viewGroup->Add(new InfoItem(ac->T("Username"), Achievements::GetUsername()));
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
	viewGroup->Add(new CheckBox(&g_Config.bAchievementsRichPresence, ac->T("Rich Presence")));
	viewGroup->Add(new CheckBox(&g_Config.bAchievementsSoundEffects, ac->T("Sound Effects")));  // not yet implemented
	viewGroup->Add(new CheckBox(&g_Config.bAchievementsLogBadMemReads, ac->T("Log bad memory accesses")));

	// Not yet fully implemented
	// viewGroup->Add(new CheckBox(&g_Config.bAchievementsChallengeMode, ac->T("Challenge Mode (no savestates)")));

	// TODO: What are these for?
	// viewGroup->Add(new CheckBox(&g_Config.bAchievementsTestMode, ac->T("Test Mode")));
	// viewGroup->Add(new CheckBox(&g_Config.bAchievementsUnofficialTestMode, ac->T("Unofficial Test Mode")));
}

void MeasureAchievement(const UIContext &dc, const Achievements::Achievement &achievement, float *w, float *h) {
	*w = 0.0f;
	*h = 72.0f;
}

void MeasureGameAchievementSummary(const UIContext &dc, int gameID, float *w, float *h) {
	*w = 0.0f;
	*h = 72.0f;
}

void MeasureLeaderboardSummary(const UIContext &dc, const Achievements::Leaderboard &achievement, float *w, float *h) {
	*w = 0.0f;
	*h = 72.0f;
}

// Graphical
void RenderAchievement(UIContext &dc, const Achievements::Achievement &achievement, AchievementRenderStyle style, const Bounds &bounds, float alpha, float startTime, float time_s) {
	using namespace UI;
	UI::Drawable background = UI::Drawable(dc.theme->backgroundColor);
	if (achievement.locked) {
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
	dc.DrawTextRect(achievement.title.c_str(), bounds.Inset(iconSpace + 12.0f, 2.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT);

	dc.SetFontScale(0.66f, 0.66f);
	dc.DrawTextRect(achievement.description.c_str(), bounds.Inset(iconSpace + 12.0f, 39.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT);

	char temp[64];
	snprintf(temp, sizeof(temp), "%d", achievement.points);

	dc.SetFontScale(1.5f, 1.5f);
	dc.DrawTextRect(temp, bounds.Expand(-5.0f, -5.0f), fgColor, ALIGN_RIGHT | ALIGN_VCENTER);

	dc.SetFontScale(1.0f, 1.0f);
	dc.Flush();

	std::string name = Achievements::GetAchievementBadgePath(achievement);
	if (g_iconCache.BindIconTexture(&dc, name)) {
		dc.Draw()->DrawTexRect(Bounds(bounds.x + 4.0f, bounds.y + 4.0f, iconSpace, iconSpace), 0.0f, 0.0f, 1.0f, 1.0f, whiteAlpha(alpha));
	}

	dc.Flush();
	dc.RebindTexture();
}

void RenderGameAchievementSummary(UIContext &dc, int gameID, const Bounds &bounds, float alpha) {
	using namespace UI;
	UI::Drawable background = dc.theme->itemStyle.background;

	background.color = colorAlpha(background.color, alpha);
	uint32_t fgColor = colorAlpha(dc.theme->itemStyle.fgColor, alpha);

	float iconSpace = 64.0f;
	dc.Flush();

	dc.Begin();
	dc.FillRect(background, bounds);

	dc.SetFontStyle(dc.theme->uiFont);

	dc.SetFontScale(1.0f, 1.0f);
	dc.DrawTextRect(Achievements::GetGameTitle().c_str(), bounds.Inset(iconSpace + 5.0f, 2.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT);

	std::string description = Achievements::GetGameAchievementSummary();
	std::string icon = Achievements::GetGameIcon();

	dc.SetFontScale(0.66f, 0.66f);
	dc.DrawTextRect(description.c_str(), bounds.Inset(iconSpace + 5.0f, 38.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT);

	dc.SetFontScale(1.0f, 1.0f);
	dc.Flush();

	std::string name = icon;
	if (g_iconCache.BindIconTexture(&dc, name)) {
		dc.Draw()->DrawTexRect(Bounds(bounds.x, bounds.y, iconSpace, iconSpace), 0.0f, 0.0f, 1.0f, 1.0f, whiteAlpha(alpha));
	}

	dc.Flush();
	dc.RebindTexture();
}

void RenderLeaderboardSummary(UIContext &dc, const Achievements::Leaderboard &leaderboard, AchievementRenderStyle style, const Bounds &bounds, float alpha, float startTime, float time_s) {
	using namespace UI;
	UI::Drawable background = UI::Drawable(dc.theme->backgroundColor);
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
	dc.DrawTextRect(leaderboard.title.c_str(), bounds.Inset(iconSpace + 12.0f, 2.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT);

	dc.SetFontScale(0.66f, 0.66f);
	dc.DrawTextRect(leaderboard.description.c_str(), bounds.Inset(iconSpace + 12.0f, 39.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT);

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

void AchievementView::Draw(UIContext &dc) {
	RenderAchievement(dc, achievement_, AchievementRenderStyle::LISTED, bounds_, 1.0f, 0.0f, 0.0f);
}

void AchievementView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	MeasureAchievement(dc, achievement_, &w, &h);
}

void AchievementView::Click() {
	// In debug builds, clicking achievements will show them being unlocked (which may be a lie).
#ifdef _DEBUG
	g_OSD.ShowAchievementUnlocked(achievement_.id);
#endif
}

void LeaderboardSummaryView::Draw(UIContext &dc) {
	RenderLeaderboardSummary(dc, leaderboard_, AchievementRenderStyle::LISTED, bounds_, 1.0f, 0.0f, 0.0f);
}

void LeaderboardSummaryView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	MeasureLeaderboardSummary(dc, leaderboard_, &w, &h);
}

void GameAchievementSummaryView::Draw(UIContext &dc) {
	RenderGameAchievementSummary(dc, gameID_, bounds_, 1.0f);
}

void GameAchievementSummaryView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	MeasureGameAchievementSummary(dc, gameID_, &w, &h);
}
