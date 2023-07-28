// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#ifndef SMART_SHARED_PTR_H
#define SMART_SHARED_PTR_H

#include <memory>

#include "libsmartmacros.h"

struct SharedPtrBase
{
    LIB_SMART_API static void logDefaultConstructor(const char *instantiation, const void *t);
    LIB_SMART_API static void logConstructor(const char *instantiation, const void *t, const void *pointee);
    LIB_SMART_API static void logCopyConstructor(const char *instantiation, const void *t, const void *refData);
    LIB_SMART_API static void logAssignment(const char *instantiation, const void *t, const void *refData);
    LIB_SMART_API static void logDestructor(const char *instantiation, const void *t, int remainingRefCount);
};

template <class T>
class SharedPtr : public SharedPtrBase {
public:
    SharedPtr() { logDefaultConstructor(typeid(T).name(), this); }

    SharedPtr(T *v) : mPtr(v)
    {
        logConstructor(typeid(T).name(), this, v);
    }

    SharedPtr(const SharedPtr<T> &other) : mPtr(other.mPtr)
    {
        logCopyConstructor(typeid(T).name(), this, data());
    }

    template<class X>
    SharedPtr(const SharedPtr<X> &other) : mPtr(other.mPtr)
    {
        logCopyConstructor(typeid(T).name(), this, data());
    }

    SharedPtr &operator=(const SharedPtr &other)
    {
        mPtr = other.mPtr;
        return *this;
    }

    T *data() const
    {
        return mPtr.get();
    }

    int useCount() const
    {
        return mPtr.use_count();
    }

    void dummyMethod1()
    {
    }

    bool isNull() const
    {
        return mPtr.get() == nullptr;
    }

    T& operator*() const
    {
        // Crashes if smart pointer is empty (just like std::shared_ptr).
        return *mPtr;
    }

    T *operator->() const
    {
        return mPtr.get();
    }

    bool operator!() const
    {
        return !mPtr;
    }

    ~SharedPtr()
    {
        if (mPtr.use_count() >= 1)
            logDestructor(typeid(T).name(), this, mPtr.use_count() - 1);
    }

    std::shared_ptr<T> mPtr;
};

#endif // SMART_SHARED_PTR_H
