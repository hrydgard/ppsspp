#include <algorithm>

#include "ppsspp_config.h"

#include "Common/StringUtils.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/System/Display.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/System/OSD.h"
#include "Common/CPUDetect.h"
#include "Common/MemoryUtil.h"
#include "Common/File/AndroidStorage.h"
#include "Common/Audio/AudioBackend.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Render/Text/draw_text.h"
#include "Common/System/Request.h"
#include "Common/UI/Context.h"
#include "Common/UI/Notice.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "GPU/GPUState.h"  // ugh
#include "UI/SystemInfoScreen.h"
#include "UI/IconCache.h"
#include "UI/BaseScreens.h"
#include "UI/MiscViews.h"
#include "UI/OnScreenDisplay.h"
#include "android/jni/app-android.h"

void SystemInfoScreen::update() {
	UITabbedBaseDialogScreen::update();
	g_OSD.NudgeIngameNotifications();
}

// TODO: How can we de-duplicate this and SystemInfoScreen::CreateTabs?
static void CopySummaryToClipboard(Draw::DrawContext *draw) {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto si = GetI18NCategory(I18NCat::DIALOG);

	char *summary = new char[100000];
	StringWriter w(summary, 100000);

	std::string_view build = "Release";
#ifdef _DEBUG
	build = "Debug";
#endif
	w.W(PPSSPP_GIT_VERSION).C(" ").W(build).endl();
	w.C("CPU: ").W(cpu_info.cpu_string).endl();
	w.C("ABI: ").W(GetCompilerABI()).endl();
	w.C("OS: ").W(System_GetProperty(SYSPROP_NAME)).C(" ").W(System_GetProperty(SYSPROP_SYSTEMBUILD)).endl();
	w.C("Page Size: ").W(StringFromFormat(si->T_cstr("%d bytes"), GetMemoryProtectPageSize())).endl();
	w.C("RW/RX exclusive: ").W(PlatformIsWXExclusive() ? "Yes" : "No").endl();

	std::string board = System_GetProperty(SYSPROP_BOARDNAME);
	if (!board.empty())
		w.C("Board: ").W(board).endl();
	w.C("3D API: ").W(draw->GetInfoString(Draw::InfoField::APINAME)).endl();
	w.C("API version: ").W(draw->GetInfoString(Draw::InfoField::APIVERSION)).endl();
	w.C("Device API version: ").W(draw->GetInfoString(Draw::InfoField::DEVICE_API_VERSION)).endl();
	w.C("Vendor: ").W(draw->GetInfoString(Draw::InfoField::VENDOR)).endl();
	w.C("VendorString: ").W(draw->GetInfoString(Draw::InfoField::VENDORSTRING)).endl();
	w.C("Driver: ").W(draw->GetInfoString(Draw::InfoField::DRIVER)).endl();
	w.C("Depth buffer format: ").W(DataFormatToString(draw->GetDeviceCaps().preferredDepthBufferFormat)).endl();
	w.C("Refresh rate: ").W(StringFromFormat(si->T_cstr("%0.2f Hz"), (float)System_GetPropertyFloat(SYSPROP_DISPLAY_REFRESH_RATE))).endl();

	System_CopyStringToClipboard(summary);
	delete[] summary;

	g_OSD.Show(OSDType::MESSAGE_INFO, ApplySafeSubstitutions(di->T("Copied to clipboard: %1"), si->T("System Information")));
}

