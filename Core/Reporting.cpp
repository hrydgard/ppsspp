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

#include "ppsspp_config.h"

#include <deque>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <set>
#include <cstdlib>
#include <cstdarg>

// for crc32
extern "C" {
#include "zlib.h"
}

#include "Core/Reporting.h"
#include "Common/File/VFS/VFS.h"
#include "Common/CPUDetect.h"
#include "Common/File/FileUtil.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/StringUtils.h"
#include "Common/System/OSD.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Net/HTTPClient.h"
#include "Common/Net/Resolve.h"
#include "Common/Net/URL.h"
#include "Common/Thread/ThreadUtil.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/Config.h"
#include "Core/CwCheat.h"
#include "Core/Loaders.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/FileSystems/BlockDevices.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/HLE/Plugins.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/scePower.h"
#include "Core/HW/Display.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"

namespace Reporting
{
	const int DEFAULT_PORT = 80;
	const u32 SPAM_LIMIT = 100;
	const int PAYLOAD_BUFFER_SIZE = 200;

	// Internal limiter on number of requests per instance.
	static u32 spamProtectionCount = 0;
	// Temporarily stores a reference to the hostname.
	static std::string lastHostname;

	// Keeps track of whether a harmful setting was ever used.
	static bool everUnsupported = false;
	// Support is cached here to avoid checking it on every single request.
	static bool currentSupported = false;
	// Whether the most recent server request seemed successful.
	static bool serverWorking = true;
	// The latest compatibility result from the server.
	static std::vector<std::string> lastCompatResult;

	static std::string lastModuleName;
	static int lastModuleVersion;
	static uint32_t lastModuleCrc;

	static std::mutex pendingMessageLock;
	static std::condition_variable pendingMessageCond;
	static std::deque<int> pendingMessages;
	static bool pendingMessagesDone = false;
	static std::thread messageThread;
	static std::thread compatThread;

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
	static Path crcFilename;
	static std::map<Path, u32> crcResults;
	static std::atomic<bool> crcPending{};
	static std::atomic<bool> crcCancel{};
	static std::thread crcThread;

	static u32 CalculateCRC(BlockDevice *blockDevice, std::atomic<bool> *cancel) {
		auto ga = GetI18NCategory(I18NCat::GAME);

		u32 crc = crc32(0, Z_NULL, 0);

		u8 block[2048];
		u32 numBlocks = blockDevice->GetNumBlocks();
		for (u32 i = 0; i < numBlocks; ++i) {
			if (cancel && *cancel) {
				g_OSD.RemoveProgressBar("crc", false, 0.0f);
				return 0;
			}
			if (!blockDevice->ReadBlock(i, block, true)) {
				ERROR_LOG(Log::FileSystem, "Failed to read block for CRC");
				g_OSD.RemoveProgressBar("crc", false, 0.0f);
				return 0;
			}
			crc = crc32(crc, block, 2048);
			g_OSD.SetProgressBar("crc", std::string(ga->T("Calculate CRC")), 0.0f, (float)numBlocks, (float)i, 0.5f);
		}

		g_OSD.RemoveProgressBar("crc", true, 0.0f);
		return crc;
	}

	static int CalculateCRCThread() {
		SetCurrentThreadName("ReportCRC");

		AndroidJNIThreadContext jniContext;

		FileLoader *fileLoader = ResolveFileLoaderTarget(ConstructFileLoader(crcFilename));
		BlockDevice *blockDevice = constructBlockDevice(fileLoader);

		u32 crc = 0;
		if (blockDevice) {
			crc = CalculateCRC(blockDevice, &crcCancel);
		}

		delete blockDevice;
		delete fileLoader;

		std::lock_guard<std::mutex> guard(crcLock);
		crcResults[crcFilename] = crc;
		crcPending = false;
		crcCond.notify_one();
		return 0;
	}

	void QueueCRC(const Path &gamePath) {
		std::lock_guard<std::mutex> guard(crcLock);

		auto it = crcResults.find(gamePath);
		if (it != crcResults.end()) {
			// Nothing to do, we've already calculated it.
			// Note: we assume it stays static until the app is closed.
			return;
		}

		if (crcPending) {
			// Already in process. This is OK - on the crash screen we call this in a polling fashion.
			return;
		}

		INFO_LOG(Log::System, "Starting CRC calculation");
		crcFilename = gamePath;
		crcPending = true;
		crcCancel = false;
		crcThread = std::thread(CalculateCRCThread);
	}

