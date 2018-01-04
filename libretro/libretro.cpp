#include "libretro.h"
#include "libretro_host.h"

#define NO_FBO 1

#include "Common/ChunkFile.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreParameter.h"
#include "Core/CoreTiming.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/HLE/sceUtility.h"
#include "Core/HLE/__sceAudio.h"
#include "Core/HW/MemoryStick.h"
#include "Core/Host.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "Log.h"
#include "LogManager.h"
#include "gfx/gl_common.h"
#include "file/vfs.h"
#include "file/zip_read.h"
#include "GPU/GPUState.h"
#include "GPU/GPUInterface.h"
#include "input/input_state.h"
#include "base/NativeApp.h"
#include "gfx/gl_common.h"
#include "gfx_es2/gpu_features.h"
#include "Common/GraphicsContext.h"
#ifndef NO_FBO
#include "native/thread/thread.h"
#include "native/thread/threadutil.h"
#endif

#include <cstring>
#include <cassert>

#ifdef _WIN32
// for MAX_PATH
#include <windows.h>
#endif

#if defined(MAX_PATH) && !defined(PATH_MAX)
#define PATH_MAX MAX_PATH
#endif

#define SAMPLERATE 44100

retro_log_printf_t log_cb;

class PrintfLogger : public LogListener {
public:
	void Log(const LogMessage &message) {
		switch (message.level) {
		case LogTypes::LVERBOSE:
			if (log_cb)
				log_cb(RETRO_LOG_INFO, "V %s.\n", message.msg.c_str());
			break;
		case LogTypes::LDEBUG:
			if (log_cb)
				log_cb(RETRO_LOG_DEBUG, "D %s.\n", message.msg.c_str());
			break;
		case LogTypes::LINFO:
			if (log_cb)
				log_cb(RETRO_LOG_INFO, "I %s.\n", message.msg.c_str());
			break;
		case LogTypes::LERROR:
			if (log_cb)
				log_cb(RETRO_LOG_ERROR, "E %s.\n", message.msg.c_str());
			break;
		case LogTypes::LWARNING:
			if (log_cb)
				log_cb(RETRO_LOG_WARN, "W %s.\n", message.msg.c_str());
			break;
		case LogTypes::LNOTICE:
		default:
			if (log_cb)
				log_cb(RETRO_LOG_INFO, "N %s.\n", message.msg.c_str());
			break;
		}
	}
};

Draw::DrawContext *libretro_draw;
static bool gl_initialized = false;

class LibretroGLGraphicsContext : public GraphicsContext {
public:
	LibretroGLGraphicsContext()
	{
	}
	bool Init();
	void Shutdown() override;
	void SwapBuffers() override;
	void SwapInterval(int interval) override {}
	void Resize() override {}
	Draw::DrawContext *GetDrawContext() override {
		return NULL;
	}

private:
};

void LibretroGLGraphicsContext::Shutdown() {
#if 0
	NativeShutdownGraphics();
	gl->ClearCurrent();
	gl->Shutdown();
	delete gl;
	finalize_glslang();
#endif
}

void LibretroGLGraphicsContext::SwapBuffers() {
}

static CoreParameter coreParam;
static struct retro_hw_render_callback hw_render;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_environment_t environ_cb;
static bool _initialized;
static bool gpu_refresh = false;
static bool threaded_input = false;

static uint32_t screen_width, screen_height,
                screen_pitch;

static bool first_ctx_reset;

// linker stubs
std::string System_GetProperty(SystemProperty prop) { return ""; }
int System_GetPropertyInt(SystemProperty prop) { return -1; }
void NativeUpdate() { }
void NativeRender(GraphicsContext *graphicsContext) { }
void NativeResized() { }
void NativeMessageReceived(const char *message, const char *value) {}
#if 0
InputState input_state;
#endif

extern "C"
{
retro_hw_get_proc_address_t libretro_get_proc_address;
}

class LibretroHost : public Host {
public:
	LibretroHost() {
	}

	void UpdateUI() override {}

	void UpdateMemView() override {}
	void UpdateDisassembly() override {}

	void SetDebugMode(bool mode) override {}

	bool InitGraphics(std::string *error_message, GraphicsContext **ctx) override { return true; }
	void ShutdownGraphics() override {}

	void InitSound() override { };
	void UpdateSound() override
   {
      static int16_t audio[SAMPLERATE];
#if 0
      int samples = __AudioMix(audio, SAMPLERATE / 2, SAMPLERATE);
#else
      int samples = __AudioMix(audio, 256, SAMPLERATE);
#endif
#if 0
      if (log_cb)
         log_cb(RETRO_LOG_INFO, "samples: %d\n", samples);
#endif
      audio_batch_cb(audio, samples);
   }
	void ShutdownSound() override {};

