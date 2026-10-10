#include "ppsspp_config.h"
#include <array>
#include <cstring>
#include <algorithm>
#include <cassert>
#include <vector>
#include <cstdlib>

#include <retro_atomic.h>
#include <retro_spsc.h>

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
#include "Common/Thread/MpscQueue.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/VFS/DirectoryReader.h"
#include "Common/Data/Text/I18n.h"
#include "Common/StringUtils.h"

#include "Common/Thread/ParkingLot.h"
#include "Common/Thread/Thread.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Core.h"
#include "Core/MIPS/MIPS.h"
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


#include <libretro.h>
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

// Audio output: System_AudioPushSamples() fills it on whichever thread runs the emulator (the
// emu thread with the GL backends), retro_run() drains it to the frontend. Stereo int16 frames.
static retro_spsc_t output_audio_ring;
static bool output_audio_ring_ok = false;
static const size_t OUTPUT_AUDIO_RING_FRAMES = 32768;
static retro_atomic_size_t output_audio_discard_until{ 0 };
static retro_atomic_int_t output_audio_clear_requested{ 0 };

struct CheatChange {
   bool reset;
   unsigned index;
   bool enabled;
   std::string code;
};
static MpscQueue<CheatChange> pendingCheats;
static void ApplyCheatReset();
static void ApplyCheatSet(unsigned index, bool enabled, const char *code);

static bool libretro_supports_bitmasks = false;
static bool show_ip_address_options = true;
static bool show_upnp_port_option = true;
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

   static bool softwareRenderInitHack = false;


   // With neither side running.
   static void init_output_audio_buffer()
   {
      output_audio_ring_ok = retro_spsc_init(&output_audio_ring, OUTPUT_AUDIO_RING_FRAMES * 4);
      retro_atomic_store_relaxed_size(&output_audio_discard_until, 0);
      retro_atomic_store_relaxed_int(&output_audio_clear_requested, 0);
      if (!output_audio_ring_ok && log_cb)
         log_cb(RETRO_LOG_ERROR, "Failed to allocate the output audio buffer\n");
   }

   static void free_output_audio_buffer()
   {
      retro_spsc_free(&output_audio_ring);
      output_audio_ring_ok = false;
   }

   // The frontend gets the ring in place, in up to two contiguous runs.
   static void upload_output_audio_buffer()
   {
      if (!output_audio_ring_ok)
         return;
      // The producer publishes a discard boundary; only the consumer advances
      // tail. Do not reset live SPSC cursors from the emulation thread.
      if (retro_atomic_exchange_int(&output_audio_clear_requested, 0)) {
         const size_t target = retro_atomic_load_acquire_size(&output_audio_discard_until);
         const size_t tail = retro_atomic_load_relaxed_size(&output_audio_ring.tail);
         const size_t discard = target - tail;
         if (discard <= retro_spsc_read_avail(&output_audio_ring))
            retro_spsc_skip(&output_audio_ring, discard);
      }
      // Deliver the current batch, not samples produced indefinitely while a
      // slow frontend callback is running.
      size_t remaining = retro_spsc_read_avail(&output_audio_ring) & ~(size_t)3;
      const void *ptr;
      size_t bytes;
      while (remaining && (bytes = retro_spsc_read_begin(&output_audio_ring, &ptr)) >= 4)
      {
         bytes = std::min(bytes & ~(size_t)3, remaining);
         const size_t frames = bytes / 4;
         const size_t consumed = std::min(audio_batch_cb((const int16_t *)ptr, frames), frames);
         retro_spsc_read_end(&output_audio_ring, consumed * 4);
         remaining -= consumed * 4;
         if (consumed < frames)
            break;
      }
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
   struct retro_vfs_interface_info vfs_iface_info { 2, nullptr };
   if (cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs_iface_info))
      File::InitLibretroVFS(&vfs_iface_info);
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

