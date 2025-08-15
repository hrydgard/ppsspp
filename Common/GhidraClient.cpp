#include "Common/Data/Format/JSONReader.h"
#include "Common/Net/HTTPClient.h"
#include "Common/Thread/ThreadUtil.h"

#include "Common/GhidraClient.h"

using namespace json;

static GhidraTypeKind ResolveTypeKind(const std::string &kind) {
	if (kind == "ENUM") return ENUM;
	if (kind == "TYPEDEF") return TYPEDEF;
	if (kind == "POINTER") return POINTER;
	if (kind == "ARRAY") return ARRAY;
	if (kind == "STRUCTURE") return STRUCTURE;
	if (kind == "UNION") return UNION;
	if (kind == "FUNCTION_DEFINITION") return FUNCTION_DEFINITION;
	if (kind == "BUILT_IN") return BUILT_IN;
	return UNKNOWN;
}

static bool IsBitfieldEnum(const std::vector<GhidraEnumMember> &enumMembers) {
	u64 previousValues = 0;
	for (const auto &member : enumMembers) {
		if (previousValues & member.value) {
			return false;
		}
		previousValues |= member.value;
	}
	return true;
}

GhidraClient::~GhidraClient() {
	if (thread_.joinable()) {
		thread_.join();
	}
}

void GhidraClient::FetchAll(const std::string &host, const int port) {
	std::lock_guard<std::mutex> lock(mutex_);
	if (status_ != Status::Idle) {
		return;
	}
	status_ = Status::Pending;
	thread_ = std::thread([this, host, port] {
		SetCurrentThreadName("GhidraClient");
		FetchAllDo(host, port);
	});
}

bool GhidraClient::FetchAllDo(const std::string &host, const int port) {
	std::lock_guard<std::mutex> lock(mutex_);
	host_ = host;
	port_ = port;
	const bool result = FetchTypes() && FetchSymbols();
	status_ = Status::Ready;
	return result;
}

void GhidraClient::UpdateResult() {
	std::lock_guard<std::mutex> lock(mutex_);
	if (status_ != Status::Ready) {
		return;
	}
	if (thread_.joinable()) {
		thread_.join();
	}
	result = std::move(pendingResult_);
	pendingResult_ = Result();
	status_ = Status::Idle;
}

bool GhidraClient::FetchSymbols() {
	std::string json;
	if (!FetchResource("/v1/symbols", json)) {
		return false;
	}
	JsonReader reader(json.c_str(), json.size());
	if (!reader.ok()) {
		pendingResult_.error = "symbols parsing error";
		return false;
	}
	const JsonValue entries = reader.root().getArray("symbols")->value;
	if (entries.getTag() != JSON_ARRAY) {
		pendingResult_.error = "symbols is not an array";
		return false;
	}

	for (const auto pEntry : entries) {
		JsonGet entry = pEntry->value;

		GhidraSymbol symbol;
		symbol.address = entry.getInt("address", 0);
		symbol.name = entry.getStringOr("name", "");
		symbol.label = strcmp(entry.getStringOr("type", ""), "Label") == 0;
		symbol.userDefined = strcmp(entry.getStringOr("source", ""), "USER_DEFINED") == 0;
		symbol.dataTypePathName = entry.getStringOr("dataTypePathName", "");
		pendingResult_.symbols.emplace_back(symbol);
	}
	return true;
}

bool GhidraClient::FetchTypes() {
	std::string json;
	if (!FetchResource("/v1/types", json)) {
		return false;
	}
	JsonReader reader(json.c_str(), json.size());
	if (!reader.ok()) {
		pendingResult_.error = "types parsing error";
		return false;
	}
	const JsonValue entries = reader.root().getArray("types")->value;
	if (entries.getTag() != JSON_ARRAY) {
		pendingResult_.error = "types is not an array";
		return false;
	}

	for (const auto pEntry : entries) {
		const JsonGet entry = pEntry->value;

		GhidraType type;
		type.displayName = entry.getStringOr("displayName", "");
		type.pathName = entry.getStringOr("pathName", "");
		type.length = entry.getInt("length", 0);
		type.alignedLength = entry.getInt("alignedLength", 0);
		type.zeroLength = entry.getBool("zeroLength", false);
		type.description = entry.getStringOr("description", "");
		type.kind = ResolveTypeKind(entry.getStringOr("kind", ""));

		switch (type.kind) {
			case ENUM: {
				const JsonNode *enumEntries = entry.getArray("members");
				if (!enumEntries) {
					pendingResult_.error = "missing enum members";
					return false;
				}
				for (const JsonNode *pEnumEntry : enumEntries->value) {
					JsonGet enumEntry = pEnumEntry->value;
					GhidraEnumMember member;
					member.name = enumEntry.getStringOr("name", "");
					member.value = enumEntry.getInt("value", 0);
					member.comment = enumEntry.getStringOr("comment", "");
					type.enumMembers.push_back(member);
				}
				type.enumBitfield = IsBitfieldEnum(type.enumMembers);
				break;
			}
			case TYPEDEF:
				type.typedefTypePathName = entry.getStringOr("typePathName", "");
				type.typedefBaseTypePathName = entry.getStringOr("baseTypePathName", "");
				break;
			case POINTER:
				type.pointerTypePathName = entry.getStringOr("typePathName", "");
				break;
			case ARRAY:
				type.arrayTypePathName = entry.getStringOr("typePathName", "");
				type.arrayElementLength = entry.getInt("elementLength", 0);
				type.arrayElementCount = entry.getInt("elementCount", 0);
				break;
			case STRUCTURE:
			case UNION: {
				const JsonNode *compositeEntries = entry.getArray("members");
				if (!compositeEntries) {
					pendingResult_.error = "missing composite members";
					return false;
				}
				for (const JsonNode *pCompositeEntry : compositeEntries->value) {
					JsonGet compositeEntry = pCompositeEntry->value;
					GhidraCompositeMember member;
					member.fieldName = compositeEntry.getStringOr("fieldName", "");
					member.ordinal = compositeEntry.getInt("ordinal", 0);
					member.offset = compositeEntry.getInt("offset", 0);
					member.length = compositeEntry.getInt("length", 0);
					member.typePathName = compositeEntry.getStringOr("typePathName", "");
					member.comment = compositeEntry.getStringOr("comment", "");
					type.compositeMembers.push_back(member);
				}
				break;
			}
			case FUNCTION_DEFINITION:
				type.functionPrototypeString = entry.getStringOr("prototypeString", "");
				break;
			case BUILT_IN:
				type.builtInGroup = entry.getStringOr("group", "");
				break;
			default:
				continue;
		}

		pendingResult_.types.emplace(type.pathName, type);
	}
	return true;
}

bool GhidraClient::FetchResource(const std::string &path, std::string &outResult) {
	http::Client http(nullptr);
	if (!http.Resolve(host_.c_str(), port_)) {
		pendingResult_.error = "can't resolve host";
		return false;
	}
	bool cancelled = false;
	if (!http.Connect(1, 5.0, &cancelled)) {
		pendingResult_.error = "can't connect to host";
		return false;
	}
	net::RequestProgress progress(&cancelled);
	Buffer result;
	const int code = http.GET(http::RequestParams(path.c_str()), &result, &progress);
	http.Disconnect();
	if (code != 200) {
		pendingResult_.error = "unsuccessful response code from API endpoint";
		return false;
	}
	result.TakeAll(&outResult);
	return true;
}
