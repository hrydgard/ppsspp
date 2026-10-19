# Shader compilation stutter

Where the time goes when a game compiles its shaders and pipelines, mainly on the Mac (Vulkan through
MoltenVK), and how to measure it. Measured in October 2026 on an M1 with MoltenVK 1.4.2. The Apple apps have
since moved to the Metal backend (see [metal-backend.md](metal-backend.md)), which compiles the same way:
Metal's MSL compile is still the expensive step.

## The path a shader takes

1. The shader generators (`GPU/Common/VertexShaderGenerator.cpp`, `FragmentShaderGenerator.cpp`)
   write GLSL from a `VShaderID` / `FShaderID`.
2. glslang compiles it to SPIR-V (`GLSLtoSPV()` in `Common/GPU/Vulkan/VulkanContext.cpp`), on a
   dedicated thread per shader (`CompileShaderModuleAsync()` in `GPU/Vulkan/ShaderManagerVulkan.cpp`).
3. `vkCreateGraphicsPipelines()` runs on the thread pool, which `VulkanRenderManager::CompileThreadFunc()`
   feeds, one task per vertex/fragment shader pair. On MoltenVK this is where the rest happens:
   SPIRV-Cross turns SPIR-V into MSL, Metal compiles the MSL into an `MTLLibrary`, and then builds the
   `MTLRenderPipelineState`. MoltenVK's `vkCreateShaderModule()` only copies the SPIR-V.
4. The render thread waits for the pipeline in `VulkanQueueRunner` (`BlockUntilReady()`) when it
   reaches the first draw that uses it. That wait is the stutter.

## Where the time goes

Wipeout Pure from a savestate, starting with no PPSSPP cache and an empty Metal cache. MoltenVK's own
counters (see below):

| Stage | Time each | Count |
|---|---|---|
| glslang, GLSL to SPIR-V | ~1 ms | 27 |
| SPIRV-Cross, SPIR-V to MSL | 0.5 ms | 40 |
| Metal, MSL to `MTLLibrary` | 85-110 ms (max ~470) | 43 |
| Metal, `MTLFunction`s to a pipeline state | 5 ms | 63 |

Nearly all the cost is Metal compiling MSL, and it happens about once per distinct shader, not once
per pipeline. Pipelines that differ only in blend, cull, depth or stencil state cost about 5 ms each
once their shaders' libraries exist. On the Mac, then:

- Speeding up the shader generators or glslang gains nothing worth having.
- Cutting the pipeline count (more dynamic state, e.g. cull mode and depth/stencil through
  `VK_EXT_extended_dynamic_state`) helps little. In this run it would have taken 58 pipelines down to
  43 that differ in anything Metal bakes in, and the expensive step doesn't repeat for those anyway.
- Fewer distinct shaders (moving ID bits into uniforms) is the lever that matters, with the runtime
  cost that ubershaders bring, mostly in fragment shaders.

### The shared `VkPipelineCache`

We used to create every pipeline with the same `VkPipelineCache`, and MoltenVK serializes the MSL
compiles of pipelines that share one: its "retrieve shader library from the cache" time, which includes
waiting for another thread's compile, averaged 142 ms. The 3.7 s of compiling blocked the render thread
for 2.9 s in total, so there was almost no parallelism. The cache is only held in memory (we don't save
its data), and on MoltenVK all it saves is the SPIR-V to MSL step, so on Apple platforms we now pass
`VK_NULL_HANDLE` (`CreateVulkanPipeline()` in `GPU/Vulkan/PipelineManagerVulkan.cpp`).

| Cold Wipeout Pure, two runs each | With the cache | Without |
|---|---|---|
| Render thread blocked on pipelines, total | 2.9 s | 1.2 s |
| Longest single stall | 1.0-1.1 s | 0.3 s |
| Average wait inside MoltenVK's cache lookup | 142 ms | 11 ms |

`MVK_CONFIG_SHOULD_MAXIMIZE_CONCURRENT_COMPILATION=1` made no measurable difference.