void SystemInfoScreen::CreateTabs() {
	using namespace Draw;
	using namespace UI;

	auto si = GetI18NCategory(I18NCat::SYSINFO);

	AddTab("Device Info", si->T("Device Info"), [this](UI::LinearLayout *parent) {
		CreateDeviceInfoTab(parent);
	});
	AddTab("Storage", si->T("Storage"), [this](UI::LinearLayout *parent) {
		CreateStorageTab(parent);
	});
	AddTab("DevSystemInfoBuildConfig", si->T("Build Config"), [this](UI::LinearLayout *parent) {
		CreateBuildConfigTab(parent);
	});
	AddTab("DevSystemInfoCPUExt", si->T("CPU Extensions"), [this](UI::LinearLayout *parent) {
		CreateCPUExtensionsTab(parent);
	});
	if (GetGPUBackend() == GPUBackend::OPENGL) {
		AddTab("DevSystemInfoOGLExt", si->T("OGL Extensions"), [this](UI::LinearLayout *parent) {
			CreateOpenGLExtsTab(parent);
		});
	} else if (GetGPUBackend() == GPUBackend::VULKAN) {
		AddTab("DevSystemInfoVulkanExt", si->T("Vulkan Extensions"), [this](UI::LinearLayout *parent) {
			CreateVulkanExtsTab(parent);
		});
	}
	AddTab("DevSystemInfoDriverBugs", si->T("Driver bugs"), [this](UI::LinearLayout *parent) {
		CreateDriverBugsTab(parent);
	});
}

void SystemInfoScreen::CreateDeviceInfoTab(UI::LinearLayout *deviceSpecs) {
	using namespace Draw;
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto si = GetI18NCategory(I18NCat::SYSINFO);
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);

	// bool portrait = GetDeviceOrientation() == DeviceOrientation::Portrait;
	// deviceSpecs->Add(new TopBar(*screenManager()->getUIContext(), portrait ? TopBarFlags::Portrait : TopBarFlags::Default, si->T("Device Info")));

	UI::CollapsibleSection *systemInfo = deviceSpecs->Add(new UI::CollapsibleSection(si->T("System Information")));

	Draw::DrawContext *draw = screenManager()->getDrawContext();

	systemInfo->Add(new Choice(si->T("Copy summary to clipboard"), ImageID("I_FILE_COPY")))->OnClick.Add([draw](UI::EventParams &e) { CopySummaryToClipboard(draw); });
	systemInfo->Add(new InfoItem(si->T("System Name"), System_GetProperty(SYSPROP_NAME)));
#if PPSSPP_PLATFORM(ANDROID)
	systemInfo->Add(new InfoItem(si->T("System Version"), StringFromInt(System_GetPropertyInt(SYSPROP_SYSTEMVERSION))));
#elif PPSSPP_PLATFORM(WINDOWS)
	std::string sysVersion = System_GetProperty(SYSPROP_SYSTEMBUILD);
	if (!sysVersion.empty()) {
		systemInfo->Add(new InfoItem(si->T("OS Build"), sysVersion));
	}
#endif
	systemInfo->Add(new InfoItem(si->T("Lang/Region"), System_GetProperty(SYSPROP_LANGREGION)));
	std::string board = System_GetProperty(SYSPROP_BOARDNAME);
	if (!board.empty())
		systemInfo->Add(new InfoItem(si->T("Board"), board));
	systemInfo->Add(new InfoItem(si->T("ABI"), GetCompilerABI()));
	if (System_GetPropertyBool(SYSPROP_DEBUGGER_PRESENT)) {
		systemInfo->Add(new InfoItem(si->T("Debugger Present"), di->T("Yes")));
	}

	UI::CollapsibleSection *cpuInfo = deviceSpecs->Add(new UI::CollapsibleSection(si->T("CPU Information")));

	// Don't bother showing the CPU name if we don't have one.
	if (equals(cpu_info.brand_string, "Unknown")) {
		cpuInfo->Add(new InfoItem(si->T("CPU Name"), cpu_info.brand_string));
	}

	int totalThreads = cpu_info.num_cores * cpu_info.logical_cpu_count;
	std::string cores = StringFromFormat(si->T_cstr("%d (%d per core, %d cores)"), totalThreads, cpu_info.logical_cpu_count, cpu_info.num_cores);
	cpuInfo->Add(new InfoItem(si->T("Threads"), cores));
#if PPSSPP_PLATFORM(IOS)
	cpuInfo->Add(new InfoItem(si->T("JIT available"), System_GetPropertyBool(SYSPROP_CAN_JIT) ? di->T("Yes") : di->T("No")));
