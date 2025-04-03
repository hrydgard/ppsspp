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

struct RegParam
{
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
	std::string strValue;  // also can be used for bin
	int intValue;
	const KeyValue *dirContents;  // intValue is the count.
};


// Dump of the PSP registry using tests/misc/reg.prx in pspautotests

// Dump of /DATA/FONT/PROPERTY/INFO0
static const KeyValue tree_DATA_FONT_PROPERTY_INFO0[] = {
	{ "h_size", ValueType::INT, "", (int)0x00000288 },
	{ "v_size", ValueType::INT, "", (int)0x00000288 },
	{ "h_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "v_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "extra_attributes", ValueType::INT, "", (int)0x00000000 },
	{ "weight", ValueType::INT, "", (int)0x00000000 },
	{ "family_code", ValueType::INT, "", (int)0x00000001 },
	{ "style", ValueType::INT, "", (int)0x00000067 },
	{ "sub_style", ValueType::INT, "", (int)0x00000000 },
	{ "language_code", ValueType::INT, "", (int)0x00000001 },
	{ "region_code", ValueType::INT, "", (int)0x00000000 },
	{ "country_code", ValueType::INT, "", (int)0x00000001 },
	{ "font_name", ValueType::STR, "FTT-NewRodin Pro DB" },
	{ "file_name", ValueType::STR, "jpn0.pgf" },
	{ "expire_date", ValueType::INT, "", (int)0x00000000 },
	{ "shadow_option", ValueType::INT, "", (int)0x00000000 },
};

// Dump of /DATA/FONT/PROPERTY/INFO1
static const KeyValue tree_DATA_FONT_PROPERTY_INFO1[] = {
	{ "h_size", ValueType::INT, "", (int)0x00000288 },
	{ "v_size", ValueType::INT, "", (int)0x00000288 },
	{ "h_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "v_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "extra_attributes", ValueType::INT, "", (int)0x00000000 },
	{ "weight", ValueType::INT, "", (int)0x00000000 },
	{ "family_code", ValueType::INT, "", (int)0x00000001 },
	{ "style", ValueType::INT, "", (int)0x00000001 },
	{ "sub_style", ValueType::INT, "", (int)0x00000000 },
	{ "language_code", ValueType::INT, "", (int)0x00000002 },
	{ "region_code", ValueType::INT, "", (int)0x00000000 },
	{ "country_code", ValueType::INT, "", (int)0x00000001 },
	{ "font_name", ValueType::STR, "FTT-NewRodin Pro Latin" },
	{ "file_name", ValueType::STR, "ltn0.pgf" },
	{ "expire_date", ValueType::INT, "", (int)0x00000000 },
	{ "shadow_option", ValueType::INT, "", (int)0x00000000 },
};

// Dump of /DATA/FONT/PROPERTY/INFO2
static const KeyValue tree_DATA_FONT_PROPERTY_INFO2[] = {
	{ "h_size", ValueType::INT, "", (int)0x00000288 },
	{ "v_size", ValueType::INT, "", (int)0x00000288 },
	{ "h_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "v_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "extra_attributes", ValueType::INT, "", (int)0x00000000 },
	{ "weight", ValueType::INT, "", (int)0x00000000 },
	{ "family_code", ValueType::INT, "", (int)0x00000002 },
	{ "style", ValueType::INT, "", (int)0x00000001 },
	{ "sub_style", ValueType::INT, "", (int)0x00000000 },
	{ "language_code", ValueType::INT, "", (int)0x00000002 },
	{ "region_code", ValueType::INT, "", (int)0x00000000 },
	{ "country_code", ValueType::INT, "", (int)0x00000001 },
	{ "font_name", ValueType::STR, "FTT-Matisse Pro Latin" },
	{ "file_name", ValueType::STR, "ltn1.pgf" },
	{ "expire_date", ValueType::INT, "", (int)0x00000000 },
	{ "shadow_option", ValueType::INT, "", (int)0x00000000 },
};

