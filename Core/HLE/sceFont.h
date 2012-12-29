#pragma once

#include "../../Common/ChunkFile.h"

void Register_sceFont();

void __FontInit();
void __FontDoState(PointerWrap &p);
