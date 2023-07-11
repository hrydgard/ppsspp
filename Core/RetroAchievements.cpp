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
	g_OSD.RemoveProgressBar(str_id);
}

void OnAchievementsLoginStateChange() {
	System_PostUIMessage("achievements_loginstatechange", "");
}

namespace Achievements {

// It's the name of the secret, not a secret name - the value is not secret :)
static const char *RA_TOKEN_SECRET_NAME = "retroachievements";

static Achievements::Statistics g_stats;

const std::string g_gameIconCachePrefix = "game:";
const std::string g_iconCachePrefix = "badge:";

Path s_game_path;
std::string s_game_hash;

std::set<uint32_t> g_activeChallenges;
bool g_isIdentifying = false;

// rc_client implementation
static rc_client_t *g_rcClient;

#define PSP_MEMORY_OFFSET 0x08000000

rc_client_t *GetClient() {
	return g_rcClient;
}

bool IsLoggedIn() {
	return rc_client_get_user_info(g_rcClient) != nullptr;
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

bool ChallengeModeActive() {
	if (!g_rcClient) {
		return false;
	}
	return IsLoggedIn() && rc_client_get_hardcore_enabled(g_rcClient);
}

bool WarnUserIfChallengeModeActive(const char *message) {
	if (!ChallengeModeActive()) {
		return false;
	}

	const char *showMessage = message;
	if (!message) {
		auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
		showMessage = ac->T("This feature is not available in Challenge Mode");
	}

	g_OSD.Show(OSDType::MESSAGE_WARNING, showMessage, 3.0f);
	return true;
}

bool IsBlockingExecution() {
	return g_isIdentifying;
}

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

bool IsActive() {
	return GetGameID() != 0;
}

// This is the function the rc_client will use to read memory for the emulator. we don't need it yet,
// so just provide a dummy function that returns "no memory read".
static uint32_t read_memory_callback(uint32_t address, uint8_t *buffer, uint32_t num_bytes, rc_client_t *client) {
	// Achievements are traditionally defined relative to the base of main memory of the emulated console.
	// This is some kind of RetroArch-related legacy. In the PSP's case, this is simply a straight offset of 0x08000000.
	uint32_t orig_address = address;
	address += PSP_MEMORY_OFFSET;

	if (!Memory::IsValidAddress(address)) {
		// Some achievement packs are really, really spammy.
		// So we'll just count the bad accesses.
		Achievements::g_stats.badMemoryAccessCount++;
		if (g_Config.bAchievementsLogBadMemReads) {
			WARN_LOG(G3D, "RetroAchievements PeekMemory: Bad address %08x (%d bytes) (%08x was passed in)", address, num_bytes, orig_address);
		}

		// TEMPORARY HACK: rcheevos' handling of bad memory accesses causes a LOT of extra work, since
		// for some reason these invalid accesses keeps happening. So we'll temporarily to back to the previous
		// behavior of simply returning 0.
		uint32_t temp = 0;
		memcpy(buffer, &temp, num_bytes);
		return num_bytes;
	}

	switch (num_bytes) {
	case 1:
		*buffer = Memory::ReadUnchecked_U8(address);
		return 1;
	case 2: {
		uint16_t temp = Memory::ReadUnchecked_U16(address);
		memcpy(buffer, &temp, 2);
		return 2;
	}
	case 4: {
		uint32_t temp = Memory::ReadUnchecked_U32(address);
		memcpy(buffer, &temp, 4);
		return 4;
	}
	default:
		return 0;
	}
}

// This is the HTTP request dispatcher that is provided to the rc_client. Whenever the client
// needs to talk to the server, it will call this function.
static void server_call_callback(const rc_api_request_t *request,
	rc_client_server_callback_t callback, void *callback_data, rc_client_t *client)
{
	// If post data is provided, we need to make a POST request, otherwise, a GET request will suffice.
	if (request->post_data) {
		g_DownloadManager.AsyncPostWithCallback(std::string(request->url), std::string(request->post_data), "application/x-www-form-urlencoded", [=](http::Download &download) {
			std::string buffer;
			download.buffer().TakeAll(&buffer);
			rc_api_server_response_t response{};
			response.body = buffer.c_str();
			response.body_length = buffer.size();
			response.http_status_code = download.ResultCode();
			callback(&response, callback_data);
		});
	} else {
		g_DownloadManager.StartDownloadWithCallback(std::string(request->url), Path(), [=](http::Download &download) {
			std::string buffer;
			download.buffer().TakeAll(&buffer);
			rc_api_server_response_t response{};
			response.body = buffer.c_str();
			response.body_length = buffer.size();
			response.http_status_code = download.ResultCode();
			callback(&response, callback_data);
		});
	}
}

// Write log messages to the console
static void log_message_callback(const char *message, const rc_client_t *client) {
	INFO_LOG(ACHIEVEMENTS, "RetroAchievements log: %s", message);
}

static void login_token_callback(int result, const char *error_message, rc_client_t *client, void *userdata) {
	switch (result) {
	case RC_OK:
		OnAchievementsLoginStateChange();
		break;
	case RC_INVALID_STATE:
	case RC_API_FAILURE:
	case RC_MISSING_VALUE:
	case RC_INVALID_JSON:
		ERROR_LOG(ACHIEVEMENTS, "Failure logging in via token: %d, %s", result, error_message);
		OnAchievementsLoginStateChange();
		break;
	}
}

// For detailed documentation, see https://github.com/RetroAchievements/rcheevos/wiki/rc_client_set_event_handler.
static void event_handler_callback(const rc_client_event_t *event, rc_client_t *client) {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	switch (event->type) {
	case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
		// An achievement was earned by the player. The handler should notify the player that the achievement was earned.
		g_OSD.ShowAchievementUnlocked(event->achievement->id);
		INFO_LOG(ACHIEVEMENTS, "Achievement unlocked: '%s' (%d)", event->achievement->title, event->achievement->id);
		break;
	case RC_CLIENT_EVENT_GAME_COMPLETED:
	{
		// All achievements for the game have been earned. The handler should notify the player that the game was completed or mastered, depending on challenge mode.
		auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

		const rc_client_game_t *gameInfo = rc_client_get_game_info(g_rcClient);

		// TODO: Translation?
		std::string title = ReplaceAll(ac->T("Mastered %1"), "%1", gameInfo->title);
		rc_client_user_game_summary_t summary;
		rc_client_get_user_game_summary(g_rcClient, &summary);

		std::string message = StringFromFormat(ac->T("%d achievements"), summary.num_unlocked_achievements);

		g_OSD.Show(OSDType::MESSAGE_INFO, title, message, DeNull(gameInfo->badge_name), 10.0f);

		INFO_LOG(ACHIEVEMENTS, "%s", message.c_str());
		break;
	}
	case RC_CLIENT_EVENT_LEADERBOARD_STARTED:
		// A leaderboard attempt has started. The handler may show a message with the leaderboard title and /or description indicating the attempt started.
		INFO_LOG(ACHIEVEMENTS, "Leaderboard attempt started: %s", event->leaderboard->title);
		g_OSD.Show(OSDType::MESSAGE_INFO, ReplaceAll(ac->T("%1: Leaderboard attempt started"), "%1", event->leaderboard->title), DeNull(event->leaderboard->description), 3.0f);
		break;
	case RC_CLIENT_EVENT_LEADERBOARD_FAILED:
		NOTICE_LOG(ACHIEVEMENTS, "Leaderboard attempt failed: %s", event->leaderboard->title);
		g_OSD.Show(OSDType::MESSAGE_INFO, ReplaceAll(ac->T("%1: Leaderboard attempt failed"), "%1", event->leaderboard->title), 3.0f);
		// A leaderboard attempt has failed.
		break;
	case RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED:
		NOTICE_LOG(ACHIEVEMENTS, "Leaderboard result submitted: %s", event->leaderboard->title);
		g_OSD.Show(OSDType::MESSAGE_SUCCESS, ReplaceAll(ReplaceAll(ac->T("%1: Submitting leaderboard score: %2!"), "%1", DeNull(event->leaderboard->title)), "%2", DeNull(event->leaderboard->tracker_value)), DeNull(event->leaderboard->description), 3.0f);
		// A leaderboard attempt was completed.The handler may show a message with the leaderboard title and /or description indicating the final value being submitted to the server.
		break;
	case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW:
		NOTICE_LOG(ACHIEVEMENTS, "Challenge indicator show: %s", event->achievement->title);
		g_OSD.ShowChallengeIndicator(event->achievement->id, true);
		g_activeChallenges.insert(event->achievement->id);
		// A challenge achievement has become active. The handler should show a small version of the achievement icon
		// to indicate the challenge is active.
		break;
	case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_HIDE:
		NOTICE_LOG(ACHIEVEMENTS, "Challenge indicator hide: %s", event->achievement->title);
		g_OSD.ShowChallengeIndicator(event->achievement->id, false);
		g_activeChallenges.erase(event->achievement->id);
		// The handler should hide the small version of the achievement icon that was shown by the corresponding RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW event.
		break;
	case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW:
		NOTICE_LOG(ACHIEVEMENTS, "Progress indicator show: %s, progress: '%s' (%f)", event->achievement->title, event->achievement->measured_progress, event->achievement->measured_percent);
		// An achievement that tracks progress has changed the amount of progress that has been made.
		// The handler should show a small version of the achievement icon along with the achievement->measured_progress text (for two seconds).
		// Only one progress indicator should be shown at a time.
		// If a progress indicator is already visible, it should be updated with the new icon and text, and the two second timer should be restarted.
		g_OSD.ShowAchievementProgress(event->achievement->id, 2.0f);
		break;
	case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_SHOW:
		NOTICE_LOG(ACHIEVEMENTS, "Leaderboard tracker show: '%s' (id %d)", event->leaderboard_tracker->display, event->leaderboard_tracker->id);
		// A leaderboard_tracker has become active. The handler should show the tracker text on screen.
		// Multiple active leaderboards may share a single tracker if they have the same definition and value.
		// As such, the leaderboard tracker IDs are unique amongst the leaderboard trackers, and have no correlation to the active leaderboard(s).
		// Use event->leaderboard_tracker->id for uniqueness checks, and display event->leaderboard_tracker->display (string)
		g_OSD.ShowLeaderboardTracker(event->leaderboard_tracker->id, event->leaderboard_tracker->display, true);
		break;
	case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_HIDE:
		// A leaderboard_tracker has become inactive. The handler should hide the tracker text from the screen.
		NOTICE_LOG(ACHIEVEMENTS, "Leaderboard tracker hide: '%s' (id %d)", event->leaderboard_tracker->display, event->leaderboard_tracker->id);
		g_OSD.ShowLeaderboardTracker(event->leaderboard_tracker->id, nullptr, false);
		break;
	case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_UPDATE:
		// A leaderboard_tracker value has been updated. The handler should update the tracker text on the screen.
		NOTICE_LOG(ACHIEVEMENTS, "Leaderboard tracker update: '%s' (id %d)", event->leaderboard_tracker->display, event->leaderboard_tracker->id);
		g_OSD.ShowLeaderboardTracker(event->leaderboard_tracker->id, event->leaderboard_tracker->display, true);
		break;
	case RC_CLIENT_EVENT_RESET:
		WARN_LOG(ACHIEVEMENTS, "Resetting game due to achievement setting change!");
		// Challenge mode was enabled, or something else that forces a game reset.
		System_PostUIMessage("reset", "");
		break;
	case RC_CLIENT_EVENT_SERVER_ERROR:
		ERROR_LOG(ACHIEVEMENTS, "Server error: %s: %s", event->server_error->api, event->server_error->error_message);
		g_OSD.Show(OSDType::MESSAGE_ERROR, "Server error");
		break;
	default:
		WARN_LOG(ACHIEVEMENTS, "Unhandled rc_client event %d, ignoring", event->type);
		break;
	}
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

	// Disable SSL for now.
	rc_client_set_host(g_rcClient, "http://retroachievements.org");

	rc_client_set_event_handler(g_rcClient, event_handler_callback);

	std::string api_token = NativeLoadSecret(RA_TOKEN_SECRET_NAME);
	if (!api_token.empty()) {
		rc_client_begin_login_with_token(g_rcClient, g_Config.sAchievementsUserName.c_str(), api_token.c_str(), &login_token_callback, nullptr);
	}

	INFO_LOG(ACHIEVEMENTS, "Achievements initialized.");
}

static void login_password_callback(int result, const char *error_message, rc_client_t *client, void *userdata) {
	switch (result) {
	case RC_OK:
	{
		// Get the token and store it.
		const rc_client_user_t *user = rc_client_get_user_info(client);
		g_Config.sAchievementsUserName = user->username;
		NativeSaveSecret(RA_TOKEN_SECRET_NAME, std::string(user->token));
		OnAchievementsLoginStateChange();
		break;
	}
	case RC_INVALID_STATE:
	case RC_API_FAILURE:
	case RC_MISSING_VALUE:
	case RC_INVALID_JSON:
		ERROR_LOG(ACHIEVEMENTS, "Failure logging in via token: %d, %s", result, error_message);
		OnAchievementsLoginStateChange();
		break;
	}

	OSDCloseBackgroundProgressDialog("cheevos_async_login");
}

bool LoginAsync(const char *username, const char *password) {
	if (IsLoggedIn() || std::strlen(username) == 0 || std::strlen(password) == 0 || IsUsingRAIntegration())
		return false;

	OSDOpenBackgroundProgressDialog("cheevos_async_login", "Logging in to RetroAchivements...", 0, 0, 0);
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
}

void DoState(PointerWrap &p) {
	auto sw = p.Section("Achievements", 0, 1);
	if (!sw) {
		// Save state is missing the section.
		// Reset the runtime.
		if (HasAchievementsOrLeaderboards()) {
			auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
			g_OSD.Show(OSDType::MESSAGE_WARNING, ac->T("Save state loaded without achievement data"), 5.0f);
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
		}
		delete[] buffer;
	} else {
		if (IsActive()) {
			auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
			g_OSD.Show(OSDType::MESSAGE_WARNING, ac->T("Save state loaded without achievement data"), 5.0f);
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
		g_DownloadManager.StartDownloadWithCallback(url, Path(), [cache_key](http::Download &download) {
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

	std::string summaryString = StringFromFormat(ac->T("Earned", "You have unlocked %d of %d achievements, earning %d of %d points"),
		summary.num_unlocked_achievements, summary.num_core_achievements + summary.num_unofficial_achievements,
		summary.points_unlocked, summary.points_core);
	if (ChallengeModeActive()) {
		summaryString.append("\n");
		summaryString.append(ac->T("Challenge Mode"));
	}
	if (EncoreModeActive()) {
		summaryString.append("\n");
		summaryString.append(ac->T("Encore Mode"));
	}
	return summaryString;
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
			Achievements::DownloadImageIfMissing(cacheId, std::move(std::string(temp)));
		}
		g_OSD.Show(OSDType::MESSAGE_INFO, std::string(gameInfo->title), GetGameAchievementSummary(), cacheId, 5.0f);
		break;
	}
	case RC_NO_GAME_LOADED:
		// The current game does not support achievements.
		g_OSD.Show(OSDType::MESSAGE_INFO, ac->T("This game has no achievements"), 3.0f);
		break;
	default:
		// Other various errors.
		ERROR_LOG(ACHIEVEMENTS, "Failed to identify/load game: %d (%s)", result, error_message);
		break;
	}

	g_isIdentifying = false;
}

void SetGame(const Path &path) {
	if (!g_rcClient || !IsLoggedIn()) {
		// Nothing to do.
		return;
	}

	rc_hash_filereader rc_filereader;
	rc_filereader.open = [](const char *utf8Path) {
		Path path(utf8Path);
		FILE *f = File::OpenCFile(path, "rb");
		return (void *)f;
	};
	rc_filereader.seek = [](void *file_handle, int64_t offset, int origin) { fseek((FILE *)file_handle, offset, origin); };
	rc_filereader.tell = [](void *file_handle) -> int64_t { return (int64_t)ftell((FILE *)file_handle); };
	rc_filereader.read = [](void *file_handle, void *buffer, size_t requested_bytes) -> size_t { return fread(buffer, 1, requested_bytes, (FILE *)file_handle); };
	rc_filereader.close = [](void *file_handle) { fclose((FILE *)file_handle); };

	// The caller should hold off on executing game code until this turns false, checking with IsBlockingExecution()
	g_isIdentifying = true;

	// Apply pre-load settings.
	rc_client_set_hardcore_enabled(g_rcClient, g_Config.bAchievementsChallengeMode ? 1 : 0);
	rc_client_set_encore_mode_enabled(g_rcClient, g_Config.bAchievementsEncoreMode ? 1 : 0);
	rc_client_set_unofficial_enabled(g_rcClient, g_Config.bAchievementsUnofficial ? 1 : 0);

	rc_hash_init_custom_filereader(&rc_filereader);
	rc_hash_init_default_cdreader();
	rc_client_begin_identify_and_load_game(g_rcClient, RC_CONSOLE_PSP, path.c_str(), nullptr, 0, &identify_and_load_callback, nullptr);
}

void UnloadGame() {
	if (g_rcClient) {
		rc_client_unload_game(g_rcClient);
	}
}

void change_media_callback(int result, const char *error_message, rc_client_t *client, void *userdata) {
	NOTICE_LOG(ACHIEVEMENTS, "Change media callback: %d (%s)", result, error_message);
	g_isIdentifying = false;
}

void ChangeUMD(const Path &path) {
	if (!IsActive()) {
		// Nothing to do.
		return;
	}

	rc_client_begin_change_media(g_rcClient, 
		path.c_str(),
		nullptr,
		0,
		&change_media_callback,
		nullptr
	);

	g_isIdentifying = true;
}

std::set<uint32_t> GetActiveChallengeIDs() {
	return g_activeChallenges;
}

} // namespace Achievements
