// Copyright (c) 2026- PPSSPP Project.

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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "Common/Data/Format/IniFile.h"
#include "Common/Data/Format/PNGLoad.h"
#include "Common/File/DirListing.h"
#include "Common/File/FileUtil.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/VR/PPSSPPVR.h"
#include "Core/Config.h"
#include "Core/Util/PathUtil.h"
#include "GPU/Common/Overlay.h"

// Overlays are full screen images, no need to go beyond what a 4K display needs.
// Also keeps us within the max texture size of most mobile GPUs.
static constexpr int MAX_OVERLAY_DIMENSION = 4096;
// Pixels with alpha above this count as the overlay's border when searching for the hole.
// Not 0, since many bezels have soft edges or a light glare over the screen area.
static constexpr uint8_t HOLE_ALPHA_THRESHOLD = 250;
static constexpr int MIN_HOLE_SIZE = 16;

static std::mutex g_overlayLock;
static std::vector<OverlayInfo> g_overlayInfo;
static bool g_overlayInfoLoaded = false;

// The currently loaded overlay.
static std::string g_activeId;
static bool g_activeFailed = false;
static OverlayInfo g_activeInfo;
static std::shared_ptr<const OverlayImage> g_activeImage;
static bool g_activeHasHole = false;
static int g_activeHole[4]{};
static uint32_t g_generation = 0;

bool DetectOverlayHole(const uint8_t *rgba, int width, int height, int *x, int *y, int *w, int *h) {
	if (!rgba || width <= 0 || height <= 0)
		return false;

	auto isBorder = [&](int px, int py) {
		return rgba[((size_t)py * width + px) * 4 + 3] > HOLE_ALPHA_THRESHOLD;
	};

	const int cx = width / 2;
	const int cy = height / 2;
	if (isBorder(cx, cy))
		return false;

	int left = cx;
	while (left > 0 && !isBorder(left - 1, cy))
		left--;
	int right = cx;
	while (right < width - 1 && !isBorder(right + 1, cy))
		right++;
	int top = cy;
	while (top > 0 && !isBorder(cx, top - 1))
		top--;
	int bottom = cy;
	while (bottom < height - 1 && !isBorder(cx, bottom + 1))
		bottom++;

	const int hw = right - left + 1;
	const int hh = bottom - top + 1;
	if (hw < MIN_HOLE_SIZE || hh < MIN_HOLE_SIZE)
		return false;

	*x = left;
	*y = top;
	*w = hw;
	*h = hh;
	return true;
}

static bool ParseHole(const std::string &str, OverlayInfo *info) {
	int x, y, w, h;
	if (sscanf(str.c_str(), "%d , %d , %d , %d", &x, &y, &w, &h) != 4 || w <= 0 || h <= 0 || x < 0 || y < 0)
		return false;
	info->holeFromIni = true;
	info->holeX = x;
	info->holeY = y;
	info->holeW = w;
	info->holeH = h;
	return true;
}

static void LoadOverlayIni(const Path &dir, const std::string &dirName, std::vector<OverlayInfo> *out) {
	IniFile ini;
	if (!ini.Load(dir / "overlay.ini"))
		return;

	for (auto &sectionPtr : ini.Sections()) {
		const Section &section = *sectionPtr;
		std::string image;
		if (section.name().empty() || !section.Get("Image", &image) || image.empty())
			continue;
		if (HasPathTraversal(image)) {
			WARN_LOG(Log::G3D, "Overlay '%s/%s': ignoring image name '%s'", dirName.c_str(), std::string(section.name()).c_str(), image.c_str());
			continue;
		}

		OverlayInfo info;
		info.id = dirName + "/" + std::string(section.name());
		info.name = std::string(section.name());
		section.Get("Name", &info.name);
		info.imagePath = dir / image;

		std::string layer;
		if (section.Get("Layer", &layer) && equalsNoCase(layer, "Background"))
			info.layer = OverlayLayer::Background;

		std::string hole;
		if (section.Get("Hole", &hole) && !hole.empty() && !ParseHole(hole, &info)) {
			WARN_LOG(Log::G3D, "Overlay '%s': bad Hole '%s', expected x,y,w,h. Will auto-detect.", info.id.c_str(), hole.c_str());
		}
		out->push_back(info);
	}
}

