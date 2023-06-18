// SPDX-FileCopyrightText: 2019-2022 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: (GPL-2.0 OR GPL-3.0 OR CC-BY-NC-ND-4.0)

// Derived from Duckstation's RetroAchievements implementation by stenzek as can be seen
// above, relicensed to GPL 2.0.
// Modifications and deletions have been made where needed.
// Refactoring it into a more PPSSPP-like style may or may not be a good idea -
// it'll make it harder to copy new achievement features from Duckstation.

#pragma once

#include "Common/StringUtils.h"
#include "Common/CommonTypes.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <mutex>

class Path;
class PointerWrap;

namespace Achievements {
enum class AchievementCategory : u8
{
	Local = 0,
	Core = 3,
	Unofficial = 5
};

struct Achievement
{
	u32 id;
	std::string title;
	std::string description;
	std::string memaddr;
	std::string badge_name;

	u32 points;
	AchievementCategory category;
	bool locked;
	bool active;
	bool primed;
};

struct Leaderboard
{
	u32 id;
	std::string title;
	std::string description;
	int format;
};

struct LeaderboardEntry
{
	std::string user;
	std::string formatted_score;
	time_t submitted;
	u32 rank;
	bool is_self;
};

// RAIntegration only exists for Windows, so no point checking it on other platforms.
#ifdef WITH_RAINTEGRATION

bool IsUsingRAIntegration();

#else

static inline bool IsUsingRAIntegration()
{
	return false;
}

#endif

bool IsActive();
bool IsLoggedIn();
bool ChallengeModeActive();
bool LeaderboardsActive();
bool IsTestModeActive();
bool IsUnofficialTestModeActive();
bool IsRichPresenceEnabled();
bool HasActiveGame();

u32 GetGameID();

/// Acquires the achievements lock. Must be held when accessing any achievement state from another thread.
std::unique_lock<std::recursive_mutex> GetLock();

void Initialize();
void UpdateSettings();

/// Called when the system is being reset. If it returns false, the reset should be aborted.
bool ConfirmSystemReset();

/// Called when the system is being shut down. If Shutdown() returns false, the shutdown should be aborted if possible.
bool Shutdown();

/// Called when the system is being paused and resumed.
void OnSystemPaused(bool paused);

/// Called once a frame at vsync time on the CPU thread.
void FrameUpdate();

/// Called when the system is paused, because FrameUpdate() won't be getting called.
void ProcessPendingHTTPRequests();

/// Saves/loads state.
bool DoState(PointerWrap &sw);

/// Returns true if the current game has any achievements or leaderboards.
/// Does not need to have the lock held.
bool SafeHasAchievementsOrLeaderboards();

const std::string &GetUsername();
const std::string &GetRichPresenceString();

bool LoginAsync(const char *username, const char *password);
bool Login(const char *username, const char *password);
void Logout();

void GameChanged(const Path &path);
void LeftGame();

/// Re-enables hardcode mode if it is enabled in the settings.
bool ResetChallengeMode();

/// Forces hardcore mode off until next reset.
void DisableChallengeMode();

/// Prompts the user to disable hardcore mode, if they agree, returns true.
bool ConfirmChallengeModeDisable(const char *trigger);

/// Returns true if features such as save states should be disabled.
bool ChallengeModeActive();

const std::string &GetGameTitle();
const std::string &GetGameIcon();

bool EnumerateAchievements(std::function<bool(const Achievement &)> callback);
u32 GetUnlockedAchiementCount();
u32 GetAchievementCount();
u32 GetMaximumPointsForGame();
u32 GetCurrentPointsForGame();

bool EnumerateLeaderboards(std::function<bool(const Leaderboard &)> callback);
std::optional<bool> TryEnumerateLeaderboardEntries(u32 id, std::function<bool(const LeaderboardEntry &)> callback);
const Leaderboard *GetLeaderboardByID(u32 id);
u32 GetLeaderboardCount();
bool IsLeaderboardTimeType(const Leaderboard &leaderboard);
u32 GetPrimedAchievementCount();

const Achievement *GetAchievementByID(u32 id);
std::pair<u32, u32> GetAchievementProgress(const Achievement &achievement);
std::string GetAchievementProgressText(const Achievement &achievement);
std::string GetAchievementBadgePath(const Achievement &achievement, bool download_if_missing = true,
	bool force_unlocked_icon = false);
std::string GetAchievementBadgeURL(const Achievement &achievement);

#ifdef WITH_RAINTEGRATION
void SwitchToRAIntegration();

namespace RAIntegration {
void MainWindowChanged(void *new_handle);
void GameChanged();
std::vector<std::tuple<int, std::string, bool>> GetMenuItems();
void ActivateMenuItem(int item);
} // namespace RAIntegration
#endif
} // namespace Achievements