	void BootDone() override {}

	bool IsDebuggingEnabled() override { return false; }
	bool AttemptLoadSymbolMap() override { return false; }
	void SetWindowTitle(const char *message) override {}
};

void retro_set_environment(retro_environment_t cb)
{
   static const struct retro_variable vars[] = {
      { "ppsspp_cpu_core", "CPU Core; jit|interpreter" },
      { "ppsspp_locked_cpu_speed", "Locked CPU Speed; off|222MHz|266MHz|333MHz" },
      { "ppsspp_language", "Language; automatic|english|japanese|french|spanish|german|italian|dutch|portuguese|russian|korean|chinese_traditional|chinese_simplified" },
      { "ppsspp_rendering_mode", "Rendering Mode; buffered|nonbuffered|read_framebuffers_to_memory_cpu|read_framebuffers_to_memory_gpu" },
      { "ppsspp_auto_frameskip", "Auto Frameskip; disabled|enabled" },
      { "ppsspp_frameskip", "Frameskip; 0|1|2|3|4|5|6|7|8|9" },
      { "ppsspp_framerate_limit", "Framerate limit; 0|15|20|30|45|50|60" },
      { "ppsspp_force_max_fps", "Force Max FPS; disabled|enabled" },
      { "ppsspp_audio_latency", "Audio latency; 0|1|2" },
      { "ppsspp_internal_resolution",
         "Internal Resolution ; 480x272|960x544|1440x816|1920x1088|2400x1360|2880x1632|3360x1904|3840x2176|4320x2448|4800x2720"
      },
      { "ppsspp_output_resolution",
         "Output Resolution (restart); 480x272|960x544|1440x816|1920x1088|2400x1360|2880x1632|3360x1904|3840x2176|4320x2448|4800x2720"
      },
      { "ppsspp_button_preference", "Confirmation Button; cross|circle" },
      { "ppsspp_fast_memory", "Fast Memory (Speedhack); enabled|disabled" },
      { "ppsspp_set_rounding_mode", "Set Rounding Mode; enabled|disabled" },
      { "ppsspp_block_transfer_gpu", "Block Transfer GPU; enabled|disabled" },
      { "ppsspp_texture_scaling_level", "Texture Scaling Level; 1|2|3|4|5|0" },
      { "ppsspp_texture_scaling_type", "Texture Scaling Type; xbrz|hybrid|bicubic|hybrid_bicubic" },
#ifdef USING_GLES2
      // TODO/FIXME - several GLES2-only devices actually have
      // GL_EXT_texture_filter_anisotropic extension, so check
      // if extension is available at runtime
      { "ppsspp_texture_anisotropic_filtering", "Anisotropic Filtering; off" },
#else
      { "ppsspp_texture_anisotropic_filtering", "Anisotropic Filtering; off|1x|2x|4x|8x|16x" },
#endif
      { "ppsspp_texture_deposterize", "Texture Deposterize; disabled|enabled" },
	  { "ppsspp_internal_shader", "Internal Shader; off|fxaa|crt|natural|vignette|grayscale|bloom|sharpen|inverse|scanlines|cartoon|4xHQ|aa-color|upscale" },
      { "ppsspp_gpu_hardware_transform", "GPU Hardware T&L; enabled|disabled" },
      { "ppsspp_vertex_cache", "Vertex Cache (Speedhack); enabled|disabled" },
      { "ppsspp_prescale_uv", "Prescale UV (Speedhack); disabled|enabled" },
      { "ppsspp_separate_io_thread", "IO Threading; disabled|enabled" },
      { "ppsspp_unsafe_func_replacements", "Unsafe FuncReplacements; enabled|disabled" },
      { "ppsspp_sound_speedhack", "Sound Speedhack; disabled|enabled" },
	  { "ppsspp_threaded_input", "Threaded input hack; disabled|enabled" },
      { NULL, NULL },
   };

   environ_cb = cb;

   cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
   (void)cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

std::string retro_base_dir;
std::string retro_save_dir;
bool retro_base_dir_found;
bool retro_save_dir_found;

void retro_init(void)
{
   const char *dir_ptr = NULL;
   struct retro_log_callback log;

   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      log_cb = log.log;
   else
      log_cb = NULL;

   retro_base_dir_found = false;
   retro_save_dir_found = false;

   if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir_ptr) && dir_ptr)
   {
      retro_base_dir = dir_ptr;
      // Make sure that we don't have any lingering slashes, etc, as they break Windows.
      size_t last = retro_base_dir.find_last_not_of("/\\");
      if (last != std::string::npos)
         last++;

      retro_base_dir = retro_base_dir.substr(0, last);
      retro_base_dir_found = true;
   }

   if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir_ptr) && dir_ptr)
   {
      retro_save_dir = dir_ptr;
      // Make sure that we don't have any lingering slashes, etc, as they break Windows.
      size_t last = retro_save_dir.find_last_not_of("/\\");
      if (last != std::string::npos)
         last++;

      retro_save_dir = retro_save_dir.substr(0, last);
      retro_save_dir_found = true;
   }

