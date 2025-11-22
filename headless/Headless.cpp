
// Headless version of PPSSPP, for testing using http://code.google.com/p/pspautotests/ .
// See headless.txt.
// To build on non-windows systems, just run CMake in the SDL directory, it will build both a normal ppsspp and the headless version.
// Example command line to run a test in the VS debugger (useful to debug failures):
// > --root pspautotests/tests/../ --compare --timeout=5 --graphics=software pspautotests/tests/cpu/cpu_alu/cpu_alu.prx
// NOTE: In MSVC, don't forget to set the working directory to $ProjectDir\.. in debug settings.

#include "ppsspp_config.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#if PPSSPP_PLATFORM(ANDROID)
#include <jni.h>
#endif

#include <algorithm>

#include "Common/Profiler/Profiler.h"
#include "Common/System/NativeApp.h"
#include "Common/System/Request.h"
#include "Common/System/System.h"

#include "Common/CommonWindows.h"
#if PPSSPP_PLATFORM(WINDOWS)
#include <timeapi.h>
#else
#include <csignal>
#endif
#include "Common/CPUDetect.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/VFS/ZipFileReader.h"
#include "Common/File/VFS/DirectoryReader.h"
#include "Common/File/FileUtil.h"
#include "Common/GraphicsContext.h"
#include "Common/TimeUtil.h"
#include "Common/StringUtils.h"
#include "Common/Thread/ThreadManager.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/System.h"
#include "Core/WebServer.h"
#include "Core/HLE/sceUtility.h"
#include "Core/SaveState.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "Common/Log.h"
#include "Common/Log/LogManager.h"

#include "Compare.h"
#include "HeadlessHost.h"
#if defined(_WIN32)
#include "WindowsHeadlessHost.h"
#elif defined(SDL)
#include "SDLHeadlessHost.h"
#endif

static HeadlessHost *g_headlessHost;

#if PPSSPP_PLATFORM(ANDROID)
JNIEnv *getEnv() {
	return nullptr;
}

jclass findClass(const char *name) {
	return nullptr;
}

bool System_AudioRecordingIsAvailable() { return false; }
bool System_AudioRecordingState() { return false; }
#endif

// Temporary hacks around annoying linking errors.
void NativeFrame(GraphicsContext *graphicsContext) { }
void NativeResized() {}
void NativeVSync(int64_t vsyncId, double frameTime, double expectedPresentationTime) {}

std::string System_GetProperty(SystemProperty prop) { return ""; }
std::vector<std::string> System_GetPropertyStringVec(SystemProperty prop) { return std::vector<std::string>(); }
int64_t System_GetPropertyInt(SystemProperty prop) {
	if (prop == SYSPROP_SYSTEMVERSION)
		return 31;
	return -1;
}
float System_GetPropertyFloat(SystemProperty prop) { return -1.0f; }
bool System_GetPropertyBool(SystemProperty prop) {
	switch (prop) {
		case SYSPROP_CAN_JIT:
			return true;
		case SYSPROP_SKIP_UI:
			return true;
		default:
			return false;
	}
}
void System_Notify(SystemNotification notification) {}
void System_PostUIMessage(UIMessage message, std::string_view param) {}
void System_RunOnMainThread(std::function<void()>) {}
bool System_MakeRequest(SystemRequestType type, int requestId, const std::string &param1, const std::string &param2, int64_t param3, int64_t param4) {
	switch (type) {
	case SystemRequestType::SEND_DEBUG_OUTPUT:
		if (g_headlessHost) {
			g_headlessHost->SendDebugOutput(param1);
			return true;
		}
		return false;
	case SystemRequestType::SEND_DEBUG_SCREENSHOT:
		if (g_headlessHost) {
			g_headlessHost->SendDebugScreenshot((const u8 *)param1.data(), (uint32_t)(param1.size() / param3), param3);
			return true;
		}
		return false;
	default:
		return false;
	}
}
void System_AskForPermission(SystemPermission permission) {}
PermissionStatus System_GetPermissionStatus(SystemPermission permission) { return PERMISSION_STATUS_GRANTED; }
void System_AudioGetDebugStats(char *buf, size_t bufSize) { if (buf) buf[0] = '\0'; }
void System_AudioClear() {}
void System_AudioPushSamples(const s32 *audio, int numSamples, float volume) {}

