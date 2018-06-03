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

#include <thread>
#include <mutex>
#include <condition_variable>

#include "Core/Reporting.h"

#include "Common/CPUDetect.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/Config.h"
#include "Core/CwCheat.h"
#include "Core/Loaders.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "Core/FileSystems/BlockDevices.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/ELF/ParamSFO.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "net/http_client.h"
#include "net/resolve.h"
#include "net/url.h"

#include "base/stringutil.h"
#include "base/buffer.h"
#include "thread/threadutil.h"
#include "file/zip_read.h"

#include <set>
#include <stdlib.h>
#include <cstdarg>

namespace Reporting
{
	const int DEFAULT_PORT = 80;
	const u32 SPAM_LIMIT = 100;
	const int PAYLOAD_BUFFER_SIZE = 200;

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
	// Whether the most recent server request seemed successful.
	static bool serverWorking = true;
	// The latest compatibility result from the server.
	static std::vector<std::string> lastCompatResult;

	enum class RequestType
	{
		NONE,
		MESSAGE,
		COMPAT,
	};

	struct Payload
	{
		RequestType type;
		std::string string1;
		std::string string2;
		int int1;
		int int2;
		int int3;
	};
	static Payload payloadBuffer[PAYLOAD_BUFFER_SIZE];
	static int payloadBufferPos = 0;

	static std::mutex crcLock;
	static std::condition_variable crcCond;
	static std::string crcFilename;
	static std::map<std::string, u32> crcResults;

	static int CalculateCRCThread() {
		setCurrentThreadName("ReportCRC");

		// TODO: Use the blockDevice from pspFileSystem?
		FileLoader *fileLoader = ConstructFileLoader(crcFilename);
		BlockDevice *blockDevice = constructBlockDevice(fileLoader);

		u32 crc = 0;
		if (blockDevice) {
			crc = blockDevice->CalculateCRC();
		}

		delete blockDevice;
		delete fileLoader;

		std::lock_guard<std::mutex> guard(crcLock);
		crcResults[crcFilename] = crc;
		crcCond.notify_one();

		return 0;
	}

	void QueueCRC() {
		std::lock_guard<std::mutex> guard(crcLock);

		const std::string &gamePath = PSP_CoreParameter().fileToStart;
		auto it = crcResults.find(gamePath);
		if (it != crcResults.end()) {
			// Nothing to do, we've already calculated it.
			// Note: we assume it stays static until the app is closed.
			return;
		}

		if (crcFilename == gamePath) {
			// Already in process.
			return;
		}

		crcFilename = gamePath;
		std::thread th(CalculateCRCThread);
		th.detach();
	}

	u32 RetrieveCRC() {
		const std::string &gamePath = PSP_CoreParameter().fileToStart;
		QueueCRC();

		std::unique_lock<std::mutex> guard(crcLock);
		auto it = crcResults.find(gamePath);
		while (it == crcResults.end()) {
			crcCond.wait(guard);
			it = crcResults.find(gamePath);
		}

		return it->second;
	}

	// Returns the full host (e.g. report.ppsspp.org:80.)
	std::string ServerHost()
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

