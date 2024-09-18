#ifndef LIBRETRO_CORE_OPTIONS_H__
#define LIBRETRO_CORE_OPTIONS_H__

#include <stdlib.h>
#include <string.h>

#include "libretro.h"
#include "retro_inline.h"

#ifndef HAVE_NO_LANGEXTRA
#include "libretro_core_options_intl.h"
#endif

#define BOOL_OPTIONS     \
{                        \
   { "disabled", NULL }, \
   { "enabled",  NULL }, \
   { NULL, NULL },       \
}

#define MAC_ADDRESS_OPTIONS \
{                           \
   { "0", NULL },           \
   { "1", NULL },           \
   { "2", NULL },           \
   { "3", NULL },           \
   { "4", NULL },           \
   { "5", NULL },           \
   { "6", NULL },           \
   { "7", NULL },           \
   { "8", NULL },           \
   { "9", NULL },           \
   { "a", NULL },           \
   { "b", NULL },           \
   { "c", NULL },           \
   { "d", NULL },           \
   { "e", NULL },           \
   { "f", NULL },           \
   { NULL, NULL },          \
}

#define IP_ADDRESS_OPTIONS \
{                          \
   { "0", NULL },          \
   { "1", NULL },          \
   { "2", NULL },          \
   { "3", NULL },          \
   { "4", NULL },          \
   { "5", NULL },          \
   { "6", NULL },          \
   { "7", NULL },          \
   { "8", NULL },          \
   { "9", NULL },          \
   { NULL, NULL },         \
}

/*
 ********************************
 * VERSION: 2.0
 ********************************
 *
 * - 2.0: Add support for core options v2 interface
 * - 1.3: Move translations to libretro_core_options_intl.h
 *        - libretro_core_options_intl.h includes BOM and utf-8
 *          fix for MSVC 2010-2013
 *        - Added HAVE_NO_LANGEXTRA flag to disable translations
 *          on platforms/compilers without BOM support
 * - 1.2: Use core options v1 interface when
 *        RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION is >= 1
 *        (previously required RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION == 1)
 * - 1.1: Support generation of core options v0 retro_core_option_value
 *        arrays containing options with a single value
 * - 1.0: First commit
*/

