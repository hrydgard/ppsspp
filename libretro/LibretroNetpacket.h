// Copyright (c) 2012- PPSSPP Project.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "libretro.h"

namespace Libretro {
bool NetpacketInit(retro_environment_t environment);
bool NetpacketEnabled();
void NetpacketConfig();
void NetpacketPoll();
void NetpacketStop();
void NetpacketShutdown();
// Start state: with the option on, the core loads the player's own state once the game has booted (each player has
// theirs: the frontend can't load one into a connected session), then holds emulation until every GameMode member of
// that state is connected again, so that all the PSPs resume together.
bool NetpacketStartStateWanted();
void NetpacketHoldAfterLoad(bool startState);
bool NetpacketHolding();
}
