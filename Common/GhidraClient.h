#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Represents symbol from a Ghidra's program.
// A symbol can be for example a data label, instruction label or a function.
struct GhidraSymbol {
	u32 address = 0;
	std::string name;
	bool label;
	bool userDefined;
	std::string dataTypePathName;
};

// Possible kinds of data types, such as enum, structures or built-ins (Ghidra's primitive types).
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

// Describes single member of an enum type.
struct GhidraEnumMember {
	std::string name;
	u64 value = 0;
	std::string comment;
};

// Describes single member of a composite (structure or union) type.
struct GhidraCompositeMember {
	std::string fieldName;
	u32 ordinal = 0;
	u32 offset = 0;
	int length = 0;
	std::string typePathName;
	std::string comment;
};

// Describes data type from Ghidra. Note that some fields of this structure will only be populated depending on the
// type's kind. Each type has a display name that is suitable for displaying to the user and a path name that
// unambiguously identifies this type.
struct GhidraType {
	GhidraTypeKind kind;
	std::string displayName;
	std::string pathName;
	int length = 0;
	int alignedLength = 0;
	bool zeroLength = false;
	std::string description;

	std::vector<GhidraCompositeMember> compositeMembers;
	std::vector<GhidraEnumMember> enumMembers;
	bool enumBitfield = false; // Determined from a simple heuristic check
	std::string pointerTypePathName;
	std::string typedefTypePathName;
	std::string typedefBaseTypePathName;
	std::string arrayTypePathName;
	int arrayElementLength = 0;
	u32 arrayElementCount = 0;
	std::string functionPrototypeString;
	std::string builtInGroup;
};

// GhidraClient implements fetching data (such as symbols or types) from a remote Ghidra project.
//
// This client uses unofficial API provided by the third party "ghidra-rest-api" extension. The extension is
// available at https://github.com/kotcrab/ghidra-rest-api.
//
// This class doesn't fetch data from every possible endpoint, only those that are actually used by PPSSPP.
//
// How to use:
// 1. The client is created in the Idle status.
// 2. To start fetching data call the FetchAll() method. The client goes to Pending status and the data is fetched
//    in a background thread so your code remains unblocked.
// 3. Periodically check with the Ready() method is the operation has completed. (i.e. check if the client
//    is in the Ready status)
// 4. If the client is ready call UpdateResult() to update result field with new data.
// 5. The client is now back to Idle status, and you can call FetchAll() again later if needed.
class GhidraClient {
	enum class Status {
		Idle,
		Pending,
		Ready,
	};

public:
	struct Result {
		std::vector<GhidraSymbol> symbols;
		std::unordered_map<std::string, GhidraType> types;
		std::string error;
	};

	// Current result of the client. Your thread is safe to access this regardless of client status.
	Result result;

	GhidraClient() : status_(Status::Idle) {}
	~GhidraClient();

	//  If client is idle then asynchronously starts fetching data from Ghidra.
	void FetchAll(const std::string &host, int port);

	// If client is ready then updates the result field with newly fetched data.
	// This must be called from the thread using the result.
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

	bool FetchAllDo(const std::string &host, int port);

	bool FetchSymbols();

	bool FetchTypes();

	bool FetchResource(const std::string &path, std::string &outResult);
};
