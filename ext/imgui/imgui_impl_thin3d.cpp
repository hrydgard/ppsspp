// dear imgui: Renderer Backend for PPSSPP's thin3d

#include "imgui.h"
#include "imgui_impl_thin3d.h"
#include <cstdio>

#include "Common/System/Display.h"
#include "Common/Math/lin/matrix4x4.h"

static Lin::Matrix4x4 g_drawMatrix;

static ImFont *g_proportionalFont = nullptr;
static ImFont *g_fixedFont = nullptr;

enum class RegisteredTextureType {
	Framebuffer,
	Texture,
	NativeTexture,
};

struct RegisteredTexture {
	RegisteredTextureType type;
	union {
		void *nativeTexture;
		Draw::Texture *texture;
		struct {
			Draw::Framebuffer *framebuffer;
			Draw::Aspect aspect;
		};
	};
	ImGuiPipeline pipeline;
};

struct BackendData {
	Draw::SamplerState *fontSampler = nullptr;
	Draw::Texture *fontImage = nullptr;
	Draw::Pipeline *pipelines[2]{};
	std::vector<RegisteredTexture> tempTextures;
};

#define TEX_ID_OFFSET 256

// Backend data stored in io.BackendRendererUserData to allow support for multiple Dear ImGui contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single Dear ImGui context + multiple windows) instead of multiple Dear ImGui contexts.
// FIXME: multi-context support is not tested and probably dysfunctional in this backend.
static BackendData *ImGui_ImplThin3d_GetBackendData() {
	return ImGui::GetCurrentContext() ? (BackendData *)ImGui::GetIO().BackendRendererUserData : nullptr;
}

// Render function
void ImGui_ImplThin3d_RenderDrawData(ImDrawData* draw_data, Draw::DrawContext *draw) {
	if (!draw_data) {
		// Possible race condition.
		return;
	}
	// Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
	int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
	int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
	if (fb_width <= 0 || fb_height <= 0) {
		return;
	}

	BackendData* bd = ImGui_ImplThin3d_GetBackendData();
	draw->BindSamplerStates(0, 1, &bd->fontSampler);

	// Setup viewport
	Draw::Viewport viewport;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.Width = (float)fb_width;
	viewport.Height = (float)fb_height;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	draw->SetViewport(viewport);

	Lin::Matrix4x4 mtx = ComputeOrthoMatrix(draw_data->DisplaySize.x, draw_data->DisplaySize.y, draw->GetDeviceCaps().coordConvention);

	Draw::VsTexColUB ub{};
	memcpy(ub.WorldViewProj, mtx.getReadPtr(), sizeof(Lin::Matrix4x4));
	ub.saturation = 1.0f;

	// Will project scissor/clipping rectangles into framebuffer space
	ImVec2 clip_off = draw_data->DisplayPos;         // (0,0) unless using multi-viewports
	ImVec2 clip_scale = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

	_assert_(sizeof(ImDrawIdx) == 2);

	ImTextureID prevTexId = (ImTextureID)-1;

	std::vector<Draw::ClippedDraw> draws;
	Draw::Texture *boundTexture = nullptr;
	Draw::Framebuffer *boundFBAsTexture = nullptr;
	void *boundNativeTexture = nullptr;
	Draw::Pipeline *boundPipeline = bd->pipelines[0];
	Draw::SamplerState *boundSampler = bd->fontSampler;
	Draw::Aspect boundAspect = Draw::Aspect::COLOR_BIT;

	// Render command lists
	for (int n = 0; n < draw_data->CmdListsCount; n++) {
		const ImDrawList* cmd_list = draw_data->CmdLists[n];
		draws.clear();
		for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
			const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
			// We don't use the callback mechanism.
			_dbg_assert_(pcmd->UserCallback == nullptr);

			// Update the texture pointers.
			if (!pcmd->TextureId) {
				// Default
				boundTexture = bd->fontImage;
				boundNativeTexture = nullptr;
				boundFBAsTexture = nullptr;
				boundPipeline = bd->pipelines[0];
				boundSampler = bd->fontSampler;
			} else {
				size_t index = (size_t)pcmd->TextureId - TEX_ID_OFFSET;
				if (index >= bd->tempTextures.size()) {
					WARN_LOG(Log::System, "Missing temp texture %d (out of %d)", (int)index, (int)bd->tempTextures.size());
					continue;
				}
				_dbg_assert_(index < bd->tempTextures.size());
				switch (bd->tempTextures[index].type) {
				case RegisteredTextureType::Framebuffer:
					boundFBAsTexture = bd->tempTextures[index].framebuffer;
					boundTexture = nullptr;
					boundNativeTexture = nullptr;
					boundAspect = bd->tempTextures[index].aspect;
					break;
				case RegisteredTextureType::Texture:
					boundTexture = bd->tempTextures[index].texture;
					boundFBAsTexture = nullptr;
					boundNativeTexture = nullptr;
					boundAspect = Draw::Aspect::COLOR_BIT;
					break;
				case RegisteredTextureType::NativeTexture:
					boundTexture = nullptr;
					boundFBAsTexture = nullptr;
					boundNativeTexture = bd->tempTextures[index].nativeTexture;
					boundAspect = Draw::Aspect::COLOR_BIT;
					break;
				}
				boundPipeline = bd->pipelines[(int)bd->tempTextures[index].pipeline];
				boundSampler = bd->fontSampler;
			}

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

			Draw::ClippedDraw clippedDraw;
			clippedDraw.pipeline = boundPipeline;
			clippedDraw.bindTexture = boundTexture;
			clippedDraw.bindFramebufferAsTex = boundFBAsTexture;
			clippedDraw.bindNativeTexture = boundNativeTexture;
			clippedDraw.samplerState = boundSampler;
			clippedDraw.aspect = boundAspect;
			clippedDraw.clipx = clip_min.x;
			clippedDraw.clipy = clip_min.y;
			clippedDraw.clipw = clip_max.x - clip_min.x;
			clippedDraw.cliph = clip_max.y - clip_min.y;
			clippedDraw.indexCount = pcmd->ElemCount;
			clippedDraw.indexOffset = pcmd->IdxOffset;
			draws.push_back(clippedDraw);
		}
		draw->DrawIndexedClippedBatchUP(cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.size(), cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.size(), draws, &ub, sizeof(ub));
	}

	draw->SetScissorRect(0, 0, fb_width, fb_height);

	// Discard temp textures.
	bd->tempTextures.clear();
}

