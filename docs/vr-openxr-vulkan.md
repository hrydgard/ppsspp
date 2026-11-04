# OpenXR on Vulkan (design and status)

VR used to be OpenGL ES only: `Common/VR` creates the OpenXR session with an EGL binding, makes one
GL swapchain per eye, and `GLRenderManager` replays the whole frame once per eye, swapping
`u_view` per pass (`UNIFORMSTEREOMATRIX`). This moves it to Vulkan, where both eyes are drawn in one
pass with multiview (the existing "stereo rendering" path, `GPU_USE_SINGLE_PASS_STEREO`), and then to
PC VR, where the desktop window stays the primary swapchain and the headset is a second output.

## Phase 1: Quest (standalone Android) on Vulkan

### Instance, device, session

- `XR_KHR_vulkan_enable` (version 1: we create the `VkInstance`/`VkDevice` ourselves, which keeps
  `VulkanContext` in charge). `VR_Init` enables it next to the GLES binding when the runtime has
  it, so the backend can be chosen later.
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

This serializes the XR frames (the emu thread can't begin frame N+1 until frame N is submitted),
which is what the OpenGL path did too. Pipelining `xrWaitFrame` is a later optimization.

### Shaders

`GPU_USE_VIRTUAL_REALITY` on Vulkan. The base uniform buffer gets the VR fields at the end:
`u_proj_lens`, the right eye's view matrix and the HUD scale. The vertex shader picks the view
matrix by `gl_ViewIndex` when multiview is on, instead of the simple stereo's fixed IPD offset. The
HUD heuristics (`GuessVRDrawingHUD`, `Is2DVRObject`, `UpdateVRProjection`, `UpdateVRView`) move out
of `ShaderManagerGLES` so both backends use them.

In VR the view and lens matrices are recomputed and the base buffer re-uploaded on every draw. They
depend on whether the draw is 2D and on the head pose, neither of which sets a dirty flag. GL gets away
with dirty flags because its uniforms are per program (2D and 3D draws rarely share one). Recomputing
only on dirty split GTA's scene into two layers moving at different speeds.

### The OpenGL-only parts, and their Vulkan versions

OpenGL VR stays an option for a while. What it does its own way, and the Vulkan equivalent:

- **Skyplane** (`compatvr.ini`, the GTA Stories games): `PreprocessSkyplane` edits GL render steps at
  replay, `PreprocessSkyplaneVulkan` does the same to Vulkan steps in `PreprocessSteps`, before
  passes are merged. A pass that samples another framebuffer is left alone (GL: a
  `BIND_FB_TEXTURE` command; Vulkan: a dependency on a framebuffer other than the target). The
  frame's first other pass is cleared to the fog color (GL inserts a `CLEAR`; Vulkan turns the
  load op into a clear, unless it clears already). Indexed draws with the depth test off are
  dropped (GL tracks `DEPTH` commands; Vulkan reads the bound pipeline's depth state). GL resets
  the once-per-frame clear per eye, Vulkan once per frame in `StartVRRender`.
- **Per-eye view matrices:** GL replays the frame per eye and `UNIFORMSTEREOMATRIX` picks the
  eye's `u_view`. Vulkan has `u_viewR` in the VR block and picks by `gl_ViewIndex`.
- **The pointer:** a scissored GL clear after the frame. On Vulkan the UI draws it
  (`GetVRMouseCursorRect`).
- **Alpha:** GL clears the eye buffers' alpha to 1 after the frame. Vulkan submits opaque layers instead.
- **`VRForce`** (developer setting, VR shaders without a headset) works on both.

The other VR compat flags (`ForceMono`, `ForceFlatScreen`, `IdentityViewHack`, `MirroringVariant`,
`ProjectionHack`, `UnitsPerMeter`) are read in shared code (`PPSSPPVR.cpp`, the shader generator),
so they don't depend on the backend.

### Fixes found on the headset

- **Depth** was inverted, on GL too: the shader replaced the lens projection's Z with the game's
  (`orgPos`) at the very end, skipping the viewport's Z scale, which broke when the viewport transform
  moved into the shader. It's replaced before the transform now (libretro already did this).
- **Gamma on GL:** the GL eye swapchains are `GL_SRGB8_ALPHA8`, and GLES encodes writes to an sRGB
  framebuffer, so our already sRGB-encoded colors came out too bright. Writes skip the encoding now
  (`GL_EXT_sRGB_write_control`).
- **The flat screen** stays upright. It used to take the head's pitch at recenter time, often looking
  down while putting the headset on. Recentering is the system gesture (hold the Meta button).
- **Restarting** (changing the backend): the native shutdown takes OpenXR down (renderer, input,
  session, instance, `ShutdownVR`), and the recreated activity sets it up again in the same process.
  It used to `System.exit` in `onDestroy` instead, which killed the restart. Controller input is
  created per session, since its spaces and attached action set belong to one.

## Phase 2: PC VR

The desktop window (Windows, then Linux) stays the primary swapchain and shows a mirror. The
headset is an extra output through the multi-swapchain API (`docs/multi-swapchain.md`): its pass
renders into the XR swapchain images, acquired with `xrAcquireSwapchainImage` instead of
`vkAcquireNextImageKHR`. The runtime paces the frame (`xrWaitFrame`), so the desktop swapchain
becomes the non-pacing one. The OpenXR loader on desktop is our dynamic one (`OpenXRLoader.cpp`).

## Status

- [x] Phase 1: runs on a Quest 2 (menus, Burnout Legends, GTA LCS in flat mode, stereo and 6DoF on)
  at 72 fps, backend switching both ways. Skyplane not checked specifically yet.
- [ ] Phase 2