static void ScanOverlays(std::vector<OverlayInfo> *out) {
	out->clear();
	const Path root = GetSysDirectory(DIRECTORY_CUSTOM_OVERLAYS);
	std::vector<File::FileInfo> files;
	File::GetFilesInDir(root, &files);

	for (const auto &file : files) {
		if (file.isDirectory) {
			LoadOverlayIni(file.fullName, file.name, out);
		} else if (endsWithNoCase(file.name, ".png")) {
			OverlayInfo info;
			info.id = file.name;
			info.name = file.name.substr(0, file.name.size() - 4);
			info.imagePath = file.fullName;
			out->push_back(info);
		}
	}

	std::sort(out->begin(), out->end(), [](const OverlayInfo &a, const OverlayInfo &b) {
		return a.name < b.name;
	});
	INFO_LOG(Log::G3D, "Found %d overlays in %s", (int)out->size(), root.c_str());
}

static void UnloadActiveLocked() {
	g_activeId.clear();
	g_activeFailed = false;
	g_activeImage.reset();
	g_activeHasHole = false;
	g_generation++;
}

void ReloadAllOverlayInfo() {
	std::lock_guard<std::mutex> guard(g_overlayLock);
	ScanOverlays(&g_overlayInfo);
	g_overlayInfoLoaded = true;

	// Keep the loaded overlay if its definition didn't change, so that rescanning (which the
	// display layout screen does every time its views are recreated) doesn't cause a reload.
	if (!g_activeId.empty()) {
		auto iter = std::find_if(g_overlayInfo.begin(), g_overlayInfo.end(), [](const OverlayInfo &info) { return info.id == g_activeId; });
		const bool unchanged = iter != g_overlayInfo.end() && !g_activeFailed &&
			iter->imagePath == g_activeInfo.imagePath && iter->layer == g_activeInfo.layer &&
			iter->holeFromIni == g_activeInfo.holeFromIni && iter->holeX == g_activeInfo.holeX &&
			iter->holeY == g_activeInfo.holeY && iter->holeW == g_activeInfo.holeW && iter->holeH == g_activeInfo.holeH;
		if (!unchanged) {
			UnloadActiveLocked();
		}
	}
}

std::vector<OverlayInfo> GetAllOverlayInfo() {
	std::lock_guard<std::mutex> guard(g_overlayLock);
	if (!g_overlayInfoLoaded) {
		ScanOverlays(&g_overlayInfo);
		g_overlayInfoLoaded = true;
	}
	return g_overlayInfo;
}

