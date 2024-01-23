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
#include <set>
#include <string>
#include <vector>
#include <mutex>

#include "ext/rcheevos/include/rcheevos.h"
#include "ext/rcheevos/include/rc_client.h"
#include "ext/rcheevos/include/rc_api_user.h"
#include "ext/rcheevos/include/rc_api_info.h"
#include "ext/rcheevos/include/rc_api_request.h"
#include "ext/rcheevos/include/rc_api_runtime.h"
#include "ext/rcheevos/include/rc_api_user.h"
#include "ext/rcheevos/include/rc_url.h"
#include "ext/rcheevos/include/rc_hash.h"
#include "ext/rcheevos/src/rhash/md5.h"

#include "ext/rapidjson/include/rapidjson/document.h"

#include "Common/Log.h"
#include "Common/File/Path.h"
#include "Common/File/FileUtil.h"
#include "Core/FileLoaders/LocalFileLoader.h"
#include "Core/FileSystems/BlockDevices.h"
#include "Common/Net/HTTPClient.h"
#include "Common/System/OSD.h"
#include "Common/System/System.h"
#include "Common/System/NativeApp.h"
#include "Common/TimeUtil.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/StringUtils.h"
#include "Common/Crypto/md5.h"
#include "Common/UI/IconCache.h"

#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/CoreParameter.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/System.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/RetroAchievements.h"

static inline const char *DeNull(const char *ptr) {
	return ptr ? ptr : "";
}

void OnAchievementsLoginStateChange() {
	System_PostUIMessage(UIMessage::ACHIEVEMENT_LOGIN_STATE_CHANGE);
}

