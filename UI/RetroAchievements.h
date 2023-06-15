#pragma once

#include "ext/rcheevos/include/rcheevos.h"

class RetroAchievements {
public:
	void Init();
	void Shutdown();
};

extern RetroAchievements g_retroAchievements;

// TODO: Move the screens out into their own files.
