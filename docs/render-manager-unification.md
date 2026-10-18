# Unifying the render managers

A design for giving the backends one render manager interface, and moving thin3d on top of it. Written
2026-10-18, during the Metal backend work (branch `metal-backend`); the counts and file references will
drift.

**Summary:** every backend gets a render manager with the same interface for resources, passes,
pipelines and uniforms. The GPU layer and thin3d both use the render manager's objects directly, so
thin3d stops wrapping them and needs one implementation, not one per backend. Render managers own their
pipelines. GL uses uniform buffers, and a small corner of `GLRenderManager` turns them back into plain
uniforms for GLES 2 and for drivers where uniform buffers are slow.

## Where things stand

The line between thin3d and the backends is fuzzy. Measured on `metal-backend`:

- The shared layer leans on thin3d heavily: 266 calls in `GPU/Common`, mostly in
  `FramebufferManagerCommon` (84), `PresentationCommon` (54), `StencilCommon` (39), `TextureCacheCommon`
  (36), `Draw2D` (25) and `DepthBufferCommon` (20). Framebuffers, copies, blits, readbacks, stencil
  uploads, reinterprets, depal and presentation all go through it.
- The backends use it lightly (Vulkan 53 calls, GL 43, D3D11 31, Metal 20) and draw the game natively,
  reaching the API through about 80 `GetNativeObject()` calls.

Three seams cause most of the trouble:

1. **Textures have two owners.** Game textures are native (`VulkanTexture`, `GLRTexture`, a raw
   `MTLTexture`), framebuffer textures are bound through thin3d, and hooks like
   `BoundFramebufferTexture()`, `GetNativeTextureView()` and `NativeObject::BOUND_TEXTURE` get the
   native handle back out. This is also why `BuildTexture` exists four times.
2. **Render state is shared and repaired by hand.** After a thin3d draw (Draw2D, stencil upload,
   reinterpret), the shared code calls `draw_->Invalidate(CACHED_RENDER_STATE)` and
   `gstate_c.Dirty(...)` so the draw engine re-applies its state. A missing call is a bug.
3. **Everything is wrapped twice.** A Vulkan framebuffer is a `VKRFramebuffer` inside a `VKFramebuffer`,
   GL wraps `GLRFramebuffer`, Metal wraps an `MTLTexture`. Each backend's thin3d driver is a few thousand
   lines, much of it forwarding.

Vulkan, GL and Metal have render managers (`VulkanRenderManager`, `GLRenderManager`,
`MetalRenderManager`) that record commands on the emulator thread and replay them on a render thread.
D3D11 has none and draws into its immediate context.

## The interface

One abstract interface, implemented by each backend's render manager. The shared code (framebuffer
manager, texture cache, Draw2D, presentation, and thin3d itself) calls it through virtual calls, which
is fine off the hot path. The backends' own draw engines keep calling their concrete render manager.

**Resources:** framebuffers (color, depth/stencil, sample count, layers), textures (levels, 3D slices,
the 16-bit formats and their swizzles, mip generation), buffers, samplers.

**Passes and copies:** binding a framebuffer as the render target with load/store actions, the
backbuffer pass, clears, copies and blits between framebuffers (including separate depth and stencil
copies), texture uploads, readbacks to memory, and binding a framebuffer as a texture.

**Pipelines:** D3D11-style creation from a description: shaders, vertex layout, and blend, depth/stencil
and raster state objects. This is thin3d's current pipeline model, moved down a level. Each backend maps
it naturally: D3D11 directly, Metal into a pipeline state plus a separate depth/stencil state, Vulkan by
baking everything into its pipeline, GL by applying the states with redundancy filtering.

**Dynamic state:** viewport, scissor, stencil reference and blend color, as commands.

**Uniforms:** std140 uniform blocks, passed as a blob with a dirty mask (see the GL section).

### Rules the interface has to follow

- **It must be implementable immediately.** D3D11 will implement it without recording, so nothing may
  depend on deferred replay: readbacks can be synchronous, and an object is usable as soon as it's
  created. This is a good test of the interface in general.