// TODO: To avoid having to define these here, these should probably be turned into system "requests".
bool NativeSaveSecret(std::string_view nameOfSecret, std::string_view data) { return false; }
std::string NativeLoadSecret(std::string_view nameOfSecret) {
	return "";
}

int printUsage(const char *progname, const char *reason)
{
	if (reason != NULL)
		fprintf(stderr, "Error: %s\n\n", reason);
	fprintf(stderr, "PPSSPP Headless\n");
	fprintf(stderr, "This is primarily meant as a non-interactive test tool.\n\n");
	fprintf(stderr, "Usage: %s file.elf... [options]\n\n", progname);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -m, --mount umd.cso   mount iso on umd1:\n");
	fprintf(stderr, "  -r, --root some/path  mount path on host0: (elfs must be in here)\n");
	fprintf(stderr, "  -l, --log             full log output, not just emulated printfs\n");
	fprintf(stderr, "  --debugger=PORT       enable websocket debugger and break at start\n");

	fprintf(stderr, "  --graphics=BACKEND    use a different gpu backend\n");
	fprintf(stderr, "                        options: gles, software, directx9, etc.\n");
	fprintf(stderr, "  --screenshot=FILE     compare against a screenshot\n");
	fprintf(stderr, "  --max-mse=NUMBER      maximum allowed MSE error for screenshot\n");
	fprintf(stderr, "  --timeout=SECONDS     abort test it if takes longer than SECONDS\n");

	fprintf(stderr, "  -v, --verbose         show the full passed/failed result\n");
	fprintf(stderr, "  -i                    use the interpreter\n");
	fprintf(stderr, "  --ir                  use ir interpreter\n");
	fprintf(stderr, "  -j                    use jit (default)\n");
	fprintf(stderr, "  -c, --compare         compare with output in file.expected\n");
	fprintf(stderr, "  --bench               run multiple times and output speed\n");
	fprintf(stderr, "\nSee headless.txt for details.\n");

	return 1;
}

static HeadlessHost *getHost(GPUCore gpuCore) {
	switch (gpuCore) {
	case GPUCORE_SOFTWARE:
		return new HeadlessHost();
#ifdef HEADLESSHOST_CLASS
	default:
		return new HEADLESSHOST_CLASS();
#else
	default:
		return new HeadlessHost();
#endif
	}
}

struct AutoTestOptions {
	double timeout;
	double maxScreenshotError;
	bool compare : 1;
	bool verbose : 1;
	bool bench : 1;
};

