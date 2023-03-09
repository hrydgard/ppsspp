// Copyright (c) 2016- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "ppsspp_config.h"

#include <png.h>

#include "GPU/Common/ReplacedTexture.h"
#include "GPU/Common/TextureReplacer.h"

#include "Common/Data/Format/IniFile.h"
#include "Common/Data/Format/ZIMLoad.h"
#include "Common/Data/Format/PNGLoad.h"
#include "Common/Thread/ParallelLoop.h"
#include "Common/Thread/Waitable.h"
#include "Common/Thread/ThreadManager.h"
#include "Common/Log.h"
#include "Common/TimeUtil.h"

class ReplacedTextureTask : public Task {
public:
	ReplacedTextureTask(VFSBackend *vfs, ReplacedTexture &tex, LimitedWaitable *w) : vfs_(vfs), tex_(tex), waitable_(w) {}

	TaskType Type() const override {
		return TaskType::IO_BLOCKING;
	}

	TaskPriority Priority() const override {
		return TaskPriority::NORMAL;
	}

	void Run() override {
		tex_.Prepare(vfs_);
		waitable_->Notify();
	}

private:
	VFSBackend *vfs_;
	ReplacedTexture &tex_;
	LimitedWaitable *waitable_;
};

bool ReplacedTexture::IsReady(double budget) {
	_assert_(vfs_ != nullptr);
	lastUsed_ = time_now_d();
	if (threadWaitable_ && !threadWaitable_->WaitFor(budget)) {
		return false;
	}

	// Loaded already, or not yet on a thread?
	if (initDone_ && levelData_ && !levelData_->data.empty()) {
		// TODO: lock?
		levelData_->lastUsed = lastUsed_;
		return true;
	}

	// Let's not even start a new texture if we're already behind.
	if (budget < 0.0)
		return false;
	if (!prepareDone_)
		return false;

	if (threadWaitable_)
		delete threadWaitable_;
	threadWaitable_ = new LimitedWaitable();
	g_threadManager.EnqueueTask(new ReplacedTextureTask(vfs_, *this, threadWaitable_));
	if (threadWaitable_->WaitFor(budget)) {
		// If we finished all the levels, we're done.
		return initDone_ && levelData_ != nullptr && !levelData_->data.empty();
	}

	// Still pending on thread.
	return false;
}

void ReplacedTexture::Prepare(VFSBackend *vfs) {
	std::unique_lock<std::mutex> lock(mutex_);
	this->vfs_ = vfs;
	if (cancelPrepare_) {
		initDone_ = true;
		return;
	}

	for (int i = 0; i < (int)levels_.size(); ++i) {
		if (cancelPrepare_)
			break;
		PrepareData(i);
	}

	initDone_ = true;
	if (!cancelPrepare_ && threadWaitable_)
		threadWaitable_->Notify();
}

