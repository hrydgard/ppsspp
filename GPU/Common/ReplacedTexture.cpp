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

#include <algorithm>

#include "ppsspp_config.h"

#include <png.h>

#include "ext/basis_universal/basisu_transcoder.h"
#include "ext/basis_universal/basisu_file_headers.h"

#include "GPU/Common/ReplacedTexture.h"
#include "GPU/Common/TextureReplacer.h"

#include "Common/Data/Format/DDSLoad.h"
#include "Common/Data/Format/ZIMLoad.h"
#include "Common/Data/Format/PNGLoad.h"
#include "Common/Thread/ParallelLoop.h"
#include "Common/Thread/Waitable.h"
#include "Common/Thread/ThreadManager.h"
#include "Common/Log.h"
#include "Common/TimeUtil.h"

#define MK_FOURCC(str) (str[0] | ((uint8_t)str[1] << 8) | ((uint8_t)str[2] << 16) | ((uint8_t)str[3] << 24))

static ReplacedImageType IdentifyMagic(const uint8_t magic[4]) {
	if (memcmp((const char *)magic, "ZIMG", 4) == 0)
		return ReplacedImageType::ZIM;
	else if (magic[0] == 0x89 && strncmp((const char *)&magic[1], "PNG", 3) == 0)
		return ReplacedImageType::PNG;
	else if (memcmp((const char *)magic, "DDS ", 4) == 0)
		return ReplacedImageType::DDS;
	else if (magic[0] == 's' && magic[1] == 'B') {
		uint16_t ver = magic[2] | (magic[3] << 8);
		if (ver >= 0x10) {
			return ReplacedImageType::BASIS;
		}
	} else if (memcmp((const char *)magic, "\xabKTX", 4) == 0) {
		// Technically, should read 12 bytes here, but this'll do.
		return ReplacedImageType::KTX2;
	}
	return ReplacedImageType::INVALID;
}

static ReplacedImageType Identify(VFSBackend *vfs, VFSOpenFile *openFile, std::string *outMagic) {
	uint8_t magic[4];
	if (vfs->Read(openFile, magic, 4) != 4) {
		*outMagic = "FAIL";
		return ReplacedImageType::INVALID;
	}
	// Turn the signature into a readable string that we can display in an error message.
	*outMagic = std::string((const char *)magic, 4);
	for (int i = 0; i < outMagic->size(); i++) {
		if ((s8)(*outMagic)[i] < 32) {
			(*outMagic)[i] = '_';
		}
	}
	vfs->Rewind(openFile);
	return IdentifyMagic(magic);
}

class ReplacedTextureTask : public Task {
public:
	ReplacedTextureTask(VFSBackend *vfs, ReplacedTexture &tex, LimitedWaitable *w) : vfs_(vfs), tex_(tex), waitable_(w) {}

	TaskType Type() const override { return TaskType::IO_BLOCKING; }
	TaskPriority Priority() const override { return TaskPriority::NORMAL; }

	void Run() override {
		tex_.Prepare(vfs_);
		waitable_->Notify();
	}

private:
	VFSBackend *vfs_;
	ReplacedTexture &tex_;
	LimitedWaitable *waitable_;
};

ReplacedTexture::ReplacedTexture(VFSBackend *vfs, const ReplacementDesc &desc) : vfs_(vfs), desc_(desc) {
	logId_ = desc.logId;
}

ReplacedTexture::~ReplacedTexture() {
	if (threadWaitable_) {
		SetState(ReplacementState::CANCEL_INIT);

		threadWaitable_->WaitAndRelease();
		threadWaitable_ = nullptr;
	}

	for (auto &level : levels_) {
		vfs_->ReleaseFile(level.fileRef);
		level.fileRef = nullptr;
	}
}

void ReplacedTexture::PurgeIfNotUsedSinceTime(double t) {
	if (State() != ReplacementState::ACTIVE) {
		return;
	}

	// If there's some leftover threadWaitable, get rid of it.
	if (threadWaitable_) {
		if (threadWaitable_->WaitFor(0.0)) {
			delete threadWaitable_;
			threadWaitable_ = nullptr;
			// Continue with purging.
		} else {
			// Try next time.
			return;
		}
	}

	// This is the only place except shutdown where a texture can transition
	// from ACTIVE to anything else, so we don't actually need to lock here.
	if (lastUsed_ >= t) {
		return;
	}

	data_.clear();
	levels_.clear();
	fmt = Draw::DataFormat::UNDEFINED;
	alphaStatus_ = ReplacedTextureAlpha::UNKNOWN;

	// This means we have to reload.  If we never purge any, there's no need.
	SetState(ReplacementState::UNLOADED);
}

