# The Metal backend

A native Metal backend for Mac and iOS (tvOS too), alongside OpenGL, D3D11 and Vulkan. It's selected as
`GPUBackend::METAL` (`--graphics=metal`), and Vulkan through MoltenVK stays the default for now. Written
2026-10-19; file references will drift.

**Why:** programmable blending. Apple GPUs let a fragment shader read the color it's about to blend with
(`[[color(0)]]`), so PSP blend modes that need shader blending no longer copy the framebuffer for every
draw. MoltenVK can't expose that. Removing the MoltenVK dependency is a smaller benefit, and comes later.

## Where things are

| Path | What |
|---|---|
| `Common/GPU/Metal/MetalRenderManager.h/.mm` | Command recording and replay, push buffers, shader translation, background compiles |
| `Common/GPU/Metal/thin3d_metal.mm` | thin3d's `DrawContext`, for the UI, presentation and the shared GPU code |
| `Common/GPU/Metal/MetalGraphicsContext.h/.mm` | The `GraphicsContext`: device, layer (or an offscreen backbuffer for headless), render thread |
| `GPU/Metal/` | The emulation side: draw engine, shader manager, texture cache, framebuffer manager, `GPU_Metal` |

The backend only exists where `PPSSPP_API(METAL)` is set: Mac and iOS, not the libretro cores. The Objective-C
files are compiled with ARC.

## Shaders

The backend uses the same shaders as Vulkan: the generators emit Vulkan GLSL, glslang compiles it to
SPIR-V, and SPIRV-Cross's `CompilerMSL` turns that into MSL 2.3 (`TranslateVulkanGLSLToMSL`). Generating
MSL directly was considered and dropped. Translation takes about 0.5 to 0.7 ms (glslang) plus 0.2 to 0.4 ms
(SPIRV-Cross) per shader, against around 100 ms for Metal's own compile of the MSL when it isn't in the
system's Metal cache.

- **Bindings:** a Vulkan binding number is used as the Metal buffer, texture and sampler index. The vertex
  buffer is at index 16, and compute shaders' push constants at 15.
- **Coordinates:** `flip_vert_y` makes clip space match Vulkan's, so everything else (viewports,
  framebuffer origin, texture coordinates) matches Vulkan exactly. thin3d reports the Vulkan coordinate
  convention.
- **Framebuffer reads:** the fragment shader generator declares a `subpassInput` for shader blending when
  `FS_BIT_USE_FRAMEBUFFER_FETCH` is set, and SPIRV-Cross (`use_framebuffer_fetch_subpasses`) turns it into
  `[[color(0)]]`. Only Apple GPUs report `framebufferFetchSupported`.
- **Invariance:** functions are compiled with `preserveInvariance`, since multipass effects depend on
  different vertex shaders producing identical positions. It costs no measurable speed.

Shaders are translated when the shader manager creates them, since recording needs the texture masks the
translation produces. The MSL compile and the pipeline state are created on compile threads
(`RunOnCompileThread`). Draws record a `MetalPipelineState` that may still be compiling, and the replay
waits for it, as Vulkan does. Nothing is skipped.

Each game's shader IDs and pipeline keys go to `<discID>.mtlshadercache` (with a checksum, like the Vulkan
and GL caches), and are recreated while the game boots. Cold Wipeout Pure, 5 emulated seconds from a
savestate: 3.0 s with synchronous compiles, 1.6 s with background compiles.

## The render manager

The emulator thread records commands into a chunk, and they're encoded into Metal command buffers on a
render thread (when `bRenderMultiThreading` is on and more than one frame is in flight), or inline at
submit.

- **Lazy passes.** A render pass starts on its first draw, since Metal needs the load actions up front and
  resets all encoder state with each pass. A clear restarts the pass with clear load actions; binding the
  framebuffer that's already bound continues the pass, like Vulkan.
- **Redundant state** is dropped while recording.
- **Pass merging.** The replay merges later passes to the same framebuffer into earlier ones when nothing
  in between reads or overwrites it, by the rules of `VulkanQueueRunner::ApplyRenderPassMerge`. God of War
  went from 41 to 18 render passes a frame.
