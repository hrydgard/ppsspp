#pragma once

class PointerWrap;

void Register_sceFont();
void Register_sceLibFttt();

// Whether flash0:/font holds the fonts this game's firmware would have had. Used two ways: the
// HLE font loader asks so it knows whether to unpack them off the disc's updater first, and
// CheckDisableHLEAvailability asks because the real libfont.prx a disc ships reads its fonts from
// there and has nothing to fall back on - so it can only replace our HLE when they're present.
bool NandFontsComplete();

void __FontInit();
void __FontShutdown();
void __FontDoState(PointerWrap &p);