// This can only return true if ACTIVE or NOT_FOUND.
bool ReplacedTexture::Poll(double budget) {
	_assert_(vfs_ != nullptr);

	double now = time_now_d();

	switch (State()) {
	case ReplacementState::ACTIVE:
	case ReplacementState::NOT_FOUND:
		if (threadWaitable_) {
			if (!threadWaitable_->WaitFor(budget)) {
				lastUsed_ = now;
				return false;
			}
			// Successfully waited! Can get rid of it.
			threadWaitable_->WaitAndRelease();
			threadWaitable_ = nullptr;
			lastUsed = now;
		}
		lastUsed_ = now;
		return true;
	case ReplacementState::CANCEL_INIT:
	case ReplacementState::PENDING:
		return false;
	case ReplacementState::UNLOADED:
		// We're gonna need to spawn a task.
		break;
	default:
		break;
	}

	lastUsed_ = now;

	// Let's not even start a new texture if we're already behind.
	// Note that 0.0 is used as a signalling value that we don't want to wait (just handling already finished textures).
	if (budget < 0.0)
		return false;

	_assert_(!threadWaitable_);
	threadWaitable_ = new LimitedWaitable();
	SetState(ReplacementState::PENDING);
	g_threadManager.EnqueueTask(new ReplacedTextureTask(vfs_, *this, threadWaitable_));
	if (threadWaitable_->WaitFor(budget)) {
		// If we successfully wait here, we're done. The thread will set state accordingly.
		_assert_(State() == ReplacementState::ACTIVE || State() == ReplacementState::NOT_FOUND || State() == ReplacementState::CANCEL_INIT);
		delete threadWaitable_;
		threadWaitable_ = nullptr;
		return true;
	}
	// Still pending on thread.
	return false;
}

inline uint32_t RoundUpTo4(uint32_t value) {
	return (value + 3) & ~3;
}

void ReplacedTexture::Prepare(VFSBackend *vfs) {
	_assert_(vfs != nullptr);

	this->vfs_ = vfs;

	std::unique_lock<std::mutex> lock(lock_);

	fmt = Draw::DataFormat::UNDEFINED;

	Draw::DataFormat pixelFormat;
	LoadLevelResult result = LoadLevelResult::LOAD_ERROR;
	if (desc_.filenames.empty()) {
		result = LoadLevelResult::DONE;
	}
	for (int i = 0; i < std::min(MAX_REPLACEMENT_MIP_LEVELS, (int)desc_.filenames.size()); ++i) {
		if (State() == ReplacementState::CANCEL_INIT) {
			break;
		}

		if (desc_.filenames[i].empty()) {
			// Out of valid mip levels.  Bail out.
			break;
		}

		std::string path(desc_.filenames[i]);
		VFSFileReference *fileRef = vfs_->GetFile(path.c_str());
		if (!fileRef) {
			if (i == 0) {
				INFO_LOG(Log::TexReplacement, "Texture replacement file '%s' not found in %s", desc_.filenames[i].c_str(), vfs_->toString().c_str());
				// No file at all. Mark as NOT_FOUND.
				SetState(ReplacementState::NOT_FOUND);
				return;
			}
			// If the file doesn't exist, let's just bail immediately here.
			// Mark as DONE, not error.
			result = LoadLevelResult::DONE;
			break;
		}

		if (i == 0) {
			fmt = Draw::DataFormat::R8G8B8A8_UNORM;
		}

		result = LoadLevelData(fileRef, desc_.filenames[i], i, &pixelFormat);
		if (result == LoadLevelResult::DONE) {
			// Loaded all the levels we're gonna get.
			fmt = pixelFormat;
			break;
		} else if (result == LoadLevelResult::CONTINUE) {
			if (i == 0) {
				fmt = pixelFormat;
			} else {
				if (fmt != pixelFormat) {
					ERROR_LOG(Log::TexReplacement, "Replacement mipmap %d doesn't have the same pixel format as mipmap 0. Stopping.", i);
					break;
				}
			}
		} else {
			// Error state.
			break;
		}
	}

	if (levels_.empty()) {
		// No replacement found.
		std::string name = TextureReplacer::HashName(desc_.cachekey, desc_.hash, 0);
		if (result == LoadLevelResult::LOAD_ERROR) {
			WARN_LOG(Log::TexReplacement, "Failed to load replacement texture '%s'", name.c_str());
		}
		SetState(ReplacementState::NOT_FOUND);
		return;
	}

	// Update the level dimensions.
	for (auto &level : levels_) {
		level.fullW = (level.w * desc_.w) / desc_.newW;
		level.fullH = (level.h * desc_.h) / desc_.newH;

		int blockSize;
		bool bc = Draw::DataFormatIsBlockCompressed(fmt, &blockSize);
		if (!bc) {
			level.fullDataSize = level.fullW * level.fullH * 4;
		} else {
			level.fullDataSize = RoundUpTo4(level.fullW) * RoundUpTo4(level.fullH) * blockSize / 16;
		}
	}

	SetState(ReplacementState::ACTIVE);

	// the caller calls threadWaitable->notify().
}