// Dump of /DATA/FONT/PROPERTY/INFO3
static const KeyValue tree_DATA_FONT_PROPERTY_INFO3[] = {
	{ "h_size", ValueType::INT, "", (int)0x00000288 },
	{ "v_size", ValueType::INT, "", (int)0x00000288 },
	{ "h_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "v_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "extra_attributes", ValueType::INT, "", (int)0x00000000 },
	{ "weight", ValueType::INT, "", (int)0x00000000 },
	{ "family_code", ValueType::INT, "", (int)0x00000001 },
	{ "style", ValueType::INT, "", (int)0x00000002 },
	{ "sub_style", ValueType::INT, "", (int)0x00000000 },
	{ "language_code", ValueType::INT, "", (int)0x00000002 },
	{ "region_code", ValueType::INT, "", (int)0x00000000 },
	{ "country_code", ValueType::INT, "", (int)0x00000001 },
	{ "font_name", ValueType::STR, "FTT-NewRodin Pro Latin" },
	{ "file_name", ValueType::STR, "ltn2.pgf" },
	{ "expire_date", ValueType::INT, "", (int)0x00000000 },
	{ "shadow_option", ValueType::INT, "", (int)0x00000000 },
};

// Dump of /DATA/FONT/PROPERTY/INFO4
static const KeyValue tree_DATA_FONT_PROPERTY_INFO4[] = {
	{ "h_size", ValueType::INT, "", (int)0x00000288 },
	{ "v_size", ValueType::INT, "", (int)0x00000288 },
	{ "h_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "v_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "extra_attributes", ValueType::INT, "", (int)0x00000000 },
	{ "weight", ValueType::INT, "", (int)0x00000000 },
	{ "family_code", ValueType::INT, "", (int)0x00000002 },
	{ "style", ValueType::INT, "", (int)0x00000002 },
	{ "sub_style", ValueType::INT, "", (int)0x00000000 },
	{ "language_code", ValueType::INT, "", (int)0x00000002 },
	{ "region_code", ValueType::INT, "", (int)0x00000000 },
	{ "country_code", ValueType::INT, "", (int)0x00000001 },
	{ "font_name", ValueType::STR, "FTT-Matisse Pro Latin" },
	{ "file_name", ValueType::STR, "ltn3.pgf" },
	{ "expire_date", ValueType::INT, "", (int)0x00000000 },
	{ "shadow_option", ValueType::INT, "", (int)0x00000000 },
};

// Dump of /DATA/FONT/PROPERTY/INFO5
static const KeyValue tree_DATA_FONT_PROPERTY_INFO5[] = {
	{ "h_size", ValueType::INT, "", (int)0x00000288 },
	{ "v_size", ValueType::INT, "", (int)0x00000288 },
	{ "h_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "v_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "extra_attributes", ValueType::INT, "", (int)0x00000000 },
	{ "weight", ValueType::INT, "", (int)0x00000000 },
	{ "family_code", ValueType::INT, "", (int)0x00000001 },
	{ "style", ValueType::INT, "", (int)0x00000005 },
	{ "sub_style", ValueType::INT, "", (int)0x00000000 },
	{ "language_code", ValueType::INT, "", (int)0x00000002 },
	{ "region_code", ValueType::INT, "", (int)0x00000000 },
	{ "country_code", ValueType::INT, "", (int)0x00000001 },
	{ "font_name", ValueType::STR, "FTT-NewRodin Pro Latin" },
	{ "file_name", ValueType::STR, "ltn4.pgf" },
	{ "expire_date", ValueType::INT, "", (int)0x00000000 },
	{ "shadow_option", ValueType::INT, "", (int)0x00000000 },
};