static void check_variables(CoreParameter &coreParam)
{

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

   var.key = "ppsspp_frame_duplication";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.bRenderDuplicateFrames = false;
      else
         g_Config.bRenderDuplicateFrames = true;
   }

   var.key = "ppsspp_inflight_frames";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "No buffer"))
         g_Config.iInflightFrames = 1;
      else if (!strcmp(var.value, "Up to 1"))
         g_Config.iInflightFrames = 2;
   }

   var.key = "ppsspp_skip_buffer_effects";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.bSkipBufferEffects = false;
      else
         g_Config.bSkipBufferEffects = true;
   }

   var.key = "ppsspp_skip_gpu_readbacks";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
         g_Config.iSkipGPUReadbackMode = (int)SkipGPUReadbackMode::NO_SKIP;
      else
         g_Config.iSkipGPUReadbackMode = (int)SkipGPUReadbackMode::SKIP;
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
   g_Config.sProAdhocServer = "";
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
      g_Config.sProAdhocServer = "";
      bool leadingZero = true;
      for (int i = 0; i < 12; i++)
      {
         if (i && i % 3 == 0)
         {
            g_Config.sProAdhocServer += '.';
            leadingZero = true;
         }

         int addressPt = ppsspp_pro_ad_hoc_ipv4[i];
         if (addressPt || i % 3 == 2)
            leadingZero = false; // We are either non-zero or the last digit of a byte

         if (! leadingZero)
            g_Config.sProAdhocServer += static_cast<char>('0' + addressPt);
      }
   }
   else
      g_Config.sProAdhocServer = changeProAdhocServer;

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

   bool updateGeometry = false;
   bool resetHWContext = false;

   if (g_Config.iInternalResolution != iInternalResolution_prev && backend != RETRO_HW_CONTEXT_NONE)
   {
      coreParam.pixelWidth  = coreParam.renderWidth  = g_Config.iInternalResolution * NATIVEWIDTH;
      coreParam.pixelHeight = coreParam.renderHeight = g_Config.iInternalResolution * NATIVEHEIGHT;

      if (gpu)
      {
         retro_system_av_info avInfo;
         retro_get_system_av_info(&avInfo);
         environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &avInfo);
         gpu->NotifyDisplayResized();
         if (ctx && ctx->GetGPUCore() != GPUCORE_VULKAN)
            resetHWContext = true;
      }
   }

   if (g_Config.bDisplayCropTo16x9 != bDisplayCropTo16x9_prev && PSP_IsInited())
   {
      updateGeometry = true;
      if (gpu)
         gpu->NotifyDisplayResized();
      if (ctx && backend != RETRO_HW_CONTEXT_NONE && ctx->GetGPUCore() != GPUCORE_VULKAN)
         resetHWContext = true;
   }

   if (g_Config.iMultiSampleLevel != iMultiSampleLevel_prev && PSP_IsInited())
   {
      if (gpu)
      {
         const DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(g_display.GetDeviceOrientation());
         gpu->NotifyRenderResized(config);
      }
   }

   if (updateGeometry)
   {
      retro_system_av_info avInfo;
      retro_get_system_av_info(&avInfo);
      environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &avInfo);
   }

   /* Must reset context to resize render area properly while running,
    * but not necessary with software, and not working with Vulkan.. (TODO) */
   if (resetHWContext)
      ((LibretroHWRenderContext *)ctx)->ContextReset();

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
      // Also enable LogOutput::DebugString unconditionally (not just via Init()'s IsDebuggerPresent()
      // auto-detection, which only fires if a debugger was already attached before Init() ran) so the
      // log always shows up in the debugger's Output window when debugging the core in-process with
      // RetroArch, regardless of where RetroArch itself routes the ExternalCallback log messages.
      g_logManager.EnableOutput(LogOutput::DebugString);
      // AddExternalLogCallback() enables LogOutput::ExternalCallback itself. Never removed - this
      // callback lives as long as the core does.
      g_logManager.AddExternalLogCallback(&RetroLogCallback, (void *)log_cb);
   }


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
   g_Config.internalDataDirectory = retro_base_dir;
   g_Config.bEnableNetworkChat = false;
   g_Config.nandRootDirectory = GetSysDirectory(PSPDirectories::DIRECTORY_NAND);

   g_VFS.Register("", new DirectoryReader(retro_base_dir));

   g_threadManager.Init(cpu_info.num_cores, cpu_info.logical_cpu_count);

   init_output_audio_buffer();
}

void retro_deinit(void)
{
   g_threadManager.Teardown();
   g_logManager.Shutdown();
   log_cb = NULL;

   libretro_supports_bitmasks = false;


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
   info->timing.fps            = 60.0 / 1.001;
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
}

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

