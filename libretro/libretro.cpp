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
#include "Core/CoreTiming.h"
#include "Core/HW/Display.h"

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

#define AUDIO_RING_BUFFER_SIZE      (1 << 16)
#define AUDIO_RING_BUFFER_SIZE_MASK (AUDIO_RING_BUFFER_SIZE - 1)
// An alpha factor of 1/180 is *somewhat* equivalent
// to calculating the average for the last 180
// frames, or 3 seconds of runtime...
#define AUDIO_FRAMES_MOVING_AVG_ALPHA (1.0f / 180.0f)

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
static std::string changeProAdhocServer;

namespace Libretro
{
   LibretroGraphicsContext *ctx;
   retro_environment_t environ_cb;
   static retro_audio_sample_batch_t audio_batch_cb;
   static retro_input_poll_t input_poll_cb;
   static retro_input_state_t input_state_cb;
} // namespace Libretro

namespace Libretro
{
   static bool detectVsyncSwapInterval = false;
   static bool detectVsyncSwapIntervalOptShown = true;

   static s64 expectedTimeUsPerRun = 0;
   static uint32_t vsyncSwapInterval = 1;
   static uint32_t vsyncSwapIntervalLast = 1;
   static uint32_t vsyncSwapIntervalCounter = 0;
   static int numVBlanksLast = 0;
   static double fpsTimeLast = 0.0;
   static float runSpeed = 0.0f;
   static s64 runTicksLast = 0;

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

namespace Libretro
{
   static std::mutex audioSampleLock_;
   static int16_t audioRingBuffer[AUDIO_RING_BUFFER_SIZE] = {0};
   static uint32_t audioRingBufferBase = 0;
   static uint32_t audioRingBufferIndex = 0;

   static int16_t *audioOutBuffer = NULL;
   static uint32_t audioOutBufferSize = 0;
   static float audioOutFramesAvg = 0.0f;
   // Set this to an arbitrarily large value,
   // it will be fine tuned in AudioUploadSamples()
   static uint32_t audioBatchFramesMax = AUDIO_RING_BUFFER_SIZE >> 1;

   static void AudioBufferFlush()
   {
      const std::lock_guard<std::mutex> lock(audioSampleLock_);
      audioRingBufferBase = 0;
      audioRingBufferIndex = 0;
      audioOutFramesAvg = (float)SAMPLERATE / (60.0f / 1.001f);
   }

   static void AudioBufferInit()
   {
      audioOutFramesAvg = (float)SAMPLERATE / (60.0f / 1.001f);
      audioOutBufferSize = ((uint32_t)audioOutFramesAvg + 1) * 2;
      audioOutBuffer = (int16_t *)malloc(audioOutBufferSize * sizeof(int16_t));
      audioBatchFramesMax = AUDIO_RING_BUFFER_SIZE >> 1;

      AudioBufferFlush();
   }

   static void AudioBufferDeinit()
   {
      if (audioOutBuffer)
         free(audioOutBuffer);
      audioOutBuffer = NULL;
      audioOutBufferSize = 0;
      audioOutFramesAvg = 0.0f;
      audioBatchFramesMax = AUDIO_RING_BUFFER_SIZE >> 1;

      AudioBufferFlush();
   }

   static uint32_t AudioBufferOccupancy()
   {
      const std::lock_guard<std::mutex> lock(audioSampleLock_);
      uint32_t occupancy = (audioRingBufferIndex - audioRingBufferBase) &
            AUDIO_RING_BUFFER_SIZE_MASK;
      return occupancy >> 1;
   }

   static void AudioBufferWrite(int16_t *audio, uint32_t frames)
   {
      const std::lock_guard<std::mutex> lock(audioSampleLock_);
      uint32_t frameIndex;
      uint32_t bufferIndex = audioRingBufferIndex;

      for (frameIndex = 0; frameIndex < frames; frameIndex++)
      {
         audioRingBuffer[audioRingBufferIndex]     = *(audio++);
         audioRingBuffer[audioRingBufferIndex + 1] = *(audio++);
         audioRingBufferIndex = (audioRingBufferIndex + 2) % AUDIO_RING_BUFFER_SIZE;
      }
   }

