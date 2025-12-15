#include "ppsspp_config.h"
#include <cstring>
#include <cassert>
#include <thread>
#include <atomic>
#include <vector>
#include <cstdlib>
#include <mutex>

#include "Common/CPUDetect.h"
#include "Common/Log.h"
#include "Common/Log/LogManager.h"
#include "Common/System/Display.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/TimeUtil.h"
#include "Common/File/FileUtil.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Input/InputState.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Thread/ThreadManager.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/VFS/DirectoryReader.h"
#include "Common/Data/Text/I18n.h"
#include "Common/StringUtils.h"

#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Core.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceUtility.h"
#include "Core/HLE/__sceAudio.h"
#include "Core/HW/MemoryStick.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/CoreTiming.h"
#include "Core/HW/Display.h"
#include "Core/CwCheat.h"
#include "Core/ELF/ParamSFO.h"

#include "GPU/GPUState.h"
#include "GPU/GPUCommon.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/TextureScalerCommon.h"
#include "GPU/Common/PresentationCommon.h"

#include "UI/AudioCommon.h"

#include "libretro/libretro.h"
#include "libretro/LibretroGraphicsContext.h"
#include "libretro/libretro_core_options.h"

#if PPSSPP_PLATFORM(ANDROID)
#include <sys/system_properties.h>
#endif

#define DIR_SEP "/"
#ifdef _WIN32
#define DIR_SEP_CHRS "/\\"
#else
#define DIR_SEP_CHRS "/"
#endif

#ifdef HAVE_LIBRETRO_VFS
#include "streams/file_stream.h"
#endif

#define SAMPLERATE 44100

/* AUDIO output buffer */
static struct {
   int16_t *data;
   int32_t size;
   int32_t capacity;
} output_audio_buffer = {NULL, 0, 0};

// Calculated swap interval is 'stable' if the same
// value is recorded for a number of retro_run()
// calls equal to VSYNC_SWAP_INTERVAL_FRAMES
#define VSYNC_SWAP_INTERVAL_FRAMES 6
// Calculated swap interval is 'valid' if it is
// within VSYNC_SWAP_INTERVAL_THRESHOLD of an integer
// value
#define VSYNC_SWAP_INTERVAL_THRESHOLD 0.05f
// Swap interval detection is only enabled if the
// core is running at 'normal' speed - i.e. if
// run speed is within VSYNC_SWAP_INTERVAL_RUN_SPEED_THRESHOLD
// percent of 100
#define VSYNC_SWAP_INTERVAL_RUN_SPEED_THRESHOLD 5.0f

static bool libretro_supports_bitmasks = false;
static bool show_ip_address_options = true;
static bool show_upnp_port_option = true;
static bool show_detect_frame_rate_option = true;
static std::string changeProAdhocServer;

void* unserialize_data = NULL;
size_t unserialize_size = 0;

namespace Libretro
{
   LibretroGraphicsContext *ctx;
   retro_environment_t environ_cb;
   retro_hw_context_type backend = RETRO_HW_CONTEXT_DUMMY;
   static retro_audio_sample_batch_t audio_batch_cb;
   static retro_input_poll_t input_poll_cb;
   static retro_input_state_t input_state_cb;
   static retro_log_printf_t log_cb;

   bool g_pendingBoot = false;
   std::string g_bootErrorString;

   static bool detectVsyncSwapInterval = false;
   static bool detectVsyncSwapIntervalOptShown = true;
   static bool softwareRenderInitHack = false;

   static s64 expectedTimeUsPerRun = 0;
   static uint32_t vsyncSwapInterval = 1;
   static uint32_t vsyncSwapIntervalLast = 1;
   static uint32_t vsyncSwapIntervalCounter = 0;
   static int numVBlanksLast = 0;
   static double fpsTimeLast = 0.0;
   static float runSpeed = 0.0f;
   static s64 runTicksLast = 0;

   static void ensure_output_audio_buffer_capacity(int32_t capacity)
   {
      if (capacity <= output_audio_buffer.capacity) {
         return;
      }

      output_audio_buffer.data = (int16_t*)realloc(output_audio_buffer.data, capacity * sizeof(*output_audio_buffer.data));
      output_audio_buffer.capacity = capacity;
      log_cb(RETRO_LOG_DEBUG, "Output audio buffer capacity set to %d\n", capacity);
   }

   static void init_output_audio_buffer(int32_t capacity)
   {
      output_audio_buffer.data = NULL;
      output_audio_buffer.size = 0;
      output_audio_buffer.capacity = 0;
      ensure_output_audio_buffer_capacity(capacity);
   }

   static void free_output_audio_buffer()
   {
      free(output_audio_buffer.data);
      output_audio_buffer.data = NULL;
      output_audio_buffer.size = 0;
      output_audio_buffer.capacity = 0;
   }

   static void upload_output_audio_buffer()
   {
      audio_batch_cb(output_audio_buffer.data, output_audio_buffer.size / 2);
      output_audio_buffer.size = 0;
   }


   /**
    * Clamp a value to a given range.
    *
    * This implementation was taken from `RGBAUtil.cpp` to allow building when `std::clamp()` is unavailable.
    *
    * @param f The value to clamp.
    * @param low The lower bound of the range.
    * @param high The upper bound of the range.
    * @return The clamped value.
    */
   template <typename T>
   static T clamp(T f, T low, T high) {
      if (f < low)
         return low;
      if (f > high)
         return high;
      return f;
   }

   static void VsyncSwapIntervalReset()
   {
      expectedTimeUsPerRun = (s64)(1000000.0f / (60.0f / 1.001f));
      vsyncSwapInterval = 1;
      vsyncSwapIntervalLast = 1;
      vsyncSwapIntervalCounter = 0;

      numVBlanksLast = 0;
      fpsTimeLast = 0.0;
      runSpeed = 0.0f;
      runTicksLast = 0;

      detectVsyncSwapIntervalOptShown = true;
   }

   static void VsyncSwapIntervalDetect()
   {
      if (!detectVsyncSwapInterval)
         return;

      // All bets are off if core is running at
      // the 'wrong' speed (i.e. cycle count for
      // this run will be meaningless if internal
      // frame rate is dropping below expected
      // value, or fast forward is enabled)
      double fpsTime = time_now_d();
      int numVBlanks = __DisplayGetNumVblanks();
      int frames = numVBlanks - numVBlanksLast;

      if (frames >= VSYNC_SWAP_INTERVAL_FRAMES << 1)
      {
         double fps = (double)frames / (fpsTime - fpsTimeLast);
         runSpeed = fps / ((60.0f / 1.001f) / 100.0f);

         fpsTimeLast = fpsTime;
         numVBlanksLast = numVBlanks;
      }

      float speedDelta = 100.0f - runSpeed;
      speedDelta = (speedDelta < 0.0f) ? -speedDelta : speedDelta;

      // Speed is measured relative to a 60 Hz refresh
      // rate. If we are transitioning from a low internal
      // frame rate to a higher internal frame rate, then
      // 'full speed' may actually equate to
      // (100 / current_swap_interval)...
      if ((vsyncSwapInterval > 1) &&
          (speedDelta >= VSYNC_SWAP_INTERVAL_RUN_SPEED_THRESHOLD))
      {
         speedDelta = 100.0f - (runSpeed * (float)vsyncSwapInterval);
         speedDelta = (speedDelta < 0.0f) ? -speedDelta : speedDelta;
      }

      if (speedDelta >= VSYNC_SWAP_INTERVAL_RUN_SPEED_THRESHOLD)
      {
         // Swap interval detection is invalid - bail out
         vsyncSwapIntervalCounter = 0;
         return;
      }

      // Get elapsed time (us) for this run
      s64 runTicks = CoreTiming::GetTicks();
      s64 runTimeUs = cyclesToUs(runTicks - runTicksLast);

      // Check if current internal frame rate is a
      // factor of the default ~60 Hz
      float swapRatio = (float)runTimeUs / (float)expectedTimeUsPerRun;
      uint32_t swapInteger;
      float swapRemainder;

      // If internal frame rate is equal to (within threshold)
      // or higher than the default ~60 Hz, fall back to a
      // swap interval of 1
      if (swapRatio < (1.0f + VSYNC_SWAP_INTERVAL_THRESHOLD))
      {
         swapInteger = 1;
         swapRemainder = 0.0f;
      }
      else
      {
         swapInteger = (uint32_t)(swapRatio + 0.5f);
         swapRemainder = swapRatio - (float)swapInteger;
         swapRemainder = (swapRemainder < 0.0f) ?
               -swapRemainder : swapRemainder;
      }

      // > Swap interval is considered 'valid' if it is
      //   within VSYNC_SWAP_INTERVAL_THRESHOLD of an integer
      //   value
      // > If valid, check if new swap interval differs from
      //   previously logged value
      if ((swapRemainder <= VSYNC_SWAP_INTERVAL_THRESHOLD) &&
          (swapInteger != vsyncSwapInterval))
      {
         vsyncSwapIntervalCounter =
               (swapInteger == vsyncSwapIntervalLast) ?
                     (vsyncSwapIntervalCounter + 1) : 0;

         // Check whether swap interval is 'stable'
         if (vsyncSwapIntervalCounter >= VSYNC_SWAP_INTERVAL_FRAMES)
         {
            vsyncSwapInterval = swapInteger;
            vsyncSwapIntervalCounter = 0;

            // Notify frontend
            retro_system_av_info avInfo;
            retro_get_system_av_info(&avInfo);
            environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &avInfo);
         }

         vsyncSwapIntervalLast = swapInteger;
      }
      else
         vsyncSwapIntervalCounter = 0;

