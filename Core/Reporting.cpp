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
#include "Common/StdThread.h"
#include "Core/CoreTiming.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/HLE/sceKernelMemory.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"

#include "net/http_client.h"
#include "net/resolve.h"
#include "net/url.h"

#include "base/buffer.h"

#include <stdlib.h>
#include <string>
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
		std::string host = ServerHost();
		if (host[0] == '[')
		{
			size_t length = host.find("]:");
			if (length != host.npos)
				++length;
			return length;
		}
		else
			return host.find(':');
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
		http::Client http;
		Buffer theVoid;

		if (output == NULL)
			output = &theVoid;

		net::Init();
		if (http.Resolve(ServerHostname(), ServerPort()))
		{
			http.Connect();
			http.POST("/report/message", data, "application/x-www-form-urlencoded", output);
			http.Disconnect();
			result = true;
		}
		net::Shutdown();

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

	int Process(int pos)
	{
		Payload &payload = payloadBuffer[pos];

		std::string gpuPrimary, gpuFull;
		if (gpu)
			gpu->GetReportingInfo(gpuPrimary, gpuFull);

		UrlEncoder postdata;
		postdata.Add("version", PPSSPP_GIT_VERSION);
		// TODO: Maybe ParamSFOData shouldn't include nulls in std::strings?  Don't work to break savedata, though...
		postdata.Add("game", StripTrailingNull(g_paramSFO.GetValueString("DISC_ID")) + "_" + StripTrailingNull(g_paramSFO.GetValueString("DISC_VERSION")));
		postdata.Add("game_title", StripTrailingNull(g_paramSFO.GetValueString("TITLE")));
		postdata.Add("gpu", gpuPrimary);
		postdata.Add("gpu_full", gpuFull);
		postdata.Add("cpu", cpu_info.Summarize());
		postdata.Add("platform", GetPlatformIdentifer());
		postdata.Add("sdkver", sceKernelGetCompiledSdkVersion());
		postdata.Add("pixel_width", PSP_CoreParameter().pixelWidth);
		postdata.Add("pixel_height", PSP_CoreParameter().pixelHeight);
		postdata.Add("ticks", (const uint64_t)CoreTiming::GetTicks());

		if (g_Config.iShowFPSCounter)
		{
			float vps, fps;
			__DisplayGetAveragedFPS(&vps, &fps);
			postdata.Add("vps", vps);
		}

		// TODO: Settings, savestate/savedata status, some measure of speed/fps?

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

	bool IsEnabled()
	{
		if (g_Config.sReportHost.empty())
			return false;
		// Disabled by default for now.
		if (g_Config.sReportHost.compare("default") == 0)
			return false;
		return true;
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