#endif

	CollapsibleSection *gpuInfo = deviceSpecs->Add(new CollapsibleSection(si->T("GPU Information")));

	const std::string apiNameKey = draw->GetInfoString(InfoField::APINAME);
	std::string_view apiName = gr->T(apiNameKey);
	gpuInfo->Add(new InfoItem(si->T("3D API"), apiName));

	// TODO: Not really vendor, on most APIs it's a device name (GL calls it vendor though).
	std::string vendorString;
	if (draw->GetDeviceCaps().deviceID != 0) {
		vendorString = StringFromFormat("%s (%08x)", draw->GetInfoString(InfoField::VENDORSTRING).c_str(), draw->GetDeviceCaps().deviceID);
	} else {
		vendorString = draw->GetInfoString(InfoField::VENDORSTRING);
	}
	gpuInfo->Add(new InfoItem(si->T("Vendor"), vendorString));
	std::string vendor = draw->GetInfoString(InfoField::VENDOR);
	if (vendor.size())
		gpuInfo->Add(new InfoItem(si->T("Vendor (detected)"), vendor));
	gpuInfo->Add(new InfoItem(si->T("Driver Version"), draw->GetInfoString(InfoField::DRIVER)));
#ifdef _WIN32
	if (GetGPUBackend() != GPUBackend::VULKAN) {
		gpuInfo->Add(new InfoItem(si->T("Driver Version"), System_GetProperty(SYSPROP_GPUDRIVER_VERSION)));
	}
#endif
	if (GetGPUBackend() == GPUBackend::OPENGL) {
		gpuInfo->Add(new InfoItem(si->T("Core Context"), gl_extensions.IsCoreContext ? di->T("Active") : di->T("Inactive")));
		const int highp_int_min = gl_extensions.range[1][5][0];
		const int highp_int_max = gl_extensions.range[1][5][1];
		const int highp_float_min = gl_extensions.range[1][2][0];
		const int highp_float_max = gl_extensions.range[1][2][1];
		if (highp_int_max != 0) {
			char temp[128];
			snprintf(temp, sizeof(temp), "%d-%d", highp_int_min, highp_int_max);
			gpuInfo->Add(new InfoItem(si->T("High precision int range"), temp));
		}
		if (highp_float_max != 0) {
			char temp[128];
			snprintf(temp, sizeof(temp), "%d-%d", highp_float_min, highp_float_max);
			gpuInfo->Add(new InfoItem(si->T("High precision float range"), temp));
		}
	}
	gpuInfo->Add(new InfoItem(si->T("Depth buffer format"), DataFormatToString(draw->GetDeviceCaps().preferredDepthBufferFormat)));

	std::string texCompressionFormats;
	// Simple non-detailed summary of supported tex compression formats.
	if (draw->GetDataFormatSupport(Draw::DataFormat::ETC2_R8G8B8_UNORM_BLOCK)) texCompressionFormats += "ETC2 ";
	if (draw->GetDataFormatSupport(Draw::DataFormat::ASTC_4x4_UNORM_BLOCK)) texCompressionFormats += "ASTC ";
	if (draw->GetDataFormatSupport(Draw::DataFormat::BC1_RGBA_UNORM_BLOCK)) texCompressionFormats += "BC1-3 ";
	if (draw->GetDataFormatSupport(Draw::DataFormat::BC4_UNORM_BLOCK)) texCompressionFormats += "BC4-5 ";
	if (draw->GetDataFormatSupport(Draw::DataFormat::BC7_UNORM_BLOCK)) texCompressionFormats += "BC7 ";
	gpuInfo->Add(new InfoItem(si->T("Compressed texture formats"), texCompressionFormats));

	CollapsibleSection *osInformation = deviceSpecs->Add(new CollapsibleSection(si->T("OS Information")));
	osInformation->Add(new InfoItem(si->T("Memory Page Size"), StringFromFormat(si->T_cstr("%d bytes"), GetMemoryProtectPageSize())));
	osInformation->Add(new InfoItem(si->T("RW/RX exclusive"), PlatformIsWXExclusive() ? di->T("Active") : di->T("Inactive")));
