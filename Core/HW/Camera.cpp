// Copyright (c) 2020- PPSSPP Project.

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
#include "Camera.h"
#include "Core/Config.h"

#include "ext/jpge/jpge.h"

void __cameraDummyImage(int width, int height, unsigned char** outData, int* outLen) {
#ifdef USE_FFMPEG
	unsigned char *rgbData = (unsigned char *)malloc(3 * width * height);
	if (!rgbData) {
		*outData = nullptr;
		return;
	}

	// Generate a solid RED surface for camera detection (e.g., Invizimals)
	// RGB: (255, 0, 0) = Red
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			rgbData[3 * (y * width + x) + 0] = 255;  // Red channel: full intensity
			rgbData[3 * (y * width + x) + 1] = 0;    // Green channel: off
			rgbData[3 * (y * width + x) + 2] = 0;    // Blue channel: off
		}
	}

	*outLen = width * height * 2;
	*outData = (unsigned char*)malloc(*outLen);

	jpge::params params;
	params.m_quality = 60;
	params.m_subsampling = jpge::H2V2;
	params.m_two_pass_flag = false;
	jpge::compress_image_to_jpeg_file_in_memory(
		*outData, *outLen, width, height, 3, rgbData, params);
	free(rgbData);
#endif //USE_FFMPEG
}
