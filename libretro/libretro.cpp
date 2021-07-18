#include "ppsspp_config.h"
#include <cstring>
#include <cassert>
#include <thread>
#include <atomic>
#include <vector>
#include <cstdlib>

#include "Common/CPUDetect.h"
#include "Common/Log.h"
#include "Common/LogManager.h"
#include "Common/System/Display.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/TimeUtil.h"
#include "Common/File/FileUtil.h"
#include "Common/Serialize/Serializer.h"
#include "Common/ConsoleListener.h"
#include "Common/Input/InputState.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Thread/ThreadManager.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/VFS/AssetReader.h"
#include "Common/Data/Text/I18n.h"

#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Core.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceUtility.h"
#include "Core/HLE/__sceAudio.h"
#include "Core/HW/MemoryStick.h"
#include "Core/Host.h"
#include "Core/MemMap.h"
#include "Core/System.h"

#include "GPU/GPUState.h"
#include "GPU/GPUInterface.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/TextureScalerCommon.h"
#include "GPU/Common/PresentationCommon.h"

#include "libretro/libretro.h"
#include "libretro/LibretroGraphicsContext.h"

#if PPSSPP_PLATFORM(ANDROID)
#include <sys/system_properties.h>
#endif

#define DIR_SEP "/"
#ifdef _WIN32
#define DIR_SEP_CHRS "/\\"
#else
#define DIR_SEP_CHRS "/"
#endif

#define SAMPLERATE 44100

static bool libretro_supports_bitmasks = false;

namespace Libretro
{
   LibretroGraphicsContext *ctx;
   retro_environment_t environ_cb;
   static retro_audio_sample_batch_t audio_batch_cb;
   static retro_input_poll_t input_poll_cb;
   static retro_input_state_t input_state_cb;
} // namespace Libretro

using namespace Libretro;

class LibretroHost : public Host
{
   public:
      LibretroHost() {}
      bool InitGraphics(std::string *error_message, GraphicsContext **ctx) override { return true; }
      void ShutdownGraphics() override {}
      void InitSound() override {}
      void UpdateSound() override
      {
         extern int hostAttemptBlockSize;
         const int blockSizeMax = 512;
         static int16_t audio[blockSizeMax * 2];
         assert(hostAttemptBlockSize <= blockSizeMax);

         int samples = __AudioMix(audio, hostAttemptBlockSize, SAMPLERATE);
         audio_batch_cb(audio, samples);
      }
      void ShutdownSound() override {}
      bool IsDebuggingEnabled() override { return false; }
      bool AttemptLoadSymbolMap() override { return false; }
};

class PrintfLogger : public LogListener
{
   public:
      PrintfLogger(retro_log_callback log) : log_(log.log) {}
      void Log(const LogMessage &message)
      {
         switch (message.level)
         {
            case LogTypes::LVERBOSE:
            case LogTypes::LDEBUG:
               log_(RETRO_LOG_DEBUG, "[%s] %s",
                     message.log, message.msg.c_str());
               break;

            case LogTypes::LERROR:
               log_(RETRO_LOG_ERROR, "[%s] %s",
                     message.log, message.msg.c_str());
               break;
            case LogTypes::LNOTICE:
            case LogTypes::LWARNING:
               log_(RETRO_LOG_WARN, "[%s] %s",
                     message.log, message.msg.c_str());
               break;
            case LogTypes::LINFO:
            default:
               log_(RETRO_LOG_INFO, "[%s] %s",
                     message.log, message.msg.c_str());
               break;
         }
      }

   private:
      retro_log_printf_t log_;
};
static PrintfLogger *printfLogger;

