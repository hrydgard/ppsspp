#include <map>
#include <algorithm>
#include <cstring>
#include <iterator>
#include <memory>
#include <set>
#include <vector>
#include "Core/HLE/PSPRegistry.h"
#include "Core/HLE/sceReg.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HLE/sceKernelModule.h"
#include "Core/HLE/VSHModuleRoute.h"
#include "Core/Config.h"
#include "Common/File/FileUtil.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeMap.h"
#include "Common/StringUtils.h"

#define SYSTEM_REGISTRY "/system"
#define REG_KEYNAME_SIZE 27

enum RegKeyTypes {
	REG_TYPE_DIR = 1,
	REG_TYPE_INT = 2,
	REG_TYPE_STR = 3,
	REG_TYPE_BIN = 4,
};

typedef unsigned int REGHANDLE;

struct RegParam {
    unsigned int regtype;     /* 0x0, set to 1 only for system */
    char name[256];        /* 0x4-0x104 */
    unsigned int namelen;     /* 0x104 */
    unsigned int unk2;     /* 0x108 */
    unsigned int unk3;     /* 0x10C */
};

struct OpenCategory {
	std::string path;
	int openMode;
	void DoState(PointerWrap &p) {
		Do(p, path);
		Do(p, openMode);
	}
};

static int g_openRegistryMode;
static int g_handleGen;  // TODO: The real PSP seems to use memory addresses. Probably it's doing allocations, which we don't really want to do unless we can match them exactly.
static std::map<int, OpenCategory> g_openCategories;

enum class ValueType {
	FAIL = 0,
	DIR = 1,
	INT = 2,
	STR = 3,
	BIN = 4,
};

struct KeyValue {
	std::string name;
	ValueType type;
	const char *strValue;  // also can be used for bin. Note: can't be std::string/string_view, will cut off the length.
	int intValue;
	const KeyValue *dirContents;  // intValue is the count.
};

struct RegistryValue {
	ValueType type = ValueType::FAIL;
	std::vector<u8> data;

	void DoState(PointerWrap &p) {
		int typeValue = (int)type;
		Do(p, typeValue);
		if (p.mode == p.MODE_READ) {
			type = (ValueType)typeValue;
		}
		Do(p, data);
	}
};

struct RegistryCategory {
	std::vector<std::string> order;
	std::map<std::string, RegistryValue> values;
	int accessError = 0;

	void DoState(PointerWrap &p) {
		Do(p, order);
		Do(p, values);
		Do(p, accessError);
	}
};

static std::map<std::string, RegistryCategory> g_registryCategories;
static bool g_registryLoaded;
static bool g_registryPersistent;
static bool g_registryDirty;
static bool g_registryRecovered;
static u64 g_registryGeneration;
static bool g_registryUsesRealFiles;
static std::unique_ptr<PSPRegistryFile> g_realRegistry;
static std::set<std::pair<std::string, std::string>> g_registryDirtyValues;

// TODO: /DATA/FONT/PROPERTY could just be generated from our fontRegistry in sceFont.cpp.

// Partial dump of the PSP registry using tests/misc/reg.prx in pspautotests
// TODO: We will need something more dynamic if we want to support writes, too.
// NOTE: updating with a new dump can cause tests to fail. These must stay as-is:
// E    {"game_exec_count", ValueType::INT, "", (int)0x00000046},
// E    {"usb_connect_count", ValueType::INT, "", (int)0x000000ec},

