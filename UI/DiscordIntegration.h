#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Simple wrapper around the Discord api.

// All platforms should call it, but we only actually take action on
// platforms where we want it enabled (only PC initially).

// All you need to call is FrameCallback, Shutdown, and UpdatePresence.

class Discord {
public:
	~Discord();
	void Update();  // Call every frame or at least regularly. Will initialize if necessary.
	void Shutdown();

	void SetPresenceGame(std::string_view gameTitle);
	void SetPresenceMenu();
	void ClearPresence();

	static bool IsAvailable();

private:
	void Init();
	bool IsEnabled() const;
	void UpdateGamePresence();
	void ResetGameState();

	bool initialized_ = false;
	bool inGame_ = false;
	int64_t startTimestamp_ = 0;
	std::string gameTitle_;
	std::string richPresence_;
	double lastRichPresenceCheck_ = 0.0;
};

extern Discord g_Discord;
