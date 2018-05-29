// Headless version of PPSSPP, for testing using http://code.google.com/p/pspautotests/ .
// See headless.txt.
// To build on non-windows systems, just run CMake in the SDL directory, it will build both a normal ppsspp and the headless version.

#include <cstdio>
#include <cstdlib>
#include <limits>

#include "file/zip_read.h"
#include "profiler/profiler.h"
#include "Common/FileUtil.h"
#include "Common/GraphicsContext.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/System.h"
#include "Core/HLE/sceUtility.h"
#include "Core/Host.h"
#include "Core/SaveState.h"
#include "Log.h"
#include "LogManager.h"
#include "base/NativeApp.h"
#include "base/timeutil.h"

#include "Compare.h"
#include "StubHost.h"
#if defined(_WIN32)
#include "WindowsHeadlessHost.h"
#elif defined(SDL)
#include "SDLHeadlessHost.h"
#endif

// https://github.com/richq/android-ndk-profiler
#ifdef ANDROID_NDK_PROFILER
#include <stdlib.h>
#include "android/android-ndk-profiler/prof.h"
#endif

class PrintfLogger : public LogListener {
public:
	void Log(const LogMessage &message) {
		switch (message.level) {
		case LogTypes::LVERBOSE:
			fprintf(stderr, "V %s", message.msg.c_str());
			break;
		case LogTypes::LDEBUG:
			fprintf(stderr, "D %s", message.msg.c_str());
			break;
		case LogTypes::LINFO:
			fprintf(stderr, "I %s", message.msg.c_str());
			break;
		case LogTypes::LERROR:
			fprintf(stderr, "E %s", message.msg.c_str());
			break;
		case LogTypes::LWARNING:
			fprintf(stderr, "W %s", message.msg.c_str());
			break;
		case LogTypes::LNOTICE:
		default:
			fprintf(stderr, "N %s", message.msg.c_str());
			break;
		}
	}
};

// Temporary hacks around annoying linking errors.
void NativeUpdate() { }
void NativeRender(GraphicsContext *graphicsContext) { }
void NativeResized() { }

std::string System_GetProperty(SystemProperty prop) { return ""; }
int System_GetPropertyInt(SystemProperty prop) {
	return -1;
}
bool System_GetPropertyBool(SystemProperty prop) {
	return false;
}
void System_SendMessage(const char *command, const char *parameter) {}
bool System_InputBoxGetWString(const wchar_t *title, const std::wstring &defaultvalue, std::wstring &outvalue) { return false; }
void System_AskForPermission(SystemPermission permission) {}
PermissionStatus System_GetPermissionStatus(SystemPermission permission) { return PERMISSION_STATUS_GRANTED; }

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

#if defined(HEADLESSHOST_CLASS)
	{
		fprintf(stderr, "  --graphics=BACKEND    use the full gpu backend (slower)\n");
		fprintf(stderr, "                        options: gles, software, directx9, etc.\n");
		fprintf(stderr, "  --screenshot=FILE     compare against a screenshot\n");
	}
#endif
	fprintf(stderr, "  --timeout=SECONDS     abort test it if takes longer than SECONDS\n");

	fprintf(stderr, "  -v, --verbose         show the full passed/failed result\n");
	fprintf(stderr, "  -i                    use the interpreter\n");
	fprintf(stderr, "  --ir                  use ir interpreter\n");
	fprintf(stderr, "  -j                    use jit (default)\n");
	fprintf(stderr, "  -c, --compare         compare with output in file.expected\n");
	fprintf(stderr, "\nSee headless.txt for details.\n");

	return 1;
}