namespace Libretro {
   bool useEmuThread = false;
   retro_atomic_int_t emuThreadState{ (int)EmuThreadState::DISABLED };

   static Thread emuThread;
   static retro_atomic_int_t emuFrameRequested{ 0 };
   static void EmuFrame() {
      // Frontend callbacks only publish changes. Files and guest memory are
      // touched here, on the same owner that executes PSP code.
      pendingCheats.Drain([](CheatChange &&change) {
         if (change.reset)
            ApplyCheatReset();
         else
            ApplyCheatSet(change.index, change.enabled, change.code.c_str());
      });
      ctx->SetRenderTarget();
      Draw::DrawContext *draw = ctx->GetDrawContext();
      if (draw) {
         draw->BeginFrame(Draw::DebugFlags::NONE);
      }

      const DisplayLayoutConfig &displayLayoutConfig = g_Config.GetDisplayLayoutConfig(g_display.GetDeviceOrientation());
      if (gpu) {
         gpu->BeginHostFrame(displayLayoutConfig);
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

      if (gpu) {
         gpu->EndHostFrame();
         if (draw) {
            gpu->PrepareCopyDisplayToOutput(displayLayoutConfig);
         }
      }

      // gotta do the backbuffer bind somewhere.
      using namespace Draw;
      if (draw) {
         draw->BindFramebufferAsRenderTarget(nullptr, {RPAction::CLEAR, RPAction::CLEAR, RPAction::CLEAR}, "BackBuffer");
      }

      if (gpu && draw) {
         gpu->CopyDisplayToOutput(displayLayoutConfig);
      }

      if (draw) {
         draw->EndFrame();
         draw->Present();
      }
   }

   static void EmuThreadFunc() {
      SetCurrentThreadName("EmuThread");

      for (;;) {
         EmuThreadState state = EmuThreadStateGet();
         switch (state)
         {
            case EmuThreadState::RUNNING:
               // A libretro run grants exactly one frame after polling input.
               // GPU buffer availability must not let guest execution run ahead
               // with the previous run's input or audio timeline.
               ParkingLotWait(&emuThreadState, [] {
                  return retro_atomic_load_acquire_int(&emuFrameRequested) != 0 ||
                     EmuThreadStateGet() != EmuThreadState::RUNNING;
               });
               if (EmuThreadStateGet() != EmuThreadState::RUNNING)
                  break;
               if (retro_atomic_exchange_int(&emuFrameRequested, 0))
                  EmuFrame();
               break;
            case EmuThreadState::PAUSE_REQUESTED:
               // CAS so a concurrent QUIT_REQUESTED is not clobbered; the loop re-reads the state.
               // The marker follows this frame's work, so the frontend has drained it on seeing it.
               if (retro_atomic_cas_int(&emuThreadState, (int)state, (int)EmuThreadState::PAUSED))
                  ctx->NotifyEmuThreadPaused();
               break;
            case EmuThreadState::PAUSED:
               ParkingLotWait(&emuThreadState, [] { return EmuThreadStateGet() != EmuThreadState::PAUSED; });
               break;
            case EmuThreadState::QUIT_REQUESTED:
               ctx->NotifyEmuThreadExit();
               EmuThreadStateSet(EmuThreadState::STOPPED);
               return;
            default:
               _dbg_assert_(false);
               return;
         }
      }
      // Unreachable
   }

   void EmuThreadStart() {
      EmuThreadState state = EmuThreadStateGet();
      bool wasPaused = state == EmuThreadState::PAUSED;

      if (state == EmuThreadState::RUNNING || (emuThread.joinable() && !wasPaused))
         return;

      EmuThreadStateSet(EmuThreadState::RUNNING);

      if (wasPaused)
         ParkingLotNotify(&emuThreadState);
      else
      {
         ctx->ThreadStart();
         emuThread = Thread(&EmuThreadFunc);
      }
   }

   void EmuThreadStop() {
      // Stop and join from any live state, not just RUNNING: unloading while
      // PAUSED (the state retro_serialize_size() leaves behind) must not leave
      // a live thread over freed engine state or a joinable Thread.
      if (!emuThread.joinable())
         return;

      if (EmuThreadStateGet() != EmuThreadState::STOPPED) {
         EmuThreadStateSet(EmuThreadState::QUIT_REQUESTED);
         ParkingLotNotify(&emuThreadState);
      }

      // Eat remaining frames.
      while (ctx->ThreadFrame()) {}

      emuThread.join();
      retro_atomic_store_relaxed_int(&emuFrameRequested, 0);
      ctx->ThreadEnd();
   }

   void EmuThreadPause() {
      if (EmuThreadStateGet() != EmuThreadState::RUNNING)
         return;

      EmuThreadStateSet(EmuThreadState::PAUSE_REQUESTED);
      ParkingLotNotify(&emuThreadState);

      // Run queued work, readback syncs included, up to the marker the emu
      // thread queues once it has parked at a frame boundary.
      ctx->ThreadRunUntilPaused();
   }

} // namespace Libretro

static void retro_check_backend(void) {
   struct retro_variable var = {0};

   var.key = "ppsspp_backend";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
      if (!strcmp(var.value, "auto"))
         backend = RETRO_HW_CONTEXT_DUMMY;
      else if (!strcmp(var.value, "opengl"))
         backend = RETRO_HW_CONTEXT_OPENGL;
      else if (!strcmp(var.value, "vulkan"))
         backend = RETRO_HW_CONTEXT_VULKAN;
      else if (!strcmp(var.value, "d3d11"))
         backend = RETRO_HW_CONTEXT_D3D11;
      else if (!strcmp(var.value, "none"))
         backend = RETRO_HW_CONTEXT_NONE;
   }
}

