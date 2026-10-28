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
}
