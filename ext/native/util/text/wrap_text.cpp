#include <cstring>
#include "util/text/utf8.h"
#include "util/text/wrap_text.h"

bool WordWrapper::IsCJK(uint32_t c) {
	if (c < 0x1000) {
		return false;
	}

	// CJK characters can be wrapped more freely.
	bool result = (c >= 0x1100 && c <= 0x11FF); // Hangul Jamo.
	result = result || (c >= 0x2E80 && c <= 0x2FFF); // Kangxi Radicals etc.
#if 0
	result = result || (c >= 0x3040 && c <= 0x31FF); // Hiragana, Katakana, Hangul Compatibility Jamo etc.
	result = result || (c >= 0x3200 && c <= 0x32FF); // CJK Enclosed
	result = result || (c >= 0x3300 && c <= 0x33FF); // CJK Compatibility
	result = result || (c >= 0x3400 && c <= 0x4DB5); // CJK Unified Ideographs Extension A
#else
	result = result || (c >= 0x3040 && c <= 0x4DB5); // Above collapsed
#endif
	result = result || (c >= 0x4E00 && c <= 0x9FBB); // CJK Unified Ideographs
	result = result || (c >= 0xAC00 && c <= 0xD7AF); // Hangul Syllables
	result = result || (c >= 0xF900 && c <= 0xFAD9); // CJK Compatibility Ideographs
	result = result || (c >= 0x20000 && c <= 0x2A6D6); // CJK Unified Ideographs Extension B
	result = result || (c >= 0x2F800 && c <= 0x2FA1D); // CJK Compatibility Supplement
	return result;
}

bool WordWrapper::IsPunctuation(uint32_t c) {
	switch (c) {
	// TODO: This list of punctuation is very incomplete.
	case ',':
	case '.':
	case ':':
	case '!':
	case ')':
	case '?':
	case 0x00AD: // SOFT HYPHEN
	case 0x3001: // IDEOGRAPHIC COMMA
	case 0x3002: // IDEOGRAPHIC FULL STOP
	case 0x06D4: // ARABIC FULL STOP
	case 0xFF01: // FULLWIDTH EXCLAMATION MARK
	case 0xFF09: // FULLWIDTH RIGHT PARENTHESIS
	case 0xFF1F: // FULLWIDTH QUESTION MARK
		return true;

	default:
		return false;
	}
}

bool WordWrapper::IsSpace(uint32_t c) {
	switch (c) {
	case '\t':
	case ' ':
	case 0x2002: // EN SPACE
	case 0x2003: // EM SPACE
	case 0x3000: // IDEOGRAPHIC SPACE
		return true;

	default:
		return false;
	}
}

bool WordWrapper::IsShy(uint32_t c) {
	return c == 0x00AD; // SOFT HYPHEN
}

std::string WordWrapper::Wrapped() {
	if (out_.empty()) {
		Wrap();
	}
	return out_;
}

void WordWrapper::WrapBeforeWord() {
	if (x_ + wordWidth_ > maxW_) {
		if (IsShy(out_[out_.size() - 1])) {
			// Soft hyphen, replace it with a real hyphen since we wrapped at it.
			// TODO: There's an edge case here where the hyphen might not fit.
			out_[out_.size() - 1] = '-';
		}
		out_ += "\n";
		x_ = 0.0f;
		forceEarlyWrap_ = false;
	}
}

void WordWrapper::AppendWord(int endIndex, bool addNewline) {
	WrapBeforeWord();
	// This will include the newline.
	out_ += std::string(str_ + lastIndex_, endIndex - lastIndex_);
	if (addNewline) {
		out_ += "\n";
	}
	lastIndex_ = endIndex;
}

void WordWrapper::Wrap() {
	out_.clear();

	// First, let's check if it fits as-is.
	size_t len = strlen(str_);
	if (MeasureWidth(str_, len) <= maxW_) {
		// If it fits, we don't need to go through each character.
		out_ = str_;
		return;
	}

	for (UTF8 utf(str_); !utf.end(); ) {
		int beforeIndex = utf.byteIndex();
		uint32_t c = utf.next();
		int afterIndex = utf.byteIndex();

		// Is this a newline character, hard wrapping?
		if (c == '\n') {
			// This will include the newline character.
			AppendWord(afterIndex, false);
			x_ = 0.0f;
			wordWidth_ = 0.0f;
			// We wrapped once, so stop forcing.
			forceEarlyWrap_ = false;
			continue;
		}

		float newWordWidth = 0.0f;
		if (c == '\n') {
			newWordWidth = wordWidth_;
		} else {
			// Measure the entire word for kerning purposes.  May not be 100% perfect.
			newWordWidth = MeasureWidth(str_ + lastIndex_, afterIndex - lastIndex_);
		}

		// Is this the end of a word (space)?
		if (wordWidth_ > 0.0f && IsSpace(c)) {
			AppendWord(afterIndex, false);
			// We include the space in the x increase.
			// If the space takes it over, we'll wrap on the next word.
			x_ += newWordWidth;
			wordWidth_ = 0.0f;
			continue;
		}

		// Can the word fit on a line even all by itself so far?
		if (wordWidth_ > 0.0f && newWordWidth > maxW_) {
			// Nope.  Let's drop what's there so far onto its own line.
			if (x_ > 0.0f && x_ + wordWidth_ > maxW_ && beforeIndex > lastIndex_) {
				// Let's put as many characters as will fit on the previous line.
				// This word can't fit on one line even, so it's going to be cut into pieces anyway.
				// Better to avoid huge gaps, in that case.
				forceEarlyWrap_ = true;

				// Now rewind back to where the word started so we can wrap at the opportune moment.
				wordWidth_ = 0.0f;
				while (utf.byteIndex() > lastIndex_) {
					utf.bwd();
				}
				continue;
			}
			// Now, add the word so far (without this latest character) and break.
			AppendWord(beforeIndex, true);
			x_ = 0.0f;
			wordWidth_ = 0.0f;
			forceEarlyWrap_ = false;
			// The current character will be handled as part of the next word.
			continue;
		}

		wordWidth_ = newWordWidth;

		// Is this the end of a word via punctuation / CJK?
		if (wordWidth_ > 0.0f && (IsCJK(c) || IsPunctuation(c) || forceEarlyWrap_)) {
			// CJK doesn't require spaces, so we treat each letter as its own word.
			AppendWord(afterIndex, false);
			x_ += wordWidth_;
			wordWidth_ = 0.0f;
		}
	}

	// Now insert the rest of the string - the last word.
	AppendWord((int)len, false);
}