      runTicksLast = runTicks;
   }
} // namespace Libretro

using namespace Libretro;

void RetroLogCallback(const LogMessage &message, void *userdata) {
   retro_log_printf_t fn = (retro_log_printf_t)userdata;
   switch (message.level) {
   case LogLevel::LVERBOSE:
   case LogLevel::LDEBUG:
      (fn)(RETRO_LOG_DEBUG, "[%s] %s", message.log, message.msg.c_str());
      break;

   case LogLevel::LERROR:
      (fn)(RETRO_LOG_ERROR, "[%s] %s", message.log, message.msg.c_str());
      break;
   case LogLevel::LNOTICE:
   case LogLevel::LWARNING:
      (fn)(RETRO_LOG_WARN, "[%s] %s", message.log, message.msg.c_str());
      break;
   case LogLevel::LINFO:
   default:
      (fn)(RETRO_LOG_INFO, "[%s] %s", message.log, message.msg.c_str());
      break;
   }
}

static bool set_variable_visibility(void)
{
   struct retro_core_option_display option_display;
   struct retro_variable var;
   bool updated = false;

   // Show/hide IP address options
   bool show_ip_address_options_prev = show_ip_address_options;
   show_ip_address_options = true;

   var.key = "ppsspp_change_pro_ad_hoc_server_address";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && strcmp(var.value, "IP address"))
      show_ip_address_options = false;

   if (show_ip_address_options != show_ip_address_options_prev)
   {
      option_display.visible = show_ip_address_options;
      for (int i = 0; i < 12; i++)
      {
         char key[64] = {0};
         option_display.key = key;
         snprintf(key, sizeof(key), "ppsspp_pro_ad_hoc_server_address%02d", i + 1);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
      }
      updated = true;
   }

   // Show/hide 'UPnP Use Original Port' option
   bool show_upnp_port_option_prev = show_upnp_port_option;
   show_upnp_port_option = true;

   var.key = "ppsspp_enable_upnp";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && !strcmp(var.value, "disabled"))
      show_upnp_port_option = false;

   if (show_upnp_port_option != show_upnp_port_option_prev)
   {
      option_display.visible = show_upnp_port_option;
      option_display.key = "ppsspp_upnp_use_original_port";
      environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
      updated = true;
   }

   // Show/hide 'Detect Frame Rate Changes' option
   bool show_detect_frame_rate_option_prev = show_detect_frame_rate_option;
   int frameskip = 0;
   bool auto_frameskip = false;
   bool dupe_frames = false;
   show_detect_frame_rate_option = true;

   var.key = "ppsspp_frameskip";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && strcmp(var.value, "disabled"))
      frameskip = atoi(var.value);
   var.key = "ppsspp_auto_frameskip";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && !strcmp(var.value, "enabled"))
      auto_frameskip = true;
   var.key = "ppsspp_frame_duplication";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && !strcmp(var.value, "enabled"))
      dupe_frames = true;

   show_detect_frame_rate_option = (frameskip == 0) && !auto_frameskip && !dupe_frames;
   if (show_detect_frame_rate_option != show_detect_frame_rate_option_prev)
   {
      option_display.visible = show_detect_frame_rate_option;
      option_display.key = "ppsspp_detect_vsync_swap_interval";
      environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
      updated = true;
   }

   return updated;
}

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;

   bool option_categories = false;
   libretro_set_core_options(environ_cb, &option_categories);
   struct retro_core_options_update_display_callback update_display_cb;
   update_display_cb.callback = set_variable_visibility;
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK, &update_display_cb);

   #ifdef HAVE_LIBRETRO_VFS
      struct retro_vfs_interface_info vfs_iface_info { 1, nullptr };
      if (cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs_iface_info))
         filestream_vfs_init(&vfs_iface_info);
   #endif
}

static int get_language_auto(void)
{
   retro_language val = RETRO_LANGUAGE_ENGLISH;
   environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &val);

   switch (val)
   {
      default:
      case RETRO_LANGUAGE_ENGLISH:
         return PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
      case RETRO_LANGUAGE_JAPANESE:
         return PSP_SYSTEMPARAM_LANGUAGE_JAPANESE;
      case RETRO_LANGUAGE_FRENCH:
         return PSP_SYSTEMPARAM_LANGUAGE_FRENCH;
      case RETRO_LANGUAGE_GERMAN:
         return PSP_SYSTEMPARAM_LANGUAGE_GERMAN;
      case RETRO_LANGUAGE_SPANISH:
         return PSP_SYSTEMPARAM_LANGUAGE_SPANISH;
      case RETRO_LANGUAGE_ITALIAN:
         return PSP_SYSTEMPARAM_LANGUAGE_ITALIAN;
      case RETRO_LANGUAGE_PORTUGUESE_BRAZIL:
      case RETRO_LANGUAGE_PORTUGUESE_PORTUGAL:
         return PSP_SYSTEMPARAM_LANGUAGE_PORTUGUESE;
      case RETRO_LANGUAGE_RUSSIAN:
         return PSP_SYSTEMPARAM_LANGUAGE_RUSSIAN;
      case RETRO_LANGUAGE_DUTCH:
         return PSP_SYSTEMPARAM_LANGUAGE_DUTCH;
      case RETRO_LANGUAGE_KOREAN:
         return PSP_SYSTEMPARAM_LANGUAGE_KOREAN;
      case RETRO_LANGUAGE_CHINESE_TRADITIONAL:
         return PSP_SYSTEMPARAM_LANGUAGE_CHINESE_TRADITIONAL;
      case RETRO_LANGUAGE_CHINESE_SIMPLIFIED:
         return PSP_SYSTEMPARAM_LANGUAGE_CHINESE_SIMPLIFIED;
   }
}

static std::string map_psp_language_to_i18n_locale(int val)
{
   switch (val)
   {
      default:
      case PSP_SYSTEMPARAM_LANGUAGE_ENGLISH:
         return "en_US";
      case PSP_SYSTEMPARAM_LANGUAGE_JAPANESE:
         return "ja_JP";
      case PSP_SYSTEMPARAM_LANGUAGE_FRENCH:
         return "fr_FR";
      case PSP_SYSTEMPARAM_LANGUAGE_GERMAN:
         return "de_DE";
      case PSP_SYSTEMPARAM_LANGUAGE_SPANISH:
         return "es_ES";
      case PSP_SYSTEMPARAM_LANGUAGE_ITALIAN:
         return "it_IT";
      case PSP_SYSTEMPARAM_LANGUAGE_PORTUGUESE:
         return "pt_PT";
      case PSP_SYSTEMPARAM_LANGUAGE_RUSSIAN:
         return "ru_RU";
      case PSP_SYSTEMPARAM_LANGUAGE_DUTCH:
         return "nl_NL";
      case PSP_SYSTEMPARAM_LANGUAGE_KOREAN:
         return "ko_KR";
      case PSP_SYSTEMPARAM_LANGUAGE_CHINESE_TRADITIONAL:
         return "zh_TW";
      case PSP_SYSTEMPARAM_LANGUAGE_CHINESE_SIMPLIFIED:
         return "zh_CN";
   }
}