bool RunAutoTest(HeadlessHost *headlessHost, CoreParameter &coreParameter, const AutoTestOptions &opt) {
	// Kinda ugly, trying to guesstimate the test name from filename...
	currentTestName = GetTestName(coreParameter.fileToStart);

	std::string output;
	if (opt.compare || opt.bench)
		coreParameter.collectDebugOutput = &output;

	if (!PSP_InitStart(coreParameter)) {
		// Shouldn't really happen anymore, the errors happen later in PSP_InitUpdate.
		fprintf(stderr, "Failed to start '%s'.\n", coreParameter.fileToStart.c_str());
		printf("TESTERROR\n");
		TeamCityPrint("testIgnored name='%s' message='PRX/ELF missing'", currentTestName.c_str());
		GitHubActionsPrint("error", "PRX/ELF missing for %s", currentTestName.c_str());
		return false;
	}

	TeamCityPrint("testStarted name='%s' captureStandardOutput='true'", currentTestName.c_str());

	if (opt.compare)
		headlessHost->SetComparisonScreenshot(ExpectedScreenshotFromFilename(coreParameter.fileToStart), opt.maxScreenshotError);

	std::string error_string;
	while (PSP_InitUpdate(&error_string) == BootState::Booting) {
		sleep_ms(1, "auto-test");
	}

	if (!PSP_IsInited()) {
		TeamCityPrint("%s", error_string.c_str());
		TeamCityPrint("testFailed name='%s' message='Startup failed'", currentTestName.c_str());
		TeamCityPrint("testFinished name='%s'", currentTestName.c_str());
		GitHubActionsPrint("error", "Test init failed for %s", currentTestName.c_str());
		return false;
	}

	System_Notify(SystemNotification::BOOT_DONE);

	PSP_UpdateDebugStats((DebugOverlay)g_Config.iDebugOverlay == DebugOverlay::DEBUG_STATS || g_Config.bLogFrameDrops);

	if (gpu) {
		gpu->BeginHostFrame(g_Config.GetDisplayLayoutConfig(DeviceOrientation::Landscape));
	}
	Draw::DrawContext *draw = coreParameter.graphicsContext ? coreParameter.graphicsContext->GetDrawContext() : nullptr;
	if (draw) {
		draw->BeginFrame(Draw::DebugFlags::NONE);
	}

	bool passed = true;
	double deadline = time_now_d() + opt.timeout;
	coreState = coreParameter.startBreak ? CORE_STEPPING_CPU : CORE_RUNNING_CPU;
	while (coreState == CORE_RUNNING_CPU || coreState == CORE_STEPPING_CPU)
	{
		int blockTicks = (int)usToCycles(1000000 / 10);
		PSP_RunLoopFor(blockTicks);

		// If we were rendering, this might be a nice time to do something about it.
		if (coreState == CORE_NEXTFRAME) {
			coreState = CORE_RUNNING_CPU;
			headlessHost->SwapBuffers();
		}
		if (coreState == CORE_STEPPING_CPU && !coreParameter.startBreak) {
			break;
		}
		bool debugger = false;
#ifdef _WIN32
		if (IsDebuggerPresent())
			debugger = true;
#endif
		if (time_now_d() > deadline && !debugger) {
			// Don't compare, print the output at least up to this point, and bail.
			if (!opt.bench) {
				printf("%s", output.c_str());

				System_SendDebugOutput("TIMEOUT\n");
				TeamCityPrint("testFailed name='%s' message='Test timeout'", currentTestName.c_str());
				GitHubActionsPrint("error", "Test timeout for %s", currentTestName.c_str());
			}

			passed = false;
			Core_Stop();
		}
	}
	if (gpu) {
		gpu->EndHostFrame();
	}

	if (draw) {
		draw->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::CLEAR, Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE }, "Headless");
		// Vulkan may get angry if we don't do a final present.
		if (gpu)
			gpu->CopyDisplayToOutput(g_Config.GetDisplayLayoutConfig(DeviceOrientation::Landscape), true);

		draw->EndFrame();
	}

	PSP_Shutdown(true);

	if (!opt.bench)
		headlessHost->FlushDebugOutput();

	if (opt.compare && passed)
		passed = CompareOutput(coreParameter.fileToStart, output, opt.verbose);

	TeamCityPrint("testFinished name='%s'", currentTestName.c_str());

	return passed;
}

std::vector<std::string> ReadFromListFile(const std::string &listFilename) {
	std::vector<std::string> testFilenames;
	char temp[2048]{};

	if (listFilename == "-") {
		// If you get stuck here in the debugger, you accidentally passed '@-' on the command line, meaning we expect
		// a list of files on stdin.
		while (scanf("%2047s", temp) == 1)
			testFilenames.push_back(temp);
	} else {
		FILE *fp = File::OpenCFile(Path(listFilename), "rt");
		if (!fp) {
			fprintf(stderr, "Unable to open '%s' as a list file\n", listFilename.c_str());
			return testFilenames;
		}

		while (fscanf(fp, "%2047s", temp) == 1)
			testFilenames.push_back(temp);
		fclose(fp);
	}

	return testFilenames;
}

static void AddRecursively(std::vector<std::string> *tests, Path actualPath) {
	// TODO: Some file systems can optimize this.
	std::vector<File::FileInfo> fileInfo;
	if (!File::GetFilesInDir(actualPath, &fileInfo, "prx")) {
		return;
	}
	for (const auto &file : fileInfo) {
		if (file.isDirectory) {
			AddRecursively(tests, actualPath / file.name);
		} else if (file.name != "Makefile") {  // hack around filter problem
			tests->push_back((actualPath / file.name).ToString());
		}
	}
}

