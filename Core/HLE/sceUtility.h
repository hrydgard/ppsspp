// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

class PointerWrap;

// Valid values for PSP_SYSTEMPARAM_ID_INT_LANGUAGE
#define PSP_SYSTEMPARAM_LANGUAGE_JAPANESE               0
#define PSP_SYSTEMPARAM_LANGUAGE_ENGLISH                1
#define PSP_SYSTEMPARAM_LANGUAGE_FRENCH                 2
#define PSP_SYSTEMPARAM_LANGUAGE_SPANISH                3
#define PSP_SYSTEMPARAM_LANGUAGE_GERMAN                 4
#define PSP_SYSTEMPARAM_LANGUAGE_ITALIAN                5
#define PSP_SYSTEMPARAM_LANGUAGE_DUTCH                  6
#define PSP_SYSTEMPARAM_LANGUAGE_PORTUGUESE             7
#define PSP_SYSTEMPARAM_LANGUAGE_RUSSIAN                8
#define PSP_SYSTEMPARAM_LANGUAGE_KOREAN                 9
#define PSP_SYSTEMPARAM_LANGUAGE_CHINESE_TRADITIONAL    10
#define PSP_SYSTEMPARAM_LANGUAGE_CHINESE_SIMPLIFIED     11

#define PSP_SYSTEMPARAM_TIME_FORMAT_24HR    0
#define PSP_SYSTEMPARAM_TIME_FORMAT_12HR    1

#define PSP_SYSTEMPARAM_ID_STRING_NICKNAME              1
#define PSP_SYSTEMPARAM_ID_INT_ADHOC_CHANNEL            2
#define PSP_SYSTEMPARAM_ID_INT_WLAN_POWERSAVE           3
#define PSP_SYSTEMPARAM_ID_INT_DATE_FORMAT              4
#define PSP_SYSTEMPARAM_ID_INT_TIME_FORMAT              5
//Timezone offset from UTC in minutes, (EST = -300 = -5 * 60)
#define PSP_SYSTEMPARAM_ID_INT_TIMEZONE                 6
#define PSP_SYSTEMPARAM_ID_INT_DAYLIGHTSAVINGS          7
#define PSP_SYSTEMPARAM_ID_INT_LANGUAGE                 8
#define PSP_SYSTEMPARAM_ID_INT_BUTTON_PREFERENCE        9
#define PSP_SYSTEMPARAM_ID_INT_LOCK_PARENTAL_LEVEL      10

// Return values for the SystemParam functions
#define PSP_SYSTEMPARAM_RETVAL_OK                       0

// Valid values for PSP_SYSTEMPARAM_ID_INT_ADHOC_CHANNEL
#define PSP_SYSTEMPARAM_ADHOC_CHANNEL_AUTOMATIC     0
#define PSP_SYSTEMPARAM_ADHOC_CHANNEL_1             1
#define PSP_SYSTEMPARAM_ADHOC_CHANNEL_6             6
#define PSP_SYSTEMPARAM_ADHOC_CHANNEL_11            11

// Valid values for PSP_SYSTEMPARAM_ID_INT_WLAN_POWERSAVE
#define PSP_SYSTEMPARAM_WLAN_POWERSAVE_OFF  0
#define PSP_SYSTEMPARAM_WLAN_POWERSAVE_ON   1

// Valid values for PSP_SYSTEMPARAM_ID_INT_DATE_FORMAT
#define PSP_SYSTEMPARAM_DATE_FORMAT_YYYYMMDD  0
#define PSP_SYSTEMPARAM_DATE_FORMAT_MMDDYYYY  1
#define PSP_SYSTEMPARAM_DATE_FORMAT_DDMMYYYY  2

// Valid values for PSP_SYSTEMPARAM_ID_INT_DAYLIGHTSAVINGS
#define PSP_SYSTEMPARAM_DAYLIGHTSAVINGS_STD    0
#define PSP_SYSTEMPARAM_DAYLIGHTSAVINGS_SAVING 1

// Valid values for PSP_SYSTEMPARAM_ID_INT_BUTTON_PREFERENCE
#define PSP_SYSTEMPARAM_BUTTON_CIRCLE  0
#define PSP_SYSTEMPARAM_BUTTON_CROSS   1

// Valid values for NetParam
#define PSP_NETPARAM_NAME               0 // string
#define PSP_NETPARAM_SSID               1 // string
#define PSP_NETPARAM_SECURE             2 // int
#define PSP_NETPARAM_WEPKEY             3 // string
#define PSP_NETPARAM_IS_STATIC_IP       4 // int
#define PSP_NETPARAM_IP                 5 // string
#define PSP_NETPARAM_NETMASK            6 // string
#define PSP_NETPARAM_ROUTE              7 // string
#define PSP_NETPARAM_MANUAL_DNS         8 // int
#define PSP_NETPARAM_PRIMARYDNS         9 // string
#define PSP_NETPARAM_SECONDARYDNS       10 // string
#define PSP_NETPARAM_PROXY_USER         11 // string
#define PSP_NETPARAM_PROXY_PASS         12 // string
#define PSP_NETPARAM_USE_PROXY          13 // int
#define PSP_NETPARAM_PROXY_SERVER       14 // string
#define PSP_NETPARAM_PROXY_PORT         15 // int
#define PSP_NETPARAM_VERSION            16 // int
#define PSP_NETPARAM_UNKNOWN            17 // int
#define PSP_NETPARAM_8021X_AUTH_TYPE    18 // int
#define PSP_NETPARAM_8021X_USER         19 // string
#define PSP_NETPARAM_8021X_PASS         20 // string
#define PSP_NETPARAM_WPA_TYPE           21 // int
#define PSP_NETPARAM_WPA_KEY            22 // string
#define PSP_NETPARAM_BROWSER            23 // int
#define PSP_NETPARAM_WIFI_CONFIG        24 // int

// X-Men Legends 2, and some homebrew may support up to 10 net config entries, but we currently only have 1 faked net config
#define PSP_NETPARAM_MAX_NUMBER_DUMMY_ENTRIES   1

enum class UtilityDialogType {
	NONE,
	SAVEDATA,
	MSG,
	OSK,
	NET,
	SCREENSHOT,
	GAMESHARING,
	GAMEDATAINSTALL,
	NPSIGNIN,
};

void __UtilityInit();
void __UtilityDoState(PointerWrap &p);
void __UtilityShutdown();

void UtilityDialogInitialize(UtilityDialogType type, int delayUs, int priority);
void UtilityDialogShutdown(UtilityDialogType type, int delayUs, int priority);

void Register_sceUtility();