   static uint32_t AudioBufferRead(int16_t *audio, uint32_t frames)
   {
      const std::lock_guard<std::mutex> lock(audioSampleLock_);
      uint32_t framesAvailable = ((audioRingBufferIndex - audioRingBufferBase) &
            AUDIO_RING_BUFFER_SIZE_MASK) >> 1;
      uint32_t frameIndex;

      if (frames > framesAvailable)
         frames = framesAvailable;

      for(frameIndex = 0; frameIndex < frames; frameIndex++)
      {
         uint32_t bufferIndex = (audioRingBufferBase + (frameIndex << 1)) &
               AUDIO_RING_BUFFER_SIZE_MASK;
         *(audio++) = audioRingBuffer[bufferIndex];
         *(audio++) = audioRingBuffer[bufferIndex + 1];
      }

      audioRingBufferBase += frames << 1;
      audioRingBufferBase &= AUDIO_RING_BUFFER_SIZE_MASK;

      return frames;
   }

   static void AudioUploadSamples()
   {

      // - If 'Detect Frame Rate Changes' is disabled, then
      //   the  core specifies a fixed frame rate of (60.0f / 1.001f)
      // - At the audio sample rate of 44100, this means the
      //   frontend expects exactly 735.735 sample frames per call of
      //   retro_run()
      // - If g_Config.bRenderDuplicateFrames is enabled and
      //   frameskip is disabled, the mean of the buffer occupancy
      //   willapproximate to this value in most cases
      uint32_t framesAvailable = AudioBufferOccupancy();

      if (framesAvailable > 0)
      {
         // Update 'running average' of buffer occupancy.
         // Note that this is not a true running
         // average, but just a leaky-integrator/
         // exponential moving average, used because
         // it is simple and fast (i.e. requires no
         // window of samples).
         audioOutFramesAvg = (AUDIO_FRAMES_MOVING_AVG_ALPHA * (float)framesAvailable) +
               ((1.0f - AUDIO_FRAMES_MOVING_AVG_ALPHA) * audioOutFramesAvg);
         uint32_t frames = (uint32_t)audioOutFramesAvg;

         if (audioOutBufferSize < (frames << 1))
         {
            audioOutBufferSize = (frames << 1);
            audioOutBuffer     = (int16_t *)realloc(audioOutBuffer,
                  audioOutBufferSize * sizeof(int16_t));
         }

         frames = AudioBufferRead(audioOutBuffer, frames);

         int16_t *audioOutBufferPtr = audioOutBuffer;
         while (frames > 0)
         {
            uint32_t framesToWrite = (frames > audioBatchFramesMax) ?
                  audioBatchFramesMax : frames;
            uint32_t framesWritten = audio_batch_cb(audioOutBufferPtr,
                  framesToWrite);

            if ((framesWritten < framesToWrite) &&
                (framesWritten > 0))
               audioBatchFramesMax = framesWritten;

            frames -= framesToWrite;
            audioOutBufferPtr += framesToWrite << 1;
         }
      }
   }
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
         AudioBufferWrite(audio, samples);
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

      void Show(bool show)
      {
      struct retro_core_option_display optionDisplay{id_, show};
      environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &optionDisplay);
      }

      void Set(const char *val)
      {
      struct retro_variable var{id_, val};
      environ_cb(RETRO_ENVIRONMENT_SET_VARIABLE, &var);
      }

   private:
      const char *id_;
      const char *name_;
      std::string options_;
      std::vector<std::pair<std::string, T>> list_;
};

#define MAC_INITIALIZER_LIST \
{                            \
   {"0", "0"},               \
   {"1", "1"},               \
   {"2", "2"},               \
   {"3", "3"},               \
   {"4", "4"},               \
   {"5", "5"},               \
   {"6", "6"},               \
   {"7", "7"},               \
   {"8", "8"},               \
   {"9", "9"},               \
   {"a", "a"},               \
   {"b", "b"},               \
   {"c", "c"},               \
   {"d", "d"},               \
   {"e", "e"},               \
   {"f", "f"}                \
}

