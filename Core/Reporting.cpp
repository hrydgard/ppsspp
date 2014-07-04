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
#include "Core/CwCheat.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/ELF/ParamSFO.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "GPU/GLES/Framebuffer.h"

#ifndef _XBOX
#include "net/http_client.h"
#include "net/resolve.h"
#include "net/url.h"
#endif

#include "base/buffer.h"
#include "thread/thread.h"
#include "file/zip_read.h"

#include <set>
#include <stdlib.h>
#include <cstdarg>

#ifdef _XBOX
namespace Reporting
{
	bool IsEnabled() { return false;}
	void ReportMessage(const char *message, ...) { }
}
#else
namespace Reporting
{
	const int DEFAULT_PORT = 80;
	const u32 SPAM_LIMIT = 100;
	const int PAYLOAD_BUFFER_SIZE = 100;

	// Internal limiter on number of requests per instance.
	static u32 spamProtectionCount = 0;
	// Temporarily stores a reference to the hostname.
	static std::string lastHostname;
	// Keeps track of report-only-once identifiers.  Since they're always constants, a pointer is okay.
	static std::set<const char *> logOnceUsed;
	// Keeps track of whether a harmful setting was ever used.
	static bool everUnsupported = false;
	// Support is cached here to avoid checking it on every single request.
	static bool currentSupported = false;

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
#elif defined(MAEMO)
		return "Nokia Maemo";
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
		currentSupported = IsSupported();
	}

	void Shutdown()
	{
		// Just so it can be enabled in the menu again.
		Init();
	}

	void DoState(PointerWrap &p)
	{
		const int LATEST_VERSION = 1;
		auto s = p.Section("Reporting", 0, LATEST_VERSION);
		if (!s || s < LATEST_VERSION) {
			// Don't report from old savestates, they may "entomb" bugs.
			everUnsupported = true;
			return;
		}

		p.Do(everUnsupported);
	}

	void UpdateConfig()
	{
		currentSupported = IsSupported();
		if (!currentSupported && PSP_IsInited())
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

		g_Config.GetReportingInfo(postdata);
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
		if (g_Config.bTimerHack)
			return false;
		if (CheatsInEffect())
			return false;
		// Not sure if we should support locked cpu at all, but definitely not far out values.
		if (g_Config.iLockedCPUSpeed != 0 && (g_Config.iLockedCPUSpeed < 111 || g_Config.iLockedCPUSpeed > 333))
			return false;

		// Some users run the exe from a zip or something, and don't have fonts.
		// This breaks things, but let's not report it since it's confusing.
#if defined(USING_WIN_UI) || defined(APPLE)
		if (!File::Exists(g_Config.flash0Directory + "/font/jpn0.pgf"))
			return false;
#else
		FileInfo fo;
		if (!VFSGetFileInfo("flash0/font/jpn0.pgf", &fo))
			return false;
#endif

		return !everUnsupported;
	}

	bool IsEnabled()
	{
		if (g_Config.sReportHost.empty() || (!currentSupported && PSP_IsInited()))
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

#endif
