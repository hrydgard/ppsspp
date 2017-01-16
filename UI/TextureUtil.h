#pragma once

#include "thin3d/thin3d.h"
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
		register_gl_resource_holder(this);
	}
	~ManagedTexture() {
		unregister_gl_resource_holder(this);
	}
	void GLLost() override {
		delete texture_;
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

	bool LoadFromFile(const std::string &filename, ImageFileType type = ImageFileType::DETECT);
	bool LoadFromFileData(const uint8_t *data, size_t dataSize, ImageFileType type = ImageFileType::DETECT);
	Draw::Texture *GetTexture() { return texture_; }  // For immediate use, don't store.
	int Width() const { return texture_->Width(); }
	int Height() const { return texture_->Height(); }

private:
	Draw::Texture *texture_;
	Draw::DrawContext *draw_;
	std::string filename_;  // Textures that are loaded from files can reload themselves automatically.
};

// Common Thin3D function, uses CreateTexture
ManagedTexture *CreateTextureFromFile(Draw::DrawContext *draw, const char *filename, ImageFileType fileType);
ManagedTexture *CreateTextureFromFileData(Draw::DrawContext *draw, const uint8_t *data, int size, ImageFileType fileType);
