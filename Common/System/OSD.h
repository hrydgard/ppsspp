#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <mutex>

#include "Common/Common.h"

// Shows a visible message to the user.
// The default implementation in NativeApp.cpp uses our "osm" system (on screen messaging).
enum class OSDType {
	MESSAGE_INFO,
	MESSAGE_SUCCESS,
	MESSAGE_WARNING,
	MESSAGE_ERROR,
	MESSAGE_ERROR_DUMP,  // displays lots of text (after the first line), small size
	MESSAGE_FILE_LINK,
	MESSAGE_CENTERED_WARNING,
	MESSAGE_CENTERED_ERROR,

	ACHIEVEMENT_UNLOCKED,

	// Side entries
	ACHIEVEMENT_PROGRESS,  // Achievement icon + "measured_progress" text, auto-hide after 2s
	ACHIEVEMENT_CHALLENGE_INDICATOR,  // Achievement icon ONLY, no auto-hide

	LEADERBOARD_TRACKER,
	LEADERBOARD_STARTED_FAILED,
	LEADERBOARD_SUBMITTED,

	PROGRESS_BAR,

	STATUS_ICON,  // Use icons from the atlas

	VALUE_COUNT,
};

#undef None  // X11, sigh.

enum class OSDMessageFlags {
	None = 0,
	SpinLeft = 1,
	SpinRight = 2,
	Transparent = 4,
};
ENUM_CLASS_BITOPS(OSDMessageFlags);

// Data holder for on-screen messages.
class OnScreenDisplay {
public:
	~OnScreenDisplay();

	// If you specify 0.0f as duration, a duration will be chosen automatically depending on type.
	void Show(OSDType type, std::string_view text, float duration_s = 0.0f, const char *id = nullptr) {
		Show(type, text, "", duration_s, id);
	}
	void Show(OSDType type, std::string_view text, std::string_view text2, float duration_s = 0.0f, const char *id = nullptr) {
		Show(type, text, text2, "", duration_s, id);
	}
	void Show(OSDType type, std::string_view text, std::string_view text2, std::string_view icon, float duration_s = 0.0f, const char *id = nullptr);

	void ShowOnOff(std::string_view message, bool on, float duration_s = 0.0f);
	void CancelById(std::string_view id);

	bool IsEmpty() const { return entries_.empty(); }  // Shortcut to skip rendering.

	// Call this every frame, cleans up old entries.
	void Update();

	// Specialized achievement-related types. These go to the side notifications, not the top-middle.
	void ShowAchievementUnlocked(int achievementID);
	void ShowAchievementProgress(int achievementID, bool show);  // call with show=false to hide.  There can only be one of these. When hiding it's ok to not pass a valid achievementID.
	void ShowChallengeIndicator(int achievementID, bool show);  // call with show=false to hide.
	void ShowLeaderboardTracker(int leaderboardTrackerID, std::string_view trackerText, bool show);   // show=true is used both for create and update.
	void ShowLeaderboardStartEnd(std::string_view title, std::string_view description, bool started);  // started = true for started, false for ended.
	void ShowLeaderboardSubmitted(std::string_view title, std::string_view value);

	// Progress bar controls
	// Set is both create and update. If you set maxValue <= minValue, you'll create an "indeterminate" progress
	// bar that doesn't show a specific amount of progress.
	void SetProgressBar(std::string_view id, std::string_view message, float minValue, float maxValue, float progress, float delay_s);
	void RemoveProgressBar(std::string_view id, bool success, float delay_s);

	// Call every frame to keep the sidebar visible. Otherwise it'll fade out.
	void NudgeIngameNotifications();
	float IngameAlpha() const;

	// Fades out everything related to achievements. Should be used on game shutdown.
	void ClearAchievementStuff();

	// Can't add an infinite number of "Show" functions, so starting to offer post-modification.
	void SetClickCallback(const char *id, void (*callback)(bool, void *), void *userdata);
	void SetFlags(const char *id, OSDMessageFlags flag);

	struct Entry {
		OSDType type;
		std::string text;
		std::string text2;
		std::string iconName;
		int numericID;
		std::string id;
		OSDMessageFlags flags;

		// We could use std::function, but prefer to do it the oldschool way.
		void (*clickCallback)(bool, void *);
		void *clickUserData;

		int instanceID;
		double startTime;
		double endTime;

		// Progress-bar-only data:
		float minValue;
		float maxValue;
		float progress;
	};

	std::vector<Entry> Entries();

	// TODO: Use something more stable than the index.
	void ClickEntry(size_t index, double now);

	static float FadeinTime() { return 0.1f; }
	static float FadeoutTime() { return 0.25f; }

private:
	std::vector<Entry> entries_;
	std::mutex mutex_;

	double sideBarShowTime_ = 0.0;
};

extern OnScreenDisplay g_OSD;
