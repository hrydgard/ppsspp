#pragma once

#include "Common/CommonTypes.h"

class PointerWrap;

void Register_sceNpDrm();

// A module wrapped in an NPDRM "\0PSPEDAT" container has its PRX encrypted against a key built
// from that container's header and, usually, the licensee key the game handed over through
// sceNpDrmSetLicenseeKey - which is why this lives here rather than with the PRX decrypter.
// Writes 16 bytes to keyOut, to be passed to pspDecryptPRX() as the seed.
//
// edatHeader must point at the 0x90 readable bytes of the container header. Returns false if the
// header asks for something we can't build, notably a licensee key the game never set.
// See docs/pkg_notes.md for the layout and where this came from.
bool NpDrmDeriveModuleKey(const u8 *edatHeader, u8 *keyOut);