static void AddTestsByPath(std::vector<std::string> *tests, std::string_view path) {
	if (endsWith(path, "/...")) {
		path = path.substr(0, path.size() - 4);
		// Recurse for tests
		AddRecursively(tests, Path(path));
	} /* else if (File::IsDirectory(Path(path))) {
		// Alternate syntax - just specify the path.
		AddRecursively(tests, Path(path));
	} */ else {
		tests->push_back(std::string(path));
	}
}

int main(int argc, const char* argv[])
{
	PROFILE_INIT();
	TimeInit();
#if PPSSPP_PLATFORM(WINDOWS)
	if (!IsDebuggerPresent()) {
		SetCleanExitOnAssert();
	}
#else
	// Ignore sigpipe.
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		perror("Unable to ignore SIGPIPE");
	}
#endif

#if defined(_DEBUG) && defined(_MSC_VER)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	AutoTestOptions testOptions{};
	testOptions.timeout = std::numeric_limits<double>::infinity();
	bool fullLog = false;
	const char *stateToLoad = 0;
	GPUCore gpuCore = GPUCORE_SOFTWARE;
	CPUCore cpuCore = CPUCore::JIT;
	int debuggerPort = -1;
	bool oldAtrac = false;
	bool outputDebugStringLog = false;

	std::vector<std::string> testFilenames;
	std::vector<std::string> ignoredTests;
	const char *mountIso = nullptr;
	const char *mountRoot = nullptr;
	const char *screenshotFilename = nullptr;

	for (int i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "-m") || !strcmp(argv[i], "--mount"))
		{
			if (++i >= argc)
				return printUsage(argv[0], "Missing argument after -m");
			mountIso = argv[i];
		}
		else if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "--root"))
		{
			if (++i >= argc)
				return printUsage(argv[0], "Missing argument after -r");
			mountRoot = argv[i];
		}
		else if (!strcmp(argv[i], "-l") || !strcmp(argv[i], "--log"))
			fullLog = true;
		else if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--odslog"))
			outputDebugStringLog = true;
		else if (!strcmp(argv[i], "-i"))
			cpuCore = CPUCore::INTERPRETER;
		else if (!strcmp(argv[i], "-j"))
			cpuCore = CPUCore::JIT;
		else if (!strcmp(argv[i], "--jit-ir"))
			cpuCore = CPUCore::JIT_IR;
		else if (!strcmp(argv[i], "--ir"))
			cpuCore = CPUCore::IR_INTERPRETER;
		else if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--compare"))
			testOptions.compare = true;
		else if (!strcmp(argv[i], "--bench"))
			testOptions.bench = true;
		else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose"))
			testOptions.verbose = true;
		else if (!strcmp(argv[i], "--old-atrac"))
			oldAtrac = true;
		else if (!strncmp(argv[i], "--graphics=", strlen("--graphics=")) && strlen(argv[i]) > strlen("--graphics="))
		{
			const char *gpuName = argv[i] + strlen("--graphics=");
			if (!strcasecmp(gpuName, "gles"))
				gpuCore = GPUCORE_GLES;
			// There used to be a separate "null" rendering core - just use software.
			else if (!strcasecmp(gpuName, "software") || !strcasecmp(gpuName, "null"))
				gpuCore = GPUCORE_SOFTWARE;
			else if (!strcasecmp(gpuName, "directx11"))
				gpuCore = GPUCORE_DIRECTX11;
			else if (!strcasecmp(gpuName, "vulkan"))
				gpuCore = GPUCORE_VULKAN;
			else
				return printUsage(argv[0], "Unknown gpu backend specified after --graphics=. Allowed: software, directx9, directx11, vulkan, gles, null.");
		}
		// Default to GLES if no value selected.
		else if (!strcmp(argv[i], "--graphics")) {
#if PPSSPP_API(ANY_GL)
			gpuCore = GPUCORE_GLES;
#else
			gpuCore = GPUCORE_DIRECTX11;
#endif
		} else if (!strncmp(argv[i], "--screenshot=", strlen("--screenshot=")) && strlen(argv[i]) > strlen("--screenshot="))
			screenshotFilename = argv[i] + strlen("--screenshot=");
		else if (!strncmp(argv[i], "--timeout=", strlen("--timeout=")) && strlen(argv[i]) > strlen("--timeout="))
			testOptions.timeout = strtod(argv[i] + strlen("--timeout="), nullptr);
		else if (!strncmp(argv[i], "--max-mse=", strlen("--max-mse=")) && strlen(argv[i]) > strlen("--max-mse="))
			testOptions.maxScreenshotError = strtod(argv[i] + strlen("--max-mse="), nullptr);
		else if (!strncmp(argv[i], "--debugger=", strlen("--debugger=")) && strlen(argv[i]) > strlen("--debugger="))
			debuggerPort = (int)strtoul(argv[i] + strlen("--debugger="), NULL, 10);
		else if (!strcmp(argv[i], "--teamcity"))
			teamCityMode = true;
		else if (!strncmp(argv[i], "--state=", strlen("--state=")) && strlen(argv[i]) > strlen("--state="))
			stateToLoad = argv[i] + strlen("--state=");
		else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))
			return printUsage(argv[0], NULL);
		else if (!strcmp(argv[i], "--ignore")) {
			if (++i >= argc)
				return printUsage(argv[0], "Missing argument after --ignore");
			ignoredTests.push_back(argv[i]);
		} else {
			AddTestsByPath(&testFilenames, argv[i]);
		}
	}

	if (testFilenames.size() == 1 && testFilenames[0][0] == '@')
		testFilenames = ReadFromListFile(testFilenames[0].substr(1));

	// Remove any ignored tests.
	testFilenames.erase(
		std::remove_if(
			testFilenames.begin(),
			testFilenames.end(),
			[&ignoredTests](const std::string& item) { return std::find(ignoredTests.begin(), ignoredTests.end(), item) != ignoredTests.end(); }
		),
		testFilenames.end()
	);

	if (testFilenames.empty())
		return printUsage(argv[0], argc <= 1 ? NULL : "No executables specified");

	g_Config.bEnableLogging = (fullLog || outputDebugStringLog);
	g_logManager.Init(&g_Config.bEnableLogging, outputDebugStringLog);

	for (int i = 0; i < (int)Log::NUMBER_OF_LOGS; i++) {
		Log type = (Log)i;
		g_logManager.SetEnabled(type, (fullLog || outputDebugStringLog));
		g_logManager.SetLogLevel(type, LogLevel::LDEBUG);
	}
	if (fullLog) {
		// Only with --log, add the printfLogger.
		g_logManager.EnableOutput(LogOutput::Printf);
	}

	// Needs to be after log so we don't interfere with test output.
	g_threadManager.Init(cpu_info.num_cores, cpu_info.logical_cpu_count);

	HeadlessHost *headlessHost = getHost(gpuCore);
	g_headlessHost = headlessHost;

	std::string error_string;
	GraphicsContext *graphicsContext = nullptr;
	bool glWorking = headlessHost->InitGraphics(&error_string, &graphicsContext, gpuCore);

	CoreParameter coreParameter;
	coreParameter.cpuCore = cpuCore;  // apprently this gets overwritten somehow by g_Config below.
	coreParameter.gpuCore = glWorking ? gpuCore : GPUCORE_SOFTWARE;
	coreParameter.graphicsContext = graphicsContext;
	coreParameter.enableSound = false;
	coreParameter.mountIso = mountIso ? Path(mountIso) : Path();
	coreParameter.mountRoot = mountRoot ? Path(mountRoot) : Path();
	coreParameter.startBreak = false;
	coreParameter.headLess = true;
	coreParameter.renderScaleFactor = 1;
	coreParameter.renderWidth = 480;
	coreParameter.renderHeight = 272;
	coreParameter.pixelWidth = 480;
	coreParameter.pixelHeight = 272;
	coreParameter.fastForward = true;

	g_Config.RestoreDefaults(RestoreSettingsBits::SETTINGS | RestoreSettingsBits::CONTROLS, true);

	// Somehow this affects the test execution of pspautotests/tests/gpu/vertices/morph.prx, even though
	// we actually set the cpu core in CoreParameter above. Probably because we end up using the JIT vs non-JIT
	// vertex decoder.
	g_Config.iCpuCore = 0;

	// NOTE: In headless mode, we never save the config. This is just for this run.
	g_Config.iDumpFileTypes = 0;
	g_Config.bEnableSound = false;
	g_Config.bFirstRun = false;
	g_Config.bIgnoreBadMemAccess = true;  // NOTE: A few tests rely on this, which is BAD: threads/mbx/refer/refer , threads/mbx/send/send, threads/vtimers/interrupt
	// Never report from tests.
	g_Config.sReportHost.clear();
	g_Config.bAutoSaveSymbolMap = false;
	g_Config.bSkipBufferEffects = false;
	g_Config.iSkipGPUReadbackMode = (int)SkipGPUReadbackMode::NO_SKIP;
	g_Config.bHardwareTransform = true;
	g_Config.iAnisotropyLevel = 0;  // When testing mipmapping we really don't want this.
	g_Config.iMultiSampleLevel = 0;
	g_Config.iLanguage = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
	g_Config.iTimeFormat = PSP_SYSTEMPARAM_TIME_FORMAT_24HR;
	g_Config.bEncryptSave = true;
	g_Config.sNickName = "shadow";
	g_Config.iTimeZone = 60;
	g_Config.iDateFormat = PSP_SYSTEMPARAM_DATE_FORMAT_DDMMYYYY;
	g_Config.iButtonPreference = PSP_SYSTEMPARAM_BUTTON_CROSS;
	g_Config.iLockParentalLevel = 9;
	g_Config.iInternalResolution = 1;
	g_Config.bEnableLogging = (fullLog || outputDebugStringLog);
	g_Config.bSoftwareSkinning = true;
	g_Config.bVertexDecoderJit = true;
	g_Config.bSoftwareRendering = coreParameter.gpuCore == GPUCORE_SOFTWARE;
	g_Config.bSoftwareRenderingJit = true;
	g_Config.iSplineBezierQuality = 2;
	g_Config.bHighQualityDepth = true;
	g_Config.bMemStickInserted = true;
	g_Config.iMemStickSizeGB = 16;
	g_Config.bEnableWlan = true;
	g_Config.sMACAddress = "12:34:56:78:9A:BC";
	g_Config.iFirmwareVersion = PSP_DEFAULT_FIRMWARE;
	g_Config.iPSPModel = PSP_MODEL_SLIM;
	g_Config.iGameVolume = VOLUMEHI_FULL;
	g_Config.iReverbVolume = VOLUMEHI_FULL;
	g_Config.internalDataDirectory.clear();
	g_Config.bUseOldAtrac = oldAtrac;
	g_Config.iForceEnableHLE = 0xFFFFFFFF;  // Run all modules as HLE. We don't have anything to load in this context.

	// g_Config.bUseOldAtrac = true;

	Path exePath = File::GetExeDirectory();
	g_Config.flash0Directory = exePath / "assets/flash0";

