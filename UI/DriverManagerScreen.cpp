#include "Common/File/VFS/ZipFileReader.h"
#include "Common/Data/Format/JSONReader.h"
#include "Common/Data/Text/I18n.h"
#include "Common/System/Request.h"
#include "Common/System/OSD.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/UI/PopupScreens.h"
#include "Common/UI/Notice.h"

#include "Core/Config.h"
#include "Core/System.h"

#include "Common/UI/View.h"
#include "UI/DriverManagerScreen.h"
#include "UI/GameSettingsScreen.h"  // for triggerrestart
#include "UI/OnScreenDisplay.h"
#include "UI/MiscScreens.h"

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
	SetSpacing(2.0f);
	if (!layoutParams) {
		layoutParams_->width = FILL_PARENT;
		layoutParams_->height = 220;
	}
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);
	auto di = GetI18NCategory(I18NCat::DIALOG);

	// Read the meta data
	DriverMeta meta{};
	bool isDefault = driverName.empty();
	if (isDefault) {
		meta.description = gr->T("Default GPU driver");
	}

	Path metaPath = GetDriverPath() / driverName / "meta.json";
	std::string metaJson;
	if (File::ReadTextFileToString(metaPath, &metaJson)) {
		std::string errorStr;
		meta.Read(metaJson, &errorStr);
	}
	Add(new Spacer(12.0));

#if PPSSPP_PLATFORM(ANDROID)
	bool usable = isDefault || meta.minApi <= System_GetPropertyInt(SYSPROP_SYSTEMVERSION);
#else
	// For testing only
	bool usable = isDefault || true;
#endif

	Add(new ItemHeader(driverName.empty() ? gr->T("Default GPU driver") : driverName))->SetLarge(true);
	if (current) {
		Add(new NoticeView(NoticeLevel::SUCCESS, gr->T("Current GPU driver"), ""));
	}

	auto horizBar = Add(new UI::LinearLayout(ORIENT_HORIZONTAL));
	std::string desc = meta.description;
	if (!desc.empty()) desc += "\n";
	if (!isDefault)
		desc += meta.vendor + ": " + meta.driverVersion;
	horizBar->Add(new TextView(desc));
	if (!current && !isDefault) {
		horizBar->Add(new Choice(ImageID("I_TRASHCAN"), new LinearLayoutParams(ITEM_HEIGHT, ITEM_HEIGHT)))->OnClick.Add([=](UI::EventParams &) {
			UI::EventParams e{};
			e.s = name_;
			OnDelete.Trigger(e);
		});
	}
	if (usable) {
		if (!current) {
			Add(new Choice(di->T("Select")))->OnClick.Add([=](UI::EventParams &) {
				UI::EventParams e{};
				e.s = name_;
				OnUse.Trigger(e);
			});
		}
	} else {
		Add(new NoticeView(NoticeLevel::WARN, ApplySafeSubstitutions(gr->T("Driver requires Android API version %1, current is %2"), meta.minApi, System_GetPropertyInt(SYSPROP_SYSTEMVERSION)),""));
	}
}

DriverManagerScreen::DriverManagerScreen(const Path & gamePath) : UITabbedBaseDialogScreen(gamePath) {}

void DriverManagerScreen::CreateTabs() {
	using namespace UI;
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);

	AddTab("DriverManagerDrivers", gr->T("Drivers"), [this](UI::LinearLayout *parent) {
		CreateDriverTab(parent);
	});
}

void DriverManagerScreen::CreateDriverTab(UI::ViewGroup *drivers) {
	using namespace UI;
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);

	drivers->Add(new ItemHeader(gr->T("AdrenoTools driver manager")));
	auto customDriverInstallChoice = drivers->Add(new Choice(gr->T("Install custom driver...")));
	drivers->Add(new Choice(di->T("More info")))->OnClick.Add([=](UI::EventParams &e) {
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org/docs/reference/custom-drivers/");
	});

	customDriverInstallChoice->OnClick.Handle(this, &DriverManagerScreen::OnCustomDriverInstall);

	drivers->Add(new ItemHeader(gr->T("Drivers")));
	bool isDefault = g_Config.sCustomDriver.empty();
	drivers->Add(new DriverChoice("", isDefault))->OnUse.Handle(this, &DriverManagerScreen::OnCustomDriverChange);

	const Path driverPath = GetDriverPath();
	std::vector<File::FileInfo> listing;
	if (File::GetFilesInDir(driverPath, &listing)) {
		for (auto driver : listing) {
			auto choice = drivers->Add(new DriverChoice(driver.name, g_Config.sCustomDriver == driver.name));
			choice->OnUse.Handle(this, &DriverManagerScreen::OnCustomDriverChange);
			choice->OnDelete.Handle(this, &DriverManagerScreen::OnCustomDriverUninstall);
		}
	}
	drivers->Add(new Spacer(12.0));
}

void DriverManagerScreen::OnCustomDriverChange(UI::EventParams &e) {
	auto di = GetI18NCategory(I18NCat::DIALOG);

	screenManager()->push(new PromptScreen(gamePath_, di->T("Changing this setting requires PPSSPP to restart."), di->T("Restart"), di->T("Cancel"), [=](bool yes) {
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

	System_BrowseForFile(GetRequesterToken(), gr->T("Install custom driver..."), BrowseFileType::ZIP, [this](const std::string &value, int) {
		if (value.empty()) {
			return;
		}

		auto gr = GetI18NCategory(I18NCat::GRAPHICS);

		Path zipPath = Path(value);

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

		const Path newCustomDriver = GetDriverPath() / meta.name;
		NOTICE_LOG(Log::G3D, "Installing driver into '%s'", newCustomDriver.c_str());
		File::CreateFullPath(newCustomDriver);

		std::vector<File::FileInfo> zipListing;
		zipFileReader->GetFileListing("", &zipListing, nullptr);

		for (auto file : zipListing) {
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
