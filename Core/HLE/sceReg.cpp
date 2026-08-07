#include <map>
#include <algorithm>
#include "Core/HLE/sceReg.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/ErrorCodes.h"
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

// Updater checks for CONFIG/SYSTEM/XMB.

// not sure what modes exist, this is conjecture.
enum RegOpenMode {
	REG_OPEN_READONLY = 2,
};

void __RegInit() {
	g_openRegistryMode = 0;
	g_handleGen = 1337;
	g_openCategories.clear();
}

void __RegShutdown() {
	g_openCategories.clear();
}

static const KeyValue *LookupCategory(std::string_view path, int *count) {
	path = StripPrefix("/", path);
	std::vector<std::string_view> parts;
	SplitString(path, '/', parts);

	const KeyValue *curDir = ROOT;
	int curCount = ARRAY_SIZE(ROOT);

	for (const auto part : parts) {
		bool found = false;
		for (int i = 0; i < curCount; i++) {
			if (equals(curDir[i].name, part)) {
				// Found the subdir.
				if (curDir[i].type == ValueType::DIR) {
					// Must update curCount before curDir, of course (since that line accesses it).
					curCount = curDir[i].intValue;
					curDir = curDir[i].dirContents;
					found = true;
					break;
				} else {
					ERROR_LOG(Log::sceReg, "Not a dir");
					return nullptr;
				}
			}
		}
		if (!found) {
			WARN_LOG(Log::sceReg, "LookupCategory: Path not found: %.*s", (int)path.size(), path.data());
			return nullptr;
		}
	}

	*count = curCount;
	return curDir;
}

void __RegDoState(PointerWrap &p) {
	auto s = p.Section("sceReg", 0, 1);
	if (!s)
		return;
	Do(p, g_openRegistryMode);
	Do(p, g_openCategories);
}

// Registry level (it seems only /system can exist, so kinda pointless)
int sceRegOpenRegistry(u32 regParamAddr, int mode, u32 regHandleAddr) {
	// There's only one registry and its handle is 0.
	if (Memory::IsValid4AlignedAddress(regHandleAddr)) {
		Memory::WriteUnchecked_U32(0, regHandleAddr);
	}
	g_openRegistryMode = mode;

	if (g_openRegistryMode != REG_OPEN_READONLY) {
		WARN_LOG(Log::HLE, "sceRegOpenRegistry: Opening registry in non-readonly mode. This is not yet supported (we'll simply emulate it as read-only anyway).");
	}

	return hleLogInfo(Log::sceReg, 0);
}

int sceRegCloseRegistry(int regHandle) {
	if (regHandle != 0) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_REGISTRY_NOT_FOUND);
	}
	g_openCategories.clear();
	return hleLogInfo(Log::sceReg, 0);
}

int sceRegFlushRegistry(int regHandle) {
	if (regHandle != 0) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_REGISTRY_NOT_FOUND);
	}
	// For us this is a no-op.
	return hleLogInfo(Log::sceReg, 0);
}

// Seems dangerous! Have not dared to test this on hardware.
int sceRegRemoveRegistry(u32 regParamAddr) {
	return hleLogError(Log::sceReg, 0, "UNIMPL");
}

int sceRegOpenCategory(int regHandle, const char *name, int mode, u32 regHandleAddr) {
	if (!Memory::IsValid4AlignedAddress(regHandleAddr)) {
		return -1;
	}
	if (!name) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_INVALID_NAME);
	}
	if (regHandle != 0) {
		Memory::WriteUnchecked_U32(-1, regHandleAddr);
		return hleLogError(Log::sceReg, SCE_REG_ERROR_REGISTRY_NOT_FOUND, "Invalid registry");
	}

	if (equals(name, "") || equals(name, "/")) {
		return hleLogError(Log::sceReg, SCE_REG_ERROR_INVALID_PATH);
	}

	int count = 0;
	const KeyValue *keyvals = LookupCategory(name, &count);
	if (!keyvals) {
		Memory::WriteUnchecked_U32(-1, regHandleAddr);
		return hleLogError(Log::sceReg, SCE_REG_ERROR_CATEGORY_NOT_FOUND);
	}

	// Let's see if this category is marked as inaccessible (presumably from user mode)..
	if (count == 1 && keyvals[0].type == ValueType::FAIL) {
		const int errorCode = keyvals[0].intValue;
		return hleLogWarning(Log::sceReg, errorCode, "Inaccessible category");
	}

	int handle = g_handleGen++;
	OpenCategory cat{ name, mode };
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
	return hleLogError(Log::sceReg, 0, "UNIMPL");
}

