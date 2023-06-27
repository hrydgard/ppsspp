// SPDX-FileCopyrightText: 2019-2022 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: (GPL-2.0 OR GPL-3.0 OR CC-BY-NC-ND-4.0)

// Derived from Duckstation's RetroAchievements implementation by stenzek as can be seen
// above, relicensed to GPL 2.0.

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <string>
#include <vector>
#include <mutex>

#include "ext/rcheevos/include/rcheevos.h"
#include "ext/rcheevos/include/rc_api_user.h"
#include "ext/rcheevos/include/rc_api_info.h"
#include "ext/rcheevos/include/rc_api_request.h"
#include "ext/rcheevos/include/rc_api_runtime.h"
#include "ext/rcheevos/include/rc_api_user.h"
#include "ext/rcheevos/include/rc_url.h"
#include "ext/rcheevos/include/rc_hash.h"
#include "ext/rcheevos/src/rhash/md5.h"

#include "UI/RetroAchievements.h"
#include "ext/rapidjson/include/rapidjson/document.h"

#include "Common/Log.h"
#include "Common/File/Path.h"
#include "Common/File/FileUtil.h"
#include "Common/Net/HTTPClient.h"
#include "Common/System/NativeApp.h"
#include "Common/TimeUtil.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Serialize/Serializer.h"
#include "Common/System/System.h"
#include "Common/Crypto/md5.h"
#include "Common/UI/IconCache.h"

#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/CoreParameter.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/System.h"
#include "Core/FileSystems/MetaFileSystem.h"

#include "UI/Root.h"

#ifdef WITH_RAINTEGRATION
// RA_Interface ends up including windows.h, with its silly macros.
#ifdef _WIN32
#include "common/windows_headers.h"
#endif
#include "RA_Interface.h"
#endif

// Simply wrap our current HTTP backend to fit the DuckStation-derived code.
namespace Common {
	class HTTPDownloader {
	public:
		static std::unique_ptr<HTTPDownloader> Create() {
			return std::unique_ptr<HTTPDownloader>(new HTTPDownloader());
		}
		class Request {
		public:
			typedef std::string Data;
			typedef std::function<void(s32 status_code, std::string content_type, Data data)> Callback;
		};

		void PollRequests() {
			downloader_.Update();
		}
		void WaitForAllRequests() {
			downloader_.WaitForAll();
		}
		bool HasAnyRequests() const {
			return downloader_.GetActiveCount() > 0;
		}
		void CreateRequest(std::string &&url, Request::Callback &&callback) {
			Request::Callback movedCallback = std::move(callback);
			downloader_.StartDownloadWithCallback(url, Path(), [=](http::Download &download) {
				std::string data;
				download.buffer().TakeAll(&data);
				movedCallback(download.ResultCode(), "", data);
			});
		}
		void CreatePostRequest(std::string &&url, const char *post_data, Request::Callback &&callback) {
			Request::Callback movedCallback = std::move(callback);
			std::string post_data_str(post_data);

			INFO_LOG(ACHIEVEMENTS, "Request: post_data=%s", post_data);

			downloader_.AsyncPostWithCallback(url, post_data_str, "application/x-www-form-urlencoded", [=](http::Download &download) {
				std::string data;
				download.buffer().TakeAll(&data);
				movedCallback(download.ResultCode(), "", data);
			});
		}

	private:
		HTTPDownloader() {}

		http::Downloader downloader_;
	};
}  // namespace

void OSDAddToast(float duration_s, const std::string &text) {
	g_OSD.Show(OSDType::MESSAGE_INFO, text);
}

void OSDAddNotification(float duration_s, const std::string &title, const std::string &summary, const std::string &iconImageData) {
	g_OSD.Show(OSDType::MESSAGE_INFO, title, summary, iconImageData, 5.0f);
}

void OSDAddAchievementUnlockedNotification(unsigned int achievementId) {
	g_OSD.ShowAchievementUnlocked(achievementId);
}

void OSDOpenBackgroundProgressDialog(const char *str_id, std::string message, s32 min, s32 max, s32 value) {
	NOTICE_LOG(ACHIEVEMENTS, "Progress dialog opened: %s %s", str_id, message.c_str());
	g_OSD.SetProgressBar(str_id, std::move(message), min, max, value);
}

void OSDUpdateBackgroundProgressDialog(const char *str_id, std::string message, s32 min, s32 max, s32 value) {
	NOTICE_LOG(ACHIEVEMENTS, "Progress dialog updated: %s %s %d/(%d->%d)", str_id, message.c_str(), value, min, max);
	g_OSD.SetProgressBar(str_id, std::move(message), min, max, value);
}

void OSDCloseBackgroundProgressDialog(const char *str_id) {
	NOTICE_LOG(ACHIEVEMENTS, "Progress dialog closed: %s", str_id);
	g_OSD.RemoveProgressBar(str_id, 0.25f);
}

void OSDAddErrorMessage(const char *str_id, std::string message, float duration) {
	g_OSD.Show(OSDType::MESSAGE_ERROR, message);
	NOTICE_LOG(ACHIEVEMENTS, "Keyed message: %s %s (%0.1f s)", str_id, message.c_str(), duration);
}

namespace Host {
void OnAchievementsRefreshed() {
	System_PostUIMessage("achievements_refreshed", "");
}
void OnAchievementsLoginStateChange() {
	System_PostUIMessage("achievements_loginstatechange", "");
}
}

namespace Achievements {

enum : s32
{
	HTTP_OK = 200,

	// Number of seconds between rich presence pings. RAIntegration uses 2 minutes.
	RICH_PRESENCE_PING_FREQUENCY = 2 * 60,
	NO_RICH_PRESENCE_PING_FREQUENCY = RICH_PRESENCE_PING_FREQUENCY * 2,
};

// temporary sounds
static constexpr UI::UISound INFO_SOUND_NAME = UI::UISound::SELECT;
static constexpr UI::UISound UNLOCK_SOUND_NAME = UI::UISound::TOGGLE_ON;
static constexpr UI::UISound LBSUBMIT_SOUND_NAME = UI::UISound::TOGGLE_OFF;

// It's the name of the secret, not a secret name - the value is not secret :)
static const char *RA_TOKEN_SECRET_NAME = "retroachievements";

static void FormattedError(const char *format, ...);
static void LogFailedResponseJSON(const Common::HTTPDownloader::Request::Data &data);
static void CheevosEventHandler(const rc_runtime_event_t *runtime_event);
static unsigned PeekMemory(unsigned address, unsigned num_bytes, void *ud);
static bool IsMastered();
static void ActivateLockedAchievements();
static bool ActivateAchievement(Achievement *achievement);
static void DeactivateAchievement(Achievement *achievement);
static void UnlockAchievement(u32 achievement_id, bool add_notification = true);
static void AchievementPrimed(u32 achievement_id);
static void AchievementUnprimed(u32 achievement_id);
static void AchievementDisabled(u32 achievement_id);
static void SubmitLeaderboard(u32 leaderboard_id, int value);
static void SendPing();
static void SendPlaying();
static void UpdateRichPresence();
static Achievement *GetMutableAchievementByID(u32 id);
static void ClearGameInfo(bool clear_achievements = true, bool clear_leaderboards = true);
static void ClearGameHash();
static std::string GetUserAgent();
static void LoginCallback(s32 status_code, std::string content_type, Common::HTTPDownloader::Request::Data data);
static void LoginASyncCallback(s32 status_code, std::string content_type, Common::HTTPDownloader::Request::Data data);
static void SendLogin(const char *username, const char *password, Common::HTTPDownloader *http_downloader,
	Common::HTTPDownloader::Request::Callback callback);
static void DownloadImage(std::string url, std::string cache_filename);
static void DisplayAchievementSummary();
static void DisplayMasteredNotification();
static void GetUserUnlocksCallback(s32 status_code, std::string content_type,
	Common::HTTPDownloader::Request::Data data);
static void GetUserUnlocks();
static void GetPatchesCallback(s32 status_code, std::string content_type, Common::HTTPDownloader::Request::Data data);
static void GetLbInfoCallback(s32 status_code, std::string content_type, Common::HTTPDownloader::Request::Data data);
static void GetPatches(u32 game_id);
static std::string GetGameHash(const Path &path);
static void SetChallengeMode(bool enabled);
static void SendGetGameId();
static void GetGameIdCallback(s32 status_code, std::string content_type, Common::HTTPDownloader::Request::Data data);
static void SendPlayingCallback(s32 status_code, std::string content_type, Common::HTTPDownloader::Request::Data data);
static void UpdateRichPresence();
static void SendPingCallback(s32 status_code, std::string content_type, Common::HTTPDownloader::Request::Data data);
static void UnlockAchievementCallback(s32 status_code, std::string content_type,
	Common::HTTPDownloader::Request::Data data);
static void SubmitLeaderboardCallback(s32 status_code, std::string content_type,
	Common::HTTPDownloader::Request::Data data);
static void ResetRuntime();

static bool s_active = false;
static bool s_logged_in = false;
static bool s_challenge_mode = false;
static u32 s_game_id = 0;

#ifdef WITH_RAINTEGRATION
static bool s_using_raintegration = false;
#endif

static std::recursive_mutex s_achievements_mutex;
static rc_runtime_t s_rcheevos_runtime;
static std::unique_ptr<Common::HTTPDownloader> s_http_downloader;

static std::string s_username;
static std::string s_api_token;

static Path s_game_path;
static std::string s_game_hash;
static std::string s_game_title;
static std::string s_game_icon;
static std::vector<Achievements::Achievement> s_achievements;
static std::vector<Achievements::Leaderboard> s_leaderboards;
static std::atomic<u32> s_primed_achievement_count{0};

static bool s_has_rich_presence = false;
static std::string s_rich_presence_string;
static double s_last_ping_time;

static u32 s_last_queried_lboard = 0;
static u32 s_submitting_lboard_id = 0;
static std::optional<std::vector<Achievements::LeaderboardEntry>> s_lboard_entries;

static Achievements::Statistics g_stats;

const std::string g_gameIconCachePrefix = "game:";
const std::string g_iconCachePrefix = "badge:";



#define PSP_MEMORY_OFFSET 0x08000000

// TODO: Add an icon cache as a string map. We won't cache achievement icons across sessions, let's just
// download them as we go.

template<typename T>
static const char *RAPIStructName();

#define RAPI_STRUCT_NAME(x)                                                                                            \
  template<>                                                                                                           \
  const char* RAPIStructName<x>()                                                                                      \
  {                                                                                                                    \
    return #x;                                                                                                         \
  }

RAPI_STRUCT_NAME(rc_api_login_request_t);
RAPI_STRUCT_NAME(rc_api_fetch_image_request_t);
RAPI_STRUCT_NAME(rc_api_resolve_hash_request_t);
RAPI_STRUCT_NAME(rc_api_fetch_game_data_request_t);
RAPI_STRUCT_NAME(rc_api_fetch_user_unlocks_request_t);
RAPI_STRUCT_NAME(rc_api_start_session_request_t);
RAPI_STRUCT_NAME(rc_api_ping_request_t);
RAPI_STRUCT_NAME(rc_api_award_achievement_request_t);
RAPI_STRUCT_NAME(rc_api_submit_lboard_entry_request_t);
RAPI_STRUCT_NAME(rc_api_fetch_leaderboard_info_request_t);

RAPI_STRUCT_NAME(rc_api_login_response_t);
RAPI_STRUCT_NAME(rc_api_resolve_hash_response_t);
RAPI_STRUCT_NAME(rc_api_fetch_game_data_response_t);
RAPI_STRUCT_NAME(rc_api_ping_response_t);
RAPI_STRUCT_NAME(rc_api_award_achievement_response_t);
RAPI_STRUCT_NAME(rc_api_submit_lboard_entry_response_t);
RAPI_STRUCT_NAME(rc_api_start_session_response_t);
RAPI_STRUCT_NAME(rc_api_fetch_user_unlocks_response_t);
RAPI_STRUCT_NAME(rc_api_fetch_leaderboard_info_response_t);

// Unused for now.
// RAPI_STRUCT_NAME(rc_api_fetch_achievement_info_response_t);
// RAPI_STRUCT_NAME(rc_api_fetch_games_list_response_t);

#undef RAPI_STRUCT_NAME

template<typename T, int (*InitFunc)(rc_api_request_t *, const T *)>
struct RAPIRequest : public T
{
private:
	rc_api_request_t api_request;

public:
	RAPIRequest() { std::memset(this, 0, sizeof(*this)); }

