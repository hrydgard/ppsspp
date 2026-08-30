
// Headless version of PPSSPP, for testing using http://code.google.com/p/pspautotests/ .
// See headless/README.md.
// To build on non-windows systems, just run CMake in the SDL directory, it will build both a normal ppsspp and the headless version.
//
// Example command line to run a test in the VS debugger (useful to debug failures):
// > --root pspautotests/tests/../ --compare --timeout=5 --graphics=software pspautotests/tests/cpu/cpu_alu/cpu_alu.prx
// Example command line for taking screenshots from a frame dump:
// > -l --graphics=vulkan --screenshot-save=vt_ref.bmp "D:\PSP ISO\dump\Depth\11578 Virtua Tennis pause menu ULES00126_0002.zip" --resolution-scale=2
// Example command line for messing with the vsh:
// > -l --vsh --memread=break --memwrite=break --break=break
// Example command line for looking at crash output:
// > -l --graphics=software pspautotests/tests/cpu/crash/crash_read_f32.prx
//
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
#include "Common/System/Request.h"
#include "Common/System/System.h"

#include "Common/CommonWindows.h"
#if PPSSPP_PLATFORM(WINDOWS)
#if PPSSPP_API(ANY_GL)
#include "Windows/GPU/WindowsGLContext.h"
#endif
#include "Windows/GPU/D3D11Context.h"
#else
#include <csignal>
#endif
#include "Common/CPUDetect.h"
#include "Common/ExceptionHandlerSetup.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/VFS/ZipFileReader.h"
#include "Common/File/VFS/DirectoryReader.h"
#include "Common/File/FileUtil.h"
#include "Common/GPU/GraphicsContext.h"
#include "Common/Net/Resolve.h"
#include "Common/TimeUtil.h"
#include "Common/StringUtils.h"
#include "Common/Thread/ThreadManager.h"
#include "Common/GPU/Vulkan/VulkanGraphicsContext.h"
#include "Core/CmdLine.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/EmuThread.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/System.h"
#include "Core/Util/PSARUnpack.h"
#include "Core/WebServer.h"
#include "Core/HLE/sceUtility.h"
#include "Core/SaveState.h"
#include "GPU/GPUCommon.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "Common/Log.h"
#include "Common/Log/LogManager.h"

#include "Compare.h"
#if defined(_WIN32)
#include "WindowsHeadlessHost.h"
#elif defined(SDL)
#include "SDLHeadlessGLGraphicsContext.h"
#endif

static Path g_comparisonScreenshot;
static Path g_screenshotSavePath;
static Path g_screenshotDiffPath;
static bool g_screenshotSaveKeepAlpha = false;
static double g_maxScreenshotError = 0.0;
static bool g_screenshotFailed = false;
static std::string g_debugOutputBuffer;
static bool g_writeFailureScreenshot = true;
static bool g_writeDebugOutput = true;
// Set from the savestate callback on the emu thread, read after it has been joined.
static bool g_stateLoadFailed = false;

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
void NativeResized() { }

void System_LaunchUrl(LaunchUrlType urlType, std::string_view url) {}
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
	case SYSPROP_IS_HEADLESS: return true;
	case SYSPROP_CAN_JIT: return true;
	default:
		return false;
	}
}
void System_Notify(SystemNotification notification) {}
void System_PostUIMessage(UIMessage message, std::string_view param) {}
void System_RunOnMainThread(std::function<void()>) {}
std::vector<std::string> System_GetCameraDeviceList() { return std::vector<std::string>(); }
void System_AskForPermission(SystemPermission permission) {}
PermissionStatus System_GetPermissionStatus(SystemPermission permission) { return PERMISSION_STATUS_GRANTED; }
void System_AudioGetDebugStats(char *buf, size_t bufSize) { if (buf) buf[0] = '\0'; }
void System_AudioClear() {}
void System_AudioPushSamples(const s32 *audio, int numSamples, float volume) {}
bool System_MakeRequest(SystemRequestType type, int requestId, const std::string &param1, const std::string &param2, int64_t param3, int64_t param4) { return false; }

