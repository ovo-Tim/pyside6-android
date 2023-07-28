// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "sbkarrayconverter.h"
#include "sbkarrayconverter_p.h"
#include "helper.h"
#include "sbkconverter.h"
#include "sbkconverter_p.h"

#include <longobject.h>
#include <floatobject.h>

#include <algorithm>

static SbkArrayConverter *ArrayTypeConverters[Shiboken::Conversions::SBK_ARRAY_IDX_SIZE] [2] = {};

namespace Shiboken {
namespace Conversions {

// Check whether Predicate is true for all elements of a sequence
template <class Predicate>
static bool sequenceAllOf(PyObject *pyIn, Predicate p)
{
    const Py_ssize_t size = PySequence_Size(pyIn);
    for (Py_ssize_t i = 0; i < size; ++i) {
        PyObject *item = PySequence_GetItem(pyIn, i);
        const bool ok = p(item);
        Py_XDECREF(item);
        if (!ok)
            return false;
    }
    return true;
}

// Convert a sequence to output iterator
template <class T, class Converter>
inline void convertPySequence(PyObject *pyIn, Converter c, T *out)
{
    const Py_ssize_t size = PySequence_Size(pyIn);
    for (Py_ssize_t i = 0; i < size; ++i) {
        PyObject *item = PySequence_GetItem(pyIn, i);
        *out++ = c(item);
        Py_XDECREF(item);
    }
}

// Internal, for usage by numpy
SbkArrayConverter *createArrayConverter(IsArrayConvertibleToCppFunc toCppCheckFunc)
{
    auto *result = new SbkArrayConverter;
    result->toCppConversions.push_back(toCppCheckFunc);
    return result;
}

static PythonToCppFunc unimplementedArrayCheck(PyObject *, int, int)
{
    return nullptr;
}

SbkArrayConverter *unimplementedArrayConverter()
{
    static SbkArrayConverter *result = createArrayConverter(unimplementedArrayCheck);
    return result;
}

// Integers

static inline bool intCheck(PyObject *pyIn)
{
    return PyLong_Check(pyIn);
}

static short toShort(PyObject *pyIn) { return short(PyLong_AsLong(pyIn)); }

static void sequenceToCppShortArray(PyObject *pyIn, void *cppOut)
{
    auto *handle = reinterpret_cast<ArrayHandle<short> *>(cppOut);
    handle->allocate(PySequence_Size(pyIn));
    convertPySequence(pyIn, toShort, handle->data());
}

static inline bool sequenceSizeCheck(PyObject *pyIn, int expectedSize = -1)
{
    if (expectedSize >= 0) {
        const int size = int(PySequence_Size(pyIn));
        if (size < expectedSize) {
            warning(PyExc_RuntimeWarning, 0, "A sequence of size %d was passed to a function that expects %d.",
                   size, expectedSize);
            return false;
        }
    }
    return true;
}

static inline bool intArrayCheck(PyObject *pyIn, int expectedSize = -1)
{
    return PySequence_Check(pyIn) && sequenceAllOf(pyIn, intCheck)
        && sequenceSizeCheck(pyIn, expectedSize);
}

static PythonToCppFunc sequenceToCppShortArrayCheck(PyObject *pyIn, int dim1, int /* dim2 */)
{
    return intArrayCheck(pyIn, dim1) ? sequenceToCppShortArray : nullptr;
}

static short toUnsignedShort(PyObject *pyIn) { return static_cast<unsigned short>(PyLong_AsUnsignedLong(pyIn)); }

static void sequenceToCppUnsignedShortArray(PyObject *pyIn, void *cppOut)
{
    auto *handle = reinterpret_cast<ArrayHandle<unsigned short> *>(cppOut);
    handle->allocate(PySequence_Size(pyIn));
    convertPySequence(pyIn, toUnsignedShort, handle->data());
}

static PythonToCppFunc sequenceToCppUnsignedShortArrayCheck(PyObject *pyIn, int dim1, int /* dim2 */)
{
    return intArrayCheck(pyIn, dim1) ? sequenceToCppUnsignedShortArray : nullptr;
}

static void sequenceToCppIntArray(PyObject *pyIn, void *cppOut)
{
    auto *handle = reinterpret_cast<ArrayHandle<int> *>(cppOut);
    handle->allocate(PySequence_Size(pyIn));
    convertPySequence(pyIn, PyLong_AsLong, handle->data());
}

static PythonToCppFunc sequenceToCppIntArrayCheck(PyObject *pyIn, int dim1, int /* dim2 */)
{
    return intArrayCheck(pyIn, dim1) ? sequenceToCppIntArray : nullptr;
}

static void sequenceToCppUnsignedArray(PyObject *pyIn, void *cppOut)
{
    auto *handle = reinterpret_cast<ArrayHandle<unsigned> *>(cppOut);
    handle->allocate(PySequence_Size(pyIn));
    convertPySequence(pyIn, PyLong_AsUnsignedLong, handle->data());
}

static PythonToCppFunc sequenceToCppUnsignedArrayCheck(PyObject *pyIn, int dim1, int /* dim2 */)
{
    return intArrayCheck(pyIn, dim1) ? sequenceToCppUnsignedArray : nullptr;
}

static void sequenceToCppLongLongArray(PyObject *pyIn, void *cppOut)
{
    auto *handle = reinterpret_cast<ArrayHandle<long long> *>(cppOut);
    handle->allocate(PySequence_Size(pyIn));
    convertPySequence(pyIn, PyLong_AsLongLong, handle->data());
}

static PythonToCppFunc sequenceToCppLongLongArrayCheck(PyObject *pyIn, int dim1, int /* dim2 */)
{
    return intArrayCheck(pyIn, dim1) ? sequenceToCppLongLongArray : nullptr;
}

static void sequenceToCppUnsignedLongLongArray(PyObject *pyIn, void *cppOut)
{
    auto *handle = reinterpret_cast<ArrayHandle<unsigned long long> *>(cppOut);
    handle->allocate(PySequence_Size(pyIn));
    convertPySequence(pyIn, PyLong_AsUnsignedLongLong, handle->data());
}

static PythonToCppFunc sequenceToCppUnsignedLongLongArrayCheck(PyObject *pyIn, int dim1, int /* dim2 */)
{
    return intArrayCheck(pyIn, dim1) ? sequenceToCppUnsignedLongLongArray : nullptr;
}

// Float

static inline bool floatCheck(PyObject *pyIn) { return PyFloat_Check(pyIn); }

static inline bool floatArrayCheck(PyObject *pyIn, int expectedSize = -1)
{
    return PySequence_Check(pyIn) && sequenceAllOf(pyIn, floatCheck)
        && sequenceSizeCheck(pyIn, expectedSize);
}

static void sequenceToCppDoubleArray(PyObject *pyIn, void *cppOut)
{
    auto *handle = reinterpret_cast<ArrayHandle<double> *>(cppOut);
    handle->allocate(PySequence_Size(pyIn));
    convertPySequence(pyIn, PyFloat_AsDouble, handle->data());
}

static inline float pyToFloat(PyObject *pyIn) { return float(PyFloat_AsDouble(pyIn)); }

static void sequenceToCppFloatArray(PyObject *pyIn, void *cppOut)
{
    auto *handle = reinterpret_cast<ArrayHandle<float> *>(cppOut);
    handle->allocate(PySequence_Size(pyIn));
    convertPySequence(pyIn, pyToFloat, handle->data());
}

static PythonToCppFunc sequenceToCppFloatArrayCheck(PyObject *pyIn, int dim1, int /* dim2 */)
{
    return floatArrayCheck(pyIn, dim1) ? sequenceToCppFloatArray : nullptr;
}

static PythonToCppFunc sequenceToCppDoubleArrayCheck(PyObject *pyIn, int dim1, int /* dim2 */)
{
    return floatArrayCheck(pyIn, dim1) ? sequenceToCppDoubleArray : nullptr;
}

#ifdef HAVE_NUMPY
void initNumPyArrayConverters();
#endif

void initArrayConverters()
{
    SbkArrayConverter **start = &ArrayTypeConverters[0][0];
    std::fill(start, start + sizeof(ArrayTypeConverters) / sizeof(ArrayTypeConverters[0][0]), nullptr);
    // Populate 1-dimensional sequence converters
    ArrayTypeConverters[SBK_DOUBLE_ARRAY_IDX][0] =
        createArrayConverter(sequenceToCppDoubleArrayCheck);
    ArrayTypeConverters[SBK_FLOAT_ARRAY_IDX][0] =
        createArrayConverter(sequenceToCppFloatArrayCheck);
    ArrayTypeConverters[SBK_SHORT_ARRAY_IDX][0] =
        createArrayConverter(sequenceToCppShortArrayCheck);
    ArrayTypeConverters[SBK_UNSIGNEDSHORT_ARRAY_IDX][0] =
        createArrayConverter(sequenceToCppUnsignedShortArrayCheck);
    ArrayTypeConverters[SBK_INT_ARRAY_IDX][0] =
        createArrayConverter(sequenceToCppIntArrayCheck);
    ArrayTypeConverters[SBK_UNSIGNEDINT_ARRAY_IDX][0] =
        createArrayConverter(sequenceToCppUnsignedArrayCheck);
    ArrayTypeConverters[SBK_LONGLONG_ARRAY_IDX][0] =
        createArrayConverter(sequenceToCppLongLongArrayCheck);
    ArrayTypeConverters[SBK_UNSIGNEDLONGLONG_ARRAY_IDX][0] =
        createArrayConverter(sequenceToCppUnsignedLongLongArrayCheck);

#ifdef HAVE_NUMPY
    initNumPyArrayConverters();
#endif
}

SbkArrayConverter *arrayTypeConverter(int index, int dimension)
{
    SbkArrayConverter *c = ArrayTypeConverters[index][dimension - 1];
    return c ? c : unimplementedArrayConverter();
}

// Internal, for usage by numpy
void setArrayTypeConverter(int index, int dimension, SbkArrayConverter *c)
{
    ArrayTypeConverters[index][dimension - 1] = c;
}

} // namespace Conversions
} // namespace Shiboken