#if PPSSPP_PLATFORM(ANDROID)
	osInformation->Add(new InfoItem(si->T("Sustained perf mode"), System_GetPropertyBool(SYSPROP_SUPPORTS_SUSTAINED_PERF_MODE) ? di->T("Supported") : di->T("Unsupported")));
#endif

	std::string_view build = si->T("Release");
#ifdef _DEBUG
	build = si->T("Debug");
#endif
	osInformation->Add(new InfoItem(si->T("PPSSPP build"), build));

	CollapsibleSection *audioInformation = deviceSpecs->Add(new CollapsibleSection(si->T("Audio Information")));
	extern AudioBackend *g_audioBackend;
	if (g_audioBackend) {
		char fmtStr[256];
		g_audioBackend->DescribeOutputFormat(fmtStr, sizeof(fmtStr));
		audioInformation->Add(new InfoItem(si->T("Stream format"), fmtStr));
	} else {
		audioInformation->Add(new InfoItem(si->T("Sample rate"), StringFromFormat(si->T_cstr("%d Hz"), System_GetPropertyInt(SYSPROP_AUDIO_SAMPLE_RATE))));
	}
	int framesPerBuffer = System_GetPropertyInt(SYSPROP_AUDIO_FRAMES_PER_BUFFER);
	if (framesPerBuffer > 0) {
		audioInformation->Add(new InfoItem(si->T("Frames per buffer"), StringFromFormat("%d", framesPerBuffer)));
	}
#if PPSSPP_PLATFORM(ANDROID)
	audioInformation->Add(new InfoItem(si->T("Optimal sample rate"), StringFromFormat(si->T_cstr("%d Hz"), System_GetPropertyInt(SYSPROP_AUDIO_OPTIMAL_SAMPLE_RATE))));
	audioInformation->Add(new InfoItem(si->T("Optimal frames per buffer"), StringFromFormat("%d", System_GetPropertyInt(SYSPROP_AUDIO_OPTIMAL_FRAMES_PER_BUFFER))));
#endif

	CollapsibleSection *displayInfo = deviceSpecs->Add(new CollapsibleSection(si->T("Display Information")));
#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(UWP)
	displayInfo->Add(new InfoItem(si->T("Native resolution"), StringFromFormat("%dx%d",
		System_GetPropertyInt(SYSPROP_DISPLAY_XRES),
		System_GetPropertyInt(SYSPROP_DISPLAY_YRES))));
