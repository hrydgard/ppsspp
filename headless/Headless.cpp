// Headless version of PPSSPP, for testing using http://code.google.com/p/pspautotests/ .
// See headless.txt.
// To build on non-windows systems, just run CMake in the SDL directory, it will build both a normal ppsspp and the headless version.

#include <cstdio>
#include <cstdlib>
#include <limits>

#include "file/zip_read.h"
#include "Common/FileUtil.h"
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
#include "input/input_state.h"
#include "base/timeutil.h"

#include "Compare.h"
#include "StubHost.h"
#ifdef _WIN32
#include "Windows/OpenGLBase.h"
#include "WindowsHeadlessHost.h"
#include "WindowsHeadlessHostDx9.h"
#endif

// https://github.com/richq/android-ndk-profiler
#ifdef ANDROID_NDK_PROFILER
#include <stdlib.h>
#include "android/android-ndk-profiler/prof.h"
#endif

class PrintfLogger : public LogListener
{
public:
	void Log(LogTypes::LOG_LEVELS level, const char *msg)
	{
		switch (level)
		{
		case LogTypes::LVERBOSE:
			fprintf(stderr, "V %s", msg);
			break;
		case LogTypes::LDEBUG:
			fprintf(stderr, "D %s", msg);
			break;
		case LogTypes::LINFO:
			fprintf(stderr, "I %s", msg);
			break;
		case LogTypes::LERROR:
			fprintf(stderr, "E %s", msg);
			break;
		case LogTypes::LWARNING:
			fprintf(stderr, "W %s", msg);
			break;
		case LogTypes::LNOTICE:
		default:
			fprintf(stderr, "N %s", msg);
			break;
		}
	}
};

struct InputState;
// Temporary hack around annoying linking error.
void GL_SwapBuffers() { }
void NativeUpdate(InputState &input_state) { }
void NativeRender() { }
void NativeResized() { }

std::string System_GetProperty(SystemProperty prop) { return ""; }
void System_SendMessage(const char *command, const char *parameter) {}
bool System_InputBoxGetWString(const wchar_t *title, const std::wstring &defaultvalue, std::wstring &outvalue) { return false; }

#ifndef _WIN32
InputState input_state;
#endif

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

#if HEADLESSHOST_CLASS != HeadlessHost
	{
		fprintf(stderr, "  --graphics=BACKEND    use the full gpu backend (slower)\n");
		fprintf(stderr, "                        options: gles, software, directx9\n");
		fprintf(stderr, "  --screenshot=FILE     compare against a screenshot\n");
	}
#endif
	fprintf(stderr, "  --timeout=SECONDS     abort test it if takes longer than SECONDS\n");

	fprintf(stderr, "  -v, --verbose         show the full passed/failed result\n");
	fprintf(stderr, "  -i                    use the interpreter\n");
	fprintf(stderr, "  -j                    use jit (default)\n");
	fprintf(stderr, "  -c, --compare         compare with output in file.expected\n");
	fprintf(stderr, "\nSee headless.txt for details.\n");

	return 1;
}

static HeadlessHost * getHost(GPUCore gpuCore) {
	switch(gpuCore) {
	case GPU_NULL:
		return new HeadlessHost();
#ifdef _WIN32
	case GPU_DIRECTX9:
		return new WindowsHeadlessHostDx9();
#endif
	default:
		return new HEADLESSHOST_CLASS();
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

	PSP_Shutdown();

	headlessHost->FlushDebugOutput();

	if (autoCompare && passed)
		passed = CompareOutput(coreParameter.fileToStart, output, verbose);

	TeamCityPrint("##teamcity[testFinished name='%s']\n", teamCityName.c_str());

	return passed;
}

int main(int argc, const char* argv[])
{
#ifdef ANDROID_NDK_PROFILER
	setenv("CPUPROFILE_FREQUENCY", "500", 1);
	setenv("CPUPROFILE", "/sdcard/gmon.out", 1);
	monstartup("ppsspp_headless");
#endif

	bool fullLog = false;
	bool useJit = true;
	bool autoCompare = false;
	bool verbose = false;
	const char *stateToLoad = 0;
	GPUCore gpuCore = GPU_NULL;
	
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
			useJit = false;
		else if (!strcmp(argv[i], "-j"))
			useJit = true;
		else if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--compare"))
			autoCompare = true;
		else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose"))
			verbose = true;
		else if (!strncmp(argv[i], "--graphics=", strlen("--graphics=")) && strlen(argv[i]) > strlen("--graphics="))
		{
			const char *gpuName = argv[i] + strlen("--graphics=");
			if (!strcasecmp(gpuName, "gles"))
				gpuCore = GPU_GLES;
			else if (!strcasecmp(gpuName, "software"))
				gpuCore = GPU_SOFTWARE;
			else if (!strcasecmp(gpuName, "directx9"))
				gpuCore = GPU_DIRECTX9;
			else if (!strcasecmp(gpuName, "null"))
				gpuCore = GPU_NULL;
			else
				return printUsage(argv[0], "Unknown gpu backend specified after --graphics=");
		}
		// Default to GLES if no value selected.
		else if (!strcmp(argv[i], "--graphics"))
			gpuCore = GPU_GLES;
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
	host = headlessHost;

	std::string error_string;
	bool glWorking = host->InitGL(&error_string);

	LogManager::Init();
	LogManager *logman = LogManager::GetInstance();
	
	PrintfLogger *printfLogger = new PrintfLogger();

	for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; i++)
	{
		LogTypes::LOG_TYPE type = (LogTypes::LOG_TYPE)i;
		logman->SetEnable(type, fullLog);
		logman->SetLogLevel(type, LogTypes::LDEBUG);
		logman->AddListener(type, printfLogger);
	}

	CoreParameter coreParameter;
	coreParameter.cpuCore = useJit ? CPU_JIT : CPU_INTERPRETER;
	coreParameter.gpuCore = glWorking ? gpuCore : GPU_NULL;
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
	g_Config.iAnisotropyLevel = 8;
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
	g_Config.iBGMVolume = MAX_CONFIG_VOLUME;
	g_Config.iSFXVolume = MAX_CONFIG_VOLUME;
	g_Config.bSoftwareSkinning = true;
	g_Config.bVertexDecoderJit = true;
	g_Config.bBlockTransferGPU = true;

#ifdef _WIN32
	InitSysDirectories();
#endif

#if defined(ANDROID)
#elif defined(BLACKBERRY) || defined(__SYMBIAN32__)
#elif !defined(_WIN32)
	g_Config.memCardDirectory = std::string(getenv("HOME")) + "/.ppsspp/";
#endif

	// Try to find the flash0 directory.  Often this is from a subdirectory.
	for (int i = 0; i < 3; ++i)
	{
		if (!File::Exists(g_Config.flash0Directory))
			g_Config.flash0Directory += "../../flash0/";
	}
	// Or else, maybe in the executable's dir.
	if (!File::Exists(g_Config.flash0Directory))
		g_Config.flash0Directory = File::GetExeDirectory() + "flash0/";

	if (screenshotFilename != 0)
		headlessHost->SetComparisonScreenshot(screenshotFilename);

#ifdef ANDROID
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

	host->ShutdownGL();
	delete host;
	host = NULL;
	headlessHost = NULL;

#ifdef ANDROID_NDK_PROFILER
	moncleanup();
#endif

	return 0;
}