// Dump of /DATA/FONT/PROPERTY/INFO6
static const KeyValue tree_DATA_FONT_PROPERTY_INFO6[] = {
	{ "h_size", ValueType::INT, "", (int)0x00000288 },
	{ "v_size", ValueType::INT, "", (int)0x00000288 },
	{ "h_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "v_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "extra_attributes", ValueType::INT, "", (int)0x00000000 },
	{ "weight", ValueType::INT, "", (int)0x00000000 },
	{ "family_code", ValueType::INT, "", (int)0x00000002 },
	{ "style", ValueType::INT, "", (int)0x00000005 },
	{ "sub_style", ValueType::INT, "", (int)0x00000000 },
	{ "language_code", ValueType::INT, "", (int)0x00000002 },
	{ "region_code", ValueType::INT, "", (int)0x00000000 },
	{ "country_code", ValueType::INT, "", (int)0x00000001 },
	{ "font_name", ValueType::STR, "FTT-Matisse Pro Latin" },
	{ "file_name", ValueType::STR, "ltn5.pgf" },
	{ "expire_date", ValueType::INT, "", (int)0x00000000 },
	{ "shadow_option", ValueType::INT, "", (int)0x00000000 },
};

// Dump of /DATA/FONT/PROPERTY/INFO7
static const KeyValue tree_DATA_FONT_PROPERTY_INFO7[] = {
	{ "h_size", ValueType::INT, "", (int)0x00000288 },
	{ "v_size", ValueType::INT, "", (int)0x00000288 },
	{ "h_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "v_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "extra_attributes", ValueType::INT, "", (int)0x00000000 },
	{ "weight", ValueType::INT, "", (int)0x00000000 },
	{ "family_code", ValueType::INT, "", (int)0x00000001 },
	{ "style", ValueType::INT, "", (int)0x00000006 },
	{ "sub_style", ValueType::INT, "", (int)0x00000000 },
	{ "language_code", ValueType::INT, "", (int)0x00000002 },
	{ "region_code", ValueType::INT, "", (int)0x00000000 },
	{ "country_code", ValueType::INT, "", (int)0x00000001 },
	{ "font_name", ValueType::STR, "FTT-NewRodin Pro Latin" },
	{ "file_name", ValueType::STR, "ltn6.pgf" },
	{ "expire_date", ValueType::INT, "", (int)0x00000000 },
	{ "shadow_option", ValueType::INT, "", (int)0x00000000 },
};

// Dump of /DATA/FONT/PROPERTY/INFO8
static const KeyValue tree_DATA_FONT_PROPERTY_INFO8[] = {
	{ "h_size", ValueType::INT, "", (int)0x00000288 },
	{ "v_size", ValueType::INT, "", (int)0x00000288 },
	{ "h_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "v_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "extra_attributes", ValueType::INT, "", (int)0x00000000 },
	{ "weight", ValueType::INT, "", (int)0x00000000 },
	{ "family_code", ValueType::INT, "", (int)0x00000002 },
	{ "style", ValueType::INT, "", (int)0x00000006 },
	{ "sub_style", ValueType::INT, "", (int)0x00000000 },
	{ "language_code", ValueType::INT, "", (int)0x00000002 },
	{ "region_code", ValueType::INT, "", (int)0x00000000 },
	{ "country_code", ValueType::INT, "", (int)0x00000001 },
	{ "font_name", ValueType::STR, "FTT-Matisse Pro Latin" },
	{ "file_name", ValueType::STR, "ltn7.pgf" },
	{ "expire_date", ValueType::INT, "", (int)0x00000000 },
	{ "shadow_option", ValueType::INT, "", (int)0x00000000 },
};