// TODO: To avoid having to define these here, these should probably be turned into system "requests".
bool NativeSaveSecret(std::string_view nameOfSecret, std::string_view data) { return false; }
std::string NativeLoadSecret(std::string_view nameOfSecret) {
	return "";
}

int printUsage(const CommandLineOptions &options, const char *progname, const char *reason) {
	options.PrintUsage(progname, reason);
	fprintf(stderr, "  --ir                  use ir interpreter\n");
	fprintf(stderr, "  --flash0=DIR          use DIR as flash0: instead of the bundled assets (e.g. a real dump)\n");
	return 1;
}

void FlushDebugOutput() {
	if (!g_debugOutputBuffer.empty()) {
		fwrite(g_debugOutputBuffer.data(), sizeof(char), g_debugOutputBuffer.length(), stdout);
		g_debugOutputBuffer.clear();
	}
}

void SetComparisonScreenshot(const Path &filename, double maxError) {
	g_comparisonScreenshot = filename;
	g_maxScreenshotError = maxError;
}
void SetScreenshotSavePath(const Path &filename) {
	g_screenshotSavePath = filename;
}
void SetWriteFailureScreenshot(bool flag) {
	g_writeFailureScreenshot = flag;
}

void SendDebugOutput(std::string_view output) {
	if (!g_writeDebugOutput)
		return;
#ifdef _WIN32
	std::string str(output);
	OutputDebugStringUTF8(str.c_str());
#endif
	if (output.find('\n') != output.npos) {
		g_debugOutputBuffer += output;
		FlushDebugOutput();
	} else {
		g_debugOutputBuffer += output;
	}
}

void SendAndCollectOutput(std::string_view output) {
	SendDebugOutput(output);
	if (PSP_CoreParameter().collectDebugOutput) {
		*PSP_CoreParameter().collectDebugOutput += output;
	}
}

void SendDebugScreenshot(const DebugScreenshotDesc &desc) {
	const u8 *pixbuf = (const u8 *)desc.data;
	u32 w = desc.stride;
	u32 h = desc.height;

	// We ignore the current framebuffer parameters and just grab the full screen.
	// TOOD: Uh, why not use them? They should be the same.
	const static u32 FRAME_STRIDE = 512;
	const static u32 FRAME_WIDTH = 480;
	const static u32 FRAME_HEIGHT = 272;

	GPUDebugBuffer buffer;
	gpu->GetCurrentFramebuffer(buffer, GPU_DBG_FRAMEBUF_DISPLAY);
	const std::vector<u32> pixels = TranslateDebugBufferToCompare(&buffer, FRAME_STRIDE, FRAME_HEIGHT);

	// If a screenshot save path is set, save unconditionally.
	if (!g_screenshotSavePath.empty()) {
		ScreenshotComparer saver(pixels, FRAME_STRIDE, FRAME_WIDTH, FRAME_HEIGHT);
		bool saved = g_screenshotSavePath.GetFileExtension() == ".png" ? saver.SaveActualPNG(g_screenshotSavePath, g_screenshotSaveKeepAlpha) : saver.SaveActualBitmap(g_screenshotSavePath);
		if (saved)
			SendAndCollectOutput("Screenshot saved to: " + g_screenshotSavePath.ToVisualString() + "\n");
	}

	// Only compare if we have a reference.
	if (g_comparisonScreenshot.empty()) {
		return;
	}

	ScreenshotComparer comparer(pixels, FRAME_STRIDE, FRAME_WIDTH, FRAME_HEIGHT);
	double errors = comparer.Compare(g_comparisonScreenshot);
	if (errors < 0) {
		SendAndCollectOutput(comparer.GetError() + "\n");
		g_screenshotFailed = true;
	}

	if (errors > g_maxScreenshotError) {
		SendAndCollectOutput(StringFromFormat("Screenshot MSE: %f\n", errors));
		g_screenshotFailed = true;
	}

	if (errors > g_maxScreenshotError && g_writeFailureScreenshot) {
		if (comparer.SaveActualBitmap(Path("__testfailure.bmp")))
			SendAndCollectOutput("Actual output written to: __testfailure.bmp\n");
		comparer.SaveVisualComparisonPNG(Path("__testcompare.png"));
	}

	// If a diff path is set, always save the visual comparison (regardless of pass/fail).
	if (!g_screenshotDiffPath.empty() && errors >= 0) {
		if (comparer.SaveVisualComparisonPNG(g_screenshotDiffPath))
			SendAndCollectOutput("Screenshot comparison saved to: " + g_screenshotDiffPath.ToVisualString() + "\n");
	}
}

