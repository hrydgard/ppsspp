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
	ACHIEVEMENT_PROGRESS,
	ACHIEVEMENT_CHALLENGE_INDICATOR,

	// PROGRESS_BAR,
	// PROGRESS_INDETERMINATE,
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
	void ShowAchievementUnlocked(int achievementID);

	void ShowAchievementProgress(int achievementID, float duration_s);

	void ShowOnOff(const std::string &message, bool on, float duration_s = 0.0f);

	bool IsEmpty() const { return entries_.empty(); }  // Shortcut to skip rendering.

	// Call this every frame, cleans up old entries.
	void Update();

	// Progress bar controls
	// Set is both create and update.
	void SetProgressBar(std::string id, std::string &&message, int minValue, int maxValue, int progress);
	void RemoveProgressBar(std::string id, float fadeout_s);

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
		int minValue;
		int maxValue;
		int progress;
		double endTime;
	};

	std::vector<Entry> Entries();
	std::vector<Entry> SideEntries();
	std::vector<ProgressBar> ProgressBars();

private:
	std::vector<Entry> entries_;
	std::vector<Entry> sideEntries_;
	std::vector<ProgressBar> bars_;
	std::mutex mutex_;
};

extern OnScreenDisplay g_OSD;