static void check_dynamic_variables(CoreParameter &coreParam) {
   if (g_Config.bForceLagSync)
   {
      bool isFastForwarding;
      if (environ_cb(RETRO_ENVIRONMENT_GET_FASTFORWARDING, &isFastForwarding))
         coreParam.fastForward = isFastForwarding;
   }
}

static void check_variables(CoreParameter &coreParam)
{
   check_dynamic_variables(coreParam);

   struct retro_variable var = {0};
   std::string sTextureShaderName_prev;
   int iInternalResolution_prev;
   int iTexScalingType_prev;
   int iTexScalingLevel_prev;
   int iMultiSampleLevel_prev;
   bool bDisplayCropTo16x9_prev;

   var.key = "ppsspp_language";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "Automatic"))
         g_Config.iLanguage = -1;
      else if (!strcmp(var.value, "English"))
         g_Config.iLanguage = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
      else if (!strcmp(var.value, "Japanese"))
         g_Config.iLanguage = PSP_SYSTEMPARAM_LANGUAGE_JAPANESE;
      else if (!strcmp(var.value, "French"))
         g_Config.iLanguage = PSP_SYSTEMPARAM_LANGUAGE_FRENCH;
      else if (!strcmp(var.value, "Spanish"))
         g_Config.iLanguage = PSP_SYSTEMPARAM_LANGUAGE_SPANISH;
      else if (!strcmp(var.value, "German"))
         g_Config.iLanguage = PSP_SYSTEMPARAM_LANGUAGE_GERMAN;
      else if (!strcmp(var.value, "Italian"))
         g_Config.iLanguage = PSP_SYSTEMPARAM_LANGUAGE_ITALIAN;
      else if (!strcmp(var.value, "Dutch"))
         g_Config.iLanguage = PSP_SYSTEMPARAM_LANGUAGE_DUTCH;
      else if (!strcmp(var.value, "Portuguese"))
         g_Config.iLanguage = PSP_SYSTEMPARAM_LANGUAGE_PORTUGUESE;
      else if (!strcmp(var.value, "Russian"))
         g_Config.iLanguage = PSP_SYSTEMPARAM_LANGUAGE_RUSSIAN;
      else if (!strcmp(var.value, "Korean"))
         g_Config.iLanguage = PSP_SYSTEMPARAM_LANGUAGE_KOREAN;
      else if (!strcmp(var.value, "Chinese Traditional"))
         g_Config.iLanguage = PSP_SYSTEMPARAM_LANGUAGE_CHINESE_TRADITIONAL;
      else if (!strcmp(var.value, "Chinese Simplified"))
         g_Config.iLanguage = PSP_SYSTEMPARAM_LANGUAGE_CHINESE_SIMPLIFIED;
   }

#ifndef __EMSCRIPTEN__
   var.key = "ppsspp_cpu_core";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "JIT"))
         g_Config.iCpuCore = (int)CPUCore::JIT;
      else if (!strcmp(var.value, "IR JIT"))
         g_Config.iCpuCore = (int)CPUCore::IR_INTERPRETER;
      else if (!strcmp(var.value, "Interpreter"))
         g_Config.iCpuCore = (int)CPUCore::INTERPRETER;
   }

   if (System_GetPropertyBool(SYSPROP_CAN_JIT) == false && g_Config.iCpuCore == (int)CPUCore::JIT) {
       // Just gonna force it to the IR interpreter on startup.
       // We don't hide the option, but we make sure it's off on bootup. In case someone wants
       // to experiment in future iOS versions or something...
       g_Config.iCpuCore = (int)CPUCore::IR_INTERPRETER;
   }
#else
   g_Config.iCpuCore = (int)CPUCore::INTERPRETER;