// Dump of /DATA/FONT/PROPERTY/INFO9
static const KeyValue tree_DATA_FONT_PROPERTY_INFO9[] = {
	{ "h_size", ValueType::INT, "", (int)0x000001c0 },
	{ "v_size", ValueType::INT, "", (int)0x000001c0 },
	{ "h_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "v_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "extra_attributes", ValueType::INT, "", (int)0x00000000 },
	{ "weight", ValueType::INT, "", (int)0x00000000 },
	{ "family_code", ValueType::INT, "", (int)0x00000001 },
	{ "style", ValueType::INT, "", (int)0x00000001 },
	{ "sub_style", ValueType::INT, "", (int)0x00000000 },
	{ "language_code", ValueType::INT, "", (int)0x00000002 },
	{ "region_code", ValueType::INT, "", (int)0x00000000 },
	{ "country_code", ValueType::INT, "", (int)0x00000001 },
	{ "font_name", ValueType::STR, "FTT-NewRodin Pro Latin" },
	{ "file_name", ValueType::STR, "ltn8.pgf" },
	{ "expire_date", ValueType::INT, "", (int)0x00000000 },
	{ "shadow_option", ValueType::INT, "", (int)0x00000000 },
};

// Dump of /DATA/FONT/PROPERTY/INFO10
static const KeyValue tree_DATA_FONT_PROPERTY_INFO10[] = {
	{ "h_size", ValueType::INT, "", (int)0x000001c0 },
	{ "v_size", ValueType::INT, "", (int)0x000001c0 },
	{ "h_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "v_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "extra_attributes", ValueType::INT, "", (int)0x00000000 },
	{ "weight", ValueType::INT, "", (int)0x00000000 },
	{ "family_code", ValueType::INT, "", (int)0x00000002 },
	{ "style", ValueType::INT, "", (int)0x00000001 },
	{ "sub_style", ValueType::INT, "", (int)0x00000000 },
	{ "language_code", ValueType::INT, "", (int)0x00000002 },
	{ "region_code", ValueType::INT, "", (int)0x00000000 },
	{ "country_code", ValueType::INT, "", (int)0x00000001 },
	{ "font_name", ValueType::STR, "FTT-Matisse Pro Latin" },
	{ "file_name", ValueType::STR, "ltn9.pgf" },
	{ "expire_date", ValueType::INT, "", (int)0x00000000 },
	{ "shadow_option", ValueType::INT, "", (int)0x00000000 },
};

// Dump of /DATA/FONT/PROPERTY/INFO11
static const KeyValue tree_DATA_FONT_PROPERTY_INFO11[] = {
	{ "h_size", ValueType::INT, "", (int)0x000001c0 },
	{ "v_size", ValueType::INT, "", (int)0x000001c0 },
	{ "h_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "v_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "extra_attributes", ValueType::INT, "", (int)0x00000000 },
	{ "weight", ValueType::INT, "", (int)0x00000000 },
	{ "family_code", ValueType::INT, "", (int)0x00000001 },
	{ "style", ValueType::INT, "", (int)0x00000002 },
	{ "sub_style", ValueType::INT, "", (int)0x00000000 },
	{ "language_code", ValueType::INT, "", (int)0x00000002 },
	{ "region_code", ValueType::INT, "", (int)0x00000000 },
	{ "country_code", ValueType::INT, "", (int)0x00000001 },
	{ "font_name", ValueType::STR, "FTT-NewRodin Pro Latin" },
	{ "file_name", ValueType::STR, "ltn10.pgf" },
	{ "expire_date", ValueType::INT, "", (int)0x00000000 },
	{ "shadow_option", ValueType::INT, "", (int)0x00000000 },
};

// Dump of /DATA/FONT/PROPERTY/INFO12
static const KeyValue tree_DATA_FONT_PROPERTY_INFO12[] = {
	{ "h_size", ValueType::INT, "", (int)0x000001c0 },
	{ "v_size", ValueType::INT, "", (int)0x000001c0 },
	{ "h_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "v_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "extra_attributes", ValueType::INT, "", (int)0x00000000 },
	{ "weight", ValueType::INT, "", (int)0x00000000 },
	{ "family_code", ValueType::INT, "", (int)0x00000002 },
	{ "style", ValueType::INT, "", (int)0x00000002 },
	{ "sub_style", ValueType::INT, "", (int)0x00000000 },
	{ "language_code", ValueType::INT, "", (int)0x00000002 },
	{ "region_code", ValueType::INT, "", (int)0x00000000 },
	{ "country_code", ValueType::INT, "", (int)0x00000001 },
	{ "font_name", ValueType::STR, "FTT-Matisse Pro Latin" },
	{ "file_name", ValueType::STR, "ltn11.pgf" },
	{ "expire_date", ValueType::INT, "", (int)0x00000000 },
	{ "shadow_option", ValueType::INT, "", (int)0x00000000 },
};

