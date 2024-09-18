#pragma once

#include "SimpleAudioDec.h"

AudioDecoder *CreateAtrac3Audio(int channels, size_t blockAlign, const uint8_t *extraData, size_t extraDataSize);
AudioDecoder *CreateAtrac3PlusAudio(int channels, size_t blockAlign);