// Dump of /DATA/FONT/PROPERTY/INFO0
static const KeyValue tree_DATA_FONT_PROPERTY_INFO0[] = {
	{ "h_size", ValueType::INT, "", (int)0x288 },  // decimal: 648
	{ "v_size", ValueType::INT, "", (int)0x288 },  // decimal: 648
	{ "h_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "v_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "extra_attributes", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "weight", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "family_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "style", ValueType::INT, "", (int)0x67 },  // decimal: 103
	{ "sub_style", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "language_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "region_code", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "country_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "font_name", ValueType::STR, "FTT-NewRodin Pro DB", 20 },
	{ "file_name", ValueType::STR, "jpn0.pgf", 9 },
	{ "expire_date", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "shadow_option", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /DATA/FONT/PROPERTY/INFO1
static const KeyValue tree_DATA_FONT_PROPERTY_INFO1[] = {
	{ "h_size", ValueType::INT, "", (int)0x288 },  // decimal: 648
	{ "v_size", ValueType::INT, "", (int)0x288 },  // decimal: 648
	{ "h_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "v_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "extra_attributes", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "weight", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "family_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "style", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "sub_style", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "language_code", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "region_code", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "country_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "font_name", ValueType::STR, "FTT-NewRodin Pro Latin", 23 },
	{ "file_name", ValueType::STR, "ltn0.pgf", 9 },
	{ "expire_date", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "shadow_option", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /DATA/FONT/PROPERTY/INFO2
static const KeyValue tree_DATA_FONT_PROPERTY_INFO2[] = {
	{ "h_size", ValueType::INT, "", (int)0x288 },  // decimal: 648
	{ "v_size", ValueType::INT, "", (int)0x288 },  // decimal: 648
	{ "h_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "v_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "extra_attributes", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "weight", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "family_code", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "style", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "sub_style", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "language_code", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "region_code", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "country_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "font_name", ValueType::STR, "FTT-Matisse Pro Latin", 22 },
	{ "file_name", ValueType::STR, "ltn1.pgf", 9 },
	{ "expire_date", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "shadow_option", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /DATA/FONT/PROPERTY/INFO3
static const KeyValue tree_DATA_FONT_PROPERTY_INFO3[] = {
	{ "h_size", ValueType::INT, "", (int)0x288 },  // decimal: 648
	{ "v_size", ValueType::INT, "", (int)0x288 },  // decimal: 648
	{ "h_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "v_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "extra_attributes", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "weight", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "family_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "style", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "sub_style", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "language_code", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "region_code", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "country_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "font_name", ValueType::STR, "FTT-NewRodin Pro Latin", 23 },
	{ "file_name", ValueType::STR, "ltn2.pgf", 9 },
	{ "expire_date", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "shadow_option", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /DATA/FONT/PROPERTY/INFO4
static const KeyValue tree_DATA_FONT_PROPERTY_INFO4[] = {
	{ "h_size", ValueType::INT, "", (int)0x288 },  // decimal: 648
	{ "v_size", ValueType::INT, "", (int)0x288 },  // decimal: 648
	{ "h_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "v_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "extra_attributes", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "weight", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "family_code", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "style", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "sub_style", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "language_code", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "region_code", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "country_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "font_name", ValueType::STR, "FTT-Matisse Pro Latin", 22 },
	{ "file_name", ValueType::STR, "ltn3.pgf", 9 },
	{ "expire_date", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "shadow_option", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /DATA/FONT/PROPERTY/INFO5
static const KeyValue tree_DATA_FONT_PROPERTY_INFO5[] = {
	{ "h_size", ValueType::INT, "", (int)0x288 },  // decimal: 648
	{ "v_size", ValueType::INT, "", (int)0x288 },  // decimal: 648
	{ "h_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "v_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "extra_attributes", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "weight", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "family_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "style", ValueType::INT, "", (int)0x5 },  // decimal: 5
	{ "sub_style", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "language_code", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "region_code", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "country_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "font_name", ValueType::STR, "FTT-NewRodin Pro Latin", 23 },
	{ "file_name", ValueType::STR, "ltn4.pgf", 9 },
	{ "expire_date", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "shadow_option", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /DATA/FONT/PROPERTY/INFO6
static const KeyValue tree_DATA_FONT_PROPERTY_INFO6[] = {
	{ "h_size", ValueType::INT, "", (int)0x288 },  // decimal: 648
	{ "v_size", ValueType::INT, "", (int)0x288 },  // decimal: 648
	{ "h_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "v_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "extra_attributes", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "weight", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "family_code", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "style", ValueType::INT, "", (int)0x5 },  // decimal: 5
	{ "sub_style", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "language_code", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "region_code", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "country_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "font_name", ValueType::STR, "FTT-Matisse Pro Latin", 22 },
	{ "file_name", ValueType::STR, "ltn5.pgf", 9 },
	{ "expire_date", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "shadow_option", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /DATA/FONT/PROPERTY/INFO7
static const KeyValue tree_DATA_FONT_PROPERTY_INFO7[] = {
	{ "h_size", ValueType::INT, "", (int)0x288 },  // decimal: 648
	{ "v_size", ValueType::INT, "", (int)0x288 },  // decimal: 648
	{ "h_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "v_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "extra_attributes", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "weight", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "family_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "style", ValueType::INT, "", (int)0x6 },  // decimal: 6
	{ "sub_style", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "language_code", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "region_code", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "country_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "font_name", ValueType::STR, "FTT-NewRodin Pro Latin", 23 },
	{ "file_name", ValueType::STR, "ltn6.pgf", 9 },
	{ "expire_date", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "shadow_option", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /DATA/FONT/PROPERTY/INFO8
static const KeyValue tree_DATA_FONT_PROPERTY_INFO8[] = {
	{ "h_size", ValueType::INT, "", (int)0x288 },  // decimal: 648
	{ "v_size", ValueType::INT, "", (int)0x288 },  // decimal: 648
	{ "h_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "v_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "extra_attributes", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "weight", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "family_code", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "style", ValueType::INT, "", (int)0x6 },  // decimal: 6
	{ "sub_style", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "language_code", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "region_code", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "country_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "font_name", ValueType::STR, "FTT-Matisse Pro Latin", 22 },
	{ "file_name", ValueType::STR, "ltn7.pgf", 9 },
	{ "expire_date", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "shadow_option", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /DATA/FONT/PROPERTY/INFO9
static const KeyValue tree_DATA_FONT_PROPERTY_INFO9[] = {
	{ "h_size", ValueType::INT, "", (int)0x1c0 },  // decimal: 448
	{ "v_size", ValueType::INT, "", (int)0x1c0 },  // decimal: 448
	{ "h_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "v_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "extra_attributes", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "weight", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "family_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "style", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "sub_style", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "language_code", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "region_code", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "country_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "font_name", ValueType::STR, "FTT-NewRodin Pro Latin", 23 },
	{ "file_name", ValueType::STR, "ltn8.pgf", 9 },
	{ "expire_date", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "shadow_option", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /DATA/FONT/PROPERTY/INFO10
static const KeyValue tree_DATA_FONT_PROPERTY_INFO10[] = {
	{ "h_size", ValueType::INT, "", (int)0x1c0 },  // decimal: 448
	{ "v_size", ValueType::INT, "", (int)0x1c0 },  // decimal: 448
	{ "h_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "v_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "extra_attributes", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "weight", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "family_code", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "style", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "sub_style", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "language_code", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "region_code", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "country_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "font_name", ValueType::STR, "FTT-Matisse Pro Latin", 22 },
	{ "file_name", ValueType::STR, "ltn9.pgf", 9 },
	{ "expire_date", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "shadow_option", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /DATA/FONT/PROPERTY/INFO11
static const KeyValue tree_DATA_FONT_PROPERTY_INFO11[] = {
	{ "h_size", ValueType::INT, "", (int)0x1c0 },  // decimal: 448
	{ "v_size", ValueType::INT, "", (int)0x1c0 },  // decimal: 448
	{ "h_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "v_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "extra_attributes", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "weight", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "family_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "style", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "sub_style", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "language_code", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "region_code", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "country_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "font_name", ValueType::STR, "FTT-NewRodin Pro Latin", 23 },
	{ "file_name", ValueType::STR, "ltn10.pgf", 10 },
	{ "expire_date", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "shadow_option", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /DATA/FONT/PROPERTY/INFO12
static const KeyValue tree_DATA_FONT_PROPERTY_INFO12[] = {
	{ "h_size", ValueType::INT, "", (int)0x1c0 },  // decimal: 448
	{ "v_size", ValueType::INT, "", (int)0x1c0 },  // decimal: 448
	{ "h_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "v_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "extra_attributes", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "weight", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "family_code", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "style", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "sub_style", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "language_code", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "region_code", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "country_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "font_name", ValueType::STR, "FTT-Matisse Pro Latin", 22 },
	{ "file_name", ValueType::STR, "ltn11.pgf", 10 },
	{ "expire_date", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "shadow_option", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /DATA/FONT/PROPERTY/INFO13
static const KeyValue tree_DATA_FONT_PROPERTY_INFO13[] = {
	{ "h_size", ValueType::INT, "", (int)0x1c0 },  // decimal: 448
	{ "v_size", ValueType::INT, "", (int)0x1c0 },  // decimal: 448
	{ "h_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "v_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "extra_attributes", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "weight", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "family_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "style", ValueType::INT, "", (int)0x5 },  // decimal: 5
	{ "sub_style", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "language_code", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "region_code", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "country_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "font_name", ValueType::STR, "FTT-NewRodin Pro Latin", 23 },
	{ "file_name", ValueType::STR, "ltn12.pgf", 10 },
	{ "expire_date", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "shadow_option", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /DATA/FONT/PROPERTY/INFO14
static const KeyValue tree_DATA_FONT_PROPERTY_INFO14[] = {
	{ "h_size", ValueType::INT, "", (int)0x1c0 },  // decimal: 448
	{ "v_size", ValueType::INT, "", (int)0x1c0 },  // decimal: 448
	{ "h_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "v_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "extra_attributes", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "weight", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "family_code", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "style", ValueType::INT, "", (int)0x5 },  // decimal: 5
	{ "sub_style", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "language_code", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "region_code", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "country_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "font_name", ValueType::STR, "FTT-Matisse Pro Latin", 22 },
	{ "file_name", ValueType::STR, "ltn13.pgf", 10 },
	{ "expire_date", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "shadow_option", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /DATA/FONT/PROPERTY/INFO15
static const KeyValue tree_DATA_FONT_PROPERTY_INFO15[] = {
	{ "h_size", ValueType::INT, "", (int)0x1c0 },  // decimal: 448
	{ "v_size", ValueType::INT, "", (int)0x1c0 },  // decimal: 448
	{ "h_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "v_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "extra_attributes", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "weight", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "family_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "style", ValueType::INT, "", (int)0x6 },  // decimal: 6
	{ "sub_style", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "language_code", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "region_code", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "country_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "font_name", ValueType::STR, "FTT-NewRodin Pro Latin", 23 },
	{ "file_name", ValueType::STR, "ltn14.pgf", 10 },
	{ "expire_date", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "shadow_option", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /DATA/FONT/PROPERTY/INFO16
static const KeyValue tree_DATA_FONT_PROPERTY_INFO16[] = {
	{ "h_size", ValueType::INT, "", (int)0x1c0 },  // decimal: 448
	{ "v_size", ValueType::INT, "", (int)0x1c0 },  // decimal: 448
	{ "h_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "v_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "extra_attributes", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "weight", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "family_code", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "style", ValueType::INT, "", (int)0x6 },  // decimal: 6
	{ "sub_style", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "language_code", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "region_code", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "country_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "font_name", ValueType::STR, "FTT-Matisse Pro Latin", 22 },
	{ "file_name", ValueType::STR, "ltn15.pgf", 10 },
	{ "expire_date", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "shadow_option", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /DATA/FONT/PROPERTY/INFO17
static const KeyValue tree_DATA_FONT_PROPERTY_INFO17[] = {
	{ "h_size", ValueType::INT, "", (int)0x288 },  // decimal: 648
	{ "v_size", ValueType::INT, "", (int)0x288 },  // decimal: 648
	{ "h_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "v_resolution", ValueType::INT, "", (int)0x2000 },  // decimal: 8192
	{ "extra_attributes", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "weight", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "family_code", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "style", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "sub_style", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "language_code", ValueType::INT, "", (int)0x3 },  // decimal: 3
	{ "region_code", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "country_code", ValueType::INT, "", (int)0x3 },  // decimal: 3
	{ "font_name", ValueType::STR, "AsiaNHH(512Johab)", 18 },
	{ "file_name", ValueType::STR, "kr0.pgf", 8 },
	{ "expire_date", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "shadow_option", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /DATA/FONT/PROPERTY
static const KeyValue tree_DATA_FONT_PROPERTY[] = {
	{ "INFO0", ValueType::DIR, "", ARRAY_SIZE(tree_DATA_FONT_PROPERTY_INFO0), tree_DATA_FONT_PROPERTY_INFO0 },
	{ "INFO1", ValueType::DIR, "", ARRAY_SIZE(tree_DATA_FONT_PROPERTY_INFO1), tree_DATA_FONT_PROPERTY_INFO1 },
	{ "INFO2", ValueType::DIR, "", ARRAY_SIZE(tree_DATA_FONT_PROPERTY_INFO2), tree_DATA_FONT_PROPERTY_INFO2 },
	{ "INFO3", ValueType::DIR, "", ARRAY_SIZE(tree_DATA_FONT_PROPERTY_INFO3), tree_DATA_FONT_PROPERTY_INFO3 },
	{ "INFO4", ValueType::DIR, "", ARRAY_SIZE(tree_DATA_FONT_PROPERTY_INFO4), tree_DATA_FONT_PROPERTY_INFO4 },
	{ "INFO5", ValueType::DIR, "", ARRAY_SIZE(tree_DATA_FONT_PROPERTY_INFO5), tree_DATA_FONT_PROPERTY_INFO5 },
	{ "INFO6", ValueType::DIR, "", ARRAY_SIZE(tree_DATA_FONT_PROPERTY_INFO6), tree_DATA_FONT_PROPERTY_INFO6 },
	{ "INFO7", ValueType::DIR, "", ARRAY_SIZE(tree_DATA_FONT_PROPERTY_INFO7), tree_DATA_FONT_PROPERTY_INFO7 },
	{ "INFO8", ValueType::DIR, "", ARRAY_SIZE(tree_DATA_FONT_PROPERTY_INFO8), tree_DATA_FONT_PROPERTY_INFO8 },
	{ "INFO9", ValueType::DIR, "", ARRAY_SIZE(tree_DATA_FONT_PROPERTY_INFO9), tree_DATA_FONT_PROPERTY_INFO9 },
	{ "INFO10", ValueType::DIR, "", ARRAY_SIZE(tree_DATA_FONT_PROPERTY_INFO10), tree_DATA_FONT_PROPERTY_INFO10 },
	{ "INFO11", ValueType::DIR, "", ARRAY_SIZE(tree_DATA_FONT_PROPERTY_INFO11), tree_DATA_FONT_PROPERTY_INFO11 },
	{ "INFO12", ValueType::DIR, "", ARRAY_SIZE(tree_DATA_FONT_PROPERTY_INFO12), tree_DATA_FONT_PROPERTY_INFO12 },
	{ "INFO13", ValueType::DIR, "", ARRAY_SIZE(tree_DATA_FONT_PROPERTY_INFO13), tree_DATA_FONT_PROPERTY_INFO13 },
	{ "INFO14", ValueType::DIR, "", ARRAY_SIZE(tree_DATA_FONT_PROPERTY_INFO14), tree_DATA_FONT_PROPERTY_INFO14 },
	{ "INFO15", ValueType::DIR, "", ARRAY_SIZE(tree_DATA_FONT_PROPERTY_INFO15), tree_DATA_FONT_PROPERTY_INFO15 },
	{ "INFO16", ValueType::DIR, "", ARRAY_SIZE(tree_DATA_FONT_PROPERTY_INFO16), tree_DATA_FONT_PROPERTY_INFO16 },
	{ "INFO17", ValueType::DIR, "", ARRAY_SIZE(tree_DATA_FONT_PROPERTY_INFO17), tree_DATA_FONT_PROPERTY_INFO17 },
};

// Dump of /DATA/FONT
static const KeyValue tree_DATA_FONT[] = {
	{ "path_name", ValueType::STR, "flash0:/font", 13 },
	{ "num_fonts", ValueType::INT, "", (int)0x12 },  // decimal: 18
	{ "PROPERTY", ValueType::DIR, "", ARRAY_SIZE(tree_DATA_FONT_PROPERTY), tree_DATA_FONT_PROPERTY },
};

// Dump of /DATA/COUNT
static const KeyValue tree_DATA_COUNT[] = {
	{ "boot_count", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "game_exec_count", ValueType::INT, "", (int)0x46 },  // decimal: 70
	{ "slide_count", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "usb_connect_count", ValueType::INT, "", (int)0xec },  // decimal: 236
	{ "wifi_connect_count", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "psn_access_count", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /DATA
static const KeyValue tree_DATA[] = {
	{ "FONT", ValueType::DIR, "", ARRAY_SIZE(tree_DATA_FONT), tree_DATA_FONT },
	{ "COUNT", ValueType::DIR, "", ARRAY_SIZE(tree_DATA_COUNT), tree_DATA_COUNT },
};

// Dump of /SYSPROFILE/RESOLUTION
static const KeyValue tree_SYSPROFILE_RESOLUTION[] = {
	{ "horizontal", ValueType::INT, "", (int)0x2012 },  // decimal: 8210
	{ "vertical", ValueType::INT, "", (int)0x2012 },  // decimal: 8210
};

// Dump of /SYSPROFILE
static const KeyValue tree_SYSPROFILE[] = {
	{ "sound_reduction", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "RESOLUTION", ValueType::DIR, "", ARRAY_SIZE(tree_SYSPROFILE_RESOLUTION), tree_SYSPROFILE_RESOLUTION },
};

// Dump of /CONFIG/VIDEO
static const KeyValue tree_CONFIG_VIDEO[] = {
	{ "menu_language", ValueType::BIN, "en", 2 },
	{ "sound_language", ValueType::BIN, "00", 2 },
	{ "subtitle_language", ValueType::BIN, "en", 2 },
	{ "appended_volume", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "lr_button_enable", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "list_play_mode", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "title_display_mode", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "output_ext_menu", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "output_ext_func", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /CONFIG/PHOTO
static const KeyValue tree_CONFIG_PHOTO[] = {
	{ "slideshow_speed", ValueType::INT, "", (int)0x1 },  // decimal: 1
};

// Dump of /CONFIG/MUSIC
static const KeyValue tree_CONFIG_MUSIC[] = {
	{ "wma_play", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "visualizer_mode", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "track_info_mode", ValueType::INT, "", (int)0x1 },  // decimal: 1
};

// Dump of /CONFIG/BROWSER
static const KeyValue tree_CONFIG_BROWSER[] = {
	{ "home_uri", ValueType::STR, "", 1 },
	{ "cookie_mode", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "proxy_mode", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "proxy_address", ValueType::STR, "", 1 },
	{ "proxy_port", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "picture", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "animation", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "javascript", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "cache_size", ValueType::INT, "", (int)0x200 },  // decimal: 512
	{ "char_size", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "disp_mode", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "connect_mode", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "flash_activated", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "flash_play", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "proxy_protect", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "proxy_autoauth", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "proxy_user", ValueType::STR, "", 1 },
	{ "proxy_password", ValueType::STR, "", 1 },
	{ "webpage_quality", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /CONFIG/BROWSER2
static const KeyValue tree_CONFIG_BROWSER2[] = {
	{ "tm_service", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "tm_ec_ttl", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "tm_ec_ttl_update_time", ValueType::BIN, "", 8 },  // (all zero)
	{ "tm_service_sub_status", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /CONFIG/LFTV
static const KeyValue tree_CONFIG_LFTV[] = {
	{ "easy_reg_done", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "netav_domain_name", ValueType::STR, "", 1 },
	{ "netav_ip_address", ValueType::STR, "172.29.71.1", 12 },
	{ "netav_port_no_home", ValueType::INT, "", (int)0x139d },  // decimal: 5021
	{ "netav_port_no_away", ValueType::INT, "", (int)0x139d },  // decimal: 5021
	{ "netav_nonce", ValueType::STR, "", 1 },
	{ "base_station_version", ValueType::STR, "0.000", 6 },
	{ "base_station_region", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "tuner_type", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "input_line", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "tv_channel", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "bitrate_home", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "bitrate_away", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "channel_setting_jp", ValueType::BIN, "", 24 },  // (all zero)
	{ "channel_setting_us", ValueType::BIN, "", 68 },  // (all zero)
	{ "channel_setting_us_catv", ValueType::BIN, "", 125 },  // (all zero)
	{ "overwrite_netav_setting", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "screen_mode", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "remocon_setting_region", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "remocon_setting", ValueType::BIN, "", 96 },  // (all zero)
	{ "remocon_setting_revision", ValueType::STR, "0000.00", 8 },
	{ "external_tuner_channel", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "ssid", ValueType::STR, "", 1 },
	{ "audio_gain", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "broadcast_standard_video1", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "broadcast_standard_video2", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "version", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "tv_channel_range", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "tuner_type_no", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "input_line_no", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "audio_channel", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "shared_remocon_setting", ValueType::BIN, "", 96 },  // (all zero)
};

// Dump of /CONFIG/RSS
static const KeyValue tree_CONFIG_RSS[] = {
	{ "download_items", ValueType::INT, "", (int)0x5 },  // decimal: 5
};

// Dump of /CONFIG/ALARM
static const KeyValue tree_CONFIG_ALARM[] = {
	{ "alarm_0_time", ValueType::INT, "", (int)0xffffffff },  // decimal: -1
	{ "alarm_1_time", ValueType::INT, "", (int)0xffffffff },  // decimal: -1
	{ "alarm_2_time", ValueType::INT, "", (int)0xffffffff },  // decimal: -1
	{ "alarm_3_time", ValueType::INT, "", (int)0xffffffff },  // decimal: -1
	{ "alarm_4_time", ValueType::INT, "", (int)0xffffffff },  // decimal: -1
	{ "alarm_5_time", ValueType::INT, "", (int)0xffffffff },  // decimal: -1
	{ "alarm_6_time", ValueType::INT, "", (int)0xffffffff },  // decimal: -1
	{ "alarm_7_time", ValueType::INT, "", (int)0xffffffff },  // decimal: -1
	{ "alarm_8_time", ValueType::INT, "", (int)0xffffffff },  // decimal: -1
	{ "alarm_9_time", ValueType::INT, "", (int)0xffffffff },  // decimal: -1
	{ "alarm_0_property", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "alarm_1_property", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "alarm_2_property", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "alarm_3_property", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "alarm_4_property", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "alarm_5_property", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "alarm_6_property", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "alarm_7_property", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "alarm_8_property", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "alarm_9_property", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /CONFIG/PREMO
static const KeyValue tree_CONFIG_PREMO[] = {
	{ "guide_page", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "response", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "ps3_name", ValueType::STR, "", 1 },
	{ "ps3_mac", ValueType::BIN, "", 6 },  // (all zero)
	{ "ps3_keytype", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "ps3_key", ValueType::BIN, "", 16 },  // (all zero)
	{ "custom_video_bitrate1", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "custom_video_bitrate2", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "custom_video_buffer1", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "custom_video_buffer2", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "setting_internet", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "button_assign", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "flags", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "account_id", ValueType::BIN, "", 16 },  // (all zero)
	{ "login_id", ValueType::STR, "", 1 },
	{ "password", ValueType::STR, "", 1 },
};

// Dump of /CONFIG/CAMERA
static const KeyValue tree_CONFIG_CAMERA[] = {
	{ "still_size", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "movie_size", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "still_quality", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "movie_quality", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "movie_fps", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "white_balance", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "exposure_bias", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "shutter_sound_mode", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "file_folder", ValueType::INT, "", (int)0x65 },  // decimal: 101
	{ "file_number", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "msid", ValueType::BIN, "", 16 },  // (all zero)
	{ "still_effect", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "medium_type", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "file_number_eflash", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "folder_number_eflash", ValueType::INT, "", (int)0x65 },  // decimal: 101
};

// Dump of /CONFIG/DISPLAY
static const KeyValue tree_CONFIG_DISPLAY[] = {
	{ "aspect_ratio", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "scan_mode", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "screensaver_start_time", ValueType::INT, "", (int)0x258 },  // decimal: 600
	{ "color_space_mode", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "pi_blending_mode", ValueType::INT, "", (int)0x1 },  // decimal: 1
};

// Dump of /CONFIG/NP
static const KeyValue tree_CONFIG_NP[] = {
	{ "env", ValueType::STR, "np", 3 },
	{ "account_id", ValueType::BIN, "", 16 },  // (all zero)
	{ "login_id", ValueType::STR, "", 1 },
	{ "password", ValueType::STR, "", 1 },
	{ "auto_sign_in_enable", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "nav_only", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "np_ad_clock_diff", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "view_mode", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "np_geo_filtering", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "guest_country", ValueType::STR, "", 1 },
	{ "guest_lang", ValueType::STR, "", 1 },
	{ "guest_yob", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "guest_mob", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "guest_dob", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "check_drm", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /CONFIG/ONESEG
static const KeyValue tree_CONFIG_ONESEG[] = {
	{ "schedule_data_key", ValueType::BIN, "", 16 },  // (all zero)
};

// Dump of /CONFIG/SYSTEM/XMB/THEME
static const KeyValue tree_CONFIG_SYSTEM_XMB_THEME[] = {
	{ "color_mode", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "wallpaper_mode", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "system_color", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "custom_theme_mode", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /CONFIG/SYSTEM/XMB
static const KeyValue tree_CONFIG_SYSTEM_XMB[] = {
	{ "theme_type", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "language", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "button_assign", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "THEME", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_SYSTEM_XMB_THEME), tree_CONFIG_SYSTEM_XMB_THEME },
};

// Dump of /CONFIG/SYSTEM/SOUND
static const KeyValue tree_CONFIG_SYSTEM_SOUND[] = {
	{ "main_volume", ValueType::INT, "", (int)0x1b },  // decimal: 27
	{ "mute", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "avls", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "equalizer_mode", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "operation_sound_mode", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "dynamic_normalizer", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /CONFIG/SYSTEM/POWER_SAVING
static const KeyValue tree_CONFIG_SYSTEM_POWER_SAVING[] = {
	{ "suspend_interval", ValueType::INT, "", (int)0x258 },  // decimal: 600
	{ "backlight_off_interval", ValueType::INT, "", (int)0x12c },  // decimal: 300
	{ "wlan_mode", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "active_backlight_mode", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /CONFIG/SYSTEM/LOCK
static const KeyValue tree_CONFIG_SYSTEM_LOCK[] = {
	{ "password", ValueType::BIN, "0000", 4 },
	{ "parental_level", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "browser_start", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /CONFIG/SYSTEM/CHARACTER_SET
static const KeyValue tree_CONFIG_SYSTEM_CHARACTER_SET[] = {
	{ "oem", ValueType::INT, "", (int)0x5 },  // decimal: 5
	{ "ansi", ValueType::INT, "", (int)0x13 },  // decimal: 19
};

// Dump of /CONFIG/SYSTEM
static const KeyValue tree_CONFIG_SYSTEM[] = {
	{ "owner_name", ValueType::STR, "P939", 5 },
	{ "backlight_brightness", ValueType::INT, "", (int)0x3 },  // decimal: 3
	{ "umd_autoboot", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "usb_charge", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "umd_cache", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "usb_auto_connect", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "slide_action", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "first_boot_tick", ValueType::BIN, "\307\216\216\266\023\074\341\000\000\000\000\000\377\377\377\377\377\377\377\377\000b\240\010p\262\241\010 \350\377\t\006\000\002\000\006\000\022\000\050\302\231\010\304\r\217\010\000\000\300\300\340\347\377\t\360\\\274\t6\077\034A", 64 },
	{ "owner_mob", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "owner_dob", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "slide_welcome", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "exh_mode", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "XMB", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_SYSTEM_XMB), tree_CONFIG_SYSTEM_XMB },
	{ "SOUND", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_SYSTEM_SOUND), tree_CONFIG_SYSTEM_SOUND },
	{ "POWER_SAVING", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_SYSTEM_POWER_SAVING), tree_CONFIG_SYSTEM_POWER_SAVING },
	{ "LOCK", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_SYSTEM_LOCK), tree_CONFIG_SYSTEM_LOCK },
	{ "CHARACTER_SET", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_SYSTEM_CHARACTER_SET), tree_CONFIG_SYSTEM_CHARACTER_SET },
};

// Dump of /CONFIG/DATE
static const KeyValue tree_CONFIG_DATE[] = {
	{ "time_format", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "date_format", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "summer_time", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "time_zone_offset", ValueType::INT, "", (int)0x3c },  // decimal: 60
	{ "time_zone_area", ValueType::STR, "sweden", 7 },
};

// Dump of /CONFIG/NETWORK/ADHOC
static const KeyValue tree_CONFIG_NETWORK_ADHOC[] = {
	{ "channel", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "ssid_prefix", ValueType::STR, "PSP", 4 },
};

// Dump of /CONFIG/NETWORK/INFRASTRUCTURE/0/SUB1
static const KeyValue tree_CONFIG_NETWORK_INFRASTRUCTURE_0_SUB1[] = {
	{ "wifisvc_auth_name", ValueType::STR, "", 1 },
	{ "wifisvc_auth_key", ValueType::STR, "", 1 },
	{ "wifisvc_option", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "last_leased_dhcp_addr", ValueType::STR, "", 1 },
	{ "bt_id", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "at_command", ValueType::STR, "", 1 },
	{ "phone_number", ValueType::STR, "", 1 },
};

// Dump of /CONFIG/NETWORK/INFRASTRUCTURE/0
static const KeyValue tree_CONFIG_NETWORK_INFRASTRUCTURE_0[] = {
	{ "SUB1", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_NETWORK_INFRASTRUCTURE_0_SUB1), tree_CONFIG_NETWORK_INFRASTRUCTURE_0_SUB1 },
	{ "version", ValueType::INT, "", (int)0x5 },  // decimal: 5
	{ "device", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "cnf_name", ValueType::STR, "", 1 },
	{ "ssid", ValueType::STR, "", 1 },
	{ "auth_proto", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "wep_key", ValueType::BIN, "", 5 },  // (all zero)
	{ "how_to_set_ip", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "ip_address", ValueType::STR, "", 1 },
	{ "netmask", ValueType::STR, "", 1 },
	{ "default_route", ValueType::STR, "", 1 },
	{ "dns_flag", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "primary_dns", ValueType::STR, "", 1 },
	{ "secondary_dns", ValueType::STR, "", 1 },
	{ "auth_name", ValueType::STR, "", 1 },
	{ "auth_key", ValueType::STR, "", 1 },
	{ "http_proxy_flag", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "http_proxy_server", ValueType::STR, "", 1 },
	{ "http_proxy_port", ValueType::INT, "", (int)0x1f90 },  // decimal: 8080
	{ "auth_8021x_type", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "auth_8021x_auth_name", ValueType::STR, "", 1 },
	{ "auth_8021x_auth_key", ValueType::STR, "", 1 },
	{ "wpa_key_type", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "wpa_key", ValueType::BIN, "", 64 },  // (all zero)
	{ "browser_flag", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "wifisvc_config", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /CONFIG/NETWORK/INFRASTRUCTURE
static const KeyValue tree_CONFIG_NETWORK_INFRASTRUCTURE[] = {
	{ "latest_id", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "eap_md5", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "auto_setting", ValueType::INT, "", (int)0x2 },  // decimal: 2
	{ "wifisvc_setting", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "btdun_warnings_check", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "0", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_NETWORK_INFRASTRUCTURE_0), tree_CONFIG_NETWORK_INFRASTRUCTURE_0 },
};

// Dump of /CONFIG/NETWORK/GO_MESSENGER
static const KeyValue tree_CONFIG_NETWORK_GO_MESSENGER[] = {
	{ "auth_name", ValueType::STR, "", 1 },
	{ "auth_key", ValueType::STR, "", 1 },
};

// Dump of /CONFIG/NETWORK
static const KeyValue tree_CONFIG_NETWORK[] = {
	{ "ADHOC", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_NETWORK_ADHOC), tree_CONFIG_NETWORK_ADHOC },
	{ "INFRASTRUCTURE", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_NETWORK_INFRASTRUCTURE), tree_CONFIG_NETWORK_INFRASTRUCTURE },
	{ "GO_MESSENGER", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_NETWORK_GO_MESSENGER), tree_CONFIG_NETWORK_GO_MESSENGER },
};

// Dump of /CONFIG/OSK
static const KeyValue tree_CONFIG_OSK[] = {
	{ "version_id", ValueType::INT, "", (int)0x226 },  // decimal: 550
	{ "disp_locale", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "writing_locale", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "input_char_mask", ValueType::INT, "", (int)0xf },  // decimal: 15
	{ "keytop_index", ValueType::INT, "", (int)0x5 },  // decimal: 5
};

// Dump of /CONFIG/INFOBOARD
static const KeyValue tree_CONFIG_INFOBOARD[] = {
	{ "locale_lang", ValueType::STR, "", 1 },
	{ "qa_server", ValueType::INT, "", (int)0x0 },  // decimal: 0
};

// Dump of /CONFIG/BT/DEVICE0
static const KeyValue tree_CONFIG_BT_DEVICE0[] = {
	{ "audio_type", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "device_type", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "device_name", ValueType::BIN, "", 64 },  // (all zero)
};

// Dump of /CONFIG/BT/DEVICE1
static const KeyValue tree_CONFIG_BT_DEVICE1[] = {
	{ "audio_type", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "device_type", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "device_name", ValueType::BIN, "", 64 },  // (all zero)
};

// Dump of /CONFIG/BT/DEVICE2
static const KeyValue tree_CONFIG_BT_DEVICE2[] = {
	{ "audio_type", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "device_type", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "device_name", ValueType::BIN, "", 64 },  // (all zero)
};

// Dump of /CONFIG/BT/DEVICE3
static const KeyValue tree_CONFIG_BT_DEVICE3[] = {
	{ "audio_type", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "device_type", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "device_name", ValueType::BIN, "", 64 },  // (all zero)
};

// Dump of /CONFIG/BT/DEVICE4
static const KeyValue tree_CONFIG_BT_DEVICE4[] = {
	{ "audio_type", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "device_type", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "device_name", ValueType::BIN, "", 64 },  // (all zero)
};

// Dump of /CONFIG/BT/DEVICE5
static const KeyValue tree_CONFIG_BT_DEVICE5[] = {
	{ "audio_type", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "device_type", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "device_name", ValueType::BIN, "", 64 },  // (all zero)
};

// Dump of /CONFIG/BT/DEVICE6
static const KeyValue tree_CONFIG_BT_DEVICE6[] = {
	{ "audio_type", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "device_type", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "device_name", ValueType::BIN, "", 64 },  // (all zero)
};

// Dump of /CONFIG/BT/DEVICE7
static const KeyValue tree_CONFIG_BT_DEVICE7[] = {
	{ "audio_type", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "device_type", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "device_name", ValueType::BIN, "", 64 },  // (all zero)
};

// Dump of /CONFIG/BT
static const KeyValue tree_CONFIG_BT[] = {
	{ "connect_mode", ValueType::INT, "", (int)0x0 },  // decimal: 0
	{ "DEVICE0", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_BT_DEVICE0), tree_CONFIG_BT_DEVICE0 },
	{ "DEVICE1", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_BT_DEVICE1), tree_CONFIG_BT_DEVICE1 },
	{ "DEVICE2", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_BT_DEVICE2), tree_CONFIG_BT_DEVICE2 },
	{ "DEVICE3", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_BT_DEVICE3), tree_CONFIG_BT_DEVICE3 },
	{ "DEVICE4", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_BT_DEVICE4), tree_CONFIG_BT_DEVICE4 },
	{ "DEVICE5", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_BT_DEVICE5), tree_CONFIG_BT_DEVICE5 },
	{ "DEVICE6", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_BT_DEVICE6), tree_CONFIG_BT_DEVICE6 },
	{ "DEVICE7", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_BT_DEVICE7), tree_CONFIG_BT_DEVICE7 },
};

// Dump of /CONFIG/GAME
static const KeyValue tree_CONFIG_GAME[] = {
	{ "hibernation_ow_guide", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "hibernation_op_guide", ValueType::INT, "", (int)0x1 },  // decimal: 1
	{ "subs_expiration_guide", ValueType::INT, "", (int)0x1 },  // decimal: 1
};

// Dump of /CONFIG
static const KeyValue tree_CONFIG[] = {
	{ "VIDEO", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_VIDEO), tree_CONFIG_VIDEO },
	{ "PHOTO", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_PHOTO), tree_CONFIG_PHOTO },
	{ "MUSIC", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_MUSIC), tree_CONFIG_MUSIC },
	{ "BROWSER", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_BROWSER), tree_CONFIG_BROWSER },
	{ "BROWSER2", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_BROWSER2), tree_CONFIG_BROWSER2 },
	{ "LFTV", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_LFTV), tree_CONFIG_LFTV },
	{ "RSS", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_RSS), tree_CONFIG_RSS },
	{ "ALARM", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_ALARM), tree_CONFIG_ALARM },
	{ "PREMO", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_PREMO), tree_CONFIG_PREMO },
	{ "CAMERA", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_CAMERA), tree_CONFIG_CAMERA },
	{ "DISPLAY", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_DISPLAY), tree_CONFIG_DISPLAY },
	{ "NP", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_NP), tree_CONFIG_NP },
	{ "ONESEG", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_ONESEG), tree_CONFIG_ONESEG },
	{ "SYSTEM", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_SYSTEM), tree_CONFIG_SYSTEM },
	{ "DATE", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_DATE), tree_CONFIG_DATE },
	{ "NETWORK", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_NETWORK), tree_CONFIG_NETWORK },
	{ "OSK", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_OSK), tree_CONFIG_OSK },
	{ "INFOBOARD", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_INFOBOARD), tree_CONFIG_INFOBOARD },
	{ "BT", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_BT), tree_CONFIG_BT },
	{ "GAME", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG_GAME), tree_CONFIG_GAME },
};

// Dump of /REGISTRY
static const KeyValue tree_REGISTRY[] = {
	{ "category_version", ValueType::INT, "", (int)0x66 },  // decimal: 102
};

// There might be more categories.
const KeyValue ROOT[] = {
	{ "REGISTRY", ValueType::DIR, "", ARRAY_SIZE(tree_REGISTRY), tree_REGISTRY },
	{ "CONFIG", ValueType::DIR, "", ARRAY_SIZE(tree_CONFIG), tree_CONFIG },
	{ "DATA", ValueType::DIR, "", ARRAY_SIZE(tree_DATA), tree_DATA },
	{ "SYSPROFILE", ValueType::DIR, "", ARRAY_SIZE(tree_SYSPROFILE), tree_SYSPROFILE },
};

namespace {

constexpr u32 VSH_REGISTRY_MAX_VALUE_SIZE = 1024 * 1024;

Path RegistryDirectoryPath() {
	return g_Config.nandRootDirectory / "flash1/registry";
}

Path RegistryIregPath() {
	return RegistryDirectoryPath() / "system.ireg";
}

Path RegistryDregPath() {
	return RegistryDirectoryPath() / "system.dreg";
}

Path RegistryDregBackupPath() {
	return RegistryDirectoryPath() / "system.dreg.bak";
}

Path RegistryDregTemporaryPath() {
	return RegistryDirectoryPath() / "system.dreg.tmp";
}

std::string NormalizeRegistryPath(std::string_view path) {
	std::string normalized(path);
	std::replace(normalized.begin(), normalized.end(), '\\', '/');

	constexpr std::string_view prefixes[] = {
		"flash1:/registry/system",
		"flash1/registry/system",
		"flash2:/registry/system",
		"flash2/registry/system",
		"/system",
	};
	for (std::string_view prefix : prefixes) {
		if (startsWithNoCase(normalized, prefix)) {
			normalized.erase(0, prefix.size());
			break;
		}
	}
	if (normalized.empty()) {
		return "/";
	}
	if (normalized.front() != '/') {
		normalized.insert(normalized.begin(), '/');
	}
	while (normalized.size() > 1 && normalized.back() == '/') {
		normalized.pop_back();
	}
	return normalized;
}

std::string ChildRegistryPath(std::string_view parent, std::string_view name) {
	const std::string normalizedParent = NormalizeRegistryPath(parent);
	return normalizedParent == "/" ? "/" + std::string(name) : normalizedParent + "/" + std::string(name);
}

void AddKeyInOrder(RegistryCategory &category, std::string_view name) {
	if (std::find(category.order.begin(), category.order.end(), name) == category.order.end()) {
		category.order.emplace_back(name);
	}
}

RegistryValue SeedRegistryValue(const KeyValue &value) {
	RegistryValue result;
	result.type = value.type;
	switch (value.type) {
	case ValueType::INT:
		result.data.resize(sizeof(u32));
		result.data[0] = (u8)value.intValue;
		result.data[1] = (u8)(value.intValue >> 8);
		result.data[2] = (u8)(value.intValue >> 16);
		result.data[3] = (u8)(value.intValue >> 24);
		break;
	case ValueType::STR:
	case ValueType::BIN:
		result.data.resize(std::max(0, value.intValue), 0);
		if (value.strValue && value.strValue[0] != '\0' && !result.data.empty()) {
			memcpy(result.data.data(), value.strValue, result.data.size());
		}
		break;
	case ValueType::DIR:
	case ValueType::FAIL:
	default:
		break;
	}
	return result;
}

void SeedRegistryCategory(std::string_view path, const KeyValue *values, int count) {
	RegistryCategory &category = g_registryCategories[NormalizeRegistryPath(path)];
	if (count == 1 && values[0].type == ValueType::FAIL) {
		category.accessError = values[0].intValue;
		return;
	}
	for (int i = 0; i < count; ++i) {
		const KeyValue &source = values[i];
		AddKeyInOrder(category, source.name);
		category.values[source.name] = SeedRegistryValue(source);
		if (source.type == ValueType::DIR) {
			SeedRegistryCategory(ChildRegistryPath(path, source.name), source.dirContents, source.intValue);
		}
	}
}

void PutRegistryInt(std::string_view path, std::string_view name, u32 value) {
	RegistryCategory &category = g_registryCategories[NormalizeRegistryPath(path)];
	AddKeyInOrder(category, name);
	RegistryValue &entry = category.values[std::string(name)];
	entry.type = ValueType::INT;
	entry.data = {(u8)value, (u8)(value >> 8), (u8)(value >> 16), (u8)(value >> 24)};
}

void PutRegistryString(std::string_view path, std::string_view name, std::string_view value) {
	RegistryCategory &category = g_registryCategories[NormalizeRegistryPath(path)];
	AddKeyInOrder(category, name);
	RegistryValue &entry = category.values[std::string(name)];
	entry.type = ValueType::STR;
	entry.data.assign(value.begin(), value.end());
	entry.data.push_back(0);
}

void PutRegistryBinary(std::string_view path, std::string_view name, size_t size, u8 value = 0) {
	RegistryCategory &category = g_registryCategories[NormalizeRegistryPath(path)];
	AddKeyInOrder(category, name);
	RegistryValue &entry = category.values[std::string(name)];
	entry.type = ValueType::BIN;
	entry.data.assign(size, value);
}

void SeedFactoryRegistry(bool directVsh) {
	g_registryCategories.clear();
	SeedRegistryCategory("/", ROOT, ARRAY_SIZE(ROOT));
	if (!directVsh) {
		return;
	}

	// The structural dump above is useful, but factory state must not preserve
	// the donor PSP's identity, counters, clock area, or first-boot payload.
	PutRegistryString("/CONFIG/SYSTEM", "owner_name", "PPSSPP");
	PutRegistryBinary("/CONFIG/SYSTEM", "first_boot_tick", 64);
	PutRegistryInt("/DATA/COUNT", "boot_count", 0);
	PutRegistryInt("/DATA/COUNT", "game_exec_count", 0);
	PutRegistryInt("/DATA/COUNT", "slide_count", 0);
	PutRegistryInt("/DATA/COUNT", "usb_connect_count", 0);
	PutRegistryInt("/DATA/COUNT", "wifi_connect_count", 0);
	PutRegistryInt("/DATA/COUNT", "psn_access_count", 0);
	PutRegistryInt("/CONFIG/DATE", "time_zone_offset", 0);
	PutRegistryString("/CONFIG/DATE", "time_zone_area", "utc");
	PutRegistryInt("/CONFIG/SYSTEM/XMB", "language", 1);
	PutRegistryInt("/CONFIG/SYSTEM/XMB", "button_assign", 1);
}

bool ReadRegistryBytes(const Path &path, std::vector<u8> *data) {
	std::string file;
	if (!File::ReadBinaryFileToString(path, &file)) {
		return false;
	}
	data->assign(file.begin(), file.end());
	return true;
}

void ImportRealRegistryCategories(const PSPRegistryFile &registry) {
	g_registryCategories.clear();
	for (const auto &[path, source] : registry.Categories()) {
		RegistryCategory category;
		category.order = source.order;
		for (const auto &[name, sourceValue] : source.values) {
			RegistryValue value;
			value.type = (ValueType)sourceValue.type;
			value.data = sourceValue.data;
			category.values.emplace(name, std::move(value));
		}
		g_registryCategories.emplace(NormalizeRegistryPath(path), std::move(category));
	}

	// IREG stores each top-level category independently. sceReg exposes their
	// names when callers enumerate the synthetic API root.
	RegistryCategory &root = g_registryCategories["/"];
	for (const auto &[path, _] : registry.Categories()) {
		if (path.size() <= 1 || path.find('/', 1) != std::string::npos) {
			continue;
		}
		const std::string name = path.substr(1);
		AddKeyInOrder(root, name);
		root.values[name].type = ValueType::DIR;
	}
}

bool LoadRealRegistryBacking(bool importCategories, std::string *error) {
	std::vector<u8> ireg;
	std::vector<u8> dreg;
	if (!ReadRegistryBytes(RegistryIregPath(), &ireg) || !ReadRegistryBytes(RegistryDregPath(), &dreg)) {
		if (error) {
			*error = "system.ireg/system.dreg are missing";
		}
		return false;
	}
	auto registry = std::make_unique<PSPRegistryFile>();
	std::string parseError;
	if (!registry->Load(ireg, dreg, &parseError)) {
		// The real backup is useful only when the primary DREG cannot pass its
		// PSP checksums. IREG is unchanged by existing-key edits.
		std::vector<u8> backupDreg;
		if (!ReadRegistryBytes(RegistryDregBackupPath(), &backupDreg) || !registry->Load(ireg, backupDreg, &parseError)) {
			if (error) {
				*error = parseError;
			}
			return false;
		}
		g_registryRecovered = true;
	}
	if (importCategories) {
		ImportRealRegistryCategories(*registry);
	}
	g_realRegistry = std::move(registry);
	return true;
}

bool WriteRealDreg(const std::vector<u8> &data) {
	const Path primary = RegistryDregPath();
	const Path backup = RegistryDregBackupPath();
	const Path temporary = RegistryDregTemporaryPath();
	File::Delete(temporary, true);
	File::IOFile output(temporary, "wb");
	if (!output || !output.WriteBytes(data.data(), data.size()) || !output.Flush() || !output.Close()) {
		File::Delete(temporary, true);
		return false;
	}
	if (File::Exists(backup) && !File::Delete(backup)) {
		File::Delete(temporary, true);
		return false;
	}
	if (!File::Rename(primary, backup)) {
		File::Delete(temporary, true);
		return false;
	}
	if (!File::Rename(temporary, primary)) {
		File::Rename(backup, primary);
		File::Delete(temporary, true);
		return false;
	}
	return true;
}

bool PersistRegistry() {
	if (!g_registryPersistent || !g_registryDirty) {
		return true;
	}
	if (!g_registryUsesRealFiles) {
		return false;
	}
	std::string error;
	if (!g_realRegistry && !LoadRealRegistryBacking(false, &error)) {
		ERROR_LOG(Log::sceReg, "Could not reload real PSP registry backing: %s", error.c_str());
		return false;
	}
	PSPRegistryFile updated = *g_realRegistry;
	for (const auto &[path, name] : g_registryDirtyValues) {
		auto category = g_registryCategories.find(path);
		if (category == g_registryCategories.end()) {
			return false;
		}
		auto value = category->second.values.find(name);
		if (value == category->second.values.end() || value->second.type == ValueType::DIR || !updated.HasValue(path, name)) {
			ERROR_LOG(Log::sceReg, "Refusing to persist non-IREG key %s/%s", path.c_str(), name.c_str());
			return false;
		}
		if (!updated.SetValue(path, name, (PSPRegistryValueType)value->second.type, value->second.data, &error)) {
			ERROR_LOG(Log::sceReg, "Could not update real PSP registry %s/%s: %s", path.c_str(), name.c_str(), error.c_str());
			return false;
		}
	}
	if (!WriteRealDreg(updated.DregData())) {
		ERROR_LOG(Log::sceReg, "Failed atomically writing flash1:/registry/system.dreg");
		return false;
	}
	g_realRegistry = std::make_unique<PSPRegistryFile>(std::move(updated));
	g_registryGeneration++;
	g_registryDirty = false;
	g_registryDirtyValues.clear();
	INFO_LOG(Log::sceReg, "Persisted real PSP system.dreg generation %llu", (unsigned long long)g_registryGeneration);
	return true;
}

bool ValidateRealRegistryValue(std::string_view path, std::string_view name, ValueType type,
	const std::vector<u8> &data, std::string *error) {
	if (!g_registryUsesRealFiles) {
		return true;
	}
	if (!g_realRegistry && !LoadRealRegistryBacking(false, error)) {
		return false;
	}
	PSPRegistryFile validation = *g_realRegistry;
	return validation.SetValue(NormalizeRegistryPath(path), std::string(name), (PSPRegistryValueType)type, data, error);
}

bool ReadRegistryInt(std::string_view path, std::string_view name, u32 *value) {
	auto category = g_registryCategories.find(NormalizeRegistryPath(path));
	if (category == g_registryCategories.end()) {
		return false;
	}
	auto entry = category->second.values.find(std::string(name));
	if (entry == category->second.values.end() || entry->second.type != ValueType::INT || entry->second.data.size() != sizeof(u32)) {
		return false;
	}
	*value = (u32)entry->second.data[0] | ((u32)entry->second.data[1] << 8) | ((u32)entry->second.data[2] << 16) | ((u32)entry->second.data[3] << 24);
	return true;
}

bool ReadRegistryString(std::string_view path, std::string_view name, std::string *value) {
	auto category = g_registryCategories.find(NormalizeRegistryPath(path));
	if (category == g_registryCategories.end()) {
		return false;
	}
	auto entry = category->second.values.find(std::string(name));
	if (entry == category->second.values.end() || entry->second.type != ValueType::STR) {
		return false;
	}
	const size_t length = !entry->second.data.empty() && entry->second.data.back() == 0 ? entry->second.data.size() - 1 : entry->second.data.size();
	if (length == 0) {
		value->clear();
	} else {
		value->assign((const char *)entry->second.data.data(), length);
	}
	return true;
}

void ApplyRegistrySystemSettings() {
	if (!g_registryPersistent) {
		return;
	}
	u32 value = 0;
	std::string stringValue;
	if (ReadRegistryString("/CONFIG/SYSTEM", "owner_name", &stringValue)) {
		g_Config.sNickName = stringValue;
	}
	if (ReadRegistryInt("/CONFIG/SYSTEM/XMB", "language", &value) && value <= 11) {
		g_Config.iLanguage = (int)value;
	}
	if (ReadRegistryInt("/CONFIG/SYSTEM/XMB", "button_assign", &value) && value <= 1) {
		g_Config.iButtonPreference = (int)value;
	}
	if (ReadRegistryInt("/CONFIG/DATE", "time_format", &value)) {
		g_Config.iTimeFormat = (int)value;
	}
	if (ReadRegistryInt("/CONFIG/DATE", "date_format", &value)) {
		g_Config.iDateFormat = (int)value;
	}
	if (ReadRegistryInt("/CONFIG/DATE", "time_zone_offset", &value)) {
		g_Config.iTimeZone = (int)value;
	}
	if (ReadRegistryInt("/CONFIG/DATE", "summer_time", &value)) {
		g_Config.bDayLightSavings = value != 0;
	}
	if (ReadRegistryInt("/CONFIG/SYSTEM/LOCK", "parental_level", &value)) {
		g_Config.iLockParentalLevel = (int)value;
	}
	if (ReadRegistryInt("/CONFIG/NETWORK/ADHOC", "channel", &value)) {
		g_Config.iWlanAdhocChannel = (int)value;
	}
	if (ReadRegistryInt("/CONFIG/SYSTEM/POWER_SAVING", "wlan_mode", &value)) {
		g_Config.bWlanPowerSave = value != 0;
	}
}

void EnsureRegistryLoaded() {
	if (g_registryLoaded) {
		return;
	}
	g_registryLoaded = true;
	g_registryPersistent = false;
	g_registryDirty = false;
	g_registryRecovered = false;
	g_registryGeneration = 0;
	g_registryUsesRealFiles = false;
	g_realRegistry.reset();
	g_registryDirtyValues.clear();
	SeedFactoryRegistry(false);
	if (!__KernelIsRunningVSH()) {
		return;
	}

	std::string error;
	if (!LoadRealRegistryBacking(true, &error)) {
		ERROR_LOG(Log::sceReg, "Direct VSH could not load flash1:/registry/system.ireg/system.dreg: %s", error.c_str());
		NOTICE_LOG(Log::sceReg, "Direct VSH registry is read-only until valid real flash1 files are provided");
		return;
	}
	g_registryPersistent = true;
	g_registryUsesRealFiles = true;
	ApplyRegistrySystemSettings();
	NOTICE_LOG(Log::sceReg, "Loaded real PSP flash1 registry: %zu categories%s", g_registryCategories.size(),
		g_registryRecovered ? " (recovered DREG backup)" : "");
	if (g_registryRecovered) {
		g_registryDirty = true;
		PersistRegistry();
	}
}

RegistryCategory *LookupMutableCategory(std::string_view path) {
	EnsureRegistryLoaded();
	auto category = g_registryCategories.find(NormalizeRegistryPath(path));
	return category == g_registryCategories.end() ? nullptr : &category->second;
}

const RegistryCategory *LookupMutableCategory(std::string_view path, bool) {
	return LookupMutableCategory(path);
}

bool CategoryIsWritable(const OpenCategory &category) {
	return g_openRegistryMode == 1 && category.openMode == 1;
}

bool EnsureCategoryPath(std::string_view path) {
	const std::string normalized = NormalizeRegistryPath(path);
	if (g_registryCategories.find(normalized) != g_registryCategories.end()) {
		return true;
	}
	if (normalized == "/") {
		g_registryCategories[normalized];
		return true;
	}
	const size_t slash = normalized.find_last_of('/');
	const std::string parent = slash == 0 ? "/" : normalized.substr(0, slash);
	const std::string name = normalized.substr(slash + 1);
	if (name.empty() || !EnsureCategoryPath(parent)) {
		return false;
	}
	RegistryCategory &parentCategory = g_registryCategories[parent];
	AddKeyInOrder(parentCategory, name);
	RegistryValue &directory = parentCategory.values[name];
	directory.type = ValueType::DIR;
	directory.data.clear();
	g_registryCategories[normalized];
	return true;
}

}  // namespace

// Updater checks for CONFIG/SYSTEM/XMB.

// not sure what modes exist, this is conjecture.
enum RegOpenMode {
	REG_OPEN_READONLY = 2,
};

void __RegInit() {
	g_openRegistryMode = 0;
	g_handleGen = 1337;
	g_openCategories.clear();
	g_registryCategories.clear();
	g_registryLoaded = false;
	g_registryPersistent = false;
	g_registryDirty = false;
	g_registryRecovered = false;
	g_registryGeneration = 0;
	g_registryUsesRealFiles = false;
	g_realRegistry.reset();
	g_registryDirtyValues.clear();
}

void __RegShutdown() {
	if (g_registryLoaded && g_registryDirty) {
		PersistRegistry();
	}
	g_openCategories.clear();
	g_registryCategories.clear();
	g_registryLoaded = false;
	g_registryUsesRealFiles = false;
	g_realRegistry.reset();
	g_registryDirtyValues.clear();
}

void __RegDoState(PointerWrap &p) {
	auto s = p.Section("sceReg", 0, 3);
	if (!s)
		return;
	Do(p, g_openRegistryMode);
	Do(p, g_openCategories);
	if (s >= 2) {
		Do(p, g_registryCategories);
		Do(p, g_registryLoaded);
		Do(p, g_registryPersistent);
		Do(p, g_registryDirty);
		Do(p, g_registryRecovered);
		Do(p, g_registryGeneration);
		if (s >= 3) {
			Do(p, g_registryUsesRealFiles);
		} else if (p.mode == p.MODE_READ) {
			g_registryUsesRealFiles = false;
		}
	} else if (p.mode == p.MODE_READ) {
		g_registryCategories.clear();
		g_registryLoaded = false;
		g_registryPersistent = false;
		g_registryDirty = false;
		g_registryRecovered = false;
		g_registryGeneration = 0;
		g_registryUsesRealFiles = false;
	}
	if (p.mode == p.MODE_READ) {
		g_realRegistry.reset();
		g_registryDirtyValues.clear();
	}
}

// Registry level (it seems only /system can exist, so kinda pointless)
int sceRegOpenRegistry(u32 regParamAddr, int mode, u32 regHandleAddr) {
	EnsureRegistryLoaded();
	// There's only one registry and its handle is 0.
	if (!Memory::IsValid4AlignedAddress(regHandleAddr)) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_INVALID_PATH, "invalid handle pointer");
	}
	Memory::WriteUnchecked_U32(0, regHandleAddr);
	g_openRegistryMode = mode;

	if (mode != 1 && mode != REG_OPEN_READONLY) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_PERMISSION_FAILURE, "invalid mode %d", mode);
	}

	return hleLogInfo(Log::sceReg, 0, "mode=%d generation=%llu persistent=%d recovered=%d", mode,
		(unsigned long long)g_registryGeneration, g_registryPersistent, g_registryRecovered);
}

int sceRegCloseRegistry(int regHandle) {
	if (regHandle != 0) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_REGISTRY_NOT_FOUND);
	}
	if (!PersistRegistry()) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_REGISTRY_NOT_FOUND, "failed to persist registry");
	}
	g_openCategories.clear();
	return hleLogInfo(Log::sceReg, 0);
}

int sceRegFlushRegistry(int regHandle) {
	if (regHandle != 0) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_REGISTRY_NOT_FOUND);
	}
	if (!PersistRegistry()) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_REGISTRY_NOT_FOUND, "failed to persist registry");
	}
	return hleLogInfo(Log::sceReg, 0);
}

// Seems dangerous! Have not dared to test this on hardware.
int sceRegRemoveRegistry(u32 regParamAddr) {
	EnsureRegistryLoaded();
	if (g_registryUsesRealFiles) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_PERMISSION_FAILURE, "refusing to replace real system.ireg/system.dreg");
	}
	if (g_openRegistryMode != 1) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_PERMISSION_FAILURE);
	}
	SeedFactoryRegistry(true);
	g_openCategories.clear();
	g_registryDirty = true;
	ApplyRegistrySystemSettings();
	if (!PersistRegistry()) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_REGISTRY_NOT_FOUND, "failed to reset registry");
	}
	return hleLogInfo(Log::sceReg, 0, "reset to deterministic defaults");
}

int sceRegOpenCategory(int regHandle, const char *name, int mode, u32 regHandleAddr) {
	if (!Memory::IsValid4AlignedAddress(regHandleAddr)) {
		return -1;
	}
	if (!name) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_INVALID_NAME);
	}
	if (mode != 1 && mode != REG_OPEN_READONLY) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_PERMISSION_FAILURE, "invalid mode %d", mode);
	}
	if (regHandle != 0) {
		Memory::WriteUnchecked_U32(-1, regHandleAddr);
		return hleLogError(Log::sceReg, SCE_REG_ERROR_REGISTRY_NOT_FOUND, "Invalid registry");
	}

	if (equals(name, "") || equals(name, "/")) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_INVALID_PATH);
	}

	const std::string path = NormalizeRegistryPath(name);
	RegistryCategory *category = LookupMutableCategory(path);
	if (!category && mode == 1 && g_openRegistryMode == 1 && !g_registryUsesRealFiles) {
		if (!EnsureCategoryPath(path)) {
			Memory::WriteUnchecked_U32(-1, regHandleAddr);
			return hleLogError(Log::sceReg, SCE_REG_ERROR_INVALID_PATH);
		}
		g_registryDirty = true;
		if (!PersistRegistry()) {
			Memory::WriteUnchecked_U32(-1, regHandleAddr);
			return hleLogError(Log::sceReg, SCE_REG_ERROR_REGISTRY_NOT_FOUND, "failed to persist new category");
		}
		category = LookupMutableCategory(path);
	}
	if (!category) {
		Memory::WriteUnchecked_U32(-1, regHandleAddr);
		return hleLogError(Log::sceReg, SCE_REG_ERROR_CATEGORY_NOT_FOUND, "%s", path.c_str());
	}

	// Let's see if this category is marked as inaccessible (presumably from user mode)..
	if (category->accessError != 0) {
		return hleLogWarning(Log::sceReg, category->accessError, "Inaccessible category");
	}

	int handle = g_handleGen++;
	OpenCategory cat{ path, mode };
	g_openCategories[handle] = cat;
	Memory::WriteUnchecked_U32(handle, regHandleAddr);
	return hleLogInfo(Log::sceReg, 0, "open handle: %d", handle);
}

int sceRegCloseCategory(int regHandle) {
	auto iter = g_openCategories.find(regHandle);
	if (iter == g_openCategories.end()) {
		// Not found
		return hleLogError(Log::sceReg, 0, "Not an open category");
	}

	g_openCategories.erase(iter);
	return hleLogInfo(Log::sceReg, 0);
}

int sceRegRemoveCategory(int regHandle, const char *name) {
	if (!name) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_INVALID_NAME);
	}
	EnsureRegistryLoaded();
	if (g_registryUsesRealFiles) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_PERMISSION_FAILURE, "real IREG category allocation is immutable");
	}
	std::string path;
	if (regHandle == 0) {
		if (g_openRegistryMode != 1) {
			return hleLogError(Log::sceReg, SCE_REG_ERROR_PERMISSION_FAILURE);
		}
		path = NormalizeRegistryPath(name);
	} else {
		auto categoryHandle = g_openCategories.find(regHandle);
		if (categoryHandle == g_openCategories.end() || !CategoryIsWritable(categoryHandle->second)) {
			return hleLogError(Log::sceReg, SCE_REG_ERROR_PERMISSION_FAILURE);
		}
		path = ChildRegistryPath(categoryHandle->second.path, name);
	}
	if (path == "/" || g_registryCategories.find(path) == g_registryCategories.end()) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_CATEGORY_NOT_FOUND);
	}
	for (auto it = g_registryCategories.begin(); it != g_registryCategories.end();) {
		if (it->first == path || startsWith(it->first, path + "/")) {
			it = g_registryCategories.erase(it);
		} else {
			++it;
		}
	}
	const size_t slash = path.find_last_of('/');
	const std::string parent = slash == 0 ? "/" : path.substr(0, slash);
	const std::string key = path.substr(slash + 1);
	auto parentCategory = g_registryCategories.find(parent);
	if (parentCategory != g_registryCategories.end()) {
		parentCategory->second.values.erase(key);
		parentCategory->second.order.erase(std::remove(parentCategory->second.order.begin(), parentCategory->second.order.end(), key), parentCategory->second.order.end());
	}
	g_registryDirty = true;
	return PersistRegistry() ? hleLogInfo(Log::sceReg, 0) : hleLogError(Log::sceReg, SCE_REG_ERROR_REGISTRY_NOT_FOUND);
}

int sceRegFlushCategory(int regHandle) {
	if (g_openCategories.find(regHandle) == g_openCategories.end()) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_CATEGORY_NOT_FOUND);
	}
	return PersistRegistry() ? hleLogInfo(Log::sceReg, 0) : hleLogError(Log::sceReg, SCE_REG_ERROR_REGISTRY_NOT_FOUND);
}

// Key level

static RegistryCategory *CategoryFromHandle(int handle, OpenCategory **openCategory = nullptr) {
	auto opened = g_openCategories.find(handle);
	if (opened == g_openCategories.end()) {
		return nullptr;
	}
	if (openCategory) {
		*openCategory = &opened->second;
	}
	return LookupMutableCategory(opened->second.path);
}

static int RegistryValueSize(const RegistryValue &value) {
	return value.type == ValueType::INT ? (int)sizeof(u32) : (value.type == ValueType::DIR ? 0 : (int)value.data.size());
}

static int WriteRegistryValue(const RegistryValue &value, u32 bufAddr, u32 size) {
	const u32 required = (u32)RegistryValueSize(value);
	if (!Memory::IsValidRange(bufAddr, size)) {
		return -1;
	}
	switch (value.type) {
	case ValueType::INT:
		if (size < sizeof(u32) || value.data.size() != sizeof(u32)) {
			return SCE_REG_ERROR_INVALID_PATH;
		}
		Memory::MemcpyUnchecked(bufAddr, value.data.data(), sizeof(u32));
		return 0;
	case ValueType::STR:
	case ValueType::BIN:
		if (size != 0 && required != 0) {
			Memory::MemcpyUnchecked(bufAddr, value.data.data(), std::min(size, required));
		}
		return 0;
	case ValueType::DIR:
	case ValueType::FAIL:
	default:
		return SCE_REG_ERROR_INVALID_PATH;
	}
}

int sceRegGetKeysNum(int catHandle, u32 numAddr) {
	RegistryCategory *category = CategoryFromHandle(catHandle);
	if (!category) {
		return hleLogError(Log::sceReg, 0, "Not an open category");
	}

	if (!Memory::IsValid4AlignedAddress(numAddr)) {
		return -1;
	}

	Memory::WriteUnchecked_U32((u32)category->order.size(), numAddr);
	return hleLogInfo(Log::sceReg, 0);
}

int sceRegGetKeys(int catHandle, u32 bufAddr, int num) {
	RegistryCategory *category = CategoryFromHandle(catHandle);
	if (!category) {
		return hleLogError(Log::sceReg, 0, "Not an open category");
	}

	if (num < 0) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_INVALID_PATH);
	}
	const int keyLen = 27; // 27 bytes per key name, including null terminator. For some reason?!?

	if (!Memory::IsValidRange(bufAddr, num * keyLen)) {
		return hleLogError(Log::sceReg, -1, "bad output addr");
	}

	const int count = std::min((int)category->order.size(), num);
	for (int i = 0; i < count; i++) {
		char *dest = (char *)Memory::GetPointerWriteOrException(bufAddr + i * keyLen);
		memset(dest, 0, keyLen);
		strncpy(dest, category->order[i].c_str(), keyLen - 1);
	}

	return hleLogInfo(Log::sceReg, 0);
}

int sceRegGetKeyInfo(int catHandle, const char *name, u32 outKeyHandleAddr, u32 outTypeAddr, u32 outSizeAddr) {
	if (!name) {
		return hleLogError(Log::sceReg, -1, "Invalid name pointer");
	}

	RegistryCategory *category = CategoryFromHandle(catHandle);
	if (!category) {
		return hleLogError(Log::sceReg, 0, "Not found");
	}

	for (int i = 0; i < (int)category->order.size(); i++) {
		if (equals(category->order[i], name)) {
			auto value = category->values.find(category->order[i]);
			if (value == category->values.end()) {
				return hleLogWarning(Log::sceReg, SCE_REG_ERROR_CATEGORY_NOT_FOUND);
			}
			// Found it!
			if (Memory::IsValid4AlignedAddress(outKeyHandleAddr)) {
				// Let's just make the index the key handle.
				Memory::WriteUnchecked_U32(i, outKeyHandleAddr);
			}
			if (Memory::IsValid4AlignedAddress(outTypeAddr)) {
				// Let's just make the index the key handle.
				Memory::WriteUnchecked_U32((int)value->second.type, outTypeAddr);
			}
			const int size = RegistryValueSize(value->second);
			if (Memory::IsValid4AlignedAddress(outSizeAddr)) {
				Memory::WriteUnchecked_U32(size, outSizeAddr);
			}
			return hleLogInfo(Log::sceReg, 0, "handle: %d type: %d size: %d", i, (int)value->second.type, size);
		}
	}

	return hleLogWarning(Log::sceReg, -1, "key with name '%s' not found", name);
}

int sceRegGetKeyInfoByName(int catHandle, const char *name, u32 typeAddr, u32 sizeAddr) {
	if (!name) {
		return hleLogError(Log::sceReg, -1, "Invalid name pointer");
	}

	RegistryCategory *category = CategoryFromHandle(catHandle);
	if (!category) {
		return hleLogError(Log::sceReg, 0, "Not an open category");
	}

	auto value = category->values.find(name);
	if (value != category->values.end()) {
			const int size = RegistryValueSize(value->second);
			if (Memory::IsValid4AlignedAddress(typeAddr)) {
				Memory::WriteUnchecked_U32((int)value->second.type, typeAddr);
			}
			if (Memory::IsValid4AlignedAddress(sizeAddr)) {
				Memory::WriteUnchecked_U32(size, sizeAddr);
			}
			return hleLogInfo(Log::sceReg, 0, "type: %d size: %d", (int)value->second.type, size);
	}

	return hleLogWarning(Log::sceReg, -1, "key with name '%s' not found", name);
}

int sceRegGetKeyValue(int catHandle, int keyHandle, u32 bufAddr, u32 size) {
	RegistryCategory *category = CategoryFromHandle(catHandle);
	if (!category) {
		return hleLogError(Log::sceReg, 0, "Not found");
	}
	if (keyHandle < 0 || keyHandle >= (int)category->order.size()) {
		return hleLogWarning(Log::sceReg, SCE_REG_ERROR_CATEGORY_NOT_FOUND);
	}
	auto value = category->values.find(category->order[keyHandle]);
	if (value == category->values.end()) {
		return hleLogWarning(Log::sceReg, SCE_REG_ERROR_CATEGORY_NOT_FOUND);
	}
	const int result = WriteRegistryValue(value->second, bufAddr, size);
	return result == 0 ? hleLogInfo(Log::sceReg, 0, "type=%d size=%d", (int)value->second.type, RegistryValueSize(value->second)) : hleLogError(Log::sceReg, result);
}

int sceRegGetKeyValueByName(int catHandle, const char *name, u32 bufAddr, u32 size) {
	if (!name) {
		return hleLogError(Log::sceReg, -1, "Invalid name pointer");
	}
	RegistryCategory *category = CategoryFromHandle(catHandle);
	if (!category) {
		return hleLogError(Log::sceReg, 0, "Not found");
	}
	auto value = category->values.find(name);
	if (value == category->values.end()) {
		return hleLogWarning(Log::sceReg, -1, "key with name '%s' not found", name);
	}
	const int result = WriteRegistryValue(value->second, bufAddr, size);
	return result == 0 ? hleLogInfo(Log::sceReg, 0, "type=%d size=%d", (int)value->second.type, RegistryValueSize(value->second)) : hleLogError(Log::sceReg, result);
}

int sceRegSetKeyValue(int catHandle, const char *name, u32 bufAddr, u32 size) {
	if (!name || size > VSH_REGISTRY_MAX_VALUE_SIZE || !Memory::IsValidRange(bufAddr, size)) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_INVALID_NAME);
	}
	OpenCategory *opened = nullptr;
	RegistryCategory *category = CategoryFromHandle(catHandle, &opened);
	if (!category || !opened) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_CATEGORY_NOT_FOUND);
	}
	if (!CategoryIsWritable(*opened)) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_PERMISSION_FAILURE);
	}
	auto value = category->values.find(name);
	if (value == category->values.end() || value->second.type == ValueType::DIR || value->second.type == ValueType::FAIL) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_REGISTRY_NOT_FOUND, "key '%s' not found", name);
	}
	std::vector<u8> newData;
	if (value->second.type == ValueType::INT) {
		if (size < sizeof(u32)) {
			return hleLogError(Log::sceReg, SCE_REG_ERROR_INVALID_PATH, "integer value too small");
		}
		newData.resize(sizeof(u32));
		Memory::MemcpyUnchecked(newData.data(), bufAddr, sizeof(u32));
	} else {
		newData.resize(size);
		if (size != 0) {
			Memory::MemcpyUnchecked(newData.data(), bufAddr, size);
		}
	}
	if (value->second.data == newData) {
		return hleLogInfo(Log::sceReg, 0, "value unchanged");
	}
	std::string backingError;
	if (!ValidateRealRegistryValue(opened->path, name, value->second.type, newData, &backingError)) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_PERMISSION_FAILURE, "%s", backingError.c_str());
	}
	value->second.data = std::move(newData);
	g_registryDirty = true;
	g_registryDirtyValues.emplace(NormalizeRegistryPath(opened->path), name);
	ApplyRegistrySystemSettings();
	if (!PersistRegistry()) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_REGISTRY_NOT_FOUND, "persistence failed");
	}
	return hleLogInfo(Log::sceReg, 0, "updated %s/%s size=%u", opened->path.c_str(), name, size);
}

