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

#include "Core/Reporting.h"

#include "Common/CPUDetect.h"
#include "Core/CoreTiming.h"
#include "Core/Config.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/ELF/ParamSFO.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "GPU/GLES/Framebuffer.h"

#include "net/http_client.h"
#include "net/resolve.h"
#include "net/url.h"

#include "base/buffer.h"
#include "thread/thread.h"

#include <set>
#include <stdlib.h>
#include <cstdarg>

namespace Reporting
{
	const int DEFAULT_PORT = 80;
	const u32 SPAM_LIMIT = 100;
	const int PAYLOAD_BUFFER_SIZE = 100;

	// Internal limiter on number of requests per instance.
	static u32 spamProtectionCount = 0;
	// Temporarily stores a reference to the hostname.
	static std::string lastHostname;
	// Keeps track of report-only-once identifiers.
	static std::set<std::string> logOnceUsed;
	// Keeps track of whether a harmful setting was ever used.
	static bool everUnsupported = false;

	enum RequestType
	{
		MESSAGE,
	};

	struct Payload
	{
		RequestType type;
		std::string string1;
		std::string string2;
	};
	static Payload payloadBuffer[PAYLOAD_BUFFER_SIZE];
	static int payloadBufferPos = 0;

	// Returns the full host (e.g. report.ppsspp.org:80.)
	inline std::string ServerHost()
	{
		if (g_Config.sReportHost.compare("default") == 0)
			return "";
		return g_Config.sReportHost;
	}

	// Returns the length of the hostname part (e.g. before the :80.)
	static size_t ServerHostnameLength()
	{
		if (!IsEnabled())
			return g_Config.sReportHost.npos;

		// IPv6 literal?
		std::string hostString = ServerHost();
		if (hostString[0] == '[')
		{
			size_t length = hostString.find("]:");
			if (length != hostString.npos)
				++length;
			return length;
		}
		else
			return hostString.find(':');
	}

	// Returns only the hostname part (e.g. "report.ppsspp.org".)
	static const char *ServerHostname()
	{
		if (!IsEnabled())
			return NULL;

		std::string host = ServerHost();
		size_t length = ServerHostnameLength();

		// This means there's no port number - it's already the hostname.
		if (length == host.npos)
			lastHostname = host;
		else
			lastHostname = host.substr(0, length);
		return lastHostname.c_str();
	}

	// Returns only the port part (e.g. 80) as an int.
	static int ServerPort()
	{
		if (!IsEnabled())
			return 0;

		std::string host = ServerHost();
		size_t offset = ServerHostnameLength();
		// If there's no port, use the default one.
		if (offset == host.npos)
			return DEFAULT_PORT;

		// Skip the colon.
		std::string port = host.substr(offset + 1);
		return atoi(port.c_str());
	}

	// Should only be called once per request.
	bool CheckSpamLimited()
	{
		return ++spamProtectionCount >= SPAM_LIMIT;
	}

	bool SendReportRequest(const char *uri, const std::string &data, Buffer *output = NULL)
	{
		bool result = false;
		net::AutoInit netInit;
		http::Client http;
		Buffer theVoid;

		if (output == NULL)
			output = &theVoid;

		if (http.Resolve(ServerHostname(), ServerPort()))
		{
			http.Connect();
			http.POST("/report/message", data, "application/x-www-form-urlencoded", output);
			http.Disconnect();
			result = true;
		}

		return result;
	}

	std::string StripTrailingNull(const std::string &str)
	{
		size_t pos = str.find_first_of('\0');
		if (pos != str.npos)
			return str.substr(0, pos);
		return str;
	}

	std::string GetPlatformIdentifer()
	{
		// TODO: Do we care about OS version?
#if defined(ANDROID)
		return "Android";
#elif defined(_WIN64)
		return "Windows 64";
#elif defined(_WIN32)
		return "Windows";
#elif defined(IOS)
		return "iOS";
#elif defined(__APPLE__)
		return "Mac";
#elif defined(__SYMBIAN32__)
		return "Symbian";
#elif defined(__FreeBSD__)
		return "BSD";
#elif defined(BLACKBERRY)
		return "Blackberry";
#elif defined(LOONGSON)
		return "Loongson";
#elif defined(MEEGO_EDITION_HARMATTAN)
		return "Nokia N9/N950";
#elif defined(__linux__)
		return "Linux";
#else
		return "Unknown";
#endif
	}

