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

// Some of the error checking used here is overly pedantic, but it is probably better 
// to give up early instead of going on and risking some terrible graphics card crash.

#include "OclScale.h"
#if defined(_WIN32) 
	#include <Windows.h>
#endif

namespace oclscale {

//Non-OpenCL errors = 1, Success = 0, Random OpenCL errors = negative numbers
#define GENERAL_ERROR 1;

//Some macros to "simplify" OpenCL error handling
//Handbrake does something similar for its OpenCL error handling
#define OCL_CREATE(out_, method_, ...)                                        \
	do {                                                                      \
		out_ = method_(__VA_ARGS__, &status);                                 \
		if (status != CL_SUCCESS) {                                           \
			HandleOCLError(status, __FUNCTION__, __LINE__, #method_);         \
			goto clean_up;                                                    \
		}                                                                     \
	} while (false)

#define OCL_FREE(method_, memory_)                                            \
	do {                                                                      \
		if (memory_ != NULL) {                                                \
			memStatus = method_(memory_);                                     \
			if (memStatus != CL_SUCCESS) {                                    \
				HandleOCLError(memStatus, __FUNCTION__, __LINE__, #method_);  \
			}                                                                 \
			memory_ = NULL;                                                   \
		}                                                                     \
	} while (false)

#define OCL_CHECK(method_, ...)                                               \
	do {                                                                      \
		status = method_(__VA_ARGS__);                                        \
		if (status != CL_SUCCESS) {                                           \
			HandleOCLError(status, __FUNCTION__, __LINE__, #method_);         \
			goto clean_up;                                                    \
		}                                                                     \
	} while (false)

//These silent checks are used when the error isn't important, or it has
//already been logged in the method being checked and the call hierarchy isn't important
#define OCL_SILENT_CHECK(method_, ...)                                        \
	do {                                                                      \
		status = method_(__VA_ARGS__);                                        \
		if (status != CL_SUCCESS) {                                           \
			goto clean_up;                                                    \
		}                                                                     \
	} while (false)

#define OCL_SILENT_CHECK_NO_ARGS(method_)                                     \
	do {                                                                      \
		status = method_();                                                   \
		if (status != CL_SUCCESS) {                                           \
			goto clean_up;                                                    \
		}                                                                     \
	} while (false)

//Used in cases where we are in a loop and want to go to the next iteration if the check fails
#define OCL_NEXT_ITER_ON_FAIL(method_, ...)                                   \
	{                                                                         \
		status = method_(__VA_ARGS__);                                        \
		if (status != CL_SUCCESS) {                                           \
			continue;                                                         \
		}                                                                     \
	}


	OclScaler::OclScaler() {
		platformID = NULL;
		deviceID = NULL;
		commandQueue = NULL;
		context = NULL;
		nnedi3Kernel = NULL;
		spline36Kernel = NULL;
		mergeIntoRGBAKernel = NULL;
		BGRAtoYUVAKernel = NULL;
		YUVAtoBGRAwithMergeKernel = NULL;
		spline36SingleChanKernel = NULL;

		for (int i = 0; i < 3; i++) {
			nns_[i] = 99;
			weightBuffers[i] = NULL;
		}
	}

	OclScaler::~OclScaler() {
		if (IsActive()) {
			Release();
		}
	}

	//Check if there exists at least one device of any kind that meets the requirements needed for scaling
	bool OclScaler::IsSupported() {
		bool support = false;
		cl_int status = CL_SUCCESS;
		cl_platform_id* platformIDs = NULL;
		cl_device_id* deviceIDs = NULL;

		cl_uint numPlatforms = 0;
		OCL_CHECK(clGetPlatformIDs, 0, NULL, &numPlatforms);
		platformIDs = new cl_platform_id[numPlatforms];
		OCL_CHECK(clGetPlatformIDs, numPlatforms, platformIDs, NULL);

		for (u32 i = 0; i < numPlatforms; i++) {
			cl_uint numDevices = 0;
			OCL_NEXT_ITER_ON_FAIL(clGetDeviceIDs, platformIDs[i], CL_DEVICE_TYPE_ALL, 0, NULL, &numDevices);
			delete[] deviceIDs;
			deviceIDs = new cl_device_id[numDevices];
			OCL_NEXT_ITER_ON_FAIL(clGetDeviceIDs, platformIDs[i], CL_DEVICE_TYPE_ALL, numDevices, deviceIDs, NULL);

			for (u32 j = 0; j < numDevices; j++) {
				OCL_NEXT_ITER_ON_FAIL(DeviceSuitable, deviceIDs[j]);
				support = true;
				goto clean_up;
			}
		}

	clean_up:
		delete[] platformIDs;
		delete[] deviceIDs;
		return support;
	}

	bool OclScaler::IsActive() {
		if (context != NULL) {
			return true;
		} else {
			return false;
		}
	}

	cl_int OclScaler::Release() {
		cl_int memStatus = CL_SUCCESS;
		platformID = NULL;
		deviceID = NULL;

		if (nnedi3Active) {
			OCL_FREE(clReleaseKernel, nnedi3Kernel);
			for (int i = 0; i < 3; i++) {
				nns_[i] = 99;
				OCL_FREE(clReleaseMemObject, weightBuffers[i]);
			}
		}

		if (spline36Active) {
			OCL_FREE(clReleaseKernel, spline36Kernel);
		}

		if (nnedi3RGBAActive) {
			OCL_FREE(clReleaseKernel, mergeIntoRGBAKernel);
		}

		if (nnedi3YUVAActive) {
			OCL_FREE(clReleaseKernel, BGRAtoYUVAKernel);
			OCL_FREE(clReleaseKernel, YUVAtoBGRAwithMergeKernel);
			OCL_FREE(clReleaseKernel, spline36SingleChanKernel);
		}

		OCL_FREE(clReleaseCommandQueue, commandQueue);
		OCL_FREE(clReleaseContext, context);

		nnedi3Active = false;
		spline36Active = false;
		nnedi3YUVAActive = false;
		nnedi3RGBAActive = false;

		return memStatus;
	}

	//This tries to setup the OpenCL context on the same GPU as the OpenGL context
	//In cases where that doesn't work it will try to find another OpenCL device
	cl_int OclScaler::SetupCtx() {
		cl_int status = CL_SUCCESS;
		cl_platform_id* platformIDs = NULL;
		cl_device_id* deviceIDs = NULL;

		cl_uint numPlatforms = 0;
		OCL_CHECK(clGetPlatformIDs, 0, NULL, &numPlatforms);
		platformIDs = new cl_platform_id[numPlatforms];
		OCL_CHECK(clGetPlatformIDs, numPlatforms, platformIDs, NULL);

		if (SetupInteropCtx(numPlatforms, platformIDs) != CL_SUCCESS) {
			//Since the above didn't work/didn't apply we try the more generic fallback approach
			OCL_SILENT_CHECK(SetupRegularCtx, numPlatforms, platformIDs);
		}

		OCL_SILENT_CHECK(OutputDeviceInfo, deviceID, CL_DEVICE_VENDOR, "device is from vendor");
		OCL_SILENT_CHECK(OutputDeviceInfo, deviceID, CL_DEVICE_NAME, "device is named");
		OCL_SILENT_CHECK(OutputDeviceInfo, deviceID, CL_DEVICE_OPENCL_C_VERSION, "device is using OpenCL C version");
		OCL_SILENT_CHECK(OutputDeviceInfo, deviceID, CL_DRIVER_VERSION, "device has the software driver version");
		OCL_SILENT_CHECK(OutputDeviceInfo, deviceID, CL_DEVICE_PROFILE, "device is using the profile");

		OCL_CREATE(commandQueue, clCreateCommandQueue, context, deviceID, CL_NONE);

	clean_up:
		delete[] platformIDs;
		return status;
	}

	//Try to setup on the same GPU as the OpenGL context
	//This requires platform specific code, and any non-Windows code has not been tested
	//For now non-Windows platforms will be forced into the fallback approach
	cl_int OclScaler::SetupInteropCtx(cl_uint numPlatforms, const cl_platform_id platformIDs[]) {
		cl_int status = CL_SUCCESS;

		#if defined(_WIN32)
		for (u32 i = 0; i < numPlatforms; i++) {
			//#if defined(__APPLE__) 
			//CGLContextObj glContext = CGLGetCurrentContext();
			//CGLShareGroupObj shareGroup = CGLGetShareGroup(glContext);
			//#endif

			cl_context_properties props[] = {
				#if defined(_WIN32) 
				CL_CONTEXT_PLATFORM, (cl_context_properties)platformIDs[i],
				CL_GL_CONTEXT_KHR, (cl_context_properties)wglGetCurrentContext(),
				CL_WGL_HDC_KHR, (cl_context_properties)wglGetCurrentDC(),
				//#elif defined(__linux__) 
				//CL_CONTEXT_PLATFORM, (cl_context_properties)platformIDs[i],
				//CL_GL_CONTEXT_KHR, (cl_context_properties)glXGetCurrentContext(),
				//CL_GLX_DISPLAY_KHR, (cl_context_properties)glXGetCurrentDisplay(),
				//#elif defined(__APPLE__) 
				//CL_CONTEXT_PLATFORM, (cl_context_properties)platformIDs[i],
				//CL_CONTEXT_PROPERTY_USE_CGL_SHAREGROUP_APPLE, (cl_context_properties)shareGroup,
				#endif
			0 };

			bool glSharingOnPlatform = false;
			if (PlatformSupportsExtension(platformIDs[i], "cl_khr_gl_sharing") == CL_SUCCESS) {
				glSharingOnPlatform = true;
			} else {
				//AMD is weird and only lists cl_khr_gl_sharing at the device level (Intel and Nvidia list it on both the device and the platform)
				if (PlatformDevicesSupportExtension(platformIDs[i], "cl_khr_gl_sharing") == CL_SUCCESS) {
					glSharingOnPlatform = true;
				}
			}

			if (glSharingOnPlatform) {
				clGetGLContextInfoKHR_fn pclGetGLContextInfoKHR = (clGetGLContextInfoKHR_fn)clGetExtensionFunctionAddress("clGetGLContextInfoKHR");
				size_t devSizeInBytes = 0;
				OCL_NEXT_ITER_ON_FAIL(pclGetGLContextInfoKHR, props, CL_CURRENT_DEVICE_FOR_GL_CONTEXT_KHR, 0, NULL, &devSizeInBytes);

				if (devSizeInBytes != 0) {
					cl_device_id tempDeviceID = NULL;
					OCL_NEXT_ITER_ON_FAIL(pclGetGLContextInfoKHR, props, CL_CURRENT_DEVICE_FOR_GL_CONTEXT_KHR, sizeof(cl_device_id), &tempDeviceID, NULL);
					cl_device_type deviceType = NULL;
					OCL_NEXT_ITER_ON_FAIL(clGetDeviceInfo, tempDeviceID, CL_DEVICE_TYPE, sizeof(cl_device_type), &deviceType, NULL);

					//Avoid setting up on non-GPU devices that happen to have access to the OpenGL context
					if (CL_DEVICE_TYPE_GPU & deviceType) {
						OCL_NEXT_ITER_ON_FAIL(DeviceSupportsExtension, tempDeviceID, "cl_khr_gl_sharing");
						OCL_NEXT_ITER_ON_FAIL(DeviceSuitable, tempDeviceID);

						cl_context tempContext = NULL;
						tempContext = clCreateContext(props, 1, &tempDeviceID, NULL, NULL, &status);

						if (status == CL_SUCCESS) {
							INFO_LOG(G3D, "The OpenCL device is a GPU.");
							platformID = platformIDs[i];
							deviceID = tempDeviceID;
							context = tempContext;
							goto clean_up;
						}
					}
				}
			}
		}
		WARN_LOG(G3D, "Could not setup OpenCL/OpenGL Interop context on any suitable device.");
		#endif
		status = GENERAL_ERROR;

	clean_up:
		return status;
	}

	//On multi-GPU machines this approach may pick a mediocre GPU since it uses the first one it finds
	cl_int OclScaler::SetupRegularCtx(cl_uint numPlatforms, const cl_platform_id platformIDs[]) {
		cl_int status = CL_SUCCESS;
		cl_device_id* deviceIDs = NULL;
		cl_platform_id cpuPlatformID, gpuPlatformID, accelPlatformID;
		cl_device_id cpuDeviceID, gpuDeviceID, accelDeviceID;
		bool isCPU = false, isGPU = false, isAccel = false;

		for (u32 i = 0; i < numPlatforms; i++) {
			cl_uint numDevices = 0;
			OCL_NEXT_ITER_ON_FAIL(clGetDeviceIDs, platformIDs[i], CL_DEVICE_TYPE_ALL, 0, NULL, &numDevices);
			delete[] deviceIDs;
			deviceIDs = new cl_device_id[numDevices];
			OCL_NEXT_ITER_ON_FAIL(clGetDeviceIDs, platformIDs[i], CL_DEVICE_TYPE_ALL, numDevices, deviceIDs, NULL);

			for (u32 j = 0; j < numDevices; j++) {
				cl_device_type deviceType = NULL;
				OCL_NEXT_ITER_ON_FAIL(clGetDeviceInfo, deviceIDs[j], CL_DEVICE_TYPE, sizeof(cl_device_type), &deviceType, NULL);
				OCL_NEXT_ITER_ON_FAIL(DeviceSuitable, deviceIDs[j]);

				//First CPU device that meets the requirements
				if (!isCPU && (CL_DEVICE_TYPE_CPU & deviceType)) {
					isCPU = true;
					cpuPlatformID = platformIDs[i];
					cpuDeviceID = deviceIDs[j];
				}

				//First GPU device that meets the requirements
				if (!isGPU && (CL_DEVICE_TYPE_GPU & deviceType)) {
					isGPU = true;
					gpuPlatformID = platformIDs[i];
					gpuDeviceID = deviceIDs[j];
				}

				//First Accelerator device that meets the requirements
				if (!isAccel && (CL_DEVICE_TYPE_ACCELERATOR & deviceType)) {
					isAccel = true;
					accelPlatformID = platformIDs[i];
					accelDeviceID = deviceIDs[j];
				}
			}
		}

		if (isGPU || isAccel || isCPU) {
			if (isGPU) {
				platformID = gpuPlatformID;
				deviceID = gpuDeviceID;
			} else if (isAccel) {
				platformID = accelPlatformID;
				deviceID = accelDeviceID;
			} else { //isCPU
				platformID = cpuPlatformID;
				deviceID = cpuDeviceID;
			}
		} else {
			ERROR_LOG(G3D, "No suitable OpenCL device detected.");
			status = GENERAL_ERROR;
			goto clean_up;
		}

		cl_context_properties props[] = { CL_CONTEXT_PLATFORM, (cl_context_properties)platformID, 0 };
		OCL_CREATE(context, clCreateContext, props, 1, &deviceID, NULL, NULL);

	clean_up:
		delete[] deviceIDs;
		return status;
	}

	//Check if the extension is listed in the platform's extensions list
	cl_int OclScaler::PlatformSupportsExtension(const cl_platform_id& curPlatformID, const char* extension) {
		cl_int status = CL_SUCCESS;
		size_t platExtStringSize = 0;
		char* platExtensions = NULL;

		OCL_SILENT_CHECK(clGetPlatformInfo, curPlatformID, CL_PLATFORM_EXTENSIONS, NULL, NULL, &platExtStringSize);
		platExtensions = new char[platExtStringSize + 1];
		OCL_SILENT_CHECK(clGetPlatformInfo, curPlatformID, CL_PLATFORM_EXTENSIONS, platExtStringSize, platExtensions, NULL);

		if (strstr(platExtensions, extension)) {
			status = CL_SUCCESS;
		} else {
			status = GENERAL_ERROR;
		}

	clean_up:
		delete[] platExtensions;
		return status;
	}

	//Check if the extension is listed in one of the devices on a platform
	cl_int OclScaler::PlatformDevicesSupportExtension(const cl_platform_id& curPlatformID, const char* extension) {
		cl_int status = CL_SUCCESS;
		cl_uint numDevices = 0;
		cl_device_id* deviceIDs = NULL;

		OCL_SILENT_CHECK(clGetDeviceIDs, curPlatformID, CL_DEVICE_TYPE_ALL, 0, NULL, &numDevices);
		deviceIDs = new cl_device_id[numDevices];
		OCL_SILENT_CHECK(clGetDeviceIDs, curPlatformID, CL_DEVICE_TYPE_ALL, numDevices, deviceIDs, NULL);

		for (u32 i = 0; i < numDevices; i++) {
			status = DeviceSupportsExtension(deviceIDs[i], extension);
			if (status == CL_SUCCESS) {
				//At least one device supports it so we should be good
				goto clean_up;
			}
		}
		status = GENERAL_ERROR;

	clean_up:
		delete[] deviceIDs;
		return status;
	}

	//Check if the extension is supported on the device
	cl_int OclScaler::DeviceSupportsExtension(const cl_device_id& curDeviceID, const char* extension) {
		cl_int status = CL_SUCCESS;
		size_t devExtStringSize = 0;
		char* devExtensions = NULL;

		OCL_SILENT_CHECK(clGetDeviceInfo, curDeviceID, CL_DEVICE_EXTENSIONS, NULL, NULL, &devExtStringSize);
		devExtensions = new char[devExtStringSize + 1];
		OCL_SILENT_CHECK(clGetDeviceInfo, curDeviceID, CL_DEVICE_EXTENSIONS, devExtStringSize, devExtensions, NULL);

		if (strstr(devExtensions, extension)) {
			status = CL_SUCCESS;
		} else {
			status = GENERAL_ERROR;
		}

	clean_up:
		delete[] devExtensions;
		return status;
	}

	//Check if the device is suitable for scaling
	cl_int OclScaler::DeviceSuitable(const cl_device_id& curDeviceID) {
		cl_int status = CL_SUCCESS;
		cl_uint maxWorkItemDims;
		size_t maxWorkItemSizes[3];
		size_t maxWorkGroupSize;
		cl_bool check;

		OCL_CHECK(clGetDeviceInfo, curDeviceID, CL_DEVICE_AVAILABLE, sizeof(cl_bool), &check, NULL);
		if (check == CL_FALSE) {
			return GENERAL_ERROR;
		}

		OCL_CHECK(clGetDeviceInfo, curDeviceID, CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS, sizeof(cl_uint), &maxWorkItemDims, NULL);
		if (maxWorkItemDims < 2) {
			return GENERAL_ERROR;
		}

		OCL_CHECK(clGetDeviceInfo, curDeviceID, CL_DEVICE_MAX_WORK_ITEM_SIZES, sizeof(size_t) * 3, &maxWorkItemSizes, NULL);
		if (maxWorkItemSizes[0] < 8 || maxWorkItemSizes[1] < 8 || maxWorkItemSizes[2] < 1) {
			return GENERAL_ERROR;
		}

		OCL_CHECK(clGetDeviceInfo, curDeviceID, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(size_t), &maxWorkGroupSize, NULL);
		if (maxWorkGroupSize < (8 * 8 * 1)) {
			return GENERAL_ERROR;
		}

		OCL_CHECK(clGetDeviceInfo, curDeviceID, CL_DEVICE_COMPILER_AVAILABLE, sizeof(cl_bool), &check, NULL);
		if (check == CL_FALSE) {
			return GENERAL_ERROR;
		}

		OCL_CHECK(clGetDeviceInfo, curDeviceID, CL_DEVICE_IMAGE_SUPPORT, sizeof(cl_bool), &check, NULL);
		if (check == CL_FALSE) {
			return GENERAL_ERROR;
		}

	clean_up:
		return status;
	}

	cl_int OclScaler::OutputDeviceInfo(const cl_device_id& curDeviceID, cl_int param, const char* paramName) {
		cl_int status = CL_SUCCESS;
		char *deviceName = NULL;
		size_t retValSize;

		OCL_CHECK(clGetDeviceInfo, curDeviceID, param, 0, NULL, &retValSize);
		deviceName = new char[retValSize + 1];
		OCL_CHECK(clGetDeviceInfo, curDeviceID, param, retValSize, deviceName, NULL);
		INFO_LOG(G3D, "The OpenCL %s: %s", paramName, deviceName);

	clean_up:
		delete[] deviceName;
		return status;
	}

	cl_int OclScaler::SetupNNEDI3() {
		cl_int status = CL_SUCCESS, memStatus = CL_SUCCESS;
		cl_program nnedi3Program = NULL;

		if (!nnedi3Active) {
			const char nnedi3oclSrc[] = "./assets/opencl/NNEDI3ocl_mad.cl";
			OCL_CHECK(SetupProgram, nnedi3oclSrc, nnedi3Program);
			OCL_CREATE(nnedi3Kernel, clCreateKernel, nnedi3Program, "nnedi3");
			nnedi3Active = true;
		}
		OCL_SILENT_CHECK_NO_ARGS(SetupSpline36);

	clean_up:
		OCL_FREE(clReleaseProgram, nnedi3Program);
		return status;
	}

	cl_int OclScaler::SetupSpline36() {
		cl_int status = CL_SUCCESS, memStatus = CL_SUCCESS;
		cl_program spline36Program = NULL;

		if (!spline36Active) {
			const char spline36Src[] = "./assets/opencl/spline36.cl";
			OCL_CHECK(SetupProgram, spline36Src, spline36Program);
			OCL_CREATE(spline36Kernel, clCreateKernel, spline36Program, "Spline36");
			spline36Active = true;
		}

	clean_up:
		OCL_FREE(clReleaseProgram, spline36Program);
		return status;
	}

	cl_int OclScaler::SetupNNEDI3RGBA() {
		cl_int status = CL_SUCCESS, memStatus = CL_SUCCESS;
		cl_program mergeIntoRGBAProgram = NULL;

		if (!nnedi3RGBAActive) {
			const char mergeIntoRGBASrc[] = "./assets/opencl/mergeIntoRGBA.cl";
			OCL_CHECK(SetupProgram, mergeIntoRGBASrc, mergeIntoRGBAProgram);
			OCL_CREATE(mergeIntoRGBAKernel, clCreateKernel, mergeIntoRGBAProgram, "MergeIntoRGBA");
			nnedi3RGBAActive = true;
		}
		OCL_SILENT_CHECK_NO_ARGS(SetupNNEDI3);

	clean_up:
		OCL_FREE(clReleaseProgram, mergeIntoRGBAProgram);
		return status;
	}

	cl_int OclScaler::SetupNNEDI3YUVA() {
		cl_int status = CL_SUCCESS, memStatus = CL_SUCCESS;
		cl_program BGRAtoYUVAProgram = NULL, YUVAtoBGRAwithMergeProgram = NULL, spline36SingleChanProgram = NULL;

		if (!nnedi3YUVAActive) {
			const char BGRAtoYUVASrc[] = "./assets/opencl/BGRAtoYUVA.cl";
			OCL_CHECK(SetupProgram, BGRAtoYUVASrc, BGRAtoYUVAProgram);
			OCL_CREATE(BGRAtoYUVAKernel, clCreateKernel, BGRAtoYUVAProgram, "BGRAtoYUVA");

			const char YUVAtoBGRAwithMergeSrc[] = "./assets/opencl/YUVAtoBGRAwithMerge.cl";
			OCL_CHECK(SetupProgram, YUVAtoBGRAwithMergeSrc, YUVAtoBGRAwithMergeProgram);
			OCL_CREATE(YUVAtoBGRAwithMergeKernel, clCreateKernel, YUVAtoBGRAwithMergeProgram, "YUVAtoBGRAwithMerge");

			const char spline36SingleChanSrc[] = "./assets/opencl/spline36SingleChan.cl";
			OCL_CHECK(SetupProgram, spline36SingleChanSrc, spline36SingleChanProgram);
			OCL_CREATE(spline36SingleChanKernel, clCreateKernel, spline36SingleChanProgram, "Spline36SingleChan");

			nnedi3YUVAActive = true;
		}
		OCL_SILENT_CHECK_NO_ARGS(SetupNNEDI3);

	clean_up:
		OCL_FREE(clReleaseProgram, BGRAtoYUVAProgram);
		OCL_FREE(clReleaseProgram, YUVAtoBGRAwithMergeProgram);
		OCL_FREE(clReleaseProgram, spline36SingleChanProgram);
		return status;
	}

	cl_int OclScaler::SetupProgram(const char* srcFileName, cl_program& program) {
		cl_int status = CL_SUCCESS;
		size_t sourceSize;
		char *sourceStr = NULL, *buildLog = NULL;

		FILE* source = fopen(srcFileName, "r");
		if (source == NULL) {
			ERROR_LOG(G3D, "Failed to open program file from %s!\n", srcFileName);
			status = GENERAL_ERROR;
			goto clean_up;
		} else {
			sourceStr = new char[MAX_SOURCE_SIZE];
			sourceSize = fread(sourceStr, 1, MAX_SOURCE_SIZE, source);
			fclose(source);
		}

		OCL_CREATE(program, clCreateProgramWithSource, context, 1, (const char **)&sourceStr, (const size_t *)&sourceSize);

		//Compile the program using every speed optimization possible
		//These optimizations shouldn't cause any major difference in output
		status = clBuildProgram(program, 1, &deviceID, "-cl-denorms-are-zero -cl-fast-relaxed-math -cl-single-precision-constant", NULL, NULL);

		if (status != CL_SUCCESS) {
			size_t retValSize;

			HandleOCLError(status, "SetupProgram", __LINE__, "clBuildProgram");

			OCL_CHECK(clGetProgramBuildInfo, program, deviceID, CL_PROGRAM_BUILD_LOG, 0, NULL, &retValSize);
			buildLog = new char[retValSize + 1];
			OCL_CHECK(clGetProgramBuildInfo, program, deviceID, CL_PROGRAM_BUILD_LOG, retValSize, buildLog, (size_t*)NULL);

			ERROR_LOG(G3D, "Failed to build the OpenCL program from %s!\nThe build log is:\n%s", srcFileName, buildLog);
		}

	clean_up:
		delete[] sourceStr;
		delete[] buildLog;
		return status;
	}

	//Load NNEDI3 weights from a weights file
	cl_int OclScaler::GrabWeights(u32 nns, cl_mem& weightBuffer) {
		cl_int status = CL_SUCCESS, memStatus = CL_SUCCESS;
		SimpleBuf<float> weights;
		std::string weightFileName;
		char* cWeightFileName = NULL;

		weightFileName = "./assets/opencl/NNEDI3ocl_mad_";
		weightFileName += std::to_string(NNST[nns]);
		weightFileName += ".bin";
		cWeightFileName = new char[weightFileName.length() + 1];
		std::strcpy(cWeightFileName, weightFileName.c_str());

		FILE* weightFile = fopen(cWeightFileName, "rb");
		if (weightFile == NULL) {
			ERROR_LOG(G3D, "Failed to open NNEDI3 weights file from %s!\n", cWeightFileName);
			status = GENERAL_ERROR;
			goto clean_up;
		} else {
			weights.resize(SIZES[nns]);
			fread(weights.data(), sizeof(float), SIZES[nns], weightFile);
			fclose(weightFile);
		}

		OCL_FREE(clReleaseMemObject, weightBuffer);
		OCL_CREATE(weightBuffer, clCreateBuffer, context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, weights.size() * sizeof(float), weights.data());

	clean_up:
		delete[] cWeightFileName;
		return status;
	}

	cl_int OclScaler::NNEDI3Scale(u32 factor, u32* src, u32* trg, u32 srcWidth, u32 srcHeight, const u32 neurons[], GLenum format) {
		cl_int status = CL_SUCCESS;

		if (!IsActive()) {
			OCL_SILENT_CHECK_NO_ARGS(SetupCtx);
		}

		//In the case of 3x and 5x trueFactor contains the actual factor value that is passed in
		//and factor is changed to either 2x or 4x respectively for 3x and 5x
		const u32 trueFactor = factor;

		//For these non-power-of-two cases spline36 is used to bring the texture to the requested size
		if (!IsPowerOfTwo(factor)) {
			factor = PrevPowerOfTwo(factor);
		}
		const u32 factorBase = LogTwoBase(factor);

		//Read in any necessary weights
		for (int i = 0; i < 3; i++) {
			if (neurons[i] != nns_[i]) {
				nns_[i] = neurons[i];
				if (nns_[i] < SPLINE36) {
					OCL_SILENT_CHECK(GrabWeights, nns_[i], weightBuffers[i]);
				}
			}
		}

		//This special path for RGBA scaling is around 20% faster than the YUVA path 
		//It avoids a colour space conversion and other unnecessary OpenCL calls
		if (neurons[0] == neurons[1] && neurons[0] == neurons[2] && neurons[1] == neurons[2]) {
			if (!nnedi3RGBAActive || !nnedi3Active || !spline36Active) {
				OCL_SILENT_CHECK_NO_ARGS(SetupNNEDI3RGBA);
			}
			OCL_SILENT_CHECK(NNEDI3RGBAScale, factor, src, trg, srcWidth, srcHeight, factorBase, trueFactor, format);
		} else { //We use the generic path
			if (!nnedi3YUVAActive || !nnedi3Active || !spline36Active) {
				OCL_SILENT_CHECK_NO_ARGS(SetupNNEDI3YUVA);
			}
			OCL_SILENT_CHECK(NNEDI3YUVAScale, factor, src, trg, srcWidth, srcHeight, factorBase, trueFactor, format);
		}
		goto end;

	clean_up:
		//If we hit this it means something went wrong at some point during the scaling
		//We don't need to touch the trg since it comes pre-zeroed out, and this scaling should fail gracefully
	end:
		return status;
	}

	//Used when all the channels are scaled using NNEDI3 at the same number of neurons
	cl_int OclScaler::NNEDI3RGBAScale(u32 factor, u32* src, u32* trg, u32 srcWidth, u32 srcHeight, u32 factorBase, u32 trueFactor, GLenum format) {
		cl_int status = CL_SUCCESS, memStatus = CL_SUCCESS;

		cl_mem weightBuf = NULL;
		cl_image_format clImageFormatSingle, clImageFormatAll;
		const u32 factorBasePlus1 = factorBase + 1;
		const size_t nns = NNST[nns_[0]];

		//For NNEDI3 scaled channels: [(chan * factorBasePlus1 * 2) + (i * 2) + 0] = no resize, [(chan * factorBasePlus1 * 2) + (i * 2) + 1] = horizontal resize
		cl_mem* clImagesSingle = new cl_mem[4 * (factorBasePlus1)* 2]();
		//[0] = mergedInto8888, [1] = result scaled up to trueFactor with center shift corrected
		cl_mem clImagesAll[2] = { NULL };
		ImageBuf image;

		clImageFormatSingle.image_channel_order = CL_R;
		clImageFormatSingle.image_channel_data_type = CL_UNORM_INT8;
		clImageFormatAll.image_channel_order = CL_RGBA;
		clImageFormatAll.image_channel_data_type = CL_UNORM_INT8;

		image.SetImage(src, srcWidth, srcHeight);
		weightBuf = weightBuffers[0];

		for (int i = 0; i < 4; i++) {
			OCL_CREATE(clImagesSingle[(i * factorBasePlus1 * 2) + (0 * 2) + 0], clCreateImage2D, context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
				&clImageFormatSingle, srcWidth, srcHeight, srcWidth * sizeof(u8), image.rgba[i].data());
		}

		u32 curWidth = srcWidth;
		u32 curHeight = srcHeight;
		size_t globalWorkGroup[2] = { 0 }, localWorkGroup[2] = { 8, 8 };

		for (int i = 0; i < 4; i++) {
			curWidth = srcWidth;
			curHeight = srcHeight;
			int swapXY = 1;
			cl_float4 matrix = { 1.0, 0.0, 0.0, 0.0 };
			int offset = 0;

			for (int j = 0; (2 << (j - 1)) < (int)factor; j++) {
				swapXY = 1;

				OCL_CREATE(clImagesSingle[(i * factorBasePlus1 * 2) + (j * 2) + 1], clCreateImage2D, context,
					CL_MEM_READ_WRITE, &clImageFormatSingle, curWidth * 2, curHeight, 0, NULL);
				OCL_CHECK(NNEDI3KernelCall, nnedi3Kernel, clImagesSingle[(i * factorBasePlus1 * 2) + (j * 2) + 0],
					clImagesSingle[(i * factorBasePlus1 * 2) + (j * 2) + 1], weightBuf, nns, curWidth,
					curHeight, swapXY, matrix, offset, globalWorkGroup, localWorkGroup);

				curWidth = srcWidth * (2 << j);
				swapXY = 0;

				OCL_CREATE(clImagesSingle[(i * factorBasePlus1 * 2) + ((j + 1) * 2) + 0], clCreateImage2D, context,
					CL_MEM_READ_WRITE, &clImageFormatSingle, curWidth, curHeight * 2, 0, NULL);
				OCL_CHECK(NNEDI3KernelCall, nnedi3Kernel, clImagesSingle[(i * factorBasePlus1 * 2) + (j * 2) + 1],
					clImagesSingle[(i * factorBasePlus1 * 2) + ((j + 1) * 2) + 0], weightBuf, nns, curHeight,
					curWidth, swapXY, matrix, offset, globalWorkGroup, localWorkGroup);

				curHeight = srcHeight * (2 << j);
			}
		}

		OCL_CREATE(clImagesAll[0], clCreateImage2D, context, CL_MEM_READ_WRITE, &clImageFormatAll, curWidth, curHeight, 0, NULL);
		OCL_CHECK(MergeIntoRGBAKernelCall, mergeIntoRGBAKernel, clImagesSingle[(0 * factorBasePlus1 * 2) + (factorBase * 2) + 0],
			clImagesSingle[(1 * factorBasePlus1 * 2) + (factorBase * 2) + 0], clImagesSingle[(2 * factorBasePlus1 * 2) + (factorBase * 2) + 0],
			clImagesSingle[(3 * factorBasePlus1 * 2) + (factorBase * 2) + 0], clImagesAll[0], curWidth, curHeight, globalWorkGroup, localWorkGroup);

		//This last "resize" corrects the center shift introduced by resizing
		float curWidthFlt = (float)curWidth;
		float curHeightFlt = (float)curHeight;
		float shiftLeft = 0.0f;
		float shiftTop = 0.0f;
		int is5551 = 0;

		if (factor == trueFactor) { //Applies for power-of-two factors (2x, 4x)
			//Post-resize shift correction
			shiftLeft = (trueFactor - 1.0f) / -2.0f;
			shiftTop = (trueFactor - 1.0f) / -2.0f;
		} else { //Applies for non-power-of-two factors (3x, 5x)
			//During resize shift correction
			if (trueFactor == 3) {
				shiftLeft = -0.70f;
				shiftTop = -0.70f;
			} else { //trueFactor == 5
				shiftLeft = -1.50f;
				shiftTop = -1.50f;
			}

			curWidth = srcWidth * trueFactor;
			curHeight = srcHeight * trueFactor;
		}

		if (format == GL_UNSIGNED_SHORT_5_5_5_1) {
			is5551 = 1;
		}

		OCL_CREATE(clImagesAll[1], clCreateImage2D, context, CL_MEM_READ_WRITE, &clImageFormatAll, curWidth, curHeight, 0, NULL);
		OCL_CHECK(Spline36KernelCall, spline36Kernel, clImagesAll[0], clImagesAll[1], curWidthFlt,
			curHeightFlt, shiftLeft, shiftTop, curWidth, curHeight, is5551, globalWorkGroup, localWorkGroup);

		OCL_CHECK(ReadOpenCLImageCall, clImagesAll[1], trg, curWidth, curHeight);

	clean_up:
		for (u32 i = 0; i < 2; i++) {
			OCL_FREE(clReleaseMemObject, clImagesAll[i]);
		}

		for (u32 i = 0; i < (4 * (factorBasePlus1)* 2); i++) {
			OCL_FREE(clReleaseMemObject, clImagesSingle[i]);
		}
		delete[] clImagesSingle;

		return status;
	}

	//Generic path used for various combinations of NNEDI3 and Spline36 scaling
	cl_int OclScaler::NNEDI3YUVAScale(u32 factor, u32* src, u32* trg, u32 srcWidth, u32 srcHeight, u32 factorBase, u32 trueFactor, GLenum format) {
		cl_int status = CL_SUCCESS, memStatus = CL_SUCCESS;

		cl_image_format clImageFormatSingle, clImageFormatAll;
		const u32 factorBasePlus1 = factorBase + 1;

		//For NNEDI3 scaled channels: [(chan * factorBasePlus1 * 2) + (i * 2) + 0] = no resize, [(chan * factorBasePlus1 * 2) + (i * 2) + 1] = horizontal resize
		//	The final scale with shift correction is in: [(chan * factorBasePlus1 * 2) + (factorBase * 2) + 1]
		//For Spline36 scaled channels: [(chan * factorBasePlus1 * 2) + (factorBase * 2) + 1] = spline36 scale with shift corrected
		cl_mem* clImagesSingle = new cl_mem[4 * (factorBasePlus1)* 2]();
		//[0] = Original Image, [1] = BGRAtoYUVA, [2] = YUVAtoBGRAwithMerge, [3] = result scaled up to trueFactor
		cl_mem clImagesAll[4] = { NULL };

		clImageFormatSingle.image_channel_order = CL_R;
		clImageFormatSingle.image_channel_data_type = CL_UNORM_INT8;
		clImageFormatAll.image_channel_order = CL_RGBA;
		clImageFormatAll.image_channel_data_type = CL_UNORM_INT8;

		//Convert the BGRA image to YUVA
		u32 curWidth = srcWidth;
		u32 curHeight = srcHeight;
		size_t globalWorkGroup[2] = { 0 }, localWorkGroup[2] = { 8, 8 };

		OCL_CREATE(clImagesAll[0], clCreateImage2D, context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
			&clImageFormatAll, curWidth, curHeight, curWidth * sizeof(u32), src);
		OCL_CREATE(clImagesAll[1], clCreateImage2D, context, CL_MEM_READ_WRITE, &clImageFormatAll, curWidth, curHeight, 0, NULL);
		OCL_CHECK(BGRAtoYUVAKernelCall, BGRAtoYUVAKernel, clImagesAll[0], clImagesAll[1], curWidth, curHeight, globalWorkGroup, localWorkGroup);

		float curWidthFlt, curHeightFlt, shiftLeft, shiftTop;

		for (int i = 0; i < 4; i++) {
			u32 curnns_;
			cl_float4 matrix = { 0.0, 0.0, 0.0, 0.0 };
			matrix.s[i] = 1.0;
			if (i > 1) {
				curnns_ = nns_[i - 1];
			} else {
				curnns_ = nns_[i];
			}

			//Scale using NNEDI3
			if (curnns_ < SPLINE36) {
				size_t nns = NNST[curnns_];
				cl_mem weightBuf;
				if (i > 1) {
					weightBuf = weightBuffers[i - 1];
				} else {
					weightBuf = weightBuffers[i];
				}

				curWidth = srcWidth;
				curHeight = srcHeight;
				int swapXY = 1;
				int offset = 0;

				for (int j = 0; (2 << (j - 1)) < (int)factor; j++) {
					swapXY = 1;

					OCL_CREATE(clImagesSingle[(i * factorBasePlus1 * 2) + (j * 2) + 1], clCreateImage2D, context,
						CL_MEM_READ_WRITE, &clImageFormatSingle, curWidth * 2, curHeight, 0, NULL);
					if (j == 0) {
						OCL_CHECK(NNEDI3KernelCall, nnedi3Kernel, clImagesAll[1], clImagesSingle[(i * factorBasePlus1 * 2) + (j * 2) + 1], 
							weightBuf, nns, curWidth, curHeight, swapXY, matrix, offset, globalWorkGroup, localWorkGroup);
						matrix.s[i] = 0.0;
						matrix.s[0] = 1.0;
					} else {
						OCL_CHECK(NNEDI3KernelCall, nnedi3Kernel, clImagesSingle[(i * factorBasePlus1 * 2) + (j * 2) + 0], 
							clImagesSingle[(i * factorBasePlus1 * 2) + (j * 2) + 1], weightBuf, nns, curWidth, 
							curHeight, swapXY, matrix, offset, globalWorkGroup, localWorkGroup);
					}

					curWidth = srcWidth * (2 << j);
					swapXY = 0;

					OCL_CREATE(clImagesSingle[(i * factorBasePlus1 * 2) + ((j + 1) * 2) + 0], clCreateImage2D, context,
						CL_MEM_READ_WRITE, &clImageFormatSingle, curWidth, curHeight * 2, 0, NULL);
					OCL_CHECK(NNEDI3KernelCall, nnedi3Kernel, clImagesSingle[(i * factorBasePlus1 * 2) + (j * 2) + 1],
						clImagesSingle[(i * factorBasePlus1 * 2) + ((j + 1) * 2) + 0], weightBuf, nns,
						curHeight, curWidth, swapXY, matrix, offset, globalWorkGroup, localWorkGroup);

					curHeight = srcHeight * (2 << j);
				}

				//Correct the center shift on the channel
				curWidthFlt = (float)curWidth;
				curHeightFlt = (float)curHeight;
				shiftLeft = (factor - 1.0f) / -2.0f;
				shiftTop = (factor - 1.0f) / -2.0f;

				OCL_CREATE(clImagesSingle[(i * factorBasePlus1 * 2) + (factorBase * 2) + 1], clCreateImage2D, context,
					CL_MEM_READ_WRITE, &clImageFormatSingle, curWidth, curHeight, 0, NULL);
				OCL_CHECK(Spline36SingleChanKernelCall, spline36SingleChanKernel, clImagesSingle[(i * factorBasePlus1 * 2) + (factorBase * 2) + 0],
					clImagesSingle[(i * factorBasePlus1 * 2) + (factorBase * 2) + 1], curWidthFlt, curHeightFlt,
					shiftLeft, shiftTop, curWidth, curHeight, globalWorkGroup, localWorkGroup, matrix);
			} else {
				//Upscale the Spline36 channels to the same size as the resized NNEDI3 channels
				curWidth = srcWidth * factor;
				curHeight = srcHeight * factor;
				curWidthFlt = (float)srcWidth;
				curHeightFlt = (float)srcHeight;

				//These are guesstimates that try to correct the center shift
				//I know there has a be a more exact equation to do this shift correction,
				//but this will have to do until someone figures it out
				shiftLeft = -0.28f;
				shiftTop = -0.28f;
				for (u32 j = 2; j < factor; j++) {
					shiftLeft += -0.045;
					shiftTop += -0.045;
				}

				OCL_CREATE(clImagesSingle[(i * factorBasePlus1 * 2) + (factorBase * 2) + 1], clCreateImage2D, context,
					CL_MEM_READ_WRITE, &clImageFormatSingle, curWidth, curHeight, 0, NULL);
				OCL_CHECK(Spline36SingleChanKernelCall, spline36SingleChanKernel, clImagesAll[1],
					clImagesSingle[(i * factorBasePlus1 * 2) + (factorBase * 2) + 1], curWidthFlt, curHeightFlt,
					shiftLeft, shiftTop, curWidth, curHeight, globalWorkGroup, localWorkGroup, matrix);
			}
		}

		//Combine the separate Y, UV, and A, and convert back into BGRA
		int is5551 = 0;
		OCL_CREATE(clImagesAll[2], clCreateImage2D, context, CL_MEM_READ_WRITE, &clImageFormatAll, curWidth, curHeight, 0, NULL);

		//Applies for power-of-two factors (2x, 4x)
		if (factor == trueFactor) {
			if (format == GL_UNSIGNED_SHORT_5_5_5_1) {
				is5551 = 1;
			}

			OCL_CHECK(YUVAtoBGRAwithMergeKernelCall, YUVAtoBGRAwithMergeKernel, clImagesSingle[(0 * factorBasePlus1 * 2) + (factorBase * 2) + 1],
				clImagesSingle[(1 * factorBasePlus1 * 2) + (factorBase * 2) + 1], clImagesSingle[(2 * factorBasePlus1 * 2) + (factorBase * 2) + 1],
				clImagesSingle[(3 * factorBasePlus1 * 2) + (factorBase * 2) + 1], clImagesAll[2], curWidth, curHeight, is5551, globalWorkGroup, localWorkGroup);

			OCL_CHECK(ReadOpenCLImageCall, clImagesAll[2], trg, curWidth, curHeight);
		} else { //Applies for non-power-of-two factors (3x, 5x)
			OCL_CHECK(YUVAtoBGRAwithMergeKernelCall, YUVAtoBGRAwithMergeKernel, clImagesSingle[(0 * factorBasePlus1 * 2) + (factorBase * 2) + 1],
				clImagesSingle[(1 * factorBasePlus1 * 2) + (factorBase * 2) + 1], clImagesSingle[(2 * factorBasePlus1 * 2) + (factorBase * 2) + 1],
				clImagesSingle[(3 * factorBasePlus1 * 2) + (factorBase * 2) + 1], clImagesAll[2], curWidth, curHeight, is5551, globalWorkGroup, localWorkGroup);

			curWidthFlt = (float)curWidth;
			curHeightFlt = (float)curHeight;
			shiftLeft = -0.125f;
			shiftTop = -0.125f;
			curWidth = srcWidth * trueFactor;
			curHeight = srcHeight * trueFactor;
			if (format == GL_UNSIGNED_SHORT_5_5_5_1) {
				is5551 = 1;
			}

			OCL_CREATE(clImagesAll[3], clCreateImage2D, context, CL_MEM_READ_WRITE, &clImageFormatAll, curWidth, curHeight, 0, NULL);
			OCL_CHECK(Spline36KernelCall, spline36Kernel, clImagesAll[2], clImagesAll[3], curWidthFlt,
				curHeightFlt, shiftLeft, shiftTop, curWidth, curHeight, is5551, globalWorkGroup, localWorkGroup);
			
			OCL_CHECK(ReadOpenCLImageCall, clImagesAll[3], trg, curWidth, curHeight);
		}

	clean_up:
		for (int i = 0; i < 4; i++) {
			OCL_FREE(clReleaseMemObject, clImagesAll[i]);
		}

		for (u32 i = 0; i < (4 * (factorBasePlus1)* 2); i++) {
			OCL_FREE(clReleaseMemObject, clImagesSingle[i]);
		}
		delete[] clImagesSingle;

		return status;
	}

	cl_int OclScaler::Spline36Scale(u32 factor, u32* src, u32* trg, u32 srcWidth, u32 srcHeight, GLenum format) {
		cl_int status = CL_SUCCESS, memStatus = CL_SUCCESS;

		cl_image_format clImageFormat;
		cl_mem clImages[2] = { NULL }; //[0] = original image, [1] = scaled image

		//Verify everything is setup
		if (!IsActive()) {
			OCL_SILENT_CHECK_NO_ARGS(SetupCtx);
			OCL_SILENT_CHECK_NO_ARGS(SetupSpline36);
		} else {
			if (!spline36Active) {
				OCL_SILENT_CHECK_NO_ARGS(SetupSpline36);
			}
		}

		clImageFormat.image_channel_order = CL_RGBA;
		clImageFormat.image_channel_data_type = CL_UNORM_INT8;

		OCL_CREATE(clImages[0], clCreateImage2D, context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
			&clImageFormat, srcWidth, srcHeight, srcWidth * sizeof(u32), src);

		u32 const resizedWidth = srcWidth * factor;
		u32 const resizedHeight = srcHeight * factor;
		size_t globalWorkGroup[2] = { 0 }, localWorkGroup[2] = { 8, 8 };
		float curWidthFlt = (float)srcWidth;
		float curHeightFlt = (float)srcHeight;

		float shiftLeft = -0.28f;
		float shiftTop = -0.28f;
		for (u32 i = 2; i < factor; i++) {
			shiftLeft += -0.045;
			shiftTop += -0.045;
		}

		int is5551 = 0;
		if (format == GL_UNSIGNED_SHORT_5_5_5_1) {
			is5551 = 1;
		}

		OCL_CREATE(clImages[1], clCreateImage2D, context, CL_MEM_READ_WRITE, &clImageFormat, resizedWidth, resizedHeight, 0, NULL);
		OCL_CHECK(Spline36KernelCall, spline36Kernel, clImages[0], clImages[1], curWidthFlt, curHeightFlt,
			shiftLeft, shiftTop, resizedWidth, resizedHeight, is5551, globalWorkGroup, localWorkGroup);

		OCL_CHECK(ReadOpenCLImageCall, clImages[1], trg, resizedWidth, resizedHeight);

		goto end;

	clean_up:
		//If we hit this it means something went wrong at some point during the scaling
		//We don't need to touch the trg since it comes pre-zeroed out, and this scaling should fail gracefully
	end :
		for (u32 i = 0; i < 2; i++) {
			OCL_FREE(clReleaseMemObject, clImages[i]);
		}

		return status;
	}

	cl_int OclScaler::NNEDI3KernelCall(const cl_kernel& nnedi3Kernel, const cl_mem& input, cl_mem& output, const cl_mem& weightBuffer, size_t nns,
		u32 curWidth, u32 curHeight, int swapXY, const cl_float4 matrix, int offset, size_t globalWorkGroup[], const size_t localWorkGroup[]) {
		cl_int status = CL_SUCCESS;
		
		OCL_CHECK(clSetKernelArg, nnedi3Kernel, 0, sizeof(size_t), &input);
		OCL_CHECK(clSetKernelArg, nnedi3Kernel, 1, sizeof(size_t), &output);
		OCL_CHECK(clSetKernelArg, nnedi3Kernel, 2, sizeof(size_t), &weightBuffer);
		OCL_CHECK(clSetKernelArg, nnedi3Kernel, 3, sizeof(u32), &nns);
		OCL_CHECK(clSetKernelArg, nnedi3Kernel, 4, sizeof(u32), &curHeight);
		OCL_CHECK(clSetKernelArg, nnedi3Kernel, 5, sizeof(u32), &curWidth);
		OCL_CHECK(clSetKernelArg, nnedi3Kernel, 6, sizeof(int), &swapXY);
		OCL_CHECK(clSetKernelArg, nnedi3Kernel, 7, sizeof(cl_float4), &matrix);
		OCL_CHECK(clSetKernelArg, nnedi3Kernel, 8, sizeof(int), &offset);

		globalWorkGroup[0] = RoundUp(curHeight / 8, 8);
		globalWorkGroup[1] = RoundUp(curWidth, 8);

		OCL_CHECK(clEnqueueNDRangeKernel, commandQueue, nnedi3Kernel, 2, NULL, globalWorkGroup, localWorkGroup, NULL, NULL, NULL);

	clean_up:
		return status;
	}

	cl_int OclScaler::Spline36KernelCall(const cl_kernel& spline36Kernel, const cl_mem& input, const cl_mem& output, float curWidthFlt, float curHeightFlt,
		float shiftLeft, float shiftTop, u32 resizedWidth, u32 resizedHeight, int is5551, size_t globalWorkGroup[], const size_t localWorkGroup[]) {
		cl_int status = CL_SUCCESS;

		OCL_CHECK(clSetKernelArg, spline36Kernel, 0, sizeof(size_t), &input);
		OCL_CHECK(clSetKernelArg, spline36Kernel, 1, sizeof(size_t), &output);
		OCL_CHECK(clSetKernelArg, spline36Kernel, 2, sizeof(float), &curWidthFlt);
		OCL_CHECK(clSetKernelArg, spline36Kernel, 3, sizeof(float), &curHeightFlt);
		OCL_CHECK(clSetKernelArg, spline36Kernel, 4, sizeof(u32), &resizedWidth);
		OCL_CHECK(clSetKernelArg, spline36Kernel, 5, sizeof(u32), &resizedHeight);
		OCL_CHECK(clSetKernelArg, spline36Kernel, 6, sizeof(float), &shiftLeft);
		OCL_CHECK(clSetKernelArg, spline36Kernel, 7, sizeof(float), &shiftTop);
		OCL_CHECK(clSetKernelArg, spline36Kernel, 8, sizeof(int), &is5551);

		globalWorkGroup[0] = RoundUp(resizedWidth, 8);
		globalWorkGroup[1] = RoundUp(resizedHeight, 8);

		OCL_CHECK(clEnqueueNDRangeKernel, commandQueue, spline36Kernel, 2, NULL, globalWorkGroup, localWorkGroup, NULL, NULL, NULL);

	clean_up:
		return status;
	}

	cl_int OclScaler::MergeIntoRGBAKernelCall(const cl_kernel& mergeIntoRGBAKernel, const cl_mem& bChan, const cl_mem& gChan, const cl_mem& rChan,
		const cl_mem& aChan, cl_mem& output, u32 curWidth, u32 curHeight, size_t globalWorkGroup[], const size_t localWorkGroup[]) {
		cl_int status = CL_SUCCESS;

		OCL_CHECK(clSetKernelArg, mergeIntoRGBAKernel, 0, sizeof(size_t), &bChan);
		OCL_CHECK(clSetKernelArg, mergeIntoRGBAKernel, 1, sizeof(size_t), &gChan);
		OCL_CHECK(clSetKernelArg, mergeIntoRGBAKernel, 2, sizeof(size_t), &rChan);
		OCL_CHECK(clSetKernelArg, mergeIntoRGBAKernel, 3, sizeof(size_t), &aChan);
		OCL_CHECK(clSetKernelArg, mergeIntoRGBAKernel, 4, sizeof(u32), &curWidth);
		OCL_CHECK(clSetKernelArg, mergeIntoRGBAKernel, 5, sizeof(u32), &curHeight);
		OCL_CHECK(clSetKernelArg, mergeIntoRGBAKernel, 6, sizeof(size_t), &output);

		globalWorkGroup[0] = RoundUp(curWidth, 8);
		globalWorkGroup[1] = RoundUp(curHeight, 8);

		OCL_CHECK(clEnqueueNDRangeKernel, commandQueue, mergeIntoRGBAKernel, 2, NULL, globalWorkGroup, localWorkGroup, NULL, NULL, NULL);

	clean_up:
		return status;
	}

	cl_int OclScaler::BGRAtoYUVAKernelCall(const cl_kernel& BGRAtoYUVAKernel, const cl_mem& input, cl_mem& output,
		u32 curWidth, u32 curHeight, size_t globalWorkGroup[], const size_t localWorkGroup[]) {
		cl_int status = CL_SUCCESS;

		OCL_CHECK(clSetKernelArg, BGRAtoYUVAKernel, 0, sizeof(size_t), &input);
		OCL_CHECK(clSetKernelArg, BGRAtoYUVAKernel, 1, sizeof(u32), &curWidth);
		OCL_CHECK(clSetKernelArg, BGRAtoYUVAKernel, 2, sizeof(u32), &curHeight);
		OCL_CHECK(clSetKernelArg, BGRAtoYUVAKernel, 3, sizeof(size_t), &output);

		globalWorkGroup[0] = RoundUp(curWidth, 8);
		globalWorkGroup[1] = RoundUp(curHeight, 8);

		OCL_CHECK(clEnqueueNDRangeKernel, commandQueue, BGRAtoYUVAKernel, 2, NULL, globalWorkGroup, localWorkGroup, NULL, NULL, NULL);

	clean_up:
		return status;
	}

	cl_int OclScaler::YUVAtoBGRAwithMergeKernelCall(const cl_kernel& YUVAtoBGRAwithMergeKernel, const cl_mem& yChan, const cl_mem& uChan, const cl_mem& vChan,
		const cl_mem& aChan, cl_mem& output, u32 curWidth, u32 curHeight, int is5551, size_t globalWorkGroup[], const size_t localWorkGroup[]) {
		cl_int status = CL_SUCCESS;

		OCL_CHECK(clSetKernelArg, YUVAtoBGRAwithMergeKernel, 0, sizeof(size_t), &yChan);
		OCL_CHECK(clSetKernelArg, YUVAtoBGRAwithMergeKernel, 1, sizeof(size_t), &uChan);
		OCL_CHECK(clSetKernelArg, YUVAtoBGRAwithMergeKernel, 2, sizeof(size_t), &vChan);
		OCL_CHECK(clSetKernelArg, YUVAtoBGRAwithMergeKernel, 3, sizeof(size_t), &aChan);
		OCL_CHECK(clSetKernelArg, YUVAtoBGRAwithMergeKernel, 4, sizeof(u32), &curWidth);
		OCL_CHECK(clSetKernelArg, YUVAtoBGRAwithMergeKernel, 5, sizeof(u32), &curHeight);
		OCL_CHECK(clSetKernelArg, YUVAtoBGRAwithMergeKernel, 6, sizeof(size_t), &output);
		OCL_CHECK(clSetKernelArg, YUVAtoBGRAwithMergeKernel, 7, sizeof(int), &is5551);

		globalWorkGroup[0] = RoundUp(curWidth, 8);
		globalWorkGroup[1] = RoundUp(curHeight, 8);

		OCL_CHECK(clEnqueueNDRangeKernel, commandQueue, YUVAtoBGRAwithMergeKernel, 2, NULL, globalWorkGroup, localWorkGroup, NULL, NULL, NULL);

	clean_up:
		return status;
	}

	cl_int OclScaler::Spline36SingleChanKernelCall(const cl_kernel& spline36Kernel, const cl_mem& input, const cl_mem& output, float curWidthFlt, float curHeightFlt,
		float shiftLeft, float shiftTop, u32 resizedWidth, u32 resizedHeight, size_t globalWorkGroup[], const size_t localWorkGroup[], const cl_float4 matrix) {
		cl_int status = CL_SUCCESS;

		OCL_CHECK(clSetKernelArg, spline36Kernel, 0, sizeof(size_t), &input);
		OCL_CHECK(clSetKernelArg, spline36Kernel, 1, sizeof(size_t), &output);
		OCL_CHECK(clSetKernelArg, spline36Kernel, 2, sizeof(float), &curWidthFlt);
		OCL_CHECK(clSetKernelArg, spline36Kernel, 3, sizeof(float), &curHeightFlt);
		OCL_CHECK(clSetKernelArg, spline36Kernel, 4, sizeof(u32), &resizedWidth);
		OCL_CHECK(clSetKernelArg, spline36Kernel, 5, sizeof(u32), &resizedHeight);
		OCL_CHECK(clSetKernelArg, spline36Kernel, 6, sizeof(float), &shiftLeft);
		OCL_CHECK(clSetKernelArg, spline36Kernel, 7, sizeof(float), &shiftTop);
		OCL_CHECK(clSetKernelArg, spline36Kernel, 8, sizeof(cl_float4), &matrix);

		globalWorkGroup[0] = RoundUp(resizedWidth, 8);
		globalWorkGroup[1] = RoundUp(resizedHeight, 8);

		OCL_CHECK(clEnqueueNDRangeKernel, commandQueue, spline36Kernel, 2, NULL, globalWorkGroup, localWorkGroup, NULL, NULL, NULL);

	clean_up:
		return status;
	}

	cl_int OclScaler::ReadOpenCLImageCall(const cl_mem& input, u32 output[], u32 curWidth, u32 curHeight) {
		cl_int status = CL_SUCCESS;
		size_t const origin[3] = { 0 }, region[3] = { curWidth, curHeight, 1 };

		OCL_CHECK(clEnqueueReadImage, commandQueue, input, CL_TRUE, origin, region, 0, 0, output, 0, NULL, NULL);

	clean_up:
		return status;
	}

	bool OclScaler::IsPowerOfTwo(u32 n) {
		return (n != 0) && ((n & (n - 1)) == 0);
	}

	//This gets us the log 2 base of a factor
	u32 OclScaler::LogTwoBase(u32 n) {
		static const int MultiplyDeBruijnBitPosition2[32] =
		{
			0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
			31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
		};
		return MultiplyDeBruijnBitPosition2[(u32)(n * 0x077CB531U) >> 27];
	}

	u32 OclScaler::PrevPowerOfTwo(u32 n) {
		n = n | (n >> 1);
		n = n | (n >> 2);
		n = n | (n >> 4);
		n = n | (n >> 8);
		n = n | (n >> 16);
		return n - (n >> 1);
	}

	u32 OclScaler::RoundUp(u32 numToRound, u32 multiple) {
		if (numToRound == 0) {
			return multiple;
		}

		if (multiple == 0) {
			return numToRound;
		}

		int remainder = numToRound % multiple;
		if (remainder == 0) {
			return numToRound;
		}

		return numToRound + multiple - remainder;
	}

	//Based on the OpenCL error handling code in an old revision of Dolphin
	void OclScaler::HandleOCLError(cl_int error, const char* func, int line, const char* method) {
		const char* name;
		switch (error) {
	#define CL_ERROR(x) case (x): name = #x; break
			CL_ERROR(CL_SUCCESS);
			CL_ERROR(CL_DEVICE_NOT_FOUND);
			CL_ERROR(CL_DEVICE_NOT_AVAILABLE);
			CL_ERROR(CL_COMPILER_NOT_AVAILABLE);
			CL_ERROR(CL_MEM_OBJECT_ALLOCATION_FAILURE);
			CL_ERROR(CL_OUT_OF_RESOURCES);
			CL_ERROR(CL_OUT_OF_HOST_MEMORY);
			CL_ERROR(CL_PROFILING_INFO_NOT_AVAILABLE);
			CL_ERROR(CL_MEM_COPY_OVERLAP);
			CL_ERROR(CL_IMAGE_FORMAT_MISMATCH);
			CL_ERROR(CL_IMAGE_FORMAT_NOT_SUPPORTED);
			CL_ERROR(CL_BUILD_PROGRAM_FAILURE);
			CL_ERROR(CL_MAP_FAILURE);
			CL_ERROR(CL_MISALIGNED_SUB_BUFFER_OFFSET);
			CL_ERROR(CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST);
			CL_ERROR(CL_COMPILE_PROGRAM_FAILURE);
			CL_ERROR(CL_LINKER_NOT_AVAILABLE);
			CL_ERROR(CL_LINK_PROGRAM_FAILURE);
			CL_ERROR(CL_DEVICE_PARTITION_FAILED);
			CL_ERROR(CL_KERNEL_ARG_INFO_NOT_AVAILABLE);
			CL_ERROR(CL_INVALID_VALUE);
			CL_ERROR(CL_INVALID_DEVICE_TYPE);
			CL_ERROR(CL_INVALID_PLATFORM);
			CL_ERROR(CL_INVALID_DEVICE);
			CL_ERROR(CL_INVALID_CONTEXT);
			CL_ERROR(CL_INVALID_QUEUE_PROPERTIES);
			CL_ERROR(CL_INVALID_COMMAND_QUEUE);
			CL_ERROR(CL_INVALID_HOST_PTR);
			CL_ERROR(CL_INVALID_MEM_OBJECT);
			CL_ERROR(CL_INVALID_IMAGE_FORMAT_DESCRIPTOR);
			CL_ERROR(CL_INVALID_IMAGE_SIZE);
			CL_ERROR(CL_INVALID_SAMPLER);
			CL_ERROR(CL_INVALID_BINARY);
			CL_ERROR(CL_INVALID_BUILD_OPTIONS);
			CL_ERROR(CL_INVALID_PROGRAM);
			CL_ERROR(CL_INVALID_PROGRAM_EXECUTABLE);
			CL_ERROR(CL_INVALID_KERNEL_NAME);
			CL_ERROR(CL_INVALID_KERNEL_DEFINITION);
			CL_ERROR(CL_INVALID_KERNEL);
			CL_ERROR(CL_INVALID_ARG_INDEX);
			CL_ERROR(CL_INVALID_ARG_VALUE);
			CL_ERROR(CL_INVALID_ARG_SIZE);
			CL_ERROR(CL_INVALID_KERNEL_ARGS);
			CL_ERROR(CL_INVALID_WORK_DIMENSION);
			CL_ERROR(CL_INVALID_WORK_GROUP_SIZE);
			CL_ERROR(CL_INVALID_WORK_ITEM_SIZE);
			CL_ERROR(CL_INVALID_GLOBAL_OFFSET);
			CL_ERROR(CL_INVALID_EVENT_WAIT_LIST);
			CL_ERROR(CL_INVALID_EVENT);
			CL_ERROR(CL_INVALID_OPERATION);
			CL_ERROR(CL_INVALID_GL_OBJECT);
			CL_ERROR(CL_INVALID_BUFFER_SIZE);
			CL_ERROR(CL_INVALID_MIP_LEVEL);
			CL_ERROR(CL_INVALID_GLOBAL_WORK_SIZE);
			CL_ERROR(CL_INVALID_PROPERTY);
			CL_ERROR(CL_INVALID_IMAGE_DESCRIPTOR);
			CL_ERROR(CL_INVALID_COMPILER_OPTIONS);
			CL_ERROR(CL_INVALID_LINKER_OPTIONS);
			CL_ERROR(CL_INVALID_DEVICE_PARTITION_COUNT);
	#undef CL_ERROR
		default:
			name = "Unknown error code";
		}
		ERROR_LOG(G3D, "OpenCL error in function %s at line %d with method %s: %s (%d)", func, line, method, name, error);
	}

#undef GENERAL_ERROR
#undef OCL_CREATE
#undef OCL_FREE
#undef OCL_CHECK
#undef OCL_SILENT_CHECK
#undef OCL_SILENT_CHECK_NO_ARGS
#undef OCL_NEXT_ITER_ON_FAIL
}