	~RAPIRequest() { rc_api_destroy_request(&api_request); }

	void Send(Common::HTTPDownloader::Request::Callback callback) { Send(s_http_downloader.get(), std::move(callback)); }

	void Send(Common::HTTPDownloader *http_downloader, Common::HTTPDownloader::Request::Callback callback)
	{
		const int error = InitFunc(&api_request, this);
		if (error != RC_OK)
		{
			FormattedError("%s failed: error %d (%s)", RAPIStructName<T>(), error, rc_error_str(error));
			callback(-1, std::string(), Common::HTTPDownloader::Request::Data());
			return;
		}

		if (api_request.post_data)
		{
			// needs to be a post
			http_downloader->CreatePostRequest(api_request.url, api_request.post_data, std::move(callback));
		} else
		{
			// get is fine
			http_downloader->CreateRequest(api_request.url, std::move(callback));
		}
	}

	bool DownloadImage(std::string cache_filename)
	{
		const int error = InitFunc(&api_request, this);
		if (error != RC_OK)
		{
			FormattedError("%s failed: error %d (%s)", RAPIStructName<T>(), error, rc_error_str(error));
			return false;
		}

		_dbg_assert_msg_(!api_request.post_data, "Download request does not have POST data");

		Achievements::DownloadImage(api_request.url, std::move(cache_filename));
		return true;
	}

	std::string GetURL()
	{
		const int error = InitFunc(&api_request, this);
		if (error != RC_OK)
		{
			FormattedError("%s failed: error %d (%s)", RAPIStructName<T>(), error, rc_error_str(error));
			return std::string();
		}

		return api_request.url;
	}
};

template<typename T, int (*ParseFunc)(T *, const char *), void (*DestroyFunc)(T *)>
struct RAPIResponse : public T
{
private:
	bool initialized = false;

public:
	RAPIResponse(s32 status_code, Common::HTTPDownloader::Request::Data &data)
	{
		if (status_code != 200 || data.empty())
		{
			FormattedError("%s failed: empty response and/or status code %d", RAPIStructName<T>(), status_code);
			LogFailedResponseJSON(data);
			return;
		}

		// ensure null termination, rapi needs it
		data.push_back(0);

		const int error = ParseFunc(this, reinterpret_cast<const char *>(data.data()));
		initialized = (error == RC_OK);

		const rc_api_response_t &response = static_cast<T *>(this)->response;
		if (error != RC_OK)
		{
			FormattedError("%s failed: parse function returned %d (%s)", RAPIStructName<T>(), error, rc_error_str(error));
			LogFailedResponseJSON(data);
		} else if (!response.succeeded)
		{
			FormattedError("%s failed: %s", RAPIStructName<T>(),
				response.error_message ? response.error_message : "<no error>");
			LogFailedResponseJSON(data);
		}
	}

	~RAPIResponse()
	{
		if (initialized)
			DestroyFunc(this);
	}

	operator bool() const { return initialized && static_cast<const T *>(this)->response.succeeded; }
};

} // namespace Achievements

#ifdef WITH_RAINTEGRATION
bool Achievements::IsUsingRAIntegration()
{
	return s_using_raintegration;
}
#endif

void Achievements::FormattedError(const char *format, ...)
{
	std::va_list ap;
	va_start(ap, format);
	char buffer[1024];
	vsnprintf(buffer, sizeof(buffer), format, ap);
	va_end(ap);

	ERROR_LOG(ACHIEVEMENTS, "%s", buffer);
	// Host::AddOSDMessage(std::move(error), 10.0f);
}

void Achievements::LogFailedResponseJSON(const Common::HTTPDownloader::Request::Data &data)
{
	const std::string str_data(reinterpret_cast<const char *>(data.data()), data.size());
	ERROR_LOG(ACHIEVEMENTS, "API call failed. Response JSON was:\n%s", str_data.c_str());
}

const Achievements::Achievement *Achievements::GetAchievementByID(u32 id)
{
	for (const Achievement &ach : s_achievements)
	{
		if (ach.id == id)
			return &ach;
	}

	return nullptr;
}

Achievements::Achievement *Achievements::GetMutableAchievementByID(u32 id)
{
	for (Achievement &ach : s_achievements)
	{
		if (ach.id == id)
			return &ach;
	}

	return nullptr;
}

void Achievements::ClearGameInfo(bool clear_achievements, bool clear_leaderboards)
{
	const bool had_game = (s_game_id != 0);

	if (clear_achievements)
	{
		while (!s_achievements.empty())
		{
			Achievement &ach = s_achievements.back();
			DeactivateAchievement(&ach);
			s_achievements.pop_back();
		}
		s_primed_achievement_count.store(0, std::memory_order_release);
	}
	if (clear_leaderboards)
	{
		while (!s_leaderboards.empty())
		{
			Leaderboard &lb = s_leaderboards.back();
			rc_runtime_deactivate_lboard(&s_rcheevos_runtime, lb.id);
			s_leaderboards.pop_back();
		}

		s_last_queried_lboard = 0;
		s_submitting_lboard_id = 0;
		s_lboard_entries.reset();
	}

	if (s_achievements.empty() && s_leaderboards.empty())
	{
		// Ready to tear down cheevos completely
		s_game_title = {};
		s_game_icon = {};
		s_rich_presence_string = {};
		s_has_rich_presence = false;
		s_game_id = 0;
	}

	// Reset statistics
	g_stats = {};

	if (had_game)
		Host::OnAchievementsRefreshed();
}

void Achievements::ClearGameHash()
{
	s_game_path.clear();
	s_game_hash.clear();
}

bool Achievements::IsActive()
{
	return s_active;
}

bool Achievements::IsLoggedIn()
{
	return s_logged_in;
}

bool Achievements::ChallengeModeActive()
{
#ifdef WITH_RAINTEGRATION
	if (IsUsingRAIntegration())
		return RA_HardcoreModeIsActive() != 0;
#endif

	return s_challenge_mode;
}