#endif

	char uiResStr[64];
	const int sysDPI = System_GetPropertyInt(SYSPROP_DISPLAY_DPI);
	if (sysDPI > 0) {
		snprintf(uiResStr, sizeof(uiResStr), "%dx%d (%s: %d)",
			g_display.dp_xres,
			g_display.dp_yres,
			si->T_cstr("DPI"),
			sysDPI);
	} else {
		snprintf(uiResStr, sizeof(uiResStr), "%dx%d",
			g_display.dp_xres,
			g_display.dp_yres);
	}
	displayInfo->Add(new InfoItem(si->T("UI resolution"), uiResStr));
	displayInfo->Add(new InfoItem(si->T("Pixel resolution"), StringFromFormat("%dx%d",
		g_display.pixel_xres,
		g_display.pixel_yres)));

	const float insets[4] = {
		System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_LEFT),
		System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_TOP),
		System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_RIGHT),
		System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_BOTTOM),
	};
	if (insets[0] != 0.0f || insets[1] != 0.0f || insets[2] != 0.0f || insets[3] != 0.0f) {
		displayInfo->Add(new InfoItem(si->T("Screen notch insets"), StringFromFormat("%0.1f %0.1f %0.1f %0.1f", insets[0], insets[1], insets[2], insets[3])));
	}

	// Don't show on Windows, since it's always treated as 60 there.

	const double displayHz = System_GetPropertyFloat(SYSPROP_DISPLAY_REFRESH_RATE);
	displayInfo->Add(new InfoItem(si->T("Refresh rate"), StringFromFormat(si->T_cstr("%0.2f Hz"), displayHz)));

	if (displayHz < 55.0f) {
		displayInfo->Add(new NoticeView(NoticeLevel::WARN, ApplySafeSubstitutions(gr->T("Your display is set to a low refresh rate: %1 Hz. 60 Hz or higher is recommended."), (int)displayHz), ""));
	}

	std::string presentModes;
	if (draw->GetDeviceCaps().presentModesSupported & Draw::PresentMode::FIFO) presentModes += "FIFO, ";
	if (draw->GetDeviceCaps().presentModesSupported & Draw::PresentMode::IMMEDIATE) presentModes += "IMMEDIATE, ";
	if (draw->GetDeviceCaps().presentModesSupported & Draw::PresentMode::MAILBOX) presentModes += "MAILBOX, ";
	if (presentModes.size() > 2) {
		presentModes.pop_back();
		presentModes.pop_back();
	}
	displayInfo->Add(new InfoItem(si->T("Present modes"), presentModes));

	CollapsibleSection *versionInfo = deviceSpecs->Add(new CollapsibleSection(si->T("Version Information")));
	std::string apiVersion;
	if (GetGPUBackend() == GPUBackend::OPENGL) {
		if (gl_extensions.IsGLES) {
			apiVersion = StringFromFormat("v%d.%d.%d ES", gl_extensions.ver[0], gl_extensions.ver[1], gl_extensions.ver[2]);
		} else {
			apiVersion = StringFromFormat("v%d.%d.%d", gl_extensions.ver[0], gl_extensions.ver[1], gl_extensions.ver[2]);
		}
		versionInfo->Add(new InfoItem(si->T("API Version"), apiVersion));
	} else {
		apiVersion = draw->GetInfoString(InfoField::APIVERSION);
		if (apiVersion.size() > 30)
			apiVersion.resize(30);
		versionInfo->Add(new InfoItem(si->T("API Version"), apiVersion));

		if (GetGPUBackend() == GPUBackend::VULKAN) {
			std::string deviceApiVersion = draw->GetInfoString(InfoField::DEVICE_API_VERSION);
			versionInfo->Add(new InfoItem(si->T("Device API version"), deviceApiVersion));
		}
	}
	versionInfo->Add(new InfoItem(si->T("Shading Language"), draw->GetInfoString(InfoField::SHADELANGVERSION)));

#if PPSSPP_PLATFORM(ANDROID)
	std::string moga = System_GetProperty(SYSPROP_MOGA_VERSION);
	if (moga.empty()) {
		moga = si->T("(none detected)");
	}
	versionInfo->Add(new InfoItem("Moga", moga));
#endif

	if (gstate_c.GetUseFlags()) {
		// We're in-game, and can determine these.
		// TODO: Call a static version of GPUCommon::CheckGPUFeatures() and derive them here directly.

		CollapsibleSection *gpuFlags = deviceSpecs->Add(new CollapsibleSection(si->T("GPU Flags")));

		for (int i = 0; i < 32; i++) {
			if (gstate_c.Use((1 << i))) {
				gpuFlags->Add(new TextView(GpuUseFlagToString(i), new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
			}
		}
	}
}

void SystemInfoScreen::CreateStorageTab(UI::LinearLayout *storage) {
	using namespace UI;

	auto si = GetI18NCategory(I18NCat::SYSINFO);

	storage->Add(new ItemHeader(si->T("Directories")));
	// Intentionally non-translated
	storage->Add(new InfoItem("MemStickDirectory", g_Config.memStickDirectory.ToVisualString()));
	storage->Add(new InfoItem("InternalDataDirectory", g_Config.internalDataDirectory.ToVisualString()));
	storage->Add(new InfoItem("AppCacheDir", g_Config.appCacheDirectory.ToVisualString()));
	storage->Add(new InfoItem("DefaultCurrentDir", g_Config.defaultCurrentDirectory.ToVisualString()));

#if PPSSPP_PLATFORM(ANDROID)
	auto di = GetI18NCategory(I18NCat::DIALOG);
	storage->Add(new InfoItem("ExtFilesDir", g_extFilesDir));
	bool scoped = System_GetPropertyBool(SYSPROP_ANDROID_SCOPED_STORAGE);
	storage->Add(new InfoItem("Scoped Storage", scoped ? di->T("Yes") : di->T("No")));
	if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 30) {
		// This flag is only relevant on Android API 30+.
		storage->Add(new InfoItem("IsStoragePreservedLegacy", Android_IsExternalStoragePreservedLegacy() ? di->T("Yes") : di->T("No")));
	}
#endif
}