	bool HasCRC(const Path &gamePath) {
		std::lock_guard<std::mutex> guard(crcLock);
		return crcResults.find(gamePath) != crcResults.end();
	}

	uint32_t RetrieveCRC(const Path &gamePath) {
		QueueCRC(gamePath);

		std::unique_lock<std::mutex> guard(crcLock);
		auto it = crcResults.find(gamePath);
		while (it == crcResults.end()) {
			crcCond.wait(guard);
			it = crcResults.find(gamePath);
		}

		if (crcThread.joinable()) {
			INFO_LOG(Log::System, "Finished CRC calculation");
			crcThread.join();
		}
		return it->second;
	}

	static uint32_t RetrieveCRCUnlessPowerSaving(const Path &gamePath) {
		// It's okay to use it if we have it already.
		if (Core_GetPowerSaving() && !HasCRC(gamePath)) {
			return 0;
		}

		return RetrieveCRC(gamePath);
	}

	static void PurgeCRC() {
		std::unique_lock<std::mutex> guard(crcLock);
		if (crcPending) {
			INFO_LOG(Log::System, "Cancelling CRC calculation");
			crcCancel = true;
			while (crcPending) {
				crcCond.wait(guard);
			}
		} else {
			DEBUG_LOG(Log::System, "No CRC pending");
		}

		if (crcThread.joinable())
			crcThread.join();
	}