bool ImGui_ImplThin3d_CreateDeviceObjects(Draw::DrawContext *draw) {
	BackendData* bd = ImGui_ImplThin3d_GetBackendData();

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

	if (!bd->pipelines[0]) {
		BackendData* bd = ImGui_ImplThin3d_GetBackendData();

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
		BlendState *blendOpaque = draw->CreateBlendState({ false, 0xF });

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

		bd->pipelines[0] = draw->CreateGraphicsPipeline(pipelineDesc, "imgui-pipeline");
		pipelineDesc.blend = blendOpaque;
		bd->pipelines[1] = draw->CreateGraphicsPipeline(pipelineDesc, "imgui-pipeline-opaque");

		inputLayout->Release();
		blend->Release();
		blendOpaque->Release();
		depthStencil->Release();
		rasterNoCull->Release();
	}

	if (!bd->fontImage) {
		ImGuiIO& io = ImGui::GetIO();
		BackendData* bd = ImGui_ImplThin3d_GetBackendData();

		unsigned char* pixels;
		int width, height;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		size_t upload_size = width * height * 4 * sizeof(char);

		Draw::TextureDesc desc{};
		desc.width = width;
		desc.height = height;
		desc.mipLevels = 1;
		desc.format = Draw::DataFormat::R8G8B8A8_UNORM;
		desc.type = Draw::TextureType::LINEAR2D;
		desc.swizzle = Draw::TextureSwizzle::DEFAULT;
		desc.depth = 1;
		desc.tag = "imgui-font";
		desc.initData.push_back((const uint8_t *)pixels);
		bd->fontImage = draw->CreateTexture(desc);
		io.Fonts->SetTexID(0);
	}

	return true;
}

void ImGui_ImplThin3d_DestroyDeviceObjects() {
	ImGuiIO& io = ImGui::GetIO();
	BackendData* bd = ImGui_ImplThin3d_GetBackendData();
	if (bd->fontImage) {
		bd->fontImage->Release();
		bd->fontImage = nullptr;
		io.Fonts->SetTexID(0);
	}
	for (int i = 0; i < ARRAY_SIZE(bd->pipelines); i++) {
		if (bd->pipelines[i]) {
			bd->pipelines[i]->Release();
			bd->pipelines[i] = nullptr;
		}
	}
	if (bd->fontSampler) {
		bd->fontSampler->Release();
		bd->fontSampler = nullptr;
	}
}