// Returns true if Prepare should keep calling this to load more levels.
ReplacedTexture::LoadLevelResult ReplacedTexture::LoadLevelData(VFSFileReference *fileRef, const std::string &filename, int mipLevel, Draw::DataFormat *pixelFormat) {
	bool good = false;

	if (data_.size() <= mipLevel) {
		data_.resize(mipLevel + 1);
	}

	if (!vfs_) {
		ERROR_LOG(Log::TexReplacement, "Unexpected null vfs_ pointer in LoadLevelData");
		return LoadLevelResult::LOAD_ERROR;
	}

	ReplacedTextureLevel level;
	size_t fileSize;
	VFSOpenFile *openFile = vfs_->OpenFileForRead(fileRef, &fileSize);
	if (!openFile) {
		// File missing, no more levels. This is alright.
		return LoadLevelResult::DONE;
	}

	std::string magic;
	ReplacedImageType imageType = Identify(vfs_, openFile, &magic);

	bool ddsDX10 = false;
	int numMips = 1;

	if (imageType == ReplacedImageType::KTX2) {
		KTXHeader header;
		good = vfs_->Read(openFile, &header, sizeof(header)) == sizeof(header);

		level.w = header.pixelWidth;
		level.h = header.pixelHeight;
		numMips = header.levelCount;

		// Additional quick checks
		good = good && header.layerCount <= 1;
	} else if (imageType == ReplacedImageType::BASIS) {
		WARN_LOG(Log::TexReplacement, "The basis texture format is not supported. Use KTX2 (basisu texture.png -uastc -ktx2 -mipmap)");

		// We simply don't support basis files currently.
		good = false;
	} else if (imageType == ReplacedImageType::DDS) {
		DDSHeader header;
		DDSHeaderDXT10 header10{};
		good = vfs_->Read(openFile, &header, sizeof(header)) == sizeof(header);

		*pixelFormat = Draw::DataFormat::UNDEFINED;
		u32 format;
		if (good && (header.ddspf.dwFlags & DDPF_FOURCC)) {
			char *fcc = (char *)&header.ddspf.dwFourCC;
			// INFO_LOG(Log::TexReplacement, "DDS fourcc: %c%c%c%c", fcc[0], fcc[1], fcc[2], fcc[3]);
			if (header.ddspf.dwFourCC == MK_FOURCC("DX10")) {
				ddsDX10 = true;
				good = good && vfs_->Read(openFile, &header10, sizeof(header10)) == sizeof(header10);
				format = header10.dxgiFormat;
				switch (format) {
				case 71: // DXGI_FORMAT_BC1_UNORM
				case 72: // DXGI_FORMAT_BC1_UNORM_SRGB
					if (!desc_.formatSupport.bc123) {
						WARN_LOG(Log::TexReplacement, "BC1 format not supported, skipping texture");
						good = false;
					}
					*pixelFormat = Draw::DataFormat::BC1_RGBA_UNORM_BLOCK;
					break;
				case 74: // DXGI_FORMAT_BC2_UNORM
				case 75: // DXGI_FORMAT_BC2_UNORM_SRGB
					if (!desc_.formatSupport.bc123) {
						WARN_LOG(Log::TexReplacement, "BC2 format not supported, skipping texture");
						good = false;
					}
					*pixelFormat = Draw::DataFormat::BC2_UNORM_BLOCK;
					break;
				case 77: // DXGI_FORMAT_BC3_UNORM
				case 78: // DXGI_FORMAT_BC3_UNORM_SRGB
					if (!desc_.formatSupport.bc123) {
						WARN_LOG(Log::TexReplacement, "BC3 format not supported, skipping texture");
						good = false;
					}
					*pixelFormat = Draw::DataFormat::BC3_UNORM_BLOCK;
					break;
				case 98: // DXGI_FORMAT_BC7_UNORM:
				case 99: // DXGI_FORMAT_BC7_UNORM_SRGB:
					if (!desc_.formatSupport.bc7) {
						WARN_LOG(Log::TexReplacement, "BC7 format not supported, skipping texture");
						good = false;
					}
					*pixelFormat = Draw::DataFormat::BC7_UNORM_BLOCK;
					break;
				default:
					WARN_LOG(Log::TexReplacement, "DXGI pixel format %d not supported.", header10.dxgiFormat);
					good = false;
				}
			} else {
				if (!desc_.formatSupport.bc123) {
					WARN_LOG(Log::TexReplacement, "BC1-3 formats not supported");
					good = false;
				}
				format = header.ddspf.dwFourCC;
				// OK, there are a number of possible formats we might have ended up with. We choose just a few
				// to support for now.
				switch (format) {
				case MK_FOURCC("DXT1"):
					*pixelFormat = Draw::DataFormat::BC1_RGBA_UNORM_BLOCK;
					break;
				case MK_FOURCC("DXT3"):
					*pixelFormat = Draw::DataFormat::BC2_UNORM_BLOCK;
					break;
				case MK_FOURCC("DXT5"):
					*pixelFormat = Draw::DataFormat::BC3_UNORM_BLOCK;
					break;
				default:
					ERROR_LOG(Log::TexReplacement, "DDS pixel format not supported.");
					good = false;
				}
			}
		} else if (good) {
			ERROR_LOG(Log::TexReplacement, "DDS non-fourCC format not supported.");
			good = false;
		}

		level.w = header.dwWidth;
		level.h = header.dwHeight;
		numMips = header.dwMipMapCount;
	} else if (imageType == ReplacedImageType::ZIM) {
		uint32_t ignore = 0;
		struct ZimHeader {
			uint32_t magic;
			uint32_t w;
			uint32_t h;
			uint32_t flags;
		} header;
		good = vfs_->Read(openFile, &header, sizeof(header)) == sizeof(header);
		level.w = header.w;
		level.h = header.h;
		good = good && (header.flags & ZIM_FORMAT_MASK) == ZIM_RGBA8888;
		*pixelFormat = Draw::DataFormat::R8G8B8A8_UNORM;
	} else if (imageType == ReplacedImageType::PNG) {
		PNGHeaderPeek headerPeek;
		good = vfs_->Read(openFile, &headerPeek, sizeof(headerPeek)) == sizeof(headerPeek);
		if (good && headerPeek.IsValidPNGHeader()) {
			level.w = headerPeek.Width();
			level.h = headerPeek.Height();
			good = true;
		} else {
			ERROR_LOG(Log::TexReplacement, "Could not get PNG dimensions: %s (zip)", filename.c_str());
			good = false;
		}
		*pixelFormat = Draw::DataFormat::R8G8B8A8_UNORM;
	} else {
		ERROR_LOG(Log::TexReplacement, "Could not load texture replacement info: %s - unsupported format %s", filename.c_str(), magic.c_str());
	}

	// TODO: We no longer really need to have a split in this function, the upper and lower parts can be merged now.

	if (good && mipLevel != 0) {
		// If loading a low mip directly (through png most likely), check that the mipmap size is correct.
		// Can't load mips of the wrong size.
		if (level.w != std::max(1, (levels_[0].w >> mipLevel)) || level.h != std::max(1, (levels_[0].h >> mipLevel))) {
			WARN_LOG(Log::TexReplacement, "Replacement mipmap invalid: size=%dx%d, expected=%dx%d (level %d)",
				level.w, level.h, levels_[0].w >> mipLevel, levels_[0].h >> mipLevel, mipLevel);
			good = false;
		}
	}

	if (!good) {
		vfs_->CloseFile(openFile);
		return LoadLevelResult::LOAD_ERROR;
	}

	vfs_->Rewind(openFile);

	level.fileRef = fileRef;

	if (imageType == ReplacedImageType::KTX2) {
		// Just slurp the whole file in one go and feed to the decoder.
		std::vector<uint8_t> buffer;
		buffer.resize(fileSize);
		buffer.resize(vfs_->Read(openFile, &buffer[0], buffer.size()));
		vfs_->CloseFile(openFile);

		basist::ktx2_transcoder transcoder;
		if (!transcoder.init(buffer.data(), (int)buffer.size())) {
			WARN_LOG(Log::TexReplacement, "Error reading KTX file");
			return LoadLevelResult::LOAD_ERROR;
		}

		// Figure out the target format.
		basist::transcoder_texture_format transcoderFormat;
		if (transcoder.is_etc1s()) {
			// We only support opaque colors with this compression method.
			alphaStatus_ = ReplacedTextureAlpha::FULL;
			// Let's pick a suitable compatible format.
			if (desc_.formatSupport.bc123) {
				transcoderFormat = basist::transcoder_texture_format::cTFBC1;
				*pixelFormat = Draw::DataFormat::BC1_RGBA_UNORM_BLOCK;
			} else if (desc_.formatSupport.etc2) {
				transcoderFormat = basist::transcoder_texture_format::cTFETC1_RGB;
				*pixelFormat = Draw::DataFormat::ETC2_R8G8B8_UNORM_BLOCK;
			} else {
				// Transcode to RGBA8 instead as a fallback. A bit slow and takes a lot of memory, but better than nothing.
				WARN_LOG(Log::TexReplacement, "Replacement texture format not supported - transcoding to RGBA8888");
				transcoderFormat = basist::transcoder_texture_format::cTFRGBA32;
				*pixelFormat = Draw::DataFormat::R8G8B8A8_UNORM;
			}
		} else if (transcoder.is_uastc()) {
			// TODO: Try to recover some indication of alpha from the actual data blocks.
			alphaStatus_ = ReplacedTextureAlpha::UNKNOWN;
			// Let's pick a suitable compatible format.
			if (desc_.formatSupport.bc7) {
				transcoderFormat = basist::transcoder_texture_format::cTFBC7_RGBA;
				*pixelFormat = Draw::DataFormat::BC7_UNORM_BLOCK;
			} else if (desc_.formatSupport.astc) {
				transcoderFormat = basist::transcoder_texture_format::cTFASTC_4x4_RGBA;
				*pixelFormat = Draw::DataFormat::ASTC_4x4_UNORM_BLOCK;
			} else {
				// Transcode to RGBA8 instead as a fallback. A bit slow and takes a lot of memory, but better than nothing.
				WARN_LOG(Log::TexReplacement, "Replacement texture format not supported - transcoding to RGBA8888");
				transcoderFormat = basist::transcoder_texture_format::cTFRGBA32;
				*pixelFormat = Draw::DataFormat::R8G8B8A8_UNORM;
			}
		} else {
			WARN_LOG(Log::TexReplacement, "PPSSPP currently only supports KTX for basis/UASTC textures. This may change in the future.");
			return LoadLevelResult::LOAD_ERROR;
		}

		int blockSize = 0;
		bool bc = Draw::DataFormatIsBlockCompressed(*pixelFormat, &blockSize);
		_dbg_assert_(bc || *pixelFormat == Draw::DataFormat::R8G8B8A8_UNORM);

		if (bc && ((level.w & 3) != 0 || (level.h & 3) != 0)) {
			WARN_LOG(Log::TexReplacement, "Block compressed replacement texture '%s' not divisible by 4x4 (%dx%d). In D3D11 (only!) we will have to expand (potentially causing glitches).", filename.c_str(), level.w, level.h);
		}

		data_.resize(numMips);

		basist::ktx2_transcoder_state transcodeState;  // Each thread needs one of these.

		transcoder.start_transcoding();
		levels_.reserve(numMips);
		for (int i = 0; i < numMips; i++) {
			std::vector<uint8_t> &out = data_[mipLevel + i];

			basist::ktx2_image_level_info levelInfo{};
			bool result = transcoder.get_image_level_info(levelInfo, i, 0, 0);
			_dbg_assert_(result);

			size_t dataSizeBytes = levelInfo.m_total_blocks * blockSize;
			size_t outputSize = levelInfo.m_total_blocks;
			size_t outputPitch = levelInfo.m_num_blocks_x;
			// Support transcoded-to-RGBA8888 images too.
			if (!bc) {
				dataSizeBytes = levelInfo.m_orig_width * levelInfo.m_orig_height * 4;
				outputSize = levelInfo.m_orig_width * levelInfo.m_orig_height;
				outputPitch = levelInfo.m_orig_width;
			}
			data_[i].resize(dataSizeBytes);

			transcodeState.clear();
			transcoder.transcode_image_level(i, 0, 0, &out[0], (uint32_t)outputSize, transcoderFormat, 0, (uint32_t)outputPitch, level.h, -1, -1, &transcodeState);
			level.w = levelInfo.m_orig_width;
			level.h = levelInfo.m_orig_height;
			if (i != 0)
				level.fileRef = nullptr;
			levels_.push_back(level);
		}
		transcoder.clear();

		return LoadLevelResult::DONE;  // don't read more levels
	} else if (imageType == ReplacedImageType::DDS) {
		// TODO: Do better with alphaStatus, it's possible.
		alphaStatus_ = ReplacedTextureAlpha::UNKNOWN;

		DDSHeader header;
		DDSHeaderDXT10 header10{};
		vfs_->Read(openFile, &header, sizeof(header));
		if (ddsDX10) {
			vfs_->Read(openFile, &header10, sizeof(header10));
		}

		int blockSize = 0;
		bool bc = Draw::DataFormatIsBlockCompressed(*pixelFormat, &blockSize);
		_dbg_assert_(bc);

		if (bc && ((level.w & 3) != 0 || (level.h & 3) != 0)) {
			WARN_LOG(Log::TexReplacement, "Block compressed replacement texture '%s' not divisible by 4x4 (%dx%d). In D3D11 (only!) we will have to expand (potentially causing glitches).", filename.c_str(), level.w, level.h);
		}

		data_.resize(numMips);

		// A DDS File can contain multiple mipmaps.
		levels_.reserve(numMips);
		for (int i = 0; i < numMips; i++) {
			std::vector<uint8_t> &out = data_[mipLevel + i];

			int bytesToRead = RoundUpTo4(level.w) * RoundUpTo4(level.h) * blockSize / 16;
			out.resize(bytesToRead);

			size_t read_bytes = vfs_->Read(openFile, &out[0], bytesToRead);
			if (read_bytes != bytesToRead) {
				WARN_LOG(Log::TexReplacement, "DDS: Expected %d bytes, got %d", bytesToRead, (int)read_bytes);
			}

			levels_.push_back(level);
			level.w = std::max(level.w / 2, 1);
			level.h = std::max(level.h / 2, 1);
			if (i != 0)
				level.fileRef = nullptr;  // We only provide a fileref on level 0 if we have mipmaps.
		}
		vfs_->CloseFile(openFile);

		return LoadLevelResult::DONE;  // don't read more levels

	} else if (imageType == ReplacedImageType::ZIM) {

		auto zim = std::make_unique<uint8_t[]>(fileSize);
		if (!zim) {
			ERROR_LOG(Log::TexReplacement, "Failed to allocate memory for texture replacement");
			vfs_->CloseFile(openFile);
			return LoadLevelResult::LOAD_ERROR;
		}

		if (vfs_->Read(openFile, &zim[0], fileSize) != fileSize) {
			ERROR_LOG(Log::TexReplacement, "Could not load texture replacement: %s - failed to read ZIM", filename.c_str());
			vfs_->CloseFile(openFile);
			return LoadLevelResult::LOAD_ERROR;
		}
		vfs_->CloseFile(openFile);

		int w, h, f;
		uint8_t *image;
		std::vector<uint8_t> &out = data_[mipLevel];
		// TODO: Zim files can actually hold mipmaps (although no tool has ever been made to create them :P)
		if (LoadZIMPtr(&zim[0], fileSize, &w, &h, &f, &image)) {
			if (w > level.w || h > level.h) {
				ERROR_LOG(Log::TexReplacement, "Texture replacement changed since header read: %s", filename.c_str());
				return LoadLevelResult::LOAD_ERROR;
			}

			out.resize(level.w * level.h * 4);
			if (w == level.w) {
				memcpy(&out[0], image, level.w * 4 * level.h);
			} else {
				for (int y = 0; y < h; ++y) {
					memcpy(&out[level.w * 4 * y], image + w * 4 * y, w * 4);
				}
			}
			free(image);

			CheckAlphaResult res = CheckAlpha32Rect((u32 *)&out[0], level.w, w, h, 0xFF000000);
			if (res == CHECKALPHA_ANY || mipLevel == 0) {
				alphaStatus_ = ReplacedTextureAlpha(res);
			}
			levels_.push_back(level);
		} else {
			good = false;
		}

		return LoadLevelResult::CONTINUE;

	} else if (imageType == ReplacedImageType::PNG) {
		png_image png = {};
		png.version = PNG_IMAGE_VERSION;

		std::string pngdata;
		pngdata.resize(fileSize);
		pngdata.resize(vfs_->Read(openFile, &pngdata[0], fileSize));
		vfs_->CloseFile(openFile);
		if (!png_image_begin_read_from_memory(&png, &pngdata[0], pngdata.size())) {
			ERROR_LOG(Log::TexReplacement, "Could not load texture replacement info: %s - %s (zip)", filename.c_str(), png.message);
			return LoadLevelResult::LOAD_ERROR;
		}
		if (png.width > (uint32_t)level.w || png.height > (uint32_t)level.h) {
			ERROR_LOG(Log::TexReplacement, "Texture replacement changed since header read: %s", filename.c_str());
			return LoadLevelResult::LOAD_ERROR;
		}

		bool checkedAlpha = false;
		if ((png.format & PNG_FORMAT_FLAG_ALPHA) == 0) {
			// Well, we know for sure it doesn't have alpha.
			if (mipLevel == 0) {
				alphaStatus_ = ReplacedTextureAlpha::FULL;
			}
			checkedAlpha = true;
		}
		png.format = PNG_FORMAT_RGBA;

		std::vector<uint8_t> &out = data_[mipLevel];
		// TODO: Should probably try to handle out-of-memory gracefully here.
		out.resize(level.w * level.h * 4);
		if (!png_image_finish_read(&png, nullptr, &out[0], level.w * 4, nullptr)) {
			ERROR_LOG(Log::TexReplacement, "Could not load texture replacement: %s - %s", filename.c_str(), png.message);
			out.resize(0);
			return LoadLevelResult::LOAD_ERROR;
		}
		png_image_free(&png);

		if (!checkedAlpha) {
			// This will only check the hashed bits.
			CheckAlphaResult res = CheckAlpha32Rect((u32 *)&out[0], level.w, png.width, png.height, 0xFF000000);
			if (res == CHECKALPHA_ANY || mipLevel == 0) {
				alphaStatus_ = ReplacedTextureAlpha(res);
			}
		}

		levels_.push_back(level);
		return LoadLevelResult::CONTINUE;
	} else {
		WARN_LOG(Log::TexReplacement, "Don't know how to load this image type! %d", (int)imageType);
		vfs_->CloseFile(openFile);
	}
	return LoadLevelResult::LOAD_ERROR;
}