int sceRegCreateKey(int catHandle, const char *name, int type, u32 size) {
	if (!name || !name[0] || strlen(name) >= REG_KEYNAME_SIZE || size > VSH_REGISTRY_MAX_VALUE_SIZE ||
		type < (int)ValueType::DIR || type > (int)ValueType::BIN) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_INVALID_NAME);
	}
	if (g_registryUsesRealFiles) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_PERMISSION_FAILURE, "real DREG key allocation is immutable");
	}
	OpenCategory *opened = nullptr;
	RegistryCategory *category = CategoryFromHandle(catHandle, &opened);
	if (!category || !opened) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_CATEGORY_NOT_FOUND);
	}
	if (!CategoryIsWritable(*opened)) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_PERMISSION_FAILURE);
	}
	if (category->values.find(name) != category->values.end()) {
		return hleLogInfo(Log::sceReg, 0, "key already exists");
	}
	RegistryValue value;
	value.type = (ValueType)type;
	if (value.type == ValueType::INT) {
		value.data.assign(sizeof(u32), 0);
	} else if (value.type == ValueType::STR || value.type == ValueType::BIN) {
		value.data.assign(size, 0);
	}
	AddKeyInOrder(*category, name);
	category->values[name] = std::move(value);
	if ((ValueType)type == ValueType::DIR) {
		EnsureCategoryPath(ChildRegistryPath(opened->path, name));
	}
	g_registryDirty = true;
	return PersistRegistry() ? hleLogInfo(Log::sceReg, 0) : hleLogError(Log::sceReg, SCE_REG_ERROR_REGISTRY_NOT_FOUND);
}
// Speculated signature
int sceRegRemoveKey(int catHandle, int key) {
	if (g_registryUsesRealFiles) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_PERMISSION_FAILURE, "real DREG key allocation is immutable");
	}
	OpenCategory *opened = nullptr;
	RegistryCategory *category = CategoryFromHandle(catHandle, &opened);
	if (!category || !opened || key < 0 || key >= (int)category->order.size()) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_CATEGORY_NOT_FOUND);
	}
	if (!CategoryIsWritable(*opened)) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_PERMISSION_FAILURE);
	}
	const std::string name = category->order[key];
	auto value = category->values.find(name);
	if (value != category->values.end() && value->second.type == ValueType::DIR) {
		const std::string childPath = ChildRegistryPath(opened->path, name);
		for (auto it = g_registryCategories.begin(); it != g_registryCategories.end();) {
			if (it->first == childPath || startsWith(it->first, childPath + "/")) {
				it = g_registryCategories.erase(it);
			} else {
				++it;
			}
		}
	}
	category->values.erase(name);
	category->order.erase(category->order.begin() + key);
	g_registryDirty = true;
	return PersistRegistry() ? hleLogInfo(Log::sceReg, 0) : hleLogError(Log::sceReg, SCE_REG_ERROR_REGISTRY_NOT_FOUND);
}