bool ImGui_ImplThin3d_Init(Draw::DrawContext *draw, const uint8_t *ttf_font, size_t size) {
	ImGuiIO& io = ImGui::GetIO();
	if (ttf_font) {
		g_proportionalFont = io.Fonts->AddFontFromMemoryTTF((void *)ttf_font, (int)size, 21.0f / g_display.dpi_scale, nullptr, io.Fonts->GetGlyphRangesDefault());
	} else {
		// fallback
		g_proportionalFont = g_fixedFont;
	}
	g_fixedFont = io.Fonts->AddFontDefault();
	ImGui::GetStyle().ScaleAllSizes(1.0f / g_display.dpi_scale);
	ImGui::GetStyle().Colors[ImGuiCol_Border] = ImColor(IM_COL32(0x2A, 0x2F, 0x3B, 0xFF));

	IMGUI_CHECKVERSION();
	IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");

	// Setup backend capabilities flags
	BackendData* bd = IM_NEW(BackendData)();
	io.BackendRendererUserData = (void*)bd;
	io.BackendRendererName = "imgui_impl_thin3d";
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
	ImGui_ImplThin3d_CreateDeviceObjects(draw);
	return true;
}

void ImGui_PushFixedFont() {
	ImGui::PushFont(g_fixedFont);
}

void ImGui_PopFont() {
	ImGui::PopFont();
}

ImFont *ImGui_GetFixedFont() {
	return g_fixedFont;
}

void ImGui_ImplThin3d_Shutdown() {
	BackendData* bd = ImGui_ImplThin3d_GetBackendData();
	IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
	ImGuiIO& io = ImGui::GetIO();

	ImGui_ImplThin3d_DestroyDeviceObjects();
	io.BackendRendererName = nullptr;
	io.BackendRendererUserData = nullptr;
	io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
	IM_DELETE(bd);
}

void ImGui_ImplThin3d_NewFrame(Draw::DrawContext *draw, Lin::Matrix4x4 drawMatrix) {
	BackendData* bd = ImGui_ImplThin3d_GetBackendData();
	IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplThin3d_Init()?");

	// This one checks if objects already have been created, so ok to call every time.
	ImGui_ImplThin3d_CreateDeviceObjects(draw);
	g_drawMatrix = drawMatrix;
}

ImTextureID ImGui_ImplThin3d_AddNativeTextureTemp(void *texture, ImGuiPipeline pipeline) {
	BackendData* bd = ImGui_ImplThin3d_GetBackendData();

	RegisteredTexture tex{ RegisteredTextureType::NativeTexture};
	tex.nativeTexture = texture;
	tex.pipeline = pipeline;

	bd->tempTextures.push_back(tex);
	return (ImTextureID)(uint64_t)(TEX_ID_OFFSET + bd->tempTextures.size() - 1);
}

ImTextureID ImGui_ImplThin3d_AddTextureTemp(Draw::Texture *texture, ImGuiPipeline pipeline) {
	BackendData* bd = ImGui_ImplThin3d_GetBackendData();

	RegisteredTexture tex{ RegisteredTextureType::Texture };
	tex.texture = texture;
	tex.pipeline = pipeline;

	bd->tempTextures.push_back(tex);
	return (ImTextureID)(uint64_t)(TEX_ID_OFFSET + bd->tempTextures.size() - 1);
}

ImTextureID ImGui_ImplThin3d_AddFBAsTextureTemp(Draw::Framebuffer *framebuffer, Draw::Aspect aspect, ImGuiPipeline pipeline) {
	BackendData* bd = ImGui_ImplThin3d_GetBackendData();

	RegisteredTexture tex{ RegisteredTextureType::Framebuffer };
	tex.framebuffer = framebuffer;
	tex.aspect = aspect;
	tex.pipeline = pipeline;

	bd->tempTextures.push_back(tex);
	return (ImTextureID)(uint64_t)(TEX_ID_OFFSET + bd->tempTextures.size() - 1);
}
