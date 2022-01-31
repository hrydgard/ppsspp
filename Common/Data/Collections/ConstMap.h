#pragma once

#include <map>

template <typename T, typename U>
class InitConstMap
{
private:
	std::map<T, U> m_map;
public:
	InitConstMap(const T& key, const U& val)
	{
		m_map[key] = val;
	}

	InitConstMap<T, U>& operator()(const T& key, const U& val)
	{
		m_map[key] = val;
		return *this;
	}

	operator std::map<T, U>()
	{
		return m_map;
	}
};
