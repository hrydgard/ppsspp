//
//  SmartKeyboardMap.cpp
//  PPSSPP
//
//  Created by xieyi on 2017/9/4.
//
//

#include "SmartKeyboardMap.hpp"
#include "Common/Input/KeyCodes.h"

InputKeyCode getSmartKeyboardMap(int keycode) {
    switch(keycode) {
        case 4: return NKCODE_A;
        case 5: return NKCODE_B;
        case 6: return NKCODE_C;
        case 7: return NKCODE_D;
        case 8: return NKCODE_E;
        case 9: return NKCODE_F;
        case 10: return NKCODE_G;
        case 11: return NKCODE_H;
        case 12: return NKCODE_I;
        case 13: return NKCODE_J;
        case 14: return NKCODE_K;
        case 15: return NKCODE_L;
        case 16: return NKCODE_M;
        case 17: return NKCODE_N;
        case 18: return NKCODE_O;
        case 19: return NKCODE_P;
        case 20: return NKCODE_Q;
        case 21: return NKCODE_R;
        case 22: return NKCODE_S;
        case 23: return NKCODE_T;
        case 24: return NKCODE_U;
        case 25: return NKCODE_V;
        case 26: return NKCODE_W;
        case 27: return NKCODE_X;
        case 28: return NKCODE_Y;
        case 29: return NKCODE_Z;
        case 30: return NKCODE_1;
        case 31: return NKCODE_2;
        case 32: return NKCODE_3;
        case 33: return NKCODE_4;
        case 34: return NKCODE_5;
        case 35: return NKCODE_6;
        case 36: return NKCODE_7;
        case 37: return NKCODE_8;
        case 38: return NKCODE_9;
        case 39: return NKCODE_0;
        case 40: return NKCODE_ENTER;
        case 43: return NKCODE_TAB;
        case 44: return NKCODE_SPACE;
        case 45: return NKCODE_MINUS;
        case 46: return NKCODE_EQUALS;
        case 47: return NKCODE_LEFT_BRACKET;
        case 48: return NKCODE_RIGHT_BRACKET;
        case 49: return NKCODE_BACKSLASH;
        case 51: return NKCODE_SEMICOLON;
        case 52: return NKCODE_APOSTROPHE;
        case 53: return NKCODE_BACK;//NKCODE_GRAVE;
        case 54: return NKCODE_COMMA;
        case 55: return NKCODE_PERIOD;
        case 56: return NKCODE_SLASH;
        case 57: return NKCODE_CAPS_LOCK;
        case 79: return NKCODE_DPAD_RIGHT;
        case 80: return NKCODE_DPAD_LEFT;
        case 81: return NKCODE_DPAD_DOWN;
        case 82: return NKCODE_DPAD_UP;
        case 224: return NKCODE_CTRL_LEFT;
        case 225: return NKCODE_SHIFT_LEFT;
        case 226: return NKCODE_META_LEFT;
        case 227: return NKCODE_ALT_LEFT;
        case 229: return NKCODE_SHIFT_RIGHT;
        case 230: return NKCODE_META_RIGHT;
        case 231: return NKCODE_ALT_RIGHT;
        default: return NKCODE_UNKNOWN;
    }
}
