# Multiple swapchains (design)

Status: design, nothing implemented yet.

The first user is an iOS "TV mode": with AirPlay connected, the game runs full screen on the TV and the
phone shows only the touch controls (and the menus). The same mechanism is meant to carry later uses:
ImGui debugger windows outside the main window (ImGui viewports), a desktop window mirroring a VR
headset, and an external display on Android or desktop. So the graphics side is general, and the iOS
part is just its first client.

## Where we are

Everything assumes one output, at every layer:

- **thin3d:** the backbuffer is the `nullptr` framebuffer. `BindFramebufferAsRenderTarget(nullptr)`,
  `GetFramebufferDimensions(nullptr)`, `CopyFramebufferToMemory(nullptr)`, `Present()`, the
  `LOST_BACKBUFFER`/`GOT_BACKBUFFER` events and the present-mode caps all mean "the" swapchain
  (`Common/GPU/thin3d.h`).
- **Vulkan:** `VulkanContext` holds one `surface_`/`swapchain_`/`presentation_` and the surface-derived
  state (format, extent, caps, transform, present modes). The render manager has one
  `FrameDataShared::swapchainImages_`, one set of backbuffer framebuffers and one depth buffer
  (`VulkanQueueRunner.h`). A frame can contain only one backbuffer pass (`SanityCheckPassesOnAdd`
  asserts). The BACKBUFFER render pass is looked up without a format in its key
  (`VKRRenderPass::Get`). Queue selection checks present support against the one surface
  (`ChooseQueue`).
- **`VulkanPresentation`** (`Common/GPU/Vulkan/VulkanPresentation.h`) already abstracts an image chain
  (acquire, present, images, extent, format, final layout). libretro and headless use it to *replace*
  the swapchain. It is the seed of this design.
- **Metal:** one `layer_`, `rtDrawable_` and `bbDepth_` in `MetalRenderManager`. Backbuffer passes are
  merged against a single `BACKBUFFER_TARGET` sentinel.
- **D3D11:** the platform context (`D3D11Context`) owns the one `IDXGISwapChain` and hands its RTV in
  through `GOT_BACKBUFFER`.
- **OpenGL:** one context, one default FBO (`g_defaultFBO`), one `swapFunction_`.
- **Frontend:** one global `g_display`, read in about 250 places, from layout to input scaling.
  `ScreenManager::Render` is the only place that binds the backbuffer. `EmuScreen::render` draws the
  game (`gpu->CopyDisplayToOutput`) and then the touch controls (`renderUI`) into that same pass.

## Graphics: `Draw::Swapchain`

A swapchain is a presentable chain of images: a window, a `CAMetalLayer`, an XR swapchain, a libretro
frame callback. It's a real object, a kind of `Framebuffer`, so the existing framebuffer calls take it
and the backbuffer stops being a special `nullptr`. The current implicit backbuffer becomes the
**primary swapchain**, which the draw context creates along with itself.

```cpp
namespace Draw {

// A Framebuffer you can render to and read back, but not texture from or blit from. Its size
// follows the surface. Owned by the DrawContext.
class Swapchain : public Framebuffer {
	// IsSwapchain() on Framebuffer returns true. The backend's subclass holds the presentation state.
};

struct SwapchainDesc {
	WindowSystem winsys;
	void *data1;     // The same as GraphicsContext::InitSurface: CAMetalLayer, HWND, ANativeWindow...
	void *data2;
	int width;       // In pixels. Used when the window system can't report it (iOS, Metal).
	int height;
	bool pacing;     // See "Frame pacing". Exactly one swapchain paces; the primary by default.
	const char *tag;
};

class DrawContext {
	// ...existing interface...

	// Null if the backend can't (caps.multipleSwapchains false), or the surface can't be presented
	// from our queue. Call between frames, on the render thread.
	virtual Swapchain *CreateSwapchain(const SwapchainDesc &desc, std::string *error) { return nullptr; }
	virtual void DestroySwapchain(Swapchain *swapchain) {}
	virtual void ResizeSwapchain(Swapchain *swapchain, int width, int height) {}
	virtual void SetPacingSwapchain(Swapchain *swapchain) {}
	Swapchain *GetPrimarySwapchain();

	// Unchanged signatures, now also taking a Swapchain:
	//   BindFramebufferAsRenderTarget(swapchain, rp, tag): at most one pass per swapchain per frame,
	//     after all offscreen passes.
	//   GetFramebufferDimensions(swapchain, &w, &h), CopyFramebufferToMemory(swapchain, ...).
	// BindFramebufferAsTexture, BlitFramebuffer and CopyFramebufferImage assert on a swapchain.
};

}
```