bool Achievements::LeaderboardsActive()
{
	return ChallengeModeActive() && g_Config.bAchievementsLeaderboards;
}

bool Achievements::IsTestModeActive()
{
	return g_Config.bAchievementsTestMode;
}

bool Achievements::IsUnofficialTestModeActive()
{
	return g_Config.bAchievementsUnofficialTestMode;
}

bool Achievements::IsRichPresenceEnabled()
{
	return g_Config.bAchievementsRichPresence;
}

bool Achievements::HasActiveGame()
{
	return s_game_id != 0;
}

u32 Achievements::GetGameID()
{
	return s_game_id;
}

std::unique_lock<std::recursive_mutex> Achievements::GetLock()
{
	return std::unique_lock(s_achievements_mutex);
}

void Achievements::Initialize()
{
	if (IsUsingRAIntegration())
		return;

	std::unique_lock lock(s_achievements_mutex);
	_assert_msg_(g_Config.bAchievementsEnable, "Achievements are enabled");

	s_http_downloader = Common::HTTPDownloader::Create();
	if (!s_http_downloader)
	{
		// TODO: Also report to user
		ERROR_LOG(ACHIEVEMENTS, "Failed to create HTTPDownloader, cannot use achievements");
		return;
	}

	s_active = true;
	s_challenge_mode = false;
	rc_runtime_init(&s_rcheevos_runtime);

	s_last_ping_time = time_now_d();
	s_username = g_Config.sAchievementsUserName;
	s_api_token = NativeLoadSecret(RA_TOKEN_SECRET_NAME);
	if (s_api_token.empty()) {
		s_api_token = g_Config.sAchievementsToken;
	}
	s_logged_in = (!s_username.empty() && !s_api_token.empty());

	// this is just the non-SSL path.
	rc_api_set_host("http://retroachievements.org");

	// if (System::IsValid())
	// GameChanged();
}

void Achievements::UpdateSettings()
{
	if (IsUsingRAIntegration())
		return;

	if (!g_Config.bAchievementsEnable)
	{
		// we're done here
		Shutdown();
		return;
	}

	if (!s_active)
	{
		// we just got enabled
		Initialize();
		return;
	}

	/*
	// TODO: We don't have an "old" config state. But we can probably maintain one right here
	// in this file.

	if (g_settings.achievements_challenge_mode != old_config.achievements_challenge_mode)
	{
		// Hardcore mode can only be enabled through reset (ResetChallengeMode()).
		if (s_challenge_mode && !g_settings.achievements_challenge_mode)
		{
			ResetChallengeMode();
		} else if (!s_challenge_mode && g_settings.achievements_challenge_mode)
		{
			ImGuiFullscreen::ShowToast(
				std::string(), Host::TranslateStdString("Achievements", "Hardcore mode will be enabled on system reset."),
				10.0f);
		}
	}
	*/

	// FIXME: Handle changes to various settings individually
	/*
	if (g_settings.achievements_test_mode != old_config.achievements_test_mode ||
		g_settings.achievements_unofficial_test_mode != old_config.achievements_unofficial_test_mode ||
		g_settings.achievements_use_first_disc_from_playlist != old_config.achievements_use_first_disc_from_playlist ||
		g_settings.achievements_rich_presence != old_config.achievements_rich_presence)
	{
		return;
	}
	*/

	Shutdown();
	Initialize();
}

bool Achievements::ConfirmChallengeModeDisable(const char *trigger)
{
#ifdef WITH_RAINTEGRATION
	if (IsUsingRAIntegration())
		return (RA_WarnDisableHardcore(trigger) != 0);
#endif

	// I really hope this doesn't deadlock :/
	/*
	const bool confirmed = Host::ConfirmMessage(
		Host::TranslateString("Achievements", "Confirm Hardcore Mode"),
		fmt::format(Host::TranslateString("Achievements",
			"{0} cannot be performed while hardcore mode is active. Do you "
			"want to disable hardcore mode? {0} will be cancelled if you select No.")
			.GetCharArray(),
			trigger));
	if (!confirmed)
		return false;
		*/

	DisableChallengeMode();
	return true;
}

void Achievements::DisableChallengeMode()
{
	if (!s_active)
		return;

#ifdef WITH_RAINTEGRATION
	if (IsUsingRAIntegration())
	{
		if (RA_HardcoreModeIsActive())
			RA_DisableHardcore();

		return;
	}
#endif

	if (s_challenge_mode)
		SetChallengeMode(false);
}

bool Achievements::ResetChallengeMode()
{
	if (!s_active || s_challenge_mode == g_Config.bAchievementsChallengeMode)
		return false;

	SetChallengeMode(g_Config.bAchievementsChallengeMode);
	return true;
}

void Achievements::SetChallengeMode(bool enabled)
{
	if (enabled == s_challenge_mode)
		return;

	// new mode
	s_challenge_mode = enabled;

	if (HasActiveGame())
	{
		auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
		auto di = GetI18NCategory(I18NCat::DIALOG);

		OSDAddNotification(5.0f, std::string(ac->T("Challenge mode")) + ": " + di->T(enabled ? "Enabled" : "Disabled"), "", "");
	}

	if (HasActiveGame() && !IsTestModeActive())
	{
		// deactivate, but don't clear all achievements (getting unlocks will reactivate them)
		std::unique_lock lock(s_achievements_mutex);
		for (Achievement &achievement : s_achievements)
		{
			DeactivateAchievement(&achievement);
			achievement.locked = true;
		}
		for (Leaderboard &leaderboard : s_leaderboards)
			rc_runtime_deactivate_lboard(&s_rcheevos_runtime, leaderboard.id);
	}

	// re-grab unlocks, this will reactivate what's locked in non-hardcore mode later on
	if (!s_achievements.empty())
		GetUserUnlocks();
}

bool Achievements::Shutdown()
{
#ifdef WITH_RAINTEGRATION
	if (IsUsingRAIntegration())
	{
		RA_SetPaused(false);
		RA_ActivateGame(0);
		return true;
	}
#endif

	if (!s_active)
		return true;

	std::unique_lock lock(s_achievements_mutex);
	s_http_downloader->WaitForAllRequests();

	ClearGameInfo();
	ClearGameHash();
	std::string().swap(s_username);
	std::string().swap(s_api_token);
	s_logged_in = false;
	Host::OnAchievementsRefreshed();

	s_active = false;
	s_challenge_mode = false;
	rc_runtime_destroy(&s_rcheevos_runtime);

	s_http_downloader.reset();
	return true;
}

bool Achievements::ConfirmSystemReset()
{
#ifdef WITH_RAINTEGRATION
	if (IsUsingRAIntegration())
		return RA_ConfirmLoadNewRom(false);
#endif

	return true;
}

void Achievements::ResetRuntime()
{
#ifdef WITH_RAINTEGRATION
	if (IsUsingRAIntegration())
	{
		RA_OnReset();
		return;
	}
#endif

	if (!s_active)
		return;

	std::unique_lock lock(s_achievements_mutex);
	INFO_LOG(ACHIEVEMENTS, "Resetting rcheevos state...");
	rc_runtime_reset(&s_rcheevos_runtime);
}

void Achievements::OnSystemPaused(bool paused)
{
#ifdef WITH_RAINTEGRATION
	if (IsUsingRAIntegration())
		RA_SetPaused(paused);
#endif
}

void Achievements::FrameUpdate()
{
	if (!IsActive())
		return;

#ifdef WITH_RAINTEGRATION
	if (IsUsingRAIntegration())
	{
		RA_DoAchievementsFrame();
		return;
	}
#endif

	s_http_downloader->PollRequests();

	if (HasActiveGame())
	{
		std::unique_lock lock(s_achievements_mutex);
		rc_runtime_do_frame(&s_rcheevos_runtime, &CheevosEventHandler, &PeekMemory, nullptr, nullptr);
		UpdateRichPresence();

		if (!IsTestModeActive())
		{
			const s32 ping_frequency =
				g_Config.bAchievementsRichPresence ? RICH_PRESENCE_PING_FREQUENCY : NO_RICH_PRESENCE_PING_FREQUENCY;
			if (static_cast<s32>(time_now_d() - s_last_ping_time) >= ping_frequency)
				SendPing();
		}
	}
}

void Achievements::ProcessPendingHTTPRequests()
{
#ifdef WITH_RAINTEGRATION
	if (IsUsingRAIntegration())
		return;
#endif

	s_http_downloader->PollRequests();
}

