# OpenXR on Vulkan (design and status)

VR used to be OpenGL ES only: `Common/VR` created the OpenXR session with an EGL binding, made one
GL swapchain per eye, and `GLRenderManager` replayed the whole frame once per eye, swapping
`u_view` per pass. This moved it to Vulkan, where both eyes are drawn in one pass with multiview (the
existing "stereo rendering" path, `GPU_USE_SINGLE_PASS_STEREO`), and then to PC VR, where the desktop
window stays the primary swapchain and the headset is a second output.

VR is Vulkan only now. OpenGL VR was removed once Vulkan VR worked, in the libretro core too, which
had its own GL path on the per-eye replay. In the VR build OpenGL isn't offered as a backend
(`Config::IsBackendEnabled`), so a config that still says OpenGL gets Vulkan.

## Phase 1: Quest (standalone Android) on Vulkan

### Instance, device, session

- `XR_KHR_vulkan_enable` (version 1: we create the `VkInstance`/`VkDevice` ourselves, which keeps
  `VulkanContext` in charge). `VR_Init` enables it when the runtime has it. A runtime without it
  gets no VR (OpenGL used to be the fallback).
- Before `vkCreateInstance`/`vkCreateDevice`, `VulkanContext` asks a hook for extra extensions
  (`xrGetVulkanInstanceExtensionsKHR`, `xrGetVulkanDeviceExtensionsKHR`), and the physical device
  is the one `xrGetVulkanGraphicsDeviceKHR` names.
- The session is created with `XrGraphicsBindingVulkanKHR` once the device exists (in
  `VulkanGraphicsContext::InitSurface`), with our graphics queue family and index.

### Presenting: the XR swapchain is a `VulkanPresentation`

There's no Android window to present to in VR, so the main swapchain is replaced the same way
libretro and the offscreen mode replace it (`VulkanPresentation`):

- One XR swapchain, `arraySize = 2`. We write sRGB-encoded values and want them shown as they are.
  On the Quest it's plain `R8G8B8A8_UNORM`, which its compositor shows as it is. The spec's way,
  `R8G8B8A8_SRGB` with `XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT` drawn through a UNORM view, makes the
  Quest compositor show "snow" along edges (worst when it reprojects a late frame), with or without
  the view formats listed through `XR_KHR_vulkan_swapchain_format_list`, and with clean
  synchronization validation on our side.
- **The backbuffer pass is multiview** (view mask 0b11), always, in VR. The backbuffer views are
  2D arrays, the depth buffer has two layers. Everything that draws to the backbuffer (UI,
  presentation) is the same in both views unless its shader reads `gl_ViewIndex`. That costs a
  second layer of fill for the menus, which is small next to the game, and means the backbuffer
  render pass and all pipelines compiled against it never change with the VR mode.
- Presenting the game samples layer `gl_ViewIndex` of the game framebuffer when that has two
  layers (stereo on), else layer 0.
- Compositor layers, as before: in a flat mode (menus, flat games) a cylinder layer showing layer 0
  (mono) or one cylinder per eye on layers 0 and 1 (stereo); in a 6DoF mode, a projection layer with
  one view per layer.

### Frame loop and threads

OpenXR wants `xrWaitFrame` → `xrBeginFrame` → acquire/wait/release → `xrEndFrame` per frame, with
`xrBeginFrame` of the next frame after `xrEndFrame` of this one. With Vulkan, NativeFrame runs on
the emu thread and the render manager's thread submits and presents:

- **Emu thread,** before NativeFrame (`StartVRRender`): wait for the previous frame's
  `xrEndFrame`, then `xrWaitFrame`, `xrBeginFrame`, locate the views, sync input. The view poses
  are then used for this frame's uniforms. While the session isn't running, skip the frame.
- **Render thread:** acquiring the backbuffer is `xrAcquireSwapchainImage` +
  `xrWaitSwapchainImage`. Presenting releases it, then `xrEndFrame` with the layers. A frame that
  never drew to the backbuffer ends with no layers, so the emu thread is never left waiting.
- The runtime may use the `VkQueue` inside `xrBeginFrame`, `xrEndFrame` and the swapchain calls, so
  they and our submits take the presentation's queue lock.