template <typename T> class RetroOption
{
   public:
      RetroOption(const char *id, const char *name, std::initializer_list<std::pair<const char *, T>> list) : id_(id), name_(name), list_(list.begin(), list.end()) {}
      RetroOption(const char *id, const char *name, std::initializer_list<const char *> list) : id_(id), name_(name) {
         for (auto option : list)
            list_.push_back({ option, (T)list_.size() });
      }
      RetroOption(const char *id, const char *name, T first, std::initializer_list<const char *> list) : id_(id), name_(name) {
         for (auto option : list)
            list_.push_back({ option, first + (int)list_.size() });
      }
      RetroOption(const char *id, const char *name, T first, int count, int step = 1) : id_(id), name_(name) {
         for (T i = first; i < first + count; i += step)
            list_.push_back({ std::to_string(i), i });
      }
      RetroOption(const char *id, const char *name, bool initial) : id_(id), name_(name) {
         list_.push_back({ initial ? "enabled" : "disabled", initial });
         list_.push_back({ !initial ? "enabled" : "disabled", !initial });
      }

      retro_variable GetOptions()
      {
         if (options_.empty())
         {
            options_ = name_;
            options_.push_back(';');
            for (auto &option : list_)
            {
               if (option.first == list_.begin()->first)
                  options_ += std::string(" ") + option.first;
               else
                  options_ += std::string("|") + option.first;
            }
         }
         return { id_, options_.c_str() };
      }

      bool Update(T *dest)
      {
         retro_variable var{ id_ };
         T val = list_.front().second;

         if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
         {
            for (auto option : list_)
            {
               if (option.first == var.value)
               {
                  val = option.second;
                  break;
               }
            }
         }

         if (*dest != val)
         {
            *dest = val;
            return true;
         }

         return false;
      }

   private:
      const char *id_;
      const char *name_;
      std::string options_;
      std::vector<std::pair<std::string, T>> list_;
};