	bool SendReportRequest(const char *uri, const std::string &data, const std::string &mimeType, Buffer *output = NULL)
	{
		bool result = false;
		http::Client http;
		Buffer theVoid;

		if (output == NULL)
			output = &theVoid;

		const char *serverHost = ServerHostname();
		if (!serverHost)
			return false;

		if (http.Resolve(serverHost, ServerPort())) {
			http.Connect();
			int result = http.POST(uri, data, mimeType, output);
			http.Disconnect();

			return result >= 200 && result < 300;
		} else {
			return false;
		}
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
#if defined(__ANDROID__)
		return "Android";
#elif defined(_WIN64)
		return "Windows 64";
#elif defined(_WIN32)
		return "Windows";
#elif defined(IOS)
		return "iOS";
#elif defined(__APPLE__)
		return "Mac";
#elif defined(LOONGSON)
		return "Loongson";
#elif defined(__linux__)
		return "Linux";
#elif defined(__Bitrig__)
		return "Bitrig";
#elif defined(__DragonFly__)
		return "DragonFly";
#elif defined(__FreeBSD__)
		return "FreeBSD";
#elif defined(__FreeBSD_kernel__) && defined(__GLIBC__)
		return "GNU/kFreeBSD";
#elif defined(__NetBSD__)
		return "NetBSD";
#elif defined(__OpenBSD__)
		return "OpenBSD";
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

	std::string CurrentGameID()
	{
		// TODO: Maybe ParamSFOData shouldn't include nulls in std::strings?  Don't work to break savedata, though...
		const std::string disc_id = StripTrailingNull(g_paramSFO.GetValueString("DISC_ID"));
		const std::string disc_version = StripTrailingNull(g_paramSFO.GetValueString("DISC_VERSION"));
		return disc_id + "_" + disc_version;
	}

	void AddGameInfo(UrlEncoder &postdata)
	{
		postdata.Add("game", CurrentGameID());
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

		float vps, fps;
		__DisplayGetAveragedFPS(&vps, &fps);
		postdata.Add("vps", vps);
		postdata.Add("fps", fps);

		postdata.Add("savestate_used", SaveState::HasLoadedState());
	}

	void AddScreenshotData(MultipartFormDataEncoder &postdata, std::string filename)
	{
		std::string data;
		if (!filename.empty() && readFileToString(false, filename.c_str(), data))
			postdata.Add("screenshot", data, "screenshot.jpg", "image/jpeg");

		const std::string iconFilename = "disc0:/PSP_GAME/ICON0.PNG";
		std::vector<u8> iconData;
		if (pspFileSystem.ReadEntireFile(iconFilename, iconData) >= 0) {
			postdata.Add("icon", iconData, "icon.png", "image/png");
		}
	}

	int Process(int pos)
	{
		setCurrentThreadName("Report");

		Payload &payload = payloadBuffer[pos];
		Buffer output;

		MultipartFormDataEncoder postdata;
		AddSystemInfo(postdata);
		AddGameInfo(postdata);
		AddConfigInfo(postdata);
		AddGameplayInfo(postdata);

		switch (payload.type)
		{
		case RequestType::MESSAGE:
			// TODO: Add CRC?
			postdata.Add("message", payload.string1);
			postdata.Add("value", payload.string2);
			// We tend to get corrupted data, this acts as a very primitive verification check.
			postdata.Add("verify", payload.string1 + payload.string2);
			payload.string1.clear();
			payload.string2.clear();

			postdata.Finish();
			serverWorking = true;
			if (!SendReportRequest("/report/message", postdata.ToString(), postdata.GetMimeType()))
				serverWorking = false;
			break;

		case RequestType::COMPAT:
			postdata.Add("compat", payload.string1);
			// We tend to get corrupted data, this acts as a very primitive verification check.
			postdata.Add("verify", payload.string1);
			postdata.Add("graphics", StringFromFormat("%d", payload.int1));
			postdata.Add("speed", StringFromFormat("%d", payload.int2));
			postdata.Add("gameplay", StringFromFormat("%d", payload.int3));
			postdata.Add("crc", StringFromFormat("%08x", Core_GetPowerSaving() ? 0 : RetrieveCRC()));
			postdata.Add("suggestions", payload.string1 != "perfect" && payload.string1 != "playable" ? "1" : "0");
			AddScreenshotData(postdata, payload.string2);
			payload.string1.clear();
			payload.string2.clear();

			postdata.Finish();
			serverWorking = true;
			if (!SendReportRequest("/report/compat", postdata.ToString(), postdata.GetMimeType(), &output)) {
				serverWorking = false;
			} else {
				std::string result;
				output.TakeAll(&result);

				lastCompatResult.clear();
				if (result.empty() || result[0] == '0')
					serverWorking = false;
				else if (result[0] != '1')
					SplitString(result, '\n', lastCompatResult);
			}
			break;

		case RequestType::NONE:
			break;
		}

		payload.type = RequestType::NONE;

		return 0;
	}

	bool IsSupported()
	{
		// Disabled when using certain hacks, because they make for poor reports.
		if (g_Config.bTimerHack)
			return false;
		if (CheatsInEffect())
			return false;
		if (g_Config.iLockedCPUSpeed != 0)
			return false;
		// Don't allow builds without version info from git.  They're useless for reporting.
		if (strcmp(PPSSPP_GIT_VERSION, "unknown") == 0)
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

	bool Enable(bool flag, std::string host)
	{
		if (IsSupported() && IsEnabled() != flag)
		{
			// "" means explicitly disabled.  Don't ever turn on by default.
			// "default" means it's okay to turn it on by default.
			g_Config.sReportHost = flag ? host : "";
			return true;
		}
		return false;
	}

	void EnableDefault()
	{
		g_Config.sReportHost = "default";
	}

	ReportStatus GetStatus()
	{
		if (!serverWorking)
			return ReportStatus::FAILING;

		for (int pos = 0; pos < PAYLOAD_BUFFER_SIZE; ++pos)
		{
			if (payloadBuffer[pos].type != RequestType::NONE)
				return ReportStatus::BUSY;
		}

		return ReportStatus::WORKING;
	}

	int NextFreePos()
	{
		int start = payloadBufferPos % PAYLOAD_BUFFER_SIZE;
		do
		{
			int pos = payloadBufferPos++ % PAYLOAD_BUFFER_SIZE;
			if (payloadBuffer[pos].type == RequestType::NONE)
				return pos;
		}
		while (payloadBufferPos != start);

		return -1;
	}

	void ReportMessage(const char *message, ...)
	{
		if (!IsEnabled() || CheckSpamLimited())
			return;
		int pos = NextFreePos();
		if (pos == -1)
			return;

		const int MESSAGE_BUFFER_SIZE = 65536;
		char temp[MESSAGE_BUFFER_SIZE];

		va_list args;
		va_start(args, message);
		vsnprintf(temp, MESSAGE_BUFFER_SIZE - 1, message, args);
		temp[MESSAGE_BUFFER_SIZE - 1] = '\0';
		va_end(args);

		Payload &payload = payloadBuffer[pos];
		payload.type = RequestType::MESSAGE;
		payload.string1 = message;
		payload.string2 = temp;

		std::thread th(Process, pos);
		th.detach();
	}

	void ReportMessageFormatted(const char *message, const char *formatted)
	{
		if (!IsEnabled() || CheckSpamLimited())
			return;
		int pos = NextFreePos();
		if (pos == -1)
			return;

		Payload &payload = payloadBuffer[pos];
		payload.type = RequestType::MESSAGE;
		payload.string1 = message;
		payload.string2 = formatted;

		std::thread th(Process, pos);
		th.detach();
	}

	void ReportCompatibility(const char *compat, int graphics, int speed, int gameplay, const std::string &screenshotFilename)
	{
		if (!IsEnabled())
			return;
		int pos = NextFreePos();
		if (pos == -1)
			return;

		Payload &payload = payloadBuffer[pos];
		payload.type = RequestType::COMPAT;
		payload.string1 = compat;
		payload.string2 = screenshotFilename;
		payload.int1 = graphics;
		payload.int2 = speed;
		payload.int3 = gameplay;

		std::thread th(Process, pos);
		th.detach();
	}

	std::vector<std::string> CompatibilitySuggestions() {
		return lastCompatResult;
	}
}
