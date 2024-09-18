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
#include <png.h>
#include <vector>

#include "headless/Compare.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/Data/Format/PNGLoad.h"
#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"
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

	bool HasMoreLines() {
		return pos_ != data_.npos;
	}

	std::string ReadLine() {
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
	Path expect_filename = bootFilename.GetFileExtension() == ".prx" ? bootFilename.WithReplacedExtension(".prx", ".expected") : bootFilename.WithExtraExtension(".expected");
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
				if (File::ReadTextFileToString(expect_filename, &fullExpected))
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

static inline double CompareChannel(int pix1, int pix2) {
	double diff = pix1 - pix2;
	return diff * diff;
}

static inline double ComparePixel(u32 pix1, u32 pix2) {
	// Ignore alpha.
	double r = CompareChannel(pix1 & 0xFF, pix2 & 0xFF);
	double g = CompareChannel((pix1 >> 8) & 0xFF, (pix2 >> 8) & 0xFF);
	double b = CompareChannel((pix1 >> 16) & 0xFF, (pix2 >> 16) & 0xFF);

	return r + g + b;
}

std::vector<u32> TranslateDebugBufferToCompare(const GPUDebugBuffer *buffer, u32 stride, u32 h) {
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
		int toLastRow = outStride * (h > buffer->GetHeight() ? buffer->GetHeight() - 1 : h - 1);
		pixels32 += toLastRow;
		pixels16 += toLastRow;
		outStride = -outStride;
	}

	// Skip the bottom of the image in the buffer was smaller.  Remember, we're flipped.
	u32 *dst = &data[0];
	if (safeH < h) {
		dst += (h - safeH) * stride;
	}

	for (u32 y = 0; y < safeH; ++y) {
		switch (buffer->GetFormat()) {
		case GPU_DBG_FORMAT_8888:
			ConvertBGRA8888ToRGBA8888(&dst[y * stride], pixels32, safeW);
			break;
		case GPU_DBG_FORMAT_8888_BGRA:
			memcpy(&dst[y * stride], pixels32, safeW * sizeof(u32));
			break;

		case GPU_DBG_FORMAT_565:
			ConvertRGB565ToBGRA8888(&dst[y * stride], pixels16, safeW);
			break;
		case GPU_DBG_FORMAT_5551:
			ConvertRGBA5551ToBGRA8888(&dst[y * stride], pixels16, safeW);
			break;
		case GPU_DBG_FORMAT_4444:
			ConvertRGBA4444ToBGRA8888(&dst[y * stride], pixels16, safeW);
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


ScreenshotComparer::~ScreenshotComparer() {
	if (reference_)
		free(reference_);
}

double ScreenshotComparer::Compare(const Path &screenshotFilename) {
	if (pixels_.size() < stride_ * h_) {
		error_ = "Buffer format conversion error";
		return -1.0f;
	}

	// We assume the bitmap is the specified size, not including whatever stride.
	std::unique_ptr<FileLoader> loader(ConstructFileLoader(screenshotFilename));
	if (loader->Exists()) {
		uint8_t header[2];
		if (loader->ReadAt(0, 2, header) != 2) {
			error_ = "Unable to read screenshot data: " + screenshotFilename.ToVisualString();
			return -1.0f;
		}

		if (header[0] == 'B' && header[1] == 'M') {
			reference_ = (u32 *)calloc(stride_ * h_, sizeof(u32));
			referenceStride_ = stride_;
			asBitmap_ = true;
			// The bitmap header is 14 + 40 bytes.  We could validate it but the test would fail either way.
			if (reference_ && loader->ReadAt(14 + 40, sizeof(u32), stride_ * h_, reference_) != stride_ * h_) {
				error_ = "Unable to read screenshot data: " + screenshotFilename.ToVisualString();
				free(reference_);
				reference_ = nullptr;
				return -1.0f;
			}
		} else {
			// For now, assume a PNG otherwise.
			std::vector<uint8_t> compressed;
			compressed.resize(loader->FileSize());
			if (loader->ReadAt(0, compressed.size(), &compressed[0]) != compressed.size()) {
				error_ = "Unable to read screenshot data: " + screenshotFilename.ToVisualString();
				return -1.0f;
			}

			int width, height;
			if (!pngLoadPtr(&compressed[0], compressed.size(), &width, &height, (unsigned char **)&reference_)) {
				error_ = "Unable to read screenshot data: " + screenshotFilename.ToVisualString();
				if (reference_)
					free(reference_);
				reference_ = nullptr;
				return -1.0f;
			}

			referenceStride_ = width;
		}
	} else {
		error_ = "Unable to read screenshot: " + screenshotFilename.ToVisualString();
		return -1.0f;
	}

	if (!reference_) {
		error_ = "Unable to allocate screenshot data: " + screenshotFilename.ToVisualString();
		return -1.0f;
	}

	double errors = 0;
	if (asBitmap_) {
		// The reference is flipped and BGRA by default for the common BMP compare case.
		for (u32 y = 0; y < h_; ++y) {
			u32 yoff = y * referenceStride_;
			for (u32 x = 0; x < w_; ++x)
				errors += ComparePixel(pixels_[y * stride_ + x], reference_[yoff + x]);
		}
	} else {
		// Just convert to BGRA for simplicity.
		ConvertRGBA8888ToBGRA8888(reference_, reference_, h_ * referenceStride_);
		for (u32 y = 0; y < h_; ++y) {
			u32 yoff = (h_ - y - 1) * referenceStride_;
			for (u32 x = 0; x < w_; ++x)
				errors += ComparePixel(pixels_[y * stride_ + x], reference_[yoff + x]);
		}
	}

	// Convert to MSE, accounting for all three channels (RGB.)
	return errors / (double)(w_ * h_ * 3);
}

bool ScreenshotComparer::SaveActualBitmap(const Path &resultFilename) {
	static const u8 header[14 + 40] = {
		0x42, 0x4D, 0x38, 0x80, 0x08, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00,
		0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x10, 0x01,
		0x00, 0x00, 0x01, 0x00, 0x20, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x02, 0x80, 0x08, 0x00, 0x12, 0x0B,
		0x00, 0x00, 0x12, 0x0B, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};

	FILE *saved = File::OpenCFile(resultFilename, "wb");
	if (saved) {
		fwrite(&header, sizeof(header), 1, saved);
		fwrite(pixels_.data(), sizeof(u32), stride_ * h_, saved);
		fclose(saved);

		return true;
	}

	return false;
}

bool ScreenshotComparer::SaveVisualComparisonPNG(const Path &resultFilename) {
	std::unique_ptr<u32[]> comparison(new u32[w_ * 2 * h_ * 2]);

	if (asBitmap_) {
		// The reference is flipped and BGRA by default for the common BMP compare case.
		for (u32 y = 0; y < h_; ++y) {
			u32 yoff = y * referenceStride_;
			u32 comparisonRow = (h_ - y - 1) * 2 * w_ * 2;
			for (u32 x = 0; x < w_; ++x) {
				PlotVisualComparison(comparison.get(), comparisonRow + x * 2, pixels_[y * stride_ + x], reference_[yoff + x]);
			}
		}
	} else {
		// Reference is already in BGRA either way.
		for (u32 y = 0; y < h_; ++y) {
			u32 yoff = (h_ - y - 1) * referenceStride_;
			u32 comparisonRow = (h_ - y - 1) * 2 * w_ * 2;
			for (u32 x = 0; x < w_; ++x) {
				PlotVisualComparison(comparison.get(), comparisonRow + x * 2, pixels_[y * stride_ + x], reference_[yoff + x]);
			}
		}
	}

	FILE *fp = File::OpenCFile(resultFilename, "wb");
	if (!fp)
		return false;

	png_image png;
	memset(&png, 0, sizeof(png));
	png.version = PNG_IMAGE_VERSION;
	png.format = PNG_FORMAT_BGRA;
	png.width = w_ * 2;
	png.height = h_ * 2;

	bool success = png_image_write_to_stdio(&png, fp, 0, comparison.get(), w_ * 2 * 4, nullptr) != 0;
	fclose(fp);
	png_image_free(&png);

	return success && png.warning_or_error < 2;
}

int ChannelDifference(u8 actual, u8 reference) {
	int diff = actual > reference ? actual - reference : reference - actual;
	if (diff == 0)
		return 0;
	if (diff < 4)
		return 1;
	if (diff < 8)
		return 2;
	if (diff < 16)
		return 3;
	if (diff < 32)
		return 4;
	return 5;
}

int PixelDifference(u32 actual, u32 reference) {
	int b = ChannelDifference((actual >> 0) & 0xFF, (reference >> 0) & 0xFF);
	int g = ChannelDifference((actual >> 8) & 0xFF, (reference >> 8) & 0xFF);
	int r = ChannelDifference((actual >> 16) & 0xFF, (reference >> 16) & 0xFF);
	return std::max(b, std::max(g, r));
}

void ScreenshotComparer::PlotVisualComparison(u32 *dst, u32 offset, u32 actual, u32 reference) {
	int diff = PixelDifference(actual, reference);
	dst[offset + 0] = actual | 0xFF000000;
	dst[offset + 1] = actual | 0xFF000000;
	dst[offset + w_ * 2 + 0] = reference | 0xFF000000;

	int alpha = 0x00000000;
	switch (diff) {
	case 0: alpha = 0xFF000000; break;
	case 1: alpha = 0xEF000000; break;
	case 2: alpha = 0xCF000000; break;
	case 3: alpha = 0xAF000000; break;
	case 4: alpha = 0x7F000000; break;
	default: break;
	}

	dst[offset + w_ * 2 + 1] = (reference & 0x00FFFFFF) | alpha;
}
