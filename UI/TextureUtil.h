#pragma once

#include "thin3d/thin3d.h"
#include "Core/Config.h"
#include "gfx/gl_lost_manager.h"

enum ImageFileType {
	PNG,
	JPEG,
	ZIM,
	DETECT,
	TYPE_UNKNOWN,
};

class ManagedTexture : public GfxResourceHolder {
public:
	ManagedTexture(Draw::DrawContext *draw) : draw_(draw), texture_(nullptr) {
		if (g_Config.iGPUBackend == (int)GPUBackend::OPENGL)
			register_gl_resource_holder(this, "managed_texture", 0);
	}
	~ManagedTexture() {
		if (texture_)
			texture_->Release();
		if (g_Config.iGPUBackend == (int)GPUBackend::OPENGL)
			unregister_gl_resource_holder(this);
	}
	void GLLost() override {
		texture_->Release();
		texture_ = nullptr;
	}
	void GLRestore() override {
		if (!filename_.empty()) {
			if (LoadFromFile(filename_.c_str())) {
				ILOG("Reloaded lost texture %s", filename_.c_str());
			}
			else {
				ELOG("Failed to reload lost texture %s", filename_.c_str());
			}
		}
		else {
			WLOG("Texture cannot be restored - has no filename");
		}
	}

	bool LoadFromFile(const std::string &filename, ImageFileType type = ImageFileType::DETECT, bool generateMips = false);
	bool LoadFromFileData(const uint8_t *data, size_t dataSize, ImageFileType type = ImageFileType::DETECT, bool generateMips = false);
	Draw::Texture *GetTexture() { return texture_; }  // For immediate use, don't store.
	int Width() const { return texture_->Width(); }
	int Height() const { return texture_->Height(); }

private:
	Draw::Texture *texture_;
	Draw::DrawContext *draw_;
	std::string filename_;  // Textures that are loaded from files can reload themselves automatically.
};

ManagedTexture *CreateTextureFromFile(Draw::DrawContext *draw, const char *filename, ImageFileType fileType, bool generateMips = false);
ManagedTexture *CreateTextureFromFileData(Draw::DrawContext *draw, const uint8_t *data, int size, ImageFileType fileType, bool generateMips = false);
