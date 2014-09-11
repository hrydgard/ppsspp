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

#pragma once

#include "../Globals.h"
#include "Common/MemoryUtil.h"
#include "Common/CommonFuncs.h"
#include "gfx/gl_common.h"
#include "opencl.h"

namespace oclscale {
	static const size_t SIZES[] = { 1088, 2176, 4352, 8704, 17408 };
	static const size_t NNST[] = { 16, 32, 64, 128, 256 };
	static const int MAX_SOURCE_SIZE = 0x100000;
	static u32 g_imgCount = 0;

	class OclScaler {
	public:
		OclScaler();
		~OclScaler();

		static bool IsSupported();
		bool IsActive();
		cl_int Release();

		cl_int NNEDI3Scale(u32 factor, //Only tested on: 2-5 (Note: non-power-of-two sizes are scaled up from a power-of-two using Spline36)
			u32* src, u32* trg, u32 srcWidth, u32 srcHeight, const u32 neurons[], GLenum format);
		cl_int Spline36Scale(u32 factor, //Only tested on: 2-5
			u32* src, u32* trg, u32 srcWidth, u32 srcHeight, GLenum format);

	private:
		cl_int SetupCtx();
		cl_int SetupOGLInteropCtx(cl_uint numPlatforms, const cl_platform_id platformIDs[]);
		cl_int SetupRegularCtx(cl_uint numPlatforms, const cl_platform_id platformIDs[]);

		static cl_int PlatformSupportsExtension(const cl_platform_id& curPlatformID, const char* extension);
		static cl_int PlatformDevicesSupportExtension(const cl_platform_id& curPlatformID, const char* extension);
		static cl_int DeviceSupportsExtension(const cl_device_id& curDeviceID, const char* extension);
		static cl_int DeviceSuitable(const cl_device_id& curDeviceID);
		static cl_int OutputDeviceInfo(const cl_device_id& curDeviceID, cl_int param, const char* paramName);

		cl_int SetupNNEDI3();
		cl_int SetupSpline36();
		cl_int SetupNNEDI3RGBA();
		cl_int SetupNNEDI3YUVA();

		cl_int SetupProgram(const char* srcFileName, cl_program& program);
		cl_int GrabWeights(u32 nns, cl_mem& weightBuffer);

		cl_int NNEDI3RGBAScale(u32 factor, u32* src, u32* trg, u32 srcWidth, u32 srcHeight, u32 factorBase, u32 trueFactor, GLenum format);
		cl_int NNEDI3YUVAScale(u32 factor, u32* src, u32* trg, u32 srcWidth, u32 srcHeight, u32 factorBase, u32 trueFactor, GLenum format);

		cl_int NNEDI3KernelCall(const cl_kernel& nnedi3Kernel, const cl_mem& input, cl_mem& output, const cl_mem& weightBuffer, size_t nns, 
			u32 curWidth, u32 curHeight, int swapXY, const cl_float4 matrix, int offset, size_t globalWorkGroup[], const size_t localWorkGroup[]);
		cl_int Spline36KernelCall(const cl_kernel& spline36Kernel, const cl_mem& input, const cl_mem& output,
			float curWidthFlt, float curHeightFlt, float shiftLeft, float shiftTop, u32 resizedWidth,
			u32 resizedHeight, int is5551, size_t globalWorkGroup[], const size_t localWorkGroup[]);
		cl_int MergeIntoRGBAKernelCall(const cl_kernel& mergeIntoRGBAKernel, const cl_mem& bChan, const cl_mem& gChan, const cl_mem& rChan,
			const cl_mem& aChan, cl_mem& output, u32 curWidth, u32 curHeight, size_t globalWorkGroup[], const size_t localWorkGroup[]);
		cl_int BGRAtoYUVAKernelCall(const cl_kernel& BGRAtoYUVAKernel, const cl_mem& input, cl_mem& output,
			u32 curWidth, u32 curHeight, size_t globalWorkGroup[], const size_t localWorkGroup[]);
		cl_int YUVAtoBGRAwithMergeKernelCall(const cl_kernel& YUVAtoBGRAwithMergeKernel, const cl_mem& yChan, const cl_mem& uChan, const cl_mem& vChan,
			const cl_mem& aChan, cl_mem& output, u32 curWidth, u32 curHeight, int is5551, size_t globalWorkGroup[], const size_t localWorkGroup[]);
		cl_int Spline36SingleChanKernelCall(const cl_kernel& spline36Kernel, const cl_mem& input, const cl_mem& output,
			float curWidthFlt, float curHeightFlt, float shiftLeft, float shiftTop, u32 resizedWidth, u32 resizedHeight,
			size_t globalWorkGroup[], const size_t localWorkGroup[], const cl_float4 matrix);
		cl_int ReadOpenCLImageCall(const cl_mem& input, u32 output[], u32 curWidth, u32 curHeight);