#endif

   var.key = "ppsspp_fast_memory";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.bFastMemory = false;
      else
         g_Config.bFastMemory = true;
   }

   var.key = "ppsspp_ignore_bad_memory_access";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.bIgnoreBadMemAccess = false;
      else
         g_Config.bIgnoreBadMemAccess = true;
   }

   var.key = "ppsspp_io_timing_method";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "Fast"))
         g_Config.iIOTimingMethod = IOTIMING_FAST;
      else if (!strcmp(var.value, "Host"))
         g_Config.iIOTimingMethod = IOTIMING_HOST;
      else if (!strcmp(var.value, "Simulate UMD delays"))
         g_Config.iIOTimingMethod = IOTIMING_REALISTIC;
      else if (!strcmp(var.value, "Simulate UMD slow reading speed"))
         g_Config.iIOTimingMethod = IOTIMING_UMDSLOWREALISTIC;
   }

   var.key = "ppsspp_force_lag_sync";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.bForceLagSync = false;
      else
         g_Config.bForceLagSync = true;
   }

   var.key = "ppsspp_locked_cpu_speed";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      g_Config.iLockedCPUSpeed = atoi(var.value);

   var.key = "ppsspp_cache_iso";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.bCacheFullIsoInRam = false;
      else
         g_Config.bCacheFullIsoInRam = true;
   }

   var.key = "ppsspp_cheats";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.bEnableCheats = false;
      else
         g_Config.bEnableCheats = true;
   }

   var.key = "ppsspp_psp_model";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "psp_1000"))
         g_Config.iPSPModel = PSP_MODEL_FAT;
      else if (!strcmp(var.value, "psp_2000_3000"))
         g_Config.iPSPModel = PSP_MODEL_SLIM;
   }

   var.key = "ppsspp_button_preference";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "Cross"))
         g_Config.iButtonPreference = PSP_SYSTEMPARAM_BUTTON_CROSS;
      else if (!strcmp(var.value, "Circle"))
         g_Config.iButtonPreference = PSP_SYSTEMPARAM_BUTTON_CIRCLE;
   }

   var.key = "ppsspp_analog_is_circular";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.bAnalogIsCircular = false;
      else
         g_Config.bAnalogIsCircular = true;
   }

   var.key = "ppsspp_analog_deadzone";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      g_Config.fAnalogDeadzone = atof(var.value);

   var.key = "ppsspp_analog_sensitivity";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      g_Config.fAnalogSensitivity = atof(var.value);

   var.key = "ppsspp_memstick_inserted";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.bMemStickInserted = false;
      else
         g_Config.bMemStickInserted = true;
   }

   var.key = "ppsspp_internal_resolution";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      iInternalResolution_prev = g_Config.iInternalResolution;

      if (!strcmp(var.value, "480x272"))
         g_Config.iInternalResolution = 1;
      else if (!strcmp(var.value, "960x544"))
         g_Config.iInternalResolution = 2;
      else if (!strcmp(var.value, "1440x816"))
         g_Config.iInternalResolution = 3;
      else if (!strcmp(var.value, "1920x1088"))
         g_Config.iInternalResolution = 4;
      else if (!strcmp(var.value, "2400x1360"))
         g_Config.iInternalResolution = 5;
      else if (!strcmp(var.value, "2880x1632"))
         g_Config.iInternalResolution = 6;
      else if (!strcmp(var.value, "3360x1904"))
         g_Config.iInternalResolution = 7;
      else if (!strcmp(var.value, "3840x2176"))
         g_Config.iInternalResolution = 8;
      else if (!strcmp(var.value, "4320x2448"))
         g_Config.iInternalResolution = 9;
      else if (!strcmp(var.value, "4800x2720"))
         g_Config.iInternalResolution = 10;

      // Force resolution to 1x without hardware context
      if (backend == RETRO_HW_CONTEXT_NONE)
         g_Config.iInternalResolution = 1;
   }

   var.key = "ppsspp_software_rendering";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!PSP_IsInited())
      {
         if (!strcmp(var.value, "disabled") && backend != RETRO_HW_CONTEXT_NONE)
            g_Config.bSoftwareRendering = false;
         else
            g_Config.bSoftwareRendering = true;
      }

      // Force resolution to 1x with software rendering
      if (g_Config.bSoftwareRendering)
         g_Config.iInternalResolution = 1;
   }

   var.key = "ppsspp_mulitsample_level";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      iMultiSampleLevel_prev = g_Config.iMultiSampleLevel;

      if (!strcmp(var.value, "Disabled"))
         g_Config.iMultiSampleLevel = 0;
      else if (!strcmp(var.value, "x2"))
         g_Config.iMultiSampleLevel = 1;
      else if (!strcmp(var.value, "x4"))
         g_Config.iMultiSampleLevel = 2;
      else if (!strcmp(var.value, "x8"))
         g_Config.iMultiSampleLevel = 3;
   }

   var.key = "ppsspp_cropto16x9";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      bDisplayCropTo16x9_prev = g_Config.bDisplayCropTo16x9;

      if (!strcmp(var.value, "disabled"))
         g_Config.bDisplayCropTo16x9 = false;
      else
         g_Config.bDisplayCropTo16x9 = true;
   }

   var.key = "ppsspp_frameskip";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      g_Config.iFrameSkip = atoi(var.value);

   var.key = "ppsspp_auto_frameskip";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.bAutoFrameSkip = false;
      else
         g_Config.bAutoFrameSkip = true;
   }

   var.key = "ppsspp_frame_duplication";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.bRenderDuplicateFrames = false;
      else
         g_Config.bRenderDuplicateFrames = true;
   }

   var.key = "ppsspp_detect_vsync_swap_interval";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         detectVsyncSwapInterval = false;
      else
         detectVsyncSwapInterval = true;
   }

   var.key = "ppsspp_inflight_frames";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "No buffer"))
         g_Config.iInflightFrames = 1;
      else if (!strcmp(var.value, "Up to 1"))
         g_Config.iInflightFrames = 2;
      else if (!strcmp(var.value, "Up to 2"))
         g_Config.iInflightFrames = 3;
   }

   var.key = "ppsspp_skip_buffer_effects";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.bSkipBufferEffects = false;
      else
         g_Config.bSkipBufferEffects = true;
   }

   var.key = "ppsspp_disable_range_culling";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.bDisableRangeCulling = false;
      else
         g_Config.bDisableRangeCulling = true;
   }

   var.key = "ppsspp_skip_gpu_readbacks";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.iSkipGPUReadbackMode = (int)SkipGPUReadbackMode::NO_SKIP;
      else
         g_Config.iSkipGPUReadbackMode = (int)SkipGPUReadbackMode::SKIP;
   }

   var.key = "ppsspp_lazy_texture_caching";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.bTextureBackoffCache = false;
      else
         g_Config.bTextureBackoffCache = true;
   }

   var.key = "ppsspp_spline_quality";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "Low"))
         g_Config.iSplineBezierQuality = 0;
      else if (!strcmp(var.value, "Medium"))
         g_Config.iSplineBezierQuality = 1;
      else if (!strcmp(var.value, "High"))
         g_Config.iSplineBezierQuality = 2;
   }

   var.key = "ppsspp_gpu_hardware_transform";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.bHardwareTransform = false;
      else
         g_Config.bHardwareTransform = true;
   }

   var.key = "ppsspp_software_skinning";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.bSoftwareSkinning = false;
      else
         g_Config.bSoftwareSkinning = true;
   }

   var.key = "ppsspp_hardware_tesselation";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.bHardwareTessellation = false;
      else
         g_Config.bHardwareTessellation = true;
   }

   var.key = "ppsspp_lower_resolution_for_effects";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.iBloomHack = 0;
      else if (!strcmp(var.value, "Safe"))
         g_Config.iBloomHack = 1;
      else if (!strcmp(var.value, "Balanced"))
         g_Config.iBloomHack = 2;
      else if (!strcmp(var.value, "Aggressive"))
         g_Config.iBloomHack = 3;
   }

   var.key = "ppsspp_texture_scaling_type";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      iTexScalingType_prev = g_Config.iTexScalingType;

      if (!strcmp(var.value, "xbrz"))
         g_Config.iTexScalingType = TextureScalerCommon::XBRZ;
      else if (!strcmp(var.value, "hybrid"))
         g_Config.iTexScalingType = TextureScalerCommon::HYBRID;
      else if (!strcmp(var.value, "bicubic"))
         g_Config.iTexScalingType = TextureScalerCommon::BICUBIC;
      else if (!strcmp(var.value, "hybrid_bicubic"))
         g_Config.iTexScalingType = TextureScalerCommon::HYBRID_BICUBIC;
   }

   var.key = "ppsspp_texture_scaling_level";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      iTexScalingLevel_prev = g_Config.iTexScalingLevel;

      if (!strcmp(var.value, "disabled"))
         g_Config.iTexScalingLevel = 1;
      else if (!strcmp(var.value, "2x"))
         g_Config.iTexScalingLevel = 2;
      else if (!strcmp(var.value, "3x"))
         g_Config.iTexScalingLevel = 3;
      else if (!strcmp(var.value, "4x"))
         g_Config.iTexScalingLevel = 4;
      else if (!strcmp(var.value, "5x"))
         g_Config.iTexScalingLevel = 5;
   }

   var.key = "ppsspp_texture_deposterize";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.bTexDeposterize = false;
      else
         g_Config.bTexDeposterize = true;
   }

   var.key = "ppsspp_texture_shader";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      sTextureShaderName_prev = g_Config.sTextureShaderName;

      if (!strcmp(var.value, "disabled"))
         g_Config.sTextureShaderName = "Off";
      else if (!strcmp(var.value, "2xBRZ"))
         g_Config.sTextureShaderName = "Tex2xBRZ";
      else if (!strcmp(var.value, "4xBRZ"))
         g_Config.sTextureShaderName = "Tex4xBRZ";
      else if (!strcmp(var.value, "MMPX"))
         g_Config.sTextureShaderName = "TexMMPX";
   }

   var.key = "ppsspp_texture_anisotropic_filtering";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.iAnisotropyLevel = 0;
      else if (!strcmp(var.value, "2x"))
         g_Config.iAnisotropyLevel = 1;
      else if (!strcmp(var.value, "4x"))
         g_Config.iAnisotropyLevel = 2;
      else if (!strcmp(var.value, "8x"))
         g_Config.iAnisotropyLevel = 3;
      else if (!strcmp(var.value, "16x"))
         g_Config.iAnisotropyLevel = 4;
   }

   var.key = "ppsspp_texture_filtering";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "Auto"))
         g_Config.iTexFiltering = 1;
      else if (!strcmp(var.value, "Nearest"))
         g_Config.iTexFiltering = 2;
      else if (!strcmp(var.value, "Linear"))
         g_Config.iTexFiltering = 3;
      else if (!strcmp(var.value, "Auto max quality"))
         g_Config.iTexFiltering = 4;
   }

   var.key = "ppsspp_smart_2d_texture_filtering";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.bSmart2DTexFiltering = false;
      else
         g_Config.bSmart2DTexFiltering = true;
   }

   var.key = "ppsspp_texture_replacement";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.bReplaceTextures = false;
      else
         g_Config.bReplaceTextures = true;
   }

   var.key = "ppsspp_enable_wlan";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.bEnableWlan = false;
      else
         g_Config.bEnableWlan = true;
   }

   var.key = "ppsspp_wlan_channel";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      g_Config.iWlanAdhocChannel = atoi(var.value);

   var.key = "ppsspp_enable_builtin_pro_ad_hoc_server";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.bEnableAdhocServer = false;
      else
         g_Config.bEnableAdhocServer = true;
   }

   var.key = "ppsspp_change_pro_ad_hoc_server_address";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      changeProAdhocServer = var.value;

   var.key = "ppsspp_enable_upnp";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.bEnableUPnP = false;
      else
         g_Config.bEnableUPnP = true;
   }

   var.key = "ppsspp_upnp_use_original_port";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.bUPnPUseOriginalPort = false;
      else
         g_Config.bUPnPUseOriginalPort = true;
   }

   var.key = "ppsspp_port_offset";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      g_Config.iPortOffset = atoi(var.value);

   var.key = "ppsspp_minimum_timeout";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      g_Config.iMinTimeout = atoi(var.value);

   var.key = "ppsspp_forced_first_connect";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.bForcedFirstConnect = false;
      else
         g_Config.bForcedFirstConnect = true;
   }

   std::string ppsspp_change_mac_address[12];
   int ppsspp_pro_ad_hoc_ipv4[12];
   char key[64] = {0};
   var.key = key;
   g_Config.sMACAddress = "";
   g_Config.proAdhocServer = "";
   for (int i = 0; i < 12; i++)
   {
      snprintf(key, sizeof(key), "ppsspp_change_mac_address%02d", i + 1);
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      {
         ppsspp_change_mac_address[i] = var.value;

         if (i && i % 2 == 0)
             g_Config.sMACAddress += ":";

         g_Config.sMACAddress += ppsspp_change_mac_address[i];
      }

      snprintf(key, sizeof(key), "ppsspp_pro_ad_hoc_server_address%02d", i + 1);
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
         ppsspp_pro_ad_hoc_ipv4[i] = atoi(var.value);
   }

   if (g_Config.sMACAddress == "00:00:00:00:00:00")
   {
      g_Config.sMACAddress = CreateRandMAC();

      for (int i = 0; i < 12; i++)
      {
         snprintf(key, sizeof(key), "ppsspp_change_mac_address%02d", i + 1);
         std::string digit = {g_Config.sMACAddress[i + i / 2]};
         var.value = digit.c_str();
         environ_cb(RETRO_ENVIRONMENT_SET_VARIABLE, &var);
      }
   }

   if (changeProAdhocServer == "IP address")
   {
      g_Config.proAdhocServer = "";
      bool leadingZero = true;
      for (int i = 0; i < 12; i++)
      {
         if (i && i % 3 == 0)
         {
            g_Config.proAdhocServer += '.';
            leadingZero = true;
         }

         int addressPt = ppsspp_pro_ad_hoc_ipv4[i];
         if (addressPt || i % 3 == 2)
            leadingZero = false; // We are either non-zero or the last digit of a byte

         if (! leadingZero)
            g_Config.proAdhocServer += static_cast<char>('0' + addressPt);
      }
   }
   else
      g_Config.proAdhocServer = changeProAdhocServer;

   g_Config.bTexHardwareScaling = g_Config.sTextureShaderName != "Off";

   if (gpu && (g_Config.iTexScalingType != iTexScalingType_prev
         || g_Config.iTexScalingLevel != iTexScalingLevel_prev
         || g_Config.sTextureShaderName != sTextureShaderName_prev))
   {
      gpu->NotifyConfigChanged();
   }

   if (g_Config.iLanguage < 0)
      g_Config.iLanguage = get_language_auto();

   g_Config.sLanguageIni = map_psp_language_to_i18n_locale(g_Config.iLanguage);
   g_i18nrepo.LoadIni(g_Config.sLanguageIni);

   // Cannot detect refresh rate changes if:
   // > Frame skipping is enabled
   // > Frame duplication is enabled
   detectVsyncSwapInterval &=
         !g_Config.bAutoFrameSkip &&
         (g_Config.iFrameSkip == 0) &&
         !g_Config.bRenderDuplicateFrames;

   bool updateAvInfo = false;
   bool updateGeometry = false;

   if (!detectVsyncSwapInterval && (vsyncSwapInterval != 1))
   {
      vsyncSwapInterval = 1;
      updateAvInfo = true;
   }

   if (g_Config.iInternalResolution != iInternalResolution_prev && backend != RETRO_HW_CONTEXT_NONE)
   {
      coreParam.pixelWidth  = coreParam.renderWidth  = g_Config.iInternalResolution * NATIVEWIDTH;
      coreParam.pixelHeight = coreParam.renderHeight = g_Config.iInternalResolution * NATIVEHEIGHT;

      if (gpu)
      {
         retro_system_av_info avInfo;
         retro_get_system_av_info(&avInfo);
         environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &avInfo);
         updateAvInfo = false;
         gpu->NotifyDisplayResized();
      }
   }

   if (g_Config.bDisplayCropTo16x9 != bDisplayCropTo16x9_prev && PSP_IsInited())
   {
      updateGeometry = true;
      if (gpu)
         gpu->NotifyDisplayResized();
   }

   if (g_Config.iMultiSampleLevel != iMultiSampleLevel_prev && PSP_IsInited())
   {
      if (gpu)
      {
         const DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(g_display.GetDeviceOrientation());
         gpu->NotifyRenderResized(config);
      }
   }

   if (updateAvInfo)
   {
      retro_system_av_info avInfo;
      retro_get_system_av_info(&avInfo);
      environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &avInfo);
   }
   else if (updateGeometry)
   {
      retro_system_av_info avInfo;
      retro_get_system_av_info(&avInfo);
      environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &avInfo);
   }

   set_variable_visibility();
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