- **`nullptr` is an alias for the primary,** so nothing breaks. The direct callers use
  `GetPrimarySwapchain()`. Some code still passes a `Framebuffer *` that may be null (non-buffered
  rendering, for one), so `nullptr` can only become an assert after that's audited. Backends that
  support one swapchain map the primary to their old `nullptr` path (`PrimaryToNull`). Metal treats
  all its swapchains the same way.
- **Size changes on resize.** Unlike other framebuffers, a swapchain's `Width()`/`Height()` change when
  its surface does, and only at a frame boundary.
- **Platform code keeps the primary out of its hands.** `GraphicsContext::InitSurface` still creates
  the primary, so platform code doesn't change.

- **One frame, many presents.** `BeginFrame`/`EndFrame` and the frame semaphores and fences stay
  global. `Present()` presents every swapchain that got a pass this frame. A swapchain without a pass
  is not acquired or presented, so a debugger window can update at 10 Hz for free.
- **Lifetime:** `CreateSwapchain` and `DestroySwapchain` run on the render thread at a frame boundary.
  Destroy waits for the frames in flight that use it. The platform keeps the native surface (layer,
  window) alive until destroy returns (see "iOS").
- **Out-of-date and resize are per swapchain.** Recreating the TV's swapchain must not touch the
  primary's. Losing a secondary surface makes it skip frames until resized or destroyed, never fail
  the frame.
- **Formats:** phase 1 requires every swapchain to use the primary's color format, which is BGRA8 on
  every platform we care about. Then the BACKBUFFER render pass and every pipeline compiled against it
  stay compatible. Mixed formats would need the format in the render-pass key and per-format pipeline
  variants; that's left for when something needs it (HDR output).
- **Caps:** `DeviceCaps::multipleSwapchains`. The frontend checks it and falls back to mirroring.

### Frame pacing

FIFO swapchains block on acquire. With two of them, the frame loop paces to whichever is slower, and
an unlucky vsync phase between two displays makes that stutter. Policy:

- **Exactly one swapchain paces.** Its acquire blocks as today, and the frame-timing machinery
  (present IDs, `FrameTimeHistory`, `GetCurrentPresentMode`) follows it. In TV mode that's the TV,
  since the game is there.
- **The others never block.** They acquire with a timeout of 0. If no image is ready, that swapchain
  skips the frame (its pass becomes a no-op), and it's presented on the next one. They use MAILBOX or
  IMMEDIATE where the surface offers it.
- **On iOS** a `CAMetalLayer` is always vsynced (`displaySyncEnabled` is macOS-only), so a
  non-pacing layer just drops frames when it has no drawable. That's acceptable for touch controls.
  This needs measuring with a 60 Hz TV and a 120 Hz ProMotion phone.

### Vulkan

1. **Split `VulkanContext`'s surface state into `VulkanSurfaceSwapchain`:** surface, swapchain,
   format, extent, caps, pre-transform, present modes, `cbGetDrawSize`. Move `InitSurface`,
   `ReinitSurface`, `InitSwapchain`, `DestroySwapchain` and `DestroySurface` onto it. `VulkanContext`
   keeps a primary one, and its current accessors forward to it, so callers don't change.
2. **Make the real swapchain a `VulkanPresentation` too.** Today it's the `presentation_ == nullptr`
   branch in each consumer. With a real-swapchain implementation, every output is a presentation:
   KHR swapchain, libretro, offscreen, later OpenXR. The render manager then deals with one interface,
   and the branches go away.
3. **Per-output state in the render manager.** `FrameDataShared::swapchainImages_` and the queue
   runner's `backbuffer_`, `framebuffers_` and `depth_` move into a `VKROutput` (presentation, images,
   views, semaphores, framebuffers, depth, out-of-date counter). The per-frame `acquireSemaphore`,
   `curSwapchainImage`, `hasAcquired` and `skipSwap` become a small array indexed by output (cap of
   4). Output 0 is the primary.
4. **Steps.** A RENDER step with no framebuffer gets an output index. `SanityCheckPassesOnAdd` changes
   from "one backbuffer pass" to "one pass per output, after all offscreen passes". `RunSteps`
   already acquires lazily on the first backbuffer step, and that becomes per output.
5. **Submit and present.** The present command buffer waits on the acquire semaphore of every output
   acquired this frame (stage COLOR_ATTACHMENT_OUTPUT) and signals each one's rendering-complete
   semaphore. Real swapchains present in a single `vkQueuePresentKHR` with `swapchainCount = N`, and
   read the per-swapchain results from `pResults`. Other presentations get their own `QueuePresent`.
