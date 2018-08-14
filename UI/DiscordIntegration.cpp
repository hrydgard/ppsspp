#include <ctime>
#include <cassert>

#include "ppsspp_config.h"
#include "Common/Log.h"
#include "Core/Config.h"
#include "DiscordIntegration.h"
#include "i18n/i18n.h"

#ifdef _WIN32

#define ENABLE_DISCORD

#else

// TODO

#endif

#ifdef ENABLE_DISCORD
#include "ext/discord-rpc/include/discord_rpc.h"
#endif

// TODO: Enable on more platforms. Make optional.

Discord g_Discord;

static const char *ppsspp_app_id = "423397985041383434";

// No context argument? What?
static void handleDiscordError(int errCode, const char *message) {
	ERROR_LOG(SYSTEM, "Discord error code %d: '%s'", message);
}

Discord::~Discord() {
	assert(!initialized_);
}

bool Discord::IsEnabled() const {
	return g_Config.bDiscordPresence;
}

void Discord::Init() {
	assert(IsEnabled());
	assert(!initialized_);

#ifdef ENABLE_DISCORD
	DiscordEventHandlers eventHandlers{};
	eventHandlers.errored = &handleDiscordError;
	Discord_Initialize(ppsspp_app_id, &eventHandlers, 0, nullptr);
#endif

	initialized_ = true;
}

void Discord::Shutdown() {
	assert(initialized_);
#ifdef ENABLE_DISCORD
	Discord_Shutdown();
#endif
	initialized_ = false;
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
#ifdef DISCORD_DISABLE_IO_THREAD
	Discord_UpdateConnection();
#endif
	Discord_RunCallbacks();
#endif
}

void Discord::SetPresenceGame(const char *gameTitle) {
	if (!IsEnabled())
		return;
	
	if (!initialized_) {
		Init();
	}

#ifdef ENABLE_DISCORD
	I18NCategory *sc = GetI18NCategory("Screen");

	DiscordRichPresence discordPresence{};
	discordPresence.state = gameTitle;
	std::string details = sc->T("Playing");
	discordPresence.details = details.c_str();
	discordPresence.startTimestamp = time(0);
#ifdef GOLD
	discordPresence.largeImageKey = "icon_gold_png";
#else
	discordPresence.largeImageKey = "icon_regular_png";
#endif
	Discord_UpdatePresence(&discordPresence);
#endif
}

void Discord::SetPresenceMenu() {
	if (!IsEnabled())
		return;

	if (!initialized_) {
		Init();
	}

#ifdef ENABLE_DISCORD
	I18NCategory *sc = GetI18NCategory("Screen");

	DiscordRichPresence discordPresence{};
	discordPresence.state = sc->T("In menu");
	discordPresence.details = "";
	discordPresence.startTimestamp = time(0);
#ifdef GOLD
	discordPresence.largeImageKey = "icon_gold_png";
#else
	discordPresence.largeImageKey = "icon_regular_png";
#endif
	Discord_UpdatePresence(&discordPresence);
#endif
}