int sceRegGetCategoryNumAtRoot(int regHandle, u32 numCategoriesPtr) {
	if (regHandle != 0) {
		return hleLogError(Log::sceReg, 0, "Not found");
	}

	RegistryCategory *root = LookupMutableCategory("/");
	if (!root) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_REGISTRY_NOT_FOUND);
	}

	if (!Memory::IsValid4AlignedAddress(numCategoriesPtr)) {
		return hleLogError(Log::sceReg, -1, "Invalid pointer");
	}

	Memory::WriteUnchecked_U32((u32)root->order.size(), numCategoriesPtr);
	return hleLogInfo(Log::sceReg, 0);
}

int sceRegGetCategoryListAtRoot(int regHandle, u32 bufPtr, int numCategories) {
	if (regHandle != 0) {
		return hleLogError(Log::sceReg, 0, "Not found");
	}

	if (numCategories < 0) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_INVALID_PATH);
	}
	RegistryCategory *root = LookupMutableCategory("/");
	if (!root) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_REGISTRY_NOT_FOUND);
	}
	if (numCategories > (int)root->order.size()) {
		WARN_LOG(Log::sceReg, "numCategories too large");
		numCategories = (int)root->order.size();
	}

	if (!Memory::IsValidRange(bufPtr, numCategories * 27)) {
		return hleLogError(Log::sceReg, -1, "bad output addr");
	}

	for (int i = 0; i < numCategories; i++) {
		char *dest = (char *)Memory::GetPointerWriteOrException(bufPtr + i * 27);
		if (dest) {
			memset(dest, 0, 27);
			strncpy(dest, root->order[i].c_str(), 26);
		}
	}

	return hleLogInfo(Log::sceReg, 0);
}