static RetroOption<CPUCore> ppsspp_cpu_core("ppsspp_cpu_core", "CPU Core", { { "JIT", CPUCore::JIT }, { "IR JIT", CPUCore::IR_JIT }, { "Interpreter", CPUCore::INTERPRETER } });
static RetroOption<int> ppsspp_locked_cpu_speed("ppsspp_locked_cpu_speed", "Locked CPU Speed", { { "off", 0 }, { "222MHz", 222 }, { "266MHz", 266 }, { "333MHz", 333 } });
static RetroOption<int> ppsspp_language("ppsspp_language", "Language", { { "Automatic", -1 }, { "English", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH }, { "Japanese", PSP_SYSTEMPARAM_LANGUAGE_JAPANESE }, { "French", PSP_SYSTEMPARAM_LANGUAGE_FRENCH }, { "Spanish", PSP_SYSTEMPARAM_LANGUAGE_SPANISH }, { "German", PSP_SYSTEMPARAM_LANGUAGE_GERMAN }, { "Italian", PSP_SYSTEMPARAM_LANGUAGE_ITALIAN }, { "Dutch", PSP_SYSTEMPARAM_LANGUAGE_DUTCH }, { "Portuguese", PSP_SYSTEMPARAM_LANGUAGE_PORTUGUESE }, { "Russian", PSP_SYSTEMPARAM_LANGUAGE_RUSSIAN }, { "Korean", PSP_SYSTEMPARAM_LANGUAGE_KOREAN }, { "Chinese Traditional", PSP_SYSTEMPARAM_LANGUAGE_CHINESE_TRADITIONAL }, { "Chinese Simplified", PSP_SYSTEMPARAM_LANGUAGE_CHINESE_SIMPLIFIED } });
static RetroOption<int> ppsspp_rendering_mode("ppsspp_rendering_mode", "Rendering Mode", { { "Buffered", FB_BUFFERED_MODE }, { "Skip Buffer Effects", FB_NON_BUFFERED_MODE } });
static RetroOption<bool> ppsspp_auto_frameskip("ppsspp_auto_frameskip", "Auto Frameskip", false);
static RetroOption<int> ppsspp_frameskip("ppsspp_frameskip", "Frameskip", { "Off", "1", "2", "3", "4", "5", "6", "7", "8" });
static RetroOption<int> ppsspp_frameskiptype("ppsspp_frameskiptype", "Frameskip Type", { {"Number of frames", 0}, {"Percent of FPS", 1} });
static RetroOption<int> ppsspp_internal_resolution("ppsspp_internal_resolution", "Internal Resolution (Restart)", 1, { "480x272", "960x544", "1440x816", "1920x1088", "2400x1360", "2880x1632", "3360x1904", "3840x2176", "4320x2448", "4800x2720" });
static RetroOption<int> ppsspp_button_preference("ppsspp_button_preference", "Confirmation Button", { { "Cross", PSP_SYSTEMPARAM_BUTTON_CROSS }, { "Circle", PSP_SYSTEMPARAM_BUTTON_CIRCLE } });
static RetroOption<bool> ppsspp_fast_memory("ppsspp_fast_memory", "Fast Memory (Speedhack)", true);
static RetroOption<bool> ppsspp_block_transfer_gpu("ppsspp_block_transfer_gpu", "Block Transfer GPU", true);
static RetroOption<int> ppsspp_inflight_frames("ppsspp_inflight_frames", "Buffered frames (Slower, less lag, restart)", { { "Up to 2", 2 }, { "Up to 1", 1 }, { "No buffer", 0 }, });
static RetroOption<int> ppsspp_texture_scaling_level("ppsspp_texture_scaling_level", "Texture Scaling Level", { { "Off", 1 }, { "2x", 2 }, { "3x", 3 }, { "4x", 4 }, { "5x", 5 } });
static RetroOption<int> ppsspp_texture_scaling_type("ppsspp_texture_scaling_type", "Texture Scaling Type", { { "xbrz", TextureScalerCommon::XBRZ }, { "hybrid", TextureScalerCommon::HYBRID }, { "bicubic", TextureScalerCommon::BICUBIC }, { "hybrid_bicubic", TextureScalerCommon::HYBRID_BICUBIC } });
static RetroOption<std::string> ppsspp_texture_shader("ppsspp_texture_shader", "Texture Shader (Vulkan only, overrides Texture Scaling Type)", { {"Off", "Off"}, {"2xBRZ", "Tex2xBRZ"}, {"4xBRZ", "Tex4xBRZ"}, {"MMPX", "TexMMPX"} });
static RetroOption<int> ppsspp_texture_filtering("ppsspp_texture_filtering", "Texture Filtering", { { "Auto", 1 }, { "Nearest", 2 }, { "Linear", 3 }, {"Auto max quality", 4}});
static RetroOption<int> ppsspp_texture_anisotropic_filtering("ppsspp_texture_anisotropic_filtering", "Anisotropic Filtering", { "off", "2x", "4x", "8x", "16x" });
static RetroOption<int> ppsspp_lower_resolution_for_effects("ppsspp_lower_resolution_for_effects", "Lower resolution for effects", { {"Off", 0}, {"Safe", 1}, {"Balanced", 2}, {"Aggressive", 3} });
static RetroOption<bool> ppsspp_texture_deposterize("ppsspp_texture_deposterize", "Texture Deposterize", false);
static RetroOption<bool> ppsspp_texture_replacement("ppsspp_texture_replacement", "Texture Replacement", false);
static RetroOption<bool> ppsspp_gpu_hardware_transform("ppsspp_gpu_hardware_transform", "GPU Hardware T&L", true);
static RetroOption<bool> ppsspp_vertex_cache("ppsspp_vertex_cache", "Vertex Cache (Speedhack)", false);
static RetroOption<bool> ppsspp_cheats("ppsspp_cheats", "Internal Cheats Support", false);
static RetroOption<IOTimingMethods> ppsspp_io_timing_method("ppsspp_io_timing_method", "IO Timing Method", { { "Fast", IOTimingMethods::IOTIMING_FAST }, { "Host", IOTimingMethods::IOTIMING_HOST }, { "Simulate UMD delays", IOTimingMethods::IOTIMING_REALISTIC } });
static RetroOption<bool> ppsspp_frame_duplication("ppsspp_frame_duplication", "Duplicate Frames in 30 Hz Games", false);
static RetroOption<bool> ppsspp_detect_vsync_swap_interval("ppsspp_detect_vsync_swap_interval", "Detect Frame Rate Changes (Notify Frontend)", false);
static RetroOption<bool> ppsspp_software_skinning("ppsspp_software_skinning", "Software Skinning", true);
static RetroOption<bool> ppsspp_ignore_bad_memory_access("ppsspp_ignore_bad_memory_access", "Ignore bad memory accesses", true);
static RetroOption<bool> ppsspp_lazy_texture_caching("ppsspp_lazy_texture_caching", "Lazy texture caching (Speedup)", false);
static RetroOption<bool> ppsspp_retain_changed_textures("ppsspp_retain_changed_textures", "Retain changed textures (Speedup, mem hog)", false);
static RetroOption<bool> ppsspp_force_lag_sync("ppsspp_force_lag_sync", "Force real clock sync (Slower, less lag)", false);
static RetroOption<int> ppsspp_spline_quality("ppsspp_spline_quality", "Spline/Bezier curves quality", { {"Low", 0}, {"Medium", 1}, {"High", 2} });
static RetroOption<bool> ppsspp_disable_slow_framebuffer_effects("ppsspp_disable_slow_framebuffer_effects", "Disable slower effects (Speedup)", false);
static RetroOption<bool> ppsspp_enable_wlan("ppsspp_enable_wlan", "Enable Networking/WLAN (beta, may break games)", false);
static RetroOption<std::string> ppsspp_change_mac_address[] = {
    {"ppsspp_change_mac_address01", "MAC address Pt  1: X-:--:--:--:--:--", MAC_INITIALIZER_LIST},
    {"ppsspp_change_mac_address02", "MAC address Pt  2: -X:--:--:--:--:--", MAC_INITIALIZER_LIST},
    {"ppsspp_change_mac_address03", "MAC address Pt  3: --:X-:--:--:--:--", MAC_INITIALIZER_LIST},
    {"ppsspp_change_mac_address04", "MAC address Pt  4: --:-X:--:--:--:--", MAC_INITIALIZER_LIST},
    {"ppsspp_change_mac_address05", "MAC address Pt  5: --:--:X-:--:--:--", MAC_INITIALIZER_LIST},
    {"ppsspp_change_mac_address06", "MAC address Pt  6: --:--:-X:--:--:--", MAC_INITIALIZER_LIST},
    {"ppsspp_change_mac_address07", "MAC address Pt  7: --:--:--:X-:--:--", MAC_INITIALIZER_LIST},
    {"ppsspp_change_mac_address08", "MAC address Pt  8: --:--:--:-X:--:--", MAC_INITIALIZER_LIST},
    {"ppsspp_change_mac_address09", "MAC address Pt  9: --:--:--:--:X-:--", MAC_INITIALIZER_LIST},
    {"ppsspp_change_mac_address10", "MAC address Pt 10: --:--:--:--:-X:--", MAC_INITIALIZER_LIST},
    {"ppsspp_change_mac_address11", "MAC address Pt 11: --:--:--:--:--:X-", MAC_INITIALIZER_LIST},
    {"ppsspp_change_mac_address12", "MAC address Pt 12: --:--:--:--:--:-X", MAC_INITIALIZER_LIST}
};
static RetroOption<int> ppsspp_wlan_channel("ppsspp_wlan_channel", "WLAN channel", {{"Auto", 0}, {"1", 1}, {"6", 6}, {"11", 11}} );
static RetroOption<bool> ppsspp_enable_builtin_pro_ad_hoc_server("ppsspp_enable_builtin_pro_ad_hoc_server", "Enable built-in PRO ad hoc server", false);
static RetroOption<std::string> ppsspp_change_pro_ad_hoc_server_address("ppsspp_change_pro_ad_hoc_server_address", "Change PRO ad hoc server IP address (localhost = multiple instances)", {
    {"socom.cc", "socom.cc"},
    {"psp.gameplayer.club", "psp.gameplayer.club"},
    {"myneighborsushicat.com", "myneighborsushicat.com"},
    {"localhost", "localhost"},
    {"IP address", "IP address"}
});
static RetroOption<int> ppsspp_pro_ad_hoc_ipv4[] = {
   {"ppsspp_pro_ad_hoc_server_address01", "PRO ad hoc server IP address Pt  1: x--.---.---.--- ", 0, 10, 1},
   {"ppsspp_pro_ad_hoc_server_address02", "PRO ad hoc server IP address Pt  2: -x-.---.---.--- ", 0, 10, 1},
   {"ppsspp_pro_ad_hoc_server_address03", "PRO ad hoc server IP address Pt  3: --x.---.---.--- ", 0, 10, 1},
   {"ppsspp_pro_ad_hoc_server_address04", "PRO ad hoc server IP address Pt  4: ---.x--.---.--- ", 0, 10, 1},
   {"ppsspp_pro_ad_hoc_server_address05", "PRO ad hoc server IP address Pt  5: ---.-x-.---.--- ", 0, 10, 1},
   {"ppsspp_pro_ad_hoc_server_address06", "PRO ad hoc server IP address Pt  6: ---.--x.---.--- ", 0, 10, 1},
   {"ppsspp_pro_ad_hoc_server_address07", "PRO ad hoc server IP address Pt  7: ---.---.x--.--- ", 0, 10, 1},
   {"ppsspp_pro_ad_hoc_server_address08", "PRO ad hoc server IP address Pt  8: ---.---.-x-.--- ", 0, 10, 1},
   {"ppsspp_pro_ad_hoc_server_address09", "PRO ad hoc server IP address Pt  9: ---.---.--x.--- ", 0, 10, 1},
   {"ppsspp_pro_ad_hoc_server_address10", "PRO ad hoc server IP address Pt 10: ---.---.---.x-- ", 0, 10, 1},
   {"ppsspp_pro_ad_hoc_server_address11", "PRO ad hoc server IP address Pt 11: ---.---.---.-x- ", 0, 10, 1},
   {"ppsspp_pro_ad_hoc_server_address12", "PRO ad hoc server IP address Pt 12: ---.---.---.--x ", 0, 10, 1}
};
static RetroOption<bool> ppsspp_enable_upnp("ppsspp_enable_upnp", "Enable UPnP (need a few seconds to detect)", false);
static RetroOption<bool> ppsspp_upnp_use_original_port("ppsspp_upnp_use_original_port", "UPnP use original port (enabled = PSP compatibility)", true);
static RetroOption<int> ppsspp_port_offset("ppsspp_port_offset", "Port offset (0 = PSP compatibility)", 0, 65001, 1000);
static RetroOption<int> ppsspp_minimum_timeout("ppsspp_minimum timeout", "Minimum timeout (override in ms, 0 = default))", 0, 5001, 100);
static RetroOption<bool> ppsspp_forced_first_connect("ppsspp_forced_first_connect", "Forced first connect (faster connect)", false);

