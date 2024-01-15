#include <cstring>
#include <algorithm>
// for std::min

#include "Common/System/OSD.h"
#include "Common/TimeUtil.h"
#include "Common/Log.h"
#include "Common/Math/math_util.h"

OnScreenDisplay g_OSD;

// Effectively forever.
constexpr double forever_s = 10000000000.0;

void OnScreenDisplay::Update() {
	std::lock_guard<std::mutex> guard(mutex_);

	double now = time_now_d();
	for (auto iter = entries_.begin(); iter != entries_.end(); ) {
		if (now >= iter->endTime) {
			iter = entries_.erase(iter);
		} else {
			iter++;
		}
	}
}

std::vector<OnScreenDisplay::Entry> OnScreenDisplay::Entries() {
	std::lock_guard<std::mutex> guard(mutex_);
	return entries_;  // makes a copy.
}

void OnScreenDisplay::NudgeSidebar() {
	sideBarShowTime_ = time_now_d();
}

float OnScreenDisplay::SidebarAlpha() const {
	double timeSinceNudge = time_now_d() - sideBarShowTime_;

	// Fade out in 1/4 second, 0.1s after the last nudge.
	return saturatef(1.0f - ((float)timeSinceNudge - 0.1f) * 4.0f);
}

void OnScreenDisplay::DismissEntry(size_t index, double now) {
	std::lock_guard<std::mutex> guard(mutex_);
	if (index < entries_.size() && entries_[index].type != OSDType::ACHIEVEMENT_CHALLENGE_INDICATOR) {
		entries_[index].endTime = std::min(now + FadeoutTime(), entries_[index].endTime);
	}
}

void OnScreenDisplay::Show(OSDType type, std::string_view text, std::string_view text2, std::string_view icon, float duration_s, const char *id) {
	// Automatic duration based on type.
	if (duration_s <= 0.0f) {
		switch (type) {
		case OSDType::MESSAGE_ERROR:
		case OSDType::MESSAGE_WARNING:
			duration_s = 4.0f;
			break;
		case OSDType::MESSAGE_FILE_LINK:
			duration_s = 5.0f;
			break;
		case OSDType::MESSAGE_INFO:
			duration_s = 3.0f;
			break;
		case OSDType::MESSAGE_SUCCESS:
			duration_s = 2.0f;
			break;
		default:
			duration_s = 1.5f;
			break;
		}
	}

	double now = time_now_d();
	std::lock_guard<std::mutex> guard(mutex_);
	if (id) {
		for (auto iter = entries_.begin(); iter != entries_.end(); ++iter) {
			if (iter->id == id) {
				Entry msg = *iter;
				msg.endTime = now + duration_s;
				msg.text = text;
				msg.text2 = text2;
				msg.type = type;
				msg.iconName = icon;
				// Move to top (should we? maybe not?)
				entries_.erase(iter);
				entries_.insert(entries_.begin(), msg);
				return;
			}
		}
	}

	Entry msg;
	msg.text = text;
	msg.text2 = text2;
	msg.iconName = icon;
	msg.startTime = now;
	msg.endTime = now + duration_s;
	msg.type = type;
	if (id) {
		msg.id = id;
	}
	entries_.insert(entries_.begin(), msg);
}

void OnScreenDisplay::ShowOnOff(std::string_view message, bool on, float duration_s) {
	std::string msg(message);
	msg += ": ";
	msg += on ? "on" : "off";
	// TODO: translate "on" and "off"? Or just get rid of this whole thing?
	Show(OSDType::MESSAGE_INFO, msg, duration_s);
}

void OnScreenDisplay::ShowAchievementUnlocked(int achievementID) {
	double now = time_now_d();

	double duration_s = 5.0;

	Entry msg;
	msg.numericID = achievementID;
	msg.type = OSDType::ACHIEVEMENT_UNLOCKED;
	msg.startTime = now;
	msg.endTime = now + duration_s;
	entries_.insert(entries_.begin(), msg);
}

void OnScreenDisplay::ShowAchievementProgress(int achievementID, bool show) {
	double now = time_now_d();

	// There can only be one of these at a time.
	for (auto &entry : entries_) {
		if (entry.type == OSDType::ACHIEVEMENT_PROGRESS) {
			if (!show) {
				// Hide and eventually delete it.
				entry.endTime = now + (double)FadeoutTime();
				// Found it, we're done.
				return;
			}
			// Else update it.
			entry.numericID = achievementID;
			entry.endTime = now + forever_s;
			return;
		}
	}

	if (!show) {
		// Sanity check
		return;
	}

	// OK, let's make a new side-entry.
	Entry entry;
	entry.numericID = achievementID;
	entry.type = OSDType::ACHIEVEMENT_PROGRESS;
	entry.startTime = now;
	entry.endTime = now + forever_s;
	entries_.insert(entries_.begin(), entry);
}