/*
bool Achievements::DoState(PointerWrap &pw)
{
	auto sw = pw.Section("Achievements", 1);
	if (!sw) {
		// Save state is missing the section.
		// Reset the runtime.
#ifdef WITH_RAINTEGRATION
		if (IsUsingRAIntegration())
			RA_OnReset();
		else
			rc_runtime_reset(&s_rcheevos_runtime);
#else
		rc_runtime_reset(&s_rcheevos_runtime);
#endif

		return;
	}

	// if we're inactive, we still need to skip the data (if any)
	if (!s_active)
	{
		u32 data_size = 0;
		sw.Do(&data_size);
		if (data_size > 0)
			sw.SkipBytes(data_size);

		return !sw.HasError();
	}

	std::unique_lock lock(s_achievements_mutex);

	if (sw.IsReading())
	{
		// if we're active, make sure we've downloaded and activated all the achievements
		// before deserializing, otherwise that state's going to get lost.
		if (!IsUsingRAIntegration() && s_http_downloader->HasAnyRequests())
		{
			Host::DisplayLoadingScreen("Downloading achievements data...");
			s_http_downloader->WaitForAllRequests();
		}

		u32 data_size = 0;
		sw.Do(&data_size);
		if (data_size == 0)
		{
			// reset runtime, no data (state might've been created without cheevos)
			DEBUG_LOG(ACHIEVEMENTS, "State is missing cheevos data, resetting runtime");
#ifdef WITH_RAINTEGRATION
			if (IsUsingRAIntegration())
				RA_OnReset();
			else
				rc_runtime_reset(&s_rcheevos_runtime);
#else
			rc_runtime_reset(&s_rcheevos_runtime);
#endif

			return !sw.HasError();
		}

		const std::unique_ptr<u8[]> data(new u8[data_size]);
		sw.DoBytes(data.get(), data_size);
		if (sw.HasError())
			return false;

#ifdef WITH_RAINTEGRATION
		if (IsUsingRAIntegration())
		{
			RA_RestoreState(reinterpret_cast<const char *>(data.get()));
		} else
		{
			const int result = rc_runtime_deserialize_progress(&s_rcheevos_runtime, data.get(), nullptr);
			if (result != RC_OK)
			{
				WARN_LOG(ACHIEVEMENTS, "Failed to deserialize cheevos state (%d), resetting", result);
				rc_runtime_reset(&s_rcheevos_runtime);
			}
		}
#endif

		return true;
	} else
	{
		u32 data_size;
		std::unique_ptr<u8[]> data;

#ifdef WITH_RAINTEGRATION
		if (IsUsingRAIntegration())
		{
			const int size = RA_CaptureState(nullptr, 0);

			data_size = (size >= 0) ? static_cast<u32>(size) : 0;
			data = std::unique_ptr<u8[]>(new u8[data_size]);

			const int result = RA_CaptureState(reinterpret_cast<char *>(data.get()), static_cast<int>(data_size));
			if (result != static_cast<int>(data_size))
			{
				Log_WarningPrint("Failed to serialize cheevos state from RAIntegration.");
				data_size = 0;
			}
		} else
#endif
		{
			// internally this happens twice.. not great.
			const int size = rc_runtime_progress_size(&s_rcheevos_runtime, nullptr);

			data_size = (size >= 0) ? static_cast<u32>(size) : 0;
			data = std::unique_ptr<u8[]>(new u8[data_size]);

			const int result = rc_runtime_serialize_progress(data.get(), &s_rcheevos_runtime, nullptr);
			if (result != RC_OK)
			{
				// set data to zero, effectively serializing nothing
				WARN_LOG(ACHIEVEMENTS, "Failed to serialize cheevos state (%d)", result);
				data_size = 0;
			}
		}

		sw.Do(&data_size);
		if (data_size > 0)
			sw.DoBytes(data.get(), data_size);

		return !sw.HasError();
	}
}
*/

bool Achievements::SafeHasAchievementsOrLeaderboards()
{
	std::unique_lock lock(s_achievements_mutex);
	return !s_achievements.empty() || !s_leaderboards.empty();
}

const std::string &Achievements::GetUsername()
{
	return s_username;
}

const std::string &Achievements::GetRichPresenceString()
{
	return s_rich_presence_string;
}

void Achievements::LoginCallback(s32 status_code, std::string content_type, Common::HTTPDownloader::Request::Data data)
{
	std::unique_lock lock(s_achievements_mutex);

	RAPIResponse<rc_api_login_response_t, rc_api_process_login_response, rc_api_destroy_login_response> response(
		status_code, data);
	if (!response || !response.username || !response.api_token)
	{
		FormattedError("Login failed. Please check your user name and password, and try again.");
		return;
	}

	std::string username(response.username);
	std::string api_token(response.api_token);

	// save to config
	g_Config.sAchievementsUserName = username;
	g_Config.sAchievementsLoginTimestamp = StringFromFormat("%llu", (unsigned long long)std::time(nullptr));
	NativeSaveSecret(RA_TOKEN_SECRET_NAME, api_token);

	g_Config.Save("AchievementsLogin");

	if (s_active)
	{
		s_username = std::move(username);
		s_api_token = std::move(api_token);
		s_logged_in = true;

		// If we have a game running, set it up.
		if (!s_game_hash.empty())
			SendGetGameId();
	}

	Host::OnAchievementsLoginStateChange();
}

void Achievements::LoginASyncCallback(s32 status_code, std::string content_type,
	Common::HTTPDownloader::Request::Data data)
{
	OSDCloseBackgroundProgressDialog("cheevos_async_login");

	LoginCallback(status_code, std::move(content_type), std::move(data));
}

void Achievements::SendLogin(const char *username, const char *password, Common::HTTPDownloader *http_downloader,
	Common::HTTPDownloader::Request::Callback callback)
{
	RAPIRequest<rc_api_login_request_t, rc_api_init_login_request> request;
	request.username = username;
	request.password = password;
	request.api_token = nullptr;
	request.Send(http_downloader, std::move(callback));
}

bool Achievements::LoginAsync(const char *username, const char *password)
{
	s_http_downloader->WaitForAllRequests();

	if (s_logged_in || std::strlen(username) == 0 || std::strlen(password) == 0 || IsUsingRAIntegration())
		return false;

	OSDOpenBackgroundProgressDialog("cheevos_async_login", "Logging in to RetroAchivements...", 0, 0, 0);

	SendLogin(username, password, s_http_downloader.get(), LoginASyncCallback);
	return true;
}

void Achievements::Logout()
{
	if (s_active)
	{
		std::unique_lock lock(s_achievements_mutex);
		s_http_downloader->WaitForAllRequests();
		if (s_logged_in)
		{
			ClearGameInfo();
			std::string().swap(s_username);
			std::string().swap(s_api_token);
			s_logged_in = false;
			Host::OnAchievementsLoginStateChange();
		}
	}

	// remove from config
	g_Config.sAchievementsUserName.clear();
	NativeSaveSecret(RA_TOKEN_SECRET_NAME, "");
	g_Config.sAchievementsLoginTimestamp.clear();
	g_Config.Save("Achievements logout");
}

void Achievements::DownloadImage(std::string url, std::string cache_filename)
{
	auto callback = [cache_filename](s32 status_code, std::string content_type, Common::HTTPDownloader::Request::Data data) {
		if (status_code != HTTP_OK)
			return;
		g_iconCache.InsertIcon(cache_filename, IconFormat::PNG, std::move(data));
	};

	if (g_iconCache.MarkPending(cache_filename)) {
		INFO_LOG(ACHIEVEMENTS, "Downloading image: %s (%s)", url.c_str(), cache_filename.c_str());
		s_http_downloader->CreateRequest(std::move(url), std::move(callback));
	}
}

Achievements::Statistics Achievements::GetStatistics() {
	return g_stats;
}

std::string Achievements::GetGameAchievementSummary() {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	std::string summary;
	summary = StringFromFormat(ac->T("Earned", "You have unlocked %d of %d achievements, earning %d of %d points"),
		GetUnlockedAchiementCount(), GetAchievementCount(), GetCurrentPointsForGame(), GetMaximumPointsForGame());
	if (GetLeaderboardCount() > 0 && LeaderboardsActive()) {
		summary.append("\n");
		summary.append(ac->T("Leaderboard submission is enabled"));
	}
	return summary;
}

void Achievements::DisplayAchievementSummary()
{
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	std::string title;
	if (ChallengeModeActive())
		title = s_game_title + " (" + ac->T("Challenge Mode") + ")";
	else
		title = s_game_title;

	std::string summary = GetGameAchievementSummary();

	OSDAddNotification(10.0f, title, summary, s_game_icon);

	// play info sound?
}

void Achievements::DisplayMasteredNotification()
{
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	if (!g_Config.bAchievementsNotifications)
		return;

	// TODO: Translation?
	std::string title = StringFromFormat(ac->T("Mastered %s"), s_game_title.c_str());
	std::string message = StringFromFormat(ac->T("%d achievements, %d points"), GetAchievementCount(), GetCurrentPointsForGame());

	OSDAddNotification(20.0f, title, message, s_game_icon);
	NOTICE_LOG(ACHIEVEMENTS, "%s", message.c_str());
}

void Achievements::GetUserUnlocksCallback(s32 status_code, std::string content_type,
	Common::HTTPDownloader::Request::Data data)
{
	// if (!System::IsValid())
	//	return;

	RAPIResponse<rc_api_fetch_user_unlocks_response_t, rc_api_process_fetch_user_unlocks_response,
		rc_api_destroy_fetch_user_unlocks_response>
		response(status_code, data);

	std::unique_lock lock(s_achievements_mutex);
	if (!response)
	{
		ClearGameInfo(true, false);
		return;
	}

	// flag achievements as unlocked
	for (u32 i = 0; i < response.num_achievement_ids; i++)
	{
		Achievement *cheevo = GetMutableAchievementByID(response.achievement_ids[i]);
		if (!cheevo)
		{
			ERROR_LOG(ACHIEVEMENTS, "Server returned unknown achievement %u", response.achievement_ids[i]);
			continue;
		}

		cheevo->locked = false;
	}

	// start scanning for locked achievements
	ActivateLockedAchievements();
	DisplayAchievementSummary();
	SendPlaying();
	UpdateRichPresence();
	SendPing();
	Host::OnAchievementsRefreshed();
}

