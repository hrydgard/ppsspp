#include <cstring>
#include "Common/Render/DrawBuffer.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Data/Text/WrapText.h"

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

bool WordWrapper::WrapBeforeWord() {
	if (flags_ & FLAG_WRAP_TEXT) {
		if (x_ + wordWidth_ > maxW_ && !out_.empty()) {
			if (IsShy(lastChar_)) {
				// Soft hyphen, replace it with a real hyphen since we wrapped at it.
				// TODO: There's an edge case here where the hyphen might not fit.
				out_[out_.size() - 2] = '-';
				out_[out_.size() - 1] = '\n';
			} else {
				out_ += "\n";
			}
			lastChar_ = '\n';
			lastLineStart_ = out_.size();
			x_ = 0.0f;
			forceEarlyWrap_ = false;
			return true;
		}
	}
	if (flags_ & FLAG_ELLIPSIZE_TEXT) {
		const bool hasEllipsis = out_.size() > 3 && out_.substr(out_.size() - 3) == "...";
		if (x_ + wordWidth_ > maxW_ && !hasEllipsis) {
			AddEllipsis();
			skipNextWord_ = true;
			if ((flags_ & FLAG_WRAP_TEXT) == 0) {
				scanForNewline_ = true;
			}
		}
	}
	return false;
}

void WordWrapper::AddEllipsis() {
	if (!out_.empty() && IsSpaceOrShy(lastChar_)) {
		UTF8 utf(out_.c_str(), (int)out_.size());
		utf.bwd();
		out_.resize(utf.byteIndex());
		out_ += "...";
	} else {
		out_ += "...";
	}
	lastChar_ = '.';
	x_ += ellipsisWidth_;
}

void WordWrapper::AppendWord(int endIndex, int lastChar, bool addNewline) {
	int lastWordStartIndex = lastIndex_;
	if (WrapBeforeWord()) {
		// Advance to the first non-whitespace UTF-8 character in the following word (if any) to prevent starting the new line with a whitespace
		UTF8 utf8Word(str_, lastWordStartIndex);
		while (lastWordStartIndex < endIndex) {
			const uint32_t c = utf8Word.next();
			if (!IsSpace(c)) {
				break;
			}
			lastWordStartIndex = utf8Word.byteIndex();
		}
	}

	lastEllipsisIndex_ = -1;
	if (skipNextWord_) {
		lastIndex_ = endIndex;
		return;
	}

	// This will include the newline.
	if (x_ <= maxW_) {
		out_.append(str_.data() + lastWordStartIndex, str_.data() + endIndex);
	} else {
		scanForNewline_ = true;
	}
	if (addNewline && (flags_ & FLAG_WRAP_TEXT)) {
		out_ += "\n";
		lastChar_ = '\n';
		lastLineStart_ = out_.size();
		scanForNewline_ = false;
		x_ = 0.0f;
	} else {
		// We may have appended a newline - check.
		size_t pos = out_.find_last_of('\n');
		if (pos != out_.npos) {
			lastLineStart_ = pos + 1;
		}

		if (lastChar == -1 && !out_.empty()) {
			UTF8 utf(out_.c_str(), (int)out_.size());
			utf.bwd();
			lastChar = utf.next();
		}
		lastChar_ = lastChar;

		if (lastLineStart_ != out_.size()) {
			// To account for kerning around spaces, we recalculate the entire line width.
			x_ = MeasureWidth(std::string_view(out_.c_str() + lastLineStart_, out_.size() - lastLineStart_));
		} else {
			x_ = 0.0f;
		}
	}
	lastIndex_ = endIndex;
	wordWidth_ = 0.0f;
}