	void CancelCRC() {
		PurgeCRC();
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
		http::Client http;
		net::RequestProgress progress(&pendingMessagesDone);
		Buffer theVoid = Buffer::Void();

		http.SetUserAgent(StringFromFormat("PPSSPP/%s", PPSSPP_GIT_VERSION));

		if (output == nullptr)
			output = &theVoid;

		const char *serverHost = ServerHostname();
		if (!serverHost)
			return false;

		if (http.Resolve(serverHost, ServerPort())) {
			int result = -1;
			if (http.Connect()) {
				result = http.POST(http::RequestParams(uri), data, mimeType, output, &progress);
				http.Disconnect();
			}

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
#elif defined(_WIN64) && defined(_M_ARM64)
		return "Windows ARM64";
#elif defined(_WIN64)
		return "Windows 64";
#elif defined(_WIN32) && defined(_M_ARM)
		return "Windows ARM32";
#elif defined(_WIN32)
		return "Windows";
#elif PPSSPP_PLATFORM(IOS)
		return "iOS";
#elif defined(__APPLE__)
		return "Mac";
#elif defined(LOONGSON)
		return "Loongson";
#elif defined(__SWITCH__)
		return "Switch";
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

	bool MessageAllowed();
	void SendReportMessage(const char *message, const char *formatted);

	void Init()
	{
		// New game, clean slate.
		spamProtectionCount = 0;
		ResetCounts();
		everUnsupported = false;
		currentSupported = IsSupported();
		pendingMessagesDone = false;
		Reporting::SetupCallbacks(&MessageAllowed, &SendReportMessage);

		lastModuleName.clear();
		lastModuleVersion = 0;
		lastModuleCrc = 0;
	}

	void Shutdown()
	{
		pendingMessageLock.lock();
		pendingMessagesDone = true;
		pendingMessageCond.notify_one();
		pendingMessageLock.unlock();
		if (compatThread.joinable())
			compatThread.join();
		if (messageThread.joinable())
			messageThread.join();
		PurgeCRC();

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

		Do(p, everUnsupported);
	}

	void UpdateConfig()
	{
		currentSupported = IsSupported();
		if (!currentSupported && PSP_IsInited())
			everUnsupported = true;
	}

	void NotifyDebugger() {
		currentSupported = false;
		everUnsupported = true;
	}

	void NotifyExecModule(const char *name, int ver, uint32_t crc) {
		lastModuleName = name;
		lastModuleVersion = ver;
		lastModuleCrc = crc;
	}

	std::string CurrentGameID()
	{
		// TODO: Maybe ParamSFOData shouldn't include nulls in std::strings?  Don't work to break savedata, though...
		const std::string disc_id = StripTrailingNull(g_paramSFO.GetDiscID());
		const std::string disc_version = StripTrailingNull(g_paramSFO.GetValueString("DISC_VERSION"));
		return disc_id + "_" + disc_version;
	}

	void AddGameInfo(UrlEncoder &postdata)
	{
		postdata.Add("game", CurrentGameID());
		postdata.Add("game_title", StripTrailingNull(g_paramSFO.GetValueString("TITLE")));
		postdata.Add("sdkver", sceKernelGetCompiledSdkVersion());
		postdata.Add("module_name", lastModuleName);
		postdata.Add("module_ver", lastModuleVersion);
		postdata.Add("module_crc", lastModuleCrc);
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
		if (PSP_IsInited())
			postdata.Add("ticks", (const uint64_t)CoreTiming::GetTicks());

		float vps, fps;
		__DisplayGetAveragedFPS(&vps, &fps);
		postdata.Add("vps", vps);
		postdata.Add("fps", fps);

		postdata.Add("savestate_used", SaveState::HasLoadedState());
	}

	void AddScreenshotData(MultipartFormDataEncoder &postdata, const Path &filename)
	{
		std::string data;
		if (!filename.empty() && File::ReadBinaryFileToString(filename, &data)) {
			postdata.Add("screenshot", data, "screenshot.jpg", "image/jpeg");
		}

		const std::string iconFilename = "disc0:/PSP_GAME/ICON0.PNG";
		std::vector<u8> iconData;
		if (pspFileSystem.ReadEntireFile(iconFilename, iconData) >= 0) {
			postdata.Add("icon", iconData, "icon.png", "image/png");
		}
	}

	int Process(int pos)
	{
		SetCurrentThreadName("Report");

		AndroidJNIThreadContext jniContext;  // destructor detaches

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
			postdata.Add("crc", StringFromFormat("%08x", RetrieveCRCUnlessPowerSaving(PSP_CoreParameter().fileToStart)));
			postdata.Add("suggestions", payload.string1 != "perfect" && payload.string1 != "playable" ? "1" : "0");
			AddScreenshotData(postdata, Path(payload.string2));
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
		if (CheatsInEffect() || HLEPlugins::HasEnabled())
			return false;
		if (GetLockedCPUSpeedMhz() != 0)
			return false;
		if (g_Config.uJitDisableFlags != 0)
			return false;
		// Don't allow builds without version info from git.  They're useless for reporting.
		if (strcmp(PPSSPP_GIT_VERSION, "unknown") == 0)
			return false;
		// Don't report from games without a version ID (i.e. random hashed homebrew IDs.)
		// The problem is, these aren't useful because the hashes end up different for different people.
		// TODO: Should really hash the ELF instead of the path, but then that affects savestates/cheats.
		if (PSP_IsInited() && g_paramSFO.GetValueString("DISC_VERSION").empty())
			return false;

		// Some users run the exe from a zip or something, and don't have fonts.
		// This breaks things, but let's not report it since it's confusing.
#if defined(USING_WIN_UI) || defined(APPLE)
		if (!File::Exists(g_Config.flash0Directory / "font/jpn0.pgf"))
			return false;
#else
		File::FileInfo fo;
		if (!g_VFS.GetFileInfo("flash0/font/jpn0.pgf", &fo))
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

	bool Enable(bool flag, const std::string &host)
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

	int ProcessPending() {
		SetCurrentThreadName("Report");

		std::unique_lock<std::mutex> guard(pendingMessageLock);
		while (!pendingMessagesDone) {
			while (!pendingMessages.empty() && !pendingMessagesDone) {
				int pos = pendingMessages.front();
				pendingMessages.pop_front();

				guard.unlock();
				Process(pos);
				guard.lock();
			}
			if (pendingMessagesDone) {
				break;
			}
			pendingMessageCond.wait(guard);
		}

		return 0;
	}

	bool MessageAllowed() {
		if (!IsEnabled() || CheckSpamLimited())
			return false;
		return true;
	}

	void SendReportMessage(const char *message, const char *formatted) {
		int pos = NextFreePos();
		if (pos == -1)
			return;

		Payload &payload = payloadBuffer[pos];
		payload.type = RequestType::MESSAGE;
		payload.string1 = message;
		payload.string2 = formatted;

		std::lock_guard<std::mutex> guard(pendingMessageLock);
		pendingMessages.push_back(pos);
		pendingMessageCond.notify_one();

		if (!messageThread.joinable()) {
			messageThread = std::thread(ProcessPending);
		}
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

		if (compatThread.joinable())
			compatThread.join();
		compatThread = std::thread(Process, pos);
	}

	std::vector<std::string> CompatibilitySuggestions() {
		return lastCompatResult;
	}
}
