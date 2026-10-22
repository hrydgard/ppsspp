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

// Overlays (bezels): an image drawn around (or behind) the emulated screen, filling the
// black bars on displays whose aspect ratio doesn't match the PSP's.
//
// Overlays live in memstick/PSP/OVERLAYS (or overlays):
//
//  - Any PNG directly in that folder is an overlay on its own. It's drawn in front of the game,
//    and the game is placed in its transparent hole, which is found automatically.
//    Existing RetroArch overlay PNGs work as-is this way. "Load overlay image..." in the display
//    layout screen copies any picked PNG into this folder, so no file management is needed.
//
//  - A subfolder with an overlay.ini can define one or more overlays, one per section:
//
//      [RP2S_Digimon]
//      Name=Retroid Pocket 2S - Digimon
//      Image=rp2s_640x480.png
//      Layer=Foreground        ; or Background (drawn behind the game)
//      Hole=0,0,640,362        ; optional, in image pixels. Auto-detected if missing.
//
// Placement: the overlay is fit to the screen (aspect preserved), then the display layout's scale
// and offset are applied to it, like they normally are to the game. So moving or resizing the
// display moves the overlay, and the portrait default (screen high up) works. The hole is mapped
// with the same scale and offset, and the PSP screen is fit into the hole using the remaining
// display options (stretch, integer scale, aspect ratio).

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Common/File/Path.h"

struct DisplayLayoutConfig;
struct FRect;

enum class OverlayLayer {
	Foreground,
	Background,
};

struct OverlayInfo {
	std::string id;     // What gets stored in the config. "file.png" or "folder/section".
	std::string name;   // Display name.
	Path imagePath;
	OverlayLayer layer = OverlayLayer::Foreground;
	// Hole from the ini, in image pixels. If not set, it's auto-detected.
	bool holeFromIni = false;
	int holeX = 0;
	int holeY = 0;
	int holeW = 0;
	int holeH = 0;
};

// The decoded image of the active overlay, for uploading to the GPU.
struct OverlayImage {
	int width = 0;
	int height = 0;
	std::vector<uint8_t> rgba;  // width * height * 4 bytes.
};

// Where things go on screen, for the active overlay.
struct OverlayLayout {
	OverlayLayer layer = OverlayLayer::Foreground;
	float overlayX = 0.0f, overlayY = 0.0f, overlayW = 0.0f, overlayH = 0.0f;  // Overlay image, in output pixels.
	bool hasHole = false;
	float holeX = 0.0f, holeY = 0.0f, holeW = 0.0f, holeH = 0.0f;  // The hole, in output pixels.
	// Changes whenever a different image gets loaded, so the presentation code knows to re-upload.
	uint32_t generation = 0;
};

// Rescans the overlays folder. The loaded overlay is kept unless its definition changed or it's gone.
void ReloadAllOverlayInfo();
std::vector<OverlayInfo> GetAllOverlayInfo();

// The "just pick an image" flow: copies a PNG from anywhere (including Android content URIs) into the
// overlays folder and rescans. On success, *id is what to put in DisplayLayoutConfig::sOverlay.
// Such overlays are always foreground with an auto-detected hole, no ini needed.
bool ImportOverlayImage(const Path &source, std::string *id);

// Returns false if no overlay is selected in this config, it failed to load, or overlays can't
// be used right now (VR, Cardboard). Cheap to call every frame; loads the image on first use.
bool GetOverlayLayout(const DisplayLayoutConfig &config, float frameX, float frameY, float frameW, float frameH, OverlayLayout *layout);

// The decoded image for the given generation, or null if it's no longer the active one.
std::shared_ptr<const OverlayImage> GetOverlayImage(uint32_t generation);

// Finds the transparent hole by walking from the image center towards each edge, stopping at
// the first nearly-opaque pixel. Returns false if the center itself is opaque, or the hole is tiny.
// Exposed for unit tests.
bool DetectOverlayHole(const uint8_t *rgba, int width, int height, int *x, int *y, int *w, int *h);