6. **Queue support.** `CreateSwapchain` checks `vkGetPhysicalDeviceSurfaceSupportKHR` for the
   existing graphics queue family and fails if it's missing. That never happens on Apple, Windows or
   desktop Linux. On Android it's always the same queue.
7. **Rotation.** The pre-transform is per surface, and so is the rect rotation that reads
   `g_display.rotation` (`VulkanRenderManager.cpp`, `RotateRectToDisplay` in the queue runner). The
   rotation goes into `VKROutput` and gets applied per pass.

The single-output path should come out doing exactly what it does now: same waits, same single
present. Most of the regression risk is in step 5, which every platform runs.

### Metal

`layer_`, `rtDrawable_`, `bbDepth_` and the pending size move into a `MetalSwapchain`. The
`BACKBUFFER_TARGET` sentinel becomes the swapchain pointer, so passes merge per target. Replay acquires
each drawable lazily, as today, and the frame's last command buffer does `presentDrawable:` for each.
For a non-pacing layer, skip the pass when `nextDrawable` would block.

### D3D11 and OpenGL

- **D3D11:** easy, and later. Creating the `IDXGISwapChain` moves from `D3D11Context` into the draw
  context, one per HWND, and `Present()` presents each one.
- **OpenGL:** `multipleSwapchains = false` for now. Desktop GL could use one context with
  `MakeCurrent(window_i)` before each window's pass and swap, which needs a per-surface default FBO
  and swap function, and a surface ID in the PRESENT task. Nothing on the iOS path uses GL.

## Frontend: outputs and roles

`g_display` stays as it is and describes the **primary output**: the screen the UI and touch input
live on (the phone). Its 250 readers don't change. Making it per-output would touch layout, input,
DPI and fonts everywhere, for no gain here.

Secondary outputs are described separately:

```cpp
enum class OutputRole {
	Game,     // Just the emulated display, laid out full screen. No UI.
	Mirror,   // A copy of the primary (unused at first, the OS mirrors for us).
	ImGui,    // Later: an ImGui viewport.
};

struct OutputDisplay {
	Draw::Swapchain *swapchain;
	OutputRole role;
	int pixelWidth, pixelHeight;
	float refreshHz;
	float overscanInset;  // TVs.
};
```

These live in a small `OutputManager` owned by the screen manager. The platform layer adds and
removes them (see "iOS").

### Rendering

There is still one screen stack. A screen can draw to more than one output:

```cpp
// Draws this screen's content for a secondary output. The swapchain is already bound.
// Returns false if the screen has nothing for it.
virtual bool renderOutput(const OutputDisplay &output) { return false; }
```

`ScreenManager::Render` stays the only place that binds a swapchain:

1. `PreRender` for every screen, as now. Emulation and post-shader passes run here.
2. For each secondary output, bind its swapchain and ask the stack from the top down. The first
   screen whose `renderOutput` returns true has drawn it. If no screen claims it, the output falls
   back to clearing it (or, on iOS, it's taken away so the system mirrors again).
   - `EmuScreen` claims `Game` outputs. It draws `CopyDisplayToOutput` with that output's
     `OutputParams` and nothing else.
   - The pause menu and the other dialogs don't claim anything. So while the pause menu is up, the
     phone shows the menu and the TV keeps the game (darkened, as behind the menu today).
   - Later, other screens can claim a TV too. The game browser could show a big-screen backdrop
     instead of a mirror of the menu.
3. The primary pass, as now. While `EmuScreen` sends the game to another output, its `render` leaves
   out its own `CopyDisplayToOutput` and draws the touch controls over black. Optionally it draws a
   small game preview, which is a second `CopyToOutput`.

The other option was a stack entry holding one screen per swapchain. It doesn't fit, because input,
focus and dialogs belong to one place (the phone). Per-output screens would have to be pushed and
popped in step, and most entries would have only one.

ImGui windows aren't screens. ImGui owns its viewports. After the screens, the ImGui step draws each
viewport to its own swapchain, so it shares the swapchain layer with the screen stack and nothing else.

### Presentation parameters

`PresentationCommon` should get its output size and layout explicitly instead of from `g_display`.
Mostly it already does, through `PSP_CoreParameter().pixelWidth/Height` and
`UpdateDisplaySize`, and "auto" render resolution follows the same values, so it scales for the TV
(1080p) on its own. What still reads the global:

- `GetScreenFrame` (insets, DPI)
- the stretch logic in `CalculateDisplayOutputRect` (device orientation)
- `rot_matrix` in `CopyToOutput` and the post-shader passes
- the DisplayLayoutConfig choice (`GetDeviceOrientation()`)

These become an `OutputParams { width, height, rotation, insets, orientation }` handed to
`CopyDisplayToOutput`. The game output on a TV is always landscape, with no rotation and with overscan
as its insets. That refactor is contained (about five call sites) and is worth doing first, since it
can be tested on its own.

### Restrictions

- **Skip buffer effects** (non-buffered rendering) draws the game straight into the bound
  backbuffer during emulation, so it can't target another output. In that mode the game stays on the
  phone, with a one-time notice.
- **Screenshots** taken from the backbuffer (the primary swapchain) would capture the
  phone. Game screenshots should read the game output, or render the game to an offscreen buffer.
- **Cardboard VR** turns off on a `Game` output.

## iOS

- **Getting the scene:** up to iOS 26, a `UIWindowSceneSessionRoleExternalDisplayNonInteractive`
  configuration in Info.plist, with `ExternalDisplaySceneDelegate`. From iOS 27, that's ignored, and the
  app has to register from a view controller instead (`registerSceneAccessory:` with
  `UISceneAccessory externalNonInteractiveSceneAccessoryWithConfiguration:`). We do both. Without a
  scene, iOS just mirrors. An AirPlay Apple TV 4K comes in at 3840x2160.
- **Connect:** the delegate keeps the scene, and posts "external display available" (size from
  `screen.nativeBounds`, refresh rate, overscan) to the native side. It doesn't create a window yet.
- **TV mode on:** when it should be active (setting on, `EmuScreen` running), the main thread creates
  a `UIWindow` on the scene with a `CAMetalLayer`-backed view (`contentsScale = screen.nativeScale`).
  It then queues a request to the render thread, which calls `CreateSwapchain(METAL_EXT, layer)` at a
  frame boundary and registers a `Game` output.
- **TV mode off, disconnect, or resign active:** the render thread destroys the swapchain first, and
  the main thread waits for that (the same handshake as `requestExitVulkanRenderLoop`). Only then does
  it remove the window. Removing the window should give the TV back to system mirroring. **Verify on
  the device:** if iOS doesn't resume mirroring, we need a `Mirror` output for the menus instead.
- **Settings:** "When a TV is connected: Show the game on the TV (default) / Mirror the screen". The
  pause menu gets a toggle for the current session.
- **Out of scope:** iPad Stage Manager's interactive extended displays (role `Application`), and
  tvOS.
- **AirPlay latency:** this is the same video stream as mirroring, so the same lag. Input stays local,
  but the picture is late.

## Later clients

- **ImGui viewports:** `ImGuiConfigFlags_ViewportsEnable`, a platform backend that creates SDL or
  Win32 windows, and renderer hooks that map to this design:
  - `Renderer_CreateWindow` → `CreateSwapchain`
  - `Renderer_RenderWindow` → `BindFramebufferAsRenderTarget(swapchain)` and draw
  - `Renderer_SwapBuffers` → nothing, `Present()` covers it

  It also needs input routed per window, which is its own piece of work.
- **Desktop VR mirror:** with OpenXR on Vulkan, each XR swapchain is a `VulkanPresentation` (acquire
  maps to `xrAcquireSwapchainImage`/`xrWaitSwapchainImage`, present to `xrReleaseSwapchainImage`),
  and the desktop window stays primary. VR is GL-only today, so this also needs a Vulkan OpenXR path.
- **Android external display:** a `Presentation` dialog with its own `Surface`, as a `Game` output on
  Vulkan.

## Phases

1. **`OutputParams` refactor** of `PresentationCommon` and `CopyDisplayToOutput`. No change in
   behaviour, checked with frametests.
2. **thin3d `Swapchain` API plus Metal,** the iOS default. Single-output path unchanged: unit tests,
   pspautotests, frametests.
3. **Dev client on macOS SDL:** a `--game-window` option opens a second SDL window as a `Game` output
   (with `--graphics=metal`). This tests most of the design in a fast loop, without AirPlay. Headless
   can add an offscreen second output, so frametests can check what it shows.
4. **iOS TV mode,** on Metal, tested with the Apple TV.
5. **Vulkan** (`VulkanSurfaceSwapchain`, presentation-for-everything, per-output render manager
   state), for Android, the Mac default and Vulkan users on iOS. Single-output path unchanged: unit
   tests, pspautotests, frametests, an Android device. Then D3D11 when something wants it.

Open questions:
- Does removing the external window resume mirroring?
- How do two vsynced layers pace on a ProMotion phone?
- Which mode does a 4K Apple TV report? AirPlay tends to cap at 1080p.