This serializes the XR frames (the emu thread can't begin frame N+1 until frame N is submitted).
Pipelining `xrWaitFrame` is a later optimization.

### Shaders

`GPU_USE_VIRTUAL_REALITY` on Vulkan. The base uniform buffer gets the VR fields at the end:
`u_proj_lens`, the right eye's view matrix and the HUD scale. The vertex shader picks the view
matrix by `gl_ViewIndex` when multiview is on, instead of the simple stereo's fixed IPD offset. The
HUD heuristics (`GuessVRDrawingHUD`, `Is2DVRObject`, `UpdateVRProjection`, `UpdateVRView`) are in
shared code.

In VR the view and lens matrices are recomputed and the base buffer re-uploaded on every draw. They
depend on whether the draw is 2D and on the head pose, neither of which sets a dirty flag.
Recomputing only on dirty split GTA's scene into two layers moving at different speeds.

### Compatibility

- **Skyplane** (`compatvr.ini`, the GTA Stories games): `PreprocessSkyplaneVulkan` edits the render
  steps in `PreprocessSteps`, before passes are merged. A pass that samples another framebuffer (a
  dependency on a framebuffer other than the target) is left alone. The frame's first other pass is
  cleared to the fog color, by turning its load op into a clear unless it clears already. Indexed
  draws with the depth test off (as the bound pipeline says) are dropped. The once-per-frame clear
  is reset in `StartVRRender`.
- **The pointer** is drawn by the UI (`GetVRMouseCursorRect`).
- **Alpha:** the layers are opaque.
- **`VRForce`** (developer setting, VR shaders without a headset) works on Vulkan.

The other VR compat flags (`ForceMono`, `ForceFlatScreen`, `IdentityViewHack`, `MirroringVariant`,
`ProjectionHack`, `UnitsPerMeter`) are read in shared code (`PPSSPPVR.cpp`, the shader generator),
so they don't depend on the backend.

### Fixes found on the headset

- **Depth** was inverted: the shader replaced the lens projection's Z with the game's
  (`orgPos`) at the very end, skipping the viewport's Z scale, which broke when the viewport transform
  moved into the shader. It's replaced before the transform now (libretro already did this).
- **The flat screen** stays upright. It used to take the head's pitch at recenter time, often looking
  down while putting the headset on. Recentering is the system gesture (hold the Meta button).
- **Restarting** (recreating the activity): the native shutdown takes OpenXR down (renderer, input,
  session, instance, `ShutdownVR`), and the recreated activity sets it up again in the same process.
  It used to `System.exit` in `onDestroy` instead, which killed the restart. Controller input is
  created per session, since its spaces and attached action set belong to one.

## Libretro

RetroArch owns OpenXR and supplies poses through `RETRO_ENVIRONMENT_GET_VR_FRAME_STATE`.
The core publishes two side-by-side eye rectangles through `RETRO_ENVIRONMENT_SET_VIDEO_VIEWS`,
with Off, Stereoscopic and Immersive options (`libretro/LibretroVR.cpp`). It needs Vulkan with
multiview; another backend, an unsupported device or frontend keeps flat output.

The Vulkan game framebuffers use the shared VR shaders and two-layer multiview rendering.
`PresentationCommon::CopyToLibretroVR` samples each layer separately into the two halves of the
ordinary libretro presentation image. Mono sources (including uploaded video frames) feed both eyes.
Stereoscopic draws a screen; Immersive draws the shared tracked cylinder when the scene is 3D.
The cylinder's camera matrix (`GetVRCanvasMatrix`) is in GL clip coordinates and converted to Vulkan's.
Post shaders are bypassed during VR because their intermediate images are mono.

The core does not create an OpenXR instance, session or swapchain. It continues to submit through
`LibretroVulkanPresentation`, with RetroArch's queue locking and image lifetime rules.
`SetRenderTarget` requests the current output extent before the frame begins, so switching VR
on or off recreates the presentation images before drawing. This does not change savestate data
or shader cache keys. IdentityViewHack games retain the existing stereo-screen fallback.

Cached frontend context resets also recreate the backbuffers. The Vulkan context releases the old
backbuffers in `GotBackbuffer`, after `GPU_Vulkan::DeviceLost` (which restarts its workers), to avoid
starting a second submission/compiler thread during a frontend reset or resolution change.

Validated on Quest 3 with RetroArch 1.22.2 (`0a463db578`): GTA Liberty City Stories and Sega Rally
Revo, Off/Stereoscopic/Immersive transitions, two-layer tracked-cylinder output, and live 2x/3x
resolution changes. Android arm64 and Linux libretro builds pass, as do the C++ unit tests and
`python3 test.py -g --graphics=software` (including relay and peer-to-peer tests).
The frontend's mirror screenshots verify screen output; they do not establish headset comfort
or eye alignment. Vulkan pipeline-cache saving still reports the existing unsupported `ab` mode
in libretro's file access path.

## Phase 2: PC VR

The desktop window (Windows, then Linux) stays the primary swapchain and shows a mirror. The
headset is an extra output through the multi-swapchain API (`docs/multi-swapchain.md`): its pass
renders into the XR swapchain images, acquired with `xrAcquireSwapchainImage` instead of
`vkAcquireNextImageKHR`. The runtime paces the frame (`xrWaitFrame`), so the desktop swapchain
becomes the non-pacing one. The OpenXR loader on desktop is our dynamic one (`OpenXRLoader.cpp`).

## Status

- [x] Phase 1: runs on a Quest 2 (menus, Burnout Legends, GTA LCS in flat mode, stereo and 6DoF on)
  at 72 fps. Skyplane not checked specifically yet.
- [x] OpenGL VR removed, also libretro's.
- [ ] Phase 2
