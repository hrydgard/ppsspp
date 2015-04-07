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

#include "headless/Compare.h"
#include "file/file_util.h"

#include "Common/ColorConv.h"
#include "Core/Host.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/TextureDecoder.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <iostream>
#include <fstream>

bool teamCityMode = false;
std::string teamCityName = "";

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

	printf("%s", temp);
}

struct BufferedLineReader {
	const static int MAX_BUFFER = 5;
	const static int TEMP_BUFFER_SIZE = 32768;

	BufferedLineReader(const std::string &data) : valid_(0), data_(data), pos_(0) {
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
	BufferedLineReader() : valid_(0) {
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

	int valid_;
	std::string buffer_[MAX_BUFFER];
	const std::string data_;
	size_t pos_;
};

struct BufferedLineReaderFile : public BufferedLineReader {
	BufferedLineReaderFile(std::ifstream &in) : BufferedLineReader(), in_(in) {
	}

protected:
	virtual bool HasMoreLines() {
		return !in_.eof();
	}

	virtual std::string ReadLine() {
		char temp[TEMP_BUFFER_SIZE];
		in_.getline(temp, TEMP_BUFFER_SIZE);
		return temp;
	}

	std::ifstream &in_;
};

std::string ExpectedFromFilename(const std::string &bootFilename)
{
	return bootFilename.substr(0, bootFilename.length() - 4) + ".expected";
}

std::string ExpectedScreenshotFromFilename(const std::string &bootFilename)
{
	return bootFilename.substr(0, bootFilename.length() - 4) + ".expected.bmp";
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

std::string GetTestName(const std::string &bootFilename)
{
	// Kinda ugly, trying to guesstimate the test name from filename...
	return ChopEnd(ChopFront(ChopFront(bootFilename, "tests/"), "pspautotests/tests/"), ".prx");
}

bool CompareOutput(const std::string &bootFilename, const std::string &output, bool verbose)
{
	std::string expect_filename = ExpectedFromFilename(bootFilename);
	std::ifstream expect_f;
	expect_f.open(expect_filename.c_str(), std::ios::in);
	if (!expect_f.fail())
	{
		BufferedLineReaderFile expected(expect_f);
		BufferedLineReader actual(output);

		bool failed = false;
		while (expected.HasLines())
		{
			if (expected.Compare(actual))
				continue;

			if (!failed)
			{
				TeamCityPrint("##teamcity[testFailed name='%s' message='Output different from expected file']\n", teamCityName.c_str());
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
		expect_f.close();

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
				if (readFileToString(true, expect_filename.c_str(), fullExpected))
					printf("%s", fullExpected.c_str());
				printf("===============================\n");
			}
		}

		return !failed;
	}
	else
	{
		fprintf(stderr, "Expectation file %s not found\n", expect_filename.c_str());
		TeamCityPrint("##teamcity[testIgnored name='%s' message='Expects file missing']\n", teamCityName.c_str());
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

std::vector<u32> TranslateDebugBufferToCompare(const GPUDebugBuffer *buffer, u32 stride, u32 h)
{
	// If the output was small, act like everything outside was 0.
	// This can happen depending on viewport parameters.
	u32 safeW = std::min(stride, buffer->GetStride());
	u32 safeH = std::min(h, buffer->GetHeight());

	std::vector<u32> data;
	data.resize(stride * h, 0);

	const u32 *pixels = (const u32 *)buffer->GetData();
	int outStride = buffer->GetStride();
	if (!buffer->GetFlipped())
	{
		// Bitmaps are flipped, so we have to compare backwards in this case.
		pixels += outStride * (buffer->GetHeight() - 1);
		outStride = -outStride;
	}

	u32 errors = 0;
	for (u32 y = 0; y < safeH; ++y)
	{
		if (buffer->GetFormat() == GPU_DBG_FORMAT_8888)
			ConvertBGRA8888ToRGBA8888(&data[y * stride], pixels, safeW);
		else if (buffer->GetFormat() == GPU_DBG_FORMAT_8888_BGRA)
			memcpy(&data[y * stride], pixels, safeW * sizeof(u32));
		else
		{
			data.resize(0);
			return data;
		}
		pixels += outStride;
	}

	return data;
}

double CompareScreenshot(const std::vector<u32> &pixels, u32 stride, u32 w, u32 h, const std::string& screenshotFilename, std::string &error)
{
	if (pixels.size() < stride * h)
	{
		error = "Buffer format conversion error";
		return -1.0f;
	}

	// We assume the bitmap is the specified size, not including whatever stride.
	u32 *reference = (u32 *) calloc(stride * h, sizeof(u32));

	FILE *bmp = fopen(screenshotFilename.c_str(), "rb");
	if (bmp)
	{
		// The bitmap header is 14 + 40 bytes.  We could validate it but the test would fail either way.
		fseek(bmp, 14 + 40, SEEK_SET);
		if (fread(reference, sizeof(u32), stride * h, bmp) != stride * h)
			error = "Unable to read screenshot data: " + screenshotFilename;
		fclose(bmp);
	}
	else
	{
		error = "Unable to read screenshot: " + screenshotFilename;
		free(reference);
		return -1.0f;
	}

	u32 errors = 0;
	for (u32 y = 0; y < h; ++y)
	{
		for (u32 x = 0; x < w; ++x)
			errors += ComparePixel(pixels[y * stride + x], reference[y * stride + x]);
	}

	free(reference);

	return (double) errors / (double) (w * h);
}