bool retro_load_game(const struct retro_game_info *game) {
   retro_pixel_format fmt = retro_pixel_format::RETRO_PIXEL_FORMAT_XRGB8888;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
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
   coreParam.bUseVertexDecoderJit = System_GetPropertyBool(SYSPROP_CAN_JIT);

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

void retro_unload_game(void) {
   if (Libretro::useEmuThread) {
      Libretro::EmuThreadStop();
   }

	ctx->LockGPU();
	PSP_Shutdown(true);
	ctx->UnlockGPU();
	System_AudioClear();
	pendingCheats.Drain([](CheatChange &&) {});
	g_pendingBoot = false;
	g_bootErrorString.clear();
	free(unserialize_data);
	unserialize_data = nullptr;
	unserialize_size = 0;
	g_VFS.Clear();

	delete ctx;
	ctx = nullptr;
	PSP_CoreParameter().graphicsContext = nullptr;
}

void retro_reset(void) {
   // EmuFrame() must not run while the core is torn down; retro_run()
   // restarts the thread.
   if (Libretro::useEmuThread)
      Libretro::EmuThreadStop();

   ctx->LockGPU();
   PSP_Shutdown(true);
   System_AudioClear();
   g_pendingBoot = false;
   free(unserialize_data);
   unserialize_data = nullptr;
   unserialize_size = 0;

   BootState state = PSP_Init(PSP_CoreParameter(), &g_bootErrorString);
   ctx->UnlockGPU();
   if (BootState::Complete != state) {
      ERROR_LOG(Log::Boot, "%s", g_bootErrorString.c_str());
      environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, nullptr);
   }
}

static void retro_input(void) {
   unsigned i;
   int16_t ret = 0;
   // clang-format off
   static const struct
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

   if (libretro_supports_bitmasks) {
      ret = input_state_cb(0, RETRO_DEVICE_JOYPAD,
         0, RETRO_DEVICE_ID_JOYPAD_MASK);
   } else {
      for (i = RETRO_DEVICE_ID_JOYPAD_B; i <= RETRO_DEVICE_ID_JOYPAD_R; i++)
         if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, i))
            ret |= (1 << i);
   }

   u32 buttons = 0;
   u32 controlledButtons = 0;
   for (const auto &entry : map) {
      controlledButtons |= entry.sceCtrl;
      if (ret & (1 << entry.retro))
         buttons |= entry.sceCtrl;
   }
   // Publish one digital snapshot rather than twelve transient combinations.
   __CtrlUpdateButtons(buttons, controlledButtons & ~buttons);

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

