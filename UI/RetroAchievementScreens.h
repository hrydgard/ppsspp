#pragma once

#include "Common/UI/View.h"
#include "Common/UI/UIScreen.h"
#include "Common/UI/ViewGroup.h"
#include "UI/MiscScreens.h"
#include "UI/TabbedDialogScreen.h"
#include "Common/File/Path.h"
#include "UI/RetroAchievements.h"

// Lists the achievements and leaderboards for one game.
class RetroAchievementsListScreen : public TabbedUIDialogScreenWithGameBackground {
public:
	RetroAchievementsListScreen(const Path &gamePath) : TabbedUIDialogScreenWithGameBackground(gamePath) {}
	const char *tag() const override { return "RetroAchievementsListScreen"; }

	void CreateTabs() override;

protected:
	bool ShowSearchControls() const override { return false; }

private:
	void CreateAchievementsTab(UI::ViewGroup *viewGroup);
	void CreateLeaderboardsTab(UI::ViewGroup *viewGroup);
	void CreateStatisticsTab(UI::ViewGroup *viewGroup);
};

// Lets you manage your account, and shows some achievement stats and stuff.
class RetroAchievementsSettingsScreen : public TabbedUIDialogScreenWithGameBackground {
public:
	RetroAchievementsSettingsScreen(const Path &gamePath) : TabbedUIDialogScreenWithGameBackground(gamePath) {}
	const char *tag() const override { return "RetroAchievementsSettingsScreen"; }

	void CreateTabs() override;
	void sendMessage(const char *message, const char *value) override;

protected:
	bool ShowSearchControls() const override { return false; }

private:
	void CreateAccountTab(UI::ViewGroup *viewGroup);
	void CreateSettingsTab(UI::ViewGroup *viewGroup);

	std::string username_;
	std::string password_;
};

class RetroAchievementsLeaderboardScreen : public TabbedUIDialogScreenWithGameBackground {
public:
	RetroAchievementsLeaderboardScreen(const Path &gamePath, int leaderboardID) : TabbedUIDialogScreenWithGameBackground(gamePath), leaderboardID_(leaderboardID) {}
	const char *tag() const override { return "RetroAchievementsLeaderboardScreen"; }

	void CreateTabs() override;

	void update() override;
protected:
	bool ShowSearchControls() const override { return false; }
private:
	void Poll();

	int leaderboardID_;
	bool done_ = false;
	std::vector<Achievements::LeaderboardEntry> entries_;
};

class UIContext;

enum class AchievementRenderStyle {
	LISTED,
	UNLOCKED,
};

void MeasureAchievement(const UIContext &dc, const Achievements::Achievement &achievement, float *w, float *h);
void RenderAchievement(UIContext &dc, const Achievements::Achievement &achievement, AchievementRenderStyle style, const Bounds &bounds, float alpha, float startTime, float time_s);
void MeasureGameAchievementSummary(const UIContext &dc, int gameID, float *w, float *h);
void RenderGameAchievementSummary(UIContext &dc, int gameID, const Bounds &bounds, float alpha);

class AchievementView : public UI::ClickableItem {
public:
	AchievementView(const Achievements::Achievement &&achievement, UI::LayoutParams *layoutParams = nullptr) : UI::ClickableItem(layoutParams), achievement_(achievement) {}

	void Click() override;
	void Draw(UIContext &dc) override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;
private:
	Achievements::Achievement achievement_;
};

class LeaderboardSummaryView : public UI::ClickableItem {
public:
	LeaderboardSummaryView(const Achievements::Leaderboard &&leaderboard, UI::LayoutParams *layoutParams = nullptr) : UI::ClickableItem(layoutParams), leaderboard_(leaderboard) {}

	void Draw(UIContext &dc) override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;

private:
	Achievements::Leaderboard leaderboard_;
};

class GameAchievementSummaryView : public UI::Item {
public:
	GameAchievementSummaryView(int gameID, UI::LayoutParams *layoutParams = nullptr) : UI::Item(layoutParams), gameID_(gameID) {}

	void Draw(UIContext &dc) override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;
private:
	int gameID_;
};
