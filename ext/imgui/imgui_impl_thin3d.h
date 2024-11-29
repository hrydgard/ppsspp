// dear imgui: Renderer Backend for Vulkan
// This needs to be used along with a Platform Backend (e.g. GLFW, SDL, Win32, custom..)

// Implemented features:
//  [!] Renderer: User texture binding. Use 'VkDescriptorSet' as ImTextureID. Read the FAQ about ImTextureID! See https://github.com/ocornut/imgui/pull/914 for discussions.
//  [X] Renderer: Large meshes support (64k+ vertices) with 16-bit indices.

// Important: on 32-bit systems, user texture binding is only supported if your imconfig file has '#define ImTextureID ImU64'.
// See imgui_impl_vulkan.cpp file for details.

// The aim of imgui_impl_vulkan.h/.cpp is to be usable in your engine without any modification.
// IF YOU FEEL YOU NEED TO MAKE ANY CHANGE TO THIS CODE, please share them and your feedback at https://github.com/ocornut/imgui/

// You can use unmodified imgui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire imgui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

// Important note to the reader who wish to integrate imgui_impl_vulkan.cpp/.h in their own engine/app.
// - Common ImGui_ImplThin3d_XXX functions and structures are used to interface with imgui_impl_vulkan.cpp/.h.
//   You will use those if you want to use this rendering backend in your engine/app.
// - Helper ImGui_ImplThin3dH_XXX functions and structures are only used by this example (main.cpp) and by
//   the backend itself (imgui_impl_vulkan.cpp), but should PROBABLY NOT be used by your own engine/app code.
// Read comments in imgui_impl_vulkan.h.

#pragma once
#ifndef IMGUI_DISABLE
#include "imgui.h"      // IMGUI_IMPL_API

#include "Common/GPU/thin3d.h"
#include "Common/Math/lin/matrix4x4.h"

// Called by user code. Takes ownership of the font buffer and later deletes it.
IMGUI_IMPL_API bool ImGui_ImplThin3d_Init(Draw::DrawContext *draw, const uint8_t *ttf_font, size_t size);
IMGUI_IMPL_API void ImGui_ImplThin3d_Shutdown();
IMGUI_IMPL_API void ImGui_ImplThin3d_NewFrame(Draw::DrawContext *draw, Lin::Matrix4x4 drawMatrix);
IMGUI_IMPL_API void ImGui_ImplThin3d_RenderDrawData(ImDrawData* draw_data, Draw::DrawContext *draw);
IMGUI_IMPL_API bool ImGui_ImplThin3d_CreateDeviceObjects(Draw::DrawContext *draw);
IMGUI_IMPL_API void ImGui_ImplThin3d_DestroyDeviceObjects();

enum class ImGuiPipeline {
	TexturedAlphaBlend = 0,
	TexturedOpaque = 1,
};

// These register a texture for imgui drawing, but just for the current frame.
// Textures are unregistered again in RenderDrawData. This is just simpler.
IMGUI_IMPL_API ImTextureID ImGui_ImplThin3d_AddTextureTemp(Draw::Texture *texture, ImGuiPipeline pipeline = ImGuiPipeline::TexturedAlphaBlend);
IMGUI_IMPL_API ImTextureID ImGui_ImplThin3d_AddNativeTextureTemp(void *texture, ImGuiPipeline pipeline = ImGuiPipeline::TexturedAlphaBlend);
IMGUI_IMPL_API ImTextureID ImGui_ImplThin3d_AddFBAsTextureTemp(Draw::Framebuffer *framebuffer, Draw::FBChannel aspect = Draw::FB_COLOR_BIT, ImGuiPipeline pipeline = ImGuiPipeline::TexturedAlphaBlend);

void ImGui_PushFixedFont();
void ImGui_PopFont();

// Helper structure to hold the data needed by one rendering context into one OS window
// (Used by example's main.cpp. Used by multi-viewport features. Probably NOT used by your own engine/app.)
struct ImGui_ImplThin3dH_Window {
	int  Width = 0;
	int  Height = 0;
	bool ClearEnable = true;
};

#endif // #ifndef IMGUI_DISABLE