## The caches

- **PPSSPP's per-game cache**, `PSP/SYSTEM/CACHE/<DISCID>.vkshadercache`. It holds the shader IDs and
  pipeline keys the game used, plus their SPIR-V, and `GPU_Vulkan::LoadCache()` recompiles all of it
  while the game boots. With it, Wipeout's 62 pipelines took 140 ms in total.
- **macOS's Metal cache**, keyed by the MSL source. With it warm, even a run *without* PPSSPP's cache
  took about 100 ms for all 62 pipelines. So on the Mac, a second boot is cheap with or without our
  cache, and a shader one game compiled is cheap in the next game too. Only the first time a shader
  is ever seen (and possibly after an OS or app update) is it slow.

### How game-specific are the shaders?

Less than expected. Across the 37 `.vkshadercache` files of one Mac's memory stick (at the current
`CACHE_VERSION`), for the seven biggest:

| Game | VS | Also in another game | FS | Also in another game |
|---|---|---|---|---|
| UCES00002 (Ridge Racer) | 12 | 92% | 47 | 51% |
| UCES01250 | 13 | 85% | 36 | 67% |
| UCES00001 (Wipeout Pure) | 14 | 93% | 25 | 80% |
| UCES00465 | 12 | 100% | 22 | 86% |
| UCES00842 (God of War: Chains of Olympus) | 11 | 82% | 21 | 76% |
| ULES00502 (GTA: Vice City Stories) | 10 | 90% | 20 | 70% |
| ULES01376 (Tekken 6) | 11 | 73% | 20 | 65% |

All 37 games together use only 48 distinct vertex shader IDs. The IDs depend on the device's GPU
features and the user's settings, and some of those caches come from short runs, so a list shipped with
the app would match less well than one gathered on the device. A per-device list of shaders seen in
several games, precompiled at boot, looks worth trying.

## Ideas not done yet

- Don't block the render thread on a pipeline that isn't ready: skip the draw, or keep the previous
  pipeline, for the few frames until it is. Visible (objects pop in), so probably an option, but it
  would remove the remaining stalls on every backend.
- Precompile cross-game shaders at boot, from the list above.
- A native Metal backend could use `MTLBinaryArchive`. Not measured. See
  [metal-backend.md](metal-backend.md).

## Measuring

`--shader-cache=false` turns off loading and saving PPSSPP's per-game cache for one run (headless or
the app; it isn't saved to the ini). On the Mac that isn't a cold start, since Metal's cache still
hits. Headless is an unbundled binary and shares Metal's cache with other command-line tools, in
`$(getconf DARWIN_USER_CACHE_DIR)com.apple.metal/`. For a real cold start, move the GPU's folders
there aside (on the M1, `16777235_322` and `32024`, the two big ones) and put them back afterwards.
`MTL_SHADER_CACHE_DISABLE` and similar environment variables don't turn the cache off.

```bash
build-release/PPSSPPHeadless "$HOME/PSP/Wipeout Pure.cso" --memstick=$HOME/.config/ppsspp \
    --state=$HOME/.config/ppsspp/PSP/PPSSPP_STATE/UCES00001_1.01_0.ppst \
    --graphics=vulkan --timeout-wall=30 --timeout-emulated=20 --log --shader-cache=false
```

- Each pipeline's creation time is logged at INFO level, as `Pipeline (x/N) time on ...`.
- MoltenVK's breakdown: set `MVK_CONFIG_PERFORMANCE_TRACKING=1` and
  `MVK_CONFIG_ACTIVITY_PERFORMANCE_LOGGING_STYLE=2`. The summary prints when the device is destroyed,
  at the end of the run.
- The render thread's stalls aren't logged. Time `BlockUntilReady()` in `VulkanQueueRunner.cpp`
  temporarily to see them; the sum of pipeline times overstates the stall, since compiles overlap.
- Run each configuration at least twice. Single cold runs varied by about 15%.
