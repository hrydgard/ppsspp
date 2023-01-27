//
//  Battery.h
//  PPSSPP
//
//  Created by Serena on 24/01/2023.
//

// NOTE: Though this is a general purpose header file,
// though the implementation right now is Darwin specific
// In case any future platform implementations are made for other platforms,
// define the function below in their own file

#ifndef BATTERY_H
#define BATTERY_H
#include "ppsspp_config.h"
//#include <Foundation/Foundation.h>

#ifdef __cplusplus
extern "C" {
#endif

#if PPSSPP_PLATFORM(IOS) || PPSSPP_PLATFORM(MAC)
#define CAN_DISPLAY_CURRENT_BATTERY_CAPACITY
/// Get the current battery %.
int getCurrentBatteryCapacity();
#endif /* PPSSPP_PLATFORM(IOS) || PPSSPP_PLATFORM(MAC) */

#ifdef __cplusplus
}
#endif

#endif /* BATTERY_H */