	void Init()
	{
		// New game, clean slate.
		spamProtectionCount = 0;
		logOnceUsed.clear();
		everUnsupported = false;
	}

	void UpdateConfig()
	{
		if (!IsSupported())
			everUnsupported = true;
	}

	bool ShouldLogOnce(const char *identifier)
	{
		// True if it wasn't there already -> so yes, log.
		return logOnceUsed.insert(identifier).second;
	}

	void AddGameInfo(UrlEncoder &postdata)
	{
		// TODO: Maybe ParamSFOData shouldn't include nulls in std::strings?  Don't work to break savedata, though...
		postdata.Add("game", StripTrailingNull(g_paramSFO.GetValueString("DISC_ID")) + "_" + StripTrailingNull(g_paramSFO.GetValueString("DISC_VERSION")));
		postdata.Add("game_title", StripTrailingNull(g_paramSFO.GetValueString("TITLE")));
		postdata.Add("sdkver", sceKernelGetCompiledSdkVersion());
	}

	void AddSystemInfo(UrlEncoder &postdata)
	{
		std::string gpuPrimary, gpuFull;
		if (gpu)
			gpu->GetReportingInfo(gpuPrimary, gpuFull);
		
		postdata.Add("version", PPSSPP_GIT_VERSION);
		postdata.Add("gpu", gpuPrimary);
		postdata.Add("gpu_full", gpuFull);
		postdata.Add("cpu", cpu_info.Summarize());
		postdata.Add("platform", GetPlatformIdentifer());
	}

	void AddConfigInfo(UrlEncoder &postdata)
	{
		postdata.Add("pixel_width", PSP_CoreParameter().pixelWidth);
		postdata.Add("pixel_height", PSP_CoreParameter().pixelHeight);

		postdata.Add("config.sys.jit", g_Config.bJit);
		postdata.Add("config.sys.cpu_thread", g_Config.bSeparateCPUThread);
		postdata.Add("config.sys.io_thread", g_Config.bSeparateIOThread);
		postdata.Add("config.sys.locked_cpu", g_Config.iLockedCPUSpeed);
		postdata.Add("config.sys.cheats", g_Config.bEnableCheats);
		postdata.Add("config.sys.lang", g_Config.iLanguage);
		postdata.Add("config.sys.encrypt_save", g_Config.bEncryptSave);
		postdata.Add("config.sys.psp_model", g_Config.iPSPModel);
		postdata.Add("config.sys.firmware_ver", g_Config.iFirmwareVersion);
		postdata.Add("config.gpu.swrast", g_Config.bSoftwareRendering);
		postdata.Add("config.gpu.hw_transform", g_Config.bHardwareTransform);
		postdata.Add("config.gpu.sw_skinning", g_Config.bSoftwareSkinning);
		postdata.Add("config.gpu.rendering_mode", g_Config.iRenderingMode);
		postdata.Add("config.gpu.tex_filtering", g_Config.iTexFiltering);
		postdata.Add("config.gpu.frameskip", g_Config.iFrameSkip);
		postdata.Add("config.gpu.autoskip", g_Config.bAutoFrameSkip);
		postdata.Add("config.gpu.vertex_cache", g_Config.bVertexCache);
		postdata.Add("config.gpu.tex_backoff_cache", g_Config.bTextureBackoffCache);
		postdata.Add("config.gpu.tex_second_cache", g_Config.bTextureSecondaryCache);
		postdata.Add("config.gpu.vertex_jit", g_Config.bVertexDecoderJit);
		postdata.Add("config.gpu.internal_res", g_Config.iInternalResolution);
		postdata.Add("config.gpu.mipmap", g_Config.bMipMap);
		postdata.Add("config.gpu.tex_scaling_level", g_Config.iTexScalingLevel);
		postdata.Add("config.gpu.tex_scaling_type", g_Config.iTexScalingType);
		postdata.Add("config.gpu.tex_deposterize", g_Config.bTexDeposterize);
		postdata.Add("config.gpu.fps_limit", g_Config.iFpsLimit);
		postdata.Add("config.gpu.force_max_fps", g_Config.iForceMaxEmulatedFPS);
		postdata.Add("config.gpu.lowq_spline_bezier", g_Config.bLowQualitySplineBezier);
		postdata.Add("config.hack.disable_stencil", g_Config.bDisableStencilTest);
		postdata.Add("config.hack.always_depth_write", g_Config.bAlwaysDepthWrite);
		postdata.Add("config.hack.timer_hack", g_Config.bTimerHack);
		postdata.Add("config.hack.disable_alpha", g_Config.bDisableAlphaTest);
		postdata.Add("config.hack.prescale_uv", g_Config.bPrescaleUV);
		postdata.Add("config.hack.discard_regs", g_Config.bDiscardRegsOnJRRA);
		postdata.Add("config.hack.skip_deadbeef", g_Config.bSkipDeadbeefFilling);
		postdata.Add("config.hack.func_hash_map", g_Config.bFuncHashMap);
		postdata.Add("config.audio.low_latency", g_Config.bLowLatencyAudio);
		postdata.Add("config.net.enable_wlan", g_Config.bEnableWlan);
	}

