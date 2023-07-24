#pragma once

#include <string>
#include <vector>
#include <mutex>

// Shows a visible message to the user.
// The default implementation in NativeApp.cpp uses our "osm" system (on screen messaging).
enum class OSDType {
	MESSAGE_INFO,
	MESSAGE_SUCCESS,
	MESSAGE_WARNING,
	MESSAGE_ERROR,
	MESSAGE_ERROR_DUMP,  // displays lots of text (after the first line), small size
	MESSAGE_FILE_LINK,

	ACHIEVEMENT_UNLOCKED,

	// Side entries
	ACHIEVEMENT_PROGRESS,  // Achievement icon + "measured_progress" text, auto-hide after 2s
	ACHIEVEMENT_CHALLENGE_INDICATOR,  // Achievement icon ONLY, no auto-hide

	LEADERBOARD_TRACKER,
};

// Data holder for on-screen messages.
class OnScreenDisplay {
public:
	// If you specify 0.0f as duration, a duration will be chosen automatically depending on type.
	void Show(OSDType type, const std::string &text, float duration_s = 0.0f, const char *id = nullptr) {
		Show(type, text, "", duration_s, id);
	}
	void Show(OSDType type, const std::string &text, const std::string &text2, float duration_s = 0.0f, const char *id = nullptr) {
		Show(type, text, text2, "", duration_s, id);
	}
	void Show(OSDType type, const std::string &text, const std::string &text2, const std::string &icon, float duration_s = 0.0f, const char *id = nullptr);

	void ShowOnOff(const std::string &message, bool on, float duration_s = 0.0f);

	bool IsEmpty() const { return entries_.empty(); }  // Shortcut to skip rendering.

	// Call this every frame, cleans up old entries.
	void Update();

	// Specialized achievement-related types. These go to the side notifications, not the top-middle.
	void ShowAchievementUnlocked(int achievementID);
	void ShowAchievementProgress(int achievementID, float duration_s);
	void ShowChallengeIndicator(int achievementID, bool show);  // call with show=false to hide.
	void ShowLeaderboardTracker(int leaderboardTrackerID, const char *trackerText, bool show);   // show=true is used both for create and update.

	// Progress bar controls
	// Set is both create and update. If you set maxValue <= minValue, you'll create an "indeterminate" progress
	// bar that doesn't show a specific amount of progress.
	void SetProgressBar(std::string id, std::string &&message, float minValue, float maxValue, float progress, float delay_s);
	void RemoveProgressBar(std::string id, bool success, float delay_s);

	// Call every frame to keep the sidebar visible. Otherwise it'll fade out.
	void NudgeSidebar();
	float SidebarAlpha() const;

	// Fades out everything related to achievements. Should be used on game shutdown.
	void ClearAchievementStuff();

	struct Entry {
		OSDType type;
		std::string text;
		std::string text2;
		std::string iconName;
		int numericID;
		const char *id;
		double startTime;
		double endTime;
	};

	struct ProgressBar {
		std::string id;
		std::string message;
		float minValue;
		float maxValue;
		float progress;
		double startTime;
		double endTime;
	};

	std::vector<Entry> Entries();
	std::vector<Entry> SideEntries();
	std::vector<ProgressBar> ProgressBars();

	static float FadeoutTime() { return 0.25f; }

private:
	std::vector<Entry> entries_;
	std::vector<Entry> sideEntries_;
	std::vector<ProgressBar> bars_;
	std::mutex mutex_;

	double sideBarShowTime_ = 0.0;
};

extern OnScreenDisplay g_OSD;
