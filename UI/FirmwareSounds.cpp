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

#include <cstring>

#include "Common/File/FileUtil.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Core/HW/SasAudio.h"
#include "UI/FirmwareSounds.h"

namespace FirmwareSounds {

namespace {

// A VAG file's header, all big-endian.
constexpr size_t VAG_HEADER_SIZE = 0x30;
constexpr size_t VAG_DATA_SIZE_OFFSET = 0x0C;
constexpr size_t VAG_RATE_OFFSET = 0x10;
constexpr size_t VAG_NAME_OFFSET = 0x20;
constexpr size_t VAG_NAME_SIZE = 16;

uint32_t ReadBE32(const uint8_t *p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

}  // namespace

static Path ResourcePath(const Path &nandRoot, std::string_view file) {
	return nandRoot / "flash0/vsh/resource" / file;
}

bool Available(const Path &nandRoot) {
	return !nandRoot.empty() && File::Exists(ResourcePath(nandRoot, "system_plugin.rco"));
}

// Every VAG in an RCO, with stereo ones (two VAGs in a row with the same name and length) joined.
static std::vector<Sound> LoadSoundsFromRCO(const Path &path) {
	std::vector<Sound> sounds;
	std::string file;
	if (!File::Exists(path) || !File::ReadBinaryFileToString(path, &file)) {
		return sounds;
	}
	const uint8_t *data = (const uint8_t *)file.data();
	const size_t size = file.size();

	size_t pos = 0;
	while (pos + VAG_HEADER_SIZE <= size) {
		const size_t start = file.find("VAGp", pos);
		if (start == std::string::npos || start + VAG_HEADER_SIZE > size) {
			break;
		}
		const uint8_t *header = data + start;
		const uint32_t dataSize = ReadBE32(header + VAG_DATA_SIZE_OFFSET);
		const uint32_t rate = ReadBE32(header + VAG_RATE_OFFSET);
		if (dataSize > size - start - VAG_HEADER_SIZE || rate < 8000 || rate > 96000) {
			pos = start + 4;
			continue;
		}
		const char *nameChars = (const char *)header + VAG_NAME_OFFSET;
		std::string name(nameChars, strnlen(nameChars, VAG_NAME_SIZE));
		std::vector<int16_t> samples;
		VagDecoder::DecodeAll(header + VAG_HEADER_SIZE, dataSize, &samples);
		pos = start + VAG_HEADER_SIZE + dataSize;
		if (samples.empty()) {
			continue;
		}

		Sound *previous = sounds.empty() ? nullptr : &sounds.back();
		if (previous && previous->name == name && previous->channels == 1 && previous->rateInHz == (int)rate && previous->samples.size() == samples.size()) {
			// The right channel of a stereo sound.
			std::vector<int16_t> stereo(samples.size() * 2);
			for (size_t i = 0; i < samples.size(); i++) {
				stereo[i * 2] = previous->samples[i];
				stereo[i * 2 + 1] = samples[i];
			}
			previous->samples = std::move(stereo);
			previous->channels = 2;
			continue;
		}
		Sound sound;
		sound.name = std::move(name);
		sound.rateInHz = (int)rate;
		sound.samples = std::move(samples);
		sounds.push_back(std::move(sound));
	}
	INFO_LOG(Log::Audio, "Found %d sounds in %s", (int)sounds.size(), path.c_str());
	return sounds;
}

std::vector<Sound> LoadXMBSounds(const Path &nandRoot) {
	if (nandRoot.empty()) {
		return {};
	}
	return LoadSoundsFromRCO(ResourcePath(nandRoot, "system_plugin.rco"));
}

std::vector<Sound> LoadCameraSounds(const Path &nandRoot) {
	if (nandRoot.empty()) {
		return {};
	}
	return LoadSoundsFromRCO(ResourcePath(nandRoot, "camera_plugin.rco"));
}

const Sound *Find(const std::vector<Sound> &sounds, std::string_view prefix) {
	for (const Sound &sound : sounds) {
		if (startsWith(sound.name, prefix)) {
			return &sound;
		}
	}
	return nullptr;
}

}  // namespace FirmwareSounds
