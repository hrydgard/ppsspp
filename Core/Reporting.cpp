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

#include "Core/Config.h"
#include "Core/System.h"

#include "net/http_client.h"
#include "net/resolve.h"
#include "base/buffer.h"

#include <stdlib.h>
#include <string>

namespace Reporting
{
	const int DEFAULT_PORT = 80;

	static size_t ServerHostnameLength()
	{
		if (g_Config.sReportHost.empty())
			return g_Config.sReportHost.npos;

		// IPv6 literal?
		if (g_Config.sReportHost[0] == '[')
		{
			size_t length = g_Config.sReportHost.find("]:");
			if (length != g_Config.sReportHost.npos)
				++length;
			return length;
		}
		else
			return g_Config.sReportHost.find(':');
	}

	static std::string lastHostname;
	static const char *ServerHostname()
	{
		if (g_Config.sReportHost.empty())
			return NULL;
		// Disabled by default for now.
		if (g_Config.sReportHost.compare("default") == 0)
			return NULL;

		size_t length = ServerHostnameLength();
		if (length == g_Config.sReportHost.npos)
			return g_Config.sReportHost.c_str();

		lastHostname = g_Config.sReportHost.substr(0, length);
		return lastHostname.c_str();
	}

	static int ServerPort()
	{
		if (g_Config.sReportHost.empty())
			return 0;
		// Disabled by default for now.
		if (g_Config.sReportHost.compare("default") == 0)
			return 0;

		size_t offset = ServerHostnameLength();
		if (offset == g_Config.sReportHost.npos)
			return DEFAULT_PORT;

		// Skip the colon.
		std::string port = g_Config.sReportHost.substr(offset + 1);
		return atoi(port.c_str());
	}

	void ReportMessage(const char *message, ...)
	{
		if (g_Config.sReportHost.empty())
			return;
		// Disabled by default for now.
		if (g_Config.sReportHost.compare("default") == 0)
			return;

		// TODO: Use a thread, enable on non-Windows.
#ifdef _MSC_VER
		char temp[32768];
		char *tempPos = &temp[0];

		// TODO: application/x-www-urlencoded?  Need to escape.
		tempPos += sprintf(tempPos, "version=%s&game=%s_%s&message=",
			PPSSPP_GIT_VERSION,
			g_paramSFO.GetValueString("DISC_ID").c_str(),
			g_paramSFO.GetValueString("DISC_VERSION").c_str());

		va_list args;
		va_start(args, message);
		vsprintf(tempPos, message, args);
		va_end(args);

		Buffer output;
		http::Client http;
		net::Init();
		if (http.Resolve(ServerHostname(), ServerPort()))
		{
			http.Connect();
			http.POST("/report/message", temp, &output);
			http.Disconnect();
		}
		net::Shutdown();
#endif
	}

}