#if PPSSPP_PLATFORM(WINDOWS)
	// Mount a filesystem
	g_Config.memStickDirectory = exePath / "memstick";
	File::CreateDir(g_Config.memStickDirectory);
	CreateSysDirectories();
#elif !PPSSPP_PLATFORM(ANDROID)
	g_Config.memStickDirectory = Path(std::string(getenv("HOME"))) / ".ppsspp";
#endif

	// Try to find the flash0 directory.  Often this is from a subdirectory.
	Path nextPath = exePath;
	for (int i = 0; i < 5; ++i) {
		if (File::Exists(nextPath / "assets/flash0")) {
			g_Config.flash0Directory = nextPath / "assets/flash0";
#if !PPSSPP_PLATFORM(ANDROID)
			g_VFS.Register("", new DirectoryReader(nextPath / "assets"));
#endif
			break;
		}

		if (!nextPath.CanNavigateUp())
			break;
		nextPath = nextPath.NavigateUp();
	}

	if (screenshotFilename)
		headlessHost->SetComparisonScreenshot(Path(std::string(screenshotFilename)), testOptions.maxScreenshotError);
	headlessHost->SetWriteFailureScreenshot(!teamCityMode && !getenv("GITHUB_ACTIONS") && !testOptions.bench);
	headlessHost->SetWriteDebugOutput(!testOptions.compare && !testOptions.bench);

