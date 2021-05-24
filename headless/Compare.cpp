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

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <iostream>
#include <vector>
#include "headless/Compare.h"

#include "Common/Data/Convert/ColorConv.h"
#include "Common/Data/Format/PNGLoad.h"
#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"
#include "Core/Host.h"
#include "Core/Loaders.h"

#include "GPU/GPUState.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/TextureDecoder.h"


bool teamCityMode = false;
std::string currentTestName = "";

void TeamCityPrint(const char *fmt, ...)
{
	if (!teamCityMode)
		return;

	const int TEMP_BUFFER_SIZE = 32768;
	char temp[TEMP_BUFFER_SIZE];

	va_list args;
	va_start(args, fmt);
	vsnprintf(temp, TEMP_BUFFER_SIZE - 1, fmt, args);
	temp[TEMP_BUFFER_SIZE - 1] = '\0';
	va_end(args);

	printf("##teamcity[%s]\n", temp);
}

void GitHubActionsPrint(const char *type, const char *fmt, ...) {
	if (!getenv("GITHUB_ACTIONS"))
		return;

	const int TEMP_BUFFER_SIZE = 32768;
	char temp[TEMP_BUFFER_SIZE];

	va_list args;
	va_start(args, fmt);
	vsnprintf(temp, TEMP_BUFFER_SIZE - 1, fmt, args);
	temp[TEMP_BUFFER_SIZE - 1] = '\0';
	va_end(args);

	printf("::%s file=%s::%s\n", type, currentTestName.c_str(), temp);
}

struct BufferedLineReader {
	const static int MAX_BUFFER = 5;
	const static int TEMP_BUFFER_SIZE = 32768;

	BufferedLineReader(const std::string &data) : data_(data) {
	}

	void Fill() {
		while (valid_ < MAX_BUFFER && HasMoreLines()) {
			buffer_[valid_++] = TrimNewlines(ReadLine());
		}
	}

	const std::string Peek(int pos) {
		if (pos >= valid_) {
			Fill();
		}
		if (pos >= valid_) {
			return "";
		}
		return buffer_[pos];
	}

	void Skip(int count) {
		if (count > valid_) {
			count = valid_;
		}
		valid_ -= count;
		for (int i = 0; i < valid_; ++i) {
			buffer_[i] = buffer_[i + count];
		}
		Fill();
	}

	const std::string Consume() {
		const std::string result = Peek(0);
		Skip(1);
		return result;
	}

	bool HasLines() {
		if (HasMoreLines()) {
			return true;
		}
		// Don't say yes if it's a blank line.
		for (int i = 0; i < valid_; ++i) {
			if (!buffer_[i].empty()) {
				return true;
			}
		}
		return false;
	}

	bool Compare(BufferedLineReader &other) {
		if (Peek(0) != other.Peek(0)) {
			return false;
		}

		Skip(1);
		other.Skip(1);
		return true;
	}

protected:
	BufferedLineReader() {
	}

	virtual bool HasMoreLines() {
		return pos_ != data_.npos;
	}

	virtual std::string ReadLine() {
		size_t next = data_.find('\n', pos_);
		if (next == data_.npos) {
			std::string result = data_.substr(pos_);
			pos_ = next;
			return result;
		} else {
			std::string result = data_.substr(pos_, next - pos_);
			pos_ = next + 1;
			return result;
		}
	}

	static std::string TrimNewlines(const std::string &s) {
		size_t p = s.find_last_not_of("\r\n");
		if (p == s.npos) {
			return "";
		}
		return s.substr(0, p + 1);
	}

	int valid_ = 0;
	std::string buffer_[MAX_BUFFER];
	const std::string data_;
	size_t pos_ = 0;
};

Path ExpectedScreenshotFromFilename(const Path &bootFilename) {
	std::string extension = bootFilename.GetFileExtension();
	if (extension.empty()) {
		return bootFilename.WithExtraExtension(".bmp");
	}

	// Let's use pngs as the default for ppdmp tests.
	if (extension == ".ppdmp") {
		return bootFilename.WithReplacedExtension(".png");
	}
	return bootFilename.WithReplacedExtension(".expected.bmp");
}

static std::string ChopFront(std::string s, std::string front)
{
	if (s.size() >= front.size())
	{
		if (s.substr(0, front.size()) == front)
			return s.substr(front.size());
	}
	return s;
}