void ReplacedTexture::PrepareData(int level) {
	_assert_msg_((size_t)level < levels_.size(), "Invalid miplevel");
	_assert_msg_(levelData_ != nullptr, "Level cache not set");

	// We must lock around access to levelData_ in case two textures try to load it at once.
	std::lock_guard<std::mutex> guard(levelData_->lock);

	const ReplacedTextureLevel &info = levels_[level];

	if (levelData_->data.size() <= level) {
		levelData_->data.resize(level + 1);
	}

	std::vector<uint8_t> &out = levelData_->data[level];

	// Already populated from cache.
	if (!out.empty())
		return;

	ReplacedImageType imageType;

	size_t fileSize;
	VFSOpenFile *openFile = vfs_->OpenFileForRead(info.fileRef, &fileSize);

	std::string magic;
	imageType = Identify(vfs_, openFile, &magic);

	auto cleanup = [&] {
		vfs_->CloseFile(openFile);
	};

	if (imageType == ReplacedImageType::ZIM) {
		std::unique_ptr<uint8_t[]> zim(new uint8_t[fileSize]);
		if (!zim) {
			ERROR_LOG(G3D, "Failed to allocate memory for texture replacement");
			cleanup();
			return;
		}

		if (vfs_->Read(openFile, &zim[0], fileSize) != fileSize) {
			ERROR_LOG(G3D, "Could not load texture replacement: %s - failed to read ZIM", info.file.c_str());
			cleanup();
			return;
		}

		int w, h, f;
		uint8_t *image;
		if (LoadZIMPtr(&zim[0], fileSize, &w, &h, &f, &image)) {
			if (w > info.w || h > info.h) {
				ERROR_LOG(G3D, "Texture replacement changed since header read: %s", info.file.c_str());
				cleanup();
				return;
			}

			out.resize(info.w * info.h * 4);
			if (w == info.w) {
				memcpy(&out[0], image, info.w * 4 * info.h);
			} else {
				for (int y = 0; y < h; ++y) {
					memcpy(&out[info.w * 4 * y], image + w * 4 * y, w * 4);
				}
			}
			free(image);
		}

		CheckAlphaResult res = CheckAlpha32Rect((u32 *)&out[0], info.w, w, h, 0xFF000000);
		if (res == CHECKALPHA_ANY || level == 0) {
			alphaStatus_ = ReplacedTextureAlpha(res);
		}
	} else if (imageType == ReplacedImageType::PNG) {
		png_image png = {};
		png.version = PNG_IMAGE_VERSION;

		std::string pngdata;
		pngdata.resize(fileSize);
		pngdata.resize(vfs_->Read(openFile, &pngdata[0], fileSize));
		if (!png_image_begin_read_from_memory(&png, &pngdata[0], pngdata.size())) {
			ERROR_LOG(G3D, "Could not load texture replacement info: %s - %s (zip)", info.file.c_str(), png.message);
			cleanup();
			return;
		}
		if (png.width > (uint32_t)info.w || png.height > (uint32_t)info.h) {
			ERROR_LOG(G3D, "Texture replacement changed since header read: %s", info.file.c_str());
			cleanup();
			return;
		}

		bool checkedAlpha = false;
		if ((png.format & PNG_FORMAT_FLAG_ALPHA) == 0) {
			// Well, we know for sure it doesn't have alpha.
			if (level == 0) {
				alphaStatus_ = ReplacedTextureAlpha::FULL;
			}
			checkedAlpha = true;
		}
		png.format = PNG_FORMAT_RGBA;

		out.resize(info.w * info.h * 4);
		if (!png_image_finish_read(&png, nullptr, &out[0], info.w * 4, nullptr)) {
			ERROR_LOG(G3D, "Could not load texture replacement: %s - %s", info.file.c_str(), png.message);
			cleanup();
			out.resize(0);
			return;
		}
		png_image_free(&png);

		if (!checkedAlpha) {
			// This will only check the hashed bits.
			CheckAlphaResult res = CheckAlpha32Rect((u32 *)&out[0], info.w, png.width, png.height, 0xFF000000);
			if (res == CHECKALPHA_ANY || level == 0) {
				alphaStatus_ = ReplacedTextureAlpha(res);
			}
		}
	}

	cleanup();
}

void ReplacedTexture::PurgeIfOlder(double t) {
	if (threadWaitable_ && !threadWaitable_->WaitFor(0.0))
		return;
	if (lastUsed_ >= t)
		return;

	if (levelData_->lastUsed < t) {
		// We have to lock since multiple textures might reference this same data.
		std::lock_guard<std::mutex> guard(levelData_->lock);
		levelData_->data.clear();
		// This means we have to reload.  If we never purge any, there's no need.
		initDone_ = false;
	}
}

ReplacedTexture::~ReplacedTexture() {
	if (threadWaitable_) {
		cancelPrepare_ = true;

		std::unique_lock<std::mutex> lock(mutex_);
		threadWaitable_->WaitAndRelease();
		threadWaitable_ = nullptr;
	}

	for (auto &level : levels_) {
		vfs_->ReleaseFile(level.fileRef);
		level.fileRef = nullptr;
	}
}

bool ReplacedTexture::CopyLevelTo(int level, void *out, int rowPitch) {
	_assert_msg_((size_t)level < levels_.size(), "Invalid miplevel");
	_assert_msg_(out != nullptr && rowPitch > 0, "Invalid out/pitch");

	if (!initDone_) {
		WARN_LOG(G3D, "Init not done yet");
		return false;
	}

	// We probably could avoid this lock, but better to play it safe.
	std::lock_guard<std::mutex> guard(levelData_->lock);

	const ReplacedTextureLevel &info = levels_[level];
	const std::vector<uint8_t> &data = levelData_->data[level];

	if (data.empty()) {
		WARN_LOG(G3D, "Level %d is empty", level);
		return false;
	}

	if (rowPitch < info.w * 4) {
		ERROR_LOG(G3D, "Replacement rowPitch=%d, but w=%d (level=%d)", rowPitch, info.w * 4, level);
		return false;
	}
	_assert_msg_(data.size() == info.w * info.h * 4, "Data has wrong size");

	if (rowPitch == info.w * 4) {
		ParallelMemcpy(&g_threadManager, out, &data[0], info.w * 4 * info.h);
	} else {
		const int MIN_LINES_PER_THREAD = 4;
		ParallelRangeLoop(&g_threadManager, [&](int l, int h) {
			for (int y = l; y < h; ++y) {
				memcpy((uint8_t *)out + rowPitch * y, &data[0] + info.w * 4 * y, info.w * 4);
			}
			}, 0, info.h, MIN_LINES_PER_THREAD);
	}

	return true;
}