#ifdef IOS
   extern bool iosCanUseJit;
   iosCanUseJit = true;
#endif
}

void retro_deinit(void) {
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   (void)port;
   (void)device;
}

void retro_get_system_info(struct retro_system_info *info)
{
   char str[256];
   memset(info, 0, sizeof(*info));
   info->library_name     = "PPSSPP";
#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif
   sprintf(str, "%s", PPSSPP_GIT_VERSION);
   info->library_version  = strdup(str);
   info->need_fullpath    = true;
   info->valid_extensions = "elf|iso|cso|prx|pbp";
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   info->timing.fps = 60.0f / 1.001f;
   info->timing.sample_rate = SAMPLERATE;

   info->geometry.base_width = screen_width;
   info->geometry.base_height = screen_height;
   info->geometry.max_width = screen_width;
   info->geometry.max_height = screen_height;
   info->geometry.aspect_ratio = 16.0 / 9.0;
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

static void extract_directory(char *buf, const char *path, size_t size)
{
   char *base;
   strncpy(buf, path, size - 1);
   buf[size - 1] = '\0';

   base = strrchr(buf, '/');
   if (!base)
      base = strrchr(buf, '\\');

   if (base)
      *base = '\0';
   else
   {
      buf[0] = '.';
      buf[1] = '\0';
   }
}

static void initialize_gl(void)
{
#if !defined(IOS) && !defined(USING_GLES2)
   if (glewInit() != GLEW_OK)
   {
      if (log_cb)
         log_cb(RETRO_LOG_ERROR, "glewInit() failed.\n");
      environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, nullptr);
      return;
   }
#endif
#if 0
   glstate.Initialize();
#endif
   CheckGLExtensions();
   gl_initialized = true;
}

static void context_reset(void)
{
   if (!first_ctx_reset)
   {
      if (log_cb)
         log_cb(RETRO_LOG_INFO, "Context reset!\n");

      if (gpu)
         gpu->DeviceLost();

      //RecreateViews(); /* TODO ? */

      //gl_lost(); /* Removed in PPSSPP upstream */

      initialize_gl();
#if 0
      glstate.Restore();
#endif
   }

   first_ctx_reset = false;
}

static void set_language_auto(void)
{
   unsigned int val = 1;

   if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &val))
   {
      // PPSSPP language values for these two languages
      // differ from the RETRO_LANGUAGE enum values
      if (val == RETRO_LANGUAGE_ENGLISH)
         val = 1;
      else if (val == RETRO_LANGUAGE_JAPANESE)
         val = 0;
   }
   else
      val = 1;

   g_Config.iLanguage = val;
}

static void check_variables(void)
{
   struct retro_variable var;

   var.key = "ppsspp_internal_resolution";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (sscanf(var.value ? var.value : "480x272", "%dx%d", &coreParam.renderWidth, &coreParam.renderHeight) != 2)
      {
         coreParam.renderWidth = 480;
         coreParam.renderHeight = 272;
      }

	  gpu_refresh = true;
   }
   else
   {
      coreParam.renderWidth = 480;
      coreParam.renderHeight = 272;
   }

   var.key = "ppsspp_output_resolution";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (sscanf(var.value ? var.value : "480x272", "%dx%d", &screen_width, &screen_height) != 2)
      {
         screen_width = 480;
         screen_height = 272;

		 gpu_refresh = true;
      }
   }
   else
   {
      screen_width = 480;
      screen_height = 272;
   }

   coreParam.pixelWidth = screen_width;
   coreParam.pixelHeight = screen_height;

   var.key = "ppsspp_button_preference";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "cross") == 0)
         g_Config.iButtonPreference = PSP_SYSTEMPARAM_BUTTON_CROSS;
      else if (strcmp(var.value, "circle") == 0)
         g_Config.iButtonPreference = PSP_SYSTEMPARAM_BUTTON_CIRCLE;
   }
   else
         g_Config.iButtonPreference = PSP_SYSTEMPARAM_BUTTON_CROSS;

   var.key = "ppsspp_fast_memory";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         g_Config.bFastMemory = true;
      else if (!strcmp(var.value, "disabled"))
         g_Config.bFastMemory = false;
   }
   else
         g_Config.bFastMemory = true;

