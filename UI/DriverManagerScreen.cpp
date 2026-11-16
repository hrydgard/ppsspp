#include "Common/File/VFS/ZipFileReader.h"
#include "Common/Data/Format/JSONReader.h"
#include "Common/Data/Text/I18n.h"
#include "Common/System/Request.h"
#include "Common/System/OSD.h"
#include "Common/GPU/Vulkan/VulkanDriverVars.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/UI/PopupScreens.h"
#include "Common/UI/Notice.h"
#include "Common/UI/ScreenManager.h"

#include "Core/Config.h"
#include "Core/System.h"
#include "Core/Util/PathUtil.h"

#include "Common/UI/View.h"
#include "UI/DriverManagerScreen.h"
#include "UI/GameSettingsScreen.h"  // for triggerrestart
#include "UI/OnScreenDisplay.h"
#include "UI/MiscScreens.h"

#if PPSSPP_PLATFORM(ANDROID)
#include <unistd.h>
#include <android/api-level.h>

// Adreno
static bool CheckKgslPresent() {
	return access("/dev/kgsl-3d0", F_OK) == 0;
}

// Mali
static bool CheckKbasePresent() {
	return access("/dev/mali0", F_OK) == 0;
}
#endif

bool SupportsCustomDriver() {
#if PPSSPP_PLATFORM(ANDROID)
	return android_get_device_api_level() >= 28 && (CheckKgslPresent() || CheckKbasePresent());
#elif defined(_DEBUG)
	return false;  // change to true to debug driver installation on other platforms. DO NOT commit as true.
#else
	return false;
#endif
}

static Path GetDriverPath() {
	if (g_Config.internalDataDirectory.empty()) {
		Path curDir = File::GetCurDirectory();
		// This is the case when testing on PC
		return GetSysDirectory(DIRECTORY_PSP) / "drivers";
	} else {
		// On Android, this is set to something usable.
		return g_Config.internalDataDirectory / "drivers";
	}
}

// Example meta.json:
// {
//   "schemaVersion": 1,
//   "name" : "Turnip driver revision 14",
//   "description" : "Compiled from Mesa source.",
//   "author" : "KIMCHI",
//   "packageVersion" : "1",
//   "vendor" : "Mesa",
//   "driverVersion" : "Vulkan 1.3.274",
//   "minApi" : 27,
//   "libraryName" : "vulkan.ad07XX.so"
// }

struct DriverMeta {
	int minApi;
	std::string name;
	std::string description;
	std::string vendor;
	std::string driverVersion;

	bool Read(std::string_view str, std::string *errorStr) {
		// Validate the json file. TODO: Be a bit more detailed.
		json::JsonReader meta = json::JsonReader((const char *)str.data(), str.size());
		if (!meta.ok()) {
			*errorStr = "meta.json not valid json";
			return false;
		}

		int schemaVersion = meta.root().getInt("schemaVersion");
		if (schemaVersion > 1) {
			*errorStr = "unknown schemaVersion in meta.json";
			return false;
		}

		if (!meta.root().getString("name", &name) || name.empty()) {
			*errorStr = "missing driver name in json";
			return false;
		}
		meta.root().getString("description", &description);
		meta.root().getString("vendor", &vendor);
		meta.root().getString("driverVersion", &driverVersion);
		minApi = meta.root().getInt("minApi");
		return true;
	}
};

// Compound view, creating a FileChooserChoice inside.
class DriverChoice : public UI::LinearLayout {
public:
	DriverChoice(const std::string &driverName, bool current, UI::LayoutParams *layoutParams = nullptr);

	UI::Event OnUse;
	UI::Event OnDelete;
	std::string name_;
};

DriverChoice::DriverChoice(const std::string &driverName, bool current, UI::LayoutParams *layoutParams) : UI::LinearLayout(ORIENT_VERTICAL, layoutParams), name_(driverName) {
	using namespace UI;
	SetSpacing(0.0f);
	if (!layoutParams) {
		layoutParams_->width = FILL_PARENT;
		layoutParams_->height = WRAP_CONTENT;
	}
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);
	auto di = GetI18NCategory(I18NCat::DIALOG);

	// Read the meta data
	DriverMeta meta{};
	bool isDefault = driverName.empty();
	if (isDefault) {
		meta.name = gr->T("Default GPU driver");
	}

	Path metaPath = GetDriverPath() / driverName / "meta.json";
	std::string metaJson;
	if (File::ReadTextFileToString(metaPath, &metaJson)) {
		std::string errorStr;
		meta.Read(metaJson, &errorStr);
	}

#if PPSSPP_PLATFORM(ANDROID)
	bool usable = isDefault || meta.minApi <= System_GetPropertyInt(SYSPROP_SYSTEMVERSION);
