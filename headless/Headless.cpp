// Headless version of PPSSPP, for testing using http://code.google.com/p/pspautotests/ .
// See headless.txt.
// To build on non-windows systems, just run CMake in the SDL directory, it will build both a normal ppsspp and the headless version.

#include <stdio.h>

#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/System.h"
#include "Core/HLE/sceUtility.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Host.h"
#include "Log.h"
#include "LogManager.h"
#include "native/input/input_state.h"

#include "Compare.h"
#include "StubHost.h"
#ifdef _WIN32
#include "Windows/OpenGLBase.h"
#include "WindowsHeadlessHost.h"
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

#ifndef _WIN32
InputState input_state;
#endif

void printUsage(const char *progname, const char *reason)
{
	if (reason != NULL)
		fprintf(stderr, "Error: %s\n\n", reason);
	fprintf(stderr, "PPSSPP Headless\n");
	fprintf(stderr, "This is primarily meant as a non-interactive test tool.\n\n");
	fprintf(stderr, "Usage: %s file.elf [options]\n\n", progname);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -m, --mount umd.cso   mount iso on umd:\n");
	fprintf(stderr, "  -l, --log             full log output, not just emulated printfs\n");

	HEADLESSHOST_CLASS h1;
	HeadlessHost h2;
	if (typeid(h1) != typeid(h2))
	{
		fprintf(stderr, "  --graphics            use the full gpu backend (slower)\n");
		fprintf(stderr, "  --screenshot=FILE     compare against a screenshot\n");
	}

	fprintf(stderr, "  -i                    use the interpreter\n");
	fprintf(stderr, "  -j                    use jit (default)\n");
	fprintf(stderr, "  -c, --compare         compare with output in file.expected\n");
	fprintf(stderr, "\nSee headless.txt for details.\n");
}

int main(int argc, const char* argv[])
{
	bool fullLog = false;
	bool useJit = true;
	bool autoCompare = false;
	bool useGraphics = false;
	
	const char *bootFilename = 0;
	const char *mountIso = 0;
	const char *screenshotFilename = 0;
	bool readMount = false;

	for (int i = 1; i < argc; i++)
	{
		if (readMount)
		{
			mountIso = argv[i];
			readMount = false;
			continue;
		}
		if (!strcmp(argv[i], "-m") || !strcmp(argv[i], "--mount"))
			readMount = true;
		else if (!strcmp(argv[i], "-l") || !strcmp(argv[i], "--log"))
			fullLog = true;
		else if (!strcmp(argv[i], "-i"))
			useJit = false;
		else if (!strcmp(argv[i], "-j"))
			useJit = true;
		else if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--compare"))
			autoCompare = true;
		else if (!strcmp(argv[i], "--graphics"))
			useGraphics = true;
		else if (!strncmp(argv[i], "--screenshot=", strlen("--screenshot=")) && strlen(argv[i]) > strlen("--screenshot="))
			screenshotFilename = argv[i] + strlen("--screenshot=");
		else if (bootFilename == 0)
			bootFilename = argv[i];
		else
		{
			if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))
				printUsage(argv[0], NULL);
			else
			{
				std::string reason = "Unexpected argument " + std::string(argv[i]);
				printUsage(argv[0], reason.c_str());
			}
			return 1;
		}
	}

	if (readMount)
	{
		printUsage(argv[0], "Missing argument after -m");
		return 1;
	}
	if (!bootFilename)
	{
		printUsage(argv[0], argc <= 1 ? NULL : "No executable specified");
		return 1;
	}

	HeadlessHost *headlessHost = useGraphics ? new HEADLESSHOST_CLASS() : new HeadlessHost();
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
	coreParameter.gpuCore = glWorking ? GPU_GLES : GPU_NULL;
	coreParameter.enableSound = false;
	coreParameter.fileToStart = bootFilename;
	coreParameter.mountIso = mountIso ? mountIso : "";
	coreParameter.startPaused = false;
	coreParameter.enableDebugging = false;
	coreParameter.printfEmuLog = true;
	coreParameter.headLess = true;
	coreParameter.renderWidth = 480;
	coreParameter.renderHeight = 272;
	coreParameter.outputWidth = 480;
	coreParameter.outputHeight = 272;
	coreParameter.pixelWidth = 480;
	coreParameter.pixelHeight = 272;
	coreParameter.unthrottle = true;

	g_Config.bEnableSound = false;
	g_Config.bFirstRun = false;
	g_Config.bIgnoreBadMemAccess = true;
	// Never report from tests.
	g_Config.sReportHost = "";
	g_Config.bAutoSaveSymbolMap = false;
	g_Config.bBufferedRendering = true;
	g_Config.bHardwareTransform = true;
#ifdef USING_GLES2
	g_Config.iAnisotropyLevel = 0;
#else
	g_Config.iAnisotropyLevel = 8;
#endif
	g_Config.bVertexCache = true;
	g_Config.bTrueColor = true;
	g_Config.ilanguage = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
	g_Config.iTimeFormat = PSP_SYSTEMPARAM_TIME_FORMAT_24HR;
	g_Config.bEncryptSave = true;
	g_Config.sNickName = "shadow";
	g_Config.iTimeZone = 60;
	g_Config.iDateFormat = PSP_SYSTEMPARAM_DATE_FORMAT_DDMMYYYY;
	g_Config.iButtonPreference = PSP_SYSTEMPARAM_BUTTON_CROSS;
	g_Config.iLockParentalLevel = 9;

#if defined(ANDROID)
#elif defined(BLACKBERRY) || defined(__SYMBIAN32__)
#elif !defined(_WIN32)
	g_Config.memCardDirectory = std::string(getenv("HOME"))+"/.ppsspp/";
	g_Config.flashDirectory = g_Config.memCardDirectory+"/flash/";
#endif

	if (!PSP_Init(coreParameter, &error_string)) {
		fprintf(stderr, "Failed to start %s. Error: %s\n", coreParameter.fileToStart.c_str(), error_string.c_str());
		printf("TESTERROR\n");
		return 1;
	}

	host->BootDone();

	if (screenshotFilename != 0)
		headlessHost->SetComparisonScreenshot(screenshotFilename);

	coreState = CORE_RUNNING;
	while (coreState == CORE_RUNNING)
	{
		// Run for a frame at a time, just because.
		u64 nowTicks = CoreTiming::GetTicks();
		u64 frameTicks = usToCycles(1000000/60);
		mipsr4k.RunLoopUntil(nowTicks + frameTicks);

		// If we were rendering, this might be a nice time to do something about it.
		if (coreState == CORE_NEXTFRAME) {
			coreState = CORE_RUNNING;
			headlessHost->SwapBuffers();
		}
	}

	host->ShutdownGL();
	PSP_Shutdown();

	delete host;
	host = NULL;
	headlessHost = NULL;

	if (autoCompare)
		CompareOutput(bootFilename);

	return 0;
}