void OnScreenDisplay::ShowChallengeIndicator(int achievementID, bool show) {
	double now = time_now_d();

	for (auto &entry : entries_) {
		if (entry.numericID == achievementID && entry.type == OSDType::ACHIEVEMENT_CHALLENGE_INDICATOR && !show) {
			// Hide and eventually delete it.
			entry.endTime = now + (double)FadeoutTime();
			// Found it, we're done.
			return;
		}
	}

	if (!show) {
		// Sanity check
		return;
	}

	// OK, let's make a new side-entry.
	Entry entry;
	entry.numericID = achievementID;
	entry.type = OSDType::ACHIEVEMENT_CHALLENGE_INDICATOR;
	entry.startTime = now;
	entry.endTime = now + forever_s;
	entries_.insert(entries_.begin(), entry);
}

void OnScreenDisplay::ShowLeaderboardTracker(int leaderboardTrackerID, const char *trackerText, bool show) {   // show=true is used both for create and update.
	double now = time_now_d();

	for (auto &entry : entries_) {
		if (entry.numericID == leaderboardTrackerID && entry.type == OSDType::LEADERBOARD_TRACKER) {
			if (show) {
				// Just an update.
				entry.text = trackerText ? trackerText : "";
				// Bump the end-time, in case it was fading out.
				entry.endTime = now + forever_s;
			} else {
				// Keep the current text, hide and eventually delete it.
				entry.endTime = now + (double)FadeoutTime();
			}
			// Found it, we're done.
			return;
		}
	}

	if (!show) {
		// Sanity check
		return;
	}

	// OK, let's make a new side-entry.
	Entry entry;
	entry.numericID = leaderboardTrackerID;
	entry.type = OSDType::LEADERBOARD_TRACKER;
	entry.startTime = now;
	entry.endTime = now + forever_s;
	if (trackerText) {
		entry.text = trackerText;
	}
	entries_.insert(entries_.begin(), entry);
}

void OnScreenDisplay::ShowLeaderboardStartEnd(const std::string &title, const std::string &description, bool started) {
	g_OSD.Show(OSDType::LEADERBOARD_STARTED_FAILED, title, description, 3.0f);
}

void OnScreenDisplay::ShowLeaderboardSubmitted(const std::string &title, const std::string &value) {
	g_OSD.Show(OSDType::LEADERBOARD_SUBMITTED, title, value, 3.0f);
}

void OnScreenDisplay::SetProgressBar(const std::string &id, std::string &&message, float minValue, float maxValue, float progress, float delay) {
	_dbg_assert_(!my_isnanorinf(progress));
	_dbg_assert_(!my_isnanorinf(minValue));
	_dbg_assert_(!my_isnanorinf(maxValue));

	double now = time_now_d();
	bool found = false;

	std::lock_guard<std::mutex> guard(mutex_);
	for (auto &bar : entries_) {
		if (bar.type == OSDType::PROGRESS_BAR && bar.id == id) {
			bar.minValue = minValue;
			bar.maxValue = maxValue;
			bar.progress = progress;
			bar.text = message;
			bar.endTime = now + 60.0;  // Nudge the progress bar to keep it shown.
			return;
		}
	}

	Entry bar;
	bar.id = id;
	bar.type = OSDType::PROGRESS_BAR;
	bar.text = std::move(message);
	bar.minValue = minValue;
	bar.maxValue = maxValue;
	bar.progress = progress;
	bar.startTime = now + delay;
	bar.endTime = now + 60.0;  // Show the progress bar for 60 seconds, then fade it out.
	entries_.push_back(bar);
}

void OnScreenDisplay::RemoveProgressBar(const std::string &id, bool success, float delay_s) {
	std::lock_guard<std::mutex> guard(mutex_);
	for (auto iter = entries_.begin(); iter != entries_.end(); iter++) {
		if (iter->type == OSDType::PROGRESS_BAR && iter->id == id) {
			if (success) {
				// Quickly shoot up to max, if we weren't there.
				if (iter->maxValue != 0.0f) {
					iter->progress = iter->maxValue;
				} else {
					// Fake a full progress
					iter->minValue = 0;
					iter->maxValue = 1;
					iter->progress = 1;
				}
			}
			iter->endTime = time_now_d() + delay_s + FadeoutTime();
			break;
		}
	}
}

// Fades out everything related to achievements. Should be used on game shutdown.
void OnScreenDisplay::ClearAchievementStuff() {
	double now = time_now_d();
	for (auto &iter : entries_) {
		switch (iter.type) {
		case OSDType::ACHIEVEMENT_CHALLENGE_INDICATOR:
		case OSDType::ACHIEVEMENT_UNLOCKED:
		case OSDType::ACHIEVEMENT_PROGRESS:
		case OSDType::LEADERBOARD_TRACKER:
		case OSDType::LEADERBOARD_STARTED_FAILED:
		case OSDType::LEADERBOARD_SUBMITTED:
			iter.endTime = now;
			break;
		default:
			break;
		}
	}
}
