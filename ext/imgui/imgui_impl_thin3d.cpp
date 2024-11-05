// dear imgui: Renderer Backend for PPSSPP's thin3d

#include "imgui.h"
#include "imgui_impl_thin3d.h"
#include <cstdio>

#include "Common/System/Display.h"
#include "Common/Math/lin/matrix4x4.h"

Lin::Matrix4x4 g_drawMatrix;

struct ImGui_ImplThin3d_Data {
	Draw::SamplerState *fontSampler = nullptr;
	Draw::Texture *fontImage = nullptr;
	Draw::Pipeline *pipeline = nullptr;
};

// Backend data stored in io.BackendRendererUserData to allow support for multiple Dear ImGui contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single Dear ImGui context + multiple windows) instead of multiple Dear ImGui contexts.
// FIXME: multi-context support is not tested and probably dysfunctional in this backend.
static ImGui_ImplThin3d_Data* ImGui_ImplThin3d_GetBackendData() {
	return ImGui::GetCurrentContext() ? (ImGui_ImplThin3d_Data *)ImGui::GetIO().BackendRendererUserData : nullptr;
}

static void ImGui_ImplThin3d_SetupRenderState(Draw::DrawContext *draw, ImDrawData* draw_data, Draw::Pipeline *pipeline, int fb_width, int fb_height) {
	ImGui_ImplThin3d_Data* bd = ImGui_ImplThin3d_GetBackendData();

	// Bind pipeline and texture
	draw->BindPipeline(pipeline);
	draw->BindTexture(0, bd->fontImage);
	draw->BindSamplerStates(0, 1, &bd->fontSampler);

	// Setup viewport:
	{
		Draw::Viewport viewport;
		viewport.TopLeftX = 0;
		viewport.TopLeftY = 0;
		viewport.Width = (float)fb_width;
		viewport.Height = (float)fb_height;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		draw->SetViewport(viewport);
	}

	// Setup scale and translation:
	// Our visible imgui space lies from draw_data->DisplayPps (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single viewport apps.
	// We currently ignore DisplayPos.
	{
		Lin::Matrix4x4 mtx = ComputeOrthoMatrix(draw_data->DisplaySize.x, draw_data->DisplaySize.y, draw->GetDeviceCaps().coordConvention);

		Draw::VsTexColUB ub{};
		memcpy(ub.WorldViewProj, mtx.getReadPtr(), sizeof(Lin::Matrix4x4));
		ub.saturation = 1.0f;
		draw->UpdateDynamicUniformBuffer(&ub, sizeof(ub));
	}
}

// Render function
void ImGui_ImplThin3d_RenderDrawData(ImDrawData* draw_data, Draw::DrawContext *draw) {
	// Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
	int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
	int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
	if (fb_width <= 0 || fb_height <= 0)
		return;

	ImGui_ImplThin3d_Data* bd = ImGui_ImplThin3d_GetBackendData();

	// Setup desired Vulkan state
	ImGui_ImplThin3d_SetupRenderState(draw, draw_data, bd->pipeline, fb_width, fb_height);

	// Will project scissor/clipping rectangles into framebuffer space
	ImVec2 clip_off = draw_data->DisplayPos;         // (0,0) unless using multi-viewports
	ImVec2 clip_scale = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

	_assert_(sizeof(ImDrawIdx) == 2);

	std::vector<Draw::ClippedDraw> draws;
	// Render command lists
	for (int n = 0; n < draw_data->CmdListsCount; n++) {
		const ImDrawList* cmd_list = draw_data->CmdLists[n];
		draws.clear();
		for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
			const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
			if (pcmd->UserCallback != nullptr) {
				// User callback, registered via ImDrawList::AddCallback()
				// (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
				if (pcmd->UserCallback == ImDrawCallback_ResetRenderState) {
					ImGui_ImplThin3d_SetupRenderState(draw, draw_data, bd->pipeline, fb_width, fb_height);
				} else {
					pcmd->UserCallback(cmd_list, pcmd);
				}
			} else {
				// Project scissor/clipping rectangles into framebuffer space
				ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
				ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);

				// Clamp to viewport as vkCmdSetScissor() won't accept values that are off bounds
				if (clip_min.x < 0.0f) { clip_min.x = 0.0f; }
				if (clip_min.y < 0.0f) { clip_min.y = 0.0f; }
				if (clip_max.x > fb_width) { clip_max.x = (float)fb_width; }
				if (clip_max.y > fb_height) { clip_max.y = (float)fb_height; }
				if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
					continue;

				Draw::ClippedDraw draw;
				draw.clipx = clip_min.x;
				draw.clipy = clip_min.y;
				draw.clipw = clip_max.x - clip_min.x;
				draw.cliph = clip_max.y - clip_min.y;
				draw.indexCount = pcmd->ElemCount;
				draw.indexOffset = pcmd->IdxOffset;
				draws.push_back(draw);
			}
		}
		draw->DrawIndexedClippedBatchUP(cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.size(), cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.size(), draws);
	}

	draw->SetScissorRect(0, 0, fb_width, fb_height);
}