bool ImportOverlayImage(const Path &source, std::string *id) {
	// Check the format by content, since content URIs may not have a usable extension.
	FILE *f = File::OpenCFile(source, "rb");
	uint8_t magic[8]{};
	const bool readOk = f && fread(magic, 1, sizeof(magic), f) == sizeof(magic);
	if (f)
		fclose(f);
	static const uint8_t pngMagic[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	if (!readOk || memcmp(magic, pngMagic, sizeof(magic)) != 0) {
		WARN_LOG(Log::G3D, "Overlay import: %s is not a PNG", source.c_str());
		return false;
	}

	const Path root = GetSysDirectory(DIRECTORY_CUSTOM_OVERLAYS);
	File::CreateFullPath(root);

	std::string base = source.GetFilename();
	if (endsWithNoCase(base, ".png"))
		base = base.substr(0, base.size() - 4);
	if (!IsSafePathComponent(base) || base.empty())
		base = "overlay";

	std::string filename = base + ".png";
	for (int i = 2; File::Exists(root / filename); i++) {
		filename = StringFromFormat("%s_%d.png", base.c_str(), i);
	}

	if (!File::Copy(source, root / filename)) {
		ERROR_LOG(Log::G3D, "Overlay import: failed to copy %s to %s", source.c_str(), (root / filename).c_str());
		return false;
	}
	INFO_LOG(Log::G3D, "Imported overlay %s as %s", source.c_str(), filename.c_str());
	// The file name is new, so this can't affect the loaded overlay.
	ReloadAllOverlayInfo();
	*id = filename;
	return true;
}

// Called with the lock held. Sets g_activeFailed on failure.
static void LoadActiveLocked(const std::string &id) {
	UnloadActiveLocked();
	g_activeId = id;

	if (!g_overlayInfoLoaded) {
		ScanOverlays(&g_overlayInfo);
		g_overlayInfoLoaded = true;
	}

	auto iter = std::find_if(g_overlayInfo.begin(), g_overlayInfo.end(), [&](const OverlayInfo &info) { return info.id == id; });
	if (iter == g_overlayInfo.end()) {
		WARN_LOG(Log::G3D, "Overlay '%s' not found", id.c_str());
		g_activeFailed = true;
		return;
	}
	g_activeInfo = *iter;

	std::string data;
	if (!File::ReadBinaryFileToString(g_activeInfo.imagePath, &data)) {
		ERROR_LOG(Log::G3D, "Overlay '%s': failed to read %s", id.c_str(), g_activeInfo.imagePath.c_str());
		g_activeFailed = true;
		return;
	}

	int width = 0, height = 0;
	unsigned char *pixels = nullptr;
	if (pngLoadPtr((const unsigned char *)data.data(), data.size(), &width, &height, &pixels, MAX_OVERLAY_DIMENSION, MAX_OVERLAY_DIMENSION) != 1 || !pixels) {
		ERROR_LOG(Log::G3D, "Overlay '%s': failed to decode %s (PNG, max %dx%d)", id.c_str(), g_activeInfo.imagePath.c_str(), MAX_OVERLAY_DIMENSION, MAX_OVERLAY_DIMENSION);
		free(pixels);
		g_activeFailed = true;
		return;
	}

	auto image = std::make_shared<OverlayImage>();
	image->width = width;
	image->height = height;
	image->rgba.assign(pixels, pixels + (size_t)width * height * 4);
	free(pixels);

	if (g_activeInfo.holeFromIni) {
		// Clamp to the image.
		int x0 = std::clamp(g_activeInfo.holeX, 0, width);
		int y0 = std::clamp(g_activeInfo.holeY, 0, height);
		int x1 = std::clamp(g_activeInfo.holeX + g_activeInfo.holeW, 0, width);
		int y1 = std::clamp(g_activeInfo.holeY + g_activeInfo.holeH, 0, height);
		g_activeHasHole = x1 - x0 >= MIN_HOLE_SIZE && y1 - y0 >= MIN_HOLE_SIZE;
		g_activeHole[0] = x0;
		g_activeHole[1] = y0;
		g_activeHole[2] = x1 - x0;
		g_activeHole[3] = y1 - y0;
	} else {
		g_activeHasHole = DetectOverlayHole(image->rgba.data(), width, height, &g_activeHole[0], &g_activeHole[1], &g_activeHole[2], &g_activeHole[3]);
	}

	if (g_activeHasHole) {
		INFO_LOG(Log::G3D, "Overlay '%s': %dx%d, hole at %d,%d size %dx%d%s", id.c_str(), width, height,
			g_activeHole[0], g_activeHole[1], g_activeHole[2], g_activeHole[3], g_activeInfo.holeFromIni ? "" : " (detected)");
	} else {
		WARN_LOG(Log::G3D, "Overlay '%s': %dx%d, no hole found, the game keeps its normal position. Set Hole=x,y,w,h in overlay.ini to place it.", id.c_str(), width, height);
	}

	g_activeImage = image;
}

bool GetOverlayLayout(const DisplayLayoutConfig &config, float frameX, float frameY, float frameW, float frameH, OverlayLayout *layout) {
	if (config.sOverlay.empty() || config.bEnableCardboardVR || IsVREnabled() || frameW <= 0.0f || frameH <= 0.0f)
		return false;
	// With skip buffer effects, the game draws straight to the backbuffer and the presentation
	// code that draws the overlay doesn't run. Keep the normal layout then, rather than a game in a
	// hole with nothing around it. The software renderer always presents, so it's fine there.
	if (g_Config.bSkipBufferEffects && !g_Config.bSoftwareRendering)
		return false;

	std::lock_guard<std::mutex> guard(g_overlayLock);
	if (config.sOverlay != g_activeId) {
		LoadActiveLocked(config.sOverlay);
	}
	if (g_activeFailed || !g_activeImage)
		return false;

	const float imgW = (float)g_activeImage->width;
	const float imgH = (float)g_activeImage->height;

	// Fit the overlay to the frame, keeping the aspect ratio. Then apply the display layout's scale
	// and offset to the overlay, the same way they'd apply to the game without one, so moving and
	// resizing the display moves the overlay (with the game inside it). This also keeps the portrait
	// default of having the screen high up.
	const float scale = std::min(frameW / imgW, frameH / imgH) * config.fDisplayScale;
	layout->overlayW = imgW * scale;
	layout->overlayH = imgH * scale;
	layout->overlayX = frameX + frameW * config.fDisplayOffsetX - layout->overlayW * 0.5f;
	layout->overlayY = frameY + frameH * config.fDisplayOffsetY - layout->overlayH * 0.5f;

	layout->layer = g_activeInfo.layer;
	layout->hasHole = g_activeHasHole;
	if (g_activeHasHole) {
		layout->holeX = layout->overlayX + g_activeHole[0] * scale;
		layout->holeY = layout->overlayY + g_activeHole[1] * scale;
		layout->holeW = g_activeHole[2] * scale;
		layout->holeH = g_activeHole[3] * scale;
	}
	layout->generation = g_generation;
	return true;
}

std::shared_ptr<const OverlayImage> GetOverlayImage(uint32_t generation) {
	std::lock_guard<std::mutex> guard(g_overlayLock);
	if (generation != g_generation)
		return nullptr;
	return g_activeImage;
}