const HLEFunction sceReg[] = {
	{ 0x92E41280, &WrapI_UIU<sceRegOpenRegistry>, "sceRegOpenRegistry", 'i', "xix" },
	{ 0xFA8A5739, &WrapI_I<sceRegCloseRegistry>, "sceRegCloseRegistry", 'i', "i" },
	{ 0xDEDA92BF, &WrapI_U<sceRegRemoveRegistry>, "sceRegRemoveRegistry", 'i', "x" },
	{ 0x1D8A762E, &WrapI_ICIU<sceRegOpenCategory>, "sceRegOpenCategory", 'i', "isix" },
	{ 0x0CAE832B, &WrapI_I<sceRegCloseCategory>, "sceRegCloseCategory", 'i', "i" },
	{ 0x39461B4D, &WrapI_I<sceRegFlushRegistry>, "sceRegFlushRegistry", 'i', "i" },
	{ 0x0D69BF40, &WrapI_I<sceRegFlushCategory>, "sceRegFlushCategory", 'i', "i" },
	{ 0x57641A81, &WrapI_ICIU<sceRegCreateKey>, "sceRegCreateKey", 'i', "isix" },
	{ 0x17768E14, &WrapI_ICUU<sceRegSetKeyValue>, "sceRegSetKeyValue", 'i', "isxx" },
	{ 0xD4475AA8, &WrapI_ICUUU<sceRegGetKeyInfo>, "sceRegGetKeyInfo", 'i', "isxxx" },
	{ 0x28A8E98A, &WrapI_IIUU<sceRegGetKeyValue>, "sceRegGetKeyValue", 'i', "iixx" },
	{ 0x2C0DB9DD, &WrapI_IU<sceRegGetKeysNum>, "sceRegGetKeysNum", 'i', "ix" },
	{ 0x2D211135, &WrapI_IUI<sceRegGetKeys>, "sceRegGetKeys", 'i', "ipi" },
	{ 0xC5768D02, &WrapI_ICUU<sceRegGetKeyInfoByName>, "sceRegGetKeyInfoByName", 'i', "isxx" },
	{ 0x30BE0259, &WrapI_ICUU<sceRegGetKeyValueByName>, "sceRegGetKeyValueByName", 'i', "isxx" },
	{ 0x4CA16893, &WrapI_IC<sceRegRemoveCategory>, "sceRegRemoveCategory", 'i', "i" },
	{ 0x3615BC87, &WrapI_II<sceRegRemoveKey>, "sceRegRemoveKey", 'i', "ii" },
	{ 0x9B25EDF1, nullptr, "sceRegExit", 'i', "i" },
	{ 0xBE8C1263, &WrapI_IU<sceRegGetCategoryNumAtRoot>, "sceRegGetCategoryNumAtRoot", 'i', "ii" },
	{ 0x835ECE6F, &WrapI_IUI<sceRegGetCategoryListAtRoot>, "sceRegGetCategoryListAtRoot", 'i', "ipi" },
	// Firmware 6.60 aliases, corroborated against Jpcsp's sceReg table.
	{ 0xDBA46704, &WrapI_UIU<sceRegOpenRegistry>, "sceRegOpenRegistry", 'i', "xix" },
	{ 0x49D77D65, &WrapI_I<sceRegCloseRegistry>, "sceRegCloseRegistry", 'i', "i" },
	{ 0x4F471457, &WrapI_ICIU<sceRegOpenCategory>, "sceRegOpenCategory", 'i', "isix" },
	{ 0xFC742751, &WrapI_I<sceRegCloseCategory>, "sceRegCloseCategory", 'i', "i" },
	{ 0x5FD4764A, &WrapI_I<sceRegFlushRegistry>, "sceRegFlushRegistry", 'i', "i" },
	{ 0xD743A608, &WrapI_I<sceRegFlushCategory>, "sceRegFlushCategory", 'i', "i" },
	{ 0x3B6CA1E6, &WrapI_ICIU<sceRegCreateKey>, "sceRegCreateKey", 'i', "isix" },
	{ 0x49C70163, &WrapI_ICUU<sceRegSetKeyValue>, "sceRegSetKeyValue", 'i', "isxx" },
	{ 0x9980519F, &WrapI_ICUUU<sceRegGetKeyInfo>, "sceRegGetKeyInfo", 'i', "isxxx" },
	{ 0xF4A3E396, &WrapI_IIUU<sceRegGetKeyValue>, "sceRegGetKeyValue", 'i', "iixx" },
	{ 0x61DB9D06, &WrapI_IC<sceRegRemoveCategory>, "sceRegRemoveCategory", 'i', "i" },
	{ 0xF2619407, &WrapI_ICUU<sceRegGetKeyInfoByName>, "sceRegGetKeyInfoByName", 'i', "isxx" },
	{ 0x38415B9F, &WrapI_ICUU<sceRegGetKeyValueByName>, "sceRegGetKeyValueByName", 'i', "isxx" },
};