static HeadlessHost *getHost(GPUCore gpuCore) {
	switch (gpuCore) {
	case GPUCORE_NULL:
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

bool RunAutoTest(HeadlessHost *headlessHost, CoreParameter &coreParameter, bool autoCompare, bool verbose, double timeout)
{
	if (teamCityMode) {
		// Kinda ugly, trying to guesstimate the test name from filename...
		teamCityName = GetTestName(coreParameter.fileToStart);
	}

	std::string output;
	if (autoCompare)
		coreParameter.collectEmuLog = &output;

	std::string error_string;
	if (!PSP_Init(coreParameter, &error_string)) {
		fprintf(stderr, "Failed to start %s. Error: %s\n", coreParameter.fileToStart.c_str(), error_string.c_str());
		printf("TESTERROR\n");
		TeamCityPrint("##teamcity[testIgnored name='%s' message='PRX/ELF missing']\n", teamCityName.c_str());
		return false;
	}

	TeamCityPrint("##teamcity[testStarted name='%s' captureStandardOutput='true']\n", teamCityName.c_str());

	host->BootDone();

	if (autoCompare)
		headlessHost->SetComparisonScreenshot(ExpectedScreenshotFromFilename(coreParameter.fileToStart));

	time_update();
	bool passed = true;
	// TODO: We must have some kind of stack overflow or we're not following the ABI right.
	// This gets trashed if it's not static.
	static double deadline;
	deadline = time_now() + timeout;

	Core_UpdateDebugStats(g_Config.bShowDebugStats || g_Config.bLogFrameDrops);

	PSP_BeginHostFrame();
	if (coreParameter.thin3d)
		coreParameter.thin3d->BeginFrame();

	coreState = CORE_RUNNING;
	while (coreState == CORE_RUNNING)
	{
		int blockTicks = usToCycles(1000000 / 10);
		PSP_RunLoopFor(blockTicks);

		// If we were rendering, this might be a nice time to do something about it.
		if (coreState == CORE_NEXTFRAME) {
			coreState = CORE_RUNNING;
			headlessHost->SwapBuffers();
		}
		time_update();
		if (time_now_d() > deadline) {
			// Don't compare, print the output at least up to this point, and bail.
			printf("%s", output.c_str());
			passed = false;

			host->SendDebugOutput("TIMEOUT\n");
			TeamCityPrint("##teamcity[testFailed name='%s' message='Test timeout']\n", teamCityName.c_str());
			Core_Stop();
		}
	}
	PSP_EndHostFrame();

	if (coreParameter.thin3d)
		coreParameter.thin3d->EndFrame();

	PSP_Shutdown();

	headlessHost->FlushDebugOutput();

	if (autoCompare && passed)
		passed = CompareOutput(coreParameter.fileToStart, output, verbose);

	TeamCityPrint("##teamcity[testFinished name='%s']\n", teamCityName.c_str());

	return passed;
}

int main(int argc, const char* argv[])
{
	PROFILE_INIT();

#if defined(_DEBUG) && defined(_MSC_VER)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

#ifdef ANDROID_NDK_PROFILER
	setenv("CPUPROFILE_FREQUENCY", "500", 1);
	setenv("CPUPROFILE", "/sdcard/gmon.out", 1);
	monstartup("ppsspp_headless");
#endif

	bool fullLog = false;
	bool autoCompare = false;
	bool verbose = false;
	const char *stateToLoad = 0;
	GPUCore gpuCore = GPUCORE_NULL;
	CPUCore cpuCore = CPUCore::JIT;
	
	std::vector<std::string> testFilenames;
	const char *mountIso = 0;
	const char *mountRoot = 0;
	const char *screenshotFilename = 0;
	float timeout = std::numeric_limits<float>::infinity();

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
		else if (!strcmp(argv[i], "-i"))
			cpuCore = CPUCore::INTERPRETER;
		else if (!strcmp(argv[i], "-j"))
			cpuCore = CPUCore::JIT;
		else if (!strcmp(argv[i], "--ir"))
			cpuCore = CPUCore::IR_JIT;
		else if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--compare"))
			autoCompare = true;
		else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose"))
			verbose = true;
		else if (!strncmp(argv[i], "--graphics=", strlen("--graphics=")) && strlen(argv[i]) > strlen("--graphics="))
		{
			const char *gpuName = argv[i] + strlen("--graphics=");
			if (!strcasecmp(gpuName, "gles"))
				gpuCore = GPUCORE_GLES;
			else if (!strcasecmp(gpuName, "software"))
				gpuCore = GPUCORE_SOFTWARE;
			else if (!strcasecmp(gpuName, "directx9"))
				gpuCore = GPUCORE_DIRECTX9;
			else if (!strcasecmp(gpuName, "directx11"))
				gpuCore = GPUCORE_DIRECTX11;
			else if (!strcasecmp(gpuName, "vulkan"))
				gpuCore = GPUCORE_VULKAN;
			else if (!strcasecmp(gpuName, "null"))
				gpuCore = GPUCORE_NULL;
			else
				return printUsage(argv[0], "Unknown gpu backend specified after --graphics=. Allowed: software, directx9, directx11, vulkan, gles, null.");
		}
		// Default to GLES if no value selected.
		else if (!strcmp(argv[i], "--graphics"))
			gpuCore = GPUCORE_GLES;
		else if (!strncmp(argv[i], "--screenshot=", strlen("--screenshot=")) && strlen(argv[i]) > strlen("--screenshot="))
			screenshotFilename = argv[i] + strlen("--screenshot=");
		else if (!strncmp(argv[i], "--timeout=", strlen("--timeout=")) && strlen(argv[i]) > strlen("--timeout="))
			timeout = strtod(argv[i] + strlen("--timeout="), NULL);
		else if (!strcmp(argv[i], "--teamcity"))
			teamCityMode = true;
		else if (!strncmp(argv[i], "--state=", strlen("--state=")) && strlen(argv[i]) > strlen("--state="))
			stateToLoad = argv[i] + strlen("--state=");
		else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))
			return printUsage(argv[0], NULL);
		else
			testFilenames.push_back(argv[i]);
	}

	// TODO: Allow a filename here?
	if (testFilenames.size() == 1 && testFilenames[0] == "@-")
	{
		testFilenames.clear();
		char temp[2048];
		temp[2047] = '\0';

		while (scanf("%2047s", temp) == 1)
			testFilenames.push_back(temp);
	}

	if (testFilenames.empty())
		return printUsage(argv[0], argc <= 1 ? NULL : "No executables specified");

	HeadlessHost *headlessHost = getHost(gpuCore);
	headlessHost->SetGraphicsCore(gpuCore);
	host = headlessHost;

	std::string error_string;
	GraphicsContext *graphicsContext = nullptr;
	bool glWorking = host->InitGraphics(&error_string, &graphicsContext);

	LogManager::Init();
	LogManager *logman = LogManager::GetInstance();
	
	PrintfLogger *printfLogger = new PrintfLogger();

	for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; i++) {
		LogTypes::LOG_TYPE type = (LogTypes::LOG_TYPE)i;
		logman->SetEnabled(type, fullLog);
		logman->SetLogLevel(type, LogTypes::LDEBUG);
	}
	logman->AddListener(printfLogger);

	CoreParameter coreParameter;
	coreParameter.cpuCore = cpuCore;
	coreParameter.gpuCore = glWorking ? gpuCore : GPUCORE_NULL;
	coreParameter.graphicsContext = graphicsContext;
	coreParameter.thin3d = graphicsContext ? graphicsContext->GetDrawContext() : nullptr;
	coreParameter.enableSound = false;
	coreParameter.mountIso = mountIso ? mountIso : "";
	coreParameter.mountRoot = mountRoot ? mountRoot : "";
	coreParameter.startPaused = false;
	coreParameter.printfEmuLog = !autoCompare;
	coreParameter.headLess = true;
	coreParameter.renderWidth = 480;
	coreParameter.renderHeight = 272;
	coreParameter.pixelWidth = 480;
	coreParameter.pixelHeight = 272;
	coreParameter.unthrottle = true;

	g_Config.bEnableSound = false;
	g_Config.bFirstRun = false;
	g_Config.bIgnoreBadMemAccess = true;
	// Never report from tests.
	g_Config.sReportHost = "";
	g_Config.bAutoSaveSymbolMap = false;
	g_Config.iRenderingMode = 1;
	g_Config.bHardwareTransform = true;
