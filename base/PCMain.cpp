// PC implementation of the framework.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <ShellAPI.h>
#else
#include <pwd.h>
#endif

#include <string>

#include "SDL/SDL.h"
#include "SDL/SDL_timer.h"
#include "SDL/SDL_audio.h"

#include "base/display.h"
#include "base/logging.h"
#include "base/timeutil.h"
#include "gfx_es2/glsl_program.h"
#include "file/zip_read.h"
#include "input/input_state.h"
#include "base/NativeApp.h"


// Simple implementations of System functions

void SystemToast(const char *text) {
#ifdef _WIN32
  MessageBox(0, text, "Toast!", MB_ICONINFORMATION);
#else
  puts(text);
#endif
}


void ShowAd(int x, int y, bool center_x) {
	// Ignore ads on PC
}

void ShowKeyboard() {
	// Irrelevant on PC
}

void Vibrate(int length_ms) {
	// Ignore on PC
}

void LaunchBrowser(const char *url)
{
#ifdef _WIN32
	ShellExecute(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#else
#endif
}

void LaunchMarket(const char *url)
{
#ifdef _WIN32
	ShellExecute(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#else
#endif
}

void LaunchEmail(const char *email_address)
{
#ifdef _WIN32
	ShellExecute(NULL, "open", (std::string("mailto:") + email_address).c_str(), NULL, NULL, SW_SHOWNORMAL);
#else
#endif
}



const int buttonMappings[12] = {
	SDLK_x,										//A            
	SDLK_s,										//B            
	SDLK_z,										//X            
	SDLK_a,										//Y            
	SDLK_w,										//LBUMPER
	SDLK_q,										//RBUMPER
	SDLK_1,										//START
	SDLK_2,										//BACK
	SDLK_UP,									//UP    
	SDLK_DOWN,								//DOWN 
	SDLK_LEFT,								//LEFT         
	SDLK_RIGHT,								//RIGHT         
};

void SimulateGamepad(const uint8 *keys, InputState *input) {
  input->pad_buttons = 0;
  input->pad_lstick_x = 0;
  input->pad_lstick_y = 0;
  input->pad_rstick_x = 0;
  input->pad_rstick_y = 0;
	for (int b = 0; b < 12; b++) {
		if (keys[buttonMappings[b]])
			input->pad_buttons |= (1<<b);
	}

	if      (keys['I']) input->pad_lstick_y=32000;
	else if (keys['K']) input->pad_lstick_y=-32000; 
	if      (keys['J']) input->pad_lstick_x=-32000;
	else if (keys['L']) input->pad_lstick_x=32000; 
	if      (keys['8']) input->pad_rstick_y=32000;
	else if (keys['2']) input->pad_rstick_y=-32000; 
	if      (keys['4']) input->pad_rstick_x=-32000;
	else if (keys['6']) input->pad_rstick_x=32000; 
}

extern void mixaudio(void *userdata, Uint8 *stream, int len) {
  NativeMix((short  *)stream, len / 4);
}

#ifdef _WIN32
#undef main
#endif
int main(int argc, char *argv[]) {
  /* // Xoom resolution. Other common tablet resolutions: 1024x600 , 1366x768
  g_xres = 1280;
  g_yres = 800;
  */ 
	std::string app_name;
	std::string app_name_nice;
	bool landscape;
	NativeGetAppInfo(&app_name, &app_name_nice, &landscape);

	if (landscape) {
		g_xres = 800;
		g_yres = 480;
	} else {
		g_xres = 1480;
		g_yres = 800;
	}

	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		fprintf(stderr, "Unable to initialize SDL: %s\n", SDL_GetError());
		return 1;
	}

	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_SWAP_CONTROL, 1);

	if (SDL_SetVideoMode(g_xres, g_yres, 0, SDL_OPENGL) == NULL) {
		fprintf(stderr, "SDL SetVideoMode failed: Unable to create OpenGL screen: %s\n", SDL_GetError());
		SDL_Quit();
		return(2);
	}
	SDL_WM_SetCaption(app_name_nice.c_str(), NULL);

	if (GLEW_OK != glewInit()) {
		printf("Failed to initialize glew!\n");
		return 1;
	}

	if (GLEW_VERSION_2_0) {
		printf("OpenGL 2.0 or higher.\n");
	} else {
		printf("Sorry, this program requires OpenGL 2.0.\n");
		return 1;
	}



#ifdef _MSC_VER
	// VFSRegister("temp/", new DirectoryAssetReader("E:\\Temp\\"));
	TCHAR path[MAX_PATH];
	SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, path);
	PathAppend(path, (app_name + "\\").c_str());
#else
	// Mac - what about linux? Also, ugly hardcoding.
	const char *path = getenv("HOME");
	if (!path) {
		struct passwd* pwd = getpwuid(getuid());
		if (pwd)
			path = pwd->pw_dir;
	}

#endif

	NativeInit(argc, (const char **)argv, path, "BADCOFFEE");
  NativeInitGraphics();

  SDL_AudioSpec fmt;
  fmt.freq = 44100;
  fmt.format = AUDIO_S16;
  fmt.channels = 2;
  fmt.samples = 1024;
  fmt.callback = &mixaudio;
  fmt.userdata = (void *)0;

  if (SDL_OpenAudio(&fmt, NULL) < 0) {
    ELOG("Failed to open audio: %s", SDL_GetError());
    return 1;
  }

  // Audio must be unpaused _after_ NativeInit()
  SDL_PauseAudio(0);

  InputState input_state;
  int framecount = 0;

	while (true) {
		SDL_Event event;

    input_state.accelerometer_valid = false;
    input_state.mouse_valid = true;
		int done = 0;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) {
				done = 1;
			} else if (event.type == SDL_KEYDOWN) {
				if (event.key.keysym.sym == SDLK_ESCAPE) {
					done = 1;
				}
			} else if (event.type == SDL_MOUSEMOTION) {
        input_state.mouse_x[0] = event.motion.x;
        input_state.mouse_y[0] = event.motion.y;
			} else if (event.type == SDL_MOUSEBUTTONDOWN) {
				if (event.button.button == SDL_BUTTON_LEFT) {
          ///input_state.mouse_buttons_down = 1;
          input_state.mouse_down[0] = true;
				}
			} else if (event.type == SDL_MOUSEBUTTONUP) {
				if (event.button.button == SDL_BUTTON_LEFT) {
          input_state.mouse_down[0] = false;
          //input_state.mouse_buttons_up = 1;
        }
			}
		}

		if (done) break;

    input_state.mouse_last[0] = input_state.mouse_down[0];
		uint8 *keys = (uint8 *)SDL_GetKeyState(NULL);
    if (keys[SDLK_ESCAPE]) {
      break;
    }
		SimulateGamepad(keys, &input_state);
    UpdateInputState(&input_state);
    NativeUpdate(input_state);
    NativeRender();
    if (framecount % 60 == 0) {
     // glsl_refresh(); // auto-reloads modified GLSL shaders once per second.
    }
 
		SDL_GL_SwapBuffers();

    // Simple framerate limiting
		static float t=0;
		while (time_now() < t+1.0f/60.0f) {
			sleep_ms(0);
      time_update();
		}
    time_update();
		t = time_now();
    framecount++;
	}
  // Faster exit, thanks to the OS. Remove this if you want to debug shutdown
  exit(0);

  NativeShutdownGraphics();
  SDL_PauseAudio(1);
  NativeShutdown();
  SDL_CloseAudio();
	SDL_Quit();
	return 0;
}