static RetroOption<CPUCore> ppsspp_cpu_core("ppsspp_cpu_core", "CPU Core", { { "jit", CPUCore::JIT }, { "IR jit", CPUCore::IR_JIT }, { "interpreter", CPUCore::INTERPRETER } });
static RetroOption<int> ppsspp_locked_cpu_speed("ppsspp_locked_cpu_speed", "Locked CPU Speed", { { "off", 0 }, { "222MHz", 222 }, { "266MHz", 266 }, { "333MHz", 333 } });
static RetroOption<int> ppsspp_language("ppsspp_language", "Language", { { "automatic", -1 }, { "english", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH }, { "japanese", PSP_SYSTEMPARAM_LANGUAGE_JAPANESE }, { "french", PSP_SYSTEMPARAM_LANGUAGE_FRENCH }, { "spanish", PSP_SYSTEMPARAM_LANGUAGE_SPANISH }, { "german", PSP_SYSTEMPARAM_LANGUAGE_GERMAN }, { "italian", PSP_SYSTEMPARAM_LANGUAGE_ITALIAN }, { "dutch", PSP_SYSTEMPARAM_LANGUAGE_DUTCH }, { "portuguese", PSP_SYSTEMPARAM_LANGUAGE_PORTUGUESE }, { "russian", PSP_SYSTEMPARAM_LANGUAGE_RUSSIAN }, { "korean", PSP_SYSTEMPARAM_LANGUAGE_KOREAN }, { "chinese_traditional", PSP_SYSTEMPARAM_LANGUAGE_CHINESE_TRADITIONAL }, { "chinese_simplified", PSP_SYSTEMPARAM_LANGUAGE_CHINESE_SIMPLIFIED } });
static RetroOption<int> ppsspp_rendering_mode("ppsspp_rendering_mode", "Rendering Mode", { { "buffered", FB_BUFFERED_MODE }, { "nonbuffered", FB_NON_BUFFERED_MODE } });
static RetroOption<bool> ppsspp_auto_frameskip("ppsspp_auto_frameskip", "Auto Frameskip", false);
static RetroOption<int> ppsspp_frameskip("ppsspp_frameskip", "Frameskip", 0, 10);
static RetroOption<int> ppsspp_frameskiptype("ppsspp_frameskiptype", "Frameskip Type", { {"number of frames", 0}, {"percent of fps", 1} });
static RetroOption<int> ppsspp_internal_resolution("ppsspp_internal_resolution", "Internal Resolution (restart)", 1, { "480x272", "960x544", "1440x816", "1920x1088", "2400x1360", "2880x1632", "3360x1904", "3840x2176", "4320x2448", "4800x2720" });
static RetroOption<int> ppsspp_button_preference("ppsspp_button_preference", "Confirmation Button", { { "cross", PSP_SYSTEMPARAM_BUTTON_CROSS }, { "circle", PSP_SYSTEMPARAM_BUTTON_CIRCLE } });
static RetroOption<bool> ppsspp_fast_memory("ppsspp_fast_memory", "Fast Memory (Speedhack)", true);
static RetroOption<bool> ppsspp_block_transfer_gpu("ppsspp_block_transfer_gpu", "Block Transfer GPU", true);
static RetroOption<int> ppsspp_texture_scaling_level("ppsspp_texture_scaling_level", "Texture Scaling Level", { { "1", 1 }, { "2", 2 }, { "3", 3 }, { "4", 4 }, { "5", 5 }, { "auto", 0 } });
static RetroOption<int> ppsspp_texture_scaling_type("ppsspp_texture_scaling_type", "Texture Scaling Type", { { "xbrz", TextureScalerCommon::XBRZ }, { "hybrid", TextureScalerCommon::HYBRID }, { "bicubic", TextureScalerCommon::BICUBIC }, { "hybrid_bicubic", TextureScalerCommon::HYBRID_BICUBIC } });
static RetroOption<int> ppsspp_texture_filtering("ppsspp_texture_filtering", "Texture Filtering", { { "auto", 1 }, { "nearest", 2 }, { "linear", 3 }, { "linear(FMV)", 4 } });
static RetroOption<int> ppsspp_texture_anisotropic_filtering("ppsspp_texture_anisotropic_filtering", "Anisotropic Filtering", { "off", "1x", "2x", "4x", "8x", "16x" });
static RetroOption<int> ppsspp_lower_resolution_for_effects("ppsspp_lower_resolution_for_effects", "Lower resolution for effects", { {"off", 0}, {"safe", 1}, {"balanced", 2}, {"aggressive", 3} });
static RetroOption<bool> ppsspp_texture_deposterize("ppsspp_texture_deposterize", "Texture Deposterize", false);
static RetroOption<bool> ppsspp_texture_replacement("ppsspp_texture_replacement", "Texture Replacement", false);
static RetroOption<bool> ppsspp_gpu_hardware_transform("ppsspp_gpu_hardware_transform", "GPU Hardware T&L", true);
static RetroOption<bool> ppsspp_vertex_cache("ppsspp_vertex_cache", "Vertex Cache (Speedhack)", false);
static RetroOption<bool> ppsspp_cheats("ppsspp_cheats", "Internal Cheats Support", false);
static RetroOption<bool> ppsspp_io_threading("ppsspp_io_threading", "I/O on thread (experimental)", true);
static RetroOption<IOTimingMethods> ppsspp_io_timing_method("ppsspp_io_timing_method", "IO Timing Method", { { "Fast", IOTimingMethods::IOTIMING_FAST }, { "Host", IOTimingMethods::IOTIMING_HOST }, { "Simulate UMD delays", IOTimingMethods::IOTIMING_REALISTIC } });
static RetroOption<bool> ppsspp_frame_duplication("ppsspp_frame_duplication", "Duplicate frames in 30hz games", false);
static RetroOption<bool> ppsspp_software_skinning("ppsspp_software_skinning", "Software Skinning", true);
static RetroOption<bool> ppsspp_ignore_bad_memory_access("ppsspp_ignore_bad_memory_access", "Ignore bad memory accesses", true);
static RetroOption<bool> ppsspp_lazy_texture_caching("ppsspp_lazy_texture_caching", "Lazy texture caching (speedup)", false);
static RetroOption<bool> ppsspp_retain_changed_textures("ppsspp_retain_changed_textures", "Retain changed textures (speedup, mem hog)", false);
static RetroOption<bool> ppsspp_force_lag_sync("ppsspp_force_lag_sync", "Force real clock sync (slower, less lag)", false);
static RetroOption<int> ppsspp_spline_quality("ppsspp_spline_quality", "Spline/Bezier curves quality", { {"low", 0}, {"medium", 1}, {"high", 2} });
static RetroOption<bool> ppsspp_disable_slow_framebuffer_effects("ppsspp_disable_slow_framebuffer_effects", "Disable slower effects (speedup)", false);