int sceRegFlushCategory(int regHandle) {
	return hleLogError(Log::sceReg, 0, "UNIMPL");
}

// Key level

int sceRegGetKeysNum(int catHandle, u32 numAddr) {
	auto iter = g_openCategories.find(catHandle);
	if (iter == g_openCategories.end()) {
		return hleLogError(Log::sceReg, 0, "Not an open category");
	}

	if (!Memory::IsValid4AlignedAddress(numAddr)) {
		return -1;
	}

	int count = 0;
	const KeyValue *keyvals = LookupCategory(iter->second.path, &count);
	if (!keyvals) {
		Memory::WriteUnchecked_U32(-1, numAddr);
		return hleLogWarning(Log::sceReg, SCE_REG_ERROR_CATEGORY_NOT_FOUND);
	}

	Memory::WriteUnchecked_U32(count, numAddr);
	return hleLogInfo(Log::sceReg, 0);
}

int sceRegGetKeys(int catHandle, u32 bufAddr, int num) {
	auto iter = g_openCategories.find(catHandle);
	if (iter == g_openCategories.end()) {
		return hleLogError(Log::sceReg, 0, "Not an open category");
	}

	const int keyLen = 27; // 27 bytes per key name, including null terminator. For some reason?!?

	if (!Memory::IsValidRange(bufAddr, num * keyLen)) {
		return hleLogError(Log::sceReg, -1, "bad output addr");
	}

	int count = 0;
	const KeyValue *keyvals = LookupCategory(iter->second.path, &count);
	if (!keyvals) {
		return hleLogWarning(Log::sceReg, SCE_REG_ERROR_CATEGORY_NOT_FOUND);
	}

	count = std::min(count, num);

	for (int i = 0; i < num; i++) {
		char *dest = (char *)Memory::GetPointerWrite(bufAddr + i * keyLen);
		strncpy(dest, keyvals[i].name.c_str(), keyLen);
	}

	return hleLogInfo(Log::sceReg, 0);
}

int sceRegGetKeyInfo(int catHandle, const char *name, u32 outKeyHandleAddr, u32 outTypeAddr, u32 outSizeAddr) {
	if (!name) {
		return hleLogError(Log::sceReg, -1, "Invalid name pointer");
	}

	auto iter = g_openCategories.find(catHandle);
	if (iter == g_openCategories.end()) {
		return hleLogError(Log::sceReg, 0, "Not found");
	}

	int count = 0;
	const KeyValue *keyvals = LookupCategory(iter->second.path, &count);
	if (!keyvals) {
		return hleLogWarning(Log::sceReg, SCE_REG_ERROR_CATEGORY_NOT_FOUND);
	}

	for (int i = 0; i < count; i++) {
		if (equals(keyvals[i].name, name)) {
			// Found it!
			if (Memory::IsValid4AlignedAddress(outKeyHandleAddr)) {
				// Let's just make the index the key handle.
				Memory::WriteUnchecked_U32(i, outKeyHandleAddr);
			}
			if (Memory::IsValid4AlignedAddress(outTypeAddr)) {
				// Let's just make the index the key handle.
				Memory::WriteUnchecked_U32((int)keyvals[i].type, outTypeAddr);
			}
			int size = 0;
			if (Memory::IsValid4AlignedAddress(outSizeAddr)) {
				switch (keyvals[i].type) {
				case ValueType::BIN: size = (int)keyvals[i].intValue; break;
				case ValueType::STR: size = (int)keyvals[i].intValue; break;
				case ValueType::DIR: size = 0; break;
				case ValueType::INT: size = 4; break;
				default: break;
				}
				// Let's just make the index the key handle.
				Memory::WriteUnchecked_U32(size, outSizeAddr);
			}
			return hleLogInfo(Log::sceReg, 0, "handle: %d type: %d size: %d", i, (int)keyvals[i].type, size);
		}
	}

	return hleLogWarning(Log::sceReg, -1, "key with name '%s' not found", name);
}