#else
	// For testing only
	bool usable = isDefault || true;
#endif

	Choice *radio = Add(new Choice(meta.name, ImageID(current ? "I_RADIO_SELECTED" : "I_RADIO_EMPTY"), new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	if (usable) {
		if (!current) {
			radio->OnClick.Add([this](UI::EventParams &) {
				UI::EventParams e{};
				e.s = name_;
				OnUse.Trigger(e);
			});
		}
	}
	if (!isDefault) {
		std::string desc = meta.description;
		Add(new InfoItem(desc, "", new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		std::string vendorInfo = meta.vendor + ": " + meta.driverVersion;
		Add(new InfoItem(vendorInfo, "", new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	}
	if (!current && !isDefault) {
		Add(new Choice(di->T("Delete"), ImageID("I_TRASHCAN"), new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)))->OnClick.Add([this](UI::EventParams &) {
			UI::EventParams e{};
			e.s = name_;
			OnDelete.Trigger(e);
		});
	}
	if (!usable) {
		Add(new NoticeView(NoticeLevel::WARN, ApplySafeSubstitutions(gr->T("Driver requires Android API version %1, current is %2"), meta.minApi, System_GetPropertyInt(SYSPROP_SYSTEMVERSION)),""));
	}

	SetRoundCorners();
}

DriverManagerScreen::DriverManagerScreen(const Path & gamePath) : UITabbedBaseDialogScreen(gamePath) {}

void DriverManagerScreen::CreateTabs() {
	using namespace UI;
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);

	AddTab("DriverManagerDrivers", gr->T("Drivers"), [this](UI::LinearLayout *parent) {
		CreateDriverTab(parent);
	});

#if PPSSPP_PLATFORM(ANDROID)
	const bool showTurnipFlags = CheckKgslPresent();
#else
	const bool showTurnipFlags = true;
#endif
	if (showTurnipFlags) {
		AddTab("DriverManagerTurnipFlags", gr->T("Turnip driver flags"), [this](UI::LinearLayout *parent) {
			CreateTurnipFlagsTab(parent);
		});
	}
}

void DriverManagerScreen::CreateTurnipFlagsTab(UI::ViewGroup *parent) {
	using namespace UI;
	ChoiceGroupList *groups = parent->Add(new ChoiceGroupList());

	// Turnip is Adreno-only. Keep the flags visible off Android, for debugging the screen.
	ChoiceGroup *adrenoEnvVarGroup = groups->Add(new ChoiceGroup());
	int count = 0;
	const AdrenoEnvVarDef *adrenoEnvVarDefs = GetAdrenoEnvVarDefs(&count);
	for (int i = 0; i < count; i++) {
		adrenoEnvVarGroup->Add(new BitCheckBox(&g_Config.iAdrenoEnvVars, (int)adrenoEnvVarDefs[i].var, adrenoEnvVarDefs[i].name));
	}
}

void DriverManagerScreen::CreateDriverTab(UI::ViewGroup *parent) {
	using namespace UI;
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);

	ChoiceGroupList *groups = parent->Add(new ChoiceGroupList());

	// Like SettingsPage::Description().
	TextView *description = groups->Add(new TextView(gr->T("CustomDriversDescription", "Custom drivers loaded through AdrenoTools replace the GPU driver that came with your device. They can be faster and have fewer bugs, but they can also be slower or less stable. Only install drivers made for your GPU. If rendering breaks, switch back to the default driver."), FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(0, 4))));
	description->SetTextSize(TextSize::Small);
	description->SetPadding(Margins(12));
	description->SetFrame(true);

	Choice *moreInfo = groups->Add(new ChoiceGroup())->Add(new Choice(di->T("More info")));
	moreInfo->SetIconRight(ImageID("I_LINK_OUT"));
	moreInfo->OnClick.Add([=](UI::EventParams &e) {
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org/docs/reference/custom-drivers/");
	});

	ChoiceGroup *driverManagerGroup = groups->Add(new ChoiceGroup(gr->T("Custom driver manager")));
	auto customDriverInstallChoice = driverManagerGroup->Add(new Choice(gr->T("Install custom driver...")));

	customDriverInstallChoice->OnClick.Handle(this, &DriverManagerScreen::OnCustomDriverInstall);

	groups->Add(new TextView(gr->T("Drivers")))->SetBig(true);

	const bool isDefault = g_Config.sCustomDriver.empty();
	groups->Add(new DriverChoice("", isDefault))->OnUse.Handle(this, &DriverManagerScreen::OnCustomDriverChange);

	const Path driverPath = GetDriverPath();
	std::vector<File::FileInfo> listing;
	if (File::GetFilesInDir(driverPath, &listing)) {
		for (auto driver : listing) {
			// A DriverChoice is itself a group.
			auto choice = groups->Add(new DriverChoice(driver.name, g_Config.sCustomDriver == driver.name));
			choice->OnUse.Handle(this, &DriverManagerScreen::OnCustomDriverChange);
			choice->OnDelete.Handle(this, &DriverManagerScreen::OnCustomDriverUninstall);
		}
	}
}

void DriverManagerScreen::OnCustomDriverChange(UI::EventParams &e) {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);

	screenManager()->push(new UI::MessagePopupScreen(gr->T("Drivers"), di->T("Changing this setting requires PPSSPP to restart."), di->T("Restart"), di->T("Cancel"), [=](bool yes) {
		if (yes) {
			INFO_LOG(Log::G3D, "Switching driver to '%s'", e.s.c_str());
			g_Config.sCustomDriver = e.s;
			TriggerRestart("GameSettingsScreen::CustomDriverYes", false, gamePath_);
		}
	}));
}