static bool set_variable_visibility(void)
{
   bool updated = false;

   if (ppsspp_change_pro_ad_hoc_server_address.Update(&changeProAdhocServer))
       updated = true;

   if (changeProAdhocServer == "IP address")
   {
      g_Config.proAdhocServer = "";
      for (int i = 0;;)
      {
         int addressPt = 0;
         ppsspp_pro_ad_hoc_ipv4[i].Show(true);
         ppsspp_pro_ad_hoc_ipv4[i].Update(&addressPt);
         g_Config.proAdhocServer += static_cast<char>('0' + addressPt);

         if (++i == 12)
            break;

         if (i % 3 == 0)
            g_Config.proAdhocServer += '.';
      }
   }
   else
   {
      g_Config.proAdhocServer = changeProAdhocServer;

      for (int i = 0; i < 12; ++i)
         ppsspp_pro_ad_hoc_ipv4[i].Show(false);
   }

   if (ppsspp_enable_upnp.Update(&g_Config.bEnableUPnP))
      updated = true;

   ppsspp_upnp_use_original_port.Show(g_Config.bEnableUPnP);

   bool detectVsyncSwapIntervalOptShownLast = detectVsyncSwapIntervalOptShown;
   bool autoFrameSkip = false;
   int frameSkip = 0;
   bool renderDuplicateFrames = false;

   ppsspp_auto_frameskip.Update(&autoFrameSkip);
   ppsspp_frameskip.Update(&frameSkip);
   ppsspp_frame_duplication.Update(&renderDuplicateFrames);

   detectVsyncSwapIntervalOptShown =
         !autoFrameSkip &&
         (frameSkip == 0) &&
         !renderDuplicateFrames;

   if (detectVsyncSwapIntervalOptShown != detectVsyncSwapIntervalOptShownLast)
   {
      ppsspp_detect_vsync_swap_interval.Show(detectVsyncSwapIntervalOptShown);
      updated = true;
   }

   return updated;
}

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;

   struct retro_core_options_update_display_callback update_display_cb;
   update_display_cb.callback = set_variable_visibility;
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK, &update_display_cb);

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
   vars.push_back(ppsspp_detect_vsync_swap_interval.GetOptions());
   vars.push_back(ppsspp_vertex_cache.GetOptions());
   vars.push_back(ppsspp_fast_memory.GetOptions());
   vars.push_back(ppsspp_block_transfer_gpu.GetOptions());
   vars.push_back(ppsspp_inflight_frames.GetOptions());
   vars.push_back(ppsspp_software_skinning.GetOptions());
   vars.push_back(ppsspp_lazy_texture_caching.GetOptions());
   vars.push_back(ppsspp_retain_changed_textures.GetOptions());
   vars.push_back(ppsspp_force_lag_sync.GetOptions());
   vars.push_back(ppsspp_disable_slow_framebuffer_effects.GetOptions());
   vars.push_back(ppsspp_lower_resolution_for_effects.GetOptions());
   vars.push_back(ppsspp_texture_scaling_level.GetOptions());
   vars.push_back(ppsspp_texture_scaling_type.GetOptions());
   vars.push_back(ppsspp_texture_shader.GetOptions());
   vars.push_back(ppsspp_texture_filtering.GetOptions());
   vars.push_back(ppsspp_texture_deposterize.GetOptions());
   vars.push_back(ppsspp_texture_replacement.GetOptions());
   vars.push_back(ppsspp_io_timing_method.GetOptions());
   vars.push_back(ppsspp_ignore_bad_memory_access.GetOptions());
   vars.push_back(ppsspp_cheats.GetOptions());
   vars.push_back(ppsspp_enable_wlan.GetOptions());
   for (int i = 0; i < 12; ++i)
      vars.push_back(ppsspp_change_mac_address[i].GetOptions());
   vars.push_back(ppsspp_wlan_channel.GetOptions());
   vars.push_back(ppsspp_enable_builtin_pro_ad_hoc_server.GetOptions());
   vars.push_back(ppsspp_change_pro_ad_hoc_server_address.GetOptions());
   for (int i = 0; i < 12; ++i)
      vars.push_back(ppsspp_pro_ad_hoc_ipv4[i].GetOptions());
   vars.push_back(ppsspp_enable_upnp.GetOptions());
   vars.push_back(ppsspp_upnp_use_original_port.GetOptions());
   vars.push_back(ppsspp_port_offset.GetOptions());
   vars.push_back(ppsspp_minimum_timeout.GetOptions());
   vars.push_back(ppsspp_forced_first_connect.GetOptions());
   vars.push_back({});

   environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void *)vars.data());
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
   bool updated = false;

   if (     coreState != CoreState::CORE_POWERUP
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
   ppsspp_io_timing_method.Update((IOTimingMethods *)&g_Config.iIOTimingMethod);
   ppsspp_lower_resolution_for_effects.Update(&g_Config.iBloomHack);
   ppsspp_frame_duplication.Update(&g_Config.bRenderDuplicateFrames);
   ppsspp_detect_vsync_swap_interval.Update(&detectVsyncSwapInterval);
   ppsspp_software_skinning.Update(&g_Config.bSoftwareSkinning);
   ppsspp_ignore_bad_memory_access.Update(&g_Config.bIgnoreBadMemAccess);
   ppsspp_lazy_texture_caching.Update(&g_Config.bTextureBackoffCache);
   ppsspp_retain_changed_textures.Update(&g_Config.bTextureSecondaryCache);
   ppsspp_force_lag_sync.Update(&g_Config.bForceLagSync);
   ppsspp_spline_quality.Update(&g_Config.iSplineBezierQuality);
   ppsspp_disable_slow_framebuffer_effects.Update(&g_Config.bDisableSlowFramebufEffects);
   ppsspp_inflight_frames.Update(&g_Config.iInflightFrames);
   const bool do_scaling_type_update = ppsspp_texture_scaling_type.Update(&g_Config.iTexScalingType);
   const bool do_scaling_level_update = ppsspp_texture_scaling_level.Update(&g_Config.iTexScalingLevel);
   const bool do_texture_shader_update = ppsspp_texture_shader.Update(&g_Config.sTextureShaderName);

   g_Config.bTexHardwareScaling = "Off" != g_Config.sTextureShaderName;

   if (gpu && (do_scaling_type_update || do_scaling_level_update || do_texture_shader_update))
   {
      gpu->ClearCacheNextFrame();
      gpu->Resized();
   }

   ppsspp_language.Update(&g_Config.iLanguage);
   if (g_Config.iLanguage < 0)
      g_Config.iLanguage = get_language_auto();

   g_Config.sLanguageIni = map_psp_language_to_i18n_locale(g_Config.iLanguage);
   i18nrepo.LoadIni(g_Config.sLanguageIni);

   // Cannot detect refresh rate changes if:
   // > Frame skipping is enabled
   // > Frame duplication is enabled
   detectVsyncSwapInterval &=
         !g_Config.bAutoFrameSkip &&
         (g_Config.iFrameSkip == 0) &&
         !g_Config.bRenderDuplicateFrames;

   bool updateAvInfo = false;
   if (!detectVsyncSwapInterval && (vsyncSwapInterval != 1))
   {
      vsyncSwapInterval = 1;
      updateAvInfo = true;
   }

   if (ppsspp_internal_resolution.Update(&g_Config.iInternalResolution) && !PSP_IsInited())
   {
      coreParam.pixelWidth  = coreParam.renderWidth  = g_Config.iInternalResolution * 480;
      coreParam.pixelHeight = coreParam.renderHeight = g_Config.iInternalResolution * 272;

      if (gpu)
      {
         retro_system_av_info avInfo;
         retro_get_system_av_info(&avInfo);
         environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &avInfo);
         updateAvInfo = false;
         gpu->Resized();
      }
   }

   if (updateAvInfo)
   {
      retro_system_av_info avInfo;
      retro_get_system_av_info(&avInfo);
      environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &avInfo);
   }

   bool isFastForwarding = environ_cb(RETRO_ENVIRONMENT_GET_FASTFORWARDING, &isFastForwarding);
   coreParam.fastForward = isFastForwarding;

   ppsspp_enable_wlan.Update(&g_Config.bEnableWlan);
   ppsspp_wlan_channel.Update(&g_Config.iWlanAdhocChannel);
   ppsspp_enable_builtin_pro_ad_hoc_server.Update(&g_Config.bEnableAdhocServer);

   ppsspp_upnp_use_original_port.Update(&g_Config.bUPnPUseOriginalPort);
   ppsspp_port_offset.Update(&g_Config.iPortOffset);
   ppsspp_minimum_timeout.Update(&g_Config.iMinTimeout);
   ppsspp_forced_first_connect.Update(&g_Config.bForcedFirstConnect);

   g_Config.sMACAddress = "";
   for (int i = 0; i < 12;)
   {
      std::string digit;
      ppsspp_change_mac_address[i].Update(&digit);
      g_Config.sMACAddress += digit;

      if (++i == 12)
         break;

      if (i % 2 == 0)
          g_Config.sMACAddress += ":";
   }

   if (g_Config.sMACAddress == "00:00:00:00:00:00")
   {
      g_Config.sMACAddress = CreateRandMAC();

      for (int i = 0; i < 12; ++i)
      {
         std::string digit = {g_Config.sMACAddress[i + i / 2]};
         ppsspp_change_mac_address[i].Set(digit.c_str());
      }
   }

   set_variable_visibility();
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