// Called every frame by retroarch.
void retro_run(void) {
   if (g_pendingBoot) {
      ctx->LockGPU();
      BootState state = PSP_InitUpdate(&g_bootErrorString);
      ctx->UnlockGPU();
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
   if (softwareRenderInitHack) {
      log_cb(RETRO_LOG_DEBUG, "Software rendering init hack for opengl triggered.\n");
      softwareRenderInitHack = false;
      g_Config.bSoftwareRendering = true;
      retro_reset();
   }

   // Update setting if any have changed.
   bool updated = false;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated) {
      // The emu thread reads g_Config mid-frame; it is resumed below.
      if (useEmuThread)
         EmuThreadPause();
      check_variables(PSP_CoreParameter());
   }

   // Process input.
   retro_input();

   // Handle thread pumping.
   if (useEmuThread) {
      // Also resumes from PAUSED, so a retro_serialize_size() without a
      // following retro_serialize() cannot freeze emulation.
      if (EmuThreadStateGet() != EmuThreadState::RUNNING) {
         EmuThreadStart();
      }
      retro_atomic_store_release_int(&emuFrameRequested, 1);
      ParkingLotNotify(&emuThreadState);

      if (!ctx->ThreadFrame()) {
         // We're done processing the last frame from the emu thread.
         return;
      }
   }
   else {
      ctx->FrameBegin();
      EmuFrame();
   }

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

   ctx->LockGPU();
   size_t measured = CChunkFileReader::MeasurePtr(state);
   ctx->UnlockGPU();
   return (measured + 0x800000) & ~0x7FFFFF;
   // We don't unpause intentionally
}

bool retro_serialize(void *data, size_t size) {
   if (!gpu) // The HW renderer isn't ready on first pass.
      return false;

   // TODO: Libretro API extension to use the savestate queue
   if (useEmuThread) {
      EmuThreadPause(); // Does nothing if already paused
   }

   SaveState::SaveStart state;
   bool retVal = false;

   ctx->LockGPU();
   // The buffer was sized by an earlier retro_serialize_size(); the state
   // may have grown since, so never write past 'size'.
   size_t measuredSize = CChunkFileReader::MeasurePtr(state);
   if (measuredSize <= size) {
      u8 *buffer = (u8 *)data;
      auto err = CChunkFileReader::MeasureAndSavePtr(state, &buffer, &measuredSize);
      retVal = err == CChunkFileReader::ERROR_NONE;
   } else {
      ERROR_LOG(Log::SaveState, "Savestate (%d bytes) does not fit in frontend buffer (%d bytes)",
                (int)measuredSize, (int)size);
   }
   ctx->UnlockGPU();

   if (useEmuThread)
      EmuThreadStart();

   return retVal;
}

bool retro_unserialize(const void *data, size_t size) {
   // The HW renderer isn't ready on first pass.
   // So we save the data until we are ready to use it.
   if (!gpu) {
      void *buffer = malloc(size);
      if (!buffer)
         return false;
      free(unserialize_data);
      unserialize_data = buffer;
      unserialize_size = size;
      memcpy(unserialize_data, data, size);
      return true;
   }

   // TODO: Libretro API extension to use the savestate queue
   if (useEmuThread)
      EmuThreadPause(); // Does nothing if already paused

   std::string errorString;
   SaveState::SaveStart state;
   ctx->LockGPU();
   bool retVal = CChunkFileReader::LoadPtr((u8 *)data, size, state, &errorString)
      == CChunkFileReader::ERROR_NONE;
   ctx->UnlockGPU();

   if (useEmuThread)
      EmuThreadStart();

   return retVal;
}

void *retro_get_memory_data(unsigned id)
{
   // Frontends query this right after retro_load_game(), before the
   // deferred boot in retro_run() has mapped PSP memory.
   if (id == RETRO_MEMORY_SYSTEM_RAM && Memory::IsActive())
      return Memory::GetPointerWriteUnchecked(PSP_GetKernelMemoryBase());
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
	if (id == RETRO_MEMORY_SYSTEM_RAM && Memory::IsActive())
		return Memory::g_MemorySize;
	return 0;
}

void retro_cheat_reset(void) {
   pendingCheats.Push(CheatChange{ true, 0, false, {} });
}

void retro_cheat_set(unsigned index, bool enabled, const char *code) {
   if (code)
      pendingCheats.Push(CheatChange{ false, index, enabled, code });
}