void Achievements::GetUserUnlocks()
{
	RAPIRequest<rc_api_fetch_user_unlocks_request_t, rc_api_init_fetch_user_unlocks_request> request;
	request.username = s_username.c_str();
	request.api_token = s_api_token.c_str();
	request.game_id = s_game_id;
	request.hardcore = static_cast<int>(ChallengeModeActive());
	request.Send(GetUserUnlocksCallback);
}

static int ValidateAddress(unsigned address) {
	return Memory::IsValidAddress(address + PSP_MEMORY_OFFSET) ? 1 : 0;
}

void Achievements::GetPatchesCallback(s32 status_code, std::string content_type,
	Common::HTTPDownloader::Request::Data data)
{
	// if (!System::IsValid())
	// 	return;

	RAPIResponse<rc_api_fetch_game_data_response_t, rc_api_process_fetch_game_data_response,
		rc_api_destroy_fetch_game_data_response>
		response(status_code, data);

	std::unique_lock lock(s_achievements_mutex);
	ClearGameInfo();
	if (!response || !response.title)
	{
		DisableChallengeMode();
		return;
	}

	// ensure fullscreen UI is ready
	// Host::RunOnCPUThread(FullscreenUI::Initialize);

	s_game_id = response.id;
	s_game_title = response.title;

	// try for a icon. not that we really need one, PSP games have their own icons...
	if (response.image_name && std::strlen(response.image_name) > 0)
	{
		s_game_icon = g_gameIconCachePrefix + StringFromFormat("%d", s_game_id);
		if (!g_iconCache.Contains(s_game_icon))
		{
			RAPIRequest<rc_api_fetch_image_request_t, rc_api_init_fetch_image_request> request;
			request.image_name = response.image_name;
			request.image_type = RC_IMAGE_TYPE_GAME;
			request.DownloadImage(s_game_icon);
		}
	}

	// parse achievements
	for (u32 i = 0; i < response.num_achievements; i++)
	{
		const rc_api_achievement_definition_t &defn = response.achievements[i];

		// Skip local and unofficial achievements for now, unless "Test Unofficial Achievements" is enabled
		if (defn.category == RC_ACHIEVEMENT_CATEGORY_UNOFFICIAL)
		{
			if (!IsUnofficialTestModeActive())
			{
				WARN_LOG(ACHIEVEMENTS, "Skipping unofficial achievement %u (%s)", defn.id, defn.title);
				continue;
			}
		}
		// local achievements shouldn't be in this list, but just in case?
		else if (defn.category != RC_ACHIEVEMENT_CATEGORY_CORE)
		{
			continue;
		}

		if (GetMutableAchievementByID(defn.id))
		{
			ERROR_LOG(ACHIEVEMENTS, "Achievement %u already exists", defn.id);
			continue;
		}

		if (!defn.definition || !defn.title || !defn.description || !defn.badge_name)
		{
			ERROR_LOG(ACHIEVEMENTS, "Incomplete achievement %u", defn.id);
			continue;
		}

		Achievement cheevo;
		cheevo.id = defn.id;
		cheevo.memaddr = defn.definition;
		cheevo.title = defn.title;
		cheevo.description = defn.description;
		cheevo.badge_name = defn.badge_name;
		cheevo.locked = true;
		cheevo.active = false;
		cheevo.primed = false;
		cheevo.points = defn.points;
		cheevo.category = static_cast<AchievementCategory>(defn.category);
		s_achievements.push_back(std::move(cheevo));
	}

	for (u32 i = 0; i < response.num_leaderboards; i++)
	{
		const rc_api_leaderboard_definition_t &defn = response.leaderboards[i];
		if (!defn.title || !defn.description || !defn.definition)
		{
			ERROR_LOG(ACHIEVEMENTS, "Incomplete achievement %u", defn.id);
			continue;
		}

		Leaderboard lboard;
		lboard.id = defn.id;
		lboard.title = defn.title;
		lboard.description = defn.description;
		lboard.format = defn.format;
		s_leaderboards.push_back(std::move(lboard));

		const int err = rc_runtime_activate_lboard(&s_rcheevos_runtime, defn.id, defn.definition, nullptr, 0);
		if (err != RC_OK)
		{
			ERROR_LOG(ACHIEVEMENTS, "Leaderboard %u memaddr parse error: %s", defn.id, rc_error_str(err));
		} else
		{
			DEBUG_LOG(ACHIEVEMENTS, "Activated leaderboard %s (%u)", defn.title, defn.id);
		}
	}

	// parse rich presence
	if (response.rich_presence_script && std::strlen(response.rich_presence_script) > 0)
	{
		const int res = rc_runtime_activate_richpresence(&s_rcheevos_runtime, response.rich_presence_script, nullptr, 0);
		if (res == RC_OK)
			s_has_rich_presence = true;
		else
			WARN_LOG(ACHIEVEMENTS, "Failed to activate rich presence: %s", rc_error_str(res));
	}

	INFO_LOG(ACHIEVEMENTS, "Game Title: %s", s_game_title.c_str());
	INFO_LOG(ACHIEVEMENTS, "Achievements: %zu", s_achievements.size());
	INFO_LOG(ACHIEVEMENTS, "Leaderboards: %zu", s_leaderboards.size());

	// We don't want to block saving/loading states when there's no achievements.
	if (s_achievements.empty() && s_leaderboards.empty())
		DisableChallengeMode();

	if (!s_achievements.empty() || s_has_rich_presence)
	{
		if (!IsTestModeActive())
		{
			GetUserUnlocks();
		} else
		{
			ActivateLockedAchievements();
			DisplayAchievementSummary();
			Host::OnAchievementsRefreshed();
		}
	} else
	{
		DisplayAchievementSummary();
	}

	if (s_achievements.empty() && s_leaderboards.empty() && !s_has_rich_presence)
	{
		ClearGameInfo();
	}

	// Hook up memory validation (doesn't seem to do much though?)
	rc_runtime_validate_addresses(&s_rcheevos_runtime, &Achievements::CheevosEventHandler, &ValidateAddress);

	// Stop the progress bar.
	OSDCloseBackgroundProgressDialog("get_patches");
}

void Achievements::GetLbInfoCallback(s32 status_code, std::string content_type,
	Common::HTTPDownloader::Request::Data data)
{
	// if (!System::IsValid())
	// 	return;

	RAPIResponse<rc_api_fetch_leaderboard_info_response_t, rc_api_process_fetch_leaderboard_info_response,
		rc_api_destroy_fetch_leaderboard_info_response>
		response(status_code, data);
	if (!response)
		return;

	std::unique_lock lock(s_achievements_mutex);
	if (response.id != s_last_queried_lboard)
	{
		// User has already requested another leaderboard, drop this data
		return;
	}

	const Leaderboard *leaderboard = GetLeaderboardByID(response.id);
	if (!leaderboard)
	{
		ERROR_LOG(ACHIEVEMENTS, "Attempting to list unknown leaderboard %u", response.id);
		return;
	}

	s_lboard_entries = std::vector<Achievements::LeaderboardEntry>();
	for (u32 i = 0; i < response.num_entries; i++)
	{
		const rc_api_lboard_info_entry_t &entry = response.entries[i];
		if (!entry.username)
			continue;

		char score[128];
		rc_runtime_format_lboard_value(score, sizeof(score), entry.score, leaderboard->format);

		LeaderboardEntry lbe;
		lbe.user = entry.username;
		lbe.rank = entry.rank;
		lbe.submitted = entry.submitted;
		lbe.formatted_score = score;
		lbe.is_self = lbe.user == s_username;

		s_lboard_entries->push_back(std::move(lbe));
	}
}

void Achievements::GetPatches(u32 game_id)
{
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	OSDOpenBackgroundProgressDialog("get_patches", ac->T("Syncing achievements data..."), 0, 0, 0);

	RAPIRequest<rc_api_fetch_game_data_request_t, rc_api_init_fetch_game_data_request> request;
	request.username = s_username.c_str();
	request.api_token = s_api_token.c_str();
	request.game_id = game_id;
	request.Send(GetPatchesCallback);
}

// File reader that can handle Path (and thus Android storage and stuff).
static void *ac_open(const char *utf8Path) {
	Path path(utf8Path);
	FILE *f = File::OpenCFile(path, "rb");
	return (void *)f;
}

static void ac_seek(void *file_handle, int64_t offset, int origin) {
	fseek((FILE *)file_handle, offset, origin);
}

static int64_t ac_tell(void *file_handle) {
	return ftell((FILE *)file_handle);
}

static size_t ac_read(void *file_handle, void *buffer, size_t requested_bytes) {
	return fread(buffer, 1, requested_bytes, (FILE *)file_handle);
}

static void ac_close(void *file_handle) {
	fclose((FILE *)file_handle);
}