#if 0
   var.key = "ppsspp_set_rounding_mode";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         g_Config.bSetRoundingMode = true;
      else if (!strcmp(var.value, "disabled"))
         g_Config.bSetRoundingMode = false;
   }
   else
         g_Config.bSetRoundingMode = false;
#endif

   var.key = "ppsspp_vertex_cache";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         g_Config.bVertexCache = true;
      else if (!strcmp(var.value, "disabled"))
         g_Config.bVertexCache = false;
   }
   else
         g_Config.bVertexCache = true;

   var.key = "ppsspp_gpu_hardware_transform";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         g_Config.bHardwareTransform = true;
      else if (!strcmp(var.value, "disabled"))
         g_Config.bHardwareTransform = false;
   }
   else
         g_Config.bHardwareTransform = true;

   var.key = "ppsspp_frameskip";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int new_val = atoi(var.value);
      g_Config.iFrameSkip = new_val;
   }
   else
      g_Config.iFrameSkip = 0;

   var.key = "ppsspp_audio_latency";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int new_val = atoi(var.value);
      g_Config.iAudioLatency = new_val;
   }
   else
      g_Config.iAudioLatency = 0;

   var.key = "ppsspp_framerate_limit";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int new_val = atoi(var.value);
      g_Config.iFpsLimit = new_val;
   }
   else
      g_Config.iFpsLimit = 0;

   var.key = "ppsspp_language";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "automatic"))
         set_language_auto();
      else if (!strcmp(var.value, "japanese"))
         g_Config.iLanguage = 0;
      else if (!strcmp(var.value, "english"))
         g_Config.iLanguage = 1;
      else if (!strcmp(var.value, "french"))
         g_Config.iLanguage = 2;
      else if (!strcmp(var.value, "spanish"))
         g_Config.iLanguage = 3;
      else if (!strcmp(var.value, "german"))
         g_Config.iLanguage = 4;
      else if (!strcmp(var.value, "italian"))
         g_Config.iLanguage = 5;
      else if (!strcmp(var.value, "dutch"))
         g_Config.iLanguage = 6;
      else if (!strcmp(var.value, "portuguese"))
         g_Config.iLanguage = 7;
      else if (!strcmp(var.value, "russian"))
         g_Config.iLanguage = 8;
      else if (!strcmp(var.value, "korean"))
         g_Config.iLanguage = 9;
      else if (!strcmp(var.value, "chinese_traditional"))
         g_Config.iLanguage = 10;
      else if (!strcmp(var.value, "chinese_simplified"))
         g_Config.iLanguage = 11;
   }
   else
      set_language_auto();

   var.key = "ppsspp_auto_frameskip";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         g_Config.bAutoFrameSkip = true;
      else if (!strcmp(var.value, "disabled"))
         g_Config.bAutoFrameSkip = false;
   }
   else
         g_Config.bAutoFrameSkip = false;

   var.key = "ppsspp_block_transfer_gpu";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         g_Config.bBlockTransferGPU = true;
      else if (!strcmp(var.value, "disabled"))
         g_Config.bBlockTransferGPU = false;
   }
   else
      g_Config.bBlockTransferGPU = true;

   var.key = "ppsspp_texture_scaling_type";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "xbrz"))
         g_Config.iTexScalingType = 0;
      else if (!strcmp(var.value, "hybrid"))
         g_Config.iTexScalingType = 1;
      else if (!strcmp(var.value, "bicubic"))
         g_Config.iTexScalingType = 2;
      else if (!strcmp(var.value, "hybrid_bicubic"))
         g_Config.iTexScalingType = 3;

	  gpu_refresh = true;
   }
   else
      g_Config.iTexScalingType = 0;

   var.key = "ppsspp_texture_scaling_level";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int new_val = atoi(var.value);
      g_Config.iTexScalingLevel = new_val;

	  gpu_refresh = true;
   }
   else
      g_Config.iTexScalingLevel = 1;

   var.key = "ppsspp_internal_shader";
   var.value = NULL;
   //off|fxaa|crt|natural|vignette|grayscale|bloom|sharpen|inverse|scanlines|cartoon|4xHQ|aa-color|upscale
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
	   if (!strcmp(var.value, "off"))
		   g_Config.sPostShaderName = "";
	   else if (!strcmp(var.value, "crt"))
		   g_Config.sPostShaderName = "CRT";
	   else if (!strcmp(var.value, "natural"))
		   g_Config.sPostShaderName = "Natural";
	   else if (!strcmp(var.value, "vignette"))
		   g_Config.sPostShaderName = "Vignette";
	   else if (!strcmp(var.value, "grayscale"))
		   g_Config.sPostShaderName = "Grayscale";
	   else if (!strcmp(var.value, "bloom"))
		   g_Config.sPostShaderName = "Bloom";
	   else if (!strcmp(var.value, "sharpen"))
		   g_Config.sPostShaderName = "Sharpen";
	   else if (!strcmp(var.value, "inverse"))
		   g_Config.sPostShaderName = "InverseColors";
	   else if (!strcmp(var.value, "scanlines"))
		   g_Config.sPostShaderName = "Scanlines";
	   else if (!strcmp(var.value, "cartoon"))
		   g_Config.sPostShaderName = "Cartoon";
	   else if (!strcmp(var.value, "4xHQ"))
		   g_Config.sPostShaderName = "4xHqGLSL";
	   else if (!strcmp(var.value, "AAColor"))
		   g_Config.sPostShaderName = "AA Color";
	   else if (!strcmp(var.value, "upscale"))
		   g_Config.sPostShaderName = "UpscaleSpline36";
	   else
		   g_Config.sPostShaderName = "";

	   gpu_refresh = true;
   }
   else
	   g_Config.sPostShaderName = "Off";

   var.key = "ppsspp_texture_anisotropic_filtering";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
