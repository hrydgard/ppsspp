// Headless version of PPSSPP, for testing using http://code.google.com/p/pspautotests/ .
// See headless.txt.
// To build on non-windows systems, just run CMake in the SDL directory, it will build both a normal ppsspp and the headless version.

#include <xtl.h>
#include <stdio.h>


#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/System.h"
#include "Core/HLE/sceUtility.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Host.h"
#include "Log.h"
#include "LogManager.h"
#include "native/input/input_state.h"
#include "native/base/NativeApp.h"

#include "Compare.h"
#include "StubHost.h"
#include "XboxHost.h"
#include "InputDevice.h"
#include "XinputDevice.h"

#include "XboxOnScreenDisplay.h"
#include "Gpu/Directx9/helper/global.h"

XinputDevice XInput;

// 1 megabyte
#define MB	(1024*1024)

// Add one line of text to the output buffer.
#define AddStr(a,b) (pstrOut += wsprintf( pstrOut, a, b ))

void displaymem()
{
    MEMORYSTATUS stat;
    CHAR strOut[1024], *pstrOut;

    // Get the memory status.
    GlobalMemoryStatus( &stat );

    // Setup the output string.
    pstrOut = strOut;
    AddStr( "%4u total MB of virtual memory.\n", stat.dwTotalVirtual / MB );
    AddStr( "%4u  free MB of virtual memory.\n", stat.dwAvailVirtual / MB );
    AddStr( "%4u total MB of physical memory.\n", stat.dwTotalPhys / MB );
    AddStr( "%4u  free MB of physical memory.\n", stat.dwAvailPhys / MB );
    AddStr( "%4u total MB of paging file.\n", stat.dwTotalPageFile / MB );
    AddStr( "%4u  free MB of paging file.\n", stat.dwAvailPageFile / MB );
    AddStr( "%4u  percent of memory is in use.\n", stat.dwMemoryLoad );

    // Output the string.
    OutputDebugString( strOut );
}


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
void GL_SwapBuffers() { 
	DebugBreak();
}
void NativeUpdate(InputState &input_state) { }
void NativeRender() { }

extern InputState input_state;


static inline float curve1(float x) {
	const float deadzone = 0.15f;
	const float factor = 1.0f / (1.0f - deadzone);
	if (x > deadzone) {
		return (x - deadzone) * (x - deadzone) * factor;
	} else if (x < -0.1f) {
		return -(x + deadzone) * (x + deadzone) * factor;
	} else {
		return 0.0f;
	}
}

static inline float clamp1(float x) {
	if (x > 1.0f) return 1.0f;
	if (x < -1.0f) return -1.0f;
	return x;
}

static void UpdateInput(InputState &input) {
	input.pad_buttons = 0;
	input.pad_lstick_x = 0;
	input.pad_lstick_y = 0;
	input.pad_rstick_x = 0;
	input.pad_rstick_y = 0;

	// Update input from xinput
	XInput.UpdateState(input);

	input.pad_buttons_down = (input.pad_last_buttons ^ input.pad_buttons) & input.pad_buttons;
	input.pad_buttons_up = (input.pad_last_buttons ^ input.pad_buttons) & input.pad_last_buttons;

	// Then translate pad input into PSP pad input. Also, add in tilt.
	static const int mapping[12][2] = {
		{PAD_BUTTON_A, CTRL_CROSS},
		{PAD_BUTTON_B, CTRL_CIRCLE},
		{PAD_BUTTON_X, CTRL_SQUARE},
		{PAD_BUTTON_Y, CTRL_TRIANGLE},
		{PAD_BUTTON_UP, CTRL_UP},
		{PAD_BUTTON_DOWN, CTRL_DOWN},
		{PAD_BUTTON_LEFT, CTRL_LEFT},
		{PAD_BUTTON_RIGHT, CTRL_RIGHT},
		{PAD_BUTTON_LBUMPER, CTRL_LTRIGGER},
		{PAD_BUTTON_RBUMPER, CTRL_RTRIGGER},
		{PAD_BUTTON_START, CTRL_START},
		{PAD_BUTTON_SELECT, CTRL_SELECT},
	};

	for (int i = 0; i < 12; i++) {
		if (input.pad_buttons_down & mapping[i][0]) {
			__CtrlButtonDown(mapping[i][1]);
		}
		if (input.pad_buttons_up & mapping[i][0]) {
			__CtrlButtonUp(mapping[i][1]);
		}
	}

	float stick_x = input.pad_lstick_x;
	float stick_y = input.pad_lstick_y;
	float rightstick_x = input.pad_rstick_x;
	float rightstick_y = input.pad_rstick_y;

	// Apply tilt to left stick
	if (g_Config.bAccelerometerToAnalogHoriz) {
		// TODO: Deadzone, etc.
		stick_x += clamp1(curve1(input.acc.y) * 2.0f);
		stick_x = clamp1(stick_x);
	}

	__CtrlSetAnalogX(stick_x, 0);
	__CtrlSetAnalogY(stick_y, 0);

	__CtrlSetAnalogX(rightstick_x, 1);
	__CtrlSetAnalogY(rightstick_y, 1);

	input.pad_last_buttons = input.pad_buttons;
}