- **Depth.** A pass where no draw tests or writes depth or stencil neither loads nor stores it. The
  backbuffer's depth is memoryless on Apple GPUs.
- **Uploads** for new textures go in a command buffer of their own, enqueued before the frame's commands.
  Texture scaling's compute dispatches run there too.
- **Lifetimes.** Command buffers don't retain what they use (`commandBufferWithUnretainedReferences`), which
  saves a retain and release per binding. Anything that may still be in use goes through `DeferRelease`,
  which keeps it until the GPU is done with the frame. Frames in flight are limited by a semaphore.
- **Push buffers** keep 256 bytes free after each allocation, since the vertex decoder can write past the
  end of its output.

## Textures and framebuffers

- 16-bit PSP textures (5650, 5551, 4444) stay 16-bit on Apple GPUs, in `B5G6R5Unorm`, `BGR5A1Unorm` and
  `ABGR4Unorm` with a swizzle. Metal names packed channels from the lowest bits up, and the PSP stores red
  lowest.
- At max quality filtering, textures up to 256x256 get a full mip chain, generated with a blit.
- Hardware texture scaling runs the same compute shaders as Vulkan. Shaders with a constant buffer (only the
  smiley test shader) aren't supported.
- Framebuffers are `RGBA8Unorm` with `Depth32Float_Stencil8`. A depth-only or stencil-only copy goes
  through a buffer, since texture copies take both planes.
- MSAA works like the Vulkan backend's: a multisampled framebuffer has multisampled color and depth textures
  that are drawn to and kept between passes, and each pass resolves them into the ordinary ones (depth and
  stencil to sample 0), which everything else reads. Copies between two multisampled framebuffers copy the
  samples too. Metal can't copy a single plane of a multisampled depth/stencil texture, so those copy both.
  With programmable blending, the shader blends each sample with its own color, so edges come out a little
  different from Vulkan's, which blends with the resolved color.
- Readbacks for the GE debugger and screenshots work. Headless has no backbuffer to read, like headless
  Vulkan, and the debugger falls back to the PSP's framebuffer.

## Testing

- `PPSSPPHeadless --graphics=metal` renders offscreen, for frame dumps, savestates and the WebSocket debugger.
- With `MTL_DEBUG_LAYER=1`, Metal validates the API use. Lifetime bugs show up more reliably in the app,
  booting and quitting a game with `NSZombieEnabled=YES` too.
- Comparing against Vulkan on the same Mac is the main check. All 1061 frame dumps in `ppsspp-framedumps`,
  rendered with both: 1049 are identical. Of the rest, 10 differ because of programmable blending, and are
  closer to the PSP captures (Tenchu went from about 10,000 differing pixels to 900). The Kurohyou dumps are
  unreliable. The benchmark savestates match Vulkan to within a few levels per channel.
- On iOS, pass launch options through `devicectl`'s process arguments (`--graphics=metal`); the app adds
  `--` options from its own launch arguments.

## Speed

Measured 2026-10-17 on an Apple Silicon Mac, 10 emulated seconds from 8 savestates, against Vulkan through
MoltenVK: wall time from 11.7% lower (Ridge Racer) to 2.6% higher, CPU time 2 to 11% lower. God of War was
6% faster after pass merging; before it, Metal was slower because of the extra render passes.

## Not done

- **Intel and AMD Macs** are untested. They get no programmable blending, no 16-bit textures and no
  memoryless depth (all Apple GPU only), so they take the same paths as other GPUs elsewhere.
- **tvOS** shares the iOS code, but hasn't been built or run with Metal.
- **The dev screen's pipeline list** is Vulkan only.
- **Stereo rendering** (multiview, framebuffer arrays) is deliberately left out.
- **Making Metal the default and removing MoltenVK** are separate steps, after this has seen some use. The
  render manager includes Vulkan headers for `GLSLtoSPV`, which has to change then.