#ifdef USING_GLES2
      // TODO/FIXME - several GLES2-only devices actually have
      // GL_EXT_texture_filter_anisotropic extension, so check
      // if extension is available at runtime
      g_Config.iAnisotropyLevel = 0;
#else
      if (!strcmp(var.value, "off"))
         g_Config.iAnisotropyLevel = 0;
      else if (!strcmp(var.value, "1x"))
         g_Config.iAnisotropyLevel = 1;
      else if (!strcmp(var.value, "2x"))
         g_Config.iAnisotropyLevel = 2;
      else if (!strcmp(var.value, "4x"))
         g_Config.iAnisotropyLevel = 3;
      else if (!strcmp(var.value, "8x"))
         g_Config.iAnisotropyLevel = 4;
      else if (!strcmp(var.value, "16x"))
         g_Config.iAnisotropyLevel = 5;
#endif
   }
   else
      g_Config.iAnisotropyLevel = 0;

   var.key = "ppsspp_texture_deposterize";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         g_Config.bTexDeposterize = true;
      else if (!strcmp(var.value, "disabled"))
         g_Config.bTexDeposterize = false;

	  gpu_refresh = true;
   }
   else
      g_Config.bTexDeposterize = false;

   var.key = "ppsspp_separate_io_thread";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         g_Config.bSeparateIOThread = true;
      else if (!strcmp(var.value, "disabled"))
         g_Config.bSeparateIOThread = false;
   }
   else
      g_Config.bSeparateIOThread = false;

   var.key = "ppsspp_unsafe_func_replacements";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         g_Config.bFuncReplacements = true;
      else if (!strcmp(var.value, "disabled"))
         g_Config.bFuncReplacements = false;
   }
   else
         g_Config.bFuncReplacements = true;

   var.key = "ppsspp_sound_speedhack";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         g_Config.bSoundSpeedHack = true;
      else if (!strcmp(var.value, "disabled"))
         g_Config.bSoundSpeedHack = false;
   }
   else
      g_Config.bSoundSpeedHack = false;

   var.key = "ppsspp_cpu_core";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "jit"))
         coreParam.cpuCore = CPUCore::JIT;
      else if (!strcmp(var.value, "interpreter"))
         coreParam.cpuCore = CPUCore::INTERPRETER;
   }
   else
      coreParam.cpuCore = CPUCore::JIT;

   var.key = "ppsspp_locked_cpu_speed";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "off"))
         g_Config.iLockedCPUSpeed = 0;
      else if (!strcmp(var.value, "222MHz"))
         g_Config.iLockedCPUSpeed = 222;
      else if (!strcmp(var.value, "266MHz"))
         g_Config.iLockedCPUSpeed = 266;
      else if (!strcmp(var.value, "333MHz"))
         g_Config.iLockedCPUSpeed = 333;
   }
   else
      g_Config.iLockedCPUSpeed = 0;

   var.key = "ppsspp_rendering_mode";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "nonbuffered"))
         g_Config.iRenderingMode = 0;
      else if (!strcmp(var.value, "buffered"))
         g_Config.iRenderingMode = 1;
      else if (!strcmp(var.value, "read_framebuffers_to_memory_cpu"))
         g_Config.iRenderingMode = 2;
      else if (!strcmp(var.value, "read_framebuffers_to_memory_gpu"))
         g_Config.iRenderingMode = 3;
   }
   else
      g_Config.iRenderingMode = 1;

   var.key = "ppsspp_force_max_fps";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.iForceMaxEmulatedFPS = 0;
      else if (!strcmp(var.value, "enabled"))
         g_Config.iForceMaxEmulatedFPS = 60;
   }
   else
      g_Config.iForceMaxEmulatedFPS = 0;

