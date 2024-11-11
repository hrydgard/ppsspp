#pragma once

#include <string>
#include <mutex>
#include <vector>
#include <unordered_map>

struct GhidraSymbol {
	u32 address = 0;
	std::string name;
	bool label;
	bool userDefined;
	std::string dataTypePathName;
};

enum GhidraTypeKind {
	ENUM,
	TYPEDEF,
	POINTER,
	ARRAY,
	STRUCTURE,
	UNION,
	FUNCTION_DEFINITION,
	BUILT_IN,
	UNKNOWN,
};

struct GhidraEnumMember {
	std::string name;
	u64 value = 0;
	std::string comment;
};

struct GhidraCompositeMember {
	std::string fieldName;
	u32 ordinal = 0;
	u32 offset = 0;
	int length = 0;
	std::string typePathName;
	std::string comment;
};

struct GhidraType {
	GhidraTypeKind kind;
	std::string name;
	std::string displayName;
	std::string pathName;
	int length = 0;
	int alignedLength = 0;
	bool zeroLength = false;
	std::string description;

	std::vector<GhidraCompositeMember> compositeMembers;
	std::vector<GhidraEnumMember> enumMembers;
	std::string pointerTypePathName;
	std::string typedefTypePathName;
	std::string typedefBaseTypePathName;
	std::string arrayTypePathName;
	int arrayElementLength = 0;
	u32 arrayElementCount = 0;
	std::string functionPrototypeString;
	std::string builtInGroup;
};

class GhidraClient {
public:
	enum class Status {
		Idle,
		Pending,
		Ready,
	};

	struct Result {
		std::vector<GhidraSymbol> symbols;
		std::unordered_map<std::string, GhidraType> types;
		std::string error;
	};

	Result result;

	~GhidraClient();

	void FetchAll(const std::string& host, int port);

	void UpdateResult();

	bool Idle() const { return status_ == Status::Idle; }

	bool Ready() const { return status_ == Status::Ready; }

	bool Failed() const { return !result.error.empty(); }

private:
	std::thread thread_;
	std::mutex mutex_;
	std::atomic<Status> status_;
	Result pendingResult_;
	std::string host_;
	int port_ = 0;

	bool FetchAllDo(const std::string& host, int port);

	bool FetchSymbols();

	bool FetchTypes();

	bool FetchResource(const std::string& path, std::string& outResult);
};