#ifdef USING_GLES2
	g_Config.iAnisotropyLevel = 0;
#else
	g_Config.iAnisotropyLevel = 0;  // When testing mipmapping we really don't want this.
#endif
	g_Config.bVertexCache = true;
	g_Config.bTrueColor = true;
	g_Config.iLanguage = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
	g_Config.iTimeFormat = PSP_SYSTEMPARAM_TIME_FORMAT_24HR;
	g_Config.bEncryptSave = true;
	g_Config.sNickName = "shadow";
	g_Config.iTimeZone = 60;
	g_Config.iDateFormat = PSP_SYSTEMPARAM_DATE_FORMAT_DDMMYYYY;
	g_Config.iButtonPreference = PSP_SYSTEMPARAM_BUTTON_CROSS;
	g_Config.iLockParentalLevel = 9;
	g_Config.iInternalResolution = 1;
	g_Config.bFrameSkipUnthrottle = false;
	g_Config.bEnableLogging = fullLog;
	g_Config.iNumWorkerThreads = 1;
	g_Config.bSoftwareSkinning = true;
	g_Config.bVertexDecoderJit = true;
	g_Config.bBlockTransferGPU = true;
	g_Config.iSplineBezierQuality = 2;
	g_Config.bHighQualityDepth = true;
	g_Config.bMemStickInserted = true;