bool ReplacedTexture::CopyLevelTo(int level, uint8_t *out, size_t outDataSize, int rowPitch) {
	_assert_msg_((size_t)level < levels_.size(), "Invalid miplevel");
	_assert_msg_(out != nullptr && rowPitch > 0, "Invalid out/pitch");

	if (State() != ReplacementState::ACTIVE) {
		WARN_LOG(Log::TexReplacement, "Init not done yet");
		return false;
	}

	// We pad the images right here during the copy.
	// TODO: Add support for the texture cache to scale texture coordinates instead.
	// It already supports this for render target textures that aren't powers of 2.

	int outW = levels_[level].fullW;
	int outH = levels_[level].fullH;

	// We probably could avoid this lock, but better to play it safe.
	std::lock_guard<std::mutex> guard(lock_);

	const ReplacedTextureLevel &info = levels_[level];
	const std::vector<uint8_t> &data = data_[level];

	if (data.empty()) {
		WARN_LOG(Log::TexReplacement, "Level %d is empty", level);
		return false;
	}

#define PARALLEL_COPY

	int blockSize;
	if (!Draw::DataFormatIsBlockCompressed(fmt, &blockSize)) {
		if (fmt != Draw::DataFormat::R8G8B8A8_UNORM) {
			ERROR_LOG(Log::TexReplacement, "Unexpected linear data format");
			return false;
		}

		if (rowPitch < info.w * 4) {
			ERROR_LOG(Log::TexReplacement, "Replacement rowPitch=%d, but w=%d (level=%d) (too small)", rowPitch, info.w * 4, level);
			return false;
		}

		_assert_msg_(data.size() == info.w * info.h * 4, "Data has wrong size");

		if (rowPitch == info.w * 4) {
#ifdef PARALLEL_COPY
			ParallelMemcpy(&g_threadManager, out, data.data(), info.w * 4 * info.h);
#else
			memcpy(out, data.data(), info.w * 4 * info.h);
#endif
		} else {
#ifdef PARALLEL_COPY
			const int MIN_LINES_PER_THREAD = 4;
			ParallelRangeLoop(&g_threadManager, [&](int l, int h) {
				int extraPixels = outW - info.w;
				for (int y = l; y < h; ++y) {
					memcpy((uint8_t *)out + rowPitch * y, data.data() + info.w * 4 * y, info.w * 4);
					// Fill the rest of the line with black.
					memset((uint8_t *)out + rowPitch * y + info.w * 4, 0, extraPixels * 4);
				}
				}, 0, info.h, MIN_LINES_PER_THREAD);
#else
			int extraPixels = outW - info.w;
			for (int y = 0; y < info.h; ++y) {
				memcpy((uint8_t *)out + rowPitch * y, data.data() + info.w * 4 * y, info.w * 4);
				memset((uint8_t *)out + rowPitch * y + info.w * 4, 0, extraPixels * 4);
			}
#endif
			// Memset the rest of the padding to avoid leaky edge pixels. Guess we could parallelize this too, but meh.
			for (int y = info.h; y < outH; y++) {
				uint8_t *dest = (uint8_t *)out + rowPitch * y;
				memset(dest, 0, outW * 4);
			}
		}
	} else {
#ifdef PARALLEL_COPY
		// Only parallel copy in the simple case for now.
		if (info.w == outW && info.h == outH) {
			// TODO: Add sanity checks here for other formats?
			ParallelMemcpy(&g_threadManager, out, data.data(), data.size());
			return true;
		}
#endif
		// Alright, so careful copying of blocks it is, padding with zero-blocks as needed.
		int inBlocksW = (info.w + 3) / 4;
		int inBlocksH = (info.h + 3) / 4;
		int outBlocksW = (info.fullW + 3) / 4;
		int outBlocksH = (info.fullH + 3) / 4;

		int paddingBlocksX = outBlocksW - inBlocksW;

		// Copy all the known blocks, and zero-fill out the lines.
		for (int y = 0; y < inBlocksH; y++) {
			const uint8_t *input = data.data() + y * inBlocksW * blockSize;
			uint8_t *output = (uint8_t *)out + y * outBlocksW * blockSize;
			memcpy(output, input, inBlocksW * blockSize);
			memset(output + inBlocksW * blockSize, 0, paddingBlocksX * blockSize);
		}

		// Vertical zero-padding.
		for (int y = inBlocksH; y < outBlocksH; y++) {
			uint8_t *output = (uint8_t *)out + y * outBlocksW * blockSize;
			memset(output, 0, outBlocksW * blockSize);
		}
	}

	return true;
}

const char *StateString(ReplacementState state) {
	switch (state) {
	case ReplacementState::UNLOADED: return "UNLOADED";
	case ReplacementState::PENDING: return "PENDING";
	case ReplacementState::NOT_FOUND: return "NOT_FOUND";
	case ReplacementState::ACTIVE: return "ACTIVE";
	case ReplacementState::CANCEL_INIT: return "CANCEL_INIT";
	default: return "N/A";
	}
}