static std::string ChopEnd(std::string s, std::string end)
{
	if (s.size() >= end.size())
	{
		size_t endpos = s.size() - end.size();
		if (s.substr(endpos) == end)
			return s.substr(0, endpos);
	}
	return s;
}

std::string GetTestName(const Path &bootFilename)
{
	// Kinda ugly, trying to guesstimate the test name from filename...
	return ChopEnd(ChopFront(ChopFront(bootFilename.ToString(), "tests/"), "pspautotests/tests/"), ".prx");
}

bool CompareOutput(const Path &bootFilename, const std::string &output, bool verbose) {
	Path expect_filename = bootFilename.WithReplacedExtension(".prx", ".expected");
	std::unique_ptr<FileLoader> expect_loader(ConstructFileLoader(expect_filename));

	if (expect_loader->Exists()) {
		std::string expect_results;
		expect_results.resize(expect_loader->FileSize());
		expect_results.resize(expect_loader->ReadAt(0, expect_loader->FileSize(), &expect_results[0]));

		BufferedLineReader expected(expect_results);
		BufferedLineReader actual(output);

		bool failed = false;
		while (expected.HasLines())
		{
			if (expected.Compare(actual))
				continue;

			if (!failed)
			{
				TeamCityPrint("testFailed name='%s' message='Output different from expected file'", currentTestName.c_str());
				GitHubActionsPrint("error", "Incorrect output for %s", currentTestName.c_str());
				failed = true;
			}

			// This is a really dirt simple comparing algorithm.

			// Perhaps it was an extra line?
			if (expected.Peek(0) == actual.Peek(1) || !expected.HasLines())
				printf("+ %s\n", actual.Consume().c_str());
			// A single missing line?
			else if (expected.Peek(1) == actual.Peek(0) || !actual.HasLines())
				printf("- %s\n", expected.Consume().c_str());
			else
			{
				printf("O %s\n", actual.Consume().c_str());
				printf("E %s\n", expected.Consume().c_str());
			}
		}

		while (actual.HasLines())
		{
			// If it's a blank line, this will pass.
			if (actual.Compare(expected))
				continue;

			printf("+ %s\n", actual.Consume().c_str());
		}

		if (verbose)
		{
			if (!failed)
			{
				printf("++++++++++++++ The Equal Output +++++++++++++\n");
				printf("%s", output.c_str());
				printf("+++++++++++++++++++++++++++++++++++++++++++++\n");
			}
			else
			{
				printf("============== output from failed %s:\n", GetTestName(bootFilename).c_str());
				printf("%s", output.c_str());
				printf("============== expected output:\n");
				std::string fullExpected;
				if (File::ReadFileToString(true, expect_filename, fullExpected))
					printf("%s", fullExpected.c_str());
				printf("===============================\n");
			}
		}

		return !failed;
	} else {
		std::unique_ptr<FileLoader> screenshot(ConstructFileLoader(ExpectedScreenshotFromFilename(bootFilename)));
		bool failed = true;
		if (screenshot->Exists()) {
			// Okay, just a screenshot then.  Allow a pass with no output (i.e. screenshot match.)
			failed = output.find_first_not_of(" \r\n\t") != output.npos;
			if (failed) {
				TeamCityPrint("testFailed name='%s' message='Output different from expected file'", currentTestName.c_str());
				GitHubActionsPrint("error", "Incorrect output for %s", currentTestName.c_str());
			}
		} else {
			fprintf(stderr, "Expectation file %s not found\n", expect_filename.c_str());
			TeamCityPrint("testIgnored name='%s' message='Expects file missing'", currentTestName.c_str());
			GitHubActionsPrint("error", "Expected file missing for %s", currentTestName.c_str());
		}

		if (verbose || (screenshot->Exists() && failed)) {
			BufferedLineReader actual(output);
			while (actual.HasLines()) {
				printf("+ %s\n", actual.Consume().c_str());
			}
		}
		return !failed;
	}
}

inline int ComparePixel(u32 pix1, u32 pix2)
{
	// For now, if they're different at all except alpha, it's an error.
	if ((pix1 & 0xFFFFFF) != (pix2 & 0xFFFFFF))
		return 1;
	return 0;
}

