/* Headless libretro frontend for running pspautotests against the core.
 * Prints the emulated program's stdout/stderr lines, then a summary with
 * frame/audio counts and FNV-1a digests of the video and audio output.
 * LRTEST_AUDIO names a file to write the audio to, as raw s16 stereo;
 * LRTEST_VIDEO one to write each frame to, packed rows of the core's pixels. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "libretro.h"

static bool shutdown_requested;
static bool exit_seen;
static unsigned pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
static uint64_t video_digest = 1469598103934665603ULL;
static uint64_t audio_digest = 1469598103934665603ULL;
static unsigned long frames_video, frames_dupe;
static unsigned long audio_frames;
static const char *system_dir;
static const char *save_dir;
static FILE *log_out;
static FILE *audio_out;
static FILE *video_out;
static bool verbose;

static void fnv(uint64_t *h, const void *data, size_t len)
{
   const unsigned char *p = (const unsigned char *)data;
   size_t i;
   for (i = 0; i < len; i++)
   {
      *h ^= p[i];
      *h *= 1099511628211ULL;
   }
}

static void core_log(enum retro_log_level level, const char *fmt, ...)
{
   char buf[4096];
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(buf, sizeof(buf), fmt, ap);
   va_end(ap);
   if (strstr(buf, "sceKernelExitGame"))
      exit_seen = true;
   /* The emulated program's output is logged as "stdout: ..." / "stderr: ...". */
   {
      const char *p = strstr(buf, "stdout: ");
      if (!p)
         p = strstr(buf, "stderr: ");
      if (p)
         fputs(p, stdout);
   }
   if (log_out)
      fputs(buf, log_out);
   if (verbose)
      fputs(buf, stderr);
   (void)level;
}

static const char *option_value(const char *key)
{
   /* LRTEST_OPT="key=value;key=value" */
   static char storage[64][256];
   static int used;
   const char *env = getenv("LRTEST_OPT");
   size_t klen = strlen(key);
   while (env && *env)
   {
      const char *end = strchr(env, ';');
      size_t len = end ? (size_t)(end - env) : strlen(env);
      if (len > klen && !strncmp(env, key, klen) && env[klen] == '=')
      {
         char *dst = storage[used++ & 63];
         size_t vlen = len - klen - 1;
         if (vlen > 255)
            vlen = 255;
         memcpy(dst, env + klen + 1, vlen);
         dst[vlen] = '\0';
         return dst;
      }
      env = end ? end + 1 : NULL;
   }
   return NULL;
}

static bool environ_cb(unsigned cmd, void *data)
{
   switch (cmd)
   {
      case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
         ((struct retro_log_callback *)data)->log = core_log;
         return true;
      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
         *(const char **)data = system_dir;
         return true;
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
         *(const char **)data = save_dir;
         return true;
      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
         pixel_format = *(const enum retro_pixel_format *)data;
         return true;
      case RETRO_ENVIRONMENT_GET_VARIABLE:
      {
         struct retro_variable *var = (struct retro_variable *)data;
         var->value = option_value(var->key);
         return var->value != NULL;
      }
      case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
         *(bool *)data = false;
         return true;
      case RETRO_ENVIRONMENT_SHUTDOWN:
         shutdown_requested = true;
         return true;
      case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
         return true;
      case RETRO_ENVIRONMENT_SET_GEOMETRY:
      case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
      case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
      case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
      case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
         return true;
      case RETRO_ENVIRONMENT_SET_HW_RENDER:
      case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
         /* Software rendering only. */
         return false;
      default:
         return false;
   }
}

static void video_cb(const void *data, unsigned width, unsigned height, size_t pitch)
{
   unsigned y;
   unsigned bpp = pixel_format == RETRO_PIXEL_FORMAT_XRGB8888 ? 4 : 2;
   if (!data)
   {
      frames_dupe++;
      return;
   }
   frames_video++;
   for (y = 0; y < height; y++)
   {
      fnv(&video_digest, (const char *)data + y * pitch, (size_t)width * bpp);
      if (video_out)
         fwrite((const char *)data + y * pitch, bpp, width, video_out);
   }
}

static size_t audio_batch_cb(const int16_t *data, size_t frames)
{
   fnv(&audio_digest, data, frames * 4);
   if (audio_out)
      fwrite(data, 4, frames, audio_out);
   audio_frames += frames;
   return frames;
}

static void audio_cb(int16_t l, int16_t r)
{
   int16_t s[2];
   s[0] = l;
   s[1] = r;
   audio_batch_cb(s, 1);
}

static void input_poll_cb(void) { }
static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id) { return 0; }

#define SYM(name) do { *(void **)&p_##name = dlsym(h, #name); if (!p_##name) { fprintf(stderr, "missing %s\n", #name); return 2; } } while (0)