std::string Achievements::GetGameHash(const Path &path)
{
	// According to https://docs.retroachievements.org/Game-Identification/, we should simply
	// concatenate param.sfo and eboot.bin, and hash the result, to obtain the game hash.

	// UNFORTUNATELY, it's borked. Turns out that retroarch's rc_hash_cd_file is broken and will read
	// outside the last sector in every case. Doubly unfortunately, all the hashes on retroachievements
	// are generated like that. Oh well.

	// We will need to reimplement it properly (hash some zeroes I guess, below) to handle file types
	// that the cdreader can't handle (or we make a custom cdreader) but for now we just return orig_hash_str.

	rc_hash_filereader rc_filereader;
	rc_filereader.open = &ac_open;
	rc_filereader.seek = &ac_seek;
	rc_filereader.tell = &ac_tell;
	rc_filereader.read = &ac_read;
	rc_filereader.close = &ac_close;

	rc_hash_init_custom_filereader(&rc_filereader);
	rc_hash_init_default_cdreader();

	char orig_hash_str[33]{};
	std::string ppath = path.ToString();

	if (0 == rc_hash_generate_from_file(orig_hash_str, RC_CONSOLE_PSP, ppath.c_str())) {
		ERROR_LOG(ACHIEVEMENTS, "Failed to generate hash from file: %s", ppath.c_str());
		return "";
	}

	return std::string(orig_hash_str);
}

void Achievements::GetGameIdCallback(s32 status_code, std::string content_type,
	Common::HTTPDownloader::Request::Data data)
{
	// if (!System::IsValid())
	// 	return;

	RAPIResponse<rc_api_resolve_hash_response_t, rc_api_process_resolve_hash_response,
		rc_api_destroy_resolve_hash_response>
		response(status_code, data);
	if (!response)
		return;

	const u32 game_id = response.game_id;
	NOTICE_LOG(ACHIEVEMENTS, "Server returned GameID %u", game_id);
	if (game_id == 0)
	{
		auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
		// We don't want to block saving/loading states when there's no achievements.
		OSDAddNotification(4.0f, ac->T("RetroAchievements are not available for this game"), "", "");
		DisableChallengeMode();
		return;
	}

	GetPatches(game_id);
}

void Achievements::LeftGame() {
	// Should just uninitialize
}

void Achievements::GameChanged(const Path &path)
{
	if (!IsActive() || s_game_path == path)
		return;

	std::string game_hash;

	game_hash = GetGameHash(path);
	if (s_game_hash == game_hash)
	{
		// only the path has changed - different format/save state/etc.
		INFO_LOG(ACHIEVEMENTS, "Detected path change from '%s' to '%s'", s_game_path.c_str(), path.c_str());
		s_game_path = path;
		return;
	}

	if (!IsUsingRAIntegration() && s_http_downloader->HasAnyRequests())
	{
		s_http_downloader->WaitForAllRequests();
	}

	std::unique_lock lock(s_achievements_mutex);
	if (!IsUsingRAIntegration())
		s_http_downloader->WaitForAllRequests();

	ClearGameInfo();
	ClearGameHash();
	s_game_path = path;
	s_game_hash = std::move(game_hash);

#ifdef WITH_RAINTEGRATION
	if (IsUsingRAIntegration())
	{
		RAIntegration::GameChanged();
		return;
	}
#endif

	if (s_game_hash.empty())
	{
		// when we're booting the bios, this will fail
		if (!s_game_path.empty())
		{
			OSDAddErrorMessage("retroachievements_disc_read_failed",
				"Failed to read executable from disc. Achievements disabled.", 10.0f);
		}

		DisableChallengeMode();
		return;
	}

	if (IsLoggedIn())
		SendGetGameId();
}

void Achievements::SendGetGameId()
{
	RAPIRequest<rc_api_resolve_hash_request_t, rc_api_init_resolve_hash_request> request;
	request.username = s_username.c_str();
	request.api_token = s_api_token.c_str();
	request.game_hash = s_game_hash.c_str();
	request.Send(GetGameIdCallback);
}

void Achievements::SendPlayingCallback(s32 status_code, std::string content_type,
	Common::HTTPDownloader::Request::Data data)
{
	// if (!System::IsValid())
	// 	return;

	RAPIResponse<rc_api_start_session_response_t, rc_api_process_start_session_response,
		rc_api_destroy_start_session_response>
		response(status_code, data);
	if (!response)
		return;

	INFO_LOG(ACHIEVEMENTS, "Playing game updated to %u (%s)", s_game_id, s_game_title.c_str());
}

void Achievements::SendPlaying()
{
	if (!HasActiveGame())
		return;

	RAPIRequest<rc_api_start_session_request_t, rc_api_init_start_session_request> request;
	request.username = s_username.c_str();
	request.api_token = s_api_token.c_str();
	request.game_id = s_game_id;
	request.Send(SendPlayingCallback);
}

void Achievements::UpdateRichPresence()
{
	if (!s_has_rich_presence)
		return;

	char buffer[512];
	const int res =
		rc_runtime_get_richpresence(&s_rcheevos_runtime, buffer, sizeof(buffer), PeekMemory, nullptr, nullptr);
	if (res <= 0)
	{
		const bool had_rich_presence = !s_rich_presence_string.empty();
		s_rich_presence_string.clear();
		if (had_rich_presence)
			Host::OnAchievementsRefreshed();

		return;
	}

	std::unique_lock lock(s_achievements_mutex);
	if (s_rich_presence_string == buffer)
		return;

	s_rich_presence_string.assign(buffer);
	Host::OnAchievementsRefreshed();
}

void Achievements::SendPingCallback(s32 status_code, std::string content_type,
	Common::HTTPDownloader::Request::Data data)
{
	// if (!System::IsValid())
	// 	return;

	RAPIResponse<rc_api_ping_response_t, rc_api_process_ping_response, rc_api_destroy_ping_response> response(status_code,
		data);
}

void Achievements::SendPing()
{
	if (!HasActiveGame())
		return;

	s_last_ping_time = time_now_d();

	RAPIRequest<rc_api_ping_request_t, rc_api_init_ping_request> request;
	request.api_token = s_api_token.c_str();
	request.username = s_username.c_str();
	request.game_id = s_game_id;
	request.rich_presence = s_rich_presence_string.c_str();
	request.Send(SendPingCallback);
}

const std::string &Achievements::GetGameTitle()
{
	return s_game_title;
}

const std::string &Achievements::GetGameIcon()
{
	return s_game_icon;
}

bool Achievements::EnumerateAchievements(std::function<bool(const Achievement &)> callback)
{
	for (const Achievement &cheevo : s_achievements)
	{
		if (!callback(cheevo))
			return false;
	}

	return true;
}

u32 Achievements::GetUnlockedAchiementCount()
{
	u32 count = 0;
	for (const Achievement &cheevo : s_achievements)
	{
		if (!cheevo.locked)
			count++;
	}

	return count;
}

u32 Achievements::GetAchievementCount()
{
	return static_cast<u32>(s_achievements.size());
}

u32 Achievements::GetMaximumPointsForGame()
{
	u32 points = 0;
	for (const Achievement &cheevo : s_achievements)
		points += cheevo.points;

	return points;
}

u32 Achievements::GetCurrentPointsForGame()
{
	u32 points = 0;
	for (const Achievement &cheevo : s_achievements)
	{
		if (!cheevo.locked)
			points += cheevo.points;
	}

	return points;
}

bool Achievements::EnumerateLeaderboards(std::function<bool(const Leaderboard &)> callback)
{
	for (const Leaderboard &lboard : s_leaderboards)
	{
		if (!callback(lboard))
			return false;
	}

	return true;
}

std::optional<bool> Achievements::TryEnumerateLeaderboardEntries(u32 id,
	std::function<bool(const LeaderboardEntry &)> callback)
{
	if (id == s_last_queried_lboard)
	{
		if (s_lboard_entries)
		{
			for (const LeaderboardEntry &entry : *s_lboard_entries)
			{
				if (!callback(entry))
					return false;
			}
			return true;
		}
	} else
	{
		s_last_queried_lboard = id;
		s_lboard_entries.reset();

		// TODO: Add paging? For now, stick to defaults
		RAPIRequest<rc_api_fetch_leaderboard_info_request_t, rc_api_init_fetch_leaderboard_info_request> request;
		request.username = s_username.c_str();
		request.leaderboard_id = id;
		request.first_entry = 0;

		// Just over what a single page can store, should be a reasonable amount for now
		request.count = 15;

		request.Send(GetLbInfoCallback);
	}

	return std::nullopt;
}

const Achievements::Leaderboard *Achievements::GetLeaderboardByID(u32 id)
{
	for (const Leaderboard &lb : s_leaderboards)
	{
		if (lb.id == id)
			return &lb;
	}

	return nullptr;
}

u32 Achievements::GetLeaderboardCount()
{
	return static_cast<u32>(s_leaderboards.size());
}

bool Achievements::IsLeaderboardTimeType(const Leaderboard &leaderboard)
{
	return leaderboard.format != RC_FORMAT_SCORE && leaderboard.format != RC_FORMAT_VALUE;
}

bool Achievements::IsMastered()
{
	for (const Achievement &cheevo : s_achievements)
	{
		if (cheevo.locked)
			return false;
	}

	return true;
}

