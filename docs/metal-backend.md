# Feasibility of a native Metal backend

An analysis of what it would take to add a Metal rendering backend to PPSSPP, and whether it's worth
doing. Written 2026-09-03 against the state of the tree at that time; the line counts and file
references will drift.

**Summary:** technically very feasible, and the tree is already shaped for it. But "get rid of the
MoltenVK dependency" is the weakest reason to do it - the good reason is programmable blending.

## What's already in place

- `ios/ViewControllerMetal.h` says, in its own comment, *"Used by both Vulkan/MoltenVK and the future
  Metal backend"*. The presentation layer is already a `CAMetalLayer`, so the window-system half of
  the job is done.
- `ext/SPIRV-Cross.vcxproj` already compiles `spirv_msl.cpp`. The CMake build only links
  `spirv-cross-glsl` (and `spirv-cross-hlsl` conditionally), so enabling MSL translation there is a
  one-line change.
- `GPUBackend` in `Core/ConfigValues.h` has a free slot at 1, where D3D9 used to live.

## Scale of the work

A backend is two layers. Measured against the existing three:

| Layer | D3D11 | GLES | Vulkan |
|---|---|---|---|
| `Common/GPU/<B>/` - thin3d driver, render manager | 2,093 | 8,308 | 14,223 |
| `GPU/<B>/` - draw engine, texture cache, framebuffers, shaders, state mapping | 2,697 | 3,540 | 5,129 |

`thin3d.h` has roughly 53 pure virtuals to satisfy across its interfaces.

Metal sits architecturally between D3D11 and Vulkan: explicit pipeline state objects and command
buffers like Vulkan, but automatic residency and resource lifetime like D3D11 - no descriptor sets,
no memory allocator, no manual barriers for the common cases. So `Common/GPU/Metal/` should land
closer to D3D11's 2k than to Vulkan's 14k. It does need a `MetalRenderManager` modelled on
`VulkanRenderManager` for render pass batching, because Apple tilers punish mid-pass flushes hard and
PPSSPP's framebuffer juggling causes a lot of them.

Estimate: **6-9k lines**, in Objective-C++.

## The real decision: how to get shaders

Game shaders are generated per shader-ID at runtime by `GPU/Common/FragmentShaderGenerator.cpp` and
`VertexShaderGenerator.cpp` through `Common/GPU/ShaderWriter.cpp`, parameterised by
`ShaderLanguageDesc`. There are 68 language-conditional sites across those three files.

**Option A - teach ShaderWriter and the generators to emit MSL.** Most work, best end result, no
runtime translator. But MSL is not a GLSL/HLSL dialect: it's C++14, with struct-based stage I/O,
explicit `[[attribute(n)]]` / `[[buffer(n)]]` binding attributes, and textures and samplers passed as
function arguments rather than declared as globals. `ShaderWriter` is built around the family
resemblance between GLSL and HLSL, so this is closer to writing a third code generator than to adding
a flag. Adding `MSL = 32` to the `ShaderLanguage` bitmask in `Common/GPU/Shader.h` is the trivial part.

**Option B - generate `GLSL_VULKAN` and translate at runtime**, glslang -> SPIR-V ->
`spirv_cross::CompilerMSL`. `Common/GPU/ShaderTranslation.cpp` already does exactly this shape for
HLSL. Far less work, and the right thing to start with - but be clear that it is reimplementing
MoltenVK's shader half, so glslang and SPIRV-Cross stay as runtime dependencies and only the Vulkan
API emulation is shed. Metal PSO creation from MSL source is also slower than Vulkan pipeline creation
from SPIR-V, so the existing asynchronous pipeline compilation machinery matters more, not less.

Option A is an optimisation that can be done later, or never.

## What a Metal backend actually buys

The strongest argument is one line of code:

```cpp
// Common/GPU/Vulkan/thin3d_vulkan.cpp
caps_.framebufferFetchSupported = false;
```

`framebufferFetchSupported` is a first-class thin3d capability. `GPU/GPUCommonHW.cpp` reads it and,
when true, switches shader blending from `FBO_TEX_COPY_BIND_TEX` to `FBO_TEX_READ_FRAMEBUFFER` - no
framebuffer copy at all. Today only the GL backend sets it, via `EXT_shader_framebuffer_fetch` /
`ARM_shader_framebuffer_fetch`. Vulkan hardcodes it to false, and MoltenVK cannot portably change
that.

Metal on Apple GPUs has native programmable blending (`[[color(0)]]` as a fragment shader input). A
Metal backend sets that capability to true and immediately lights up an existing, already-tested code
path, removing a framebuffer copy per draw in every game that needs PSP blend mode emulation. That is
a structural win on exactly the workload PPSSPP is bottlenecked on, and no MoltenVK release can
deliver it.

Secondary wins:

- `MTLStorageModeMemoryless` for depth/stencil on tilers - never allocate backing store for buffers
  that don't survive the render pass.
- Real Xcode GPU frame capture and shader profiling, instead of debugging through a translation layer.
- No vendored `ios/MoltenVK/MoltenVK.xcframework/ios-arm64/libMoltenVK.a` in the repo.

That last one - the actual "remove the dependency" item - is the smallest of them.

## What it costs

A permanent additional backend to maintain, testable only on Apple hardware, duplicating effort every
time something changes in the shared GPU code. PPSSPP already carries three. MoltenVK is
Khronos-maintained and works. If the Metal backend ships and isn't clearly faster, the project has
taken on maintenance for nothing.

One thing that is *not* a cost: the feature envelope. Metal has no geometry shaders, but neither does
MoltenVK, so the Vulkan backend's `geometryShader` feature check already fails on Apple and PPSSPP
already runs the fallback paths there. A Metal backend targets the exact reduced feature set Apple
users run today - there are no new gaps to fill.

## Suggested sequencing

Do the risky part first and make it provable:

1. `Common/GPU/Metal/thin3d_metal.mm` implementing `DrawContext`, with shaders via option B.
2. Bring up the **UI** on it - the UI renders through thin3d directly, so this exercises the ~53
   virtuals, the render manager design and the shader pipeline against something you can see, with
   none of the emulation layer involved.
3. Only then port the emulation layer into `GPU/Metal/`, cribbing structure from `GPU/D3D11/`, which
   is the closest existing fit.

Step 2 is the go/no-go gate: if the numbers don't justify continuing, everything up to that point is
cheap to throw away.
