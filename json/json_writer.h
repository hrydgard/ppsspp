// Minimal-state JSON writer. Consumes almost no memory
// apart from the string being built-up, which could easily be replaced
// with a file stream (although I've chosen not to do that just yet).
//
// Writes nicely 2-space indented output with correct comma-placement
// in arrays and dictionaries.
//
// Does not deal with encodings in any way.
//
// Zero dependencies apart from stdlib.
// See json_writer_test.cpp for usage.

#include <string>
#include <vector>
#include <sstream>

#include "base/basictypes.h"

class JsonWriter {
public:
	JsonWriter();
	~JsonWriter();
	void begin();
	void end();
	void pushDict(const char *name);
	void pushArray(const char *name);
	void pop();
	void writeBool(bool value);
	void writeBool(const char *name, bool value);
	void writeInt(int value);
	void writeInt(const char *name, int value);
	void writeFloat(double value);
	void writeFloat(const char *name, double value);
	void writeString(const char *value);
	void writeString(const char *name, const char *value);

	std::string str() const {
		return str_.str();
	}

private:
	const char *indent(int n) const;
	const char *comma() const;
	const char *arrayComma() const;
	const char *indent() const;
	const char *arrayIndent() const;
	enum BlockType {
		ARRAY,
		DICT,
	};
	struct StackEntry {
		StackEntry(BlockType t) : type(t), first(true) {}
		BlockType type;
		bool first;
	};
	std::vector<StackEntry> stack_;
	std::ostringstream str_;

	DISALLOW_COPY_AND_ASSIGN(JsonWriter);
};
