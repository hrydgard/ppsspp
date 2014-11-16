#pragma once

#include <string>

void SetBackgroundAudioGame(const std::string &path);
int MixBackgroundAudio(short *buffer, int size);
