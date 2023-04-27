//
//  CocoaBarItems.h
//  PPSSPP
//
//  Created by Serena on 06/02/2023.
//

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void initializeOSXExtras();

/* Yes it is awkward to put this here but I don't feel like making an entire file for 2 functions */
/* Prefixing with `OSX` to avoid any possible header collisions in the future */
void OSXShowInFinder(const char *path);
void OSXOpenURL(const char *url);

#ifdef __cplusplus
}
#endif
