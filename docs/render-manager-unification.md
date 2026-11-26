# Unifying the render managers

A design for giving the backends one render manager interface, and moving thin3d on top of it. Written
2026-10-18 during the Metal backend work, updated 2026-11-26 after Metal was merged; the counts and file
references will drift.

**Summary:** every backend gets a render manager with the same interface for resources, passes,
pipelines and uniforms. The GPU layer and thin3d both use the render manager's objects directly, so
thin3d stops wrapping them and needs one implementation, not one per backend. Render managers own their
pipelines. GL uses uniform buffers, and a small corner of `GLRenderManager` turns them back into plain
uniforms for GLES 2 and for drivers where uniform buffers are slow. Later, if the overhead stays under
1%, game draws go through the same interface, and the end state is one command list format with a
replay written per backend. That list is what lets the shared code edit a frame before it's replayed:
inserting fullscreen effects between the scene and the HUD, splitting a pass into a Z prepass, AO and
a final pass, and interpolating frames by replaying with in-between transforms.

## Where things stand

The line between thin3d and the backends is fuzzy. Measured on master, 2026-11-26:

- The shared layer leans on thin3d heavily: 314 uses of `draw_` in `GPU/Common`, mostly in
  `FramebufferManagerCommon`, `PresentationCommon`, `StencilCommon`, `TextureCacheCommon`, `Draw2D` and
  `DepthBufferCommon`. Framebuffers, copies, blits, readbacks, stencil uploads, reinterprets, depal and
  presentation all go through it.
- The backends use it lightly and draw the game natively, reaching the API through about 100
  `GetNativeObject()` calls.

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
- **Device loss is part of the contract.** Android GL loses its context when the app is backgrounded,
  and D3D11 devices can be removed. Today `GPUCommon::DeviceLost()`/`DeviceRestore()` tear down and
  rebuild each backend's objects by hand. The interface says which objects a render manager recreates by
  itself (pipelines from their descriptions, samplers) and which the owner has to recreate (textures and
  framebuffers, whose contents are gone), and how the owner is told.
- **The render manager tracks the current state.** Utility passes and game draws go through the same
  render manager, so it always knows what's bound. That replaces the `Invalidate`/`Dirty` calls (seam 2):
  the draw engine re-applies its state when the render manager says it changed.

## Pipelines move into the render manager

The render manager owns API-level pipeline objects, deduplicated by a hash of their description. It
also owns:

- **variants:** Vulkan already builds a pipeline per render pass type and sample count, which nothing
  outside should need to know;
- **background compilation:** a pipeline handle can be bound while its pipeline is still compiling, and
  the replay waits for it, as Vulkan and Metal both do now. Nothing is skipped;
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

thin3d's GL driver already does this for its own shaders: a pipeline's `UniformBufferDesc` lists each
field's offset and type, and the driver issues `glUniform*` calls for them from the blob
(`dynamicUniformLocs_` in `Common/GPU/OpenGL/thin3d_gl.cpp`). That code is the starting point; it moves
down into `GLRenderManager` and gains the dirty mask.

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

Game draws don't go through thin3d. They go to the render managers, at first through each backend's own
draw engine, and later possibly through the interface (see below).

## What it unlocks

- `BuildTexture` can move into `TextureCacheCommon`, with the backends doing only the native create and
  upload (format choice, the 16-bit decision, the max-quality mip rule and the level loop are the same in
  all four today).
- The bound-texture hooks and most `GetNativeObject()` calls disappear.
- The software-transform half of `DrawEngine*::Flush`, nearly identical in all four backends, gets
  easier to share once state and binding go through one interface.

## Later: game draws through the interface

If the interface is fast enough, the game draws can go through it too, and the four draw engines become
one. The target is under 1% overhead.