void Register_sceReg() {
	RegisterHLEModule("sceReg", ARRAY_SIZE(sceReg), sceReg);
	RegisterHLEModule("sceReg_driver", ARRAY_SIZE(sceReg), sceReg);
}

bool __RegGetValueForDebugger(std::string_view path, std::string_view name, int *type, std::vector<u8> *data) {
	RegistryCategory *category = LookupMutableCategory(path);
	if (!category) {
		return false;
	}
	auto value = category->values.find(std::string(name));
	if (value == category->values.end()) {
		return false;
	}
	if (type) {
		*type = (int)value->second.type;
	}
	if (data) {
		*data = value->second.data;
	}
	return true;
}

bool __RegGetInt(std::string_view path, std::string_view name, u32 *value) {
	EnsureRegistryLoaded();
	return ReadRegistryInt(path, name, value);
}

bool __RegGetString(std::string_view path, std::string_view name, std::string *value) {
	EnsureRegistryLoaded();
	return ReadRegistryString(path, name, value);
}

bool __RegSetInt(std::string_view path, std::string_view name, u32 value) {
	const std::vector<u8> data = {(u8)value, (u8)(value >> 8), (u8)(value >> 16), (u8)(value >> 24)};
	return __RegSetValueForDebugger(path, name, (int)ValueType::INT, data, nullptr);
}

