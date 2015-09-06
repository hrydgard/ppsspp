#pragma once

// Statistics collection. Not very developed.

#define STATS_ENABLE

#ifdef STATS_ENABLE

void IncrementStat(const char *name);

#define INCSTAT(name) IncrementStat(name);

#else

#define INCSTAT(name)

#endif