std::string System_GetProperty(SystemProperty prop) { return ""; }

extern bool useVsync;

int main(int argc, const char* argv[])
{
	bool fullLog = true;
	bool useJit = true;
	bool autoCompare = false;
	bool useGraphics = false;
	
	const char *bootFilename = 0;
	const char *mountIso = 0;
	const char *screenshotFilename = 0;
	bool readMount = false;

	displaymem();

	DWORD launchSize = 0;
	XGetLaunchDataSize(&launchSize);

	useJit = true;
	
	//bootFilename = "game:\\AI Go.iso";
	//bootFilename = "game:\\PSP_-_Puzzle_Bobble.ISO";
	//bootFilename = "game:\\cube.elf";
	//bootFilename = "game:\\string.prx";
	//bootFilename = "game:\\PSP\\GAME\\PSPRICK\\EBOOT.PBP";

	//bootFilename = "game:\\wippure.iso";

	//bootFilename = "game:\\nl-wipeoutp.iso";
	//bootFilename = "game:\\TRSI-TOE.ISO";
	//bootFilename = "game:\\nl-mhf2.iso";
	//bootFilename = "game:\\pa-sstars.iso";
	//bootFilename = "game:\\as-pmcee.iso";
	//bootFilename = "game:\\Barykelo-FF3.ISO";
	
	// bootFilename = "game:\\tests\\video\\pmf\\pmf.prx"; // FAIL
	// bootFilename = "game:\\tests\\video\\mpeg\\basic.prx"; // FAIL

	//bootFilename = "game:\\psp.iso";
	bootFilename = "game:\\[Psp] - Ridge Racer 2.cso";
	//bootFilename = "game:\\fantasia2.cso";
	//bootFilename = "game:\\Monster Hunter 3rd Patcher Fr.iso";

	//bootFilename = "game:\\Dragon Ball Z Tenkaichi Tag Team.cso";
	//bootFilename = "game:\\Crash Tag Team Racing.cso";

	//bootFilename = "game:\\em-ff7cc.iso";

//	bootFilename = "game:\\ind-gowgsp.iso";

	//bootFilename = "game:\\PSPKiNG-pfg.ISO";

	//bootFilename = "game:\\2ch-disg.iso";
	//bootFilename = "game:\\God of War Chains of Olympus [Rip].cso";
	//bootFilename = "game:\\Bleach.iso";
	//bootFilename = "game:\\0-khbbs.iso";
	//bootFilename = "game:\\Castlevania Dracula X Chronicle.cso";
	
	//bootFilename = "game:\\pgs-bof3.iso";

	//bootFilename = "game:\\wkco-rant.iso";

	//bootFilename = "game:\\Barykelo-FF3.ISO";
	//bootFilename = "game:\\[Psp] - Tekken Dark Resurrection.cso";
	//bootFilename = "game:\\psy-scbd.iso";

	//bootFilename = "game:\\pspking-d012dff.ISO";
	//bootFilename = "game:\\et-brave.iso";

	
	//bootFilename = "game:\\b-valpro.iso";

	//bootFilename = "game:\\b-valpro.iso";

	//bootFilename = "game:\\pspking-lssh.ISO";

	//bootFilename = "game:\\psp.iso";
	//bootFilename = "game:\\psy-sre.iso";
	
	//bootFilename = "game:\\pspking-nsunh3.ISO";
	//bootFilename = "game:\\naruto.ISO";

	// Savedata tests
	//bootFilename = "game:\\tests\\utility\\savedata\\autosave.prx"; //  Same as result as pc version => Fail ?	
	//bootFilename = "game:\\tests\\utility\\savedata\\filelist.prx"; // PASS
	// bootFilename = "game:\\tests\\utility\\savedata\\getsize.prx"; // PASS
	// bootFilename = "game:\\tests\\utility\\savedata\\idlist.prx"; // PASS
	// bootFilename = "game:\\tests\\utility\\savedata\\makedata.prx"; // Same as result as pc version => Fail ?
	// bootFilename = "game:\\tests\\utility\\savedata\\sizes.prx"; // Same as result as pc version => Fail ?

	// bootFilename = "game:\\tests\\utility\\systemparam\\systemparam.prx"; // PASS

	// bootFilename = "game:\\tests\\font\\fonttest.prx";

	// bootFilename = "game:\\tests\\mstick\\mstick.prx"; // PASS

	// bootFilename = "game:\\tests\\threads\\lwmutex\\create.prx"; // PASS
	// bootFilename = "game:\\tests\\threads\\lwmutex\\delete.prx"; // PASS
	// bootFilename = "game:\\tests\\threads\\lwmutex\\lock.prx"; // PASS
	// bootFilename = "game:\\tests\\threads\\lwmutex\\priority.prx"; // PASS
	// bootFilename = "game:\\tests\\threads\\lwmutex\\refer.prx"; // PASS
	// bootFilename = "game:\\tests\\threads\\lwmutex\\try.prx"; // PASS
	// bootFilename = "game:\\tests\\threads\\lwmutex\\try600.prx"; // PASS
	// bootFilename = "game:\\tests\\threads\\lwmutex\\unlock.prx"; // PASS

	// bootFilename = "game:\\tests\\threads\\mutex\\cancel.prx";	// FAIL (same as PC ?)
	// bootFilename = "game:\\tests\\threads\\mutex\\create.prx";	// PASS
	// bootFilename = "game:\\tests\\threads\\mutex\\delete.prx";	// PASS
	// bootFilename = "game:\\tests\\threads\\mutex\\lock.prx";		// PASS
	// bootFilename = "game:\\tests\\threads\\mutex\\priority.prx";	// PASS
	// bootFilename = "game:\\tests\\threads\\mutex\\refer.prx";	// PASS
	// bootFilename = "game:\\tests\\threads\\mutex\\try.prx";		// PASS
	// bootFilename = "game:\\tests\\threads\\mutex\\unlock.prx";	// PASS

	//	bootFilename = "game:\\demos\\celshading.elf";			// FAIL
	//bootFilename = "game:\\demos\\cubevfpu.prx";			// FAIL
	//bootFilename = "game:\\tests\\cpu\\vfpu\\colors.prx";	// FAIL
	//	bootFilename = "game:\\tests\\cpu\\vfpu\\convert.prx";	// FAIL
	//	bootFilename = "game:\\tests\\cpu\\vfpu\\gum.prx";		// FAIL
	//	bootFilename = "game:\\tests\\cpu\\vfpu\\matrix.prx";	// FAIL
	//	bootFilename = "game:\\tests\\cpu\\vfpu\\prefixes.prx";	// FAIL
	//  bootFilename = "game:\\tests\\cpu\\vfpu\\vector.prx";	// FAIL
	//bootFilename = "game:\\tests\\gpu\\reflection\\reflection.prx";	// FAIL

	
	
	//bootFilename = "game:\\cube.elf";
	//bootFilename = "game:\\demos\\celshading.elf";			// FAIL

	//bootFilename = "game:\\demos\\lights.pbp";			// FAIL
	//bootFilename = "game:\\demos\\clut.pbp";			// FAIL
	//bootFilename = "game:\\demos\\envmap.pbp";			// FAIL

	//bootFilename = "game:\\demos\\morphskin.elf";

	//bootFilename = "game:\\demos\\morph.elf";

	//bootFilename = "game:\\demos\\reflection.pbp";

	//bootFilename = "game:\\demos\\skinning.pbp";

//	bootFilename = "game:\\demos\\3dstudio.prx";


	//bootFilename = "game:\\demos\\ortho.pbp";

	//bootFilename = "game:\\demos\\rendertarget.elf";

	//bootFilename = "game:\\psy-ff1e.iso";

	//bootFilename = "game:\\b-gowcoo.iso";

	//bootFilename = "game:\\0-mgspw.iso";

	//bootFilename = "game:\\psy-mmhx.iso"; // infinite loop at access ...

	//bootFilename = "game:\\enigmaconsole-castelvaniax.iso";


	//bootFilename = "game:\\tests\\cpu\\fpu\\fpu.prx";	// FAIL


	//bootFilename = "game:\\resistance.retribution.eur.psp.iso";

	/*
	swap32_struct_t l;
	printf("Szir of u32_le: %d\r\n", sizeof(u32_le));
	*/
	DX9::DirectxInit(NULL);

	XboxHost *xbhost = new XboxHost();
	host = xbhost;

	std::string error_string;
	bool glWorking = host->InitGL(&error_string);

	LogManager::Init();

	CoreParameter coreParameter;

	
	g_Config.Load("game:\\ppsspp.ini", "game:\\controls.ini");

#if 1
	//coreParameter.cpuCore = CPU_INTERPRETER;
	coreParameter.cpuCore = CPU_JIT;
	coreParameter.gpuCore = GPU_DIRECTX9;
	coreParameter.enableSound = true;
	coreParameter.fileToStart = bootFilename;
	coreParameter.mountIso = "";
	coreParameter.startPaused = false;
	coreParameter.enableDebugging = false;
	coreParameter.printfEmuLog = true;
	coreParameter.headLess = false;
	coreParameter.renderWidth = 1920;
	coreParameter.renderHeight = 1080;
	coreParameter.outputWidth = 480*2;
	coreParameter.outputHeight = 272*2;
	coreParameter.pixelWidth = 1920;
	coreParameter.pixelHeight = 1080;
	coreParameter.unthrottle = true;

#ifdef _DEBUG
	g_Config.bEnableLogging = true;
#endif
	g_Config.bEnableSound = true;
	g_Config.bFirstRun = false;
	g_Config.bIgnoreBadMemAccess = true;
	g_Config.bFastMemory = true;
	// Never report from tests.
	g_Config.sReportHost = "";
	g_Config.bAutoSaveSymbolMap = false;
	/*
	FB_NON_BUFFERED_MODE	= 0,
	FB_BUFFERED_MODE		= 1,
	FB_READFBOMEMORY_CPU	= 2,
	FB_READFBOMEMORY_GPU	= 3,
	*/
	g_Config.iRenderingMode = 0;
	g_Config.bHardwareTransform = true;

	g_Config.iAnisotropyLevel = 8;

	g_Config.bVertexCache = false;
	g_Config.bTrueColor = true;
	g_Config.iLanguage = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
	g_Config.iTimeFormat = PSP_SYSTEMPARAM_TIME_FORMAT_24HR;
	g_Config.bEncryptSave = true;
	g_Config.sNickName = "shadow";
	g_Config.iTimeZone = 60;
	g_Config.iDateFormat = PSP_SYSTEMPARAM_DATE_FORMAT_DDMMYYYY;
	g_Config.iButtonPreference = PSP_SYSTEMPARAM_BUTTON_CROSS;
	g_Config.iLockParentalLevel = 9;
	g_Config.iShowFPSCounter = true;

#if 1
	g_Config.bSeparateCPUThread = true;
	g_Config.bSeparateIOThread = true;
#else
	g_Config.bSeparateCPUThread = false;
	g_Config.bSeparateIOThread = false;
#endif

	g_Config.iTexScalingLevel = 0;
	g_Config.iTexScalingType = 0;
	g_Config.iNumWorkerThreads = 5;


	// Wait
	g_Config.iFpsLimit = 60;

	// Needed for gow
	g_Config.iForceMaxEmulatedFPS = 60;

	// Auto frameskip
	g_Config.iFrameSkip = 0;

	// Speed Hack ?
	g_Config.iLockedCPUSpeed = 111;
#else	
	coreParameter.renderWidth = 1280;
	coreParameter.renderHeight = 720;
	coreParameter.outputWidth = 480*2;
	coreParameter.outputHeight = 272*2;
	coreParameter.pixelWidth = 1280;
	coreParameter.pixelHeight = 720;	
	
	coreParameter.cpuCore = CPU_JIT;
	coreParameter.gpuCore = GPU_DIRECTX9;
#endif

	coreParameter.fileToStart = bootFilename;

	g_Config.memCardDirectory = "game:\\memstick\\";
	g_Config.flash0Directory = "game:\\flash0\\";


	// Set ...
	g_Config.Save();

	if (!PSP_Init(coreParameter, &error_string)) {
		fprintf(stderr, "Failed to start %s. Error: %s\n", coreParameter.fileToStart.c_str(), error_string.c_str());
		printf("TESTERROR\n");
		return 1;
	}

	host->BootDone();

	
	xbhost->BeginFrame();

	coreState = CORE_RUNNING;
	while (coreState == CORE_RUNNING)
	{

		// Run for a frame at a time, just because.
		u64 nowTicks = CoreTiming::GetTicks();
		u64 frameTicks = usToCycles(1000000/60);

		PSP_RunLoopUntil(nowTicks + frameTicks);
		//mipsr4k.RunLoopUntil(nowTicks + frameTicks);

		UpdateInput(input_state);

		// If we were rendering, this might be a nice time to do something about it.
		if (coreState == CORE_NEXTFRAME) {
			coreState = CORE_RUNNING;

			
			xbhost->EndFrame();
			xbhost->SwapBuffers();
			xbhost->BeginFrame();
		}
	}

	host->ShutdownGL();
	PSP_Shutdown();

	delete host;
	host = NULL;
	xbhost = NULL;

	return 0;
}

