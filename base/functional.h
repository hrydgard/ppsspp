// This file simply includes <functional> and applies any necessary compatibility fixes for
// strange platforms like iOS.

// Use placeholder as the namespace for placeholders.

#pragma once

#include <functional>
#ifdef __SYMBIAN32__
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/weak_ptr.hpp>
#else
#include <memory>
#endif
#include <vector>

#if defined(__SYMBIAN32__) || defined(IOS) || defined(MACGNUSTD)
#ifndef __SYMBIAN32__
#include <tr1/functional>
#include <tr1/memory>
#endif
namespace std {
#ifdef __SYMBIAN32__
	using boost::bind;
	using boost::function;
	using boost::shared_ptr;
#else
    using tr1::bind;
    using tr1::function;
    using tr1::shared_ptr;
#endif

    template <typename T>
    inline shared_ptr<T> make_shared()
    {
        return shared_ptr<T>(new T());
    }

    template <typename T, typename Arg1>
    inline shared_ptr<T> make_shared(Arg1& arg1)
    {
        return shared_ptr<T>(new T(arg1));
    }
}
#endif

#ifdef __SYMBIAN32__
#define placeholder
#elif defined(IOS)
namespace placeholder = std::tr1::placeholders;
#else
namespace placeholder = std::placeholders;
#endif

