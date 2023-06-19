#include "UI/RetroAchievementScreens.h"
#include "UI/RetroAchievements.h"
#include "Common/System/Request.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/Context.h"
#include "Common/Data/Text/I18n.h"
#include "Common/UI/IconCache.h"

#include "Core/Config.h"

void RetroAchievementsListScreen::CreateViews() {
	auto di = GetI18NCategory(I18NCat::DIALOG);

	using namespace UI;

	root_ = new ScrollView(UI::ORIENT_VERTICAL);

	LinearLayout *listLayout = root_->Add(new LinearLayout(UI::ORIENT_VERTICAL));
	listLayout->SetSpacing(0.0f);

	std::vector<Achievements::Achievement> achievements;

	Achievements::EnumerateAchievements([&](const Achievements::Achievement &achievement) {
		achievements.push_back(achievement);
		return true;
	});

	for (auto achievement : achievements) {
		listLayout->Add(new AchievementView(achievement));
	}
}

void RetroAchievementsSettingsScreen::CreateTabs() {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	using namespace UI;

	LinearLayout *account = AddTab("AchievementsAccount", ac->T("Account"));
	CreateAccountTab(account);

	LinearLayout *settings = AddTab("AchievementsSettings", ac->T("Settings"));
	CreateSettingsTab(settings);
}

void RetroAchievementsSettingsScreen::sendMessage(const char *message, const char *value) {
	TabbedUIDialogScreenWithGameBackground::sendMessage(message, value);

	if (!strcmp(message, "achievements_loginstatechange")) {
		RecreateViews();
	}
}

void RetroAchievementsSettingsScreen::CreateAccountTab(UI::ViewGroup *viewGroup) {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	using namespace UI;

	if (Achievements::IsLoggedIn()) {
		viewGroup->Add(new InfoItem(ac->T("User Name"), Achievements::GetUsername()));
		viewGroup->Add(new Choice(ac->T("Log out")))->OnClick.Add([=](UI::EventParams &) -> UI::EventReturn {
			Achievements::Logout();
			return UI::EVENT_DONE;
		});
	} else {
		// TODO: Add UI for platforms that don't support System_AskUsernamePassword.
		viewGroup->Add(new Choice(ac->T("Log in to RetroAchievements")))->OnClick.Add([=](UI::EventParams &) -> UI::EventReturn {
			System_AskUsernamePassword(ac->T("Log in"), [](const std::string &value, int) {
				std::vector<std::string> parts;
				SplitString(value, '\n', parts);
				if (parts.size() == 2 && !parts[0].empty() && !parts[1].empty()) {
					Achievements::LoginAsync(parts[0].c_str(), parts[1].c_str());
				}
			});
			return UI::EVENT_DONE;
		});
		viewGroup->Add(new Choice(ac->T("Register on www.retroachievements.org")))->OnClick.Add([&](UI::EventParams &) -> UI::EventReturn {
			System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://retroachievements.org/createaccount.php");
			return UI::EVENT_DONE;
		});
	}
	viewGroup->Add(new Choice(ac->T("About RetroAchievements")))->OnClick.Add([&](UI::EventParams &) -> UI::EventReturn {
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.retroachievements.org/");
		return UI::EVENT_DONE;
	});
}

void RetroAchievementsSettingsScreen::CreateSettingsTab(UI::ViewGroup *viewGroup) {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	using namespace UI;
	viewGroup->Add(new ItemHeader(ac->T("Settings")));
	viewGroup->Add(new CheckBox(&g_Config.bAchievementsRichPresence, ac->T("Rich Presence")));
	viewGroup->Add(new CheckBox(&g_Config.bAchievementsSoundEffects, ac->T("Sound Effects")));

	// Not yet fully implemented
	// viewGroup->Add(new CheckBox(&g_Config.bAchievementsChallengeMode, ac->T("Challenge Mode")));

	// TODO: What are these for?
	// viewGroup->Add(new CheckBox(&g_Config.bAchievementsTestMode, ac->T("Test Mode")));
	// viewGroup->Add(new CheckBox(&g_Config.bAchievementsUnofficialTestMode, ac->T("Unofficial Test Mode")));
}

void MeasureAchievement(const Achievements::Achievement &achievement, float *w, float *h) {
	*w = 0.0f;
	*h = 60.0f;
}


// Render style references:

// https://www.trueachievements.com/achievement-meme-maker


// Graphical
void RenderAchievement(UIContext &dc, const Achievements::Achievement &achievement, AchievementRenderStyle style, const Bounds &bounds, float opacity) {
	using namespace UI;
	UI::Drawable background = achievement.locked ? dc.theme->popupStyle.background : dc.theme->itemStyle.background;

	background.color = colorAlpha(background.color, opacity);

	float iconSpace = 64.0f;

	dc.Begin();
	dc.FillRect(background, bounds);

	dc.SetFontScale(0.7f, 0.7f);
	dc.DrawTextRect(achievement.title.c_str(), bounds.Inset(iconSpace + 5.0f, 5.0f, 5.0f, 5.0f), dc.theme->itemStyle.fgColor, ALIGN_TOPLEFT);

	dc.SetFontScale(0.5f, 0.5f);
	dc.DrawTextRect(achievement.description.c_str(), bounds.Inset(iconSpace + 5.0f, 30.0f, 5.0f, 5.0f), dc.theme->itemStyle.fgColor, ALIGN_TOPLEFT);

	char temp[64];
	snprintf(temp, sizeof(temp), "%d", achievement.points);

	dc.SetFontScale(1.5f, 1.5f);
	dc.DrawTextRect(temp, bounds.Expand(-5.0f, -5.0f), dc.theme->itemStyle.fgColor, ALIGN_RIGHT | ALIGN_VCENTER);

	dc.SetFontScale(1.0f, 1.0f);
	dc.Flush();

	std::string name = Achievements::GetAchievementBadgePath(achievement);
	if (g_iconCache.BindIconTexture(&dc, name)) {
		dc.Draw()->DrawTexRect(Bounds(bounds.x, bounds.y, iconSpace, iconSpace), 0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF);
	}

	dc.Flush();
	dc.RebindTexture();

	dc.Flush();
}

void AchievementView::Draw(UIContext &dc) {
	RenderAchievement(dc, achievement_, AchievementRenderStyle::LISTED, bounds_, 0.0f);
}

void AchievementView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	MeasureAchievement(achievement_, &w, &h);
}
