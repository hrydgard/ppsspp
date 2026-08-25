#include <cstdarg>
#include "Common/Data/Text/StringWriter.h"


StringWriter &StringWriter::F(const char *format, ...) {
	const size_t remainder = bufSize_ - (p_ - start_);
	if (remainder < 3) {
		return *this;
	}
	va_list args;
	va_start(args, format);
	int wouldHaveBeenWritten = vsnprintf(p_, remainder, format, args);
	p_ += std::min((int)remainder, wouldHaveBeenWritten);
	va_end(args);
	return *this;
}