void Achievements::ActivateLockedAchievements()
{
	for (Achievement &cheevo : s_achievements)
	{
		if (cheevo.locked)
			ActivateAchievement(&cheevo);
	}
}

bool Achievements::ActivateAchievement(Achievement *achievement)
{
	if (achievement->active)
		return true;

	const int err =
		rc_runtime_activate_achievement(&s_rcheevos_runtime, achievement->id, achievement->memaddr.c_str(), nullptr, 0);
	if (err != RC_OK)
	{
		ERROR_LOG(ACHIEVEMENTS, "Achievement %u memaddr parse error: %s", achievement->id, rc_error_str(err));
		return false;
	}

	achievement->active = true;

	DEBUG_LOG(ACHIEVEMENTS, "Activated achievement %s (%u)", achievement->title.c_str(), achievement->id);
	return true;
}

void Achievements::DeactivateAchievement(Achievement *achievement)
{
	if (!achievement->active)
		return;

	rc_runtime_deactivate_achievement(&s_rcheevos_runtime, achievement->id);
	achievement->active = false;

	if (achievement->primed)
	{
		achievement->primed = false;
		s_primed_achievement_count.fetch_sub(std::memory_order_acq_rel);
	}

	DEBUG_LOG(ACHIEVEMENTS, "Deactivated achievement %s (%u)", achievement->title.c_str(), achievement->id);
}

void Achievements::UnlockAchievementCallback(s32 status_code, std::string content_type,
	Common::HTTPDownloader::Request::Data data)
{
	// if (!System::IsValid())
	// 	return;

	RAPIResponse<rc_api_award_achievement_response_t, rc_api_process_award_achievement_response,
		rc_api_destroy_award_achievement_response>
		response(status_code, data);
	if (!response)
		return;

	INFO_LOG(ACHIEVEMENTS, "Successfully unlocked achievement %u, new score %u", response.awarded_achievement_id,
		response.new_player_score);
}

void Achievements::SubmitLeaderboardCallback(s32 status_code, std::string content_type,
	Common::HTTPDownloader::Request::Data data)
{
	// if (!System::IsValid())
	// 	return;

	RAPIResponse<rc_api_submit_lboard_entry_response_t, rc_api_process_submit_lboard_entry_response,
		rc_api_destroy_submit_lboard_entry_response>
		response(status_code, data);
	if (!response)
		return;

	// Force the next leaderboard query to repopulate everything, just in case the user wants to see their new score
	s_last_queried_lboard = 0;

	// RA API doesn't send us the leaderboard ID back.. hopefully we don't submit two at once :/
	if (s_submitting_lboard_id == 0)
		return;

	const Leaderboard *lb = GetLeaderboardByID(std::exchange(s_submitting_lboard_id, 0u));
	if (!lb || !g_Config.bAchievementsNotifications)
		return;

	char submitted_score[128];
	char best_score[128];
	rc_runtime_format_lboard_value(submitted_score, sizeof(submitted_score), response.submitted_score, lb->format);
	rc_runtime_format_lboard_value(best_score, sizeof(best_score), response.best_score, lb->format);

	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
	const char *formatString = ac->T("Submitted Score");
	std::string summary = StringFromFormat(formatString,
		submitted_score, best_score, response.new_rank, response.num_entries);

	OSDAddNotification(10.0f, lb->title, std::move(summary), s_game_icon);

	// Technically not going through the resource API, but since we're passing this to something else, we can't.
	if (g_Config.bAchievementsSoundEffects)
		UI::PlayUISound(LBSUBMIT_SOUND_NAME);
}

void Achievements::UnlockAchievement(u32 achievement_id, bool add_notification /* = true*/)
{
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	std::unique_lock lock(s_achievements_mutex);

	Achievement *achievement = GetMutableAchievementByID(achievement_id);
	if (!achievement)
	{
		ERROR_LOG(ACHIEVEMENTS, "Attempting to unlock unknown achievement %u", achievement_id);
		return;
	} else if (!achievement->locked)
	{
		WARN_LOG(ACHIEVEMENTS, "Achievement %u for game %u is already unlocked", achievement_id, s_game_id);
		return;
	}

	achievement->locked = false;
	DeactivateAchievement(achievement);

	INFO_LOG(ACHIEVEMENTS, "Achievement %s (%u) for game %u unlocked", achievement->title.c_str(), achievement_id, s_game_id);

	if (g_Config.bAchievementsNotifications)
	{
		std::string title;
		switch (achievement->category)
		{
		case AchievementCategory::Local:
			title = achievement->title + " (" + ac->T("Local") + ")";
			break;
		case AchievementCategory::Unofficial:
			title = achievement->title + " (" + ac->T("Unofficial") + ")";
			break;
		case AchievementCategory::Core:
		default:
			title = achievement->title;
			break;
		}

		OSDAddNotification(15.0f, std::move(title), achievement->description,
			GetAchievementBadgePath(*achievement));
	}

	if (g_Config.bAchievementsSoundEffects)
		UI::PlayUISound(UNLOCK_SOUND_NAME);

	if (IsMastered())
		DisplayMasteredNotification();

	if (IsTestModeActive())
	{
		WARN_LOG(ACHIEVEMENTS, "Skipping sending achievement %u unlock to server because of test mode.", achievement_id);
		return;
	}

	if (achievement->category != AchievementCategory::Core)
	{
		WARN_LOG(ACHIEVEMENTS, "Skipping sending achievement %u unlock to server because it's not from the core set.",
			achievement_id);
		return;
	}

	RAPIRequest<rc_api_award_achievement_request_t, rc_api_init_award_achievement_request> request;
	request.username = s_username.c_str();
	request.api_token = s_api_token.c_str();
	request.game_hash = s_game_hash.c_str();
	request.achievement_id = achievement_id;
	request.hardcore = static_cast<int>(ChallengeModeActive());
	request.Send(UnlockAchievementCallback);
}

void Achievements::SubmitLeaderboard(u32 leaderboard_id, int value)
{
	if (IsTestModeActive())
	{
		WARN_LOG(ACHIEVEMENTS, "Skipping sending leaderboard %u result to server because of test mode.", leaderboard_id);
		return;
	}

	if (!ChallengeModeActive())
	{
		WARN_LOG(ACHIEVEMENTS, "Skipping sending leaderboard %u result to server because Challenge mode is off.", leaderboard_id);
		return;
	}

	if (!LeaderboardsActive())
	{
		WARN_LOG(ACHIEVEMENTS, "Skipping sending leaderboard %u result to server because leaderboards are disabled.", leaderboard_id);
		return;
	}

	std::unique_lock lock(s_achievements_mutex);

	s_submitting_lboard_id = leaderboard_id;

	RAPIRequest<rc_api_submit_lboard_entry_request_t, rc_api_init_submit_lboard_entry_request> request;
	request.username = s_username.c_str();
	request.api_token = s_api_token.c_str();
	request.game_hash = s_game_hash.c_str();
	request.leaderboard_id = leaderboard_id;
	request.score = value;
	request.Send(SubmitLeaderboardCallback);
}

void Achievements::AchievementPrimed(u32 achievement_id)
{
	std::unique_lock lock(s_achievements_mutex);
	Achievement *achievement = GetMutableAchievementByID(achievement_id);
	if (!achievement || achievement->primed)
		return;

	achievement->primed = true;
	s_primed_achievement_count.fetch_add(std::memory_order_acq_rel);
}

void Achievements::AchievementUnprimed(u32 achievement_id)
{
	std::unique_lock lock(s_achievements_mutex);
	Achievement *achievement = GetMutableAchievementByID(achievement_id);
	if (!achievement || !achievement->primed)
		return;

	achievement->primed = false;
	s_primed_achievement_count.fetch_sub(std::memory_order_acq_rel);
}

void Achievements::AchievementDisabled(u32 achievement_id)
{
	std::unique_lock lock(s_achievements_mutex);
	Achievement *achievement = GetMutableAchievementByID(achievement_id);
	if (!achievement)
		return;

	// Have not seen this trigger yet, despite games doing bad memory accesses.
	INFO_LOG(ACHIEVEMENTS, "Achievement disabled due to invalid memory access: %s", achievement->title.c_str());
	achievement->disabled = true;
}

std::pair<u32, u32> Achievements::GetAchievementProgress(const Achievement &achievement)
{
	std::pair<u32, u32> result;
	rc_runtime_get_achievement_measured(&s_rcheevos_runtime, achievement.id, &result.first, &result.second);
	return result;
}

std::string Achievements::GetAchievementProgressText(const Achievement &achievement)
{
	char buf[256];
	rc_runtime_format_achievement_measured(&s_rcheevos_runtime, achievement.id, buf, std::size(buf));
	return buf;
}

// Note that this returns an g_iconCache key, rather than an actual filename. So look up your image there.
std::string Achievements::GetAchievementBadgePath(const Achievement &achievement, bool download_if_missing, bool force_unlocked_icon)
{
	const bool use_locked = (achievement.locked && !force_unlocked_icon);

	std::string badge_path = g_iconCachePrefix + achievement.badge_name + std::string(use_locked ? "_lock" : "");
	if (g_iconCache.Contains(badge_path)) {
		return badge_path;
	}

	// need to download it
	if (download_if_missing) {
		RAPIRequest<rc_api_fetch_image_request_t, rc_api_init_fetch_image_request> request;
		request.image_name = achievement.badge_name.c_str();
		request.image_type = use_locked ? RC_IMAGE_TYPE_ACHIEVEMENT_LOCKED : RC_IMAGE_TYPE_ACHIEVEMENT;
		request.DownloadImage(badge_path);
	}

	return badge_path;
}