#if 0
   var.key = "ppsspp_prescale_uv";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         g_Config.bPrescaleUV = 1;
      else if (!strcmp(var.value, "disabled"))
         g_Config.bPrescaleUV = 0;
   }
   else
      g_Config.bPrescaleUV = 0;
#endif

   var.key = "ppsspp_threaded_input";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
	   if (!strcmp(var.value, "enabled"))
		   threaded_input = true;
	   else if (!strcmp(var.value, "disabled"))
		   threaded_input = false;
   }
   else
	   threaded_input = false;
}

bool retro_load_game(const struct retro_game_info *game)
{
   const char *tmp = NULL;
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
#ifdef _WIN32
   char slash = '\\';
#else
   char slash = '/';
#endif

   struct retro_input_descriptor desc[] = {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Cross" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Circle" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Triangle" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Square" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Select" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Analog X" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Analog Y" },

      { 0 },
   };

   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

   if (!retro_base_dir_found)
   {
      char _dir[PATH_MAX];
      extract_directory(_dir, game->path, sizeof(_dir));
      retro_base_dir = std::string(_dir);
   }

   if (!retro_save_dir_found)
   {
      char _dir[PATH_MAX];
      extract_directory(_dir, game->path, sizeof(_dir));
      retro_save_dir = std::string(_dir);
   }

   retro_base_dir += slash;
   retro_base_dir += "PPSSPP";
   retro_base_dir += slash;

   retro_save_dir += slash;

   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      if (log_cb)
         log_cb(RETRO_LOG_ERROR, "XRGB8888 is not supported.\n");
      return false;
   }

   first_ctx_reset = true;

#ifdef USING_GLES2
   hw_render.context_type = RETRO_HW_CONTEXT_OPENGLES2;
#else
   hw_render.context_type = RETRO_HW_CONTEXT_OPENGL;
#endif
   hw_render.context_reset = context_reset;
   hw_render.bottom_left_origin = true;
   hw_render.depth = true;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
      return false;

   libretro_get_proc_address = hw_render.get_proc_address;

   VFSRegister("", new DirectoryAssetReader(retro_base_dir.c_str()));

   host = new LibretroHost;

   // We do this here, instead of in NativeInitGraphics, because the display may be reset.
   // When it's reset we don't want to forget all our managed things.
   //gl_lost_manager_init(); /* Removed in PPSSPP upstream */

   LogManager::Init();
   LogManager *logman = LogManager::GetInstance();

   PrintfLogger *printfLogger = new PrintfLogger();

   bool fullLog = true;
   for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; i++) {
	   LogTypes::LOG_TYPE type = (LogTypes::LOG_TYPE)i;
	   logman->SetEnabled(type, fullLog);
	   logman->SetLogLevel(type, LogTypes::LDEBUG);
   }
   logman->AddListener(printfLogger);

#if 0
   g_Config.Load("");
#endif

   g_Config.currentDirectory      = retro_base_dir;
   g_Config.externalDirectory     = retro_base_dir;
   g_Config.memStickDirectory     = retro_save_dir;
   g_Config.flash0Directory       = retro_base_dir + "flash0/";
   g_Config.internalDataDirectory = retro_base_dir;
   g_Config.iShowFPSCounter = false;
   g_Config.bFrameSkipUnthrottle = false;
   g_Config.bVSync = false;
   g_Config.bEnableLogging = true;
   g_Config.bMemStickInserted = PSP_MEMORYSTICK_STATE_INSERTED;

   if (environ_cb(RETRO_ENVIRONMENT_GET_USERNAME, &tmp) && tmp)
      g_Config.sNickName = std::string(tmp);

   coreParam.gpuCore     = GPUCORE_GLES;
   coreParam.cpuCore     = CPUCore::JIT;
   coreParam.graphicsContext = new LibretroGLGraphicsContext;
   coreParam.enableSound = true;
   coreParam.fileToStart = std::string(game->path);
   coreParam.mountIso = "";
   coreParam.startPaused = false;
   coreParam.printfEmuLog = true;
   coreParam.headLess = true;
   coreParam.unthrottle = true;

   g_Config.iGlobalVolume = VOLUME_MAX - 1;
   g_Config.bEnableSound  = true;
   g_Config.bAudioResampler = false;
   _initialized = false;
   check_variables();