bool __RegSetString(std::string_view path, std::string_view name, std::string_view value) {
	std::vector<u8> data(value.begin(), value.end());
	data.push_back(0);
	return __RegSetValueForDebugger(path, name, (int)ValueType::STR, data, nullptr);
}

bool __RegGetBinary(std::string_view path, std::string_view name, std::vector<u8> *value) {
	EnsureRegistryLoaded();
	auto category = g_registryCategories.find(NormalizeRegistryPath(path));
	if (category == g_registryCategories.end()) {
		return false;
	}
	auto entry = category->second.values.find(std::string(name));
	if (entry == category->second.values.end() || entry->second.type != ValueType::BIN) {
		return false;
	}
	*value = entry->second.data;
	return true;
}

bool __RegSetBinary(std::string_view path, std::string_view name, const std::vector<u8> &value) {
	return __RegSetValueForDebugger(path, name, (int)ValueType::BIN, value, nullptr);
}

bool __RegSetValueForDebugger(std::string_view path, std::string_view name, int type, const std::vector<u8> &data, std::string *errorString) {
	EnsureRegistryLoaded();
	if (__KernelIsRunningVSH() && VSHRouteForcesRealModule("sceRegistry_Service")) {
		if (errorString) {
			*errorString = "Sony registry.prx owns Direct VSH writes; use the real Settings UI";
		}
		return false;
	}
	if (!g_registryPersistent) {
		if (errorString) {
			*errorString = "Direct VSH persistent registry is not active";
		}
		return false;
	}
	if (name.empty() || name.size() >= REG_KEYNAME_SIZE || type < (int)ValueType::INT || type > (int)ValueType::BIN ||
		data.size() > VSH_REGISTRY_MAX_VALUE_SIZE || (type == (int)ValueType::INT && data.size() != sizeof(u32))) {
		if (errorString) {
			*errorString = "Invalid registry value";
		}
		return false;
	}
	const std::string normalizedPath = NormalizeRegistryPath(path);
	auto categoryIt = g_registryCategories.find(normalizedPath);
	if (categoryIt == g_registryCategories.end()) {
		if (errorString) {
			*errorString = "Registry category is not present in system.ireg";
		}
		return false;
	}
	RegistryCategory &category = categoryIt->second;
	auto valueIt = category.values.find(std::string(name));
	if (valueIt == category.values.end() || valueIt->second.type != (ValueType)type) {
		if (errorString) {
			*errorString = "Registry key/type is not present in system.dreg";
		}
		return false;
	}
	RegistryValue &value = valueIt->second;
	if (value.type == (ValueType)type && value.data == data) {
		if (errorString) {
			errorString->clear();
		}
		return true;
	}
	std::string backingError;
	if (!ValidateRealRegistryValue(normalizedPath, name, value.type, data, &backingError)) {
		if (errorString) {
			*errorString = backingError;
		}
		return false;
	}
	value.data = data;
	g_registryDirty = true;
	g_registryDirtyValues.emplace(normalizedPath, std::string(name));
	ApplyRegistrySystemSettings();
	if (!PersistRegistry()) {
		if (errorString) {
			*errorString = "Could not persist registry";
		}
		return false;
	}
	if (errorString) {
		errorString->clear();
	}
	return true;
}