	void AddGameplayInfo(UrlEncoder &postdata)
	{
		// Just to get an idea of how long they played.
		postdata.Add("ticks", (const uint64_t)CoreTiming::GetTicks());

		if (g_Config.iShowFPSCounter && g_Config.iShowFPSCounter < 4)
		{
			float vps, fps;
			__DisplayGetAveragedFPS(&vps, &fps);
			postdata.Add("vps", vps);
			postdata.Add("fps", fps);
		}

		postdata.Add("savestate_used", SaveState::HasLoadedState());
	}

	int Process(int pos)
	{
		Payload &payload = payloadBuffer[pos];

		UrlEncoder postdata;
		AddSystemInfo(postdata);
		AddGameInfo(postdata);
		AddConfigInfo(postdata);
		AddGameplayInfo(postdata);

		switch (payload.type)
		{
		case MESSAGE:
			postdata.Add("message", payload.string1);
			postdata.Add("value", payload.string2);
			payload.string1.clear();
			payload.string2.clear();

			SendReportRequest("/report/message", postdata.ToString());
			break;
		}

		return 0;
	}

	bool IsSupported()
	{
		// Disabled when using certain hacks, because they make for poor reports.
		if (g_Config.iRenderingMode >= FBO_READFBOMEMORY_MIN)
			return false;

		// Some users run the exe from a zip or something, and don't have fonts.
		// This breaks things, but let's not report it since it's confusing.
		if (!pspFileSystem.GetFileInfo("flash0:/font").exists)
			return false;

		return true;
	}

	bool IsEnabled()
	{
		if (g_Config.sReportHost.empty() || !IsSupported() || everUnsupported)
			return false;
		// Disabled by default for now.
		if (g_Config.sReportHost.compare("default") == 0)
			return false;
		return true;
	}

	void Enable(bool flag, std::string host)
	{
		if (IsSupported() && IsEnabled() != flag)
		{
			// "" means explicitly disabled.  Don't ever turn on by default.
			// "default" means it's okay to turn it on by default.
			g_Config.sReportHost = flag ? host : "";
		}
	}

	void EnableDefault()
	{
		g_Config.sReportHost = "default";
	}

	void ReportMessage(const char *message, ...)
	{
		if (!IsEnabled() || CheckSpamLimited())
			return;

		const int MESSAGE_BUFFER_SIZE = 32768;
		char temp[MESSAGE_BUFFER_SIZE];

		va_list args;
		va_start(args, message);
		vsnprintf(temp, MESSAGE_BUFFER_SIZE - 1, message, args);
		temp[MESSAGE_BUFFER_SIZE - 1] = '\0';
		va_end(args);

		int pos = payloadBufferPos++ % PAYLOAD_BUFFER_SIZE;
		Payload &payload = payloadBuffer[pos];
		payload.type = MESSAGE;
		payload.string1 = message;
		payload.string2 = temp;

		std::thread th(Process, pos);
		th.detach();
	}

}
