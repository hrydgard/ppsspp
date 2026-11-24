
#include <ctime>
#include <string>

#include "ppsspp_config.h"
#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/RetroAchievements.h"
#include "UI/DiscordIntegration.h"
#include "Common/Data/Text/I18n.h"
#include "Common/System/System.h"
#include "Common/TimeUtil.h"

#if (PPSSPP_PLATFORM(WINDOWS) || PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(LINUX)) && !PPSSPP_PLATFORM(ANDROID) && !PPSSPP_PLATFORM(UWP)

#ifdef _MSC_VER
#define ENABLE_DISCORD
#elif USE_DISCORD
#define ENABLE_DISCORD
#endif

#else

// TODO

#endif

#ifdef ENABLE_DISCORD
#include "ext/discord-rpc/include/discord_rpc.h"
#endif

// TODO: Enable on more platforms. Make optional.

Discord g_Discord;

static const char *ppsspp_app_id = "423397985041383434";
static const char *ppsspp_app_id_gold = "1471464214710190152";

#ifdef ENABLE_DISCORD
// No context argument? What?
static void handleDiscordError(int errCode, const char *message) {
	ERROR_LOG(Log::System, "Discord error code %d: '%s'", errCode, message);
}

// Discord rejects the whole presence if a string field is longer than this.
static constexpr size_t DISCORD_MAX_FIELD_LEN = 127;

static std::string GetRichPresenceForDiscord() {
	char buf[512];
	size_t sz = Achievements::GetRichPresenceMessage(buf, sizeof(buf));
	if (sz == (size_t)-1) {
		return std::string();
	}
	if (sz > DISCORD_MAX_FIELD_LEN) {
		sz = DISCORD_MAX_FIELD_LEN;
		// Don't cut a UTF-8 sequence in half.
		while (sz > 0 && ((uint8_t)buf[sz] & 0xC0) == 0x80) {
			sz--;
		}
	}
	return std::string(buf, sz);
}

// Rich Presence scripts often change every frame (timers, counters), and evaluating one reads
// emulated memory, so only check now and then.
static constexpr double RICH_PRESENCE_CHECK_INTERVAL = 10.0;  // seconds
#endif

Discord::~Discord() {
	if (initialized_) {
		ERROR_LOG(Log::System, "Discord destructor running though g_Discord.Shutdown() has not been called.");
	}
}

bool Discord::IsAvailable() {
#ifdef ENABLE_DISCORD
	return true;
#else
	return false;
#endif
}

bool Discord::IsEnabled() const {
	return g_Config.bDiscordRichPresence;
}

void Discord::Init() {
	_assert_(IsEnabled());
	_assert_(!initialized_);

#ifdef ENABLE_DISCORD
	DiscordEventHandlers eventHandlers{};
	eventHandlers.errored = &handleDiscordError;

	const char *appId = ppsspp_app_id;
	if (System_GetPropertyBool(SYSPROP_APP_GOLD)) {
		appId = ppsspp_app_id_gold;
	}

	Discord_Initialize(appId, &eventHandlers, 0, nullptr);
	INFO_LOG(Log::System, "Discord connection initialized");
#endif

	initialized_ = true;
}

void Discord::Shutdown() {
	if (initialized_) {
#ifdef ENABLE_DISCORD
		Discord_Shutdown();
#endif
		initialized_ = false;
	}
	ResetGameState();
}

void Discord::ResetGameState() {
	inGame_ = false;
	startTimestamp_ = 0;
	gameTitle_.clear();
	richPresence_.clear();
	lastRichPresenceCheck_ = 0.0;
}

void Discord::Update() {
	if (!IsEnabled()) {
		if (initialized_) {
			Shutdown();
		}
		return;
	} else {
		if (!initialized_) {
			Init();
		}
	}

#ifdef ENABLE_DISCORD
	double now = time_now_d();
	if (inGame_ && now >= lastRichPresenceCheck_ + RICH_PRESENCE_CHECK_INTERVAL) {
		lastRichPresenceCheck_ = now;
		std::string currentRP = GetRichPresenceForDiscord();
		if (currentRP != richPresence_) {
			richPresence_ = std::move(currentRP);
			UpdateGamePresence();
		}
	}

#ifdef DISCORD_DISABLE_IO_THREAD
	Discord_UpdateConnection();
#endif
	Discord_RunCallbacks();
#endif
}

void Discord::UpdateGamePresence() {
#ifdef ENABLE_DISCORD
	DiscordRichPresence discordPresence{};
	if (!richPresence_.empty()) {
		discordPresence.details = gameTitle_.c_str();
		discordPresence.state = richPresence_.c_str();
	} else {
		auto sc = GetI18NCategory(I18NCat::SCREEN);
		discordPresence.state = gameTitle_.c_str();
		discordPresence.details = sc->T_cstr("Playing");
	}
	discordPresence.startTimestamp = startTimestamp_;
	discordPresence.largeImageText = "PPSSPP is the best PlayStation Portable emulator around!";
	discordPresence.largeImageKey = System_GetPropertyBool(SYSPROP_APP_GOLD) ? "icon_gold_png" : "icon_regular_png";
	Discord_UpdatePresence(&discordPresence);
#endif
}

void Discord::SetPresenceGame(std::string_view gameTitle) {
	if (!IsEnabled())
		return;

	if (!initialized_) {
		Init();
	}

	inGame_ = true;
	gameTitle_ = std::string(gameTitle);
	startTimestamp_ = time(0);
#ifdef ENABLE_DISCORD
	richPresence_ = GetRichPresenceForDiscord();
	lastRichPresenceCheck_ = time_now_d();
#endif
	UpdateGamePresence();
}

void Discord::SetPresenceMenu() {
	if (!IsEnabled())
		return;

	if (!initialized_) {
		Init();
	}

	ResetGameState();

#ifdef ENABLE_DISCORD
	auto sc = GetI18NCategory(I18NCat::SCREEN);

	DiscordRichPresence discordPresence{};
	discordPresence.state = sc->T_cstr("In menu");
	discordPresence.details = "";
	discordPresence.startTimestamp = time(0);
	discordPresence.largeImageText = "PPSSPP is the best PlayStation Portable emulator around!";
	discordPresence.largeImageKey = System_GetPropertyBool(SYSPROP_APP_GOLD) ? "icon_gold_png" : "icon_regular_png";
	Discord_UpdatePresence(&discordPresence);
#endif
}

void Discord::ClearPresence() {
	ResetGameState();

	if (!IsEnabled() || !initialized_)
		return;

#ifdef ENABLE_DISCORD
	Discord_ClearPresence();
#endif
}