- **One lifetime rule.** thin3d objects are refcounted, while the render managers each release things
  their own way (Vulkan's per-frame delete queues, GL's queued deletes, Metal's `DeferRelease`). The
  interface defines when an object may be released and who keeps it alive until the GPU is done with it.
  Mistakes here crash at shutdown or on device loss, so this needs settling before anything moves.
- **The render manager tracks the current state.** Utility passes and game draws go through the same
  render manager, so it always knows what's bound. That replaces the `Invalidate`/`Dirty` calls (seam 2):
  the draw engine re-applies its state when the render manager says it changed.

## Pipelines move into the render manager

The render manager owns API-level pipeline objects, deduplicated by a hash of their description. It
also owns:

- **variants:** Vulkan already builds a pipeline per render pass type and sample count, which nothing
  outside should need to know;
- **background compilation and waiting,** which Vulkan and Metal now each do in their own way;
- **lifetime.**

The GPU layer keeps everything PSP-specific:

- the translation from GE state to a pipeline description, and the shader IDs;
- the disk cache, which records the combinations used and replays them at boot;
- a small cache from its GE key to the pipeline handle, so the hot path stays one lookup. Without it,
  every state change would go GE key, description, hash, lookup.

Vulkan bakes depth and stencil state into its pipelines, so its pipeline count stays as it is.
Extended dynamic state could reduce it later, but support on older Mali and Adreno drivers is uneven.

## GL uniforms

GLES 3 and desktop GL use real uniform buffers, with the same std140 blocks as Vulkan and Metal, filled
from the same blob. `GLRenderManager` binds buffer ranges, preferably from a ring buffer with
`glBindBufferRange`; updating a buffer the GPU is still reading with `glBufferSubData` can stall.

GLES 2 has no uniform buffers, and some GLES 3 drivers handle them slowly. For both, `GLRenderManager`
cracks the blob into plain uniforms:

- the shader generators emit either the uniform block or loose uniforms, chosen per device, plus a
  table mapping each field's offset in the blob to its uniform location and type;
- the render manager receives the same blob and dirty mask either way, and issues `glUniform*` calls for
  the dirty fields. The dirty mask keeps this as cheap as today's per-uniform tracking in
  `ShaderManagerGLES`;
- nothing above `GLRenderManager` knows which path is in use.

The choice is a `Draw::Bugs` entry (say `UNIFORM_BUFFERS_SLOW`), always set on GLES 2 and set for GLES 3
drivers where uniform buffers are measured to be slower. Measure with the ring buffer in place before
listing a driver, since the slowness is often the update pattern.

The cracking path can be forced on desktop GL, so it gets frame-dump coverage without the hardware.

Simulating uniform buffers with uniform arrays on GLES 2 was considered and dropped: GLES 2 only
guarantees 16 vec4 uniforms in a fragment shader, which the base block exceeds, and it has no integer
types or `floatBitsToUint` for the block's `uint` fields.

GLES 2 stays supported. Android's minimum is API 21, and GLES 2-only GPUs (Mali-400/450, Tegra 4,
older PowerVR SGX) still show up there. The manifest declares GLES 2 (`android/AndroidManifest.xml`).

## thin3d

Once a backend's render manager implements the interface, thin3d's `DrawContext` for it becomes a
facade over that interface. The facade is the same for every backend, so the per-backend thin3d drivers
go away one at a time. The UI, presentation, libretro and anything else that wants a generic API keep
using `DrawContext`.

Game draws stay outside thin3d. Routing them through its virtual pipeline and state calls would cost
speed on the hot path.

## What it unlocks

- `BuildTexture` can move into `TextureCacheCommon`, with the backends doing only the native create and
  upload (format choice, the 16-bit decision, the max-quality mip rule and the level loop are the same in
  all four today).
- The bound-texture hooks and most `GetNativeObject()` calls disappear.
- The software-transform half of `DrawEngine*::Flush`, nearly identical in all four backends, gets
  easier to share once state and binding go through one interface.

## Order of work

1. Settle the lifetime rule and the state-tracking contract.
2. Define the interface for framebuffers, textures and buffers. Implement it on Metal and Vulkan, whose
   render managers have the same shape, and move `FramebufferManagerCommon` and `TextureCacheCommon`
   onto it.
3. Make Metal's thin3d a facade over the interface, and check the frame dumps against Vulkan
   (`frametests.py`, and the full-dump Metal/Vulkan comparison).
4. Do the same for Vulkan.
5. Write the D3D11 render manager, implemented immediately, and move D3D11 over.
6. Move pipelines into the render managers, Metal and Vulkan first.
7. GL: uniform buffers, the cracking corner, then the facade.

Each step is a PR of its own and leaves every backend working.

## Open questions

- The exact lifetime rule, and whether thin3d's refcounting stays as the outer layer.
- How the draw engine learns that the render manager's state changed: a counter it checks, or a
  callback.
- Whether pipelines bake viewport-independent raster state on every backend, or GL and D3D11 keep it
  dynamic.
- Which GLES 3 drivers need the uniform cracking path. That needs measurements on devices.
