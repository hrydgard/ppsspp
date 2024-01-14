#pragma once

#include <cstdint>

#include "Common/File/Path.h"
#include "Common/UI/View.h"
#include "Common/UI/UIScreen.h"
#include "Common/UI/ViewGroup.h"
#include "Core/RetroAchievements.h"
#include "UI/MiscScreens.h"
#include "UI/TabbedDialogScreen.h"

#include "ext/rcheevos/include/rc_client.h"

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
	~RetroAchievementsSettingsScreen();
	const char *tag() const override { return "RetroAchievementsSettingsScreen"; }

	void CreateTabs() override;
	void sendMessage(UIMessage message, const char *value) override;

protected:
	bool ShowSearchControls() const override { return false; }

private:
	void CreateAccountTab(UI::ViewGroup *viewGroup);
	void CreateCustomizeTab(UI::ViewGroup *viewGroup);
	void CreateDeveloperToolsTab(UI::ViewGroup *viewGroup);

	std::string username_;
	std::string password_;
};

class RetroAchievementsLeaderboardScreen : public TabbedUIDialogScreenWithGameBackground {
public:
	RetroAchievementsLeaderboardScreen(const Path &gamePath, int leaderboardID);
	~RetroAchievementsLeaderboardScreen();

	const char *tag() const override { return "RetroAchievementsLeaderboardScreen"; }

	void CreateTabs() override;

	void update() override;

protected:
	bool ShowSearchControls() const override { return false; }

private:
	void FetchEntries();
	void Poll();

	int leaderboardID_;
	bool nearMe_ = false;

	// Keep the fetched list alive and destroy in destructor.
	rc_client_leaderboard_entry_list_t *entryList_ = nullptr;

	rc_client_leaderboard_entry_list_t *pendingEntryList_ = nullptr;

	rc_client_async_handle_t *pendingAsyncCall_ = nullptr;
};

class UIContext;

enum class AchievementRenderStyle {
	LISTED,
	UNLOCKED,
	PROGRESS_INDICATOR,
	CHALLENGE_INDICATOR,
};

void MeasureAchievement(const UIContext &dc, const rc_client_achievement_t *achievement, AchievementRenderStyle style, float *w, float *h);
void RenderAchievement(UIContext &dc, const rc_client_achievement_t *achievement, AchievementRenderStyle style, const Bounds &bounds, float alpha, float startTime, float time_s, bool hasFocus);

void MeasureGameAchievementSummary(const UIContext &dc, float *w, float *h);
void RenderGameAchievementSummary(UIContext &dc, const Bounds &bounds, float alpha);

void MeasureLeaderboardEntry(const UIContext &dc, const rc_client_leaderboard_entry_t *entry, float *w, float *h);
void RenderLeaderboardEntry(UIContext &dc, const rc_client_leaderboard_entry_t *entry, const Bounds &bounds, float alpha);

class AchievementView : public UI::ClickableItem {
public:
	AchievementView(const rc_client_achievement_t *achievement, UI::LayoutParams *layoutParams = nullptr) : UI::ClickableItem(layoutParams), achievement_(achievement) {
		layoutParams_->height = UI::WRAP_CONTENT;  // Override the standard Item fixed height.
	}

	void Click() override;
	void Draw(UIContext &dc) override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;
private:
	const rc_client_achievement_t *achievement_;
};

class GameAchievementSummaryView : public UI::Item {
public:
	GameAchievementSummaryView(UI::LayoutParams *layoutParams = nullptr) : UI::Item(layoutParams) {
		layoutParams_->height = UI::WRAP_CONTENT;  // Override the standard Item fixed height.
	}

	void Draw(UIContext &dc) override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;
};

class LeaderboardSummaryView : public UI::ClickableItem {
public:
	LeaderboardSummaryView(const rc_client_leaderboard_t *leaderboard, UI::LayoutParams *layoutParams = nullptr) : UI::ClickableItem(layoutParams), leaderboard_(leaderboard) {
		layoutParams_->height = UI::WRAP_CONTENT;  // Override the standard Item fixed height.
	}

	void Draw(UIContext &dc) override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;

private:
	const rc_client_leaderboard_t *leaderboard_;
};

class LeaderboardEntryView : public UI::Item {
public:
	LeaderboardEntryView(const rc_client_leaderboard_entry_t *entry, bool isCurrentUser, UI::LayoutParams *layoutParams = nullptr)
		: UI::Item(layoutParams), entry_(entry), isCurrentUser_(isCurrentUser) {
		layoutParams_->height = UI::WRAP_CONTENT;  // Override the standard Item fixed height.
	}

	void Draw(UIContext &dc) override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;

private:
	const rc_client_leaderboard_entry_t *entry_;
	bool isCurrentUser_;
};