Virtual calls aren't the problem. A game draw is about ten operations (bind the pipeline, push uniforms,
vertices and indices, bind textures and samplers, set dynamic state, draw). Even at a few thousand draws
a frame (Tekken's many tiny draws are the worst measured case), that's a few million calls a second, a
fraction of a percent of one core. The risk is what an abstraction loses on the way:

- **Push buffers.** Vertices are decoded straight into mapped push buffer memory. The interface hands out
  a raw pointer plus a buffer handle and offset, so nothing is copied twice.
- **Per-bind costs.** Binds take plain handles, with no refcounting, and pipelines a precomputed handle.
  Hashing a pipeline description per draw would be the most expensive mistake.
- **Fast paths.** Skipping redundant state while recording (Metal), caching descriptor sets (Vulkan) and
  tracking dirty uniforms (GL) live inside the render managers, below the interface. Most already do.
- **Feature differences.** Provoking vertex, dual-source blending, framebuffer fetch versus copying the
  framebuffer for shader blending, depth clamp and clip distances stay capability flags that the shared
  draw engine branches on, as each `Flush` does today.

The interface then also needs push buffer allocation for vertices, indices and uniforms, binding vertex
and index buffers at an offset, binding uniform block ranges per slot, texture and sampler slots, and
draws. Clears inside a pass and render pass merging stay internal to the render managers.

The design of the resource interface (step 4 below) has to allow for this from the start: plain handles,
no refcounting on bind, push buffers as pointers.

**Measuring it:** rewrite `DrawEngineMetal` against the interface and compare it with the current one on
the benchmark savestates, by instructions retired and cycles rather than wall time, which is too noisy
for a difference this size. Tekken is the stress case. If Metal stays under 1%, the shared draw engine
replaces Vulkan's next, then the others.

**End state:** a command list format shared by all backends, recorded by the shared code, with a replay
("player") written per backend. Vulkan, GL and Metal already record commands and replay them, each in a
format of its own. D3D11 would get a player too, replaying on the emulator thread. Recording, state
deduplication and render pass merging could then be written once.

## Later: editing the frame

A shared command list is also what makes frame-level effects possible. The current post-processing
shaders (`PresentationCommon`) only see the final image: no depth, and the HUD already baked in. With
the game's draws recorded in one format, the shared code can rewrite a frame before it's replayed:

- **Inserting passes:** fullscreen effects (blur, bloom, color grading, SSAO) between the 3D scene and
  the HUD, reading the scene's color and depth.
- **Splitting passes:** replay the scene's draws depth-only as a Z prepass, compute AO from that depth,
  then replay them again with depth test `EQUAL` and the AO texture applied in the fragment shader.
- **Frame interpolation:** match draws between two consecutive frames, interpolate their transforms, and
  replay the frame with the in-between matrices. Rendering motion vectors from the previous frame's
  matrices is the cheaper variant, feeding a GPU-side interpolator (FSR 3, MetalFX) instead of a
  second full replay.

None of this needs vertex data rewritten; it all works on draws, passes and their state. It does put
requirements on the design, which are cheap to meet up front and expensive to retrofit:

- **Commands carry GE-side metadata.** The player ignores it, but an editor needs, per draw: the PSP
  framebuffer and depth buffer addresses, through mode (a reasonable first cut at "HUD": the first
  through-mode draw after 3D in the displayed framebuffer is where the scene ends), the world, view and
  projection matrices (or their known offsets in the uniform blob, which has one shared layout), and a
  draw identity stable across frames for interpolation (vertex address, texture, shader ID and order
  within the pass).
- **The edit runs on the shared list, before backend optimization.** Record, then edit, then merge
  passes, deduplicate state and replay. If a backend merges passes first, the editor can't find the
  scene's boundaries.
- **Pipeline descriptions can be rewritten.** A depth-only variant (color writes off), depth func
  `EQUAL`, and fragment shader variants that sample the AO texture. The AO variant is a new shader ID
  bit, so a cache version bump (see AGENTS.md).
- **Depth is sampleable on every backend**, MSAA resolved. Vulkan and Metal already sample depth for
  depal and depth readback; GL and D3D11 need checking, and GLES 2 may simply not get these effects.
- **Replaying a frame twice must be safe.** Commands that have effects outside the frame (readbacks to
  PSP memory, framebuffer downloads, copies other frames depend on) run once. The uniform and vertex
  push buffer memory stays alive until every replay of the frame is done.

Limits to expect:

- **Software transform** bakes the matrices into the vertices, so interpolation needs hardware
  transform. So does anything else the CPU animates: CPU skinning and morphing, games that transform
  on the VFPU and draw in through mode, texture scrolling done by rewriting UVs.
- **Matching draws** fails when a game reorders its draws between frames or streams vertices into
  reused buffers; unmatched draws are drawn as in the newer frame.
- **Render-to-texture chains** (reflections, shadow maps, the many games that render to a texture and
  then draw it) make "the scene" a heuristic. The scene pass is the one that ends in the displayed
  framebuffer with a depth buffer attached; effects that guess wrong need a per-game override.

## Order of work

0. Merge the Metal backend. Done: it's the default on iOS, while Vulkan through MoltenVK stays the
   default on ARM Macs. Its render manager is the newest and the closest to what the shared one should
   look like.
1. A small abstract `RenderManager` parent class (`Common/GPU/RenderManager.h`), starting with what thin3d's
   `DrawContext` answers per backend today: device caps, bugs, the shader language description and the
   supported data formats. Each render manager does its own detection and owns the result, and the thin3d
   drivers forward to it. Behavior doesn't change, which a before/after dump of the caps and bugs per
   backend shows.
2. A D3D11 render manager in `Common/GPU/D3D11/`, shaped after Vulkan's, owning the device, the
   immediate context and the caps. It executes immediately, so the interface meets the "no deferred
   replay" rule from the start.
3. Settle the lifetime rule and the state-tracking contract.
4. Define the interface for framebuffers, textures and buffers. Implement it on all four render managers,
   Metal and Vulkan first, and move `FramebufferManagerCommon` and `TextureCacheCommon` onto it.
5. Move pipelines into the render managers, Metal and Vulkan first. This comes before the facade,
   since Draw2D, presentation and the UI create their pipelines through `DrawContext`.
6. Make Metal's thin3d a facade over the interface, and check the frame dumps against Vulkan
   (`frametests.py`, and the full-dump Metal/Vulkan comparison). Then Vulkan, D3D11 and GL. GL's
   facade keeps thin3d's uniform cracking until step 7 moves it into `GLRenderManager`.
7. GL: uniform buffers, the cracking corner.
8. Game draws through the interface: Metal's draw engine first, measured against the current one, then
   the other backends if it holds up.
9. The shared command list, with a player per backend, carrying the GE metadata from the start.
10. Frame editing: first a fullscreen pass inserted between the scene and the HUD, since it needs
    only pass boundaries and sampleable depth. Then the Z prepass and AO split, then interpolation
    (motion vectors first, a full second replay after).

Each step is a PR of its own and leaves every backend working.

## Open questions

- The exact lifetime rule, and whether thin3d's refcounting stays as the outer layer.
- How the draw engine learns that the render manager's state changed: a counter it checks, or a
  callback.
- Whether pipelines bake viewport-independent raster state on every backend, or GL and D3D11 keep it
  dynamic.
- Which GLES 3 drivers need the uniform cracking path. That needs measurements on devices.
- How effects find the scene pass and the HUD boundary across games, and how per-game overrides are
  expressed (compat.ini, or something in the post-shader definitions).
- Whether frame interpolation holds up on real games, given how many animate on the CPU. Worth a
  survey of the benchmark savestates (how many draws match, how many are hardware-transformed) before
  building it.
