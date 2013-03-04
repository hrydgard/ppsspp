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

#include "Common/StdThread.h"
#include "Core/Config.h"
#include "Core/System.h"

#include "net/http_client.h"
#include "net/resolve.h"
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
			http.POST("/report/message", data, "application/x-www-urlencoded", output);
			http.Disconnect();
			result = true;
		}
		net::Shutdown();

		return result;
	}

	int Process(int pos)
	{
		Payload &payload = payloadBuffer[pos];

		const int PARAM_BUFFER_SIZE = 4096;
		char temp[PARAM_BUFFER_SIZE];

		// TODO: Need to escape these values, add more.
		snprintf(temp, PARAM_BUFFER_SIZE - 1, "version=%s&game=%s_%s",
			PPSSPP_GIT_VERSION,
			g_paramSFO.GetValueString("DISC_ID").c_str(),
			g_paramSFO.GetValueString("DISC_VERSION").c_str());

		std::string data;
		switch (payload.type)
		{
		case MESSAGE:
			// TODO: Escape.
			data = std::string(temp) + "&message=" + payload.string1 + "&value=" + payload.string2;
			payload.string1.clear();
			payload.string2.clear();

			SendReportRequest("/report/message", data);
			break;
		}

		return 0;
	}

	void ReportMessage(const char *message, ...)
	{
		if (g_Config.sReportHost.empty() || CheckSpamLimited())
			return;
		// Disabled by default for now.
		if (g_Config.sReportHost.compare("default") == 0)
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