#pragma once

#include <string>
#include <string_view>

class WordWrapper {
public:
	WordWrapper(std::string_view str, float maxW, int flags)
		: str_(str), maxW_(maxW), flags_(flags) {
	}
	virtual ~WordWrapper() {}

	// TODO: This should return a vector of std::string_view for the lines, instead of building up a new string.
	std::string Wrapped();

protected:
	virtual float MeasureWidth(std::string_view str) = 0;

	void Wrap();
	bool WrapBeforeWord();
	void AppendWord(int endIndex, int lastChar, bool addNewline);
	void AddEllipsis();

	static bool IsCJK(uint32_t c);
	static bool IsPunctuation(uint32_t c);
	static bool IsSpace(uint32_t c);
	static bool IsShy(uint32_t c);
	static bool IsSpaceOrShy(uint32_t c) {
		return IsSpace(c) || IsShy(c);
	}

	const std::string_view str_;
	const float maxW_;
	const int flags_;
	std::string out_;

	// Index of last output / start of current word.
	int lastIndex_ = 0;
	// Ideal place to put an ellipsis if we run out of space.
	int lastEllipsisIndex_ = -1;
	// Index of last line start.
	size_t lastLineStart_ = 0;
	// Last character written to out_.
	int lastChar_ = 0;
	// Position the current word starts at.
	float x_ = 0.0f;
	// Most recent width of word since last index.
	float wordWidth_ = 0.0f;
	// Width of "..." when flag is set, zero otherwise.
	float ellipsisWidth_ = 0.0f;
	// Force the next word to cut partially and wrap.
	bool forceEarlyWrap_ = false;
	// Skip all characters until the next newline.
	bool scanForNewline_ = false;
	// Skip the next word, replaced with ellipsis.
	bool skipNextWord_ = false;
};