#if PPSSPP_PLATFORM(ANDROID)
	// For some reason the debugger installs it with this name?
	if (File::Exists(Path("/data/app/org.ppsspp.ppsspp-2.apk"))) {
		g_VFS.Register("", ZipFileReader::Create(Path("/data/app/org.ppsspp.ppsspp-2.apk"), "assets/"));
	}
	if (File::Exists(Path("/data/app/org.ppsspp.ppsspp.apk"))) {
		g_VFS.Register("", ZipFileReader::Create(Path("/data/app/org.ppsspp.ppsspp.apk"), "assets/"));
	}
#elif PPSSPP_PLATFORM(LINUX)
	g_VFS.Register("", new DirectoryReader(Path("/usr/local/share/ppsspp/assets")));
	g_VFS.Register("", new DirectoryReader(Path("/usr/local/share/games/ppsspp/assets")));
	g_VFS.Register("", new DirectoryReader(Path("/usr/share/ppsspp/assets")));
	g_VFS.Register("", new DirectoryReader(Path("/usr/share/games/ppsspp/assets")));
#endif

	UpdateUIState(UISTATE_INGAME);

	if (debuggerPort > 0) {
		g_Config.iRemoteISOPort = debuggerPort;
		coreParameter.startBreak = true;
		StartWebServer(WebServerFlags::DEBUGGER);
	}

	if (stateToLoad != NULL)
		SaveState::Load(Path(stateToLoad), -1);

	std::vector<std::string> failedTests;
	std::vector<std::string> passedTests;
	for (size_t i = 0; i < testFilenames.size(); ++i)
	{
		coreParameter.fileToStart = Path(testFilenames[i]);
		if (testOptions.compare)
			printf("%s:\n", coreParameter.fileToStart.c_str());
		bool passed = RunAutoTest(headlessHost, coreParameter, testOptions);
		if (testOptions.bench) {
			double st = time_now_d();
			double deadline = st + testOptions.timeout;
			double runs = 0.0;
			for (int i = 0; i < 100; ++i) {
				RunAutoTest(headlessHost, coreParameter, testOptions);
				runs++;

				if (time_now_d() > deadline)
					break;
			}
			double et = time_now_d();

			std::string testName = GetTestName(coreParameter.fileToStart);
			printf("  %s - %f seconds average\n", testName.c_str(), (et - st) / runs);
		}
		if (testOptions.compare) {
			std::string testName = GetTestName(coreParameter.fileToStart);
			if (passed) {
				passedTests.push_back(testName);
				printf("  %s - passed!\n", testName.c_str());
			}
			else
				failedTests.push_back(testName);
		}
	}

	if (testOptions.compare) {
		printf("%d tests passed, %d tests failed.\n", (int)passedTests.size(), (int)failedTests.size());
		if (!failedTests.empty())
		{
			printf("Failed tests:\n");
			for (size_t i = 0; i < failedTests.size(); ++i) {
				printf("  %s\n", failedTests[i].c_str());
			}
		}
	}

	if (debuggerPort > 0) {
		ShutdownWebServer();
	}

	headlessHost->ShutdownGraphics();
	delete headlessHost;
	headlessHost = nullptr;
	g_headlessHost = nullptr;

	g_VFS.Clear();
	g_logManager.Shutdown();

#if PPSSPP_PLATFORM(WINDOWS)
	timeEndPeriod(1);
#endif

	g_threadManager.Teardown();

	if (!failedTests.empty() && !teamCityMode)
		return 1;
	return 0;
}