void DriverManagerScreen::OnCustomDriverUninstall(UI::EventParams &e) {
	if (e.s.empty()) {
		return;
	}
	INFO_LOG(Log::G3D, "Uninstalling driver: %s", e.s.c_str());

	Path folder = GetDriverPath() / e.s;
	File::DeleteDirRecursively(folder);

	RecreateViews();
}

void DriverManagerScreen::OnCustomDriverInstall(UI::EventParams &e) {
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);

	System_BrowseForFile(GetRequesterToken(), gr->T("Install custom driver..."), BrowseFileType::ZIP, [this](std::string_view value, int) {
		if (value.empty()) {
			return;
		}

		auto gr = GetI18NCategory(I18NCat::GRAPHICS);

		Path zipPath(value);

		// Don't bother checking the file extension. Can't always do that with files from Download (they have paths like content://com.android.providers.downloads.documents/document/msf%3A1000001095).
		// Though, it may be possible to get it in other ways.

		std::unique_ptr<ZipFileReader> zipFileReader = std::unique_ptr<ZipFileReader>(ZipFileReader::Create(zipPath, "", true));
		if (!zipFileReader) {
			g_OSD.Show(OSDType::MESSAGE_ERROR, gr->T("The chosen ZIP file doesn't contain a valid driver", "couldn't open zip"));
			ERROR_LOG(Log::System, "Failed to open file '%s' as zip", zipPath.c_str());
			return;
		}

		size_t metaDataSize;
		uint8_t *metaData = zipFileReader->ReadFile("meta.json", &metaDataSize);
		if (!metaData) {
			g_OSD.Show(OSDType::MESSAGE_ERROR, gr->T("The chosen ZIP file doesn't contain a valid driver"), "meta.json missing");
			return;
		}

		DriverMeta meta;
		std::string errorStr;
		if (!meta.Read(std::string_view((const char *)metaData, metaDataSize), &errorStr)) {
			delete[] metaData;
			g_OSD.Show(OSDType::MESSAGE_ERROR, gr->T("The chosen ZIP file doesn't contain a valid driver"), errorStr);
			return;
		}
		delete[] metaData;

		// meta.name and the zip entry names below both get joined onto GetDriverPath()
		// verbatim (Path::operator/ does no ".." normalization) - without checking for
		// parent-directory components, a crafted driver zip could write files outside
		// the intended drivers/<name>/ directory (zip slip, CWE-22).
		if (HasParentDirComponent(meta.name)) {
			g_OSD.Show(OSDType::MESSAGE_ERROR, gr->T("The chosen ZIP file doesn't contain a valid driver"), "invalid driver name");
			return;
		}

		const Path newCustomDriver = GetDriverPath() / meta.name;
		NOTICE_LOG(Log::G3D, "Installing driver into '%s'", newCustomDriver.c_str());
		File::CreateFullPath(newCustomDriver);

		std::vector<File::FileInfo> zipListing;
		zipFileReader->GetFileListing("", &zipListing, nullptr);

		for (auto file : zipListing) {
			if (HasParentDirComponent(file.name)) {
				WARN_LOG(Log::G3D, "Skipping zip entry with parent directory component: %s", file.name.c_str());
				continue;
			}

			File::CreateEmptyFile(newCustomDriver / file.name);

			size_t size;
			uint8_t *data = zipFileReader->ReadFile(file.name.c_str(), &size);
			if (!data) {
				g_OSD.Show(OSDType::MESSAGE_ERROR, gr->T("The chosen ZIP file doesn't contain a valid driver"), file.name.c_str());
				return;
			}
			File::WriteDataToFile(false, data, size, newCustomDriver / file.name);
			delete[] data;
		}

		auto iz = GetI18NCategory(I18NCat::INSTALLZIP);
		g_OSD.Show(OSDType::MESSAGE_SUCCESS, iz->T("Installed!"));
		RecreateViews();
	});
}
