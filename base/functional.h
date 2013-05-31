// This file simply includes <functional> and applies any necessary compatibility fixes for
// strange platforms like iOS.

// Use p as the namespace for placeholders.

#pragma once

#include <functional>

#if defined(IOS) || defined(MACGNUSTD)
#include <tr1/functional>
namespace std {
	using tr1::bind;
}
#endif

#ifdef __SYMBIAN32__
#define p
#elif defined(IOS)
#include <tr1/functional>
namespace p = std::tr1::placeholders;
#else
namespace p = std::placeholders;
#endif
