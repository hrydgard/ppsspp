#pragma once

// Wraps NVDA text-to-speech and possibly other TTS libraries later.
//
// If NVDA is unsupported on the platform or not activated, all the calls will just fail,
// so we don't have to add checks all over the place.

bool TTS_Active();
void TTS_Say(const char *text);
void TTS_Braille(const char *text);
