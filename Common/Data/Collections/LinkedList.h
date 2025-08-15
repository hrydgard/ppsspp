#pragma once

template <class T>
struct LinkedListItem : public T {
	LinkedListItem<T> *next;
};