static GraphicsContext *CreateGraphicsContext(GPUCore gpuCore, std::string **deviceSetting) {
#ifdef SDL
	*deviceSetting = nullptr;
	switch (gpuCore) {
	case GPUCORE_GLES:
		return new SDLHeadlessGLGraphicsContext();
	case GPUCORE_VULKAN:
		*deviceSetting = &g_Config.sVulkanDevice;
		return new VulkanGraphicsContext();
	default:
		return nullptr;
	}
#elif PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
	switch (gpuCore) {
#if PPSSPP_API(ANY_GL)
	case GPUCORE_GLES:
		*deviceSetting = nullptr;
		return new WindowsGLContext();
#endif
	case GPUCORE_DIRECTX11:
		*deviceSetting = &g_Config.sD3D11Device;
		return new D3D11Context();
	case GPUCORE_VULKAN:
		*deviceSetting = &g_Config.sVulkanDevice;
		return new VulkanGraphicsContext();
	case GPUCORE_SOFTWARE:
	default:
		return nullptr;
	}
#elif PPSSPP_ARCH(LOONGARCH64)
	// The loongarch64 cross-compilation toolchain has no SDL3 packages available (see the
	// LOONGARCH64_DEVICE branch in CMakeLists.txt), so this build is compile-tested only and
	// never actually needs to create a graphics context at runtime.
	*deviceSetting = nullptr;
	return nullptr;
#elif PPSSPP_PLATFORM(ANDROID)
	// The Android headless build is compile-tested only and never actually needs to create a graphics context at runtime.
	// However, it would still be nice if hardware rendering worked - it can be executed over adb.
	*deviceSetting = nullptr;
	return nullptr;
#else
#error The Headless build is not supported on this platform. Please use SDL (Mac/Linux) or Windows (non-UWP).
	return nullptr;
#endif
}

struct AutoTestOptions {
	double timeout;
	double maxScreenshotError;
	bool compare;
	bool verbose;
	bool bench;
	bool printEqualLines;
};