void retro_set_environment(retro_environment_t cb)
{
   std::vector<retro_variable> vars;
   vars.push_back(ppsspp_internal_resolution.GetOptions());
   vars.push_back(ppsspp_cpu_core.GetOptions());
   vars.push_back(ppsspp_locked_cpu_speed.GetOptions());
   vars.push_back(ppsspp_language.GetOptions());
   vars.push_back(ppsspp_button_preference.GetOptions());
   vars.push_back(ppsspp_rendering_mode.GetOptions());
   vars.push_back(ppsspp_gpu_hardware_transform.GetOptions());
   vars.push_back(ppsspp_texture_anisotropic_filtering.GetOptions());
   vars.push_back(ppsspp_spline_quality.GetOptions());
   vars.push_back(ppsspp_auto_frameskip.GetOptions());
   vars.push_back(ppsspp_frameskip.GetOptions());
   vars.push_back(ppsspp_frameskiptype.GetOptions());
   vars.push_back(ppsspp_frame_duplication.GetOptions());
   vars.push_back(ppsspp_vertex_cache.GetOptions());
   vars.push_back(ppsspp_fast_memory.GetOptions());
   vars.push_back(ppsspp_block_transfer_gpu.GetOptions());
   vars.push_back(ppsspp_software_skinning.GetOptions());
   vars.push_back(ppsspp_lazy_texture_caching.GetOptions());
   vars.push_back(ppsspp_retain_changed_textures.GetOptions());
   vars.push_back(ppsspp_force_lag_sync.GetOptions());   
   vars.push_back(ppsspp_disable_slow_framebuffer_effects.GetOptions());
   vars.push_back(ppsspp_lower_resolution_for_effects.GetOptions());
   vars.push_back(ppsspp_texture_scaling_level.GetOptions());
   vars.push_back(ppsspp_texture_scaling_type.GetOptions());
   vars.push_back(ppsspp_texture_filtering.GetOptions());
   vars.push_back(ppsspp_texture_deposterize.GetOptions());
   vars.push_back(ppsspp_texture_replacement.GetOptions());
   vars.push_back(ppsspp_io_threading.GetOptions());
   vars.push_back(ppsspp_io_timing_method.GetOptions());
   vars.push_back(ppsspp_ignore_bad_memory_access.GetOptions());
   vars.push_back(ppsspp_cheats.GetOptions());
   vars.push_back({});

   environ_cb = cb;

   cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void *)vars.data());
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