void WordWrapper::Wrap() {
	// First, let's check if it fits as-is.
	size_t len = str_.length();
	if (MeasureWidth(str_) <= maxW_) {
		// If it fits, we don't need to go through each character.
		out_ = std::string(str_);
		return;
	}

	out_.clear();
	// We know it'll be approximately this size. It's fine if the guess is a little off.
	out_.reserve(len + len / 16);

	if (flags_ & FLAG_ELLIPSIZE_TEXT) {
		ellipsisWidth_ = MeasureWidth("...");
	}

	for (UTF8 utf(str_); !utf.end(); ) {
		int beforeIndex = utf.byteIndex();
		uint32_t c = utf.next();
		int afterIndex = utf.byteIndex();

		// Is this a newline character, hard wrapping?
		if (c == '\n') {
			if (skipNextWord_) {
				lastIndex_ = beforeIndex;
				skipNextWord_ = false;
			}
			// This will include the newline character.
			AppendWord(afterIndex, c, false);
			// We wrapped once, so stop forcing.
			forceEarlyWrap_ = false;
			scanForNewline_ = false;
			continue;
		}

		if (scanForNewline_) {
			// We're discarding the rest of the characters until a newline (no wrapping.)
			lastIndex_ = afterIndex;
			continue;
		}

		// Measure the entire word for kerning purposes.  May not be 100% perfect.
		float newWordWidth = 0.0f;
		if (afterIndex <= str_.length()) {
			newWordWidth = MeasureWidth(str_.substr(lastIndex_, afterIndex - lastIndex_));
		}

		// Is this the end of a word (space)?  We'll also output up to a soft hyphen.
		if (wordWidth_ > 0.0f && IsSpaceOrShy(c)) {
			AppendWord(afterIndex, c, false);
			skipNextWord_ = false;
			continue;
		}

		// We're scanning for the next word.
		if (skipNextWord_)
			continue;

		if ((flags_ & FLAG_ELLIPSIZE_TEXT) != 0 && wordWidth_ > 0.0f && lastEllipsisIndex_ == -1) {
			float checkX = x_;
			// If we allow wrapping, assume we'll wrap as needed.
			if ((flags_ & FLAG_WRAP_TEXT) != 0 && x_ >= maxW_) {
				checkX = 0;
			}

			// If we can only fit an ellipsis, time to output and skip ahead.
			// Ignore x for newWordWidth, because we might wrap.
			if (checkX + wordWidth_ + ellipsisWidth_ <= maxW_ && newWordWidth + ellipsisWidth_ > maxW_) {
				lastEllipsisIndex_ = beforeIndex;
				continue;
			}
		}

		// Can the word fit on a line even all by itself so far?
		if (wordWidth_ > 0.0f && newWordWidth > maxW_) {
			// If we had a good place for an ellipsis, let's do that.
			if (lastEllipsisIndex_ != -1) {
				AppendWord(lastEllipsisIndex_, -1, false);
				AddEllipsis();
				skipNextWord_ = true;
				if ((flags_ & FLAG_WRAP_TEXT) == 0) {
					scanForNewline_ = true;
				}
				continue;
			}

			// Doesn't fit.  Let's drop what's there so far onto its own line.
			if (x_ > 0.0f && x_ + wordWidth_ > maxW_ && beforeIndex > lastIndex_ && (flags_ & FLAG_WRAP_TEXT) != 0) {
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
			AppendWord(beforeIndex, -1, true);
			forceEarlyWrap_ = false;
			// The current character will be handled as part of the next word.
			continue;
		}

		if ((flags_ & FLAG_ELLIPSIZE_TEXT) && wordWidth_ > 0.0f && x_ + newWordWidth + ellipsisWidth_ > maxW_) {
			if ((flags_ & FLAG_WRAP_TEXT) == 0 && x_ + wordWidth_ + ellipsisWidth_ <= maxW_) {
				// Now, add the word so far (without this latest character) and show the ellipsis.
				AppendWord(lastEllipsisIndex_ != -1 ? lastEllipsisIndex_ : beforeIndex, -1, false);
				AddEllipsis();
				forceEarlyWrap_ = false;
				skipNextWord_ = true;
				if ((flags_ & FLAG_WRAP_TEXT) == 0) {
					scanForNewline_ = true;
				}
				continue;
			}
		}

		wordWidth_ = newWordWidth;

		// Is this the end of a word via punctuation / CJK?
		if (wordWidth_ > 0.0f && (IsCJK(c) || IsPunctuation(c) || forceEarlyWrap_)) {
			// CJK doesn't require spaces, so we treat each letter as its own word.
			AppendWord(afterIndex, c, false);
		}
	}

	// Now insert the rest of the string - the last word.
	AppendWord((int)len, 0, false);
}
