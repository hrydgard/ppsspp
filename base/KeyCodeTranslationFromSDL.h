#include "util/const_map.h"
#include <map>

// TODO: fill out this with a full mapping
static const std::map<int, int> KeyMapRawSDLtoNative = InitConstMap<int, int>
	(SDLK_p, KEYCODE_P)
	(SDLK_o, KEYCODE_O)
	(SDLK_i, KEYCODE_I)
	(SDLK_u, KEYCODE_U)
	(SDLK_y, KEYCODE_Y)
	(SDLK_t, KEYCODE_T)
	(SDLK_r, KEYCODE_R)
	(SDLK_e, KEYCODE_E)
	(SDLK_w, KEYCODE_W)
	(SDLK_q, KEYCODE_Q)
	(SDLK_l, KEYCODE_L)
	(SDLK_k, KEYCODE_K)
	(SDLK_j, KEYCODE_J)
	(SDLK_h, KEYCODE_H)
	(SDLK_g, KEYCODE_G)
	(SDLK_f, KEYCODE_F)
	(SDLK_d, KEYCODE_D)
	(SDLK_s, KEYCODE_S)
	(SDLK_a, KEYCODE_A)
	(SDLK_m, KEYCODE_M)
	(SDLK_n, KEYCODE_N)
	(SDLK_b, KEYCODE_B)
	(SDLK_v, KEYCODE_V)
	(SDLK_c, KEYCODE_C)
	(SDLK_x, KEYCODE_X)
	(SDLK_z, KEYCODE_Z);