static const struct retro_controller_description psp_controllers[] =
{
   { "PSP", RETRO_DEVICE_JOYPAD },
   { NULL, 0 }
};

static const struct retro_controller_info ports[] =
{
   { psp_controllers, 1 },
   { NULL, 0 }
};

void retro_init(void)
{
   TimeInit();
   SetCurrentThreadName("Main");

   struct retro_log_callback log;
   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
   {
      log_cb = log.log;
      g_logManager.Init(&g_Config.bEnableLogging);
      g_logManager.SetOutputsEnabled(LogOutput::ExternalCallback);
      g_logManager.SetExternalLogCallback(&RetroLogCallback, (void *)log_cb);
   }

   VsyncSwapIntervalReset();

   struct retro_input_descriptor desc[] = {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "D-Pad Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "D-Pad Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "D-Pad Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Cross" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Circle" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "Triangle" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Square" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "L" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "R" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },
      { 0 },
   };
   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);
   environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);

   if (environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL))
      libretro_supports_bitmasks = true;

   g_Config.Load("", "");
   g_Config.iInternalResolution = 0;

   // Log levels must be set after g_Config.Load
   g_logManager.SetAllLogLevels(LogLevel::LINFO);

   const char* nickname = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_USERNAME, &nickname) && nickname)
      g_Config.sNickName = std::string(nickname);

   Path retro_base_dir;
   Path retro_save_dir;
   const char* dir_ptr = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir_ptr) && dir_ptr)
      retro_base_dir = Path(dir_ptr);

   if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir_ptr) && dir_ptr)
      retro_save_dir = Path(dir_ptr);

   retro_base_dir /= "PPSSPP";

   // Check if '<system_dir>/PPSSPP/compat.ini' exists, if not we can assume
   // the user is missing the assets entirely, so let's warn them about it.
   if (!File::Exists(Path(retro_base_dir / "compat.ini")))
   {
      const char* str = "Core system files missing, expect bugs.";
      unsigned msg_interface_version = 0;
      environ_cb(RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION, &msg_interface_version);

      if (msg_interface_version >= 1)
      {
         retro_message_ext msg = {
            str,
            3000,
            3,
            RETRO_LOG_WARN,
            RETRO_MESSAGE_TARGET_ALL,
            RETRO_MESSAGE_TYPE_NOTIFICATION,
            -1
         };
         environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &msg);
      }
      else
      {
         retro_message msg = {
            str,
            180
         };
         environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
      }

      // OSD messages should be kept pretty short, but
      // let's give the user a bit more info in logs.
      WARN_LOG(Log::System, "Please check the docs for more informations on how to install "
                            "the PPSSPP assets: https://docs.libretro.com/library/ppsspp/");
   }

   g_Config.currentDirectory = retro_base_dir;
   g_Config.defaultCurrentDirectory = retro_base_dir;
   g_Config.memStickDirectory = retro_save_dir;
   g_Config.flash0Directory = retro_base_dir / "flash0";
   g_Config.internalDataDirectory = retro_base_dir;
   g_Config.bEnableNetworkChat = false;
   g_Config.bDiscordRichPresence = false;

   g_VFS.Register("", new DirectoryReader(retro_base_dir));

   g_threadManager.Init(cpu_info.num_cores, cpu_info.logical_cpu_count);

   init_output_audio_buffer(2048);
}

void retro_deinit(void)
{
   g_threadManager.Teardown();
   g_logManager.Shutdown();
   log_cb = NULL;

   libretro_supports_bitmasks = false;

   VsyncSwapIntervalReset();

   free_output_audio_buffer();
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
	(void)port;
	(void)device;
}

