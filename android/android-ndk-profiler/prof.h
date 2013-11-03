/*
 * Part of the android-ndk-profiler library.
 * Copyright (C) Richard Quirk
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
#ifndef prof_h_seen
#define prof_h_seen
#ifdef __cplusplus
extern "C" {
#endif

void monstartup(const char *libname);
void moncleanup(void);

#ifdef __cplusplus
}
#endif
#endif