// Dump of /DATA/FONT/PROPERTY/INFO13
static const KeyValue tree_DATA_FONT_PROPERTY_INFO13[] = {
	{ "h_size", ValueType::INT, "", (int)0x000001c0 },
	{ "v_size", ValueType::INT, "", (int)0x000001c0 },
	{ "h_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "v_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "extra_attributes", ValueType::INT, "", (int)0x00000000 },
	{ "weight", ValueType::INT, "", (int)0x00000000 },
	{ "family_code", ValueType::INT, "", (int)0x00000001 },
	{ "style", ValueType::INT, "", (int)0x00000005 },
	{ "sub_style", ValueType::INT, "", (int)0x00000000 },
	{ "language_code", ValueType::INT, "", (int)0x00000002 },
	{ "region_code", ValueType::INT, "", (int)0x00000000 },
	{ "country_code", ValueType::INT, "", (int)0x00000001 },
	{ "font_name", ValueType::STR, "FTT-NewRodin Pro Latin" },
	{ "file_name", ValueType::STR, "ltn12.pgf" },
	{ "expire_date", ValueType::INT, "", (int)0x00000000 },
	{ "shadow_option", ValueType::INT, "", (int)0x00000000 },
};

// Dump of /DATA/FONT/PROPERTY/INFO14
static const KeyValue tree_DATA_FONT_PROPERTY_INFO14[] = {
	{ "h_size", ValueType::INT, "", (int)0x000001c0 },
	{ "v_size", ValueType::INT, "", (int)0x000001c0 },
	{ "h_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "v_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "extra_attributes", ValueType::INT, "", (int)0x00000000 },
	{ "weight", ValueType::INT, "", (int)0x00000000 },
	{ "family_code", ValueType::INT, "", (int)0x00000002 },
	{ "style", ValueType::INT, "", (int)0x00000005 },
	{ "sub_style", ValueType::INT, "", (int)0x00000000 },
	{ "language_code", ValueType::INT, "", (int)0x00000002 },
	{ "region_code", ValueType::INT, "", (int)0x00000000 },
	{ "country_code", ValueType::INT, "", (int)0x00000001 },
	{ "font_name", ValueType::STR, "FTT-Matisse Pro Latin" },
	{ "file_name", ValueType::STR, "ltn13.pgf" },
	{ "expire_date", ValueType::INT, "", (int)0x00000000 },
	{ "shadow_option", ValueType::INT, "", (int)0x00000000 },
};

// Dump of /DATA/FONT/PROPERTY/INFO15
static const KeyValue tree_DATA_FONT_PROPERTY_INFO15[] = {
	{ "h_size", ValueType::INT, "", (int)0x000001c0 },
	{ "v_size", ValueType::INT, "", (int)0x000001c0 },
	{ "h_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "v_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "extra_attributes", ValueType::INT, "", (int)0x00000000 },
	{ "weight", ValueType::INT, "", (int)0x00000000 },
	{ "family_code", ValueType::INT, "", (int)0x00000001 },
	{ "style", ValueType::INT, "", (int)0x00000006 },
	{ "sub_style", ValueType::INT, "", (int)0x00000000 },
	{ "language_code", ValueType::INT, "", (int)0x00000002 },
	{ "region_code", ValueType::INT, "", (int)0x00000000 },
	{ "country_code", ValueType::INT, "", (int)0x00000001 },
	{ "font_name", ValueType::STR, "FTT-NewRodin Pro Latin" },
	{ "file_name", ValueType::STR, "ltn14.pgf" },
	{ "expire_date", ValueType::INT, "", (int)0x00000000 },
	{ "shadow_option", ValueType::INT, "", (int)0x00000000 },
};