#ifdef __cplusplus
extern "C" {
#endif

/*
 ********************************
 * Core Option Definitions
 ********************************
*/

/* RETRO_LANGUAGE_ENGLISH */

/* Default language:
 * - All other languages must include the same keys and values
 * - Will be used as a fallback in the event that frontend language
 *   is not available
 * - Will be used as a fallback for any missing entries in
 *   frontend language definition */

struct retro_core_option_v2_category option_cats_us[] = {
   {
      "system",
      "System",
      "Configure system options."
   },
   {
      "video",
      "Video",
      "Configure video options."
   },
   {
      "input",
      "Input",
      "Configure input options."
   },
   {
      "hacks",
      "Hacks",
      "Configure speed and emulation hacks. Can cause rendering errors!"
   },
   {
      "network",
      "Network",
      "Configure network options."
   },
   { NULL, NULL, NULL },
};

struct retro_core_option_v2_definition option_defs_us[] = {
   {
      "ppsspp_cpu_core",
      "CPU Core",
      NULL,
      NULL,
      NULL,
      "system",
      {
         { "JIT",         "Dynarec (JIT)" },
         { "IR JIT",      "IR Interpreter" },
         { "Interpreter", NULL },
         { NULL, NULL },
      },
      "JIT"
   },
   {
      "ppsspp_fast_memory",
      "Fast Memory",
      NULL,
      "Unstable.",
      NULL,
      "system",
      BOOL_OPTIONS,
      "enabled"
   },
   {
      "ppsspp_ignore_bad_memory_access",
      "Ignore Bad Memory Accesses",
      NULL,
      NULL,
      NULL,
      "system",
      BOOL_OPTIONS,
      "enabled"
   },
   {
      "ppsspp_io_timing_method",
      "I/O Timing Method",
      NULL,
      NULL,
      NULL,
      "system",
      {
         { "Fast",                NULL },
         { "Host",                NULL },
         { "Simulate UMD delays", NULL },
         { NULL, NULL },
      },
      "Fast"
   },
   {
      "ppsspp_force_lag_sync",
      "Force Real Clock Sync",
      NULL,
      "Slower, less lag.",
      NULL,
      "system",
      BOOL_OPTIONS,
      "disabled"
   },
   {
      "ppsspp_locked_cpu_speed",
      "Locked CPU Speed",
      NULL,
      NULL,
      NULL,
      "system",
      {
         { "disabled", NULL },
         { "222MHz",   NULL },
         { "266MHz",   NULL },
         { "333MHz",   NULL },
         { "444MHz",   NULL },
         { "555MHz",   NULL },
         { "666MHz",   NULL },
         { "777MHz",   NULL },
         { "888MHz",   NULL },
         { "999MHz",   NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "ppsspp_memstick_inserted",
      "Memory Stick Inserted",
      NULL,
      "Some games require ejecting/inserting the Memory Stick.",
      NULL,
      "system",
      BOOL_OPTIONS,
      "enabled"
   },
   {
      "ppsspp_cache_iso",
      "Cache Full ISO in RAM",
      NULL,
      NULL,
      NULL,
      "system",
      BOOL_OPTIONS,
      "disabled"
   },
   {
      "ppsspp_cheats",
      "Internal Cheats Support",
      NULL,
      NULL,
      NULL,
      "system",
      BOOL_OPTIONS,
      "disabled"
   },
   {
      "ppsspp_language",
      "Game Language",
      NULL,
      "'Automatic' will use the frontend language.",
      NULL,
      "system",
      {
         { "Automatic",           NULL },
         { "English",             NULL },
         { "Japanese",            NULL },
         { "French",              NULL },
         { "Spanish",             NULL },
         { "German",              NULL },
         { "Italian",             NULL },
         { "Dutch",               NULL },
         { "Portuguese",          NULL },
         { "Russian",             NULL },
         { "Korean",              NULL },
         { "Chinese Traditional", NULL },
         { "Chinese Simplified",  NULL },
         { NULL, NULL },
      },
      "Automatic"
   },
   {
      "ppsspp_psp_model",
      "PSP Model",
      NULL,
      NULL,
      NULL,
      "system",
      {
         { "psp_1000",      "PSP-1000" },
         { "psp_2000_3000", "PSP-2000/3000" },
         { NULL, NULL },
      },
      "psp_2000_3000"
   },
   {
      "ppsspp_backend",
      "Backend",
      NULL,
      "'Automatic' will use the frontend video driver. Core restart required.",
      NULL,
      "video",
      {
         { "auto",   "Automatic" },
         { "opengl", "OpenGL" },
#ifndef HAVE_LIBNX
         { "vulkan", "Vulkan" },
#endif
#ifdef _WIN32
         { "d3d11",  "D3D11" },
#endif
         { "none",   "None" },
         { NULL, NULL },
      },
      "auto"
   },
   {
      "ppsspp_software_rendering",
      "Software Rendering",
      NULL,
      "Slow, accurate. Core restart required.",
      NULL,
      "video",
      BOOL_OPTIONS,
      "disabled"
   },
   {
      "ppsspp_internal_resolution",
      "Rendering Resolution",
      NULL,
      "Core restart required with Vulkan.",
      NULL,
      "video",
      {
         { "480x272",   "1x (480x272)" },
         { "960x544",   "2x (960x544)" },
         { "1440x816",  "3x (1440x816)" },
         { "1920x1088", "4x (1920x1088)" },
         { "2400x1360", "5x (2400x1360)" },
         { "2880x1632", "6x (2880x1632)" },
         { "3360x1904", "7x (3360x1904)" },
         { "3840x2176", "8x (3840x2176)" },
         { "4320x2448", "9x (4320x2448)" },
         { "4800x2720", "10x (4800x2720)" },
         { NULL, NULL },
      },
      "480x272"
   },
#if 0 // see issue #16786
   {
      "ppsspp_mulitsample_level",
      "MSAA Antialiasing (Vulkan Only)",
      NULL,
      NULL,
      NULL,
      "video",
      {
         {"Disabled", NULL},
         {"x2", NULL},
         {"x4", NULL},
         {"x8", NULL},
         {NULL, NULL}
      },
      "Disabled"
   },
#endif
   {
      "ppsspp_cropto16x9",
      "Crop to 16x9",
      NULL,
      "Remove one line from top and bottom to get exact 16:9. Core restart required with Vulkan.",
      NULL,
      "video",
      BOOL_OPTIONS,
      "enabled"
   },
   {
      "ppsspp_frameskip",
      "Frameskip",
      NULL,
      NULL,
      NULL,
      "video",
      {
         { "disabled", NULL },
         { "1",        NULL },
         { "2",        NULL },
         { "3",        NULL },
         { "4",        NULL },
         { "5",        NULL },
         { "6",        NULL },
         { "7",        NULL },
         { "8",        NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "ppsspp_frameskiptype",
      "Frameskip Type",
      NULL,
      NULL,
      NULL,
      "video",
      {
         { "Number of frames", NULL },
         { "Percent of FPS",   NULL },
         { NULL, NULL },
      },
      "Number of frames"
   },
   {
      "ppsspp_auto_frameskip",
      "Auto Frameskip",
      NULL,
      NULL,
      NULL,
      "video",
      BOOL_OPTIONS,
      "disabled"
   },
   {
      "ppsspp_frame_duplication",
      "Render Duplicate Frames to 60 Hz",
      NULL,
      "Can make framerate smoother in games that run at lower framerates.",
      NULL,
      "video",
      BOOL_OPTIONS,
      "enabled"
   },
   {
      "ppsspp_detect_vsync_swap_interval",
      "Detect Frame Rate Changes",
      NULL,
      "Notify frontend.",
      NULL,
      "video",
      BOOL_OPTIONS,
      "disabled"
   },
   {
      "ppsspp_inflight_frames",
      "Buffer Graphics Commands",
      NULL,
      "GL/Vulkan only, slower, less lag, restart.",
      NULL,
      "video",
      {
         { "No buffer", NULL },
         { "Up to 1",   NULL },
         { "Up to 2",   NULL },
         { NULL, NULL },
      },
      "Up to 2"
   },
   {
      "ppsspp_button_preference",
      "Confirmation Button",
      NULL,
      NULL,
      NULL,
      "input",
      {
         { "Cross",  NULL },
         { "Circle", NULL },
         { NULL, NULL },
      },
      "Cross"
   },
   {
      "ppsspp_analog_is_circular",
      "Analog Circle vs Square Gate Compensation",
      NULL,
      NULL,
      NULL,
      "input",
      BOOL_OPTIONS,
      "disabled"
   },
   {
      "ppsspp_skip_buffer_effects",
      "Skip Buffer Effects",
      NULL,
      "Faster, but nothing may draw in some games.",
      NULL,
      "hacks",
      BOOL_OPTIONS,
      "disabled"
   },
   {
      "ppsspp_disable_range_culling",
      "Disable Culling",
      NULL,
      "",
      NULL,
      "hacks",
      BOOL_OPTIONS,
      "disabled"
   },
   {
      "ppsspp_skip_gpu_readbacks",
      "Skip GPU Readbacks",
      NULL,
      "Some games require GPU readbacks, so be careful.",
      NULL,
      "hacks",
      BOOL_OPTIONS,
      "disabled"
   },
   {
      "ppsspp_lazy_texture_caching",
      "Lazy Texture Caching (Speedup)",
      NULL,
      "Faster, but can cause text problems in a few games.",
      NULL,
      "hacks",
      BOOL_OPTIONS,
      "disabled"
   },
   {
      "ppsspp_spline_quality",
      "Spline/Bezier Curves Quality",
      NULL,
      "Only used by some games, controls smoothness of curves.",
      NULL,
      "hacks",
      {
         { "Low",    NULL },
         { "Medium", NULL },
         { "High",   NULL },
         { NULL, NULL },
      },
      "High"
   },
   {
      "ppsspp_lower_resolution_for_effects",
      "Lower Resolution for Effects",
      NULL,
      "Reduces artifacts.",
      NULL,
      "hacks",
      {
         { "disabled",   NULL },
         { "Safe",       NULL },
         { "Balanced",   NULL },
         { "Aggressive", NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "ppsspp_gpu_hardware_transform",
      "Hardware Transform",
      NULL,
      NULL,
      NULL,
      "video",
      BOOL_OPTIONS,
      "enabled"
   },
   {
      "ppsspp_software_skinning",
      "Software Skinning",
      NULL,
      "Combine skinned model draws on the CPU, faster in most games.",
      NULL,
      "video",
      BOOL_OPTIONS,
      "enabled"
   },
   {
      "ppsspp_hardware_tesselation",
      "Hardware Tesselation",
      NULL,
      "Uses hardware to make curves.",
      NULL,
      "video",
      BOOL_OPTIONS,
      "disabled"
   },
   {
      "ppsspp_vertex_cache",
      "Vertex Cache",
      NULL,
      "Faster, but may cause temporary flicker.",
      NULL,
      "video",
      BOOL_OPTIONS,
      "disabled"
   },
   {
      "ppsspp_texture_scaling_type",
      "Texture Upscale Type",
      NULL,
      NULL,
      NULL,
      "video",
      {
         { "xbrz",           "xBRZ" },
         { "hybrid",         "Hybrid" },
         { "bicubic",        "Bicubic" },
         { "hybrid_bicubic", "Hybrid + Bicubic" },
         { NULL, NULL },
      },
      "xbrz"
   },
   {
      "ppsspp_texture_scaling_level",
      "Texture Upscaling Level",
      NULL,
      "CPU heavy, some scaling may be delayed to avoid stutter.",
      NULL,
      "video",
      {
         { "disabled", NULL },
         { "2x",       NULL },
         { "3x",       NULL },
         { "4x",       NULL },
         { "5x",       NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "ppsspp_texture_deposterize",
      "Texture Deposterize",
      NULL,
      "Fixes visual banding glitches in upscaled textures.",
      NULL,
      "video",
      BOOL_OPTIONS,
      "disabled"
   },
   {
      "ppsspp_texture_shader",
      "Texture Shader",
      NULL,
      "Vulkan only, overrides 'Texture Scaling Type'.",
      NULL,
      "video",
      {
         { "disabled", NULL },
         { "2xBRZ",    "Tex2xBRZ" },
         { "4xBRZ",    "Tex4xBRZ" },
         { "MMPX",     "TexMMPX" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "ppsspp_texture_anisotropic_filtering",
      "Anisotropic Filtering",
      NULL,
      NULL,
      NULL,
      "video",
      {
         { "disabled", NULL },
         { "2x",       NULL },
         { "4x",       NULL },
         { "8x",       NULL },
         { "16x",      NULL },
         { NULL, NULL },
      },
      "16x"
   },
   {
      "ppsspp_texture_filtering",
      "Texture Filtering",
      NULL,
      NULL,
      NULL,
      "video",
      {
         { "Auto",             NULL },
         { "Nearest",          NULL },
         { "Linear",           NULL },
         { "Auto max quality", NULL },
         { NULL, NULL },
      },
      "Auto"
   },
   {
      "ppsspp_smart_2d_texture_filtering",
      "Smart 2D Texture Filtering",
      NULL,
      "Gets rid of some visual artifacts caused by unnecessary texture filtering in some 2D games, by switching to nearest filtering.",
      NULL,
      "video",
      BOOL_OPTIONS,
      "disabled"
   },
   {
      "ppsspp_texture_replacement",
      "Texture Replacement",
      NULL,
      NULL,
      NULL,
      "video",
      BOOL_OPTIONS,
      "disabled"
   },
   {
      "ppsspp_enable_wlan",
      "Enable Networking/WLAN (Beta, may break games)",
      NULL,
      NULL,
      NULL,
      "network",
      BOOL_OPTIONS,
      "disabled"
   },
   {
      "ppsspp_change_mac_address01",
      "MAC Address Pt  1: X-:--:--:--:--:--",
      NULL,
      NULL,
      NULL,
      "network",
      MAC_ADDRESS_OPTIONS,
      "0"
   },
   {
      "ppsspp_change_mac_address02",
      "MAC Address Pt  2: -X:--:--:--:--:--",
      NULL,
      NULL,
      NULL,
      "network",
      MAC_ADDRESS_OPTIONS,
      "0"
   },
   {
      "ppsspp_change_mac_address03",
      "MAC Address Pt  3: --:X-:--:--:--:--",
      NULL,
      NULL,
      NULL,
      "network",
      MAC_ADDRESS_OPTIONS,
      "0"
   },
   {
      "ppsspp_change_mac_address04",
      "MAC Address Pt  4: --:-X:--:--:--:--",
      NULL,
      NULL,
      NULL,
      "network",
      MAC_ADDRESS_OPTIONS,
      "0"
   },
   {
      "ppsspp_change_mac_address05",
      "MAC Address Pt  5: --:--:X-:--:--:--",
      NULL,
      NULL,
      NULL,
      "network",
      MAC_ADDRESS_OPTIONS,
      "0"
   },
   {
      "ppsspp_change_mac_address06",
      "MAC Address Pt  6: --:--:-X:--:--:--",
      NULL,
      NULL,
      NULL,
      "network",
      MAC_ADDRESS_OPTIONS,
      "0"
   },
   {
      "ppsspp_change_mac_address07",
      "MAC Address Pt  7: --:--:--:X-:--:--",
      NULL,
      NULL,
      NULL,
      "network",
      MAC_ADDRESS_OPTIONS,
      "0"
   },
   {
      "ppsspp_change_mac_address08",
      "MAC Address Pt  8: --:--:--:-X:--:--",
      NULL,
      NULL,
      NULL,
      "network",
      MAC_ADDRESS_OPTIONS,
      "0"
   },
   {
      "ppsspp_change_mac_address09",
      "MAC Address Pt  9: --:--:--:--:X-:--",
      NULL,
      NULL,
      NULL,
      "network",
      MAC_ADDRESS_OPTIONS,
      "0"
   },
   {
      "ppsspp_change_mac_address10",
      "MAC Address Pt 10: --:--:--:--:-X:--",
      NULL,
      NULL,
      NULL,
      "network",
      MAC_ADDRESS_OPTIONS,
      "0"
   },
   {
      "ppsspp_change_mac_address11",
      "MAC Address Pt 11: --:--:--:--:--:X-",
      NULL,
      NULL,
      NULL,
      "network",
      MAC_ADDRESS_OPTIONS,
      "0"
   },
   {
      "ppsspp_change_mac_address12",
      "MAC Address Pt 12: --:--:--:--:--:-X",
      NULL,
      NULL,
      NULL,
      "network",
      MAC_ADDRESS_OPTIONS,
      "0"
   },
   {
      "ppsspp_wlan_channel",
      "WLAN Channel",
      NULL,
      NULL,
      NULL,
      "network",
      {
         { "Auto", NULL },
         { "1",    NULL },
         { "6",    NULL },
         { "11",   NULL },
         { NULL, NULL },
      },
      "Auto"
   },
   {
      "ppsspp_enable_builtin_pro_ad_hoc_server",
      "Enable Built-in PRO Ad Hoc Server",
      NULL,
      NULL,
      NULL,
      "network",
      BOOL_OPTIONS,
      "disabled"
   },
   {
      "ppsspp_change_pro_ad_hoc_server_address",
      "Change PRO Ad Hoc Server IP Address ('localhost' = multiple instances)",
      NULL,
      NULL,
      NULL,
      "network",
      {
         { "socom.cc",               NULL },
         { "psp.gameplayer.club",    NULL },
         { "myneighborsushicat.com", NULL },
         { "localhost",              NULL },
         { "IP address",             NULL },
         { NULL, NULL },
      },
      "socom.cc"
   },
   {
      "ppsspp_pro_ad_hoc_server_address01",
      "PRO Ad Hoc Server IP Address Pt  1: x--.---.---.---",
      NULL,
      NULL,
      NULL,
      "network",
      IP_ADDRESS_OPTIONS,
      "0"
   },
   {
      "ppsspp_pro_ad_hoc_server_address02",
      "PRO Ad Hoc Server IP Address Pt  2: -x-.---.---.---",
      NULL,
      NULL,
      NULL,
      "network",
      IP_ADDRESS_OPTIONS,
      "0"
   },
   {
      "ppsspp_pro_ad_hoc_server_address03",
      "PRO Ad Hoc Server IP Address Pt  3: --x.---.---.---",
      NULL,
      NULL,
      NULL,
      "network",
      IP_ADDRESS_OPTIONS,
      "0"
   },
   {
      "ppsspp_pro_ad_hoc_server_address04",
      "PRO Ad Hoc Server IP Address Pt  4: ---.x--.---.---",
      NULL,
      NULL,
      NULL,
      "network",
      IP_ADDRESS_OPTIONS,
      "0"
   },
   {
      "ppsspp_pro_ad_hoc_server_address05",
      "PRO Ad Hoc Server IP Address Pt  5: ---.-x-.---.---",
      NULL,
      NULL,
      NULL,
      "network",
      IP_ADDRESS_OPTIONS,
      "0"
   },
   {
      "ppsspp_pro_ad_hoc_server_address06",
      "PRO Ad Hoc Server IP Address Pt  6: ---.--x.---.---",
      NULL,
      NULL,
      NULL,
      "network",
      IP_ADDRESS_OPTIONS,
      "0"
   },
   {
      "ppsspp_pro_ad_hoc_server_address07",
      "PRO Ad Hoc Server IP Address Pt  7: ---.---.x--.---",
      NULL,
      NULL,
      NULL,
      "network",
      IP_ADDRESS_OPTIONS,
      "0"
   },
   {
      "ppsspp_pro_ad_hoc_server_address08",
      "PRO Ad Hoc Server IP Address Pt  8: ---.---.-x-.---",
      NULL,
      NULL,
      NULL,
      "network",
      IP_ADDRESS_OPTIONS,
      "0"
   },
   {
      "ppsspp_pro_ad_hoc_server_address09",
      "PRO Ad Hoc Server IP Address Pt  9: ---.---.--x.---",
      NULL,
      NULL,
      NULL,
      "network",
      IP_ADDRESS_OPTIONS,
      "0"
   },
   {
      "ppsspp_pro_ad_hoc_server_address10",
      "PRO Ad Hoc Server IP Address Pt 10: ---.---.---.x--",
      NULL,
      NULL,
      NULL,
      "network",
      IP_ADDRESS_OPTIONS,
      "0"
   },
   {
      "ppsspp_pro_ad_hoc_server_address11",
      "PRO Ad Hoc Server IP Address Pt 11: ---.---.---.-x-",
      NULL,
      NULL,
      NULL,
      "network",
      IP_ADDRESS_OPTIONS,
      "0"
   },
   {
      "ppsspp_pro_ad_hoc_server_address12",
      "PRO Ad Hoc Server IP Address Pt 12: ---.---.---.--x",
      NULL,
      NULL,
      NULL,
      "network",
      IP_ADDRESS_OPTIONS,
      "0"
   },
   {
      "ppsspp_enable_upnp",
      "Enable UPnP (Need a few seconds to detect)",
      NULL,
      NULL,
      NULL,
      "network",
      BOOL_OPTIONS,
      "disabled"
   },
   {
      "ppsspp_upnp_use_original_port",
      "UPnP Use Original Port ('ON' = PSP compatibility)",
      NULL,
      NULL,
      NULL,
      "network",
      BOOL_OPTIONS,
      "enabled"
   },
   {
      "ppsspp_port_offset",
      "Port Offset ('0' = PSP compatibility)",
      NULL,
      NULL,
      NULL,
      "network",
      {
         { "0",     NULL },
         { "1000",  NULL },
         { "2000",  NULL },
         { "3000",  NULL },
         { "4000",  NULL },
         { "5000",  NULL },
         { "6000",  NULL },
         { "7000",  NULL },
         { "8000",  NULL },
         { "9000",  NULL },
         { "10000", NULL },
         { "11000", NULL },
         { "12000", NULL },
         { "13000", NULL },
         { "14000", NULL },
         { "15000", NULL },
         { "16000", NULL },
         { "17000", NULL },
         { "18000", NULL },
         { "19000", NULL },
         { "20000", NULL },
         { "31000", NULL },
         { "32000", NULL },
         { "33000", NULL },
         { "34000", NULL },
         { "35000", NULL },
         { "36000", NULL },
         { "37000", NULL },
         { "38000", NULL },
         { "39000", NULL },
         { "40000", NULL },
         { "41000", NULL },
         { "42000", NULL },
         { "43000", NULL },
         { "44000", NULL },
         { "45000", NULL },
         { "46000", NULL },
         { "47000", NULL },
         { "48000", NULL },
         { "49000", NULL },
         { "50000", NULL },
         { "51000", NULL },
         { "52000", NULL },
         { "53000", NULL },
         { "54000", NULL },
         { "55000", NULL },
         { "56000", NULL },
         { "57000", NULL },
         { "58000", NULL },
         { "59000", NULL },
         { "60000", NULL },
         { "61000", NULL },
         { "62000", NULL },
         { "63000", NULL },
         { "64000", NULL },
         { "65000", NULL },
         { NULL, NULL },
      },
      "0"
   },
   {
      "ppsspp_minimum_timeout",
      "Minimum Timeout (Override in ms, '0' = default)",
      NULL,
      NULL,
      NULL,
      "network",
      {
         { "0",    NULL },
         { "100",  NULL },
         { "200",  NULL },
         { "300",  NULL },
         { "400",  NULL },
         { "500",  NULL },
         { "600",  NULL },
         { "700",  NULL },
         { "800",  NULL },
         { "900",  NULL },
         { "1000", NULL },
         { "1100", NULL },
         { "1200", NULL },
         { "1300", NULL },
         { "1400", NULL },
         { "1500", NULL },
         { "1600", NULL },
         { "1700", NULL },
         { "1800", NULL },
         { "1900", NULL },
         { "2000", NULL },
         { "3100", NULL },
         { "3200", NULL },
         { "3300", NULL },
         { "3400", NULL },
         { "3500", NULL },
         { "3600", NULL },
         { "3700", NULL },
         { "3800", NULL },
         { "3900", NULL },
         { "4000", NULL },
         { "4100", NULL },
         { "4200", NULL },
         { "4300", NULL },
         { "4400", NULL },
         { "4500", NULL },
         { "4600", NULL },
         { "4700", NULL },
         { "4800", NULL },
         { "4900", NULL },
         { "5000", NULL },
         { NULL, NULL },
      },
      "0"
   },
   {
      "ppsspp_forced_first_connect",
      "Forced First Connect (Faster connect)",
      NULL,
      NULL,
      NULL,
      "network",
      BOOL_OPTIONS,
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};

struct retro_core_options_v2 options_us = {
   option_cats_us,
   option_defs_us
};

/*
 ********************************
 * Language Mapping
 ********************************
*/

#ifndef HAVE_NO_LANGEXTRA
struct retro_core_options_v2 *options_intl[RETRO_LANGUAGE_LAST] = {
   &options_us, /* RETRO_LANGUAGE_ENGLISH */
   NULL,        /* RETRO_LANGUAGE_JAPANESE */
   NULL,        /* RETRO_LANGUAGE_FRENCH */
   NULL,        /* RETRO_LANGUAGE_SPANISH */
   NULL,        /* RETRO_LANGUAGE_GERMAN */
   NULL,        /* RETRO_LANGUAGE_ITALIAN */
   NULL,        /* RETRO_LANGUAGE_DUTCH */
   NULL,        /* RETRO_LANGUAGE_PORTUGUESE_BRAZIL */
   NULL,        /* RETRO_LANGUAGE_PORTUGUESE_PORTUGAL */
   NULL,        /* RETRO_LANGUAGE_RUSSIAN */
   NULL,        /* RETRO_LANGUAGE_KOREAN */
   NULL,        /* RETRO_LANGUAGE_CHINESE_TRADITIONAL */
   NULL,        /* RETRO_LANGUAGE_CHINESE_SIMPLIFIED */
   NULL,        /* RETRO_LANGUAGE_ESPERANTO */
   NULL,        /* RETRO_LANGUAGE_POLISH */
   NULL,        /* RETRO_LANGUAGE_VIETNAMESE */
   NULL,        /* RETRO_LANGUAGE_ARABIC */
   NULL,        /* RETRO_LANGUAGE_GREEK */
   NULL,        /* RETRO_LANGUAGE_TURKISH */
   NULL,        /* RETRO_LANGUAGE_SLOVAK */
   NULL,        /* RETRO_LANGUAGE_PERSIAN */
   NULL,        /* RETRO_LANGUAGE_HEBREW */
   NULL,        /* RETRO_LANGUAGE_ASTURIAN */
   NULL,        /* RETRO_LANGUAGE_FINNISH */
   NULL,        /* RETRO_LANGUAGE_INDONESIAN */
   NULL,        /* RETRO_LANGUAGE_SWEDISH */
   NULL,        /* RETRO_LANGUAGE_UKRAINIAN */
   NULL,        /* RETRO_LANGUAGE_CZECH */
};
#endif

/*
 ********************************
 * Functions
 ********************************
*/

/* Handles configuration/setting of core options.
 * Should be called as early as possible - ideally inside
 * retro_set_environment(), and no later than retro_load_game()
 * > We place the function body in the header to avoid the
 *   necessity of adding more .c files (i.e. want this to
 *   be as painless as possible for core devs)
 */

static INLINE void libretro_set_core_options(retro_environment_t environ_cb,
      bool *categories_supported)
{
   unsigned version  = 0;
#ifndef HAVE_NO_LANGEXTRA
   unsigned language = 0;
#endif

   if (!environ_cb || !categories_supported)
      return;

   *categories_supported = false;

   if (!environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version))
      version = 0;

   if (version >= 2)
   {
#ifndef HAVE_NO_LANGEXTRA
      struct retro_core_options_v2_intl core_options_intl;

      core_options_intl.us    = &options_us;
      core_options_intl.local = NULL;

      if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &language) &&
          (language < RETRO_LANGUAGE_LAST) && (language != RETRO_LANGUAGE_ENGLISH))
         core_options_intl.local = options_intl[language];

      *categories_supported = environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL,
            &core_options_intl);
#else
      *categories_supported = environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2,
            &options_us);
#endif
   }
   else
   {
      size_t i, j;
      size_t option_index              = 0;
      size_t num_options               = 0;
      struct retro_core_option_definition
            *option_v1_defs_us         = NULL;
#ifndef HAVE_NO_LANGEXTRA
      size_t num_options_intl          = 0;
      struct retro_core_option_v2_definition
            *option_defs_intl          = NULL;
      struct retro_core_option_definition
            *option_v1_defs_intl       = NULL;
      struct retro_core_options_intl
            core_options_v1_intl;
#endif
      struct retro_variable *variables = NULL;
      char **values_buf                = NULL;

      /* Determine total number of options */
      while (true)
      {
         if (option_defs_us[num_options].key)
            num_options++;
         else
            break;
      }

      if (version >= 1)
      {
         /* Allocate US array */
         option_v1_defs_us = (struct retro_core_option_definition *)
               calloc(num_options + 1, sizeof(struct retro_core_option_definition));

         /* Copy parameters from option_defs_us array */
         for (i = 0; i < num_options; i++)
         {
            struct retro_core_option_v2_definition *option_def_us = &option_defs_us[i];
            struct retro_core_option_value *option_values         = option_def_us->values;
            struct retro_core_option_definition *option_v1_def_us = &option_v1_defs_us[i];
            struct retro_core_option_value *option_v1_values      = option_v1_def_us->values;

            option_v1_def_us->key           = option_def_us->key;
            option_v1_def_us->desc          = option_def_us->desc;
            option_v1_def_us->info          = option_def_us->info;
            option_v1_def_us->default_value = option_def_us->default_value;

            /* Values must be copied individually... */
            while (option_values->value)
            {
               option_v1_values->value = option_values->value;
               option_v1_values->label = option_values->label;

               option_values++;
               option_v1_values++;
            }
         }

#ifndef HAVE_NO_LANGEXTRA
         if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &language) &&
             (language < RETRO_LANGUAGE_LAST) && (language != RETRO_LANGUAGE_ENGLISH) &&
             options_intl[language])
            option_defs_intl = options_intl[language]->definitions;

         if (option_defs_intl)
         {
            /* Determine number of intl options */
            while (true)
            {
               if (option_defs_intl[num_options_intl].key)
                  num_options_intl++;
               else
                  break;
            }

            /* Allocate intl array */
            option_v1_defs_intl = (struct retro_core_option_definition *)
                  calloc(num_options_intl + 1, sizeof(struct retro_core_option_definition));

            /* Copy parameters from option_defs_intl array */
            for (i = 0; i < num_options_intl; i++)
            {
               struct retro_core_option_v2_definition *option_def_intl = &option_defs_intl[i];
               struct retro_core_option_value *option_values           = option_def_intl->values;
               struct retro_core_option_definition *option_v1_def_intl = &option_v1_defs_intl[i];
               struct retro_core_option_value *option_v1_values        = option_v1_def_intl->values;

               option_v1_def_intl->key           = option_def_intl->key;
               option_v1_def_intl->desc          = option_def_intl->desc;
               option_v1_def_intl->info          = option_def_intl->info;
               option_v1_def_intl->default_value = option_def_intl->default_value;

               /* Values must be copied individually... */
               while (option_values->value)
               {
                  option_v1_values->value = option_values->value;
                  option_v1_values->label = option_values->label;

                  option_values++;
                  option_v1_values++;
               }
            }
         }

         core_options_v1_intl.us    = option_v1_defs_us;
         core_options_v1_intl.local = option_v1_defs_intl;

         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL, &core_options_v1_intl);
#else
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, option_v1_defs_us);
#endif
      }
      else
      {
         /* Allocate arrays */
         variables  = (struct retro_variable *)calloc(num_options + 1,
               sizeof(struct retro_variable));
         values_buf = (char **)calloc(num_options, sizeof(char *));

         if (!variables || !values_buf)
            goto error;

         /* Copy parameters from option_defs_us array */
         for (i = 0; i < num_options; i++)
         {
            const char *key                        = option_defs_us[i].key;
            const char *desc                       = option_defs_us[i].desc;
            const char *default_value              = option_defs_us[i].default_value;
            struct retro_core_option_value *values = option_defs_us[i].values;
            size_t buf_len                         = 3;
            size_t default_index                   = 0;

            values_buf[i] = NULL;

            if (desc)
            {
               size_t num_values = 0;

               /* Determine number of values */
               while (true)
               {
                  if (values[num_values].value)
                  {
                     /* Check if this is the default value */
                     if (default_value)
                        if (strcmp(values[num_values].value, default_value) == 0)
                           default_index = num_values;

                     buf_len += strlen(values[num_values].value);
                     num_values++;
                  }
                  else
                     break;
               }

               /* Build values string */
               if (num_values > 0)
               {
                  buf_len += num_values - 1;
                  buf_len += strlen(desc);

                  values_buf[i] = (char *)calloc(buf_len, sizeof(char));
                  if (!values_buf[i])
                     goto error;

                  strcpy(values_buf[i], desc);
                  strcat(values_buf[i], "; ");

                  /* Default value goes first */
                  strcat(values_buf[i], values[default_index].value);

                  /* Add remaining values */
                  for (j = 0; j < num_values; j++)
                  {
                     if (j != default_index)
                     {
                        strcat(values_buf[i], "|");
                        strcat(values_buf[i], values[j].value);
                     }
                  }
               }
            }

            variables[option_index].key   = key;
            variables[option_index].value = values_buf[i];
            option_index++;
         }

         /* Set variables */
         environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
      }

error:
      /* Clean up */

      if (option_v1_defs_us)
      {
         free(option_v1_defs_us);
         option_v1_defs_us = NULL;
      }

#ifndef HAVE_NO_LANGEXTRA
      if (option_v1_defs_intl)
      {
         free(option_v1_defs_intl);
         option_v1_defs_intl = NULL;
      }
#endif

      if (values_buf)
      {
         for (i = 0; i < num_options; i++)
         {
            if (values_buf[i])
            {
               free(values_buf[i]);
               values_buf[i] = NULL;
            }
         }

         free(values_buf);
         values_buf = NULL;
      }

      if (variables)
      {
         free(variables);
         variables = NULL;
      }
   }
}

#ifdef __cplusplus
}
#endif

#endif