static void check_variables(CoreParameter &coreParam)
{
   bool updated = false;

   if (     coreState != CORE_POWERUP 
         && environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) 
         && !updated)
      return;

   ppsspp_button_preference.Update(&g_Config.iButtonPreference);
   ppsspp_fast_memory.Update(&g_Config.bFastMemory);
   ppsspp_vertex_cache.Update(&g_Config.bVertexCache);
   ppsspp_gpu_hardware_transform.Update(&g_Config.bHardwareTransform);
   ppsspp_frameskip.Update(&g_Config.iFrameSkip);
   ppsspp_frameskiptype.Update(&g_Config.iFrameSkipType);
   ppsspp_auto_frameskip.Update(&g_Config.bAutoFrameSkip);
   ppsspp_block_transfer_gpu.Update(&g_Config.bBlockTransferGPU);
   ppsspp_texture_filtering.Update(&g_Config.iTexFiltering);
   ppsspp_texture_anisotropic_filtering.Update(&g_Config.iAnisotropyLevel);
   ppsspp_texture_deposterize.Update(&g_Config.bTexDeposterize);
   ppsspp_texture_replacement.Update(&g_Config.bReplaceTextures);
   ppsspp_cheats.Update(&g_Config.bEnableCheats);
   ppsspp_locked_cpu_speed.Update(&g_Config.iLockedCPUSpeed);
   ppsspp_rendering_mode.Update(&g_Config.iRenderingMode);
   ppsspp_cpu_core.Update((CPUCore *)&g_Config.iCpuCore);
   ppsspp_io_threading.Update(&g_Config.bSeparateIOThread);
   ppsspp_io_timing_method.Update((IOTimingMethods *)&g_Config.iIOTimingMethod);
   ppsspp_lower_resolution_for_effects.Update(&g_Config.iBloomHack);
   ppsspp_frame_duplication.Update(&g_Config.bRenderDuplicateFrames);
   ppsspp_software_skinning.Update(&g_Config.bSoftwareSkinning);
   ppsspp_ignore_bad_memory_access.Update(&g_Config.bIgnoreBadMemAccess);
   ppsspp_lazy_texture_caching.Update(&g_Config.bTextureBackoffCache);
   ppsspp_retain_changed_textures.Update(&g_Config.bTextureSecondaryCache);
   ppsspp_force_lag_sync.Update(&g_Config.bForceLagSync);
   ppsspp_spline_quality.Update(&g_Config.iSplineBezierQuality);
   ppsspp_disable_slow_framebuffer_effects.Update(&g_Config.bDisableSlowFramebufEffects);

   ppsspp_language.Update(&g_Config.iLanguage);
   if (g_Config.iLanguage < 0)
      g_Config.iLanguage = get_language_auto();

   g_Config.sLanguageIni = "en_US";
   auto langValuesMapping = GetLangValuesMapping();
   for (auto i = langValuesMapping.begin(); i != langValuesMapping.end(); ++i)
   {
      if (i->second.second == g_Config.iLanguage)
      {
         g_Config.sLanguageIni = i->first;
      }
   }
   i18nrepo.LoadIni(g_Config.sLanguageIni);

   if (!PSP_IsInited() && ppsspp_internal_resolution.Update(&g_Config.iInternalResolution))
   {
      coreParam.pixelWidth  = coreParam.renderWidth  = g_Config.iInternalResolution * 480;
      coreParam.pixelHeight = coreParam.renderHeight = g_Config.iInternalResolution * 272;

      if (gpu)
      {
         retro_system_av_info av_info;
         retro_get_system_av_info(&av_info);
         environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av_info);
         gpu->Resized();
      }
   }

   if (ppsspp_texture_scaling_type.Update(&g_Config.iTexScalingType) && gpu)
      gpu->ClearCacheNextFrame();

   if (ppsspp_texture_scaling_level.Update(&g_Config.iTexScalingLevel) && gpu)
      gpu->ClearCacheNextFrame();
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

