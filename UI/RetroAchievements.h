#pragma once

#include "ext/rcheevos/include/rcheevos.h"
#include "ext/rcheevos/include/rc_runtime.h"

enum class RetroState {
	NONE,
	LOGGED_IN,
};

class RetroAchievements {
public:
	RetroAchievements();
	~RetroAchievements();

	void Login(const char *username, const char *password);
	void Logout();

private:
	RetroState state_ = RetroState::LOGGED_IN;

	rc_runtime_t *runtime_ = nullptr;
};

extern RetroAchievements g_retroAchievements;

// TODO: Move the screens out into their own files.
