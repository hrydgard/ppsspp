#include "UI/RetroAchievementScreens.h"
#include "UI/RetroAchievements.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/Data/Text/I18n.h"

void RetroAchievementsListScreen::CreateViews() {
	auto di = GetI18NCategory(I18NCat::DIALOG);

	using namespace UI;

	root_ = new ScrollView(UI::ORIENT_VERTICAL);

	LinearLayout *listLayout = root_->Add(new LinearLayout(UI::ORIENT_VERTICAL));

	std::vector<Achievements::Achievement> achievements;

	Achievements::EnumerateAchievements([&](const Achievements::Achievement &achievement) {
		achievements.push_back(achievement);
		return true;
	});

	for (auto achievement : achievements) {
		listLayout->Add(new TextView(achievement.title));
		listLayout->Add(new TextView(achievement.description));
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