bool __RegTestStoreRoundTrip(std::string *errorString) {
	if (!PSPRegistryFileTestRoundTrip(errorString)) {
		return false;
	}
	if (errorString) {
		errorString->clear();
	}
	return true;
#if 0
	const auto savedCategories = g_registryCategories;
	const bool savedLoaded = g_registryLoaded;
	const bool savedPersistent = g_registryPersistent;
	const bool savedDirty = g_registryDirty;
	const bool savedRecovered = g_registryRecovered;
	const u64 savedGeneration = g_registryGeneration;
	const std::string savedNickname = g_Config.sNickName;
	const int savedLanguage = g_Config.iLanguage;
	const int savedButtonPreference = g_Config.iButtonPreference;
	const int savedTimeFormat = g_Config.iTimeFormat;
	const int savedDateFormat = g_Config.iDateFormat;
	const int savedTimeZone = g_Config.iTimeZone;
	const bool savedDaylightSavings = g_Config.bDayLightSavings;
	const int savedParentalLevel = g_Config.iLockParentalLevel;
	const int savedAdhocChannel = g_Config.iWlanAdhocChannel;
	const bool savedWlanPowerSave = g_Config.bWlanPowerSave;

	auto restore = [&] {
		g_registryCategories = savedCategories;
		g_registryLoaded = savedLoaded;
		g_registryPersistent = savedPersistent;
		g_registryDirty = savedDirty;
		g_registryRecovered = savedRecovered;
		g_registryGeneration = savedGeneration;
		g_Config.sNickName = savedNickname;
		g_Config.iLanguage = savedLanguage;
		g_Config.iButtonPreference = savedButtonPreference;
		g_Config.iTimeFormat = savedTimeFormat;
		g_Config.iDateFormat = savedDateFormat;
		g_Config.iTimeZone = savedTimeZone;
		g_Config.bDayLightSavings = savedDaylightSavings;
		g_Config.iLockParentalLevel = savedParentalLevel;
		g_Config.iWlanAdhocChannel = savedAdhocChannel;
		g_Config.bWlanPowerSave = savedWlanPowerSave;
	};
	auto fail = [&](const char *message) {
		if (errorString) {
			*errorString = message;
		}
		restore();
		return false;
	};

	SeedFactoryRegistry(true);
	struct RequiredSetting {
		const char *path;
		const char *name;
		ValueType type;
	};
	constexpr RequiredSetting requiredSettings[] = {
		{"/CONFIG/SYSTEM", "owner_name", ValueType::STR},
		{"/CONFIG/SYSTEM", "backlight_brightness", ValueType::INT},
		{"/CONFIG/SYSTEM/XMB", "language", ValueType::INT},
		{"/CONFIG/SYSTEM/XMB", "button_assign", ValueType::INT},
		{"/CONFIG/SYSTEM/XMB", "theme_type", ValueType::INT},
		{"/CONFIG/SYSTEM/XMB/THEME", "wallpaper_mode", ValueType::INT},
		{"/CONFIG/SYSTEM/SOUND", "main_volume", ValueType::INT},
		{"/CONFIG/SYSTEM/SOUND", "avls", ValueType::INT},
		{"/CONFIG/SYSTEM/POWER_SAVING", "suspend_interval", ValueType::INT},
		{"/CONFIG/DATE", "time_format", ValueType::INT},
		{"/CONFIG/DATE", "date_format", ValueType::INT},
		{"/CONFIG/DATE", "time_zone_offset", ValueType::INT},
		{"/CONFIG/NETWORK/INFRASTRUCTURE", "latest_id", ValueType::INT},
		{"/CONFIG/ALARM", "alarm_0_time", ValueType::INT},
	};
	for (const auto &required : requiredSettings) {
		auto category = g_registryCategories.find(required.path);
		if (category == g_registryCategories.end()) {
			return fail("required settings category missing");
		}
		auto value = category->second.values.find(required.name);
		if (value == category->second.values.end() || value->second.type != required.type) {
			return fail("required setting missing or has wrong type");
		}
	}
	auto system = g_registryCategories.find("/CONFIG/SYSTEM");
	if (system == g_registryCategories.end()) {
		return fail("factory SYSTEM category missing");
	}
	auto owner = system->second.values.find("owner_name");
	if (owner == system->second.values.end() || owner->second.type != ValueType::STR ||
		owner->second.data != std::vector<u8>{'P', 'P', 'S', 'S', 'P', 'P', 0}) {
		return fail("factory owner_name is not deterministic");
	}
	PutRegistryString("/CONFIG/SYSTEM", "owner_name", "Milestone2");
	PutRegistryInt("/CONFIG/SYSTEM/XMB", "language", 9);
	PutRegistryInt("/CONFIG/SYSTEM/SOUND", "main_volume", 12);
	PutRegistryInt("/CONFIG/SYSTEM/SOUND", "avls", 1);
	PutRegistryInt("/CONFIG/SYSTEM/XMB/THEME", "wallpaper_mode", 1);
	PutRegistryInt("/CONFIG/SYSTEM/XMB", "button_assign", 0);
	PutRegistryInt("/CONFIG/DATE", "time_format", 1);
	PutRegistryInt("/CONFIG/DATE", "date_format", 1);
	PutRegistryInt("/CONFIG/DATE", "time_zone_offset", 180);
	PutRegistryInt("/CONFIG/DATE", "summer_time", 1);
	PutRegistryInt("/CONFIG/SYSTEM/LOCK", "parental_level", 5);
	PutRegistryInt("/CONFIG/NETWORK/ADHOC", "channel", 6);
	PutRegistryInt("/CONFIG/SYSTEM/POWER_SAVING", "wlan_mode", 1);
	g_registryPersistent = true;
	ApplyRegistrySystemSettings();
	if (g_Config.sNickName != "Milestone2" || g_Config.iLanguage != 9 || g_Config.iButtonPreference != 0 ||
		g_Config.iTimeFormat != 1 || g_Config.iDateFormat != 1 || g_Config.iTimeZone != 180 ||
		!g_Config.bDayLightSavings || g_Config.iLockParentalLevel != 5 || g_Config.iWlanAdhocChannel != 6 ||
		!g_Config.bWlanPowerSave) {
		return fail("registry settings were not applied to PSP system parameters");
	}
	EnsureCategoryPath("/CONFIG/NETWORK/INFRASTRUCTURE/42");
	PutRegistryString("/CONFIG/NETWORK/INFRASTRUCTURE/42", "cnf_name", "RoundTrip");

	const std::vector<u8> serialized = SerializeRegistry(7);
	std::map<std::string, RegistryCategory> loaded;
	u64 generation = 0;
	const std::string serializedString((const char *)serialized.data(), serialized.size());
	if (!DeserializeRegistry(serializedString, &loaded, &generation) || generation != 7) {
		return fail("valid registry failed to deserialize");
	}
	auto loadedSystem = loaded.find("/CONFIG/SYSTEM");
	if (loadedSystem == loaded.end() || loadedSystem->second.values["owner_name"].data !=
		std::vector<u8>{'M', 'i', 'l', 'e', 's', 't', 'o', 'n', 'e', '2', 0}) {
		return fail("string value did not round-trip");
	}
	if (loaded.find("/CONFIG/NETWORK/INFRASTRUCTURE/42") == loaded.end()) {
		return fail("created category did not round-trip");
	}
	std::string corrupt = serializedString;
	corrupt.back() ^= 0x80;
	if (DeserializeRegistry(corrupt, &loaded, &generation)) {
		return fail("corrupt registry passed checksum validation");
	}

	if (errorString) {
		errorString->clear();
	}
	restore();
	return true;
#endif
}
