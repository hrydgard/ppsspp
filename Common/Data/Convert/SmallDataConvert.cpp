#include "Common/Data/Convert/SmallDataConvert.h"

alignas(16) const float one_over_255_x4[4] = { 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, };
alignas(16) const float exactly_255_x4[4] = { 255.0f, 255.0f, 255.0f, 255.0f, };
