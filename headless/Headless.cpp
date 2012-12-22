// Headless version of PPSSPP, for testing using http://code.google.com/p/pspautotests/ .
// See headless.txt.
// To build on non-windows systems, just run CMake in the SDL directory, it will build both a normal ppsspp and the headless version.

#include <stdio.h>

#include "../Core/Config.h"
#include "../Core/Core.h"
#include "../Core/CoreTiming.h"
#include "../Core/System.h"
#include "../Core/MIPS/MIPS.h"
#include "../Core/Host.h"
#include "Log.h"
#include "LogManager.h"

// TODO: Get rid of this junk
class HeadlessHost : public Host
{
public:
	// virtual void StartThread()
	virtual void UpdateUI() {}

	virtual void UpdateMemView() {}
	virtual void UpdateDisassembly() {}

	virtual void SetDebugMode(bool mode) { }

	virtual void InitGL() {}
	virtual void BeginFrame() {}
	virtual void EndFrame() {}
	virtual void ShutdownGL() {}

	virtual void InitSound(PMixer *mixer) {}
	virtual void UpdateSound() {}
	virtual void ShutdownSound() {}

	// this is sent from EMU thread! Make sure that Host handles it properly!
	virtual void BootDone() {} 
	virtual void PrepareShutdown() {}

	virtual bool IsDebuggingEnabled() {return false;}
	virtual bool AttemptLoadSymbolMap() {return false;}
};

class PrintfLogger : public LogListener
{
public:
	void Log(LogTypes::LOG_LEVELS level, const char *msg)
	{
		switch (level)
		{
		case LogTypes::LDEBUG:
			printf("D %s", msg);
			break;
		case LogTypes::LINFO:
			printf("I %s", msg);
			break;
		case LogTypes::LERROR:
			printf("E %s", msg);
			break;
		case LogTypes::LWARNING:
			printf("W %s", msg);
			break;
		case LogTypes::LNOTICE:
		default:
			printf("N %s", msg);
			break;
		}
	}
};

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
	fprintf(stderr, "  -f                    use the fast interpreter\n");
	fprintf(stderr, "  -j                    use jit (overrides -f)\n");
	fprintf(stderr, "  -c, --compare         compare with output in file.expected\n");
	fprintf(stderr, "\nSee headless.txt for details.\n");
}

int main(int argc, const char* argv[])
{
	bool fullLog = false;
	bool useJit = false;
	bool fastInterpreter = false;
	bool autoCompare = false;
	
	const char *bootFilename = 0;
	const char *mountIso = 0;
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
		else if (!strcmp(argv[i], "-j"))
			useJit = true;
		else if (!strcmp(argv[i], "-f"))
			fastInterpreter = true;
		else if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--compare"))
			autoCompare = true;
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

	host = new HeadlessHost();

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
	coreParameter.fileToStart = bootFilename;
	coreParameter.mountIso = mountIso ? mountIso : "";
	coreParameter.startPaused = false;
	coreParameter.cpuCore = useJit ? CPU_JIT : (fastInterpreter ? CPU_FASTINTERPRETER : CPU_INTERPRETER);
	coreParameter.gpuCore = GPU_NULL;
	coreParameter.enableSound = false;
	coreParameter.headLess = true;
	coreParameter.printfEmuLog = true;

	g_Config.bEnableSound = false;
	g_Config.bFirstRun = false;
	g_Config.bIgnoreBadMemAccess = true;

	std::string error_string;

	if (!PSP_Init(coreParameter, &error_string)) {
		fprintf(stderr, "Failed to start %s. Error: %s\n", coreParameter.fileToStart.c_str(), error_string.c_str());
		printf("TESTERROR\n");
		return 1;
	}

	coreState = CORE_RUNNING;

	while (coreState == CORE_RUNNING)
	{
		// Run for a frame at a time, just because.
		u64 nowTicks = CoreTiming::GetTicks();
		u64 frameTicks = usToCycles(1000000/60);
		mipsr4k.RunLoopUntil(nowTicks + frameTicks);

		// If we were rendering, this might be a nice time to do something about it.
		if (coreState == CORE_NEXTFRAME)
			coreState = CORE_RUNNING;
	}

	// NOTE: we won't get here until I've gotten rid of the exit(0) in sceExitProcess or whatever it's called

	PSP_Shutdown();

	if (autoCompare)
	{
		std::string expect_filename = std::string(bootFilename).substr(strlen(bootFilename - 4)) + ".expected";
		if (File::Exists(expect_filename))
		{
			// TODO: Do the compare here
		}
		else
		{
			fprintf(stderr, "Expectation file %s not found", expect_filename.c_str());
		}
	}

	return 0;
}