namespace Achievements {

// It's the name of the secret, not a secret name - the value is not secret :)
static const char * const RA_TOKEN_SECRET_NAME = "retroachievements";

static Achievements::Statistics g_stats;

const std::string g_gameIconCachePrefix = "game:";
const std::string g_iconCachePrefix = "badge:";

Path s_game_path;
std::string s_game_hash;

std::set<uint32_t> g_activeChallenges;
bool g_isIdentifying = false;
bool g_isLoggingIn = false;
bool g_hasRichPresence = false;
int g_loginResult;

double g_lastLoginAttemptTime;

// rc_client implementation
static rc_client_t *g_rcClient;
static const std::string g_RAImageID = "I_RETROACHIEVEMENTS_LOGO";
constexpr double LOGIN_ATTEMPT_INTERVAL_S = 10.0;

struct FileContext {
	BlockDevice *bd;
	int64_t seekPos;
};
static BlockDevice *g_blockDevice;

#define PSP_MEMORY_OFFSET 0x08000000

static void TryLoginByToken(bool isInitialAttempt);

rc_client_t *GetClient() {
	return g_rcClient;
}

bool IsLoggedIn() {
	return rc_client_get_user_info(g_rcClient) != nullptr && !g_isLoggingIn;
}

// This is the RetroAchievements game ID, rather than the PSP game ID.
static u32 GetGameID() {
	if (!g_rcClient) {
		return 0;
	}

	const rc_client_game_t *info = rc_client_get_game_info(g_rcClient);
	if (!info) {
		return 0;
	}
	return info->id;  // 0 if not identified
}

bool EncoreModeActive() {
	if (!g_rcClient) {
		return false;
	}
	return rc_client_get_encore_mode_enabled(g_rcClient);
}

bool UnofficialEnabled() {
	if (!g_rcClient) {
		return false;
	}
	return rc_client_get_unofficial_enabled(g_rcClient);
}

bool HardcoreModeActive() {
	if (!g_rcClient) {
		return false;
	}
	// See "Enabling Hardcore" under https://github.com/RetroAchievements/rcheevos/wiki/rc_client-integration.
	return IsLoggedIn() && rc_client_get_hardcore_enabled(g_rcClient) && rc_client_is_processing_required(g_rcClient);
}

size_t GetRichPresenceMessage(char *buffer, size_t bufSize) {
	if (!IsLoggedIn() || !rc_client_has_rich_presence(g_rcClient)) {
		return (size_t)-1;
	}
	return rc_client_get_rich_presence_message(g_rcClient, buffer, bufSize);
}

bool WarnUserIfHardcoreModeActive(bool isSaveStateAction, const char *message) {
	if (!HardcoreModeActive() || (isSaveStateAction && g_Config.bAchievementsSaveStateInHardcoreMode)) {
		return false;
	}

	const char *showMessage = message;
	if (!message) {
		auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
		showMessage = ac->T("This feature is not available in Hardcore Mode");
	}

	g_OSD.Show(OSDType::MESSAGE_WARNING, showMessage, "", g_RAImageID, 3.0f);
	return true;
}

bool IsBlockingExecution() {
	if (g_isLoggingIn || g_isIdentifying) {
		// Useful for debugging race conditions.
		// INFO_LOG(ACHIEVEMENTS, "isLoggingIn: %d   isIdentifying: %d", (int)g_isLoggingIn, (int)g_isIdentifying);
	}
	return g_isLoggingIn || g_isIdentifying;
}

bool IsActive() {
	return GetGameID() != 0;
}

static uint32_t read_memory_callback(uint32_t address, uint8_t *buffer, uint32_t num_bytes, rc_client_t *client) {
	// Achievements are traditionally defined relative to the base of main memory of the emulated console.
	// This is some kind of RetroArch-related legacy. In the PSP's case, this is simply a straight offset of 0x08000000.
	uint32_t orig_address = address;
	address += PSP_MEMORY_OFFSET;

	if (!Memory::ValidSize(address, num_bytes)) {
		// Some achievement packs are really, really spammy.
		// So we'll just count the bad accesses.
		Achievements::g_stats.badMemoryAccessCount++;
		if (g_Config.bAchievementsLogBadMemReads) {
			WARN_LOG(G3D, "RetroAchievements PeekMemory: Bad address %08x (%d bytes) (%08x was passed in)", address, num_bytes, orig_address);
		}

		// This tells rcheevos that the access was bad, which should now be handled properly.
		return 0;
	}

	Memory::MemcpyUnchecked(buffer, address, num_bytes);
	return num_bytes;
}

// This is the HTTP request dispatcher that is provided to the rc_client. Whenever the client
// needs to talk to the server, it will call this function.
static void server_call_callback(const rc_api_request_t *request,
	rc_client_server_callback_t callback, void *callback_data, rc_client_t *client)
{
	// If post data is provided, we need to make a POST request, otherwise, a GET request will suffice.
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
	if (request->post_data) {
		std::shared_ptr<http::Request> download = g_DownloadManager.AsyncPostWithCallback(std::string(request->url), std::string(request->post_data), "application/x-www-form-urlencoded", http::ProgressBarMode::DELAYED, [=](http::Request &download) {
			std::string buffer;
			download.buffer().TakeAll(&buffer);
			rc_api_server_response_t response{};
			response.body = buffer.c_str();
			response.body_length = buffer.size();
			response.http_status_code = download.ResultCode();
			callback(&response, callback_data);
		}, ac->T("Contacting RetroAchievements server..."));
	} else {
		std::shared_ptr<http::Request> download = g_DownloadManager.StartDownloadWithCallback(std::string(request->url), Path(), http::ProgressBarMode::DELAYED, [=](http::Request &download) {
			std::string buffer;
			download.buffer().TakeAll(&buffer);
			rc_api_server_response_t response{};
			response.body = buffer.c_str();
			response.body_length = buffer.size();
			response.http_status_code = download.ResultCode();
			callback(&response, callback_data);
		}, ac->T("Contacting RetroAchievements server..."));
	}
}

static void log_message_callback(const char *message, const rc_client_t *client) {
	INFO_LOG(ACHIEVEMENTS, "RetroAchievements: %s", message);
}

// For detailed documentation, see https://github.com/RetroAchievements/rcheevos/wiki/rc_client_set_event_handler.
static void event_handler_callback(const rc_client_event_t *event, rc_client_t *client) {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	switch (event->type) {
	case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
		// An achievement was earned by the player. The handler should notify the player that the achievement was earned.
		g_OSD.ShowAchievementUnlocked(event->achievement->id);
		System_PostUIMessage(UIMessage::REQUEST_PLAY_SOUND, "achievement_unlocked");
		INFO_LOG(ACHIEVEMENTS, "Achievement unlocked: '%s' (%d)", event->achievement->title, event->achievement->id);
		break;

	case RC_CLIENT_EVENT_GAME_COMPLETED:
	{
		// TODO: Do some zany fireworks!

		// All achievements for the game have been earned. The handler should notify the player that the game was completed or mastered, depending on mode, hardcore or not.
		auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

		const rc_client_game_t *gameInfo = rc_client_get_game_info(g_rcClient);

		// TODO: Translation?
		std::string title = ApplySafeSubstitutions(ac->T("Mastered %1"), gameInfo->title);

		rc_client_user_game_summary_t summary;
		rc_client_get_user_game_summary(g_rcClient, &summary);

		std::string message = ApplySafeSubstitutions(ac->T("%1 achievements, %2 points"), summary.num_unlocked_achievements, summary.points_unlocked);

		g_OSD.Show(OSDType::MESSAGE_INFO, title, message, DeNull(gameInfo->badge_name), 10.0f);

		System_PostUIMessage(UIMessage::REQUEST_PLAY_SOUND, "achievement_unlocked");

		INFO_LOG(ACHIEVEMENTS, "%s", message.c_str());
		break;
	}
	case RC_CLIENT_EVENT_LEADERBOARD_STARTED:
	case RC_CLIENT_EVENT_LEADERBOARD_FAILED:
	{
		bool started = event->type == RC_CLIENT_EVENT_LEADERBOARD_STARTED;
		// A leaderboard attempt has started. The handler may show a message with the leaderboard title and /or description indicating the attempt started.
		const char *title = "";
		const char *description = "";
		// Hack around some problematic events in Burnout Legends. Hopefully this can be fixed in the backend.
		if (strlen(event->leaderboard->title) > 0) {
			title = event->leaderboard->title;
			description = event->leaderboard->description;
		} else {
			title = event->leaderboard->description;
		}
		INFO_LOG(ACHIEVEMENTS, "Attempt %s: %s", started ? "started" : "failed", title);
		g_OSD.ShowLeaderboardStartEnd(ApplySafeSubstitutions(ac->T(started ? "%1: Attempt started" : "%1: Attempt failed"), title), description, started);
		break;
	}
	case RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED:
	{
		INFO_LOG(ACHIEVEMENTS, "Leaderboard result submitted: %s", event->leaderboard->title);
		const char *title = "";
		// Hack around some problematic events in Burnout Legends. Hopefully this can be fixed in the backend.
		if (strlen(event->leaderboard->title) > 0) {
			title = event->leaderboard->title;
		} else {
			title = event->leaderboard->description;
		}
		g_OSD.ShowLeaderboardSubmitted(ApplySafeSubstitutions(ac->T("Submitted %1 for %2"), DeNull(event->leaderboard->tracker_value), title), "");
		System_PostUIMessage(UIMessage::REQUEST_PLAY_SOUND, "leaderboard_submitted");
		break;
	}
	case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW:
		INFO_LOG(ACHIEVEMENTS, "Challenge indicator show: %s", event->achievement->title);
		g_OSD.ShowChallengeIndicator(event->achievement->id, true);
		g_activeChallenges.insert(event->achievement->id);
		// A challenge achievement has become active. The handler should show a small version of the achievement icon
		// to indicate the challenge is active.
		break;
	case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_HIDE:
		INFO_LOG(ACHIEVEMENTS, "Challenge indicator hide: %s", event->achievement->title);
		g_OSD.ShowChallengeIndicator(event->achievement->id, false);
		g_activeChallenges.erase(event->achievement->id);
		// The handler should hide the small version of the achievement icon that was shown by the corresponding RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW event.
		break;
	case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW:
		INFO_LOG(ACHIEVEMENTS, "Progress indicator show: %s, progress: '%s' (%f)", event->achievement->title, event->achievement->measured_progress, event->achievement->measured_percent);
		// An achievement that tracks progress has changed the amount of progress that has been made.
		// The handler should show a small version of the achievement icon along with the achievement->measured_progress text (for two seconds).
		// Only one progress indicator should be shown at a time.
		// If a progress indicator is already visible, it should be updated with the new icon and text, and the two second timer should be restarted.
		g_OSD.ShowAchievementProgress(event->achievement->id, true);
		break;
	case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE:
		INFO_LOG(ACHIEVEMENTS, "Progress indicator update: %s, progress: '%s' (%f)", event->achievement->title, event->achievement->measured_progress, event->achievement->measured_percent);
		g_OSD.ShowAchievementProgress(event->achievement->id, true);
		break;
	case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_HIDE:
		INFO_LOG(ACHIEVEMENTS, "Progress indicator hide");
		// An achievement that tracks progress has changed the amount of progress that has been made.
		// The handler should show a small version of the achievement icon along with the achievement->measured_progress text (for two seconds).
		// Only one progress indicator should be shown at a time.
		// If a progress indicator is already visible, it should be updated with the new icon and text, and the two second timer should be restarted.
		g_OSD.ShowAchievementProgress(0, false);
		break;
	case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_SHOW:
		INFO_LOG(ACHIEVEMENTS, "Leaderboard tracker show: '%s' (id %d)", event->leaderboard_tracker->display, event->leaderboard_tracker->id);
		// A leaderboard_tracker has become active. The handler should show the tracker text on screen.
		// Multiple active leaderboards may share a single tracker if they have the same definition and value.
		// As such, the leaderboard tracker IDs are unique amongst the leaderboard trackers, and have no correlation to the active leaderboard(s).
		// Use event->leaderboard_tracker->id for uniqueness checks, and display event->leaderboard_tracker->display (string)
		g_OSD.ShowLeaderboardTracker(event->leaderboard_tracker->id, event->leaderboard_tracker->display, true);
		break;
	case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_HIDE:
		// A leaderboard_tracker has become inactive. The handler should hide the tracker text from the screen.
		INFO_LOG(ACHIEVEMENTS, "Leaderboard tracker hide: '%s' (id %d)", event->leaderboard_tracker->display, event->leaderboard_tracker->id);
		g_OSD.ShowLeaderboardTracker(event->leaderboard_tracker->id, nullptr, false);
		break;
	case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_UPDATE:
		// A leaderboard_tracker value has been updated. The handler should update the tracker text on the screen.
		INFO_LOG(ACHIEVEMENTS, "Leaderboard tracker update: '%s' (id %d)", event->leaderboard_tracker->display, event->leaderboard_tracker->id);
		g_OSD.ShowLeaderboardTracker(event->leaderboard_tracker->id, event->leaderboard_tracker->display, true);
		break;
	case RC_CLIENT_EVENT_RESET:
		WARN_LOG(ACHIEVEMENTS, "Resetting game due to achievement setting change!");
		// Hardcore mode was enabled, or something else that forces a game reset.
		System_PostUIMessage(UIMessage::REQUEST_GAME_RESET);
		break;
	case RC_CLIENT_EVENT_SERVER_ERROR:
		ERROR_LOG(ACHIEVEMENTS, "Server error: %s: %s", event->server_error->api, event->server_error->error_message);
		g_OSD.Show(OSDType::MESSAGE_ERROR, "Server error", "", g_RAImageID);
		break;
	default:
		WARN_LOG(ACHIEVEMENTS, "Unhandled rc_client event %d, ignoring", event->type);
		break;
	}
}

static void login_token_callback(int result, const char *error_message, rc_client_t *client, void *userdata) {
	bool isInitialAttempt = userdata != nullptr;
	switch (result) {
	case RC_OK:
	{
		INFO_LOG(ACHIEVEMENTS, "Successful login by token.");
		OnAchievementsLoginStateChange();
		if (!isInitialAttempt) {
			auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
			g_OSD.Show(OSDType::MESSAGE_SUCCESS, ac->T("Reconnected to RetroAchievements."), "", g_RAImageID);
		}
		break;
	}
	case RC_NO_RESPONSE:
	{
		if (isInitialAttempt) {
			auto di = GetI18NCategory(I18NCat::DIALOG);
			g_OSD.Show(OSDType::MESSAGE_WARNING, di->T("Failed to connect to server, check your internet connection."), "", g_RAImageID);
		}
		break;
	}
	case RC_ACCESS_DENIED:
	case RC_INVALID_CREDENTIALS:
	case RC_EXPIRED_TOKEN:
	case RC_API_FAILURE:
	case RC_INVALID_STATE:
	case RC_MISSING_VALUE:
	case RC_INVALID_JSON:
	default:
	{
		ERROR_LOG(ACHIEVEMENTS, "Callback: Failure logging in via token: %d, %s", result, error_message);
		if (isInitialAttempt) {
			auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
			g_OSD.Show(OSDType::MESSAGE_WARNING, ac->T("Failed logging in to RetroAchievements"), "", g_RAImageID);
		}
		OnAchievementsLoginStateChange();
		break;
	}
	}
	g_loginResult = result;
	g_isLoggingIn = false;
}

void Initialize() {
	if (!g_Config.bAchievementsEnable) {
		_dbg_assert_(!g_rcClient);
		INFO_LOG(ACHIEVEMENTS, "Achievements are disabled, not initializing.");
		return;
	}
	_assert_msg_(!g_rcClient, "Achievements already initialized");

	g_rcClient = rc_client_create(read_memory_callback, server_call_callback);
	if (!g_rcClient) {
		// Shouldn't happen really.
		return;
	}

	// Provide a logging function to simplify debugging
	rc_client_enable_logging(g_rcClient, RC_CLIENT_LOG_LEVEL_VERBOSE, log_message_callback);

	if (!System_GetPropertyBool(SYSPROP_SUPPORTS_HTTPS)) {
		// Disable SSL if not supported by our platform implementation.
		rc_client_set_host(g_rcClient, "http://retroachievements.org");
	}

	rc_client_set_event_handler(g_rcClient, event_handler_callback);

	rc_hash_filereader rc_filereader;
	rc_filereader.open = [](const char *utf8Path) -> void *{
		if (!g_blockDevice) {
			ERROR_LOG(ACHIEVEMENTS, "No block device");
			return nullptr;
		}

		return (void *) new FileContext{ g_blockDevice, 0 };
	};
	rc_filereader.seek = [](void *file_handle, int64_t offset, int origin) {
		FileContext *ctx = (FileContext *)file_handle;
		switch (origin) {
		case SEEK_SET: ctx->seekPos = offset; break;
		case SEEK_END: ctx->seekPos = ctx->bd->GetBlockSize() * ctx->bd->GetNumBlocks() + offset; break;
		case SEEK_CUR: ctx->seekPos += offset; break;
		default: break;
		}
	};
	rc_filereader.tell = [](void *file_handle) -> int64_t {
		return ((FileContext *)file_handle)->seekPos;
	};
	rc_filereader.read = [](void *file_handle, void *buffer, size_t requested_bytes) -> size_t {
		FileContext *ctx = (FileContext *)file_handle;

		int blockSize = ctx->bd->GetBlockSize();

		int64_t offset = ctx->seekPos;
		int64_t endOffset = ctx->seekPos + requested_bytes;
		int firstBlock = offset / blockSize;
		int afterLastBlock = (endOffset + blockSize - 1) / blockSize;
		int numBlocks = afterLastBlock - firstBlock;
		// This is suboptimal, but good enough since we're not doing a lot of accesses.
		uint8_t *buf = new uint8_t[numBlocks * blockSize];
		bool success = ctx->bd->ReadBlocks(firstBlock, numBlocks, (u8 *)buf);
		if (success) {
			int64_t firstOffset = firstBlock * blockSize;
			memcpy(buffer, buf + (offset - firstOffset), requested_bytes);
			ctx->seekPos += requested_bytes;
			delete[] buf;
			return requested_bytes;
		} else {
			delete[] buf;
			ERROR_LOG(ACHIEVEMENTS, "Block device load fail");
			return 0;
		}
	};
	rc_filereader.close = [](void *file_handle) {
		FileContext *ctx = (FileContext *)file_handle;
		delete ctx->bd;
		delete ctx;
	};
	rc_hash_init_custom_filereader(&rc_filereader);
	rc_hash_init_default_cdreader();

	TryLoginByToken(true);
}

bool HasToken() {
	return !NativeLoadSecret(RA_TOKEN_SECRET_NAME).empty();
}

bool LoginProblems(std::string *errorString) {
	// TODO: Set error string.
	return g_loginResult != RC_OK;
}

static void TryLoginByToken(bool isInitialAttempt) {
	if (g_Config.sAchievementsUserName.empty()) {
		// Don't even look for a token - without a username we can't login.
		return;
	}
	std::string api_token = NativeLoadSecret(RA_TOKEN_SECRET_NAME);
	if (!api_token.empty()) {
		g_isLoggingIn = true;
		rc_client_begin_login_with_token(g_rcClient, g_Config.sAchievementsUserName.c_str(), api_token.c_str(), &login_token_callback, (void *)isInitialAttempt);
	}
}

static void login_password_callback(int result, const char *error_message, rc_client_t *client, void *userdata) {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	switch (result) {
	case RC_OK:
	{
		// Get the token and store it.
		const rc_client_user_t *user = rc_client_get_user_info(client);
		g_Config.sAchievementsUserName = user->username;
		NativeSaveSecret(RA_TOKEN_SECRET_NAME, std::string(user->token));
		OnAchievementsLoginStateChange();
		g_OSD.Show(OSDType::MESSAGE_SUCCESS, di->T("Logged in!"), "", g_RAImageID);
		break;
	}
	case RC_NO_RESPONSE:
	{
		auto di = GetI18NCategory(I18NCat::DIALOG);
		g_OSD.Show(OSDType::MESSAGE_WARNING, di->T("Failed to connect to server, check your internet connection."), "", g_RAImageID);
		break;
	}
	case RC_INVALID_STATE:
	case RC_API_FAILURE:
	case RC_MISSING_VALUE:
	case RC_INVALID_JSON:
	case RC_ACCESS_DENIED:
	case RC_INVALID_CREDENTIALS:
	case RC_EXPIRED_TOKEN:
	default:
	{
		ERROR_LOG(ACHIEVEMENTS, "Failure logging in via password: %d, %s", result, error_message);
		g_OSD.Show(OSDType::MESSAGE_WARNING, di->T("Failed to log in, check your username and password."), "", g_RAImageID);
		OnAchievementsLoginStateChange();
		break;
	}
	}

	g_OSD.RemoveProgressBar("cheevos_async_login", true, 0.1f);
	g_loginResult = RC_OK;  // For these, we don't want the "permanence" of the login-by-token failure, this prevents LoginProblems from returning true.
	g_isLoggingIn = false;
}

bool LoginAsync(const char *username, const char *password) {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	if (IsLoggedIn() || std::strlen(username) == 0 || std::strlen(password) == 0 || IsUsingRAIntegration())
		return false;

	g_OSD.SetProgressBar("cheevos_async_login", di->T("Logging in..."), 0, 0, 0, 0.0f);

	g_isLoggingIn = true;
	rc_client_begin_login_with_password(g_rcClient, username, password, &login_password_callback, nullptr);
	return true;
}

void Logout() {
	rc_client_logout(g_rcClient);
	// remove from config
	g_Config.sAchievementsUserName.clear();
	NativeSaveSecret(RA_TOKEN_SECRET_NAME, "");
	g_Config.Save("Achievements logout");
	g_activeChallenges.clear();
	g_loginResult = RC_OK;  // Allow trying again
	OnAchievementsLoginStateChange();
}

void UpdateSettings() {
	if (g_rcClient && !g_Config.bAchievementsEnable) {
		// we're done here
		Shutdown();
		return;
	}

	if (!g_rcClient && g_Config.bAchievementsEnable) {
		// we just got enabled.
		Initialize();
	}
}

bool Shutdown() {
	g_activeChallenges.clear();
	rc_client_destroy(g_rcClient);
	g_rcClient = nullptr;
	INFO_LOG(ACHIEVEMENTS, "Achievements shut down.");
	return true;
}

void ResetRuntime() {
	INFO_LOG(ACHIEVEMENTS, "Resetting rcheevos state...");
	rc_client_reset(g_rcClient);
	g_activeChallenges.clear();
}

void FrameUpdate() {
	if (!g_rcClient)
		return;
	rc_client_do_frame(g_rcClient);
}

void Idle() {
	rc_client_idle(g_rcClient);

	double now = time_now_d();

	// If failed to log in, occasionally try again while the user is at the menu.
	// Do not try if if in-game, that could get confusing.
	if (g_Config.bAchievementsEnable && GetUIState() == UISTATE_MENU && now > g_lastLoginAttemptTime + LOGIN_ATTEMPT_INTERVAL_S) {
		g_lastLoginAttemptTime = now;
		if (g_rcClient && IsLoggedIn()) {
			return;  // All good.
		}
		if (g_Config.sAchievementsUserName.empty() || g_isLoggingIn || !HasToken()) {
			// Didn't try to login yet or is in the process of logging in. Also OK.
			return;
		}

		// In this situation, there's a token, but we're not logged in. Probably disrupted internet connection
		// during startup.
		// Let's make an attempt.
		INFO_LOG(ACHIEVEMENTS, "Retrying login..");
		TryLoginByToken(false);
	}
}

void DoState(PointerWrap &p) {
	auto sw = p.Section("Achievements", 0, 1);
	if (!sw) {
		// Save state is missing the section.
		// Reset the runtime.
		if (HasAchievementsOrLeaderboards()) {
			auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
			g_OSD.Show(OSDType::MESSAGE_WARNING, ac->T("Save state loaded without achievement data"), "", g_RAImageID, 5.0f, "");
		}
		rc_client_reset(g_rcClient);
		return;
	}

	uint32_t data_size = 0;

	if (!IsActive()) {
		Do(p, data_size);
		if (p.mode == PointerWrap::MODE_READ) {
			WARN_LOG(ACHIEVEMENTS, "Save state contained achievement data, but achievements are not active. Ignore.");
		}
		p.SkipBytes(data_size);
		return;
	}

	if (p.mode == PointerWrap::MODE_MEASURE || p.mode == PointerWrap::MODE_WRITE || p.mode == PointerWrap::MODE_VERIFY || p.mode == PointerWrap::MODE_NOOP) {
		data_size = (uint32_t)(g_rcClient ? rc_client_progress_size(g_rcClient) : 0);
	}
	Do(p, data_size);

	if (data_size > 0) {
		uint8_t *buffer = new uint8_t[data_size];
		switch (p.mode) {
		case PointerWrap::MODE_NOOP:
		case PointerWrap::MODE_MEASURE:
		case PointerWrap::MODE_WRITE:
		case PointerWrap::MODE_VERIFY:
		{
			int retval = rc_client_serialize_progress(g_rcClient, buffer);
			if (retval != RC_OK) {
				ERROR_LOG(ACHIEVEMENTS, "Error %d serializing achievement data. Ignoring.", retval);
			}
			break;
		}
		default:
			break;
		}

		DoArray(p, buffer, data_size);

		switch (p.mode) {
		case PointerWrap::MODE_READ:
		{
			int retval = rc_client_deserialize_progress(g_rcClient, buffer);
			if (retval != RC_OK) {
				// TODO: What should we really do here?
				ERROR_LOG(ACHIEVEMENTS, "Error %d deserializing achievement data. Ignoring.", retval);
			}
			break;
		}
		default:
			break;
		}
		delete[] buffer;
	} else {
		if (IsActive()) {
			auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
			g_OSD.Show(OSDType::MESSAGE_WARNING, ac->T("Save state loaded without achievement data"), "", g_RAImageID, 5.0f);
		}
		rc_client_reset(g_rcClient);
	}
}

bool HasAchievementsOrLeaderboards() {
	if (!g_rcClient) {
		return false;
	}
	return IsActive();
}

void DownloadImageIfMissing(const std::string &cache_key, std::string &&url) {
	if (g_iconCache.MarkPending(cache_key)) {
		INFO_LOG(ACHIEVEMENTS, "Downloading image: %s (%s)", url.c_str(), cache_key.c_str());
		g_DownloadManager.StartDownloadWithCallback(url, Path(), http::ProgressBarMode::NONE, [cache_key](http::Request &download) {
			if (download.ResultCode() != 200)
				return;
			std::string data;
			download.buffer().TakeAll(&data);
			g_iconCache.InsertIcon(cache_key, IconFormat::PNG, std::move(data));
		});
	}
}

Statistics GetStatistics() {
	return g_stats;
}

std::string GetGameAchievementSummary() {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	rc_client_user_game_summary_t summary;
	rc_client_get_user_game_summary(g_rcClient, &summary);

	std::string summaryString;
	if (summary.num_core_achievements + summary.num_unofficial_achievements == 0) {
		summaryString = ac->T("This game has no achievements");
	} else {
		summaryString = ApplySafeSubstitutions(ac->T("Earned", "You have unlocked %1 of %2 achievements, earning %3 of %4 points"),
			summary.num_unlocked_achievements, summary.num_core_achievements + summary.num_unofficial_achievements,
			summary.points_unlocked, summary.points_core);
		if (HardcoreModeActive()) {
			summaryString.append("\n");
			summaryString.append(ac->T("Hardcore Mode"));
		}
		if (EncoreModeActive()) {
			summaryString.append("\n");
			summaryString.append(ac->T("Encore Mode"));
		}
		if (UnofficialEnabled()) {
			summaryString.append("\n");
			summaryString.append(ac->T("Unofficial achievements"));
		}
	}
	return summaryString;
}

// Can happen two ways.
void ShowNotLoggedInMessage() {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
	g_OSD.Show(OSDType::MESSAGE_ERROR, ac->T("Failed to connect to RetroAchievements. Achievements will not unlock."), "", g_RAImageID, 6.0f);
}

void identify_and_load_callback(int result, const char *error_message, rc_client_t *client, void *userdata) {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	NOTICE_LOG(ACHIEVEMENTS, "Load callback: %d (%s)", result, error_message);

	switch (result) {
	case RC_OK:
	{
		// Successful! Show a message that we're active.
		const rc_client_game_t *gameInfo = rc_client_get_game_info(client);

		char cacheId[128];
		snprintf(cacheId, sizeof(cacheId), "gi:%s", gameInfo->badge_name);

		char temp[512];
		if (RC_OK == rc_client_game_get_image_url(gameInfo, temp, sizeof(temp))) {
			Achievements::DownloadImageIfMissing(cacheId, std::string(temp));
		}
		g_OSD.Show(OSDType::MESSAGE_INFO, std::string(gameInfo->title), GetGameAchievementSummary(), cacheId, 5.0f);
		break;
	}
	case RC_NO_GAME_LOADED:
		// The current game does not support achievements.
		g_OSD.Show(OSDType::MESSAGE_INFO, ac->T("RetroAchievements are not available for this game"), "", g_RAImageID, 3.0f);
		break;
	case RC_NO_RESPONSE:
		// We lost the internet connection at some point and can't report achievements.
		ShowNotLoggedInMessage();
		break;
	default:
		// Other various errors.
		ERROR_LOG(ACHIEVEMENTS, "Failed to identify/load game: %d (%s)", result, error_message);
		g_OSD.Show(OSDType::MESSAGE_ERROR, ac->T("Failed to identify game. Achievements will not unlock."), "", g_RAImageID,  6.0f);
		break;
	}

	g_isIdentifying = false;
}

bool IsReadyToStart() {
	return !g_isLoggingIn;
}

void SetGame(const Path &path, IdentifiedFileType fileType, FileLoader *fileLoader) {
	switch (fileType) {
	case IdentifiedFileType::PSP_ISO:
	case IdentifiedFileType::PSP_ISO_NP:
		// These file types are OK.
		break;
	default:
		// Other file types are not yet supported.
		// TODO: Should we show an OSD popup here?
		WARN_LOG(ACHIEVEMENTS, "File type of '%s' is not yet compatible with RetroAchievements", path.c_str());
		return;
	}

	if (g_isLoggingIn) {
		// IsReadyToStart should have been checked the same frame, so we shouldn't be here.
		// Maybe there's a race condition possible, but don't think so.
		ERROR_LOG(ACHIEVEMENTS, "Still logging in during SetGame - shouldn't happen");
	}

	if (!g_rcClient || !IsLoggedIn()) {
		if (g_Config.bAchievementsEnable && HasToken()) {
			ShowNotLoggedInMessage();
		}
		// Nothing to do.
		return;
	}

	_dbg_assert_(!g_blockDevice);

	// TODO: Fish the block device out of the loading process somewhere else. Though, probably easier to just do it here.
	g_blockDevice = constructBlockDevice(fileLoader);
	if (!g_blockDevice) {
		ERROR_LOG(ACHIEVEMENTS, "Failed to construct block device for '%s' - can't identify", path.c_str());
		return;
	}

	// The caller should hold off on executing game code until this turns false, checking with IsBlockingExecution()
	g_isIdentifying = true;

	// Apply pre-load settings.
	rc_client_set_hardcore_enabled(g_rcClient, g_Config.bAchievementsChallengeMode ? 1 : 0);
	rc_client_set_encore_mode_enabled(g_rcClient, g_Config.bAchievementsEncoreMode ? 1 : 0);
	rc_client_set_unofficial_enabled(g_rcClient, g_Config.bAchievementsUnofficial ? 1 : 0);

	rc_client_begin_identify_and_load_game(g_rcClient, RC_CONSOLE_PSP, path.c_str(), nullptr, 0, &identify_and_load_callback, nullptr);

	// fclose above will have deleted it.
	g_blockDevice = nullptr;
}

void UnloadGame() {
	if (g_rcClient) {
		rc_client_unload_game(g_rcClient);
	}
}

void change_media_callback(int result, const char *error_message, rc_client_t *client, void *userdata) {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
	NOTICE_LOG(ACHIEVEMENTS, "Change media callback: %d (%s)", result, error_message);
	g_isIdentifying = false;

	switch (result) {
	case RC_OK:
	{
		// Successful! Later, show a message that we succeeded.
		break;
	}
	case RC_NO_GAME_LOADED:
		// The current game does not support achievements.
		g_OSD.Show(OSDType::MESSAGE_INFO, ac->T("RetroAchievements are not available for this game"), "", g_RAImageID, 3.0f);
		break;
	case RC_NO_RESPONSE:
		// We lost the internet connection at some point and can't report achievements.
		ShowNotLoggedInMessage();
		break;
	default:
		// Other various errors.
		ERROR_LOG(ACHIEVEMENTS, "Failed to identify/load game: %d (%s)", result, error_message);
		g_OSD.Show(OSDType::MESSAGE_ERROR, ac->T("Failed to identify game. Achievements will not unlock."), "", g_RAImageID, 6.0f);
		break;
	}
}

void ChangeUMD(const Path &path, FileLoader *fileLoader) {
	if (!IsActive()) {
		// Nothing to do.
		return;
	}

	g_blockDevice = constructBlockDevice(fileLoader);
	if (!g_blockDevice) {
		ERROR_LOG(ACHIEVEMENTS, "Failed to construct block device for '%s' - can't identify", path.c_str());
		return;
	}

	g_isIdentifying = true;

	rc_client_begin_change_media(g_rcClient,
		path.c_str(),
		nullptr,
		0,
		&change_media_callback,
		nullptr
	);

	// fclose above will have deleted it.
	g_blockDevice = nullptr;
}

std::set<uint32_t> GetActiveChallengeIDs() {
	return g_activeChallenges;
}

} // namespace Achievements
