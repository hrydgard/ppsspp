#pragma once

#include "math/lin/vec3.h"

enum {
  PAD_BUTTON_A = 1,
  PAD_BUTTON_B = 2,
  PAD_BUTTON_X = 4,
  PAD_BUTTON_Y = 8,
  PAD_BUTTON_LBUMPER = 16,
  PAD_BUTTON_RBUMPER = 32,
  PAD_BUTTON_START = 64,
  PAD_BUTTON_BACK = 128,
  PAD_BUTTON_UP = 256,
  PAD_BUTTON_DOWN = 512,
  PAD_BUTTON_LEFT = 1024,
  PAD_BUTTON_RIGHT = 2048,
};

// Agglomeration of all possible inputs, and automatically computed
// deltas where applicable.
struct InputState {
  // Gamepad style input
  int pad_buttons; // bitfield
  int pad_last_buttons;
  int pad_buttons_down;  // buttons just pressed this frame
  int pad_buttons_up;  // buttons just pressed last frame
  float pad_lstick_x;
  float pad_lstick_y;
  float pad_rstick_x;
  float pad_rstick_y;
  float pad_ltrigger;
  float pad_rtrigger;

  // Mouse/singletouch style input
  volatile bool mouse_valid;
  int mouse_x;
  int mouse_y;
  int mouse_buttons;
  int mouse_buttons_down;
  int mouse_buttons_up;
  int mouse_last_buttons;

  // Accelerometer
  bool accelerometer_valid;
  Vec3 acc;
};

inline void UpdateInputState(InputState *input) {
  input->pad_buttons_down = (input->pad_last_buttons ^ input->pad_buttons) & input->pad_buttons;
  input->pad_buttons_up = (input->pad_last_buttons ^ input->pad_buttons) & input->pad_last_buttons;
  input->pad_last_buttons = input->pad_buttons;
}