#if 0
   g_Config.bVertexDecoderJit = (coreParam.cpuCore == CPU_JIT) ? true : false;
#endif

   return true;
}

static bool should_reset = false;

void retro_reset(void)
{
   should_reset = true;
}

const int buttonMap[] =
{
   CTRL_UP,
   CTRL_DOWN,
   CTRL_LEFT,
   CTRL_RIGHT,
   CTRL_TRIANGLE,
   CTRL_CIRCLE,
   CTRL_CROSS,
   CTRL_SQUARE,
   CTRL_LTRIGGER,
   CTRL_RTRIGGER,
   CTRL_START,
   CTRL_SELECT
};

static void retro_input(void)
{
   int i;
   float analogX, analogY;

   static unsigned map[] = {
      RETRO_DEVICE_ID_JOYPAD_UP,
      RETRO_DEVICE_ID_JOYPAD_DOWN,
      RETRO_DEVICE_ID_JOYPAD_LEFT,
      RETRO_DEVICE_ID_JOYPAD_RIGHT,
      RETRO_DEVICE_ID_JOYPAD_X,
      RETRO_DEVICE_ID_JOYPAD_A,
      RETRO_DEVICE_ID_JOYPAD_B,
      RETRO_DEVICE_ID_JOYPAD_Y,
      RETRO_DEVICE_ID_JOYPAD_L,
      RETRO_DEVICE_ID_JOYPAD_R,
      RETRO_DEVICE_ID_JOYPAD_START,
      RETRO_DEVICE_ID_JOYPAD_SELECT,
   };

	if (coreState == CORE_POWERDOWN)
      return;

   for (i = 0; i < 12; i++)
   {
      if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, map[i]))
         __CtrlButtonDown(buttonMap[i]);
      else
         __CtrlButtonUp  (buttonMap[i]);
   }

   analogX = (float)input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X) / 32768.0f;
   analogY = (float)input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y) / -32768.0f;
   __CtrlSetAnalogX(analogX);
   __CtrlSetAnalogY(analogY);
}

#if 0
static std::thread *input_thread = NULL;
#endif
static bool running = false;

static inline void rarch_sleep(unsigned msec)
{
#if defined(__CELLOS_LV2__) && !defined(__PSL1GHT__)
   sys_timer_usleep(1000 * msec);
#elif defined(PSP)
   sceKernelDelayThread(1000 * msec);
#elif defined(_WIN32)
   Sleep(msec);
#elif defined(XENON)
   udelay(1000 * msec);
#elif defined(GEKKO) || defined(__PSL1GHT__) || defined(__QNX__)
   usleep(1000 * msec);
#else
   struct timespec tv = {0};
   tv.tv_sec = msec / 1000;
   tv.tv_nsec = (msec % 1000) * 1000000;
   nanosleep(&tv, NULL);
#endif
}

#if 0
void retro_input_poll_thread()
{
	setCurrentThreadName("Input Thread");

	while (threaded_input)
   {
      if (input_poll_cb)
         input_poll_cb();
      retro_input();
      rarch_sleep(4);
   }
}
#endif

void retro_run(void)
{
   bool updated = false;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
   {
	   check_variables();
	   if (gpu_refresh)
	   {
		   switch (coreParam.renderWidth)
		   {
			   case 480:
				   g_Config.iInternalResolution = 1;
				   break;
			   case 960:
				   g_Config.iInternalResolution = 2;
				   break;
			   case 1440:
				   g_Config.iInternalResolution = 3;
				   break;
			   case 1920:
				   g_Config.iInternalResolution = 4;
				   break;
			   case 2400:
				   g_Config.iInternalResolution = 5;
				   break;
			   case 2880:
				   g_Config.iInternalResolution = 6;
				   break;
			   case 3360:
				   g_Config.iInternalResolution = 7;
				   break;
			   case 3840:
				   g_Config.iInternalResolution = 8;
				   break;
			   case 4320:
				   g_Config.iInternalResolution = 9;
				   break;
			   case 4800:
				   g_Config.iInternalResolution = 10;
				   break;

		   }

		   if (gpu)
		   {
			   gpu->ClearCacheNextFrame();
			   gpu->Resized();
			   gpu_refresh = false;
		   }
	   }
   }

   if (should_reset)
      PSP_Shutdown();

   if (!_initialized || should_reset)
   {
      should_reset = false;

      if (!gl_initialized)
      {
         initialize_gl();
      }

      std::string error_string;
      if (gl_initialized)
      {
	      libretro_draw         = Draw::T3DCreateGLContext();
	      coreParam.thin3d      = libretro_draw;

	      bool success = libretro_draw->CreatePresets();
	      assert(success);

	      bool bootPending_ = !PSP_Init(coreParam, &error_string);

	      if(PSP_IsIniting())
	      {
		     bootPending_ = !PSP_InitUpdate(&error_string);

		     if(!bootPending_)
		     {
			_initialized = !PSP_IsInited();
			if (!_initialized)
			{
				if (log_cb)
					log_cb(RETRO_LOG_ERROR, "PSP_Init() failed: %s.\n", error_string.c_str());
				environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, nullptr);
			}
		     }
	      }

	      host->BootDone();
	      _initialized = true;
	      coreState = CORE_RUNNING;
	      extern GLuint g_defaultFBO;
	      g_defaultFBO = hw_render.get_current_framebuffer();
      }

   }

   PSP_BeginHostFrame();

