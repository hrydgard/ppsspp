// Copyright (c) 2012- PPSSPP Project.

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

#include "Compare.h"
#include "FileUtil.h"

#include <math.h>

bool CompareOutput(const std::string bootFilename)
{
	std::string expect_filename = bootFilename.substr(bootFilename.length() - 4) + ".expected";
	if (File::Exists(expect_filename))
	{
		// TODO: Do the compare here
		return true;
	}
	else
	{
		fprintf(stderr, "Expectation file %s not found", expect_filename.c_str());
		return false;
	}
}

inline int ComparePixel(u32 pix1, u32 pix2)
{
	// For now, if they're different at all except alpha, it's an error.
	if ((pix1 & 0xFFFFFF) != (pix2 & 0xFFFFFF))
		return 1;
	return 0;
}

double CompareScreenshot(const u8 *pixels, int w, int h, int stride, const std::string screenshotFilename, std::string &error)
{
	u32 *pixels32 = (u32 *) pixels;
	// We assume the bitmap is the specified size, not including whatever stride.
	u32 *reference = (u32 *) calloc(w * h, sizeof(u32));

	FILE *bmp = fopen(screenshotFilename.c_str(), "rb");
	if (bmp)
	{
		// The bitmap header is 14 + 40 bytes.  We could validate it but the test would fail either way.
		fseek(bmp, 14 + 40, SEEK_SET);
		fread(reference, sizeof(u32), w * h, bmp);
		fclose(bmp);
	}
	else
	{
		error = "Unable to read screenshot: " + screenshotFilename;
		free(reference);
		return -1.0f;
	}

	u32 errors = 0;
	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
			errors += ComparePixel(pixels32[y * stride + x], reference[y * w + x]);
	}

	free(reference);

	return (double) errors / (double) (w * h);
}