static bool RunAutoTest(GraphicsContext *graphicsContext, CoreParameter &coreParameter, const AutoTestOptions &opt) {
	using namespace Draw;

	// Kinda ugly, trying to guesstimate the test name from filename...
	currentTestName = GetTestName(coreParameter.fileToStart);
	g_screenshotFailed = false;

	std::string output;
	if (opt.compare || opt.bench) {
		coreParameter.collectDebugOutput = &output;
	}

	if (!PSP_InitStart(coreParameter)) {
		// Shouldn't really happen anymore, the errors happen later in PSP_InitUpdate.
		fprintf(stderr, "Failed to start '%s'.\n", coreParameter.fileToStart.c_str());
		printf("TESTERROR\n");
		GitHubActionsPrint("error", "PRX/ELF missing for %s", currentTestName.c_str());
		return false;
	}

	if (opt.compare) {
		SetComparisonScreenshot(ExpectedScreenshotFromFilename(coreParameter.fileToStart), opt.maxScreenshotError);
	}

	std::string error_string;
	while (PSP_InitUpdate(&error_string) == BootState::Booting) {
		sleep_ms(1, "auto-test");
	}

	if (!PSP_IsInited()) {
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
	while (coreState == CORE_RUNNING_CPU || coreState == CORE_STEPPING_CPU) {
		// Savestate loads/saves are queued and applied here, same as EmuScreen::render does in the
		// app. Without this, --state silently did nothing at all.
		SaveState::Process();

		int blockTicks = (int)usToCycles(1000000 / 10);
		PSP_RunLoopFor(blockTicks);

		// If we were rendering, this might be a nice time to do something about it.
		if (coreState == CORE_NEXTFRAME) {
			// INFO_LOG(Log::System, "(frame)");
			coreState = CORE_RUNNING_CPU;
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

				SendDebugOutput("TIMEOUT\n");
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
		// Vulkan may get angry if we don't do a final present.
		if (gpu) {
			gpu->SetCurFramebufferDirty(true);
			gpu->PrepareCopyDisplayToOutput(g_Config.GetDisplayLayoutConfig(DeviceOrientation::Landscape));
			draw->BindFramebufferAsRenderTarget(nullptr, {RPAction::CLEAR, RPAction::DONT_CARE, RPAction::DONT_CARE}, "BackBuffer");
			gpu->CopyDisplayToOutput(g_Config.GetDisplayLayoutConfig(DeviceOrientation::Landscape));
		}

		draw->EndFrame();
	}

	PSP_Shutdown(true);

	if (!opt.bench) {
		FlushDebugOutput();
	}

	if (opt.compare && passed) {
		passed = CompareOutput(coreParameter.fileToStart, output, opt.verbose, opt.printEqualLines);
	}

	// Screenshot comparison failures are recorded in SendDebugScreenshot.
	if (!g_comparisonScreenshot.empty() && g_screenshotFailed) {
		passed = false;
	}

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

static void AddToTestsByPath(std::vector<std::string> *tests, std::string_view path) {
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

// Returns the retval that will be returned from main.
int RunTests(GraphicsContext *graphicsContext, CoreParameter &coreParameter, const AutoTestOptions &testOptions, const std::vector<std::string> &testFilenames) {
	std::vector<std::string> failedTests;
	std::vector<std::string> passedTests;
	std::vector<std::string> missingTests;

	for (size_t i = 0; i < testFilenames.size(); ++i) {
		coreParameter.fileToStart = Path(testFilenames[i]);
		if (!File::Exists(coreParameter.fileToStart)) {
			fprintf(stderr, "File not found: %s\n", coreParameter.fileToStart.c_str());
			missingTests.push_back(testFilenames[i]);
			continue;
		}
		if (testOptions.compare) {
			printf("%s:\n", coreParameter.fileToStart.c_str());
		}
		const bool passed = RunAutoTest(graphicsContext, coreParameter, testOptions);
		if (testOptions.bench) {
			double st = time_now_d();
			double deadline = st + testOptions.timeout;
			double runs = 0.0;
			for (int i = 0; i < 100; ++i) {
				RunAutoTest(graphicsContext, coreParameter, testOptions);
				runs++;
				if (time_now_d() > deadline) {
					break;
				}
			}
			double et = time_now_d();

			std::string testName = GetTestName(coreParameter.fileToStart);
			printf("  %s - %f seconds average\n", testName.c_str(), (et - st) / runs);
		}
		if (testOptions.compare || !g_comparisonScreenshot.empty()) {
			std::string testName = GetTestName(coreParameter.fileToStart);
			if (passed) {
				passedTests.push_back(testName);
				printf("  %s - passed!\n", testName.c_str());
			} else {
				failedTests.push_back(testName);
			}
		}
	}

	if (testOptions.compare || !g_comparisonScreenshot.empty()) {
		printf("%d tests passed, %d tests failed, %d tests missing.\n", (int)passedTests.size(), (int)failedTests.size(), (int)missingTests.size());
		if (!failedTests.empty()) {
			printf("Failed tests:\n");
			for (size_t i = 0; i < failedTests.size(); ++i) {
				printf("  %s\n", failedTests[i].c_str());
			}
			return 1;
		}
	}

	return 0;
}

class HeadlessApplication : public Application {
public:
	bool InitGraphics(GraphicsContext *graphicsContext) override {
		Core_SetGraphicsContext(graphicsContext);
		return true;
	}
	void ShutdownGraphics(GraphicsContext *graphicsContext) override {
		graphicsContext->NotifyEmuThreadExit();
	}
};

int main(int argc, const char* argv[]) {
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

	SetupCRT(true);

	CommandLineOptions cmdLineOptions;
	CommandLineParseResult parseResult = cmdLineOptions.Parse(argc, argv, CmdLineMode::Headless);
	switch (parseResult) {
	case CommandLineParseResult::Exit:
		return 0;
	case CommandLineParseResult::Error:
		return 1;
	case CommandLineParseResult::Continue:
		break;
	}

	if (cmdLineOptions.generateInterpreterDispatch.value_or(false)) {
		std::string code = GenerateInterpreterDispatch();
		fwrite(code.data(), 1, code.size(), stdout);
		return 0;
	}

	// Needed before any sockets can be used (WSAStartup on Windows) - without this, the
	// WebSocket debugger silently fails to listen. Only done when requested since headless
	// otherwise has no use for networking.
	if (cmdLineOptions.debuggerPort.has_value())
		net::Init();

	AutoTestOptions testOptions{};
	testOptions.compare = cmdLineOptions.compare.value_or(false);
	testOptions.bench = cmdLineOptions.bench.value_or(false);
	testOptions.timeout = cmdLineOptions.timeout.value_or(std::numeric_limits<double>::infinity());
	testOptions.verbose = cmdLineOptions.verbose.value_or(false);
	testOptions.printEqualLines = cmdLineOptions.printEqualLines.value_or(false);
	testOptions.maxScreenshotError = cmdLineOptions.maxScreenshotError.value_or(0.0);

	bool fullLog = cmdLineOptions.enableLogging.value_or(false);
	const char *stateToLoad = cmdLineOptions.stateToLoad.has_value() ? cmdLineOptions.stateToLoad.value().c_str() : nullptr;
	bool oldAtrac = false;
	bool outputDebugStringLog = cmdLineOptions.odsLog.value_or(false);

	std::vector<std::string> testFilenames;
	const std::vector<std::string> &ignoredTests = cmdLineOptions.ignoredTests;
	std::string mountIso = cmdLineOptions.mountIso.value_or("");
	std::string mountRoot;

	if (cmdLineOptions.root.has_value()) {
		mountRoot = cmdLineOptions.root.value().c_str();
	}

	for (const std::string &filename : cmdLineOptions.bootFilenames) {
		AddToTestsByPath(&testFilenames, filename);
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

	g_Config.bEnableLogging = (fullLog || outputDebugStringLog);
	g_logManager.Init(&g_Config.bEnableLogging, outputDebugStringLog);

	for (int i = 0; i < (int)Log::NUMBER_OF_LOGS; i++) {
		Log type = (Log)i;
		g_logManager.SetEnabled(type, (fullLog || outputDebugStringLog));
		g_logManager.SetLogLevel(type, LogLevel::LDEBUG);  // TODO: Make the level configurable.
	}
	if (fullLog) {
		// Only with --log, add the printfLogger.
		g_logManager.EnableOutput(LogOutput::Printf);
	}

	// Unpacking an updater is a standalone action - no emulation involved, so do it here and exit
	// before any of the setup below.
	if (cmdLineOptions.unpackUpdater.has_value()) {
		if (cmdLineOptions.bootFilenames.size() != 1) {
			fprintf(stderr, "--unpack-updater takes exactly one updater, disc image or PSAR\n");
			return 1;
		}

		PSARUnpackOptions unpackOptions;
		unpackOptions.verbose = testOptions.verbose;
		if (cmdLineOptions.unpackUpdaterModel.has_value() &&
			!PSPModelGenerationFromString(cmdLineOptions.unpackUpdaterModel.value(), &unpackOptions.model)) {
			fprintf(stderr, "Unknown PSP model '%s' - expected 01g..12g or any\n", cmdLineOptions.unpackUpdaterModel.value().c_str());
			return 1;
		}
		PSARUnpackStats stats;
		std::string unpackError;
		const bool ok = UnpackUpdater(Path(cmdLineOptions.bootFilenames[0]), Path(cmdLineOptions.unpackUpdater.value()), unpackOptions, &stats, &unpackError);
		if (!ok) {
			fprintf(stderr, "Unpacking failed: %s\n", unpackError.c_str());
		}
		printf("Firmware %s (model %s): %d entries, %d files written, %d directories, %d unresolved names, %d for other models, %d failed\n",
			stats.firmwareVersion.c_str(), PSPModelGenerationToString(unpackOptions.model), stats.entries, stats.written,
			stats.directories, stats.unnamed, stats.otherModel, stats.failed);
		printf("Compression: none=%d zlib=%d KL4E=%d KL3E=%d LZR=%d unknown=%d\n",
			stats.compressionCounts[(int)PSARCompression::None],
			stats.compressionCounts[(int)PSARCompression::Zlib],
			stats.compressionCounts[(int)PSARCompression::KL4E],
			stats.compressionCounts[(int)PSARCompression::KL3E],
			stats.compressionCounts[(int)PSARCompression::LZR],
			stats.compressionCounts[(int)PSARCompression::Unknown]);
		return ok ? 0 : 1;
	}

	g_Config.RestoreDefaults(RestoreSettingsBits::SETTINGS | RestoreSettingsBits::CONTROLS | RestoreSettingsBits::RECENT, false);

	Core_RegisterDebugOutputListeners(&SendDebugOutput, &SendDebugScreenshot);

	// Needs to be after log so we don't interfere with test output.
	g_threadManager.Init(cpu_info.num_cores, cpu_info.logical_cpu_count);

	std::string error_string;

	// Force known values for deterministic test execution. This happens before
	// ApplyToConfig() below, so a matching command line flag can still override any of it -
	// ApplyToConfig() always has the final say on the settings in g_Config.
	//
	// This affects the test execution of pspautotests/tests/gpu/vertices/morph.prx, even though
	// we actually set the cpu core in CoreParameter below.
	// The check that decides that is in the DrawEngineCommon constructor.
	g_Config.iCpuCore = (int)CPUCore::INTERPRETER;

	// NOTE: In headless mode, we never save the config. This is just for this run.
	g_Config.iDumpFileTypes = 0;
	g_Config.bEnableSound = false;
	g_Config.bFirstRun = false;
	g_Config.bIgnoreBadMemAccess = false;
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
	g_Config.iInternalResolution = cmdLineOptions.resolutionScale.value_or(1);
	g_Config.bEnableLogging = (fullLog || outputDebugStringLog);
	g_Config.bVertexDecoderJit = true;
	g_Config.bSoftwareRendering = cmdLineOptions.softwareRendering.value_or(false);
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
	g_Config.bSkipDeadbeefFilling = false;

	// ApplyToConfig() has the final say, applied after RestoreDefaults() and the headless
	// overrides above, so a matching command line flag always wins.
	cmdLineOptions.ApplyToConfig();

	// This looks contradictory to the above. But, this preserves the old test behavior which apparently ran the JIT for the CPU
	// but ended up running software vertex decoding due to the setting in g_Config. Yeah, it's a mess.
	CPUCore cpuCore = CPUCore::JIT;
	if (cmdLineOptions.cpuCore.has_value()) {
		cpuCore = cmdLineOptions.cpuCore.value();
	}

	GPUCore gpuCore = GPUCORE_SOFTWARE;
	// Translate backend to core. We probably should consider merging these enums.
	if (!g_Config.bSoftwareRendering) {
		switch ((GPUBackend)g_Config.iGPUBackend) {
		case GPUBackend::OPENGL:
			gpuCore = GPUCORE_GLES;
			break;
		case GPUBackend::DIRECT3D11:
			gpuCore = GPUCORE_DIRECTX11;
			break;
		case GPUBackend::VULKAN:
			gpuCore = GPUCORE_VULKAN;
			break;
		}
	}

	// Time to set up graphics
	GraphicsContext *graphicsContext = nullptr;
	std::string *deviceSetting = nullptr;
	void *window = nullptr;
	WindowDesc windowDesc;
	if (g_Config.bSoftwareRendering) {
		// For software rendering, we just create a dummy graphics context (to share as much code as possible).
		// We don't bother with a window.
		graphicsContext = new NullGraphicsContext();
	} else {
#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_ARCH(LOONGARCH64)
		fprintf(stderr, "Headless graphics context creation is not supported on this platform.\n");
		return 1;
#else
		// TODO: Will we need a larger window for higher resolutions? Well, not if we use buffered rendering.
		window = CreateHiddenWindow(480, 272, cmdLineOptions.gpuBackend.value_or(GPUBackend::OPENGL), &windowDesc);
		if (!windowDesc.Valid()) {
			fprintf(stderr, "Failed to create a window for graphics context");
			return 1;
		}
		graphicsContext = CreateGraphicsContext(gpuCore, &deviceSetting);
		if (!graphicsContext) {
			// If we don't get the desired context, we DO NOT fall back.
			fprintf(stderr, "Failed to create a graphics context for GPU core");
			return 1;
		}
#endif
	}

	// TODO: This whole function should be refactored to set up CoreParameter in one place,
	// but not now.
	CoreParameter coreParameter;
	coreParameter.cpuCore = (CPUCore)cpuCore;
	coreParameter.gpuCore = (GPUCore)gpuCore;
	coreParameter.graphicsContext = graphicsContext;
	coreParameter.enableSound = false;
	coreParameter.mountIso = mountIso.empty() ? Path() : Path(mountIso);
	coreParameter.mountRoot = mountRoot.empty() ? Path() : Path(mountRoot);
	coreParameter.startBreak = false;
	coreParameter.headLess = true;
	coreParameter.loadGameConfigs = false;
	coreParameter.renderScaleFactor = cmdLineOptions.resolutionScale.value_or(1);
	coreParameter.renderWidth = 480 * coreParameter.renderScaleFactor;
	coreParameter.renderHeight = 272 * coreParameter.renderScaleFactor;
	coreParameter.pixelWidth = 480 * coreParameter.renderScaleFactor;
	coreParameter.pixelHeight = 272 * coreParameter.renderScaleFactor;
	coreParameter.fastForward = true;

	Path exePath = File::GetExeDirectory();

	// --memstick, applied by ApplyToConfig() further up, wins. This runs after it, so without the
	// check the default below would silently overwrite whatever was asked for.
	if (!cmdLineOptions.memStick.has_value()) {
		// TODO: Share this derivation with the main build.
#if PPSSPP_PLATFORM(WINDOWS)
		// Mount a filesystem
		g_Config.memStickDirectory = exePath / "memstick";
		File::CreateDir(g_Config.memStickDirectory, true);
		CreateSysDirectories();
#elif !PPSSPP_PLATFORM(ANDROID)
		g_Config.memStickDirectory = Path(std::string(getenv("HOME"))) / ".ppsspp";
#endif
	}
	g_Config.nandRootDirectory = GetSysDirectory(DIRECTORY_NAND);
	coreParameter.nandRoot = g_Config.nandRootDirectory;

	// Try to find the assets flash0 directory. Often this is from a subdirectory.
	// This is needed for our fallback fonts.
	Path nextPath = exePath;
	for (int i = 0; i < 5; ++i) {
		if (File::Exists(nextPath / "assets/flash0")) {
#if !PPSSPP_PLATFORM(ANDROID)
			g_VFS.Register("", new DirectoryReader(nextPath / "assets"));
#endif
			break;
		}

		if (!nextPath.CanNavigateUp())
			break;
		nextPath = nextPath.NavigateUp();
	}

	if (cmdLineOptions.screenshotFilename.has_value()) {
		SetComparisonScreenshot(Path(std::string(cmdLineOptions.screenshotFilename.value())), testOptions.maxScreenshotError);
	}
	if (cmdLineOptions.screenshotFilenameSave.has_value()) {
		SetScreenshotSavePath(Path(std::string(cmdLineOptions.screenshotFilenameSave.value())));
	}
	if (cmdLineOptions.screenshotFilenameDiff.has_value()) {
		g_screenshotDiffPath = Path(std::string(cmdLineOptions.screenshotFilenameDiff.value()));
	}
	if (cmdLineOptions.screenshotSaveKeepAlpha.has_value()) {
		g_screenshotSaveKeepAlpha = cmdLineOptions.screenshotSaveKeepAlpha.value();
	}

	SetWriteFailureScreenshot(!getenv("GITHUB_ACTIONS") && !testOptions.bench);
	g_writeDebugOutput = !testOptions.compare && !testOptions.bench;

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

	if (cmdLineOptions.bootVSH.has_value() && cmdLineOptions.bootVSH.value()) {
		AddToTestsByPath(&testFilenames, (coreParameter.nandRoot / "flash0/vsh/module/vshmain.prx").ToString());
	}

	if (testFilenames.empty()) {
		return printUsage(cmdLineOptions, argv[0], argc <= 1 ? NULL : "No executables specified");
	}

	if (cmdLineOptions.debuggerPort.has_value()) {
		coreParameter.startBreak = true;
		StartWebServer(WebServerFlags::DEBUGGER);
		// We break at start and wait for a debugger to drive us, so coming up without one just
		// hangs until the timeout. Better to say why and bail - see WebServerSetRequireExactPort().
		if (!WebServerWaitForStartup()) {
			fprintf(stderr, "Failed to start the debugger web server on port %d\n", cmdLineOptions.debuggerPort.value());
			// The server thread has exited but is still joinable - without this, its std::thread
			// destructor would call std::terminate() on the way out and we'd abort instead of
			// returning a useful exit code.
			ShutdownWebServer();
			return 1;
		}
	}

	if (stateToLoad) {
		// Queued now, actually applied by SaveState::Process() once the game is up and running.
		SaveState::Load(Path(stateToLoad), -1, [](SaveState::Status status, std::string_view message, std::string_view) {
			// The message already reads as a full sentence, e.g. "Failed to load state: <reason>".
			fprintf(stderr, "%.*s\n", (int)message.size(), message.data());
			if (status == SaveState::Status::FAILURE) {
				g_stateLoadFailed = true;
			}
		});
	}

	std::string errorMessage;
	if (!graphicsContext->InitAPI(windowDesc.data2, deviceSetting, &errorMessage)) {
		// No fallbacks in headless - if we can't run it, we can't. Let's not get confusing.
		fprintf(stderr, "Failed to initialize graphics API: %s\n", errorMessage.c_str());
		return 1;
	}

	int retval = 0;
	if (!MainThreadFunc(graphicsContext, new HeadlessApplication(), windowDesc, [&retval, &coreParameter, &testOptions, &testFilenames](GraphicsContext *graphicsContext) {
		retval = RunTests(graphicsContext, coreParameter, testOptions, testFilenames);
		return false;
	}, &errorMessage)) {
		// No fallbacks in headless - if we can't run it, we can't. Let's not get confusing.
		fprintf(stderr, "Failed to initialize graphics surface: %s\n", errorMessage.c_str());
		retval = 1;
	}

	if (g_stateLoadFailed && retval == 0) {
		// Whatever the run itself reported, the state we were told to load never got applied.
		retval = 1;
	}

	graphicsContext->ShutdownAPI();

	delete graphicsContext;

	if (cmdLineOptions.debuggerPort.has_value()) {
		ShutdownWebServer();
	}

#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_ARCH(LOONGARCH64)
	// ... see above
#else
	if (window) {
		DestroyHiddenWindow(window,	windowDesc);
	}
#endif

	g_VFS.Clear();
	g_logManager.Shutdown();
	if (cmdLineOptions.debuggerPort.has_value()) {
		net::Shutdown();
	}
	TimeShutdown();

	g_threadManager.Teardown();

	return retval;
}