void SystemInfoScreen::CreateBuildConfigTab(UI::LinearLayout *buildConfig) {
	using namespace UI;

	auto si = GetI18NCategory(I18NCat::SYSINFO);

	buildConfig->Add(new ItemHeader(si->T("Build Configuration")));
#ifdef ANDROID_LEGACY
	buildConfig->Add(new InfoItem("ANDROID_LEGACY", ""));
#endif
#ifdef _DEBUG
	buildConfig->Add(new InfoItem("_DEBUG", ""));
#else
	buildConfig->Add(new InfoItem("NDEBUG", ""));
#endif
#ifdef USE_ASAN
	buildConfig->Add(new InfoItem("USE_ASAN", ""));
#endif
#ifdef USING_GLES2
	buildConfig->Add(new InfoItem("USING_GLES2", ""));
#endif
#ifdef MOBILE_DEVICE
	buildConfig->Add(new InfoItem("MOBILE_DEVICE", ""));
#endif
#if PPSSPP_ARCH(ARMV7S)
	buildConfig->Add(new InfoItem("ARMV7S", ""));
#endif
#if PPSSPP_ARCH(ARM_NEON)
	buildConfig->Add(new InfoItem("ARM_NEON", ""));
#endif
#ifdef _M_SSE
	buildConfig->Add(new InfoItem("_M_SSE", StringFromFormat("0x%x", _M_SSE)));
#endif
	if (System_GetPropertyBool(SYSPROP_APP_GOLD)) {
		buildConfig->Add(new InfoItem("GOLD", ""));
	}
}

