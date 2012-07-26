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

/* For ANSI-challenged compilers, you may want to #define
 * NO_MEMBER_TEMPLATES, explicit or mutable */

#ifdef _MSC_VER
#define NO_MEMBER_TEMPLATES
#endif

template <class X> class linked_ptr
{
public:

#ifndef NO_MEMBER_TEMPLATES
#   define TEMPLATE_FUNCTION template <class Y>
    TEMPLATE_FUNCTION friend class linked_ptr<Y>;
#else
#   define TEMPLATE_FUNCTION
    typedef X Y;
#endif

    typedef X element_type;

    explicit linked_ptr(X* p = 0) throw()
        : itsPtr(p) {itsPrev = itsNext = this;}
    ~linked_ptr()
        {release();}
    linked_ptr(const linked_ptr& r) throw()
        {acquire(r);}
    linked_ptr& operator=(const linked_ptr& r)
    {
        if (this != &r) {
            release();
            acquire(r);
        }
        return *this;
    }

#ifndef NO_MEMBER_TEMPLATES
    template <class Y> friend class linked_ptr<Y>;
    template <class Y> linked_ptr(const linked_ptr<Y>& r) throw()
        {acquire(r);}
    template <class Y> linked_ptr& operator=(const linked_ptr<Y>& r)
    {
        if (this != &r) {
            release();
            acquire(r);
        }
        return *this;
    }
#endif // NO_MEMBER_TEMPLATES

    X& operator*()  const throw()   {return *itsPtr;}
    X* operator->() const throw()   {return itsPtr;}
    X* get()        const throw()   {return itsPtr;}
    bool unique()   const throw()   {return itsPrev ? itsPrev==this : true;}

private:
    X*                          itsPtr;
    mutable const linked_ptr*   itsPrev;
    mutable const linked_ptr*   itsNext;

    void acquire(const linked_ptr& r) throw()
    { // insert this to the list
        itsPtr = r.itsPtr;
        itsNext = r.itsNext;
        itsNext->itsPrev = this;
        itsPrev = &r;
#ifndef mutable
        r.itsNext = this;
#else // for ANSI-challenged compilers
        (const_cast<linked_ptr<X>*>(&r))->itsNext = this;
#endif
    }

#ifndef NO_MEMBER_TEMPLATES
    template <class Y> void acquire(const linked_ptr<Y>& r) throw()
    { // insert this to the list
        itsPtr = r.itsPtr;
        itsNext = r.itsNext;
        itsNext->itsPrev = this;
        itsPrev = &r;
#ifndef mutable
        r.itsNext = this;
#else // for ANSI-challenged compilers
        (const_cast<linked_ptr<X>*>(&r))->itsNext = this;
#endif
    }
#endif // NO_MEMBER_TEMPLATES

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