std::string Achievements::GetAchievementBadgeURL(const Achievement &achievement)
{
	RAPIRequest<rc_api_fetch_image_request_t, rc_api_init_fetch_image_request> request;
	request.image_name = achievement.badge_name.c_str();
	request.image_type = achievement.locked ? RC_IMAGE_TYPE_ACHIEVEMENT_LOCKED : RC_IMAGE_TYPE_ACHIEVEMENT;
	return request.GetURL();
}

u32 Achievements::GetPrimedAchievementCount()
{
	// Relaxed is fine here, worst that happens is we draw the triggers one frame late.
	return s_primed_achievement_count.load(std::memory_order_relaxed);
}

void Achievements::CheevosEventHandler(const rc_runtime_event_t *runtime_event)
{
	static const char *events[] = { "RC_RUNTIME_EVENT_ACHIEVEMENT_ACTIVATED", "RC_RUNTIME_EVENT_ACHIEVEMENT_PAUSED",
								   "RC_RUNTIME_EVENT_ACHIEVEMENT_RESET",     "RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED",
								   "RC_RUNTIME_EVENT_ACHIEVEMENT_PRIMED",    "RC_RUNTIME_EVENT_LBOARD_STARTED",
								   "RC_RUNTIME_EVENT_LBOARD_CANCELED",       "RC_RUNTIME_EVENT_LBOARD_UPDATED",
								   "RC_RUNTIME_EVENT_LBOARD_TRIGGERED",      "RC_RUNTIME_EVENT_ACHIEVEMENT_DISABLED",
								   "RC_RUNTIME_EVENT_LBOARD_DISABLED" };
	const char *event_text =
		((unsigned)runtime_event->type >= ARRAY_SIZE(events)) ? "unknown" : events[(unsigned)runtime_event->type];
	DEBUG_LOG(ACHIEVEMENTS, "Cheevos Event %s for %u", event_text, runtime_event->id);

	switch (runtime_event->type)
	{
	case RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED:
		UnlockAchievement(runtime_event->id);
		break;

	case RC_RUNTIME_EVENT_ACHIEVEMENT_PRIMED:
		AchievementPrimed(runtime_event->id);
		break;

	case RC_RUNTIME_EVENT_ACHIEVEMENT_UNPRIMED:
		AchievementUnprimed(runtime_event->id);
		break;

	case RC_RUNTIME_EVENT_ACHIEVEMENT_DISABLED:
		AchievementDisabled(runtime_event->id);
		break;

	case RC_RUNTIME_EVENT_LBOARD_TRIGGERED:
		SubmitLeaderboard(runtime_event->id, runtime_event->value);
		break;

	default:
		break;
	}
}

unsigned Achievements::PeekMemory(unsigned address, unsigned num_bytes, void *ud) {
	// Achievements are traditionally defined relative to the base of main memory of the emulated console.
	// This is some kind of RetroArch-related legacy. In the PSP's case, this is simply a straight offset of 0x08000000.
	address += PSP_MEMORY_OFFSET;

	if (!Memory::IsValidAddress(address)) {
		// Some achievement packs are really, really spammy.
		// So we'll just count the bad accesses.
		g_stats.badMemoryAccessCount++;

		if (g_Config.bAchievementsLogBadMemReads) {
			WARN_LOG(G3D, "RetroAchievements PeekMemory: Bad address %08x (%d bytes)", address, num_bytes);
		}
		return 0;
	}

	switch (num_bytes) {
	case 1: return Memory::ReadUnchecked_U8(address);
	case 2: return Memory::ReadUnchecked_U16(address);
	case 4: return Memory::ReadUnchecked_U32(address);
	default:
		return 0;
	}
}

#ifdef WITH_RAINTEGRATION

#include "RA_Consoles.h"

namespace Achievements::RAIntegration {
static void InitializeRAIntegration(void *main_window_handle);

static int RACallbackIsActive();
static void RACallbackCauseUnpause();
static void RACallbackCausePause();
static void RACallbackRebuildMenu();
static void RACallbackEstimateTitle(char *buf);
static void RACallbackResetEmulator();
static void RACallbackLoadROM(const char *unused);
static unsigned char RACallbackReadMemory(unsigned int address);
static unsigned int RACallbackReadMemoryBlock(unsigned int nAddress, unsigned char *pBuffer, unsigned int nBytes);
static void RACallbackWriteMemory(unsigned int address, unsigned char value);

static bool s_raintegration_initialized = false;
} // namespace Achievements::RAIntegration

void Achievements::SwitchToRAIntegration()
{
	s_using_raintegration = true;
	s_active = true;

	// Not strictly the case, but just in case we gate anything by IsLoggedIn().
	s_logged_in = true;
}

void Achievements::RAIntegration::InitializeRAIntegration(void *main_window_handle)
{
	RA_InitClient((HWND)main_window_handle, "DuckStation", g_scm_tag_str);
	RA_SetUserAgentDetail(Achievements::GetUserAgent().c_str());

	RA_InstallSharedFunctions(RACallbackIsActive, RACallbackCauseUnpause, RACallbackCausePause, RACallbackRebuildMenu,
		RACallbackEstimateTitle, RACallbackResetEmulator, RACallbackLoadROM);
	RA_SetConsoleID(PlayStation);

	// Apparently this has to be done early, or the memory inspector doesn't work.
	// That's a bit unfortunate, because the RAM size can vary between games, and depending on the option.
	RA_InstallMemoryBank(0, RACallbackReadMemory, RACallbackWriteMemory, Bus::RAM_2MB_SIZE);
	RA_InstallMemoryBankBlockReader(0, RACallbackReadMemoryBlock);

	// Fire off a login anyway. Saves going into the menu and doing it.
	RA_AttemptLogin(0);

	s_raintegration_initialized = true;

	// this is pretty lame, but we may as well persist until we exit anyway
	std::atexit(RA_Shutdown);
}

void Achievements::RAIntegration::MainWindowChanged(void *new_handle)
{
	if (s_raintegration_initialized)
	{
		RA_UpdateHWnd((HWND)new_handle);
		return;
	}

	InitializeRAIntegration(new_handle);
}

void Achievements::RAIntegration::GameChanged()
{
	s_game_id = s_game_hash.empty() ? 0 : RA_IdentifyHash(s_game_hash.c_str());
	RA_ActivateGame(s_game_id);
}

std::vector<std::tuple<int, std::string, bool>> Achievements::RAIntegration::GetMenuItems()
{
	std::array<RA_MenuItem, 64> items;
	const int num_items = RA_GetPopupMenuItems(items.data());

	std::vector<std::tuple<int, std::string, bool>> ret;
	ret.reserve(static_cast<u32>(num_items));

	for (int i = 0; i < num_items; i++)
	{
		const RA_MenuItem &it = items[i];
		if (!it.sLabel)
			ret.emplace_back(0, std::string(), false);
		else
			ret.emplace_back(static_cast<int>(it.nID), StringUtil::WideStringToUTF8String(it.sLabel), it.bChecked);
	}

	return ret;
}

void Achievements::RAIntegration::ActivateMenuItem(int item)
{
	RA_InvokeDialog(item);
}

int Achievements::RAIntegration::RACallbackIsActive()
{
	return static_cast<int>(HasActiveGame());
}

void Achievements::RAIntegration::RACallbackCauseUnpause()
{
	System::PauseSystem(false);
}

void Achievements::RAIntegration::RACallbackCausePause()
{
	System::PauseSystem(true);
}

void Achievements::RAIntegration::RACallbackRebuildMenu()
{
	// unused, we build the menu on demand
}

void Achievements::RAIntegration::RACallbackEstimateTitle(char *buf)
{
	StringUtil::Strlcpy(buf, System::GetGameTitle(), 256);
}

void Achievements::RAIntegration::RACallbackResetEmulator()
{
	if (System::IsValid())
		System::ResetSystem();
}

void Achievements::RAIntegration::RACallbackLoadROM(const char *unused)
{
	// unused
	UNREFERENCED_PARAMETER(unused);
}

unsigned char Achievements::RAIntegration::RACallbackReadMemory(unsigned int address)
{
	if (!System::IsValid())
		return 0;

	u8 value = 0;
	CPU::SafeReadMemoryByte(address, &value);
	return value;
}

void Achievements::RAIntegration::RACallbackWriteMemory(unsigned int address, unsigned char value)
{
	CPU::SafeWriteMemoryByte(address, value);
}

unsigned int Achievements::RAIntegration::RACallbackReadMemoryBlock(unsigned int nAddress, unsigned char *pBuffer,
	unsigned int nBytes)
{
	if (nAddress >= Bus::g_ram_size)
		return 0;

	const u32 copy_size = std::min<u32>(Bus::g_ram_size - nAddress, nBytes);
	std::memcpy(pBuffer, Bus::g_ram + nAddress, copy_size);
	return copy_size;
}

#endif