void retro_get_system_info(struct retro_system_info *info)
{
   *info = {};
   info->library_name     = "PPSSPP";
   info->library_version  = PPSSPP_GIT_VERSION;
   info->need_fullpath    = true;
   info->valid_extensions = "elf|iso|cso|prx|pbp|chd";
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   *info = {};
   info->timing.fps            = (60.0 / 1.001) / (double)vsyncSwapInterval;
   info->timing.sample_rate    = SAMPLERATE;

   _dbg_assert_(g_Config.iInternalResolution != 0);

   info->geometry.base_width   = g_Config.iInternalResolution * NATIVEWIDTH;
   info->geometry.base_height  = g_Config.iInternalResolution * NATIVEHEIGHT;
   info->geometry.max_width    = g_Config.iInternalResolution * NATIVEWIDTH;
   info->geometry.max_height   = g_Config.iInternalResolution * NATIVEHEIGHT;

   if (g_Config.bDisplayCropTo16x9)
      info->geometry.base_height -= g_Config.iInternalResolution * 2;

   info->geometry.aspect_ratio = (float)info->geometry.base_width / (float)info->geometry.base_height;

   PSP_CoreParameter().pixelWidth  = PSP_CoreParameter().renderWidth  = info->geometry.base_width;
   PSP_CoreParameter().pixelHeight = PSP_CoreParameter().renderHeight = info->geometry.base_height;

   /* Must reset context to resize render area properly while running,
    * but not necessary with software, and not working with Vulkan.. (TODO) */
   if (PSP_IsInited() && ctx && backend != RETRO_HW_CONTEXT_NONE && ctx->GetGPUCore() != GPUCORE_VULKAN)
      ((LibretroHWRenderContext *)Libretro::ctx)->ContextReset();
}

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

namespace Libretro
{
   bool useEmuThread = false;
   std::atomic<EmuThreadState> emuThreadState(EmuThreadState::DISABLED);

   static std::thread emuThread;
   static void EmuFrame()
   {
      ctx->SetRenderTarget();
      if (ctx->GetDrawContext()) {
         ctx->GetDrawContext()->BeginFrame(Draw::DebugFlags::NONE);
      }

      if (gpu) {
         const DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(g_display.GetDeviceOrientation());
         gpu->BeginHostFrame(config);
      }

      PSP_RunLoopWhileState();
      switch (coreState) {
      case CORE_NEXTFRAME:
      case CORE_POWERDOWN:
         // Reached the end of the frame while running at full blast, all good. Set back to running for the next frame
         coreState = CORE_RUNNING_CPU;
         break;
      default:
         // We're not handling the various states used for debugging in the libretro port.
         break;
      }

      if (gpu)
         gpu->EndHostFrame();

      if (ctx->GetDrawContext()) {
         ctx->GetDrawContext()->EndFrame();
         ctx->GetDrawContext()->Present(Draw::PresentMode::FIFO);
      }
   }

   static void EmuThreadFunc()
   {
      SetCurrentThreadName("EmuThread");

      for (;;)
      {
         switch ((EmuThreadState)emuThreadState)
         {
            case EmuThreadState::START_REQUESTED:
               emuThreadState = EmuThreadState::RUNNING;
               [[fallthrough]];
            case EmuThreadState::RUNNING:
               EmuFrame();
               break;
            case EmuThreadState::PAUSE_REQUESTED:
               emuThreadState = EmuThreadState::PAUSED;
               [[fallthrough]];
            case EmuThreadState::PAUSED:
               sleep_ms(1, "libretro-paused");
               break;
            default:
            case EmuThreadState::QUIT_REQUESTED:
               emuThreadState = EmuThreadState::STOPPED;
               return;
         }
      }
   }

   void EmuThreadStart()
   {
      bool wasPaused = emuThreadState == EmuThreadState::PAUSED;
      emuThreadState = EmuThreadState::START_REQUESTED;

      if (!wasPaused)
      {
         ctx->ThreadStart();
         emuThread = std::thread(&EmuThreadFunc);
      }
   }

   void EmuThreadStop()
   {
      if (emuThreadState != EmuThreadState::RUNNING)
         return;

      emuThreadState = EmuThreadState::QUIT_REQUESTED;

      // Need to keep eating frames to allow the EmuThread to exit correctly.
      ctx->ThreadFrameUntilCondition([]() -> bool {
         return emuThreadState == EmuThreadState::STOPPED;
      });

      emuThread.join();
      emuThread = std::thread();
      ctx->ThreadEnd();
   }

   void EmuThreadPause()
   {
      if (emuThreadState != EmuThreadState::RUNNING)
         return;

      emuThreadState = EmuThreadState::PAUSE_REQUESTED;

      // Is this safe?
      ctx->ThreadFrame(true); // Eat 1 frame

      while (emuThreadState != EmuThreadState::PAUSED)
         sleep_ms(1, "libretro-pause-poll");
   }

} // namespace Libretro

static void retro_check_backend(void)
{
   struct retro_variable var = {0};

   var.key = "ppsspp_backend";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "auto"))
         backend = RETRO_HW_CONTEXT_DUMMY;
      else if (!strcmp(var.value, "opengl"))
         backend = RETRO_HW_CONTEXT_OPENGL;
      else if (!strcmp(var.value, "vulkan"))
         backend = RETRO_HW_CONTEXT_VULKAN;
      else if (!strcmp(var.value, "d3d11"))
         backend = RETRO_HW_CONTEXT_DIRECT3D;
      else if (!strcmp(var.value, "none"))
         backend = RETRO_HW_CONTEXT_NONE;
   }
}

bool retro_load_game(const struct retro_game_info *game)
{
   retro_pixel_format fmt = retro_pixel_format::RETRO_PIXEL_FORMAT_XRGB8888;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      ERROR_LOG(Log::System, "XRGB8888 is not supported.\n");
      return false;
   }

   retro_check_backend();

   ctx       = LibretroGraphicsContext::CreateGraphicsContext();

   INFO_LOG(Log::System, "Using %s backend", ctx->Ident());

   Core_SetGraphicsContext(ctx);
   SetGPUBackend((GPUBackend)g_Config.iGPUBackend);

   useEmuThread              = ctx->GetGPUCore() == GPUCORE_GLES;

   // default to interpreter to allow startup in platforms w/o JIT capability
   // TODO: I guess we should auto detect? And also, default to IR Interpreter...
   g_Config.iCpuCore         = (int)CPUCore::INTERPRETER;

   CoreParameter coreParam   = {};
   coreParam.enableSound     = true;
   coreParam.fileToStart     = Path(game->path);
   coreParam.startBreak      = false;
   coreParam.headLess        = true;  // really?
   coreParam.graphicsContext = ctx;
   coreParam.gpuCore         = ctx->GetGPUCore();
   check_variables(coreParam);

   // TODO: OpenGL goes black when inited with software rendering,
   // therefore start without, set back after init, and reset.
   softwareRenderInitHack    = ctx->GetGPUCore() == GPUCORE_GLES && g_Config.bSoftwareRendering;
   if (softwareRenderInitHack)
      g_Config.bSoftwareRendering = false;

   // set cpuCore from libretro setting variable
   coreParam.cpuCore         =  (CPUCore)g_Config.iCpuCore;

   g_pendingBoot = true;

   struct retro_core_option_display option_display;

   // Show/hide 'MSAA' and 'Texture Shader' options, Vulkan only
   option_display.visible = (g_Config.iGPUBackend == (int)GPUBackend::VULKAN);
   option_display.key = "ppsspp_mulitsample_level";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "ppsspp_texture_shader";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

   // Show/hide 'Buffered Frames' option, Vulkan/GL only
   option_display.visible = (g_Config.iGPUBackend == (int)GPUBackend::VULKAN ||
      g_Config.iGPUBackend == (int)GPUBackend::OPENGL);
   option_display.key = "ppsspp_inflight_frames";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

   set_variable_visibility();

   // NOTE: At this point we haven't really booted yet, but "in-game" we'll just keep polling
   // PSP_InitUpdate until done.

   // Launch the init process.
   if (!PSP_InitStart(coreParam)) {
      g_bootErrorString = coreParam.errorString;
      // Can't really fail, the errors normally happen later during InitUpdate
      ERROR_LOG(Log::Boot, "%s", g_bootErrorString.c_str());
      g_pendingBoot = false;
      return false;
   }

   return true;
}