std::vector<u32> TranslateDebugBufferToCompare(const GPUDebugBuffer *buffer, u32 stride, u32 h)
{
	// If the output was small, act like everything outside was 0.
	// This can happen depending on viewport parameters.
	u32 safeW = std::min(stride, buffer->GetStride());
	u32 safeH = std::min(h, buffer->GetHeight());

	std::vector<u32> data;
	data.resize(stride * h, 0);

	const u32 *pixels32 = (const u32 *)buffer->GetData();
	const u16 *pixels16 = (const u16 *)buffer->GetData();
	int outStride = buffer->GetStride();
	if (!buffer->GetFlipped()) {
		// Bitmaps are flipped, so we have to compare backwards in this case.
		int toLastRow = outStride * (buffer->GetHeight() - 1);
		pixels32 += toLastRow;
		pixels16 += toLastRow;
		outStride = -outStride;
	}

	u32 errors = 0;
	for (u32 y = 0; y < safeH; ++y) {
		switch (buffer->GetFormat()) {
		case GPU_DBG_FORMAT_8888:
			ConvertBGRA8888ToRGBA8888(&data[y * stride], pixels32, safeW);
			break;
		case GPU_DBG_FORMAT_8888_BGRA:
			memcpy(&data[y * stride], pixels32, safeW * sizeof(u32));
			break;

		case GPU_DBG_FORMAT_565:
			ConvertRGB565ToBGRA8888(&data[y * stride], pixels16, safeW);
			break;
		case GPU_DBG_FORMAT_5551:
			ConvertRGBA5551ToBGRA8888(&data[y * stride], pixels16, safeW);
			break;
		case GPU_DBG_FORMAT_4444:
			ConvertRGBA4444ToBGRA8888(&data[y * stride], pixels16, safeW);
			break;

		default:
			data.resize(0);
			return data;
		}

		pixels32 += outStride;
		pixels16 += outStride;
	}

	return data;
}

double CompareScreenshot(const std::vector<u32> &pixels, u32 stride, u32 w, u32 h, const Path& screenshotFilename, std::string &error)
{
	if (pixels.size() < stride * h)
	{
		error = "Buffer format conversion error";
		return -1.0f;
	}

	// We assume the bitmap is the specified size, not including whatever stride.
	u32 *reference = nullptr;
	bool asBitmap = false;

	std::unique_ptr<FileLoader> loader(ConstructFileLoader(screenshotFilename));
	if (loader->Exists()) {
		uint8_t header[2];
		if (loader->ReadAt(0, 2, header) != 2) {
			error = "Unable to read screenshot data: " + screenshotFilename.ToVisualString();
			return -1.0f;
		}

		if (header[0] == 'B' && header[1] == 'M') {
			reference = (u32 *)calloc(stride * h, sizeof(u32));
			asBitmap = true;
			// The bitmap header is 14 + 40 bytes.  We could validate it but the test would fail either way.
			if (reference && loader->ReadAt(14 + 40, sizeof(u32), stride * h, reference) != stride * h) {
				error = "Unable to read screenshot data: " + screenshotFilename.ToVisualString();
				return -1.0f;
			}
		} else {
			// For now, assume a PNG otherwise.
			std::vector<uint8_t> compressed;
			compressed.resize(loader->FileSize());
			if (loader->ReadAt(0, compressed.size(), &compressed[0]) != compressed.size()) {
				error = "Unable to read screenshot data: " + screenshotFilename.ToVisualString();
				return -1.0f;
			}

			int width, height;
			if (!pngLoadPtr(&compressed[0], compressed.size(), &width, &height, (unsigned char **)&reference)) {
				error = "Unable to read screenshot data: " + screenshotFilename.ToVisualString();
				return -1.0f;
			}
		}
	} else {
		error = "Unable to read screenshot: " + screenshotFilename.ToVisualString();
		return -1.0f;
	}

	if (!reference) {
		error = "Unable to allocate screenshot data: " + screenshotFilename.ToVisualString();
		return -1.0f;
	}

	u32 errors = 0;
	if (asBitmap) {
		// The reference is flipped and BGRA by default for the common BMP compare case.
		for (u32 y = 0; y < h; ++y) {
			u32 yoff = y * stride;
			for (u32 x = 0; x < w; ++x)
				errors += ComparePixel(pixels[y * stride + x], reference[yoff + x]);
		}
	} else {
		// Just convert to BGRA for simplicity.
		ConvertRGBA8888ToBGRA8888(reference, reference, h * stride);
		for (u32 y = 0; y < h; ++y) {
			u32 yoff = (h - y - 1) * stride;
			for (u32 x = 0; x < w; ++x)
				errors += ComparePixel(pixels[y * stride + x], reference[yoff + x]);
		}
	}

	free(reference);

	return (double) errors / (double) (w * h);
}