void retro_init(void)
{
   struct retro_log_callback log;
#if 0
   g_Config.Load("");
#endif

   g_Config.bEnableLogging = true;
   // libretro does its own timing, so this should stay CONTINUOUS.
   g_Config.iUnthrottleMode = (int)UnthrottleMode::CONTINUOUS;
   g_Config.bMemStickInserted = true;
   g_Config.iGlobalVolume = VOLUME_MAX - 1;
   g_Config.iAltSpeedVolume = -1;
   g_Config.bEnableSound = true;
   g_Config.iCwCheatRefreshRate = 60;
   g_Config.iMemStickSizeGB = 16;
   g_Config.bFuncReplacements = true;
   g_Config.bEncryptSave = true;
   g_Config.bHighQualityDepth = true;
   g_Config.bLoadPlugins = true;
   g_Config.bFragmentTestCache = true;
   g_Config.bSavedataUpgrade= true;
   g_Config.bSeparateSASThread = true;

   g_Config.iFirmwareVersion = PSP_DEFAULT_FIRMWARE;
   g_Config.iPSPModel = PSP_MODEL_SLIM;

   LogManager::Init(&g_Config.bEnableLogging);

   g_threadManager.Init(cpu_info.num_cores, cpu_info.logical_cpu_count);

   host = new LibretroHost;

   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
   {
      printfLogger = new PrintfLogger(log);
      LogManager *logman = LogManager::GetInstance();
      logman->RemoveListener(logman->GetConsoleListener());
      logman->RemoveListener(logman->GetDebuggerListener());
      logman->ChangeFileLog(nullptr);
      logman->AddListener(printfLogger);
#if 1
      logman->SetAllLogLevels(LogTypes::LINFO);
#endif
   }

   if (environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL))
      libretro_supports_bitmasks = true;
}

void retro_deinit(void)
{
   LogManager::Shutdown();

   delete printfLogger;
   printfLogger = nullptr;

   delete host;
   host = nullptr;

   libretro_supports_bitmasks = false;
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
   info->valid_extensions = "elf|iso|cso|prx|pbp";
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   *info = {};
   info->timing.fps            = 60.0f / 1.001f;
   info->timing.sample_rate    = SAMPLERATE;

   info->geometry.base_width   = g_Config.iInternalResolution * 480;
   info->geometry.base_height  = g_Config.iInternalResolution * 272;
   info->geometry.max_width    = g_Config.iInternalResolution * 480;
   info->geometry.max_height   = g_Config.iInternalResolution * 272;
   info->geometry.aspect_ratio = 480.0 / 272.0;  // Not 16:9! But very, very close.
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
      if (ctx->GetDrawContext())
         ctx->GetDrawContext()->BeginFrame();

      gpu->BeginHostFrame();

      coreState = CORE_RUNNING;
      PSP_RunLoopUntil(UINT64_MAX);

      gpu->EndHostFrame();

      if (ctx->GetDrawContext())
         ctx->GetDrawContext()->EndFrame();
   }

   static void EmuThreadFunc()
   {
      SetCurrentThreadName("Emu");

      for (;;)
      {
         switch ((EmuThreadState)emuThreadState)
         {
            case EmuThreadState::START_REQUESTED:
               emuThreadState = EmuThreadState::RUNNING;
               /* fallthrough */
            case EmuThreadState::RUNNING:
               EmuFrame();
               break;
            case EmuThreadState::PAUSE_REQUESTED:
               emuThreadState = EmuThreadState::PAUSED;
               /* fallthrough */
            case EmuThreadState::PAUSED:
               sleep_ms(1);
               break;
            default:
            case EmuThreadState::QUIT_REQUESTED:
               emuThreadState = EmuThreadState::STOPPED;
               ctx->StopThread();
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
      while (ctx->ThreadFrame())
         continue;

      emuThread.join();
      emuThread = std::thread();
      ctx->ThreadEnd();
   }

   void EmuThreadPause()
   {
      if (emuThreadState != EmuThreadState::RUNNING)
         return;

      emuThreadState = EmuThreadState::PAUSE_REQUESTED;

      ctx->ThreadFrame(); // Eat 1 frame

      while (emuThreadState != EmuThreadState::PAUSED)
         sleep_ms(1);
   }

} // namespace Libretro

bool retro_load_game(const struct retro_game_info *game)
{
   static Path retro_base_dir;
   static Path retro_save_dir;
   std::string error_string;
   enum retro_pixel_format fmt          = RETRO_PIXEL_FORMAT_XRGB8888;
   const char *nickname                 = NULL;
   const char *dir_ptr                  = NULL;
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

   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      ERROR_LOG(SYSTEM, "XRGB8888 is not supported.\n");
      return false;
   }

   if (environ_cb(RETRO_ENVIRONMENT_GET_USERNAME, &nickname) && nickname)
      g_Config.sNickName = std::string(nickname);


   if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir_ptr) 
         && dir_ptr)
   {
      retro_base_dir = Path(dir_ptr);
   }

   if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir_ptr) 
         && dir_ptr)
   {
      retro_save_dir = Path(dir_ptr);
   }

   if (retro_base_dir.empty())
      retro_base_dir = Path(game->path).NavigateUp();

   retro_base_dir /= "PPSSPP";

   if (retro_save_dir.empty())
      retro_save_dir = Path(game->path).NavigateUp();

   g_Config.currentDirectory = retro_base_dir;
   g_Config.defaultCurrentDirectory = retro_base_dir;
   g_Config.memStickDirectory     = retro_save_dir;
   g_Config.flash0Directory       = retro_base_dir / "flash0";
   g_Config.internalDataDirectory = retro_base_dir;

   VFSRegister("", new DirectoryAssetReader(retro_base_dir));

   coreState = CORE_POWERUP;
   ctx       = LibretroGraphicsContext::CreateGraphicsContext();
   INFO_LOG(SYSTEM, "Using %s backend", ctx->Ident());

   Core_SetGraphicsContext(ctx);
   SetGPUBackend((GPUBackend)g_Config.iGPUBackend);

   useEmuThread              = ctx->GetGPUCore() == GPUCORE_GLES;

   CoreParameter coreParam   = {};
   coreParam.enableSound     = true;
   coreParam.fileToStart     = Path(std::string(game->path));
   coreParam.mountIso.clear();
   coreParam.startBreak      = false;
   coreParam.printfEmuLog    = true;
   coreParam.headLess        = true;
   coreParam.unthrottle      = true;
   coreParam.graphicsContext = ctx;
   coreParam.gpuCore         = ctx->GetGPUCore();
   coreParam.cpuCore         = CPUCore::JIT;
   check_variables(coreParam);