int sceRegGetKeyInfoByName(int catHandle, const char *name, u32 typeAddr, u32 sizeAddr) {
	if (!name) {
		return hleLogError(Log::sceReg, -1, "Invalid name pointer");
	}

	auto iter = g_openCategories.find(catHandle);
	if (iter == g_openCategories.end()) {
		return hleLogError(Log::sceReg, 0, "Not an open category");
	}

	int count = 0;
	const KeyValue *keyvals = LookupCategory(iter->second.path, &count);
	if (!keyvals) {
		return hleLogWarning(Log::sceReg, SCE_REG_ERROR_CATEGORY_NOT_FOUND);
	}

	for (int i = 0; i < count; i++) {
		if (equals(keyvals[i].name, name)) {
			int size = 0;
			if (Memory::IsValid4AlignedAddress(typeAddr)) {
				Memory::WriteUnchecked_U32((int)keyvals[i].type, typeAddr);
			}
			if (Memory::IsValid4AlignedAddress(sizeAddr)) {
				switch (keyvals[i].type) {
				case ValueType::BIN: size = (int)keyvals[i].intValue; break;
				case ValueType::STR: size = (int)keyvals[i].intValue; break;
				case ValueType::DIR: size = 0; break;
				case ValueType::INT: size = 4; break;
				default: break;
				}
				Memory::WriteUnchecked_U32(size, sizeAddr);
			}
			return hleLogInfo(Log::sceReg, 0, "type: %d size: %d", (int)keyvals[i].type, size);
		}
	}

	return hleLogWarning(Log::sceReg, -1, "key with name '%s' not found", name);
}

int sceRegGetKeyValue(int catHandle, int keyHandle, u32 bufAddr, u32 size) {
	if (!Memory::IsValidRange(bufAddr, size)) {
		return -1;
	}

	auto iter = g_openCategories.find(catHandle);
	if (iter == g_openCategories.end()) {
		return hleLogError(Log::sceReg, 0, "Not found");
	}

	int count = 0;
	const KeyValue *keyvals = LookupCategory(iter->second.path, &count);
	if (!keyvals) {
		return hleLogWarning(Log::sceReg, SCE_REG_ERROR_CATEGORY_NOT_FOUND);
	}

	if (keyHandle < 0 || keyHandle >= count) {
		return hleLogWarning(Log::sceReg, SCE_REG_ERROR_CATEGORY_NOT_FOUND);
	}

	const KeyValue &keyval = keyvals[keyHandle];
	switch (keyval.type) {
	case ValueType::BIN:
		Memory::MemcpyUnchecked(bufAddr, keyval.strValue, std::min(size, (u32)keyval.intValue));
		return hleLogInfo(Log::sceReg, 0);
	case ValueType::STR:
		Memory::MemcpyUnchecked(bufAddr, keyval.strValue, std::min(size, (u32)keyval.intValue));
		return hleLogInfo(Log::sceReg, 0, "value: '%s'", keyval.strValue);
	case ValueType::INT:
		Memory::WriteUnchecked_U32(keyval.intValue, bufAddr);
		return hleLogInfo(Log::sceReg, 0, "value: %d (0x%08x)", keyval.intValue, keyval.intValue);
	case ValueType::DIR:
	case ValueType::FAIL:
	default:
		// Return an error?
		return hleLogWarning(Log::sceReg, 0, "Unexpected type for sceRegGetKeyValue");
	}
}