int main(int argc, char **argv)
{
   void (*p_retro_init)(void);
   void (*p_retro_deinit)(void);
   void (*p_retro_set_environment)(retro_environment_t);
   void (*p_retro_set_video_refresh)(retro_video_refresh_t);
   void (*p_retro_set_audio_sample)(retro_audio_sample_t);
   void (*p_retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
   void (*p_retro_set_input_poll)(retro_input_poll_t);
   void (*p_retro_set_input_state)(retro_input_state_t);
   bool (*p_retro_load_game)(const struct retro_game_info *);
   void (*p_retro_unload_game)(void);
   void (*p_retro_run)(void);
   size_t (*p_retro_serialize_size)(void);
   bool (*p_retro_serialize)(void *, size_t);
   bool (*p_retro_unserialize)(const void *, size_t);
   void (*p_retro_reset)(void);
   struct retro_game_info info;
   void *h;
   int max_frames, i;
   int state_every = 0;
   int reset_at = -1;
   int frames_after_exit = 5;
   int ran = 0;
   int boot_polls = 0;
   bool booted = false;

   if (argc < 4)
   {
      fprintf(stderr, "usage: %s core.so content max_frames [state_every] [reset_at]\n", argv[0]);
      return 2;
   }
   max_frames = atoi(argv[3]);
   if (argc > 4)
      state_every = atoi(argv[4]);
   if (argc > 5)
      reset_at = atoi(argv[5]);
   system_dir = getenv("LRTEST_SYSTEM") ? getenv("LRTEST_SYSTEM") : "/tmp";
   save_dir = getenv("LRTEST_SAVE") ? getenv("LRTEST_SAVE") : "/tmp";
   verbose = getenv("LRTEST_VERBOSE") != NULL;
   if (getenv("LRTEST_LOG"))
      log_out = fopen(getenv("LRTEST_LOG"), "w");
   if (getenv("LRTEST_VIDEO"))
      video_out = fopen(getenv("LRTEST_VIDEO"), "wb");
   if (getenv("LRTEST_AUDIO"))
      audio_out = fopen(getenv("LRTEST_AUDIO"), "wb");

   h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
   if (!h)
   {
      fprintf(stderr, "dlopen: %s\n", dlerror());
      return 2;
   }
   SYM(retro_init); SYM(retro_deinit); SYM(retro_set_environment);
   SYM(retro_set_video_refresh); SYM(retro_set_audio_sample);
   SYM(retro_set_audio_sample_batch); SYM(retro_set_input_poll);
   SYM(retro_set_input_state); SYM(retro_load_game); SYM(retro_unload_game);
   SYM(retro_run); SYM(retro_serialize_size); SYM(retro_serialize);
   SYM(retro_unserialize); SYM(retro_reset);

   p_retro_set_environment(environ_cb);
   p_retro_set_video_refresh(video_cb);
   p_retro_set_audio_sample(audio_cb);
   p_retro_set_audio_sample_batch(audio_batch_cb);
   p_retro_set_input_poll(input_poll_cb);
   p_retro_set_input_state(input_state_cb);
   p_retro_init();

   memset(&info, 0, sizeof(info));
   info.path = argv[2];
   if (!p_retro_load_game(&info))
   {
      fprintf(stderr, "retro_load_game failed\n");
      p_retro_deinit();
      return 3;
   }

   for (i = 0; i < max_frames && !shutdown_requested; i++)
   {
      unsigned long before = frames_video + audio_frames;
      p_retro_run();
      /* While booting, retro_run returns straight away with a dupe frame.
       * Those don't count; give up after 60 s of them. */
      if (frames_video + audio_frames == before && !booted)
      {
         if (++boot_polls > 60000)
         {
            fprintf(stderr, "boot timeout\n");
            break;
         }
         usleep(1000);
         i--;
         continue;
      }
      booted = true;
      ran++;
      if (exit_seen && --frames_after_exit <= 0)
         break;
      if (state_every > 0 && i > 0 && i % state_every == 0)
      {
         size_t size = p_retro_serialize_size();
         void *buf = malloc(size);
         if (buf && p_retro_serialize(buf, size))
            p_retro_unserialize(buf, size);
         free(buf);
      }
      if (i == reset_at)
         p_retro_reset();
   }

   p_retro_unload_game();
   p_retro_deinit();
   printf("@@ frames=%d video=%lu dupe=%lu audio_frames=%lu video_digest=%016llx audio_digest=%016llx exit=%d\n",
         ran, frames_video, frames_dupe, audio_frames,
         (unsigned long long)video_digest, (unsigned long long)audio_digest, exit_seen ? 1 : 0);
   if (log_out)
      fclose(log_out);
   if (audio_out)
      fclose(audio_out);
   if (video_out)
      fclose(video_out);
   dlclose(h);
   return 0;
}