#if 0
   g_Config.bVertexDecoderJit = (coreParam.cpuCore == CPU_JIT) ? true : false;
#endif

   if (!PSP_InitStart(coreParam, &error_string))
   {
      ERROR_LOG(BOOT, "%s", error_string.c_str());
      return false;
   }

   return true;
}

void retro_unload_game(void)
{
	if (Libretro::useEmuThread)
		Libretro::EmuThreadStop();

	PSP_Shutdown();
	VFSShutdown();

	delete ctx;
	ctx = nullptr;
	PSP_CoreParameter().graphicsContext = nullptr;

   g_threadManager.Teardown();
}

void retro_reset(void)
{
   std::string error_string;

   PSP_Shutdown();

#if 0
   coreState = CORE_POWERUP;
   if (!PSP_InitStart(PSP_CoreParameter(), &error_string))
   {
      ERROR_LOG(BOOT, "%s", error_string.c_str());
      environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, nullptr);
   }
#else
   if (!PSP_Init(PSP_CoreParameter(), &error_string))
   {
      ERROR_LOG(BOOT, "%s", error_string.c_str());
      environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, nullptr);
   }
#endif
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
      bool pressed          = ret & (1 << map[i].retro);

      if (pressed)
      {
         __CtrlButtonDown(map[i].sceCtrl);
      }
      else
      {
         __CtrlButtonUp(map[i].sceCtrl);
      }
   }

   float x_left = input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X) / 32767.0f;
   float y_left = input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y) / -32767.0f;
   float x_right = input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X) / 32767.0f;
   float y_right = input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y) / -32767.0f;
   __CtrlSetAnalogXY(CTRL_STICK_LEFT, x_left, y_left);
   __CtrlSetAnalogXY(CTRL_STICK_RIGHT, x_right, y_right);
}

