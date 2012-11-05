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

void printUsage()
{
	fprintf(stderr, "PPSSPP Headless\n");
	fprintf(stderr, "Usage: ppsspp-headless file.elf [-c] [-m] [-j] [-c]\n");
	fprintf(stderr, "See headless.txt for details.\n");
}

int main(int argc, const char* argv[])
{
	bool fullLog = false;
	bool useJit = false;
	bool autoCompare = false;
	
	const char *bootFilename = argc > 1 ? argv[1] : 0;
	const char *mountIso = 0;
	bool readMount = false;

	for (int i = 2; i < argc; i++)
	{
		if (readMount)
		{
			mountIso = argv[i];
			readMount = false;
			continue;
		}
		if (!strcmp(argv[i], "-m"))
			readMount = true;
		else if (!strcmp(argv[i], "-l"))
			fullLog = true;
		else if (!strcmp(argv[i], "-j"))
			useJit = true;
		else if (!strcmp(argv[i], "-c"))
			autoCompare = true;
	}

	if (!bootFilename)
	{
		printUsage();
		return 1;
	}

	host = new HeadlessHost();

	LogManager::Init();
	LogManager *logman = LogManager::GetInstance();
	
	for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; i++)
	{
		LogTypes::LOG_TYPE type = (LogTypes::LOG_TYPE)i;
		logman->SetEnable(type, fullLog);
		logman->SetLogLevel(type, LogTypes::LDEBUG);
		// logman->AddListener(type, logger);
	}

	CoreParameter coreParameter;
	coreParameter.fileToStart = bootFilename;
	coreParameter.mountIso = mountIso ? mountIso : "";
	coreParameter.startPaused = false;
	coreParameter.cpuCore = useJit ? CPU_JIT : CPU_INTERPRETER;
	coreParameter.gpuCore = GPU_NULL;
	coreParameter.enableSound = false;
	coreParameter.headLess = true;

	g_Config.bEnableSound = true;
	g_Config.bFirstRun = false;
	g_Config.bIgnoreBadMemAccess = true;

	std::string error_string;

	if (!PSP_Init(coreParameter, &error_string)) {
		fprintf(stderr, "Failed to start PSP executable. Error: %s\n", error_string.c_str());
		return 1;
	}

	coreState = CORE_RUNNING;

	while (coreState == CORE_RUNNING)
	{
		// Run for a frame at a time, just because.
		u64 nowTicks = CoreTiming::GetTicks();
		u64 frameTicks = usToCycles(1000000/60);
		mipsr4k.RunLoopUntil(nowTicks + frameTicks);
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

