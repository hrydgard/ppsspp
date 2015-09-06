// From http://ootips.org/yonat/4dev/linked_ptr.h

/*
* linked_ptr - simple reference linked pointer
* (like reference counting, just using a linked list of the references
* instead of their count.)
*
* The implementation stores three pointers for every linked_ptr, but
* does not allocate anything on the free store.
*/

#pragma once

template <class X> class linked_ptr
{
public:
	explicit linked_ptr(X* p = 0) throw() : itsPtr(p) {itsPrev = itsNext = this;}
	~linked_ptr() {release();}
	linked_ptr(const linked_ptr& r) throw() {acquire(r);}

	linked_ptr& operator=(const linked_ptr& r)
	{
		if (this != &r) {
			release();
			acquire(r);
		}
		return *this;
	}

	X& operator*()	const throw() {return *itsPtr;}
	X* operator->() const throw() {return itsPtr;}
	X* get()				const throw() {return itsPtr;}
	bool unique()	 const throw() {return itsPrev ? itsPrev==this : true;}

private:
	X*													itsPtr;
	mutable const linked_ptr*	 itsPrev;
	mutable const linked_ptr*	 itsNext;

	void acquire(const linked_ptr& r) throw()
	{ // insert this to the list
		itsPtr = r.itsPtr;
		itsNext = r.itsNext;
		itsNext->itsPrev = this;
		itsPrev = &r;
		r.itsNext = this;
	}

	void release()
	{ // erase this from the list, delete if unique
		if (unique()) delete itsPtr;
		else {
			itsPrev->itsNext = itsNext;
			itsNext->itsPrev = itsPrev;
			itsPrev = itsNext = 0;
		}
		itsPtr = 0;
	}
};