void retro_unload_game(void)
{
	if (Libretro::useEmuThread)
		Libretro::EmuThreadStop();

	PSP_Shutdown(true);
	g_VFS.Clear();

	delete ctx;
	ctx = nullptr;
	PSP_CoreParameter().graphicsContext = nullptr;
}

void retro_reset(void)
{
   PSP_Shutdown(true);

   if (BootState::Complete != PSP_Init(PSP_CoreParameter(), &g_bootErrorString))
   {
      ERROR_LOG(Log::Boot, "%s", g_bootErrorString.c_str());
      environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, nullptr);
   }
}

static void retro_input(void)
{
   unsigned i;
   int16_t ret = 0;
   // clang-format off
   static struct
   {
      u32 retro;
      u32 sceCtrl;
   } map[] = {
      { RETRO_DEVICE_ID_JOYPAD_UP,     CTRL_UP },
      { RETRO_DEVICE_ID_JOYPAD_DOWN,   CTRL_DOWN },
      { RETRO_DEVICE_ID_JOYPAD_LEFT,   CTRL_LEFT },
      { RETRO_DEVICE_ID_JOYPAD_RIGHT,  CTRL_RIGHT },
      { RETRO_DEVICE_ID_JOYPAD_X,      CTRL_TRIANGLE },
      { RETRO_DEVICE_ID_JOYPAD_A,      CTRL_CIRCLE },
      { RETRO_DEVICE_ID_JOYPAD_B,      CTRL_CROSS },
      { RETRO_DEVICE_ID_JOYPAD_Y,      CTRL_SQUARE },
      { RETRO_DEVICE_ID_JOYPAD_L,      CTRL_LTRIGGER },
      { RETRO_DEVICE_ID_JOYPAD_R,      CTRL_RTRIGGER },
      { RETRO_DEVICE_ID_JOYPAD_START,  CTRL_START },
      { RETRO_DEVICE_ID_JOYPAD_SELECT, CTRL_SELECT },
   };
   // clang-format on

   input_poll_cb();

   if (libretro_supports_bitmasks)
      ret = input_state_cb(0, RETRO_DEVICE_JOYPAD,
            0, RETRO_DEVICE_ID_JOYPAD_MASK);
   else
   {
      for (i = RETRO_DEVICE_ID_JOYPAD_B; i <= RETRO_DEVICE_ID_JOYPAD_R; i++)
         if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, i))
            ret |= (1 << i);
   }

   for (i = 0; i < sizeof(map) / sizeof(*map); i++)
   {
      bool pressed = ret & (1 << map[i].retro);

      if (pressed)
      {
         __CtrlUpdateButtons(map[i].sceCtrl, 0);
      }
      else
      {
         __CtrlUpdateButtons(0, map[i].sceCtrl);
      }
   }

   float x_left = input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X) / 32767.0f;
   float y_left = input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y) / -32767.0f;
   float x_right = input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X) / 32767.0f;
   float y_right = input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y) / -32767.0f;

   // Analog circle vs square gate compensation,
   // deadzone and sensitivity copied from ControlMapper.cpp's
   // ConvertAnalogStick and MapAxisValue functions
   const bool isCircular = g_Config.bAnalogIsCircular;

   float norm = std::max(fabsf(x_left), fabsf(y_left));

   if (norm == 0.0f)
   {
      __CtrlSetAnalogXY(CTRL_STICK_LEFT, x_left, y_left);
      __CtrlSetAnalogXY(CTRL_STICK_RIGHT, x_right, y_right);
      return;
   }

   if (isCircular)
   {
      float newNorm = sqrtf(x_left * x_left + y_left * y_left);
      float factor = newNorm / norm;
      x_left *= factor;
      y_left *= factor;
      norm = newNorm;
   }

   const float deadzone = g_Config.fAnalogDeadzone;
   const float sensitivity = g_Config.fAnalogSensitivity;
   const float sign = norm >= 0.0f ? 1.0f : -1.0f;
   float mappedNorm = norm;

   // Apply deadzone
   mappedNorm = Libretro::clamp((fabsf(mappedNorm) - deadzone) / (1.0f - deadzone), 0.0f, 1.0f);

   // Apply sensitivity
   if (mappedNorm != 0.0f)
      mappedNorm = Libretro::clamp(mappedNorm * sensitivity * sign, -1.0f, 1.0f);

   x_left = Libretro::clamp(x_left / norm * mappedNorm, -1.0f, 1.0f);
   y_left = Libretro::clamp(y_left / norm * mappedNorm, -1.0f, 1.0f);

   __CtrlSetAnalogXY(CTRL_STICK_LEFT, x_left, y_left);
   __CtrlSetAnalogXY(CTRL_STICK_RIGHT, x_right, y_right);
}

void retro_run(void)
{
   if (g_pendingBoot) {
      BootState state = PSP_InitUpdate(&g_bootErrorString);
      switch (state) {
      case BootState::Failed:
         g_pendingBoot = false;
         ERROR_LOG(Log::Boot, "%s", g_bootErrorString.c_str());
         environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, nullptr);
         return;
      case BootState::Booting:
         // Not done yet. Do maintenance stuff and bail.
         retro_input();
         ctx->SwapBuffers();
         return;
      case BootState::Off:
         // shouldn't happen.
         _dbg_assert_(false);
         return;
      case BootState::Complete:
         // done, continue.
         break;
      }

      // BootState is BootState::Complete.
      // Here's where we finish the boot process.
      coreState = CORE_RUNNING_CPU;
      g_bootErrorString.clear();
      g_pendingBoot = false;

      if (unserialize_data) {
         retro_unserialize(unserialize_data, unserialize_size);

         free(unserialize_data);
         unserialize_data = NULL;
      }
   }

   // TODO: This seems dubious.
   if (softwareRenderInitHack)
   {
      log_cb(RETRO_LOG_DEBUG, "Software rendering init hack for opengl triggered.\n");
      softwareRenderInitHack = false;
      g_Config.bSoftwareRendering = true;
      retro_reset();
   }

   bool updated;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated)
      && updated)
      check_variables(PSP_CoreParameter());
   else
      check_dynamic_variables(PSP_CoreParameter());

   retro_input();

   if (useEmuThread)
   {
      if (  emuThreadState == EmuThreadState::PAUSED ||
            emuThreadState == EmuThreadState::PAUSE_REQUESTED)
      {
         VsyncSwapIntervalDetect();
         ctx->SwapBuffers();
         return;
      }

      if (emuThreadState != EmuThreadState::RUNNING)
         EmuThreadStart();

      if (!ctx->ThreadFrame(true))
      {
         VsyncSwapIntervalDetect();
         return;
      }
   }
   else
      EmuFrame();

   VsyncSwapIntervalDetect();
   ctx->SwapBuffers();
   upload_output_audio_buffer();
}

unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num) { return false; }

namespace SaveState
{
   struct SaveStart
   {
      void DoState(PointerWrap &p);
   };
} // namespace SaveState

size_t retro_serialize_size(void)
{
   if (!gpu) // The HW renderer isn't ready on first pass.
      return 134217728; // 128MB ought to be enough for anybody.

   SaveState::SaveStart state;
   // TODO: Libretro API extension to use the savestate queue
   if (useEmuThread)
      EmuThreadPause();

   return (CChunkFileReader::MeasurePtr(state) + 0x800000) & ~0x7FFFFF;
   // We don't unpause intentionally
}

bool retro_serialize(void *data, size_t size)
{
   if (!gpu) // The HW renderer isn't ready on first pass.
      return false;

   // TODO: Libretro API extension to use the savestate queue
   if (useEmuThread)
      EmuThreadPause(); // Does nothing if already paused

   size_t measuredSize;
   SaveState::SaveStart state;
   auto err = CChunkFileReader::MeasureAndSavePtr(state, (u8 **)&data, &measuredSize);
   bool retVal = err == CChunkFileReader::ERROR_NONE;

   if (useEmuThread)
   {
      EmuThreadStart();
      sleep_ms(4, "libretro-serialize");
   }

   return retVal;
}