void retro_run(void)
{
   if (PSP_IsIniting())
   {
      std::string error_string;
#if 0
      if (!PSP_InitUpdate(&error_string))
      {
         graphics_context->SwapBuffers();
         return;
      }
#else
      while (!PSP_InitUpdate(&error_string))
         sleep_ms(4);
#endif

      if (!PSP_IsInited())
      {
         ERROR_LOG(BOOT, "%s", error_string.c_str());
         environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, nullptr);
         return;
      }
   }

   check_variables(PSP_CoreParameter());

   retro_input();

   if (useEmuThread)
   {
      if(   emuThreadState == EmuThreadState::PAUSED || 
            emuThreadState == EmuThreadState::PAUSE_REQUESTED)
      {
         ctx->SwapBuffers();
         return;
      }

      if (emuThreadState != EmuThreadState::RUNNING)
         EmuThreadStart();

      if (!ctx->ThreadFrame())
         return;
   }
   else
      EmuFrame();

   ctx->SwapBuffers();
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
   SaveState::SaveStart state;
   // TODO: Libretro API extension to use the savestate queue
   if (useEmuThread)
      EmuThreadPause();

   return (CChunkFileReader::MeasurePtr(state) + 0x800000) 
      & ~0x7FFFFF; // We don't unpause intentionally
}

bool retro_serialize(void *data, size_t size)
{
   bool retVal;
   SaveState::SaveStart state;
   // TODO: Libretro API extension to use the savestate queue
   if (useEmuThread)
      EmuThreadPause(); // Does nothing if already paused

   assert(CChunkFileReader::MeasurePtr(state) <= size);
   retVal = CChunkFileReader::SavePtr((u8 *)data, state) 
      == CChunkFileReader::ERROR_NONE;

   if (useEmuThread)
   {
      EmuThreadStart();
      sleep_ms(4);
   }

   return retVal;
}

bool retro_unserialize(const void *data, size_t size)
{
   bool retVal;
   SaveState::SaveStart state;
   // TODO: Libretro API extension to use the savestate queue
   if (useEmuThread)
      EmuThreadPause(); // Does nothing if already paused

   std::string errorString;
   retVal = CChunkFileReader::LoadPtr((u8 *)data, state, &errorString) 
      == CChunkFileReader::ERROR_NONE;

   if (useEmuThread)
   {
      EmuThreadStart();
      sleep_ms(4);
   }

   return retVal;
}

void *retro_get_memory_data(unsigned id)
{
   if ( id == RETRO_MEMORY_SYSTEM_RAM )
      return Memory::GetPointerUnchecked(PSP_GetKernelMemoryBase()) ;
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
	if ( id == RETRO_MEMORY_SYSTEM_RAM )
		return Memory::g_MemorySize ;
	return 0;
}

void retro_cheat_reset(void) {}

void retro_cheat_set(unsigned index, bool enabled, const char *code) { }

int System_GetPropertyInt(SystemProperty prop)
{
   switch (prop)
   {
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
         return 60.f;
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

std::string System_GetProperty(SystemProperty prop) { return ""; }
std::vector<std::string> System_GetPropertyStringVec(SystemProperty prop) { return std::vector<std::string>(); }

void System_SendMessage(const char *command, const char *parameter) {}
void NativeUpdate() {}
void NativeRender(GraphicsContext *graphicsContext) {}
void NativeResized() {}

#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(IOS)
std::vector<std::string> __cameraGetDeviceList() { return std::vector<std::string>(); }
bool audioRecording_Available() { return false; }
bool audioRecording_State() { return false; }

void System_InputBoxGetString(const std::string &title, const std::string &defaultValue, std::function<void(bool, const std::string &)> cb) { cb(false, ""); }
#endif
