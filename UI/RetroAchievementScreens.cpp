#include "UI/RetroAchievementScreens.h"
#include "UI/RetroAchievements.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/Context.h"
#include "Common/Data/Text/I18n.h"

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
		viewGroup->Add(new Choice(ac->T("Log out")))->OnClick.Add([&](UI::EventParams &) -> UI::EventReturn {
			Achievements::Logout();
			return UI::EVENT_DONE;
		});
	} else {
		viewGroup->Add(new Choice(ac->T("Log in to RetroAchievements")))->OnClick.Add([&](UI::EventParams &) -> UI::EventReturn {
			// TODO: Actually allow entering user/pass.			
			Achievements::LoginAsync("fakeuser", "fakepassword");
			return UI::EVENT_DONE;
		});
	}
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

	dc.Begin();
	dc.FillRect(background, bounds);

	dc.SetFontScale(0.7f, 0.7f);
	dc.DrawTextRect(achievement.title.c_str(), bounds.Expand(-5.0f, -5.0f), dc.theme->itemStyle.fgColor, ALIGN_TOPLEFT);

	dc.SetFontScale(0.5f, 0.5f);
	dc.DrawTextRect(achievement.description.c_str(), bounds.Expand(-5.0f, -5.0f).Offset(0.0f, 30.0f), dc.theme->itemStyle.fgColor, ALIGN_TOPLEFT);

	char temp[64];
	snprintf(temp, sizeof(temp), "%d", achievement.points);

	dc.SetFontScale(1.5f, 1.5f);
	dc.DrawTextRect(temp, bounds.Expand(-5.0f, -5.0f), dc.theme->itemStyle.fgColor, ALIGN_RIGHT | ALIGN_VCENTER);

	dc.SetFontScale(1.0f, 1.0f);

	dc.Flush();
}

void AchievementView::Draw(UIContext &dc) {
	RenderAchievement(dc, achievement_, AchievementRenderStyle::LISTED, bounds_, 0.0f);
}

void AchievementView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	MeasureAchievement(achievement_, &w, &h);
}
