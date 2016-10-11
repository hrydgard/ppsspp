// This file simply includes <functional> and applies any necessary compatibility fixes for
// strange platforms like iOS.

// Use placeholder as the namespace for placeholders.

#pragma once

#include <functional>
#include <memory>
#include <vector>

#if defined(MACGNUSTD)
#include <tr1/functional>
#include <tr1/memory>
namespace std {
	using tr1::bind;
	using tr1::function;
	using tr1::shared_ptr;

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

namespace placeholder = std::placeholders;

