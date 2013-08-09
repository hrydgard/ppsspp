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

extern std::map<int, std::string> g_TextTranslationTable;

#define TR_BACK 0
#define TR_ENTER 1
#define TR_YES 2
#define TR_NO 3
#define TR_OK 4
#define TR_SELECT 5
#define TR_DELETE 6
#define TR_FINISH 7
#define TR_SHIFT 8
#define TR_SAVE 9
#define TR_LOAD 10
#define TR_NEW_DATA 11
#define TR_STATE_LOAD_SUCCESS 12
#define TR_STATE_SAVE_SUCCESS 13
#define TR_STATE_SAVE_FAILURE 14
#define TR_SAVE_CONFIRM 15
#define TR_SAVE_OVERWRITE 16
#define TR_NON_BUFFERED_RENDERING 17
#define TR_BUFFERED_RENDERING 18
#define TR_FBO_MEMORY_CPU 19
#define TR_FBO_MEMORY_GPU 20
#define TR_HW_TRANSFORM 21
#define TR_DYNAREC 22
#define TR_SAVING 23
#define TR_SAVE_COMPLETE 24
#define TR_LOAD_CONFIRM 25
#define TR_LOADING 26
#define TR_LOAD_COMPLETE 27
#define TR_DELETE_CONFIRM 28
#define TR_DELETING 29
#define TR_DELETE_COMPLETE 30
#define TR_NO_DATA 31
#define TR_PRESS_ESCAPE 32
#define TR_SPEED_FIXED 33
#define TR_SPEED_STANDARD 34
#define TR_PLAY 35
#define TR_GAME_SETTINGS 36
#define TR_DELETE_SAVEDATA 37
#define TR_DELETE_GAME 38
#define TR_RENDERING_MODE 39
#define TR_MODE 40
#define TR_FEATURES 41
#define TR_ANTIALIASING 42
#define TR_VERTEX_CACHE 43
#define TR_STRETCH_DISPLAY 44
#define TR_MIPMAPPING 45
#define TR_TRUE_COLOR 46
#define TR_VSYNC 47
#define TR_FULLSCREEN 48
#define TR_FRAMERATE_CONTROL 49
#define TR_SHOW_FPS_COUNTER 50
#define TR_SHOW_DEBUG_STATS 51
#define TR_FRAMESKIPPING 52
#define TR_FORCE_60_FPS 53
#define TR_ANISO_FILTERING 54
#define TR_UPSCALE_LEVEL 55
#define TR_UPSCALE_TYPE 56
#define TR_DEPOSTERIZE 57
#define TR_TEX_FILTERING 58
#define TR_ENABLE_SOUND 59
#define TR_ENABLE_ATRAC3_PLUS 60
#define TR_SFX_VOLUME 61
#define TR_BGM_VOLUME 62
#define TR_ONSCREEN 63
#define TR_SHOW_LEFT_ANALOG 64
#define TR_TILT_ANALOG_HORIZ 65
#define TR_CONTROL_MAPPING 66
#define TR_BUTTON_OPACITY 67
#define TR_FAST_MEMORY 68
#define TR_UNLOCK_CPU_CLOCK 69
#define TR_DAYLIGHT_SAVINGS 70
#define TR_DATE_FORMAT 71
#define TR_TIME_FORMAT 72
#define TR_BUTTON_PREF 73
#define TR_USE_NEWUI 74
#define TR_SEND_COMPAT_REPORTS 75
#define TR_ENABLE_CHEATS 76
#define TR_SCREENSHOTS_PNG 77
#define TR_SYSTEM_LANG 78
#define TR_DEV_TOOLS 79
#define TR_GENERAL 80
#define TR_RUN_CPU_TESTS 81
#define TR_ENABLE_DEBUG_LOGGING 82
#define TR_CREATED 83
#define TR_LICENSE 84
#define TR_SETTINGS 85
#define TR_CREDITS 86
#define TR_EXIT 87
#define TR_RECENT 88
#define TR_CONTINUE 89
#define TR_BACK_MENU 90
#define TR_PARTIAL_VERT_STRETCH 91
#define TR_FRAMES 92
#define TR_AUTO 93
#define TR_PLUS_ONE 94
#define TR_MINUS_ONE 95
#define TR_SAVE_STATE 96
#define TR_LOAD_STATE 97
#define TR_AUDIO 98
#define TR_AUDIO_DESC 99
#define TR_GRAPHICS 100
#define TR_GRAPHICS_DESC 101
#define TR_SYSTEM 102
#define TR_SYSTEM_DESC 103
#define TR_CONTROLS 104
#define TR_CONTROLS_DESC 105
#define TR_DEVELOPER 106
#define TR_DEVELOPER_DESC 107
#define TR_LANG_INI_LOAD 108
#define TR_LANG_INI_SAVE 109
#define TR_FRAME_DUMP 110
#define TR_AUDIO_SETTINGS 111
#define TR_GRAPHICS_SETTINGS 112
#define TR_PAGE_PREV 113
#define TR_PAGE_NEXT 114
#define TR_LEVEL 115
#define TR_TYPE 116
#define TR_XBRZ 117
#define TR_HYBRID 118
#define TR_BICUBIC 119
#define TR_HYBRID_BYCUBIC 120
#define TR_2X 121
#define TR_3X 122
#define TR_4X 123
#define TR_5X 124
#define TR_8X 125
#define TR_16X 126
#define TR_SHOW_SPEED_INTERNAL_FPS 127
#define TR_INTERNAL_FPS_GAME_DEPENDENT 128
#define TR_DISPLAY_SPEED 129
#define TR_DISPLAY_FPS 130
#define TR_DISPLAY_BOTH 131
#define TR_SPEED 132
#define TR_FPS 133
#define TR_BOTH 134
#define TR_PLUS_TEN 135
#define TR_MINUS_TEN 136
#define TR_TOGGLED_SPEED 137
#define TR_SPEED_COLON 138
#define TR_FRAMES_COLON 139
#define TR_LANGUAGE 140
#define TR_FREQ_COLON 141
#define TR_SYS_LANG_COLON 142
#define TR_BUTTON_PREF_CIRCLE 143
#define TR_BUTTON_PREF_CROSS 144
#define TR_USE_CIRCLE 145
#define TR_USE_CROSS 146
#define TR_MAX_NUM_RECENTS 147
#define TR_CLEAR 148
#define TR_FORMAT_COLON 149
#define TR_12HR 150
#define TR_24HR 151
#define TR_SYSTEM_NICKNAME 152
#define TR_CHANGE 153
#define TR_CONTROL_SETTINGS 154
#define TR_BUTTON_SCALING 155
#define TR_BUTTON_OPACITY_COLON 156
#define TR_KEY_MAPPING 157
#define TR_DEFAULT_MAPPING 158
#define TR_PREV 159
#define TR_NEXT 160
#define TR_MAP_NEW_KEY 161
#define TR_MAP_CURRENT_KEY 162
#define TR_MAP_KEY_WARN 163
#define TR_SAVE_MAPPING 164
#define TR_CANCEL 165
#define TR_UP 166
#define TR_HOME 167
#define TR_TITLE 168
#define TR_CREATED 169
#define TR_CONTRIB 170
#define TR_WRITTEN 171
#define TR_TOOLS 172
#define TR_WEBSITE 173
#define TR_LIST 174
#define TR_CHECK 175
#define TR_INFO1 176
#define TR_INFO2 177
#define TR_INFO3 178
#define TR_INFO4 179
#define TR_INFO5 180
#define TR_BUY_GOLD 181
#define TR_DUBIOUS_ORIGINS 182
#define TR_DOWNLOAD_INSTALL 183
#define TR_MORE_INFO 184
#define TR_FAILED_REACH_SERVER 185
#define TR_PLUGIN_NOT_SUPPORTED 186
#define TR_PLUGIN_NOT_INSTALLED 187
#define TR_PLUGIN_REINSTALL 188
#define TR_PLUGIN_INSTALL_SUCCCESS 189
#define TR_PLUGIN_DOWNLOAD_FAILURE 190
#define TR_START 191
#define TR_TEX_SCALING 192
#define TR_PLUGIN_DOWNLOAD 193
#define TR_PLUGIN_REDOWNLOAD 194