#if 0
   if (log_cb)
      //log_cb(RETRO_LOG_INFO, "Locked CPU Speed: %d\n", g_Config.iLockedCPUSpeed);
      //log_cb(RETRO_LOG_INFO, "Audio Latency: %d\n", g_Config.iAudioLatency);
      //log_cb(RETRO_LOG_INFO, "Rendering Mode: %d\n", g_Config.iRenderingMode);
      log_cb(RETRO_LOG_INFO, "Function replacements: %d\n", g_Config.bFuncReplacements);
#endif

   if (_initialized)
   {
#if 0
	   if (threaded_input)
	   {
		   if (!input_thread)
			   input_thread = new std::thread(&retro_input_poll_thread);
	   }
	   else
#endif
	   {
		   if (input_poll_cb)
			   input_poll_cb();
		   retro_input();
	   }

	   // We just run the CPU until we get to vblank. This will quickly sync up pretty nicely.
	   // The actual number of cycles doesn't matter so much here as we will break due to CORE_NEXTFRAME, most of the time hopefully...
	   int blockTicks = usToCycles(1000000 / 10);
	   PSP_RunLoopFor(blockTicks);

	   // Hopefully coreState is now CORE_NEXTFRAME
	   if (coreState == CORE_NEXTFRAME)
		   // set back to running for the next frame
		   coreState = CORE_RUNNING;

#ifndef NO_FBO
	   bool useBufferedRendering = g_Config.iRenderingMode != 0;
	   if (useBufferedRendering)
		   fbo_unbind();
#endif
   }

   video_cb(((gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME) == 0) ? NULL : RETRO_HW_FRAME_BUFFER_VALID, screen_width, screen_height, 0);
   PSP_EndHostFrame();
}

void retro_unload_game(void)
{
   if (threaded_input)
      threaded_input = false;

	PSP_Shutdown();
	VFSShutdown();

	delete libretro_draw;
	libretro_draw = nullptr;

#if 0
	if (input_thread)
	{
      input_thread->join();
		delete input_thread;
		input_thread = NULL;
	}
#endif
}
unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
   (void)type;
   (void)info;
   (void)num;
   return false;
}

size_t retro_serialize_size(void)
{
   // TODO: what is a sane maximum size?
   return 96 * 1024 * 1024;
}

bool retro_serialize(void *data, size_t size)
{
   std::vector<u8> state;

   if (!_initialized)
      return false;

   if (SaveState::SaveToRam(state) == CChunkFileReader::ERROR_NONE &&
       size >= (sizeof(uint32_t) + state.size()*sizeof(u8)))
   {
      static_cast<uint32_t*>(data)[0] = state.size();
      std::memcpy(static_cast<uint32_t*>(data)+1, state.data(), state.size()*sizeof(u8));
      return true;
   }
      return false;
}

bool retro_unserialize(const void *data, size_t size)
{
   if (size < static_cast<uint32_t const*>(data)[0]*sizeof(u8) + sizeof(uint32_t))
      return false;

   u8 const* state_data = static_cast<u8 const*>(data)+4;
   std::vector<u8> state(state_data, state_data+static_cast<uint32_t const*>(data)[0]);
   return SaveState::LoadFromRam(state) == CChunkFileReader::ERROR_NONE;
}

void *retro_get_memory_data(unsigned id)
{
   (void)id;
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
   (void)id;
   return 0;
}

void retro_cheat_reset(void)
{}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index;
   (void)enabled;
   (void)code;
}

void System_SendMessage(const char *command, const char *parameter) {}