		//These are from: https://graphics.stanford.edu/~seander/bithacks.html
		static bool IsPowerOfTwo(u32 n);
		static u32 LogTwoBase(u32 n);

		//This is from: http://stackoverflow.com/questions/2679815/previous-power-of-2
		static u32 PrevPowerOfTwo(u32 n);

		static u32 RoundUp(u32 numToRound, u32 multiple);

		static void HandleOCLError(cl_int error, const char* func, int line, const char* method);

		u32 nns_[3];
		cl_mem weightBuffers[3];

		cl_platform_id platformID;
		cl_device_id deviceID;
		cl_context context;
		cl_command_queue commandQueue;
		cl_kernel nnedi3Kernel;
		cl_kernel spline36Kernel;
		cl_kernel mergeIntoRGBAKernel;
		cl_kernel BGRAtoYUVAKernel;
		cl_kernel YUVAtoBGRAwithMergeKernel;
		cl_kernel spline36SingleChanKernel;

		bool nnedi3Active = false;
		bool spline36Active = false;
		bool nnedi3YUVAActive = false;
		bool nnedi3RGBAActive = false;

		enum { SPLINE36 = 5 };
	};

	class ImageBuf {
	public:
		ImageBuf() {
			Resize(0);
		}

		ImageBuf(u32 size) {
			Resize(size);
		}

		void Resize(u32 size) {
			for (int i = 0; i < 4; i++) {
				rgba[i].resize(size);
			}
		}

		void SetImage(const u32* image, u32 width, u32 height) {
			SeparateChannels(image, width*height);
		}

		//This prints out a 4 component RGBA PAM image
		//It expects the image in the buffer to be BGRA
		void DbgPAM(u32 w, u32 h, const char* prefix = "dbg_ocl_") {
			char fn[32];
			snprintf(fn, 32, "%s%04d.pam", prefix, g_imgCount++);
			FILE *fp = fopen(fn, "wb");
			fprintf(fp, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", w, h);
			for (u32 j = 0; j < h; j++) {
				for (u32 i = 0; i < w; i++) {
					static unsigned char color[4];
					color[0] = rgba[0][(j * w + i)];
					color[1] = rgba[1][(j * w + i)];
					color[2] = rgba[2][(j * w + i)];
					color[3] = rgba[3][(j * w + i)];
					fwrite(color, 1, 4, fp);
				}
			}
			fclose(fp);
		}

		SimpleBuf<u8> rgba[4];

	private:
		void SeparateChannels(const u32* image, u32 size) {
			Resize(size);

			u8* u8Image = (u8*)image;

			for (u32 i = 0, j = 0; i < size; i++, j = j + 4) {
				rgba[0][i] = u8Image[j];
				rgba[1][i] = u8Image[j + 1];
				rgba[2][i] = u8Image[j + 2];
				rgba[3][i] = u8Image[j + 3];
			}
		}
	};
}