int sceRegGetKeyValueByName(int catHandle, const char *name, u32 bufAddr, u32 size) {
	if (!name) {
		return hleLogError(Log::sceReg, -1, "Invalid name pointer");
	}
	if (!Memory::IsValidRange(bufAddr, size)) {
		return -1;
	}

	auto iter = g_openCategories.find(catHandle);
	if (iter == g_openCategories.end()) {
		return hleLogError(Log::sceReg, 0, "Not found");
	}

	int count = 0;
	const KeyValue *keyvals = LookupCategory(iter->second.path, &count);
	if (!keyvals) {
		return hleLogWarning(Log::sceReg, SCE_REG_ERROR_CATEGORY_NOT_FOUND);
	}

	for (int i = 0; i < count; i++) {
		if (!equals(keyvals[i].name, name))
			continue;

		const KeyValue &keyval = keyvals[i];
		switch (keyval.type) {
		case ValueType::BIN:
			Memory::MemcpyUnchecked(bufAddr, keyval.strValue, std::min(size, (u32)keyval.intValue));
			return hleLogInfo(Log::sceReg, 0);
		case ValueType::STR:
			Memory::MemcpyUnchecked(bufAddr, keyval.strValue, std::min(size, (u32)keyval.intValue));
			return hleLogInfo(Log::sceReg, 0, "value: '%s'", keyval.strValue);
		case ValueType::INT:
			if (size >= sizeof(u32))
				Memory::WriteUnchecked_U32(keyval.intValue, bufAddr);
			return hleLogInfo(Log::sceReg, 0, "value: %d (0x%08x)", keyval.intValue, keyval.intValue);
		case ValueType::DIR:
		case ValueType::FAIL:
		default:
			return hleLogWarning(Log::sceReg, 0, "Unexpected type for sceRegGetKeyValueByName");
		}
	}

	return hleLogWarning(Log::sceReg, -1, "key with name '%s' not found", name);
}

int sceRegSetKeyValue(int catHandle, const char *name, u32 bufAddr, u32 size) {
	return hleLogError(Log::sceReg, 0);
}

int sceRegCreateKey(int catHandle, const char *name, int type, u32 size) {
	return hleLogError(Log::sceReg, 0);
}
// Speculated signature
int sceRegRemoveKey(int catHandle, int key) {
	return hleLogError(Log::sceReg, 0);
}

int sceRegGetCategoryNumAtRoot(int regHandle, u32 numCategoriesPtr) {
	if (regHandle != 0) {
		return hleLogError(Log::sceReg, 0, "Not found");
	}

	constexpr int numCategories = ARRAY_SIZE(ROOT);
	static_assert(numCategories == 4);

	if (!Memory::IsValid4AlignedAddress(numCategoriesPtr)) {
		return hleLogError(Log::sceReg, -1, "Invalid pointer");
	}

	Memory::WriteUnchecked_U32(numCategories, numCategoriesPtr);
	return hleLogInfo(Log::sceReg, 0);
}

int sceRegGetCategoryListAtRoot(int regHandle, u32 bufPtr, int numCategories) {
	if (regHandle != 0) {
		return hleLogError(Log::sceReg, 0, "Not found");
	}

	if (numCategories > ARRAY_SIZE(ROOT)) {
		WARN_LOG(Log::sceReg, "numCategories too large");
		numCategories = ARRAY_SIZE(ROOT);
	}

	if (!Memory::IsValidRange(bufPtr, numCategories * 27)) {
		return hleLogError(Log::sceReg, -1, "bad output addr");
	}

	for (int i = 0; i < numCategories; i++) {
		const KeyValue &kv = ROOT[i];
		_dbg_assert_msg_(kv.type == ValueType::DIR, "Unexpected non-dir in ROOT");
		char *dest = (char *)Memory::GetPointerWrite(bufPtr + i * 27);
		if (dest) {
			strncpy(dest, kv.name.c_str(), 27);
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
};

void Register_sceReg() {
	RegisterHLEModule("sceReg", ARRAY_SIZE(sceReg), sceReg);
}