void SystemInfoScreen::CreateCPUExtensionsTab(UI::LinearLayout *cpuExtensions) {
	using namespace UI;

	auto si = GetI18NCategory(I18NCat::SYSINFO);

	cpuExtensions->Add(new ItemHeader(si->T("CPU Extensions")));
	std::vector<std::string> exts = cpu_info.Features();
	for (std::string &ext : exts) {
		cpuExtensions->Add(new TextView(ext, new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
	}
}

void SystemInfoScreen::CreateDriverBugsTab(UI::LinearLayout *driverBugs) {
	using namespace UI;
	using namespace Draw;

	auto si = GetI18NCategory(I18NCat::SYSINFO);

	driverBugs->Add(new ItemHeader(si->T("Driver bugs")));

	bool anyDriverBugs = false;
	Draw::DrawContext *draw = screenManager()->getDrawContext();
	for (int i = 0; i < (int)draw->GetBugs().MaxBugIndex(); i++) {
		if (draw->GetBugs().Has(i)) {
			anyDriverBugs = true;
			driverBugs->Add(new InfoItem(draw->GetBugs().GetBugName(i), ""));
		}
	}

	if (!anyDriverBugs) {
		driverBugs->Add(new InfoItem(si->T("No GPU driver bugs detected"), ""));
	}
}

void SystemInfoScreen::CreateOpenGLExtsTab(UI::LinearLayout *gpuExtensions) {
	using namespace UI;

	auto si = GetI18NCategory(I18NCat::SYSINFO);
	Draw::DrawContext *draw = screenManager()->getDrawContext();

	if (!gl_extensions.IsGLES) {
		gpuExtensions->Add(new ItemHeader(si->T("OpenGL Extensions")));
	} else if (gl_extensions.GLES3) {
		gpuExtensions->Add(new ItemHeader(si->T("OpenGL ES 3.0 Extensions")));
	} else {
		gpuExtensions->Add(new ItemHeader(si->T("OpenGL ES 2.0 Extensions")));
	}

	std::vector<std::string> exts;
	SplitString(g_all_gl_extensions, ' ', exts);
	std::sort(exts.begin(), exts.end());
	for (auto &extension : exts) {
		gpuExtensions->Add(new TextView(extension, new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
	}

	exts.clear();
	SplitString(g_all_egl_extensions, ' ', exts);
	std::sort(exts.begin(), exts.end());

	// If there aren't any EGL extensions, no need to show the tab.
	gpuExtensions->Add(new ItemHeader(si->T("EGL Extensions")));
	for (auto &extension : exts) {
		gpuExtensions->Add(new TextView(extension, new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
	}
}

void SystemInfoScreen::CreateVulkanExtsTab(UI::LinearLayout *gpuExtensions) {
	using namespace UI;

	auto si = GetI18NCategory(I18NCat::SYSINFO);
	auto di = GetI18NCategory(I18NCat::DIALOG);

	Draw::DrawContext *draw = screenManager()->getDrawContext();

	CollapsibleSection *vulkanFeatures = gpuExtensions->Add(new CollapsibleSection(si->T("Vulkan Features")));
	std::vector<std::string> features = draw->GetFeatureList();
	for (const auto &feature : features) {
		vulkanFeatures->Add(new TextView(feature, FLAG_DYNAMIC_ASCII, true, new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
	}

	CollapsibleSection *presentModes = gpuExtensions->Add(new CollapsibleSection(si->T("Present modes")));
	for (const auto &mode : draw->GetPresentModeList(di->T("Current"))) {
		presentModes->Add(new TextView(mode, FLAG_DYNAMIC_ASCII, true, new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
	}

	CollapsibleSection *colorFormats = gpuExtensions->Add(new CollapsibleSection(si->T("Display Color Formats")));
	for (const auto &format : draw->GetSurfaceFormatList()) {
		colorFormats->Add(new TextView(format, FLAG_DYNAMIC_ASCII, true, new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
	}

	CollapsibleSection *enabledExtensions = gpuExtensions->Add(new CollapsibleSection(std::string(si->T("Vulkan Extensions")) + " (" + std::string(di->T("Enabled")) + ")"));
	std::vector<std::string> extensions = draw->GetExtensionList(true, true);
	std::sort(extensions.begin(), extensions.end());
	for (auto &extension : extensions) {
		enabledExtensions->Add(new TextView(extension, FLAG_DYNAMIC_ASCII, true, new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
	}
	// Also get instance extensions
	enabledExtensions->Add(new ItemHeader(si->T("Instance")));
	extensions = draw->GetExtensionList(false, true);
	std::sort(extensions.begin(), extensions.end());
	for (auto &extension : extensions) {
		enabledExtensions->Add(new TextView(extension, FLAG_DYNAMIC_ASCII, true, new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
	}

	CollapsibleSection *vulkanExtensions = gpuExtensions->Add(new CollapsibleSection(si->T("Vulkan Extensions")));
	extensions = draw->GetExtensionList(true, false);
	std::sort(extensions.begin(), extensions.end());
	for (auto &extension : extensions) {
		vulkanExtensions->Add(new TextView(extension, FLAG_DYNAMIC_ASCII, true, new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
	}

	vulkanExtensions->Add(new ItemHeader(si->T("Instance")));
	// Also get instance extensions
	extensions = draw->GetExtensionList(false, false);
	std::sort(extensions.begin(), extensions.end());
	for (auto &extension : extensions) {
		vulkanExtensions->Add(new TextView(extension, FLAG_DYNAMIC_ASCII, true, new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
	}
}