bool retro_unserialize(const void *data, size_t size)
{
   // The HW renderer isn't ready on first pass.
   // So we save the data until we are ready to use it.
   if (!gpu) {
      unserialize_data = malloc(size);
      memcpy(unserialize_data, data, size);
      return true;
   }

   // TODO: Libretro API extension to use the savestate queue
   if (useEmuThread)
      EmuThreadPause(); // Does nothing if already paused

   std::string errorString;
   SaveState::SaveStart state;
   bool retVal = CChunkFileReader::LoadPtr((u8 *)data, state, &errorString)
      == CChunkFileReader::ERROR_NONE;

   if (useEmuThread)
   {
      EmuThreadStart();
      sleep_ms(4, "libretro-unserialize");
   }

   return retVal;
}

void *retro_get_memory_data(unsigned id)
{
   if ( id == RETRO_MEMORY_SYSTEM_RAM )
      return Memory::GetPointerWriteUnchecked(PSP_GetKernelMemoryBase()) ;
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
	if ( id == RETRO_MEMORY_SYSTEM_RAM )
		return Memory::g_MemorySize ;
	return 0;
}

void retro_cheat_reset(void) {
   // Init Cheat Engine
   CWCheatEngine *cheatEngine = new CWCheatEngine(g_paramSFO.GetDiscID());
   Path file=cheatEngine->CheatFilename();

   // Output cheats to cheat file
   std::ofstream outFile;
   outFile.open(file.c_str());
   outFile << "_S " << g_paramSFO.GetDiscID() << std::endl;
   outFile.close();

   g_Config.bReloadCheats = true;

   // Parse and Run the Cheats
   cheatEngine->ParseCheats();
   if (cheatEngine->HasCheats()) {
      cheatEngine->Run();
   }

}

void retro_cheat_set(unsigned index, bool enabled, const char *code) {
   // Initialize Cheat Engine
   CWCheatEngine *cheatEngine = new CWCheatEngine(g_paramSFO.GetDiscID());
   cheatEngine->CreateCheatFile();
   Path file=cheatEngine->CheatFilename();

   // Read cheats file
   std::vector<std::string> cheats;
   std::ifstream cheat_content(file.c_str());
   std::stringstream buffer;
   buffer << cheat_content.rdbuf();
   std::string existing_cheats=ReplaceAll(buffer.str(), std::string("\n_C"), std::string("|"));
   SplitString(existing_cheats, '|', cheats);

   // Generate Cheat String
   std::stringstream cheat("");
   cheat << (enabled ? "1 " : "0 ") << index << std::endl;
   std::string code_str(code);
   std::vector<std::string> codes;
   code_str=ReplaceAll(code_str, std::string(" "), std::string("+"));
   SplitString(code_str, '+', codes);
   int part=0;
   for (int i=0; i < codes.size(); i++) {
      if (codes[i].size() <= 2) {
         // _L _M ..etc
         // Assume _L
      } else if (part == 0) {
         cheat << "_L " << codes[i] << " ";
         part++;
      } else {
         cheat << codes[i] << std::endl;
         part=0;
      }
   }

   // Add or Replace the Cheat
   if (index + 1 < cheats.size()) {
      cheats[index + 1]=cheat.str();
   } else {
      cheats.push_back(cheat.str());
   }

   // Output cheats to cheat file
   std::ofstream outFile;
   outFile.open(file.c_str());
   outFile << "_S " << g_paramSFO.GetDiscID() << std::endl;
   for (int i=1; i < cheats.size(); i++) {
      outFile << "_C" << cheats[i] << std::endl;
   }
   outFile.close();

   g_Config.bReloadCheats = true;

   // Parse and Run the Cheats
   cheatEngine->ParseCheats();
   if (cheatEngine->HasCheats()) {
      cheatEngine->Run();
   }
}

int64_t System_GetPropertyInt(SystemProperty prop) {
   switch (prop) {
      case SYSPROP_AUDIO_SAMPLE_RATE:
         return SAMPLERATE;
#if PPSSPP_PLATFORM(ANDROID)
      case SYSPROP_SYSTEMVERSION: {
         char sdk[PROP_VALUE_MAX] = {0};
         if (__system_property_get("ro.build.version.sdk", sdk) != 0) {
            return atoi(sdk);
         }
         return -1;
      }
#endif
      default:
         break;
   }

   return -1;
}

float System_GetPropertyFloat(SystemProperty prop)
{
   switch (prop)
   {
      case SYSPROP_DISPLAY_REFRESH_RATE:
         return 60.0f / 1.001f;
      case SYSPROP_DISPLAY_SAFE_INSET_LEFT:
      case SYSPROP_DISPLAY_SAFE_INSET_RIGHT:
      case SYSPROP_DISPLAY_SAFE_INSET_TOP:
      case SYSPROP_DISPLAY_SAFE_INSET_BOTTOM:
         return 0.0f;
      default:
         break;
   }

   return -1;
}

bool System_GetPropertyBool(SystemProperty prop)
{
   switch (prop)
   {
   case SYSPROP_CAN_JIT:
#if PPSSPP_PLATFORM(IOS)
      bool can_jit;
      return (environ_cb(RETRO_ENVIRONMENT_GET_JIT_CAPABLE, &can_jit) && can_jit);
#else
      return true;
#endif
   default:
      return false;
   }
}

std::string System_GetProperty(SystemProperty prop) { return ""; }
std::vector<std::string> System_GetPropertyStringVec(SystemProperty prop) { return std::vector<std::string>(); }

void System_Notify(SystemNotification notification) {
   switch (notification) {
   default:
      break;
   }
}
bool System_MakeRequest(SystemRequestType type, int requestId, const std::string &param1, const std::string &param2, int64_t param3, int64_t param4) { return false; }
void System_PostUIMessage(UIMessage message, std::string_view param) {}
void System_RunOnMainThread(std::function<void()>) {}
void NativeFrame(GraphicsContext *graphicsContext) {}
void NativeResized() {}
void NativeVSync(int64_t vsyncId, double frameTime, double expectedPresentationTime) {}

void System_Toast(std::string_view str) {}

inline int16_t Clamp16(int32_t sample) {
   if (sample < -32767) return -32767;
   if (sample > 32767) return 32767;
   return sample;
}

void System_AudioPushSamples(const int32_t *audio, int numSamples, float volume) {
   // We ignore volume here, because it's handled by libretro presumably.

   // Convert to 16-bit audio for further processing.
   int16_t buffer[1024 * 2];
   int origSamples = numSamples * 2;

   while (numSamples > 0) {
      int blockSize = std::min(1024, numSamples);
      for (int i = 0; i < blockSize; i++) {
         buffer[i * 2] = Clamp16(audio[i * 2]);
         buffer[i * 2 + 1] = Clamp16(audio[i * 2 + 1]);
      }

      numSamples -= blockSize;
   }

   if (output_audio_buffer.capacity - output_audio_buffer.size < origSamples)
      ensure_output_audio_buffer_capacity((output_audio_buffer.capacity + origSamples) * 1.5);
   memcpy(output_audio_buffer.data + output_audio_buffer.size, buffer, origSamples * sizeof(*output_audio_buffer.data));
   output_audio_buffer.size += origSamples;
}

void System_AudioGetDebugStats(char *buf, size_t bufSize) { if (buf) buf[0] = '\0'; }
void System_AudioClear() {}

#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(IOS)
std::vector<std::string> System_GetCameraDeviceList() { return std::vector<std::string>(); }
bool System_AudioRecordingIsAvailable() { return false; }
bool System_AudioRecordingState() { return false; }
#elif PPSSPP_PLATFORM(MAC)
std::vector<std::string> __mac_getDeviceList() { return std::vector<std::string>(); }
int __mac_startCapture(int width, int height) { return 0; }
int __mac_stopCapture() { return 0; }
#endif

// TODO: To avoid having to define these here, these should probably be turned into system "requests".
bool NativeSaveSecret(std::string_view nameOfSecret, std::string_view data) { return false; }
std::string NativeLoadSecret(std::string_view nameOfSecret) {
   return "";
}
