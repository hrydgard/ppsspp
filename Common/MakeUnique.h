#pragma once

#include <memory>
#include <type_traits>

// Custom make_unique so that C++14 support will not be necessary for compilation
template<class T, class... Args,
	typename std::enable_if<!std::is_array<T>::value, int>::type = 0>
std::unique_ptr<T> make_unique(Args&&... args)
{
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

template<class T,
	typename std::enable_if<std::is_array<T>::value && std::extent<T>::value == 0, int>::type = 0>
std::unique_ptr<T> make_unique(std::size_t size)
{
	return std::unique_ptr<T>(new typename std::remove_extent<T>::type[size]());
}

template<class T, class... Args,
	typename std::enable_if<std::extent<T>::value != 0, int>::type = 0>
void make_unique(Args&&... args) = delete;


template<class T,
	typename std::enable_if<!std::is_array<T>::value, int>::type = 0>
	std::unique_ptr<T> make_unique_default_init()
{
	return std::unique_ptr<T>(new T);
}

template<class T,
	typename std::enable_if<std::is_array<T>::value && std::extent<T>::value == 0, int>::type = 0>
	std::unique_ptr<T> make_unique_default_init(std::size_t size)
{
	return std::unique_ptr<T>(new typename std::remove_extent<T>::type[size]);
}

template<class T, class... Args,
	typename std::enable_if<std::extent<T>::value != 0, int>::type = 0>
	void make_unique_default_init(Args&&... args) = delete;