#ifdef _WIN32
	InitSysDirectories();
#endif

#if !defined(__ANDROID__) && !defined(_WIN32)
	g_Config.memStickDirectory = std::string(getenv("HOME")) + "/.ppsspp/";
#endif

	// Try to find the flash0 directory.  Often this is from a subdirectory.
	for (int i = 0; i < 3; ++i)
	{
		if (!File::Exists(g_Config.flash0Directory))
			g_Config.flash0Directory += "../../flash0/";
	}
	// Or else, maybe in the executable's dir.
	if (!File::Exists(g_Config.flash0Directory))
		g_Config.flash0Directory = File::GetExeDirectory() + "assets/flash0/";

	if (screenshotFilename != 0)
		headlessHost->SetComparisonScreenshot(screenshotFilename);

#ifdef __ANDROID__
	// For some reason the debugger installs it with this name?
	if (File::Exists("/data/app/org.ppsspp.ppsspp-2.apk")) {
		VFSRegister("", new ZipAssetReader("/data/app/org.ppsspp.ppsspp-2.apk", "assets/"));
	}
	if (File::Exists("/data/app/org.ppsspp.ppsspp.apk")) {
		VFSRegister("", new ZipAssetReader("/data/app/org.ppsspp.ppsspp.apk", "assets/"));
	}
#endif

	if (stateToLoad != NULL)
		SaveState::Load(stateToLoad);

	std::vector<std::string> failedTests;
	std::vector<std::string> passedTests;
	for (size_t i = 0; i < testFilenames.size(); ++i)
	{
		coreParameter.fileToStart = testFilenames[i];
		if (autoCompare)
			printf("%s:\n", coreParameter.fileToStart.c_str());
		bool passed = RunAutoTest(headlessHost, coreParameter, autoCompare, verbose, timeout);
		if (autoCompare)
		{
			std::string testName = GetTestName(coreParameter.fileToStart);
			if (passed)
			{
				passedTests.push_back(testName);
				printf("  %s - passed!\n", testName.c_str());
			}
			else
				failedTests.push_back(testName);
		}
	}

	if (autoCompare)
	{
		printf("%d tests passed, %d tests failed.\n", (int)passedTests.size(), (int)failedTests.size());
		if (!failedTests.empty())
		{
			printf("Failed tests:\n");
			for (size_t i = 0; i < failedTests.size(); ++i) {
				printf("  %s\n", failedTests[i].c_str());
			}
		}
	}

	host->ShutdownGraphics();
	delete host;
	host = nullptr;
	headlessHost = nullptr;

	VFSShutdown();
	LogManager::Shutdown();
	delete printfLogger;

#ifdef ANDROID_NDK_PROFILER
	moncleanup();
#endif

	return 0;
}