void retro_init(void)
{
   VsyncSwapIntervalReset();
   AudioBufferInit();

   g_threadManager.Init(cpu_info.num_cores, cpu_info.logical_cpu_count);

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

   if (environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL))
      libretro_supports_bitmasks = true;

   struct retro_log_callback log;
   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
   {
      LogManager::Init(&g_Config.bEnableLogging);
      printfLogger = new PrintfLogger(log);
      LogManager* logman = LogManager::GetInstance();
      logman->RemoveListener(logman->GetConsoleListener());
      logman->RemoveListener(logman->GetDebuggerListener());
      logman->ChangeFileLog(nullptr);
      logman->AddListener(printfLogger);
      logman->SetAllLogLevels(LogTypes::LINFO);
   }

   g_Config.Load("", "");
   g_Config.iInternalResolution = 0;

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

   g_Config.currentDirectory = retro_base_dir;
   g_Config.defaultCurrentDirectory = retro_base_dir;
   g_Config.memStickDirectory = retro_save_dir;
   g_Config.flash0Directory = retro_base_dir / "flash0";
   g_Config.internalDataDirectory = retro_base_dir;
   g_Config.bEnableNetworkChat = false;
   g_Config.bDiscordPresence = false;

   VFSRegister("", new DirectoryAssetReader(retro_base_dir));

   host = new LibretroHost();
}