bool ImGui_ImplThin3d_CreateDeviceObjects(Draw::DrawContext *draw) {
	ImGui_ImplThin3d_Data* bd = ImGui_ImplThin3d_GetBackendData();

	if (!bd->fontSampler) {
		// Bilinear sampling is required by default. Set 'io.Fonts->Flags |= ImFontAtlasFlags_NoBakedLines' or 'style.AntiAliasedLinesUseTex = false' to allow point/nearest sampling.
		Draw::SamplerStateDesc desc{};
		desc.magFilter = Draw::TextureFilter::LINEAR;
		desc.minFilter = Draw::TextureFilter::LINEAR;
		desc.mipFilter = Draw::TextureFilter::NEAREST;
		desc.wrapU = Draw::TextureAddressMode::REPEAT;
		desc.wrapV = Draw::TextureAddressMode::REPEAT;
		desc.wrapW = Draw::TextureAddressMode::REPEAT;
		desc.maxAniso = 1.0f;
		bd->fontSampler = draw->CreateSamplerState(desc);
	}

	if (!bd->pipeline) {
		ImGui_ImplThin3d_Data* bd = ImGui_ImplThin3d_GetBackendData();

		using namespace Draw;

		static const Draw::InputLayoutDesc ilDesc = {
			sizeof(ImDrawVert),
			{
				{ SEM_POSITION, DataFormat::R32G32_FLOAT, offsetof(ImDrawVert, pos) },
				{ SEM_TEXCOORD0, DataFormat::R32G32_FLOAT, offsetof(ImDrawVert, uv) },
				{ SEM_COLOR0, DataFormat::R8G8B8A8_UNORM, offsetof(ImDrawVert, col) },
			},
		};
		InputLayout *inputLayout = draw->CreateInputLayout(ilDesc);

		BlendState *blend = draw->CreateBlendState({ true, 0xF,
			BlendFactor::SRC_ALPHA, BlendFactor::ONE_MINUS_SRC_ALPHA, BlendOp::ADD,
			BlendFactor::ONE, BlendFactor::ONE_MINUS_SRC_ALPHA, BlendOp::ADD,
			});

		DepthStencilStateDesc dsDesc{};
		DepthStencilState *depthStencil = draw->CreateDepthStencilState(dsDesc);
		RasterState *rasterNoCull = draw->CreateRasterState({});

		ShaderModule *vs_texture_color_2d = draw->GetVshaderPreset(VS_TEXTURE_COLOR_2D);
		ShaderModule *fs_texture_color_2d = draw->GetFshaderPreset(FS_TEXTURE_COLOR_2D);

		PipelineDesc pipelineDesc{
			Primitive::TRIANGLE_LIST,
			{ vs_texture_color_2d, fs_texture_color_2d },
			inputLayout,
			depthStencil,
			blend,
			rasterNoCull,
			&vsTexColBufDesc
		};

		bd->pipeline = draw->CreateGraphicsPipeline(pipelineDesc, "imgui-pipeline");
	}

	if (!bd->fontImage) {
		ImGuiIO& io = ImGui::GetIO();
		ImGui_ImplThin3d_Data* bd = ImGui_ImplThin3d_GetBackendData();

		unsigned char* pixels;
		int width, height;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		size_t upload_size = width * height * 4 * sizeof(char);

		Draw::TextureDesc desc;
		desc.width = width;
		desc.height = height;
		desc.mipLevels = 1;
		desc.format = Draw::DataFormat::R8G8B8A8_UNORM;
		desc.type = Draw::TextureType::LINEAR2D;
		desc.depth = 1;
		desc.tag = "imgui-font";
		desc.initData.push_back((const uint8_t *)pixels);
		bd->fontImage = draw->CreateTexture(desc);
		io.Fonts->SetTexID((ImTextureID)bd->fontImage);
	}

	return true;
}

void ImGui_ImplThin3d_DestroyDeviceObjects() {
	ImGuiIO& io = ImGui::GetIO();
	ImGui_ImplThin3d_Data* bd = ImGui_ImplThin3d_GetBackendData();
	if (bd->fontImage) {
		bd->fontImage->Release();
		bd->fontImage = nullptr;
		io.Fonts->SetTexID(0);
	}
	if (bd->pipeline) {
		bd->pipeline->Release();
		bd->pipeline = nullptr;
	}
	if (bd->fontSampler) {
		bd->fontSampler->Release();
		bd->fontSampler = nullptr;
	}
}

bool ImGui_ImplThin3d_Init(Draw::DrawContext *draw) {
	ImGuiIO& io = ImGui::GetIO();
	IMGUI_CHECKVERSION();
	IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");

	// Setup backend capabilities flags
	ImGui_ImplThin3d_Data* bd = IM_NEW(ImGui_ImplThin3d_Data)();
	io.BackendRendererUserData = (void*)bd;
	io.BackendRendererName = "imgui_impl_thin3d";
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
	ImGui_ImplThin3d_CreateDeviceObjects(draw);
	return true;
}

void ImGui_ImplThin3d_Shutdown() {
	ImGui_ImplThin3d_Data* bd = ImGui_ImplThin3d_GetBackendData();
	IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
	ImGuiIO& io = ImGui::GetIO();

	ImGui_ImplThin3d_DestroyDeviceObjects();
	io.BackendRendererName = nullptr;
	io.BackendRendererUserData = nullptr;
	io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
	IM_DELETE(bd);
}

void ImGui_ImplThin3d_NewFrame(Draw::DrawContext *draw, Lin::Matrix4x4 drawMatrix) {
	ImGui_ImplThin3d_Data* bd = ImGui_ImplThin3d_GetBackendData();
	IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplThin3d_Init()?");

	// This one checks if objects already have been created, so ok to call every time.
	ImGui_ImplThin3d_CreateDeviceObjects(draw);
	g_drawMatrix = drawMatrix;
}

// Register a texture. No-op.
ImTextureID ImGui_ImplThin3d_AddTexture(Draw::Texture *texture) {
	ImGui_ImplThin3d_Data* bd = ImGui_ImplThin3d_GetBackendData();
	return (void *)texture;
}

// Unregister a texture. No-op.
Draw::Texture *ImGui_ImplThin3d_RemoveTexture(ImTextureID tex) {
	return (Draw::Texture *)tex;
}