static void ApplyCheatReset() {
   // Init Cheat Engine
   CWCheatEngine cheatEngineObj(g_paramSFO.GetDiscID());
   CWCheatEngine *cheatEngine = &cheatEngineObj;
   Path file=cheatEngine->CheatFilename();

   // Output cheats to cheat file
   FILE *outFile = File::OpenCFile(file, "wb");
   if (outFile != nullptr) {
      fprintf(outFile, "_S %s\n", g_paramSFO.GetDiscID().c_str());
      fclose(outFile);
   }

   g_Config.bReloadCheats = true;

   // Parse and Run the Cheats
   cheatEngine->ParseCheats();
   if (cheatEngine->HasCheats()) {
      cheatEngine->Run();
   }

}

static void ApplyCheatSet(unsigned index, bool enabled, const char *code) {
   // Initialize Cheat Engine
   CWCheatEngine cheatEngineObj(g_paramSFO.GetDiscID());
   CWCheatEngine *cheatEngine = &cheatEngineObj;
   cheatEngine->CreateCheatFile();
   Path file=cheatEngine->CheatFilename();

   // Read cheats file
   std::vector<std::string> cheats;
   std::string cheat_content;
   FILE *inFile = File::OpenCFile(file, "rb");
   if (inFile != nullptr) {
      std::array<uint8_t, 4096> buffer;
      for (;;) {
         size_t n = fread(buffer.data(), 1, buffer.size(), inFile);
         cheat_content.append((const char *)buffer.data(), n);
         if (n < buffer.size()) {
            break;
         }
      }
      fclose(inFile);
   }
   std::string existing_cheats=ReplaceAll(cheat_content, std::string("\n_C"), std::string("|"));
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
   FILE *outFile = File::OpenCFile(file, "wb");
   if (outFile != nullptr) {
      fprintf(outFile, "_S %s\n", g_paramSFO.GetDiscID().c_str());
      for (const std::string &cheat : cheats) {
         fprintf(outFile, "_C%s\n", cheat.c_str());
      }
      fclose(outFile);
   }

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

float System_GetPropertyFloat(SystemProperty prop) {
   switch (prop) {
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

bool System_GetPropertyBool(SystemProperty prop) {
   switch (prop) {
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
void System_Toast(std::string_view str) {}
void System_LaunchUrl(LaunchUrlType urlType, std::string_view url) {}

inline int16_t Clamp16(int32_t sample) {
   if (sample < -32767) return -32767;
   if (sample > 32767) return 32767;
   return sample;
}

void System_AudioPushSamples(const int32_t *audio, int numSamples, float volume) {
   // We ignore volume here, because it's handled by libretro presumably.

   if (!audio || numSamples <= 0)
      return;

   if (!output_audio_ring_ok)
      return;

   // Convert straight into the ring. Whole frames only, so every run stays frame-aligned;
   // what doesn't fit is dropped.
   int frames = numSamples;
   while (frames > 0) {
      void *ptr;
      const size_t room = retro_spsc_write_begin(&output_audio_ring, &ptr) / 4;
      if (room == 0)
         break;
      const int n = (int)std::min<size_t>(room, (size_t)frames);
      int16_t *dst = (int16_t *)ptr;
      for (int i = 0; i < n * 2; i++)
         dst[i] = Clamp16(audio[i]);
      retro_spsc_write_end(&output_audio_ring, (size_t)n * 4);
      audio += n * 2;
      frames -= n;
   }
}

void System_AudioGetDebugStats(char *buf, size_t bufSize) { if (buf) buf[0] = '\0'; }
void System_AudioClear() {
   if (!output_audio_ring_ok)
      return;
   retro_atomic_store_release_size(&output_audio_discard_until,
      retro_atomic_load_acquire_size(&output_audio_ring.head));
   retro_atomic_store_release_int(&output_audio_clear_requested, 1);
}
#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(IOS)
bool System_AudioRecordingIsAvailable() { return false; }
bool System_AudioRecordingState() { return false; }
#endif
// Stub for now.
std::vector<std::string> System_GetCameraDeviceList() { return std::vector<std::string>(); }

// TODO: To avoid having to define these here, these should probably be turned into system "requests".
bool NativeSaveSecret(std::string_view nameOfSecret, std::string_view data) { return false; }
std::string NativeLoadSecret(std::string_view nameOfSecret) {
   return "";
}