void retro_deinit(void)
{
   g_threadManager.Teardown();
   LogManager::Shutdown();

   delete printfLogger;
   printfLogger = nullptr;

   delete host;
   host = nullptr;

   libretro_supports_bitmasks = false;

   VsyncSwapIntervalReset();
   AudioBufferDeinit();
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
   info->timing.fps            = (60.0 / 1.001) / (double)vsyncSwapInterval;
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
         AudioBufferFlush();

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
      AudioBufferFlush();

      while (emuThreadState != EmuThreadState::PAUSED)
         sleep_ms(1);
   }

} // namespace Libretro

bool retro_load_game(const struct retro_game_info *game)
{
   retro_pixel_format fmt = retro_pixel_format::RETRO_PIXEL_FORMAT_XRGB8888;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      ERROR_LOG(SYSTEM, "XRGB8888 is not supported.\n");
      return false;
   }

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
   coreParam.graphicsContext = ctx;
   coreParam.gpuCore         = ctx->GetGPUCore();
   coreParam.cpuCore         = (CPUCore)g_Config.iCpuCore;
   check_variables(coreParam);

   std::string error_string;
   if (!PSP_InitStart(coreParam, &error_string))
   {
      ERROR_LOG(BOOT, "%s", error_string.c_str());
      return false;
   }

   set_variable_visibility();

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
}