// Dump of /DATA/FONT/PROPERTY/INFO16
static const KeyValue tree_DATA_FONT_PROPERTY_INFO16[] = {
	{ "h_size", ValueType::INT, "", (int)0x000001c0 },
	{ "v_size", ValueType::INT, "", (int)0x000001c0 },
	{ "h_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "v_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "extra_attributes", ValueType::INT, "", (int)0x00000000 },
	{ "weight", ValueType::INT, "", (int)0x00000000 },
	{ "family_code", ValueType::INT, "", (int)0x00000002 },
	{ "style", ValueType::INT, "", (int)0x00000006 },
	{ "sub_style", ValueType::INT, "", (int)0x00000000 },
	{ "language_code", ValueType::INT, "", (int)0x00000002 },
	{ "region_code", ValueType::INT, "", (int)0x00000000 },
	{ "country_code", ValueType::INT, "", (int)0x00000001 },
	{ "font_name", ValueType::STR, "FTT-Matisse Pro Latin" },
	{ "file_name", ValueType::STR, "ltn15.pgf" },
	{ "expire_date", ValueType::INT, "", (int)0x00000000 },
	{ "shadow_option", ValueType::INT, "", (int)0x00000000 },
};

// Dump of /DATA/FONT/PROPERTY/INFO17
static const KeyValue tree_DATA_FONT_PROPERTY_INFO17[] = {
	{ "h_size", ValueType::INT, "", (int)0x00000288 },
	{ "v_size", ValueType::INT, "", (int)0x00000288 },
	{ "h_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "v_resolution", ValueType::INT, "", (int)0x00002000 },
	{ "extra_attributes", ValueType::INT, "", (int)0x00000000 },
	{ "weight", ValueType::INT, "", (int)0x00000000 },
	{ "family_code", ValueType::INT, "", (int)0x00000001 },
	{ "style", ValueType::INT, "", (int)0x00000001 },
	{ "sub_style", ValueType::INT, "", (int)0x00000000 },
	{ "language_code", ValueType::INT, "", (int)0x00000003 },
	{ "region_code", ValueType::INT, "", (int)0x00000000 },
	{ "country_code", ValueType::INT, "", (int)0x00000003 },
	{ "font_name", ValueType::STR, "AsiaNHH(512Johab)" },
	{ "file_name", ValueType::STR, "kr0.pgf" },
	{ "expire_date", ValueType::INT, "", (int)0x00000000 },
	{ "shadow_option", ValueType::INT, "", (int)0x00000000 },
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
	{ "path_name", ValueType::STR, "flash0:/font" },
	{ "num_fonts", ValueType::INT, "", (int)0x00000012 },
	{ "PROPERTY", ValueType::DIR, "", ARRAY_SIZE(tree_DATA_FONT_PROPERTY), tree_DATA_FONT_PROPERTY },
};

// Dump of /DATA/COUNT
static const KeyValue tree_DATA_COUNT[] = {
	{ "boot_count", ValueType::INT, "", (int)0x00000000 },
	{ "game_exec_count", ValueType::INT, "", (int)0x00000046 },
	{ "slide_count", ValueType::INT, "", (int)0x00000000 },
	{ "usb_connect_count", ValueType::INT, "", (int)0x000000ec },
	{ "wifi_connect_count", ValueType::INT, "", (int)0x00000000 },
	{ "psn_access_count", ValueType::INT, "", (int)0x00000000 },
};

// Dump of /DATA
static const KeyValue tree_DATA[] = {
	{ "FONT", ValueType::DIR, "", ARRAY_SIZE(tree_DATA_FONT), tree_DATA_FONT },
	{ "COUNT", ValueType::DIR, "", ARRAY_SIZE(tree_DATA_COUNT), tree_DATA_COUNT },
};

// Dump of /SYSPROFILE/RESOLUTION
static const KeyValue tree_SYSPROFILE_RESOLUTION[] = {
	{ "horizontal", ValueType::INT, "", (int)0x00002012 },
	{ "vertical", ValueType::INT, "", (int)0x00002012 },
};

// Dump of /SYSPROFILE
static const KeyValue tree_SYSPROFILE[] = {
	{ "sound_reduction", ValueType::INT, "", (int)0x00000000 },
	{ "RESOLUTION", ValueType::DIR, "", ARRAY_SIZE(tree_SYSPROFILE_RESOLUTION), tree_SYSPROFILE_RESOLUTION },
};

// There's also SYSTEM, CONFIG, REGISTRY but not adding them until they are needed.
const KeyValue ROOT[] = {
	{ "DATA", ValueType::DIR, "", ARRAY_SIZE(tree_DATA), tree_DATA },
	{ "SYSPROFILE", ValueType::DIR, "", ARRAY_SIZE(tree_SYSPROFILE), tree_SYSPROFILE },
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
			WARN_LOG(Log::sceReg, "Path not found: %.*s", (int)path.size(), path.data());
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

	if (!Memory::IsValidRange(bufAddr, num * 27)) {
		return hleLogError(Log::sceReg, -1, "bad output addr");
	}

	const int addrLen = 27;  // for some reason

	int count = 0;
	const KeyValue *keyvals = LookupCategory(iter->second.path, &count);
	if (!keyvals) {
		return hleLogWarning(Log::sceReg, SCE_REG_ERROR_CATEGORY_NOT_FOUND);
	}

	count = std::min(count, num);

	for (int i = 0; i < num; i++) {
		char *dest = (char *)Memory::GetPointerWrite(bufAddr + i * 27);
		strncpy(dest, keyvals[i].name.c_str(), 27);
	}

	return hleLogInfo(Log::sceReg, 0);
}

int sceRegGetKeyInfo(int catHandle, const char *name, u32 outKeyHandleAddr, u32 outTypeAddr, u32 outSizeAddr) {
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
				case ValueType::BIN: size = (int)keyvals[i].strValue.size(); break;
				case ValueType::STR: size = (int)keyvals[i].strValue.size() + 1; break;
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
	return hleLogError(Log::sceReg, 0);
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
		Memory::MemcpyUnchecked(bufAddr, keyval.strValue.data(), std::min(size, (u32)keyval.strValue.size()));
		return hleLogInfo(Log::sceReg, 0);
	case ValueType::STR:
		Memory::MemcpyUnchecked(bufAddr, keyval.strValue.data(), std::min(size, (u32)keyval.strValue.size() + 1));
		return hleLogInfo(Log::sceReg, 0, "value: '%s'", keyval.strValue.data());
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
	return hleLogError(Log::sceReg, 0);
}

int sceRegSetKeyValue(int catHandle, const char *name, u32 bufAddr, u32 size) {
	return hleLogError(Log::sceReg, 0);
}

int sceRegCreateKey(int catHandle, const char *name, int type, u32 size) {
	return hleLogError(Log::sceReg, 0);
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
	{ 0x3615BC87, nullptr, "sceRegRemoveKey", 'i', "i" },
	{ 0x9B25EDF1, nullptr, "sceRegExit", 'i', "i" },
	// TODO: Add test for these.
	{ 0xBE8C1263, nullptr, "sceRegGetCategoryNumAtRoot", 'i', "ii" },
	{ 0x835ECE6F, nullptr, "sceRegGetCategoryListAtRoot", 'i', "ipi" },
};

void Register_sceReg() {
	RegisterHLEModule("sceReg", ARRAY_SIZE(sceReg), sceReg);
}