void retro_reset(void)
{
   std::string error_string;

   PSP_Shutdown();

   if (!PSP_Init(PSP_CoreParameter(), &error_string))
   {
      ERROR_LOG(BOOT, "%s", error_string.c_str());
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
      while (!PSP_InitUpdate(&error_string))
         sleep_ms(4);

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
         VsyncSwapIntervalDetect();
         AudioUploadSamples();
         ctx->SwapBuffers();
         return;
      }

      if (emuThreadState != EmuThreadState::RUNNING)
         EmuThreadStart();

      if (!ctx->ThreadFrame())
      {
         VsyncSwapIntervalDetect();
         AudioUploadSamples();
         return;
      }
   }
   else
      EmuFrame();

   VsyncSwapIntervalDetect();
   AudioUploadSamples();
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
   if(!gpu) { // The HW renderer isn't ready on first pass.
      return 134217728; // 128MB ought to be enough for anybody.
   }

   SaveState::SaveStart state;
   // TODO: Libretro API extension to use the savestate queue
   if (useEmuThread)
      EmuThreadPause();

   return (CChunkFileReader::MeasurePtr(state) + 0x800000)
      & ~0x7FFFFF; // We don't unpause intentionally
}

bool retro_serialize(void *data, size_t size)
{
   if(!gpu) { // The HW renderer isn't ready on first pass.
      return false;
   }

   bool retVal;
   SaveState::SaveStart state;
   // TODO: Libretro API extension to use the savestate queue
   if (useEmuThread)
      EmuThreadPause(); // Does nothing if already paused

   size_t measured = CChunkFileReader::MeasurePtr(state);
   assert(measured <= size);
   auto err = CChunkFileReader::SavePtr((u8 *)data, state, measured);
   retVal = err == CChunkFileReader::ERROR_NONE;

   if (useEmuThread)
   {
      EmuThreadStart();
      sleep_ms(4);
   }

   AudioBufferFlush();

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

   AudioBufferFlush();

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
         // Have to lie here and report 60 Hz instead
         // of (60.0 / 1.001), otherwise the internal
         // stereo resampler will output at the wrong
         // frequency...
         return 60.0f;
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
      return true;
   default:
      return false;
   }
}

std::string System_GetProperty(SystemProperty prop) { return ""; }
std::vector<std::string> System_GetPropertyStringVec(SystemProperty prop) { return std::vector<std::string>(); }

void System_SendMessage(const char *command, const char *parameter) {}
void NativeUpdate() {}
void NativeRender(GraphicsContext *graphicsContext) {}
void NativeResized() {}

void System_Toast(const char *str) {}

#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(IOS)
std::vector<std::string> __cameraGetDeviceList() { return std::vector<std::string>(); }
bool audioRecording_Available() { return false; }
bool audioRecording_State() { return false; }

void System_InputBoxGetString(const std::string &title, const std::string &defaultValue, std::function<void(bool, const std::string &)> cb) { cb(false, ""); }
#endif
